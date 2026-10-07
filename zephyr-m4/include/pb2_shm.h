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

/* PRU subsystem shared RAM. Local view for both PRUs, CPU view for Linux. */
#define PB2_ARM_LOCAL  0x00010000u
#define PB2_ARM_CPU    0x30050000u
#define PB2_ARM_MAGIC  0x50423241u /* 'PB2A' */

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
