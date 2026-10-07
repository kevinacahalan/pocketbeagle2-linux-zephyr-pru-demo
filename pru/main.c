/*
 * PRU firmware for the PocketBeagle 2 hello-world.
 *
 * PRU0 is the only RPMsg core. It takes commands from Linux and from the
 * Zephyr mailbox, and arms PRU1 through the shared-RAM block.
 * PRU1 does not use RPMsg. Its loop period is a cycle delay latched at the
 * start of each iteration, not one step per message.
 *
 * Build twice: -DPRU_CORE=0 and -DPRU_CORE=1.
 */

#include <stdint.h>
#include "pb2_shm.h"

#if PRU_CORE == 0

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
					n = append_str(tx, 0, sizeof(tx), "pru0: xy seq ");
					n = append_u32(tx, n, sizeof(tx), xy_seq);
					n = append_str(tx, n, sizeof(tx), " x ");
					n = append_u32(tx, n, sizeof(tx), x);
					n = append_str(tx, n, sizeof(tx), " y ");
					n = append_u32(tx, n, sizeof(tx), y);
					send_bytes(&transport, dst, src, tx, n);
				} else if (parse_arm(cmd, &period, &count)) {
					arm_pru1(arm, &arm_seq, period, count);
					n = append_str(tx, 0, sizeof(tx), "pru0: linux arm ");
					n = append_u32(tx, n, sizeof(tx), period);
					n = append_str(tx, n, sizeof(tx), " ");
					n = append_u32(tx, n, sizeof(tx), count);
					send_bytes(&transport, dst, src, tx, n);
				} else {
					n = append_str(tx, 0, sizeof(tx), "pru0: ");
					n = append_str(tx, n, sizeof(tx), cmd);
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
			} else if (parse_arm(cmd, &period, &count)) {
				arm_pru1(arm, &arm_seq, period, count);
			}
			if (peer_known) {
				uint16_t n = 0;

				n = append_str(tx, n, sizeof(tx), "pru0: shm ");
				n = append_u32(tx, n, sizeof(tx), seq);
				n = append_str(tx, n, sizeof(tx), " ");
				n = append_str(tx, n, sizeof(tx), cmd);
				send_bytes(&transport, dst, src, tx, n);
			}
		}
	}
}

#elif PRU_CORE == 1

#include <rsc_types.h>

struct pb2_empty_rsc {
	uint32_t ver;
	uint32_t num;
	uint32_t reserved[2];
};

#pragma DATA_SECTION(resourceTable, ".resource_table")
#pragma RETAIN(resourceTable)
struct pb2_empty_rsc resourceTable = { 1, 0, { 0, 0 } };

volatile register uint32_t __R30;

static void delay_cycles(uint32_t cycles)
{
	while (cycles >= 1000u) {
		__delay_cycles(1000);
		cycles -= 1000u;
	}
}

static void uart_tx_byte(uint8_t byte)
{
	uint32_t bit;

	__R30 &= ~(1u << PB2_XY_TX_BIT);
	__delay_cycles(PB2_UART_BIT_CYCLES);
	for (bit = 0; bit < 8u; bit++) {
		if (byte & 1u) {
			__R30 |= (1u << PB2_XY_TX_BIT);
		} else {
			__R30 &= ~(1u << PB2_XY_TX_BIT);
		}
		byte = (uint8_t)(byte >> 1);
		__delay_cycles(PB2_UART_BIT_CYCLES);
	}
	__R30 |= (1u << PB2_XY_TX_BIT);
	__delay_cycles(PB2_UART_BIT_CYCLES);
}

static uint16_t slip_push(uint8_t *dst, uint16_t n, uint8_t byte)
{
	if (byte == PB2_SLIP_END) {
		dst[n++] = PB2_SLIP_ESC;
		dst[n++] = PB2_SLIP_ESC_END;
	} else if (byte == PB2_SLIP_ESC) {
		dst[n++] = PB2_SLIP_ESC;
		dst[n++] = PB2_SLIP_ESC_ESC;
	} else {
		dst[n++] = byte;
	}
	return n;
}

/* SLIP frame, then each byte as UART 8N1, LSB first. */
static void transmit_xy(uint32_t x, uint32_t y)
{
	uint8_t raw[8];
	uint8_t frame[22];
	uint16_t n = 0;
	uint16_t i;
	uint8_t sum = 0;

	for (i = 0; i < 4u; i++) {
		raw[i] = (uint8_t)(x >> (8u * i));
		raw[4u + i] = (uint8_t)(y >> (8u * i));
	}
	for (i = 0; i < 8u; i++) {
		sum = (uint8_t)(sum ^ raw[i]);
	}
	frame[n++] = PB2_SLIP_END;
	for (i = 0; i < 8u; i++) {
		n = slip_push(frame, n, raw[i]);
	}
	n = slip_push(frame, n, sum);
	frame[n++] = PB2_SLIP_END;
	for (i = 0; i < n; i++) {
		uart_tx_byte(frame[i]);
	}
}

void main(void)
{
	volatile struct pb2_arm *arm = (volatile struct pb2_arm *)PB2_ARM_LOCAL;
	volatile struct pb2_xy *xy = (volatile struct pb2_xy *)PB2_XY_LOCAL;
	uint32_t seen;
	uint32_t xy_seen;
	uint32_t period = 100000u;
	uint32_t left = 0;
	uint32_t tick = 0;
	uint32_t done = 0;

	__R30 |= (1u << PB2_XY_TX_BIT);

	/* Adopt the seq already in RAM so a stale arm is not a new command. */
	seen = arm->seq;
	xy_seen = xy->seq;

	while (1) {
		uint32_t step = period;

		delay_cycles(step);
		tick++;

		if (arm->magic == PB2_ARM_MAGIC) {
			uint32_t seq = arm->seq;

			if (seq != seen) {
				uint32_t next = arm->period_cycles;
				uint32_t count = arm->periods;

				if (arm->seq == seq && arm->magic == PB2_ARM_MAGIC) {
					if (next < PB2_ARM_PERIOD_MIN) {
						next = PB2_ARM_PERIOD_MIN;
					}
					if (next > PB2_ARM_PERIOD_MAX) {
						next = PB2_ARM_PERIOD_MAX;
					}
					seen = seq;
					period = next;
					left = count;
					done = 0;
				}
			}
		}

		if (left) {
			done++;
			left--;
		}

		if (xy->magic == PB2_XY_MAGIC) {
			uint32_t seq = xy->seq;

			if (seq != xy_seen) {
				uint32_t x = xy->x;
				uint32_t y = xy->y;

				if (xy->seq == seq && xy->magic == PB2_XY_MAGIC) {
					xy_seen = seq;
					transmit_xy(x, y);
					xy->consumed = seq;
				}
			}
		}

		arm->tick = tick;
		arm->done = done;
		arm->applied_seq = seen;
		arm->applied_period = period;
	}
}

#else
#error "PRU_CORE must be 0 or 1"
#endif
