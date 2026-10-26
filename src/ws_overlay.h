#pragma once
#include <stdint.h>
/* Render thread publishes a logical 568x320 widget rectangle. The display
 * callback paints its outline only after GPU completion. No GL calls. */
void ws_overlay_focus(float x,float y,float width,float height);
void ws_overlay_draw(uint32_t *pixels,unsigned width,unsigned height,unsigned stride);
