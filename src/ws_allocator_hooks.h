#ifndef WS_ALLOCATOR_HOOKS_H
#define WS_ALLOCATOR_HOOKS_H
#include "so_util.h"
/* Must run before the first libv constructor/allocation. All allocation and
 * size/free entrypoints are switched together; never mix jemalloc and TLSF. */
int ws_allocator_attach(so_module *libv);
#endif
