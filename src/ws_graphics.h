#ifndef WS_GRAPHICS_H
#define WS_GRAPHICS_H
#include <stdint.h>
#include "so_util.h"

/* Single render thread, matching EAGLView's original drawing model. */
int ws_graphics_init(void);
/* Coarse, render-owner-only pulse during pre-title synchronous data loads. */
void ws_graphics_loading_tick(void);
uintptr_t ws_graphics_symbol(const char *name);
/* Requires the pinned, relocated 1.12 libverde image. Install before UIKit
 * creates a surface. It does not initialize the Objective-C runtime or game. */
int ws_graphics_attach(so_module *game, so_module *objc, unsigned *wrapped_functions);
#endif
