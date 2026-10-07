/*
 * eRPC allocator for the M4. Zephyr's minimal C++ library already provides
 * operator new, so this file only maps erpc_malloc/erpc_free onto k_malloc.
 */

#include <zephyr/kernel.h>

#include "erpc_port.h"

void *erpc_malloc(size_t size)
{
	return k_malloc(size);
}

void erpc_free(void *ptr)
{
	k_free(ptr);
}
