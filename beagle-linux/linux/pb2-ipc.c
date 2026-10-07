#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "pb2_erpc_linux.h"
#include "pb2_shm.h"

/*
 * pb2-ipc peer <label> <rpmsg-dev> <fifo>
 *   Owns one RPMsg device. Used for PRU0.
 *
 * pb2-ipc erpc <rpmsg-dev> <fifo>
 *   Calls Zephyr functions over eRPC on the existing RPMsg fd.
 *
 * pb2-ipc watch
 *   Prints PRU1's shared-RAM status. PRU1 has no RPMsg endpoint.
 *
 * pb2-ipc linux <dir>
 *   Sends the hello-world script, then reads commands.
 */

static int write_all(int fd, const void *buf, size_t len)
{
	const char *p = buf;

	while (len) {
		ssize_t n = write(fd, p, len);

		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			return -1;
		}
		p += n;
		len -= (size_t)n;
	}
	return 0;
}

static int peer_main(int argc, char **argv)
{
	const char *label = argv[2];
	const char *devpath = argv[3];
	const char *fifopath = argv[4];
	int dev, fifo;
	char rx[512];
	char line[512];
	size_t fill = 0;

	if (argc != 5) {
		fprintf(stderr, "usage: %s peer <label> <dev> <fifo>\n", argv[0]);
		return 2;
	}

	dev = open(devpath, O_RDWR);
	if (dev < 0) {
		perror(devpath);
		return 1;
	}
	fifo = open(fifopath, O_RDONLY);
	if (fifo < 0) {
		perror(fifopath);
		return 1;
	}

	printf("%s window ready on %s\n", label, devpath);
	fflush(stdout);

	for (;;) {
		struct pollfd pf[2];
		char chunk[256];
		ssize_t n;
		size_t i;

		pf[0].fd = dev;
		pf[0].events = POLLIN;
		pf[1].fd = fifo;
		pf[1].events = POLLIN;
		if (poll(pf, 2, -1) < 0) {
			if (errno == EINTR) {
				continue;
			}
			perror("poll");
			return 1;
		}

		if (pf[0].revents & POLLIN) {
			n = read(dev, rx, sizeof(rx));
			if (n > 0) {
				printf("%s <- %.*s", label, (int)n, rx);
				if (rx[n - 1] != '\n') {
					printf("\n");
				}
				fflush(stdout);
			}
		}

		if (pf[1].revents & (POLLIN | POLLHUP)) {
			n = read(fifo, chunk, sizeof(chunk));
			if (n <= 0) {
				continue;
			}
			for (i = 0; i < (size_t)n; i++) {
				if (chunk[i] == '\n') {
					line[fill] = '\0';
					if (fill && write_all(dev, line, fill)) {
						perror("rpmsg write");
						return 1;
					}
					if (fill) {
						printf("%s -> %s\n", label, line);
						fflush(stdout);
					}
					fill = 0;
				} else if (fill + 1 < sizeof(line)) {
					line[fill++] = chunk[i];
				}
			}
		}
	}
}

static void *map_phys(off_t addr, size_t len)
{
	int fd = open("/dev/mem", O_RDWR | O_SYNC);
	void *p;

	if (fd < 0) {
		perror("/dev/mem");
		return NULL;
	}
	p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, addr);
	close(fd);
	if (p == MAP_FAILED) {
		perror("mmap");
		return NULL;
	}
	return p;
}

static int watch_main(void)
{
	volatile struct pb2_arm *arm = map_phys(PB2_ARM_CPU, 4096);
	uint32_t last_tick = 0;
	int samples = 0;

	if (!arm) {
		return 1;
	}
	printf("pru1 window ready, shared ram 0x%x\n", PB2_ARM_CPU);
	fflush(stdout);
	for (;;) {
		uint32_t tick = arm->tick;
		uint32_t done = arm->done;
		uint32_t seq = arm->applied_seq;
		uint32_t period = arm->applied_period;
		uint32_t arm_seq = arm->seq;

		printf("pru1 tick=%u done=%u applied_seq=%u period=%u arm_seq=%u\n",
		       tick, done, seq, period, arm_seq);
		if (samples > 0 && tick != last_tick) {
			printf("pru1 tick moved %u -> %u\n", last_tick, tick);
		}
		last_tick = tick;
		samples++;
		fflush(stdout);
		usleep(200000);
	}
}

static uint32_t chunk_seq = 1;

static int do_chunk(const char *text)
{
	volatile struct pb2_bulk *bulk = map_phys(PB2_BULK_ADDR, PB2_BULK_SIZE);
	uint32_t n;
	uint32_t crc;
	uint32_t got = 0;
	uint32_t seq;
	uint32_t i;

	if (!bulk) {
		return -1;
	}
	n = (uint32_t)strlen(text);
	if (n > sizeof(bulk->data)) {
		n = sizeof(bulk->data);
	}
	crc = pb2_crc32((const uint8_t *)text, n);
	/*
	 * This DDR mapping is device memory. A memcpy wider than one store
	 * raises SIGBUS. Byte stores through the volatile mapping do not.
	 */
	for (i = 0; i < n; i++) {
		bulk->data[i] = (uint8_t)text[i];
	}
	bulk->length = n;
	bulk->crc = crc;
	bulk->magic = PB2_BULK_MAGIC;
	seq = chunk_seq++;
	bulk->seq = seq;
	if (pb2_erpc_take(seq, n, crc, &got)) {
		printf("zephyr chunk seq %u len %u crc %08x -> mismatch %08x\n",
		       seq, n, crc, got);
		fflush(stdout);
		return -1;
	}
	printf("zephyr chunk seq %u len %u crc %08x -> ok\n", seq, n, crc);
	fflush(stdout);
	return 0;
}

static int erpc_line(const char *line)
{
	char reply[200];

	if (!strncmp(line, "say ", 4)) {
		if (pb2_erpc_say(line + 4, reply, sizeof(reply))) {
			printf("zephyr say failed\n");
			fflush(stdout);
			return -1;
		}
		printf("zephyr <- %s\n", reply);
		fflush(stdout);
		return 0;
	}
	if (!strncmp(line, "arm ", 4)) {
		unsigned period = 0, count = 0;

		if (sscanf(line + 4, "%u %u", &period, &count) != 2) {
			printf("zephyr arm wants <period> <count>\n");
			fflush(stdout);
			return -1;
		}
		if (pb2_erpc_arm(period, count)) {
			printf("zephyr arm failed\n");
			fflush(stdout);
			return -1;
		}
		printf("zephyr arm %u %u -> ok\n", period, count);
		fflush(stdout);
		return 0;
	}
	if (!strncmp(line, "chunk ", 6)) {
		return do_chunk(line + 6);
	}
	printf("zephyr commands: say <text> | arm <period> <count> | chunk <text>\n");
	fflush(stdout);
	return -1;
}

static int erpc_main(int argc, char **argv)
{
	const char *devpath = argv[2];
	const char *fifopath = argv[3];
	int dev, fifo;
	char chunk[256];
	char line[512];
	size_t fill = 0;

	if (argc != 4) {
		fprintf(stderr, "usage: %s erpc <dev> <fifo>\n", argv[0]);
		return 2;
	}
	dev = open(devpath, O_RDWR);
	if (dev < 0) {
		perror(devpath);
		return 1;
	}
	if (pb2_erpc_open(dev)) {
		fprintf(stderr, "eRPC client init failed\n");
		return 1;
	}
	fifo = open(fifopath, O_RDONLY);
	if (fifo < 0) {
		perror(fifopath);
		return 1;
	}
	printf("zephyr window ready on %s (eRPC)\n", devpath);
	fflush(stdout);

	for (;;) {
		ssize_t n;
		size_t i;

		n = read(fifo, chunk, sizeof(chunk));
		if (n <= 0) {
			continue;
		}
		for (i = 0; i < (size_t)n; i++) {
			if (chunk[i] == '\n') {
				line[fill] = '\0';
				if (fill) {
					printf("zephyr -> %s\n", line);
					fflush(stdout);
					erpc_line(line);
				}
				fill = 0;
			} else if (fill + 1 < sizeof(line)) {
				line[fill++] = chunk[i];
			}
		}
	}
}

static int open_out(const char *dir, const char *name)
{
	char path[512];
	int fd;

	snprintf(path, sizeof(path), "%s/%s.in", dir, name);
	printf("opening %s\n", path);
	fflush(stdout);
	fd = open(path, O_WRONLY);
	if (fd < 0) {
		perror(path);
	}
	return fd;
}

static int send_to(int fd, const char *who, const char *msg)
{
	char buf[480];
	int n = snprintf(buf, sizeof(buf), "%s\n", msg);

	if (n < 0 || (size_t)n >= sizeof(buf)) {
		fprintf(stderr, "message too long\n");
		return -1;
	}
	if (write_all(fd, buf, (size_t)n)) {
		perror(who);
		return -1;
	}
	printf("linux -> %s: %s\n", who, msg);
	fflush(stdout);
	return 0;
}

static int linux_main(int argc, char **argv)
{
	const char *dir = argv[2];
	int zephyr, pru0;
	char input[512];

	if (argc != 3) {
		fprintf(stderr, "usage: %s linux <fifo-dir>\n", argv[0]);
		return 2;
	}

	zephyr = open_out(dir, "zephyr");
	pru0 = open_out(dir, "pru0");
	if (zephyr < 0 || pru0 < 0) {
		return 1;
	}

	/* PRU0 learns Linux's endpoint from the first RPMsg it receives. */
	if (send_to(pru0, "pru0", "ping")) {
		return 1;
	}
	usleep(300000);
	if (send_to(zephyr, "zephyr", "say hello from linux")) {
		return 1;
	}
	usleep(300000);
	if (send_to(zephyr, "zephyr", "arm 100000 30")) {
		return 1;
	}
	usleep(300000);
	if (send_to(zephyr, "zephyr", "chunk bulk-hello")) {
		return 1;
	}
	usleep(300000);
	if (send_to(pru0, "pru0", "arm 250000 20")) {
		return 1;
	}

	printf("linux window ready. commands: zephyr say|arm|chunk ... | pru0 <text>\n");
	fflush(stdout);

	while (fgets(input, sizeof(input), stdin)) {
		char *msg;
		size_t len = strlen(input);

		while (len && (input[len - 1] == '\n' || input[len - 1] == '\r')) {
			input[--len] = '\0';
		}
		if (!len || input[0] == '#') {
			continue;
		}
		msg = strchr(input, ' ');
		if (!msg) {
			printf("use zephyr say|arm|chunk ... or pru0 <text>\n");
			fflush(stdout);
			continue;
		}
		*msg++ = '\0';
		if (!strcmp(input, "zephyr")) {
			send_to(zephyr, "zephyr", msg);
		} else if (!strcmp(input, "pru0")) {
			send_to(pru0, "pru0", msg);
		} else if (!strcmp(input, "quit")) {
			break;
		} else {
			printf("use zephyr say|arm|chunk ... or pru0 <text>\n");
			fflush(stdout);
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s peer|erpc|watch|linux ...\n", argv[0]);
		return 2;
	}
	if (!strcmp(argv[1], "peer")) {
		return peer_main(argc, argv);
	}
	if (!strcmp(argv[1], "erpc")) {
		return erpc_main(argc, argv);
	}
	if (!strcmp(argv[1], "watch")) {
		return watch_main();
	}
	if (!strcmp(argv[1], "linux")) {
		return linux_main(argc, argv);
	}
	fprintf(stderr, "unknown command %s\n", argv[1]);
	return 2;
}
