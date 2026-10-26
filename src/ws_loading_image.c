/* Small bounded PNG reader for the game's unmodified RGB/RGBA loading art.
 * No GL/context is needed while Android libraries and VitaGL are starting. */
#include "ws_loading.h"
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
static uint32_t big32(const unsigned char *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static int paeth(int a,int b,int c){int p=a+b-c,pa=abs(p-a),pb=abs(p-b),pc=abs(p-c);return pa<=pb&&pa<=pc?a:pb<=pc?b:c;}
int ws_loading_image(const unsigned char *png,size_t size,uint32_t *pixels,unsigned width,unsigned height,unsigned stride){
    if(!png||size<33||size>512*1024||!pixels||!width||!height||width>2048||height>1536||stride<width||
       memcmp(png,"\x89PNG\r\n\x1a\n",8))return -1;
    unsigned w=0,h=0,bpp=0;size_t used=0;int ended=0;
    unsigned char *packed=malloc(size),*raw=NULL;if(!packed)return -1;
    for(size_t at=8;at+12<=size;){
        uint32_t n=big32(png+at);if(n>size-at-12)goto bad;
        const unsigned char *kind=png+at+4,*data=png+at+8;
        if((uint32_t)crc32(0,kind,n+4)!=big32(data+n))goto bad;
        if(!memcmp(kind,"IHDR",4)){
            if(at!=8||n!=13||data[8]!=8||(data[9]!=2&&data[9]!=6)||data[10]||data[11]||data[12])goto bad;
            w=big32(data);h=big32(data+4);bpp=data[9]==2?3:4;
            if(!w||!h||w>2048||h>1536)goto bad;
        }else if(!memcmp(kind,"IDAT",4)){
            if(!w||used+n>size)goto bad;
            memcpy(packed+used,data,n);used+=n;
        }else if(!memcmp(kind,"IEND",4)){if(n)goto bad;ended=1;break;}
        else if(!(kind[0]&32)&&memcmp(kind,"PLTE",4))goto bad;
        at+=n+12;
    }
    if(!w||!used||!ended)goto bad;
    size_t row=(size_t)w*bpp;uLongf raw_size=(row+1)*h;
    raw=malloc(raw_size);if(!raw)goto bad;
    uLongf produced=raw_size;if(uncompress(raw,&produced,packed,used)!=Z_OK||produced!=raw_size)goto bad;
    for(unsigned y=0;y<h;++y){
        unsigned char *r=raw+y*(row+1)+1,*above=y?r-(row+1):NULL;
        unsigned filter=r[-1];if(filter>4)goto bad;
        for(size_t x=0;x<row;++x){int a=x>=bpp?r[x-bpp]:0,b=above?above[x]:0,c=above&&x>=bpp?above[x-bpp]:0;
            int delta=filter==1?a:filter==2?b:filter==3?(a+b)/2:filter==4?paeth(a,b,c):0;
            r[x]=(unsigned char)(r[x]+delta);
        }
    }
    unsigned dw=width,dh=(unsigned)((uint64_t)h*width/w);
    if(dh>height){dh=height;dw=(unsigned)((uint64_t)w*height/h);}
    if(!dw||!dh)goto bad;
    unsigned left=(width-dw)/2,top=(height-dh)/2;
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)pixels[(size_t)y*stride+x]=0xff000000;
    for(unsigned y=0;y<dh;++y){const unsigned char *r=raw+(size_t)(y*h/dh)*(row+1)+1;
        for(unsigned x=0;x<dw;++x){const unsigned char *p=r+(size_t)(x*w/dw)*bpp;unsigned a=bpp==4?p[3]:255;
            pixels[(size_t)(top+y)*stride+left+x]=0xff000000u|((p[2]*a/255)<<16)|((p[1]*a/255)<<8)|(p[0]*a/255);
        }
    }
    free(raw);free(packed);return 0;
bad:free(raw);free(packed);return -1;
}
void ws_loading_dots(uint32_t *pixels,unsigned width,unsigned height,unsigned stride,unsigned phase){
    if(!pixels||width<100||height<60||stride<width)return;
    unsigned cx=width/2,cy=height-24;
    for(unsigned y=cy-7;y<=cy+7;++y)for(unsigned x=cx-30;x<=cx+30;++x)pixels[(size_t)y*stride+x]=0xff000000;
    for(int dot=0;dot<3;++dot){uint32_t color=(unsigned)dot==phase%3?0xffa9bbc8:0xff39434b;
        for(int y=-4;y<=4;++y)for(int x=-4;x<=4;++x)if(x*x+y*y<=16)
            pixels[(size_t)(cy+y)*stride+cx+(dot-1)*20+x]=color;
    }
}
