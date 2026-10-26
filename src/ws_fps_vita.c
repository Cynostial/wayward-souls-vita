#include "ws_fps.h"
#include "ws_overlay.h"
#include "ws_loading.h"
#include "ws_help.h"
#include <vitaGL.h>
#include <psp2/kernel/processmgr.h>
extern unsigned DISPLAY_WIDTH,DISPLAY_HEIGHT,DISPLAY_STRIDE;
static ws_fps counter;
static void present(void *framebuffer){
    uint32_t *pixels=framebuffer;
    if(ws_loading_present(pixels,DISPLAY_WIDTH,DISPLAY_HEIGHT,DISPLAY_STRIDE))return;
    if(ws_help_draw(pixels,DISPLAY_WIDTH,DISPLAY_HEIGHT,DISPLAY_STRIDE))return;
    ws_overlay_draw(pixels,DISPLAY_WIDTH,DISPLAY_HEIGHT,DISPLAY_STRIDE);
    ws_fps_sample(&counter,sceKernelGetProcessTimeWide());
    if(ws_fps_visible())ws_fps_draw(&counter,pixels,DISPLAY_WIDTH,DISPLAY_HEIGHT,DISPLAY_STRIDE);
}
void ws_fps_install(void){vglSetDisplayCallback(present);}
