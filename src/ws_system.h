#ifndef WS_SYSTEM_H
#define WS_SYSTEM_H
#include <stdint.h>
uintptr_t ws_system_symbol(const char *);
long ws_syscall(long number,...);
int ws_cxa_atexit(void (*function)(void *),void *argument,void *dso);
void ws_cxa_finalize(void *dso);
#endif
