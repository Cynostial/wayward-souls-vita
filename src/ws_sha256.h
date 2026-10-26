#ifndef WS_SHA256_H
#define WS_SHA256_H
#include <stddef.h>
#include <stdint.h>
void ws_sha256(const void *,size_t,uint8_t out[32]);
typedef struct { uint32_t h[8]; uint64_t bytes; uint8_t tail[64]; size_t used; } ws_sha256_state;
void ws_sha256_init(ws_sha256_state *);
void ws_sha256_update(ws_sha256_state *,const void *,size_t);
void ws_sha256_final(ws_sha256_state *,uint8_t [32]);
#endif
