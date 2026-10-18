/* SPDX-License-Identifier: MIT */
/*
 * PRU0 firmware. This core is the transfer core.
 *
 * Linux and Zephyr hand it a 32-bit X and a 32-bit Y on the existing RPMsg
 * path. It writes that pair into PRUSS shared RAM for PRU1, and it arms
 * PRU1 through the same shared RAM. It does not send the SLIP frame.
 */

#include <stdint.h>
#include "pb2_shm.h"

#include <pru_intc.h>
#include <rsc_types.h>
#include <pru_rpmsg.h>
#include "resource_table.h"
#include "intc_map_0.h"

#define HOST_INT      ((uint32_t)1 << 30)
#define TO_ARM_HOST   16
#define FROM_ARM_HOST 17
#define CHAN_PORT     30
#define CHAN_NAME     "rpmsg-raw"
#define VIRTIO_CONFIG_S_DRIVER_OK 4

volatile register uint32_t __R31;

static uint8_t payload[RPMSG_MESSAGE_SIZE];
/*
 * tx and cmd stay in .bss. A 496-byte local on the downward-growing stack
 * left pru_rpmsg_channel() with no room, so the name-service announce never
 * finished.
 */
static char tx[RPMSG_MESSAGE_SIZE];
static char cmd[56];

static uint16_t append_str(char *dst, uint16_t n, uint16_t max, const char *s)
{
	while (*s && n + 1 < max) {
		dst[n++] = *s++;
	}
	dst[n] = 0;
	return n;
}

static uint16_t append_u32(char *dst, uint16_t n, uint16_t max, uint32_t v)
{
	char tmp[10];
	uint16_t i = 0;

	if (v == 0) {
		return append_str(dst, n, max, "0");
	}
	while (v && i < sizeof(tmp)) {
		tmp[i++] = (char)('0' + (v % 10));
		v /= 10;
	}
	while (i && n + 1 < max) {
		dst[n++] = tmp[--i];
	}
	dst[n] = 0;
	return n;
}

static int parse_u32(const char **pp, uint32_t *out)
{
	const char *s = *pp;
	uint32_t v = 0;
	int any = 0;

	while (*s >= '0' && *s <= '9') {
		v = v * 10u + (uint32_t)(*s - '0');
		s++;
		any = 1;
	}
	if (!any) {
		return 0;
	}
	*out = v;
	*pp = s;
	return 1;
}

/* "arm <period> <count>" */
static int parse_arm(const char *s, uint32_t *period, uint32_t *count)
{
	if (s[0] != 'a' || s[1] != 'r' || s[2] != 'm' || s[3] != ' ') {
		return 0;
	}
	s += 4;
	if (!parse_u32(&s, period) || *s != ' ') {
		return 0;
	}
	s++;
	if (!parse_u32(&s, count)) {
		return 0;
	}
	return 1;
}

/* "xy <x> <y>", both decimal uint32. */
static int parse_xy(const char *s, uint32_t *x, uint32_t *y)
{
	if (s[0] != 'x' || s[1] != 'y' || s[2] != ' ') {
		return 0;
	}
	s += 3;
	if (!parse_u32(&s, x) || *s != ' ') {
		return 0;
	}
	s++;
	if (!parse_u32(&s, y)) {
		return 0;
	}
	return 1;
}

static void publish_xy(volatile struct pb2_xy *xy, uint32_t *xy_seq,
		       uint32_t x, uint32_t y)
{
	xy->x = x;
	xy->y = y;
	xy->magic = PB2_XY_MAGIC;
	*xy_seq += 1u;
	xy->seq = *xy_seq;
}

/* Who asked, and what PRU0 stored for PRU1. No printf on this core. */
static uint16_t reply_xy(char *dst, uint16_t max, const char *who,
			 uint32_t seq, uint32_t x, uint32_t y)
{
	uint16_t n = 0;

	n = append_str(dst, n, max, who);
	n = append_str(dst, n, max, " asked PRU0 to publish X/Y, and PRU0 wrote x ");
	n = append_u32(dst, n, max, x);
	n = append_str(dst, n, max, " y ");
	n = append_u32(dst, n, max, y);
	n = append_str(dst, n, max, " seq ");
	n = append_u32(dst, n, max, seq);
	n = append_str(dst, n, max, " for PRU1");
	return n;
}

static uint16_t reply_arm(char *dst, uint16_t max, const char *who,
			  uint32_t period, uint32_t count)
{
	uint16_t n = 0;

	n = append_str(dst, n, max, who);
	n = append_str(dst, n, max, " asked PRU0 to arm PRU1, and PRU0 wrote period ");
	n = append_u32(dst, n, max, period);
	n = append_str(dst, n, max, " count ");
	n = append_u32(dst, n, max, count);
	n = append_str(dst, n, max, " for PRU1");
	return n;
}

static uint16_t reply_echo(char *dst, uint16_t max, const char *who, const char *text)
{
	uint16_t n = 0;

	n = append_str(dst, n, max, who);
	n = append_str(dst, n, max, " sent \"");
	n = append_str(dst, n, max, text);
	n = append_str(dst, n, max, "\" to PRU0, and PRU0 echoed it");
	return n;
}

static void arm_pru1(volatile struct pb2_arm *arm, uint32_t *arm_seq,
		     uint32_t period, uint32_t count)
{
	if (period < PB2_ARM_PERIOD_MIN) {
		period = PB2_ARM_PERIOD_MIN;
	}
	if (period > PB2_ARM_PERIOD_MAX) {
		period = PB2_ARM_PERIOD_MAX;
	}
	arm->period_cycles = period;
	arm->periods = count;
	arm->magic = PB2_ARM_MAGIC;
	*arm_seq += 1u;
	arm->seq = *arm_seq;
}

static int16_t send_bytes(struct pru_rpmsg_transport *transport,
			  uint16_t src, uint16_t dst, const char *msg, uint16_t len)
{
	if (len > RPMSG_MESSAGE_SIZE) {
		len = RPMSG_MESSAGE_SIZE;
	}
	return pru_rpmsg_send(transport, src, dst, (void *)msg, len);
}

void main(void)
{
	struct pru_rpmsg_transport transport;
	volatile struct pb2_shm *shm = (volatile struct pb2_shm *)PB2_SHM_ADDR;
	volatile struct pb2_arm *arm = (volatile struct pb2_arm *)PB2_ARM_LOCAL;
	volatile struct pb2_xy *xy = (volatile struct pb2_xy *)PB2_XY_LOCAL;
	volatile uint8_t *status;
	uint16_t src = 0;
	uint16_t dst = 0;
	uint16_t len;
	uint32_t last_seq = 0;
	uint32_t arm_seq = 0;
	uint32_t xy_seq = 0;
	uint16_t peer_known = 0;

	CT_INTC.STATUS_CLR_INDEX_REG_bit.STATUS_CLR_INDEX = FROM_ARM_HOST;
	status = &resourceTable.rpmsg_vdev.status;
	while (!(*status & VIRTIO_CONFIG_S_DRIVER_OK)) {
	}

	pru_rpmsg_init(&transport, &resourceTable.rpmsg_vring0,
		       &resourceTable.rpmsg_vring1, TO_ARM_HOST, FROM_ARM_HOST);
	while (pru_rpmsg_channel(RPMSG_NS_CREATE, &transport, CHAN_NAME,
				 CHAN_PORT) != PRU_RPMSG_SUCCESS) {
	}

	/* After the announce, so a shared-RAM store cannot block the channel. */
	arm->magic = 0;
	arm->seq = 0;
	xy->x = 0;
	xy->y = 0;
	xy->consumed = 0;
	xy->magic = 0;
	xy->seq = 0;

	while (1) {
		if (__R31 & HOST_INT) {
			CT_INTC.STATUS_CLR_INDEX_REG_bit.STATUS_CLR_INDEX = FROM_ARM_HOST;
			while (pru_rpmsg_receive(&transport, &src, &dst, payload, &len) ==
			       PRU_RPMSG_SUCCESS) {
				uint16_t n = 0;
				uint16_t i;
				uint16_t copy = len;
				uint32_t period, count;
				uint32_t x, y;

				peer_known = 1;
				while (copy && (payload[copy - 1] == '\n' ||
						payload[copy - 1] == '\r')) {
					copy--;
				}
				for (i = 0; i < copy && i + 1 < sizeof(cmd); i++) {
					cmd[i] = (char)payload[i];
				}
				cmd[i] = 0;
				if (parse_xy(cmd, &x, &y)) {
					publish_xy(xy, &xy_seq, x, y);
					n = reply_xy(tx, sizeof(tx), "Linux", xy_seq, x, y);
					send_bytes(&transport, dst, src, tx, n);
				} else if (parse_arm(cmd, &period, &count)) {
					arm_pru1(arm, &arm_seq, period, count);
					n = reply_arm(tx, sizeof(tx), "Linux", period, count);
					send_bytes(&transport, dst, src, tx, n);
				} else {
					n = reply_echo(tx, sizeof(tx), "Linux", cmd);
					send_bytes(&transport, dst, src, tx, n);
				}
			}
		}

		if (shm->magic == PB2_SHM_MAGIC && shm->seq != last_seq) {
			uint32_t seq = shm->seq;
			uint16_t i;
			uint32_t period, count;
			uint32_t x, y;

			if (seq != shm->seq || shm->magic != PB2_SHM_MAGIC) {
				continue;
			}
			last_seq = seq;
			for (i = 0; i < sizeof(cmd) - 1 && shm->cmd[i]; i++) {
				cmd[i] = shm->cmd[i];
			}
			cmd[i] = 0;
			if (parse_xy(cmd, &x, &y)) {
				publish_xy(xy, &xy_seq, x, y);
				if (peer_known) {
					uint16_t n = reply_xy(tx, sizeof(tx), "Zephyr",
							      xy_seq, x, y);

					send_bytes(&transport, dst, src, tx, n);
				}
			} else if (parse_arm(cmd, &period, &count)) {
				arm_pru1(arm, &arm_seq, period, count);
				if (peer_known) {
					uint16_t n = reply_arm(tx, sizeof(tx), "Zephyr",
							       period, count);

					send_bytes(&transport, dst, src, tx, n);
				}
			} else if (peer_known) {
				uint16_t n = reply_echo(tx, sizeof(tx), "Zephyr", cmd);

				send_bytes(&transport, dst, src, tx, n);
			}
		}
	}
}

