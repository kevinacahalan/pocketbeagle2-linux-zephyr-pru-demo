/* SPDX-License-Identifier: MIT */
/*
 * Zephyr on the PocketBeagle 2 M4.
 *
 * Linux calls Zephyr functions over eRPC on RPMsg (channel "rpmsg-raw",
 * address 1024). armPru1 publishes a command in the DDR mailbox at
 * PB2_SHM_ADDR. PRU0 polls that mailbox and arms PRU1.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/ipm.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/barrier.h>
#include <zephyr/cache.h>
#include <zephyr/logging/log.h>

#include <errno.h>

#include <openamp/open_amp.h>
#include <metal/sys.h>
#include <metal/io.h>
#include <resource_table.h>
#include <addr_translation.h>

#include "pb2_shm.h"
#include "pb2_erpc.h"

LOG_MODULE_REGISTER(pb2_m4, LOG_LEVEL_INF);

#define RPMSG_CHAN_NAME "rpmsg-raw"
#define RPMSG_CHAN_ADDR 1024

#if CONFIG_IPM_MAX_DATA_SIZE > 0
#define IPM_SEND(dev, w, id, d, s) ipm_send(dev, w, id, d, s)
#else
#define IPM_SEND(dev, w, id, d, s) ipm_send(dev, w, id, NULL, 0)
#endif

#define SHM_NODE       DT_CHOSEN(zephyr_ipc_shm)
#define SHM_START_ADDR DT_REG_ADDR(SHM_NODE)
#define SHM_SIZE       DT_REG_SIZE(SHM_NODE)

#if !DT_HAS_CHOSEN(zephyr_ipc_shm)
#error "M4 board must choose zephyr,ipc_shm"
#endif

static const struct device *const ipm_handle = DEVICE_DT_GET(DT_CHOSEN(zephyr_ipc));

static metal_phys_addr_t shm_physmap = SHM_START_ADDR;
static metal_phys_addr_t rsc_tab_physmap;
static struct metal_io_region shm_io_data;
static struct metal_io_region rsc_io_data;
static struct metal_io_region *shm_io = &shm_io_data;
static struct metal_io_region *rsc_io = &rsc_io_data;
static struct rpmsg_virtio_device rvdev;
static struct rpmsg_device *rpdev;
static struct rpmsg_endpoint ept;
static void *rsc_table;

static K_SEM_DEFINE(mb_sem, 0, 1);

static uint32_t shm_seq;

static volatile struct pb2_shm *const pru_shm =
	(volatile struct pb2_shm *)PB2_SHM_ADDR;

void pb2_shm_publish(const char *cmd)
{
	size_t n = strlen(cmd);

	if (n > sizeof(pru_shm->cmd) - 1) {
		n = sizeof(pru_shm->cmd) - 1;
	}
	memcpy((void *)pru_shm->cmd, cmd, n);
	((volatile char *)pru_shm->cmd)[n] = '\0';
	pru_shm->magic = PB2_SHM_MAGIC;
	barrier_dmem_fence_full();
	pru_shm->seq = ++shm_seq;
	barrier_dmem_fence_full();
	sys_cache_data_flush_range((void *)pru_shm, sizeof(*pru_shm));
	LOG_INF("shm seq %u cmd %s", shm_seq, cmd);
}

static void platform_ipm_callback(const struct device *dev, void *context,
				  uint32_t id, volatile void *data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(context);
	ARG_UNUSED(id);
	ARG_UNUSED(data);
	k_sem_give(&mb_sem);
}

static int mailbox_notify(void *priv, uint32_t id)
{
	ARG_UNUSED(priv);
	IPM_SEND(ipm_handle, 0, id, &id, 4);
	return 0;
}

static int platform_init(void)
{
	struct metal_init_params metal_params = METAL_INIT_DEFAULTS;
	int rsc_size;
	int status;

	status = metal_init(&metal_params);
	if (status) {
		LOG_ERR("metal_init failed: %d", status);
		return -1;
	}

	metal_io_init(shm_io, (void *)SHM_START_ADDR, &shm_physmap, SHM_SIZE,
		      -1, 0, addr_translation_get_ops(shm_physmap));

	rsc_table_get(&rsc_table, &rsc_size);
	rsc_tab_physmap = (uintptr_t)rsc_table;
	metal_io_init(rsc_io, rsc_table, &rsc_tab_physmap, rsc_size, -1, 0, NULL);

	if (!device_is_ready(ipm_handle)) {
		LOG_ERR("IPM device is not ready");
		return -1;
	}
	ipm_register_callback(ipm_handle, platform_ipm_callback, NULL);
	status = ipm_set_enabled(ipm_handle, 1);
	if (status) {
		LOG_ERR("ipm_set_enabled failed");
		return -1;
	}
	return 0;
}

static struct rpmsg_device *platform_create_rpmsg_vdev(void)
{
	struct fw_rsc_vdev_vring *vring_rsc;
	struct virtio_device *vdev;
	int ret;

	vdev = rproc_virtio_create_vdev(VIRTIO_DEV_DEVICE, VDEV_ID,
					rsc_table_to_vdev(rsc_table),
					rsc_io, NULL, mailbox_notify, NULL);
	if (!vdev) {
		LOG_ERR("failed to create vdev");
		return NULL;
	}

	rproc_virtio_wait_remote_ready(vdev);

	vring_rsc = rsc_table_get_vring0(rsc_table);
	ret = rproc_virtio_init_vring(vdev, 0, vring_rsc->notifyid,
				      (void *)vring_rsc->da, rsc_io,
				      vring_rsc->num, vring_rsc->align);
	if (ret) {
		LOG_ERR("failed to init vring 0");
		goto failed;
	}

	vring_rsc = rsc_table_get_vring1(rsc_table);
	ret = rproc_virtio_init_vring(vdev, 1, vring_rsc->notifyid,
				      (void *)vring_rsc->da, rsc_io,
				      vring_rsc->num, vring_rsc->align);
	if (ret) {
		LOG_ERR("failed to init vring 1");
		goto failed;
	}

	ret = rpmsg_init_vdev(&rvdev, vdev, NULL, shm_io, NULL);
	if (ret) {
		LOG_ERR("failed rpmsg_init_vdev");
		goto failed;
	}
	return rpmsg_virtio_get_rpmsg_device(&rvdev);

failed:
	rproc_virtio_remove_vdev(vdev);
	return NULL;
}

static int rx_cb(struct rpmsg_endpoint *ept_dev, void *data, size_t len,
		 uint32_t src, void *priv)
{
	ARG_UNUSED(ept_dev);
	ARG_UNUSED(src);
	ARG_UNUSED(priv);

	pb2_erpc_push(data, len);
	return RPMSG_SUCCESS;
}

int pb2_rpmsg_tx(const void *data, size_t len)
{
	int ret = rpmsg_send(&ept, (void *)data, len);

	return ret < 0 ? -1 : 0;
}

static void app_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	pb2_erpc_server();
}

static void rpmsg_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	if (platform_init()) {
		return;
	}
	rpdev = platform_create_rpmsg_vdev();
	if (!rpdev) {
		return;
	}

	if (rpmsg_create_ept(&ept, rpdev, RPMSG_CHAN_NAME, RPMSG_CHAN_ADDR,
			     RPMSG_ADDR_ANY, rx_cb, NULL)) {
		LOG_ERR("rpmsg_create_ept failed");
		return;
	}
	LOG_INF("rpmsg channel %s addr %d is up", RPMSG_CHAN_NAME, RPMSG_CHAN_ADDR);

	while (1) {
		k_sem_take(&mb_sem, K_FOREVER);
		rproc_virtio_notified(rvdev.vdev, VRING1_ID);
	}
}

K_THREAD_DEFINE(rpmsg_tid, 4096, rpmsg_thread, NULL, NULL, NULL, K_PRIO_COOP(8), 0, 0);
K_THREAD_DEFINE(app_tid, 8192, app_thread, NULL, NULL, NULL, K_PRIO_COOP(7), 0, 0);

int pb2_spi2_write(const void *buf, size_t len)
{
	static const struct spi_config cfg = {
		.frequency = 1000000,
		.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
		.peripheral = 0,
	};
	const struct device *spi = DEVICE_DT_GET(DT_NODELABEL(main_spi2));
	const struct spi_buf txb = {
		.buf = (void *)buf,
		.len = len,
	};
	const struct spi_buf_set tx = {
		.buffers = &txb,
		.count = 1,
	};

	if (!buf || !len) {
		return -EINVAL;
	}
	if (!device_is_ready(spi)) {
		LOG_ERR("spi2 not ready");
		return -ENODEV;
	}
	return spi_write(spi, &cfg, &tx);
}

static void pb2_spi_hello(void)
{
	static const uint8_t hello[] = {'h', 'e', 'l', 'l', 'o'};
	int ret = pb2_spi2_write(hello, sizeof(hello));
	char msg[56];

	if (ret == -ENODEV) {
		pb2_shm_publish("spi hello not ready");
		return;
	}
	if (ret) {
		LOG_ERR("spi2 hello failed: %d", ret);
		snprintk(msg, sizeof(msg), "spi hello fail %d", ret);
		pb2_shm_publish(msg);
		return;
	}
	LOG_INF("spi2 hello wrote %u bytes", (unsigned int)sizeof(hello));
	pb2_shm_publish("spi hello ok");
}

int main(void)
{
	pb2_shm_publish("hello from zephyr");
	LOG_INF("pb2 m4 hello, shm seq %u", shm_seq);
	pb2_spi_hello();
	return 0;
}
