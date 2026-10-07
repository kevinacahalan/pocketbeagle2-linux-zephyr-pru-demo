/*
 * Functions Linux calls through Zephyr eRPC.
 * arm_pru1 asks PRU0, via the small mailbox, to arm PRU1.
 * take_chunk checks a bulk buffer Linux already wrote.
 */

#include <zephyr/kernel.h>
#include <zephyr/cache.h>
#include <zephyr/logging/log.h>

#include <stdio.h>
#include <string.h>

#include "pb2_erpc.h"
#include "pb2_shm.h"

LOG_MODULE_DECLARE(pb2_m4, LOG_LEVEL_INF);

static volatile struct pb2_bulk *const bulk =
	(volatile struct pb2_bulk *)PB2_BULK_ADDR;

char *say(const char *msg)
{
	size_t n;
	char *out;

	if (!msg) {
		msg = "";
	}
	n = strlen(msg);
	if (n > 160) {
		n = 160;
	}
	out = k_malloc(n + 16);
	if (!out) {
		return NULL;
	}
	snprintf(out, n + 16, "zephyr: %.*s", (int)n, msg);
	LOG_INF("erpc say: %s", out);
	return out;
}

int32_t armPru1(uint32_t period_cycles, uint32_t periods)
{
	char cmd[56];

	snprintf(cmd, sizeof(cmd), "arm %u %u", period_cycles, periods);
	pb2_shm_publish(cmd);
	LOG_INF("erpc arm pru1 period %u count %u", period_cycles, periods);
	return 0;
}

int32_t sendXy(uint32_t x, uint32_t y)
{
	char cmd[56];

	snprintf(cmd, sizeof(cmd), "xy %u %u", x, y);
	pb2_shm_publish(cmd);
	LOG_INF("erpc xy x %u y %u", x, y);
	return 0;
}

uint32_t takeChunk(uint32_t seq, uint32_t length, uint32_t crc)
{
	uint32_t got;
	uint32_t magic;
	uint32_t got_seq;
	uint32_t got_len;
	uint32_t got_crc;

	if (length > sizeof(bulk->data)) {
		return 0;
	}
	sys_cache_data_invd_range((void *)bulk, 16u + length);
	magic = bulk->magic;
	got_seq = bulk->seq;
	got_len = bulk->length;
	got_crc = bulk->crc;
	if (magic != PB2_BULK_MAGIC || got_seq != seq || got_len != length ||
	    got_crc != crc) {
		LOG_INF("chunk mismatch magic %08x seq %u len %u crc %08x",
			magic, got_seq, got_len, got_crc);
		return 0;
	}
	got = pb2_crc32((const uint8_t *)bulk->data, length);
	LOG_INF("chunk seq %u len %u crc %08x", seq, length, got);
	return got;
}
