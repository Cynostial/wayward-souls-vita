#pragma once
#include <stddef.h>
#include <stdint.h>
int ws_loading_image(const unsigned char *,size_t,uint32_t *,unsigned,unsigned,unsigned);
void ws_loading_dots(uint32_t *,unsigned,unsigned,unsigned,unsigned);
void ws_loading_start(const char *path);
void ws_loading_handoff(int game_ready);
int ws_loading_active(void);
int ws_loading_present(uint32_t *,unsigned,unsigned,unsigned);
