#ifndef WS_MEMORY_H
#define WS_MEMORY_H
#include <stddef.h>
typedef struct {
    size_t capacity, live_blocks, live_bytes, peak_bytes, failed_requests;
} ws_memory_stats;
/* Caller owns the aligned arena and keeps it alive until shutdown.
 * Initialize before mapping guest heap operations and before ELF constructors. */
int ws_memory_init(void *arena, size_t bytes);
int ws_memory_shutdown(void);
int ws_memory_check(void);
ws_memory_stats ws_memory_get_stats(void);
void *ws_malloc(size_t bytes);
void *ws_calloc(size_t count, size_t bytes);
void *ws_realloc(void *pointer, size_t bytes);
void ws_free(void *pointer);
int ws_posix_memalign(void **result, size_t alignment, size_t bytes);
char *ws_strdup(const char *text);
/* Returns zero for NULL and addresses outside the guest allocation region. */
size_t ws_malloc_usable_size(const void *pointer);
void *ws_memalign(size_t alignment,size_t bytes);
void *ws_valloc(size_t bytes);
void *ws_reallocf(void *pointer,size_t bytes);
#endif
