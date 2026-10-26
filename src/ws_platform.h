#ifndef WS_PLATFORM_H
#define WS_PLATFORM_H
#include "ws_loader.h"
int ws_platform_init(ws_loader *,void *arena,size_t bytes);
void ws_platform_visible(unsigned count);
const so_default_dynlib *ws_platform_bindings(size_t *count);
uintptr_t ws_platform_symbol(const char *name);
/* Emits all still-missing strong external bindings; constructors remain gated. */
unsigned ws_platform_missing(void (*report)(const char *));
#endif
