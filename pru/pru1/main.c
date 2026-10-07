/*
 * PRU1 firmware. This core sends the SLIP frame.
 *
 * It reads the X/Y record PRU0 published in PRUSS shared RAM and shifts it
 * out as UART 8N1 on R30 bit 5. It does not use RPMsg. Its loop period is a
 * cycle delay latched at the start of each iteration, not one step per message.
 */

#include <stdint.h>
#include "pb2_shm.h"
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

