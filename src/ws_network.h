#ifndef WS_NETWORK_H
#define WS_NETWORK_H
#include <stdint.h>
/* The current port has no Internet transport. Numeric address conversion and
 * local socketpairs work; external connections report the offline state. */
typedef struct ws_addrinfo {int32_t flags,family,type,protocol;uint32_t size;char *canonical;void *address;struct ws_addrinfo *next;} ws_addrinfo;
#if defined(__arm__)
_Static_assert(sizeof(ws_addrinfo)==32,"Android addrinfo ABI");
#endif
void ws_network_free_addresses(ws_addrinfo *);
uintptr_t ws_network_symbol(const char *);
#endif
