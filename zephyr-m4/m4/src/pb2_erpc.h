#ifndef PB2_ERPC_H_
#define PB2_ERPC_H_

#include <stddef.h>

void pb2_shm_publish(const char *cmd);
int pb2_rpmsg_tx(const void *data, size_t len);
void pb2_erpc_push(const void *data, size_t len);
void pb2_erpc_server(void);

#endif
