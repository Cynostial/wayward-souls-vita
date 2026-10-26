#pragma once
#include <stdint.h>
typedef struct { uint64_t start; unsigned frames,tenths; int started,valid; } ws_fps;
void ws_fps_sample(ws_fps *,uint64_t);
void ws_fps_draw(const ws_fps *,uint32_t *,unsigned,unsigned,unsigned);
void ws_fps_install(void);
void ws_fps_set_visible(int visible);
int ws_fps_visible(void);

