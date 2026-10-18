/* SPDX-License-Identifier: MIT */
/*
 * Linux eRPC client. The transport is the existing /dev/rpmsg character device.
 */

#include "erpc_framed_transport.hpp"
#include "erpc_client_setup.h"
#include "erpc_mbf_setup.h"
#include "erpc_port.h"
#include "c_pb2_client.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int g_fd = -1;
static uint8_t stash[512];
static size_t stash_len;
static size_t stash_pos;

class RpmsgLinuxTransport : public erpc::FramedTransport {
protected:
	erpc_status_t underlyingSend(const uint8_t *data, uint32_t size) override
	{
		while (size) {
			uint32_t n = size > 480u ? 480u : size;
			const uint8_t *p = data;
			uint32_t left = n;

			while (left) {
				ssize_t w = write(g_fd, p, left);

				if (w < 0) {
					if (errno == EINTR) {
						continue;
					}
					return kErpcStatus_SendFailed;
				}
				p += w;
				left -= static_cast<uint32_t>(w);
			}
			data += n;
			size -= n;
		}
		return kErpcStatus_Success;
	}

	erpc_status_t underlyingReceive(uint8_t *data, uint32_t size) override
	{
		uint32_t got = 0;

		while (got < size) {
			if (stash_pos >= stash_len) {
				ssize_t n = read(g_fd, stash, sizeof(stash));

				if (n < 0) {
					if (errno == EINTR) {
						continue;
					}
					return kErpcStatus_ReceiveFailed;
				}
				if (n == 0) {
					return kErpcStatus_ReceiveFailed;
				}
				stash_len = static_cast<size_t>(n);
				stash_pos = 0;
			}
			data[got++] = stash[stash_pos++];
		}
		return kErpcStatus_Success;
	}
};

static RpmsgLinuxTransport *g_transport;

extern "C" int pb2_erpc_open(int fd)
{
	erpc_mbf_t mbf;
	erpc_client_t client;

	g_fd = fd;
	g_transport = new RpmsgLinuxTransport();
	mbf = erpc_mbf_dynamic_init();
	client = erpc_client_init(reinterpret_cast<erpc_transport_t>(g_transport), mbf);
	if (!client) {
		return -1;
	}
	initPb2Service_client(client);
	return 0;
}

extern "C" int pb2_erpc_say(const char *msg, char *out, size_t outlen)
{
	char *reply = say(msg);

	if (!reply) {
		return -1;
	}
	snprintf(out, outlen, "%s", reply);
	erpc_free(reply);
	return 0;
}

extern "C" int pb2_erpc_spi(const char *text, char *out, size_t outlen)
{
	char *reply = spiWrite(text ? text : "");

	if (!reply) {
		return -1;
	}
	snprintf(out, outlen, "%s", reply);
	erpc_free(reply);
	return 0;
}

extern "C" int pb2_erpc_arm(uint32_t period, uint32_t count)
{
	int32_t rc = armPru1(period, count);

	return rc == 0 ? 0 : -1;
}

extern "C" int pb2_erpc_xy(uint32_t x, uint32_t y)
{
	int32_t rc = sendXy(x, y);

	return rc == 0 ? 0 : -1;
}

extern "C" int pb2_erpc_take(uint32_t seq, uint32_t length, uint32_t crc, uint32_t *got)
{
	uint32_t rc = takeChunk(seq, length, crc);

	*got = rc;
	return rc == crc ? 0 : -1;
}
