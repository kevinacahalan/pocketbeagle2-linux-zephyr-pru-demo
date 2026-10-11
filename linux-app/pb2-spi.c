/*
 * Clock a 5-byte "hello" out of Linux SPI0 through spidev.
 * Default device is /dev/spidev0.0, which is main_spi0 on this image
 * once k3-am62-pocketbeagle2-spi0-hello.dtbo is loaded.
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	const char *dev = argc > 1 ? argv[1] : "/dev/spidev0.0";
	const uint8_t tx[] = {'h', 'e', 'l', 'l', 'o'};
	uint8_t rx[sizeof(tx)];
	uint8_t mode = SPI_MODE_0;
	uint8_t bits = 8;
	uint32_t speed = 1000000;
	int fd;

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
	printf("spi hello wrote %zu bytes on %s\n", sizeof(tx), dev);
	close(fd);
	return 0;
}
