/*
 * PRU1 firmware. This core sends the SLIP frame.
 *
 * It reads the X/Y record PRU0 published in PRUSS shared RAM. The SLIP frame
 * and the UART bit timing live in slip_tx.asm. This file is the sample loop.
 * It does not use RPMsg. Its loop period is a cycle delay latched at the
 * start of each iteration, not one step per message.
 */

#include <stdint.h>
#include "pb2_shm.h"
#include <rsc_types.h>

extern void transmit_xy(uint32_t x, uint32_t y);

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

