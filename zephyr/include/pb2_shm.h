/* SPDX-License-Identifier: MIT */
/*
 * Shared layout for the PocketBeagle 2 hello-world.
 *
 * Small mailbox: Zephyr writes, PRU0 polls. Not on PRU1's timing path.
 * Bulk region: Linux writes chunks, Zephyr reads them. Separate page range.
 * Arm block: PRU0 writes it in PRU shared RAM. PRU1 samples it once per period.
 *
 * Do not put a loadable ELF section on the DDR addresses. The Zephyr overlay
 * and these macros must stay on the same addresses.
 */
#ifndef PB2_SHM_H_
#define PB2_SHM_H_

#include <stdint.h>

#define PB2_SHM_ADDR   0x9D9FF000u
#define PB2_SHM_MAGIC  0x50423253u /* 'PB2S' */

#define PB2_BULK_ADDR  0x9D9EF000u
#define PB2_BULK_SIZE  0x00010000u
#define PB2_BULK_MAGIC 0x50423242u /* 'PB2B' */

/*
 * PRU subsystem shared RAM. Both cores see the same block.
 * The AM62x linker map (PRU_SHAREDMEM) puts 32 KiB at local 0x00010000
 * on C28. C28's pointer is the address shifted right by 8, so 0x0100
 * would select that base. The firmware does not write C28. It uses the
 * same direct local address as the arm block, which already works.
 * The SoC address of local 0x00010000 is 0x30050000 (PRUSS at 0x30040000).
 * The arm block uses the first 32 bytes. The X/Y record starts at
 * local 0x00010040, still inside the first 256 bytes and not in DDR.
 */
#define PB2_ARM_LOCAL  0x00010000u
#define PB2_ARM_CPU    0x30050000u
#define PB2_ARM_MAGIC  0x50423241u /* 'PB2A' */

#define PB2_XY_LOCAL   0x00010040u
#define PB2_XY_CPU     0x30050040u
#define PB2_XY_MAGIC   0x50423258u /* 'PB2X' */

/* SLIP (RFC 1055) framing of the X/Y payload. */
#define PB2_SLIP_END      0xC0u
#define PB2_SLIP_ESC      0xDBu
#define PB2_SLIP_ESC_END  0xDCu
#define PB2_SLIP_ESC_ESC  0xDDu

/*
 * PRU1 shifts each SLIP byte out as UART 8N1 on R30 bit 5.
 * Header P1.20, ball Y24, pad offset 0x00cc, mux mode 2 (PR0_PRU1_GPO5).
 * On the BeagleBoard Debian image the PRUSS core clock is 250 MHz.
 * 250000000 / 115200 is 2170 cycles per bit (about 115207 baud).
 */
#define PB2_XY_TX_BIT         5u
#define PB2_PRU_HZ            250000000u
#define PB2_UART_BAUD         115200u
#define PB2_UART_BIT_CYCLES   2170u

#define PB2_ARM_PERIOD_MIN 20000u
#define PB2_ARM_PERIOD_MAX 5000000u

struct pb2_shm {
	uint32_t magic;
	uint32_t seq;
	char cmd[56];
};

struct pb2_bulk {
	uint32_t magic;
	uint32_t seq;
	uint32_t length;
	uint32_t crc;
	uint8_t data[PB2_BULK_SIZE - 16u];
};

struct pb2_arm {
	uint32_t magic;
	uint32_t seq;
	uint32_t period_cycles;
	uint32_t periods;
	uint32_t tick;
	uint32_t done;
	uint32_t applied_seq;
	uint32_t applied_period;
};

/* PRU0 fills x, y, magic, then seq. PRU1 writes consumed after the frame. */
struct pb2_xy {
	uint32_t magic;
	uint32_t seq;
	uint32_t x;
	uint32_t y;
	uint32_t consumed;
};

#ifndef PRU_CORE
static inline uint32_t pb2_crc32(const uint8_t *data, uint32_t len)
{
	uint32_t crc = 0xffffffffu;
	uint32_t i, b;

	for (i = 0; i < len; i++) {
		crc ^= data[i];
		for (b = 0; b < 8u; b++) {
			uint32_t mask = -(crc & 1u);

			crc = (crc >> 1) ^ (0xedb88320u & mask);
		}
	}
	return ~crc;
}
#endif

#endif
