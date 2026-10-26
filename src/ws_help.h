#pragma once
#include <stdint.h>
void ws_help_show(int visible);
int ws_help_active(void);
int ws_help_draw(uint32_t *pixels,unsigned width,unsigned height,unsigned stride);
