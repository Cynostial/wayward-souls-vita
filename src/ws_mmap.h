#ifndef WS_MMAP_H
#define WS_MMAP_H
#include <stdint.h>
#include <stddef.h>
void *ws_mmap(void *,size_t,int,int,int,int32_t);
int ws_munmap(void *,size_t);
/* Dedicated executable pages are outside the bounded general-purpose heap. */
size_t ws_mmap_executable_bytes(void);
uintptr_t ws_mmap_symbol(const char *);
#endif
