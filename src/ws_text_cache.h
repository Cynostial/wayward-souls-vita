#ifndef WS_TEXT_CACHE_H
#define WS_TEXT_CACHE_H
#include <stdint.h>
#include <stddef.h>
#define WS_TEXT_SLOTS 65536
#define WS_TEXT_LIMIT (WS_TEXT_SLOTS/2)
#define WS_TEXT_BYTES (1024u*1024u)
typedef struct {uint32_t hash,offset;uint16_t length;uintptr_t object;} ws_text_entry;
typedef struct {
    ws_text_entry entries[WS_TEXT_SLOTS];
    char bytes[WS_TEXT_BYTES];
    unsigned used,count,hits,misses;
} ws_text_cache;
/* Single owner. Stored immutable objects must remain retained for this
 * cache's lifetime. No eviction: borrowed results remain alive across calls. */
uintptr_t ws_text_cache_find(ws_text_cache *,const char *);
int ws_text_cache_insert(ws_text_cache *,const char *,uintptr_t);
#endif
