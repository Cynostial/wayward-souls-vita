#include "ws_text_cache.h"
#include <string.h>
static ws_text_entry *slot(ws_text_cache *c,const char *s,size_t *length,uint32_t *hash){
    if(!c||!s)return NULL;
    size_t n=0;uint32_t h=2166136261u;
    while(s[n]&&n<192){h=(h^(unsigned char)s[n])*16777619u;++n;}
    if(s[n])return NULL; /* Large JSON/base64/story strings keep normal ownership. */
    *length=n;*hash=h;
    for(unsigned i=0;i<32;++i){ws_text_entry *e=&c->entries[(h+i)&(WS_TEXT_SLOTS-1)];
        if(!e->object||(e->hash==h&&e->length==n&&!memcmp(c->bytes+e->offset,s,n+1)))return e;
    }
    return NULL;
}
uintptr_t ws_text_cache_find(ws_text_cache *c,const char *s){
    size_t n;uint32_t h;ws_text_entry *e=slot(c,s,&n,&h);
    if(e&&e->object){++c->hits;return e->object;}
    if(c)++c->misses;
    return 0;
}
int ws_text_cache_insert(ws_text_cache *c,const char *s,uintptr_t object){
    size_t n;uint32_t h;ws_text_entry *e=slot(c,s,&n,&h);
    if(!object||!e||e->object||c->count>=WS_TEXT_LIMIT||n+1>WS_TEXT_BYTES-c->used)return 0;
    memcpy(c->bytes+c->used,s,n+1);*e=(ws_text_entry){h,c->used,(uint16_t)n,object};
    c->used+=(unsigned)n+1;++c->count;return 1;
}
