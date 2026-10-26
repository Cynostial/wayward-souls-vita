#include "ws_setup_screen.h"
#include "ws_setup_font.h"
#include <stdio.h>
#include <string.h>
static const uint32_t *art;
void ws_setup_screen_art(const uint32_t *pixels){art=pixels;}
static void rect(uint32_t *p,unsigned x,unsigned y,unsigned w,unsigned h,uint32_t color){
    for(unsigned j=y;j<y+h&&j<544;++j)for(unsigned i=x;i<x+w&&i<960;++i)p[j*960+i]=color;
}
static void glyph(uint32_t *p,unsigned x,unsigned y,unsigned char c,unsigned size,uint32_t color){
    if(c<32||c>126)c='?';
    const unsigned char *g=setup_font_pixels+(c-32)*28*32;
    for(unsigned j=0;j<32*size/24&&y+j<544;++j)for(unsigned i=0;i<28*size/24&&x+i<960;++i){
        unsigned a=g[(j*24/size)*28+i*24/size];if(!a)continue;
        uint32_t *q=&p[(y+j)*960+x+i],v=0xff000000;
        for(unsigned s=0;s<24;s+=8)v|=((((color>>s)&255)*a+((*q>>s)&255)*(255-a)+127)/255)<<s;
        *q=v;
    }
}
static unsigned advance(unsigned char c,unsigned size){return (setup_font_advance[c>=32&&c<=126?c-32:'?'-32]*size+12)/24;}
static unsigned word_width(const char *s,unsigned size){
    unsigned n=0;for(;*s&&*s!=' '&&*s!='\n';++s)n+=advance((unsigned char)*s,size);return n;
}
static unsigned text_pass(uint32_t *p,unsigned y,const char *s,unsigned size,uint32_t color){
    const unsigned left=64,right=456;unsigned x=left,top=y,height=32*size/24,step=height+3;int word=1;
    for(;*s;++s){
        if(*s=='\n'){x=left;y+=step;word=1;continue;}
        if(*s==' '&&x==left)continue;
        if(word&&*s!=' '&&x>left&&x+word_width(s,size)>right){x=left;y+=step;}
        unsigned n=advance((unsigned char)*s,size);
        if(x>left&&x+n>right){x=left;y+=step;if(*s==' ')continue;}
        if(p)glyph(p,x,y,(unsigned char)*s,size,color);
        x+=n;word=*s==' ';
    }
    return y-top+height;
}
static void text(uint32_t *p,unsigned y,const char *s,unsigned size,uint32_t color,unsigned bottom){
    while(size>12&&text_pass(NULL,y,s,size,color)>bottom-y)--size;
    text_pass(p,y,s,size,color);
}
void ws_setup_screen(uint32_t *p,const char *message,unsigned done,unsigned total,int mode){
    if(art)memcpy(p,art,960u*544u*4u);else rect(p,0,0,960,544,0xff000000);
    const uint32_t ink=0xffdfe8ed,muted=0xff96a1ad;
    if(mode==1){
        text(p,176,message&&*message?message:"Preparing",28,ink,230);
        unsigned percent=total?(unsigned)((uint64_t)done*100/total):0;if(percent>100)percent=100;
        rect(p,64,245,352,4,0xff303339);rect(p,64,245,352*percent/100,4,ink);
        char value[16];snprintf(value,sizeof(value),"%u%%",percent);text(p,264,value,24,ink,310);
        text(p,342,"Circle   Pause",22,muted,390);
    }else if(mode==2){
        text(p,176,"Ready to play",28,ink,230);
        text(p,268,"START   Play",24,ink,320);
        text(p,334,"Keep source.apk in this folder.",20,muted,402);
        if(message&&*message&&strcmp(message,"Ready to play."))text(p,420,message,18,muted,512);
    }else if(mode==3){
        text(p,176,"Before you play",28,ink,230);
        text(p,244,message&&*message?message:"Setup cannot continue.",22,muted,378);
        text(p,420,"Circle   Close",22,ink,480);
    }else{
        text(p,116,"Place APK in",26,ink,160);
        text(p,162,"ux0:data/wayward/",26,ink,210);
        text(p,222,"Android 1.12 ARMv7",20,muted,266);
        text(p,310,"START   Prepare",24,ink,352);
        text(p,360,"Circle   Close",20,muted,402);
        if(message&&*message)text(p,424,message,18,muted,516);
    }
}
