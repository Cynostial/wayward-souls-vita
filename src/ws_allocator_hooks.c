#include "ws_allocator_hooks.h"
#include "ws_memory.h"
#include <stdio.h>
#include <string.h>
static void *tagged(size_t bytes,uintptr_t tag,uintptr_t flags){(void)tag;(void)flags;return ws_malloc(bytes);}
static size_t good_size(size_t bytes){return bytes?bytes:1;}
static void stats(uint8_t *out) {
    if(!out)return;
    /* Recovered 56-byte jemalloc_stats ABI. Snapshot values: mapped +36,
     * committed +40, allocated +44, dirty +48. Fixed pools retain freed pages. */
    ws_memory_stats s=ws_memory_get_stats();uint32_t words[14]={0};
    words[2]=1;words[3]=UINT32_MAX;words[4]=8;words[5]=(uint32_t)s.capacity;
    words[6]=(uint32_t)s.capacity;words[7]=(uint32_t)s.capacity;
    words[9]=words[10]=(uint32_t)s.capacity;words[11]=(uint32_t)s.live_bytes;
    memcpy(out,words,sizeof(words));
}
static void print_stats(void){ws_memory_stats s=ws_memory_get_stats();fprintf(stderr,"Wayward heap: %zu/%zu bytes, peak %zu, %zu blocks, %zu failed requests\n",s.live_bytes,s.capacity,s.peak_bytes,s.live_blocks,s.failed_requests);}
typedef struct {const char *name;uint32_t offset;uintptr_t replacement;} patch;
int ws_allocator_attach(so_module *libv) {
    if(!libv||!ws_memory_get_stats().capacity)return -1;
    const patch patches[]={
        {"__wrap_malloc",0x31eb8,(uintptr_t)ws_malloc},
        {"__wrap_calloc",0x3348c,(uintptr_t)ws_calloc},
        {"__wrap_realloc",0x337c0,(uintptr_t)ws_realloc},
        {"__wrap_free",0x341c4,(uintptr_t)ws_free},
        {"__wrap_strdup",0xf758,(uintptr_t)ws_strdup},
        {"__wrap_memalign",0x3234c,(uintptr_t)ws_memalign},
        {"__wrap_posix_memalign",0x33388,(uintptr_t)ws_posix_memalign},
        {"__wrap_valloc",0x333d8,(uintptr_t)ws_valloc},
        {"__wrap_malloc_usable_size",0x3514c,(uintptr_t)ws_malloc_usable_size},
        {"reallocf",0x354d8,(uintptr_t)ws_reallocf},
        {"je_malloc_tagged",0x3232c,(uintptr_t)tagged},
        {"malloc_good_size",0x350d8,(uintptr_t)good_size},
        {"jemalloc_stats",0x35318,(uintptr_t)stats},
        {"malloc_print_stats",0x312a0,(uintptr_t)print_stats}
    };
    for(size_t i=0;i<sizeof(patches)/sizeof(*patches);++i)
        if(so_symbol(libv,patches[i].name)!=libv->load_addr+patches[i].offset)return -1;
    for(size_t i=0;i<sizeof(patches)/sizeof(*patches);++i)
        hook_addr(libv->load_addr+patches[i].offset,patches[i].replacement);
    /* The original C++ new wrappers deliberately calloc and delete calls
     * __wrap_free. Retain those semantics and redirect their callee entries. */
    so_flush_caches(libv);return 0;
}
