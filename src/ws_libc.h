#ifndef WS_LIBC_H
#define WS_LIBC_H
#include <stdint.h>
/* Pure C/softfp numeric/string services. Storage/stdio, allocation, TLS,
 * signals, process and socket APIs are supplied by their own ABI adapters. */
int ws_libc_install(int *(*android_errno)(void));
uintptr_t ws_libc_resolve(const char *);
#endif
