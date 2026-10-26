#include "ws_loading.h"
#include <psp2/kernel/processmgr.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum {W=960,H=544,STRIDE=960};
static uint32_t *art;
static atomic_int ready;
void ws_loading_start(const char *path){
    if(art)return;
    FILE *file=fopen(path,"rb");if(!file)return;
    if(fseek(file,0,SEEK_END)){fclose(file);return;}long size=ftell(file);
    if(size<33||size>512*1024||fseek(file,0,SEEK_SET)){fclose(file);return;}
    unsigned char *png=malloc((size_t)size);if(!png){fclose(file);return;}
    size_t read=fread(png,1,(size_t)size,file);fclose(file);
    if(read!=(size_t)size){free(png);return;}
    /* Immutable CPU image within the existing native heap, never a scanout
     * buffer. Only VitaGL's completed-frame callback writes display pixels.
     * Keep this small image alive for queued callbacks until process exit. */
    art=malloc((size_t)STRIDE*H*sizeof(*art));
    if(!art){free(png);return;}
    if(ws_loading_image(png,size,art,W,H,STRIDE)){free(png);free(art);art=NULL;return;}
    free(png);
    fprintf(stderr,"Wayward official loading art ready (CPU image; VitaGL owns display).\n");
}
void ws_loading_handoff(int game_ready){
    if(game_ready)atomic_store(&ready,1);
}
int ws_loading_active(void){return art&&!atomic_load(&ready);}
int ws_loading_present(uint32_t *pixels,unsigned width,unsigned height,unsigned stride){
    if(!art||atomic_load(&ready)||!pixels||width!=W||height!=H||stride<width)return 0;
    for(unsigned y=0;y<H;++y)memcpy(pixels+(size_t)y*stride,art+(size_t)y*STRIDE,W*4);
    ws_loading_dots(pixels,width,height,stride,(unsigned)(sceKernelGetProcessTimeWide()/250000));
    return 1;
}
