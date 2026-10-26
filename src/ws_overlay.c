#include "ws_overlay.h"
#include <stdatomic.h>
#include <math.h>
#include <stddef.h>

_Static_assert(ATOMIC_INT_LOCK_FREE==2,"Display overlay needs lock-free 32-bit atomics");
/* Single producer (render thread), single reader (display callback). All
 * fields are atomic; a bounded sequence check skips an overlapping update
 * rather than blocking the display thread or using mixed coordinates. */
static atomic_uint sequence,corner0,corner1;
static int bound(int n,int low,int high){return n<low?low:n>high?high:n;}
static unsigned pack(int x,int y){return (unsigned)(uint16_t)x|((unsigned)(uint16_t)y<<16);}
void ws_overlay_focus(float x,float y,float width,float height){
    unsigned a=0,b=0;
    if(isfinite(x)&&isfinite(y)&&isfinite(width)&&isfinite(height)&&width>0&&height>0&&
       x<568&&y<320&&x+width>0&&y+height>0){
        /* Clamp in floating point before conversion, including huge widgets.
         * Keep a small offscreen margin so clipping doesn't invent a border. */
        int x0=(int)floorf(fmaxf(-8,x-2)),y0=(int)floorf(fmaxf(-8,y-2));
        int x1=(int)ceilf(fminf(576,x+width+2)),y1=(int)ceilf(fminf(328,y+height+2));
        a=pack(x0,y0);b=pack(x1,y1);
    }
    atomic_fetch_add(&sequence,1);
    atomic_store(&corner0,a);atomic_store(&corner1,b);
    atomic_fetch_add(&sequence,1);
}
static void fill(uint32_t *pixels,unsigned width,unsigned height,unsigned stride,
                 int x0,int y0,int x1,int y1,uint32_t color){
    x0=bound(x0,0,(int)width);x1=bound(x1,0,(int)width);
    y0=bound(y0,0,(int)height);y1=bound(y1,0,(int)height);
    for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x)pixels[(size_t)y*stride+x]=color;
}
static void border(uint32_t *p,unsigned w,unsigned h,unsigned stride,
                   int x0,int y0,int x1,int y1,int thickness,uint32_t color){
    fill(p,w,h,stride,x0,y0,x1,y0+thickness,color);
    fill(p,w,h,stride,x0,y1-thickness,x1,y1,color);
    fill(p,w,h,stride,x0,y0+thickness,x0+thickness,y1-thickness,color);
    fill(p,w,h,stride,x1-thickness,y0+thickness,x1,y1-thickness,color);
}
void ws_overlay_draw(uint32_t *pixels,unsigned width,unsigned height,unsigned stride){
    if(!pixels||!width||!height||stride<width||width>65535||height>65535)return;
    unsigned before=atomic_load(&sequence);if(before&1)return;
    unsigned a=atomic_load(&corner0),b=atomic_load(&corner1);
    if(before!=atomic_load(&sequence)||a==b)return;
    int x0=(int16_t)a,y0=(int16_t)(a>>16),x1=(int16_t)b,y1=(int16_t)(b>>16);
    unsigned view_w=width,view_h=width*320/568;
    if(view_h>height){view_h=height;view_w=height*568/320;}
    int left=(int)(width-view_w)/2,top=(int)(height-view_h)/2;
    x0=left+(int)floorf((float)x0*view_w/568);x1=left+(int)ceilf((float)x1*view_w/568);
    y0=top+(int)floorf((float)y0*view_h/320);y1=top+(int)ceilf((float)y1*view_h/320);
    if(x1-x0<6||y1-y0<6)return;
    /* Only a thin outline is written. The original widget/sprite interior,
     * framebuffer row padding and all other rendered pixels stay untouched. */
    border(pixels,width,height,stride,x0,y0,x1,y1,4,0xff080f14u);
    border(pixels,width,height,stride,x0+1,y0+1,x1-1,y1-1,2,0xff59dbffu);
}
