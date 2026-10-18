/* SPDX-License-Identifier: MIT */
/*
 * eRPC server over the existing OpenAMP RPMsg endpoint.
 * One RPMsg message is a slice of an eRPC frame. Frames are small.
 */

#include "erpc_framed_transport.hpp"
#include "erpc_server_setup.h"
#include "erpc_mbf_setup.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

extern "C" {
#include "c_pb2_server.h"
#include "pb2_erpc.h"
}

LOG_MODULE_DECLARE(pb2_m4, LOG_LEVEL_INF);

static K_SEM_DEFINE(rx_sem, 0, 16);
static uint8_t rx_buf[1024];
static size_t rx_len;

extern "C" void pb2_erpc_push(const void *data, size_t len)
{
	const uint8_t *src = static_cast<const uint8_t *>(data);
	unsigned int key = irq_lock();
	size_t space = sizeof(rx_buf) - rx_len;
	size_t dropped = 0;

	/* Frames are far smaller than rx_buf. Dropping bytes breaks eRPC
	 * framing, so say so instead of failing silently.
	 */
	if (len > space) {
		dropped = len - space;
		len = space;
	}
	memcpy(rx_buf + rx_len, src, len);
	rx_len += len;
	irq_unlock(key);
	if (dropped) {
		LOG_ERR("rpmsg rx buffer full, dropped %u bytes", (unsigned int)dropped);
	}
	k_sem_give(&rx_sem);
}

class RpmsgTransport : public erpc::FramedTransport {
protected:
	erpc_status_t underlyingSend(const uint8_t *data, uint32_t size) override
	{
		while (size) {
			uint32_t n = size > 480u ? 480u : size;

			if (pb2_rpmsg_tx(data, n) != 0) {
				return kErpcStatus_SendFailed;
			}
			data += n;
			size -= n;
		}
		return kErpcStatus_Success;
	}

	erpc_status_t underlyingReceive(uint8_t *data, uint32_t size) override
	{
		uint32_t got = 0;

		while (got < size) {
			unsigned int key = irq_lock();
			size_t n = rx_len;

			if (n > size - got) {
				n = size - got;
			}
			if (n) {
				memcpy(data + got, rx_buf, n);
				memmove(rx_buf, rx_buf + n, rx_len - n);
				rx_len -= n;
				got += static_cast<uint32_t>(n);
			}
			irq_unlock(key);
			if (got >= size) {
				break;
			}
			k_sem_take(&rx_sem, K_FOREVER);
		}
		return kErpcStatus_Success;
	}
};

extern "C" void pb2_erpc_server(void)
{
	RpmsgTransport transport;
	erpc_mbf_t mbf = erpc_mbf_dynamic_init();
	erpc_server_t server = erpc_server_init(reinterpret_cast<erpc_transport_t>(&transport), mbf);
	erpc_service_t service = create_Pb2Service_service();

	if (!server || !service) {
		LOG_ERR("eRPC server init failed");
		return;
	}
	erpc_add_service_to_server(server, service);
	LOG_INF("eRPC server waiting on rpmsg");
	while (1) {
		erpc_status_t status = erpc_server_poll(server);

		if (status != kErpcStatus_Success) {
			LOG_ERR("eRPC poll status %d", (int)status);
		}
	}
}
