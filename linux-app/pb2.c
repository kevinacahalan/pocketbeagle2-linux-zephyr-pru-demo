/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pb2_erpc_linux.h"
#include "pb2_shm.h"

/*
 * pb2 peer <label> <rpmsg-dev> <fifo>
 *   Owns one RPMsg device. Used for PRU0.
 *
 * pb2 erpc <rpmsg-dev> <fifo>
 *   Calls Zephyr functions over eRPC on the existing RPMsg fd.
 *
 * pb2 watch
 *   Prints PRU1's shared-RAM status. PRU1 has no RPMsg endpoint.
 *
 * pb2 xy
 *   Prints the shared-RAM X/Y record once and exits.
 *
 * pb2 spi [device]
 *   Clocks the five-byte SPI hello. Default device is /dev/spidev0.0.
 *
 * pb2 linux <dir>
 *   Sends the startup script, then reads commands such as xy and spi.
 */

static int g_reply_fd = -1;

static void unquote(char *s)
{
	char *w = s;

	for (char *r = s; *r; r++) {
		if (*r != '"') {
			*w++ = *r;
		}
	}
	*w = '\0';
}

static void print_help(void)
{
	printf("commands:\n");
	printf("  zephyr say <text>\n");
	printf("  zephyr arm <period> <count>\n");
	printf("  zephyr chunk <text>\n");
	printf("  zephyr xy <x> <y>\n");
	printf("  zephyr spi [text]\n");
	printf("  pru0 <text>\n");
	printf("  spi\n");
	printf("  help\n");
	fflush(stdout);
}

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

/* Print on this process, and hand the same line to the linux window. */
static void note_line(const char *line)
{
	char buf[720];
	int n;

	printf("%s\n", line);
	fflush(stdout);
	if (g_reply_fd < 0) {
		return;
	}
	n = snprintf(buf, sizeof(buf), "%s\n", line);
	if (n > 0 && (size_t)n < sizeof(buf)) {
		write_all(g_reply_fd, buf, (size_t)n);
	}
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
				/* The firmware sentence already names who asked
				 * and what this core did. Do not prefix it with
				 * the core name again.
				 */
				printf("%.*s", (int)n, rx);
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
						printf("sent to %s: %s\n", label, line);
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
	volatile struct pb2_xy *xy;
	uint32_t last_arm_seq = 0;
	uint32_t last_xy_seq = 0;
	int primed = 0;

	if (!arm) {
		return 1;
	}
	xy = (volatile struct pb2_xy *)((uintptr_t)arm + (PB2_XY_CPU - PB2_ARM_CPU));
	printf("pru1 window ready, shared ram 0x%x\n", PB2_ARM_CPU);
	fflush(stdout);
	for (;;) {
		uint32_t arm_seq = arm->seq;
		uint32_t xy_seq = xy->seq;

		/* Sample the seq first. An unchanged seq is not news. */
		if (!primed) {
			last_arm_seq = arm_seq;
			last_xy_seq = xy_seq;
			primed = 1;
		} else {
			if (arm_seq != last_arm_seq) {
				printf("pru1 arm seq=%u period=%u count=%u\n",
				       arm_seq, arm->period_cycles, arm->periods);
				last_arm_seq = arm_seq;
			}
			if (xy_seq != last_xy_seq) {
				printf("pru1 xy seq=%u x=%u y=%u consumed=%u\n",
				       xy_seq, xy->x, xy->y, xy->consumed);
				last_xy_seq = xy_seq;
			}
		}
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
		printf("Linux asked Zephyr to check the chunk, and Zephyr saw a mismatch seq %u len %u crc %08x got %08x\n",
		       seq, n, crc, got);
		fflush(stdout);
		return -1;
	}
	printf("Linux asked Zephyr to check the chunk, and Zephyr matched seq %u len %u crc %08x\n",
	       seq, n, crc);
	fflush(stdout);
	return 0;
}

static int erpc_line(const char *line)
{
	char reply[200];

	if (!strncmp(line, "say ", 4)) {
		char text[512];
		char shown[800];

		snprintf(text, sizeof(text), "%s", line + 4);
		unquote(text);
		if (pb2_erpc_say(text, reply, sizeof(reply))) {
			snprintf(shown, sizeof(shown),
				 "Linux asked Zephyr to say \"%s\", and Zephyr failed",
				 text);
			note_line(shown);
			return -1;
		}
		snprintf(shown, sizeof(shown),
			 "Linux asked Zephyr to say \"%s\", and Zephyr replied \"%s\"",
			 text, reply);
		note_line(shown);
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
			printf("Linux asked Zephyr to arm PRU1, and Zephyr failed\n");
			fflush(stdout);
			return -1;
		}
		printf("Linux asked Zephyr to arm PRU1, and Zephyr published period %u count %u for PRU0\n",
		       period, count);
		fflush(stdout);
		return 0;
	}
	if (!strncmp(line, "chunk ", 6)) {
		return do_chunk(line + 6);
	}
	if (!strncmp(line, "xy ", 3)) {
		unsigned x = 0, y = 0;

		if (sscanf(line + 3, "%u %u", &x, &y) != 2) {
			printf("zephyr xy wants <x> <y>\n");
			fflush(stdout);
			return -1;
		}
		if (pb2_erpc_xy(x, y)) {
			printf("Linux asked Zephyr to publish X/Y, and Zephyr failed\n");
			fflush(stdout);
			return -1;
		}
		printf("Linux asked Zephyr to publish X/Y %u %u, and Zephyr sent that pair to PRU0\n",
		       x, y);
		fflush(stdout);
		return 0;
	}
	if (!strcmp(line, "spi") || !strncmp(line, "spi ", 4)) {
		char text[512];
		char shown[800];
		const char *label;

		text[0] = '\0';
		if (!strncmp(line, "spi ", 4)) {
			snprintf(text, sizeof(text), "%s", line + 4);
			unquote(text);
		}
		label = text[0] ? text : "hello";
		if (pb2_erpc_spi(text, reply, sizeof(reply))) {
			snprintf(shown, sizeof(shown),
				 "Linux asked Zephyr to write SPI2 \"%s\", and Zephyr failed",
				 label);
			note_line(shown);
			return -1;
		}
		snprintf(shown, sizeof(shown),
			 "Linux asked Zephyr to write SPI2 \"%s\", and Zephyr replied \"%s\"",
			 label, reply);
		note_line(shown);
		return 0;
	}
	printf("zephyr commands: say <text> | arm <period> <count> | chunk <text> | xy <x> <y> | spi [text]\n");
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
	{
		char reply_path[512];
		size_t n = strlen(fifopath);

		if (n >= 3 && !strcmp(fifopath + n - 3, ".in") &&
		    snprintf(reply_path, sizeof(reply_path), "%.*s.reply",
			     (int)(n - 3), fifopath) < (int)sizeof(reply_path)) {
			g_reply_fd = open(reply_path, O_RDWR | O_NONBLOCK);
		}
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

static void print_reply(void)
{
	char reply[700];
	struct pollfd pf;
	size_t fill = 0;

	fflush(stdout);
	if (g_reply_fd < 0) {
		return;
	}
	pf.fd = g_reply_fd;
	pf.events = POLLIN;
	while (fill + 1 < sizeof(reply)) {
		ssize_t got;
		char c;

		if (poll(&pf, 1, 3000) <= 0) {
			break;
		}
		got = read(g_reply_fd, &c, 1);
		if (got <= 0) {
			break;
		}
		if (c == '\n') {
			reply[fill] = '\0';
			printf("%s\n", reply);
			fflush(stdout);
			return;
		}
		reply[fill++] = c;
	}
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
	if (!strcmp(who, "pru0") && !strncmp(msg, "xy ", 3)) {
		printf("Linux asked PRU0 to publish X/Y %s\n", msg + 3);
	} else if (!strcmp(who, "pru0") && !strncmp(msg, "arm ", 4)) {
		printf("Linux asked PRU0 to arm PRU1 %s\n", msg + 4);
	} else if (!strcmp(who, "pru0")) {
		printf("Linux sent \"%s\" to PRU0\n", msg);
	} else if (!strcmp(who, "zephyr") && !strncmp(msg, "xy ", 3)) {
		printf("Linux asked Zephyr to publish X/Y %s\n", msg + 3);
	} else if (!strcmp(who, "zephyr") && !strncmp(msg, "arm ", 4)) {
		printf("Linux asked Zephyr to arm PRU1 %s\n", msg + 4);
	} else if (!strcmp(who, "zephyr") && !strncmp(msg, "chunk ", 6)) {
		printf("Linux asked Zephyr to check chunk %s\n", msg + 6);
	} else if (!strcmp(who, "zephyr") && !strncmp(msg, "say ", 4)) {
		printf("Linux asked Zephyr to say %s\n", msg + 4);
		print_reply();
		return 0;
	} else if (!strcmp(who, "zephyr") &&
		   (!strcmp(msg, "spi") || !strncmp(msg, "spi ", 4))) {
		if (!strcmp(msg, "spi") || msg[4] == '\0') {
			printf("Linux asked Zephyr to write SPI2 hello\n");
		} else {
			printf("Linux asked Zephyr to write SPI2 %s\n", msg + 4);
		}
		print_reply();
		return 0;
	} else {
		printf("Linux sent %s %s\n", who, msg);
	}
	fflush(stdout);
	return 0;
}

static int spi_hello(const char *dev)
{
	const uint8_t tx[] = {'h', 'e', 'l', 'l', 'o'};
	uint8_t rx[sizeof(tx)];
	uint8_t mode = SPI_MODE_0;
	uint8_t bits = 8;
	uint32_t speed = 1000000;
	int fd;

	if (!dev || !dev[0]) {
		dev = "/dev/spidev0.0";
	}
	fd = open(dev, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "open %s: %s\n", dev, strerror(errno));
		return 1;
	}
	if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0 ||
	    ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
	    ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
		fprintf(stderr, "spi setup: %s\n", strerror(errno));
		close(fd);
		return 1;
	}

	memset(rx, 0, sizeof(rx));
	struct spi_ioc_transfer tr = {
		.tx_buf = (unsigned long)tx,
		.rx_buf = (unsigned long)rx,
		.len = sizeof(tx),
		.speed_hz = speed,
		.bits_per_word = bits,
	};
	if (ioctl(fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
		fprintf(stderr, "spi transfer: %s\n", strerror(errno));
		close(fd);
		return 1;
	}
	printf("Linux wrote the SPI hello, %zu bytes on %s\n", sizeof(tx), dev);
	fflush(stdout);
	close(fd);
	return 0;
}

static int xy_main(void)
{
	volatile struct pb2_arm *base = map_phys(PB2_ARM_CPU, 4096);
	volatile struct pb2_xy *xy;

	if (!base) {
		return 1;
	}
	xy = (volatile struct pb2_xy *)((uintptr_t)base + (PB2_XY_CPU - PB2_ARM_CPU));
	printf("xy cpu 0x%x magic=%08x seq=%u x=%u y=%u consumed=%u\n",
	       PB2_XY_CPU, xy->magic, xy->seq, xy->x, xy->y, xy->consumed);
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

	{
		char path[512];
		int fd;

		snprintf(path, sizeof(path), "%s/zephyr.reply", dir);
		unlink(path);
		if (mkfifo(path, 0666) && errno != EEXIST) {
			perror(path);
		}
		fd = open(path, O_RDWR | O_NONBLOCK);
		if (fd >= 0) {
			g_reply_fd = fd;
		}
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
	if (send_to(zephyr, "zephyr", "xy 41 83")) {
		return 1;
	}
	usleep(300000);
	if (send_to(zephyr, "zephyr", "spi")) {
		return 1;
	}
	usleep(300000);
	if (send_to(pru0, "pru0", "arm 250000 20")) {
		return 1;
	}

	printf("linux window ready. commands: zephyr say|arm|chunk|xy|spi ... | pru0 <text> | spi | help\n");
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
		if (!strcmp(input, "quit")) {
			break;
		}
		if (!strcmp(input, "help")) {
			print_help();
			continue;
		}
		if (!strcmp(input, "spi") || !strncmp(input, "spi ", 4)) {
			const char *dev = "/dev/spidev0.0";

			if (!strncmp(input, "spi ", 4)) {
				dev = input + 4;
				while (*dev == ' ') {
					dev++;
				}
				if (!*dev) {
					dev = "/dev/spidev0.0";
				}
			}
			spi_hello(dev);
			continue;
		}
		msg = strchr(input, ' ');
		if (!msg) {
			printf("use zephyr say|arm|chunk|xy|spi ... or pru0 <text> or spi or help\n");
			fflush(stdout);
			continue;
		}
		*msg++ = '\0';
		if (!strcmp(input, "zephyr")) {
			if (!strncmp(msg, "say ", 4) || !strncmp(msg, "spi ", 4)) {
				unquote(msg + 4);
			}
			send_to(zephyr, "zephyr", msg);
		} else if (!strcmp(input, "pru0")) {
			send_to(pru0, "pru0", msg);
		} else {
			printf("use zephyr say|arm|chunk|xy|spi ... or pru0 <text> or spi or help\n");
			fflush(stdout);
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s peer|erpc|watch|xy|spi|linux ...\n", argv[0]);
		return 2;
	}
	if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
		print_help();
		return 0;
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
	if (!strcmp(argv[1], "xy")) {
		return xy_main();
	}
	if (!strcmp(argv[1], "spi")) {
		return spi_hello(argc > 2 ? argv[2] : "/dev/spidev0.0");
	}
	if (!strcmp(argv[1], "linux")) {
		return linux_main(argc, argv);
	}
	fprintf(stderr, "unknown command %s\n", argv[1]);
	return 2;
}
