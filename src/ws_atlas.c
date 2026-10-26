#include "ws_atlas.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

/* Restrict a rectangular atlas region to its outer texel centers. Vertex
 * positions, tint, winding and tile adjacency are unchanged. Repeating or
 * nonrectangular coordinates bypass this rule. Existing insets never expand. */
int ws_atlas_inset(float out[8],const float uv[8],int width,int height){
    if(!out||!uv||width<2||height<2)return 0;
    float low[2]={uv[0],uv[1]},high[2]={uv[0],uv[1]};
    for(unsigned i=0;i<8;++i){
        if(!isfinite(uv[i])||uv[i]<0||uv[i]>1)return 0;
        unsigned axis=i&1;if(uv[i]<low[axis])low[axis]=uv[i];if(uv[i]>high[axis])high[axis]=uv[i];
    }
    if(low[0]==0&&low[1]==0&&high[0]==1&&high[1]==1)return 0;
    for(unsigned i=0;i<8;++i)if(uv[i]!=low[i&1]&&uv[i]!=high[i&1])return 0;
    float a[2],b[2];int sizes[2]={width,height};
    for(unsigned axis=0;axis<2;++axis){
        float size=(float)sizes[axis];
        if((high[axis]-low[axis])*size<1)return 0;
        a[axis]=(floorf(low[axis]*size)+0.5f)/size;
        b[axis]=(ceilf(high[axis]*size)-0.5f)/size;
        if(a[axis]<low[axis])a[axis]=low[axis];
        if(b[axis]>high[axis])b[axis]=high[axis];
        if(a[axis]>b[axis])return 0;
    }
    for(unsigned i=0;i<8;++i)out[i]=uv[i]==low[i&1]?a[i&1]:b[i&1];
    return 1;
}

/* Values, not texture/object pointers, are the key: atlas objects can be freed
 * or resized between levels. Fixed storage also caches bypass decisions.
 * Owned exclusively by the single EAGL/VitaGL renderer. Avoid GCC emulated
 * TLS (a pthread lookup/kernel TLS query for every access) in this hot path.
 * Four-way sets retain animated frames that hash to the same slot. */
typedef struct {uint32_t key[10];float uv[8];unsigned valid;} atlas_entry;
enum {ATLAS_SETS=1024,ATLAS_WAYS=4};
static atlas_entry atlas_cache[ATLAS_SETS][ATLAS_WAYS];
static unsigned char next_way[ATLAS_SETS];
static ws_atlas_stats counters;
const float *ws_atlas_cached(const float uv[8],int width,int height){
    if(!uv||width<2||height<2)return uv;
    uint32_t key[10];memcpy(key,uv,32);key[8]=(uint32_t)width;key[9]=(uint32_t)height;
    uint32_t hash=2166136261u;
    for(unsigned i=0;i<10;++i)hash=(hash^key[i])*16777619u;
    hash^=hash>>16;
    unsigned set=hash&(ATLAS_SETS-1);
    atlas_entry *entry=NULL;
    for(unsigned i=0;i<ATLAS_WAYS;++i){
        atlas_entry *candidate=&atlas_cache[set][i];
        if(candidate->valid&&!memcmp(candidate->key,key,sizeof(key))){
            ++counters.hits;return candidate->valid==2?candidate->uv:uv;
        }
        if(!candidate->valid&&!entry)entry=candidate;
    }
    if(!entry){entry=&atlas_cache[set][next_way[set]];next_way[set]=(next_way[set]+1)%ATLAS_WAYS;}
    ++counters.misses;
    memcpy(entry->key,key,sizeof(key));
    entry->valid=ws_atlas_inset(entry->uv,uv,width,height)?2:1;
    return entry->valid==2?entry->uv:uv;
}
void ws_atlas_counters(ws_atlas_stats *out){if(out)*out=counters;}
typedef uintptr_t object;
typedef void (*quad_fn)(object,object,const float *,const float *,const float *);
static quad_fn quad_color,quad_colors;
typedef void (*poly_fn)(object,object,const float *,int,float,const float *);
static poly_fn poly_color,poly_strip,poly_colors;
static void (*frame_scale)(object,object,int,int,int);
static void (*frame_size)(object,object,int,int,int,int,int);
static uint32_t *buffer_texture,*frame_texture,*texture_width,*texture_height;
static uint32_t *num_tris,*max_tris,*vertex_buffer,*texcoord_buffer,*color_buffer;
static object frame_scope;
static float frame_uv[8];
static uint32_t word(object self,const uint32_t *offset){uint32_t value=0;if(self)memcpy(&value,(void *)(self+*offset),4);return value;}
static const float *inset_texture(const float *uv,object texture){
    return texture?ws_atlas_cached(uv,(int)word(texture,texture_width),(int)word(texture,texture_height)):uv;
}
/* Equivalent to the two pinned triangle appenders, in their exact winding.
 * Capacity-edge cases retain the original partial/drop behavior. Flush,
 * pooling, bloom and texture selection remain with their original callers. */
static int append_quad(object self,const float *v,const float *t,const float *c,int varied){
    uint32_t n=word(self,num_tris),limit=word(self,max_tris);
    if(n>limit||limit-n<2||limit>1048576||!v||!t||(varied&&!c))return 0;
    float *vertices=(void *)(uintptr_t)word(self,vertex_buffer);
    float *uv=(void *)(uintptr_t)word(self,texcoord_buffer);
    float *colors=(void *)(uintptr_t)word(self,color_buffer);
    if(!vertices||!uv||(c&&!colors))return 0;
    vertices+=n*9;uv+=n*6;
    memcpy(vertices,v,9*sizeof(float));
    memcpy(vertices+9,v+6,3*sizeof(float));
    memcpy(vertices+12,v+3,3*sizeof(float));
    memcpy(vertices+15,v+9,3*sizeof(float));
    memcpy(uv,t,6*sizeof(float));
    memcpy(uv+6,t+4,2*sizeof(float));
    memcpy(uv+8,t+2,2*sizeof(float));
    memcpy(uv+10,t+6,2*sizeof(float));
    if(c){
        colors+=n*12;
        static const unsigned index[6]={0,1,2,2,1,3};
        for(unsigned i=0;i<6;++i)memcpy(colors+i*4,c+(varied?index[i]*4:0),4*sizeof(float));
    }
    *(uint32_t *)(self+*num_tris)=n+2;
    ++counters.quads;return 1;
}
static void draw_quad(object self,object sel,const float *vertices,const float *uv,const float *color){
    uv=inset_texture(uv,word(self,buffer_texture));
    if(append_quad(self,vertices,uv,color,0))return;
    ++counters.fallbacks;quad_color(self,sel,vertices,uv,color);
}
static void draw_quad_colors(object self,object sel,const float *vertices,const float *uv,const float *colors){
    uv=inset_texture(uv,word(self,buffer_texture));
    if(append_quad(self,vertices,uv,colors,1))return;
    ++counters.fallbacks;quad_colors(self,sel,vertices,uv,colors);
}
/* The particle engine emits filled fans/strips, often 10-14 vertices each.
 * The original makes a fresh ObjC triangle call and reloads buffer ivars for
 * every triangle. Append the identical vertex/color bytes in one pass.
 * Capacity truncation and strip winding deliberately match the APK; texture
 * coordinates, flush boundaries and effect simulation are untouched. */
static int append_poly(object self,const float *v,int count,float depth,const float *c,int strip,int varied){
    if(count<=2)return 1;
    uint32_t n=word(self,num_tris),limit=word(self,max_tris);
    if(n>=limit)return 1;
    if(!v||(varied&&!c)||limit>1048576)return 0;
    float *vertices=(void *)(uintptr_t)word(self,vertex_buffer);
    float *colors=(void *)(uintptr_t)word(self,color_buffer);
    if(!vertices||(c&&!colors))return 0;
    unsigned added=(unsigned)(count-2);if(added>limit-n)added=limit-n;
    vertices+=n*9;if(c)colors+=n*12;
    for(unsigned i=0;i<added;++i){
        unsigned indices[3]={strip?i:0,i+1,i+2};
        for(unsigned j=0;j<3;++j){
            unsigned k=indices[j];
            vertices[0]=v[k*2];vertices[1]=v[k*2+1];vertices[2]=depth;vertices+=3;
            if(c){memcpy(colors,c+(varied?k*4:0),4*sizeof(float));colors+=4;}
        }
    }
    *(uint32_t *)(self+*num_tris)=n+added;
    return 1;
}
static void draw_poly(object self,object sel,const float *v,int n,float z,const float *c){
    if(!append_poly(self,v,n,z,c,0,0))poly_color(self,sel,v,n,z,c);
}
static void draw_strip(object self,object sel,const float *v,int n,float z,const float *c){
    if(!append_poly(self,v,n,z,c,1,0))poly_strip(self,sel,v,n,z,c);
}
static void draw_poly_colors(object self,object sel,const float *v,int n,float z,const float *c){
    if(!append_poly(self,v,n,z,c,0,1))poly_colors(self,sel,v,n,z,c);
}
static void draw_frame(object self,object sel,int x,int y,int scale){
    object previous=frame_scope;frame_scope=word(self,frame_texture);
    frame_scale(self,sel,x,y,scale);frame_scope=previous;
}
static void draw_frame_size(object self,object sel,int x,int y,int width,int height,int scale){
    object previous=frame_scope;frame_scope=word(self,frame_texture);
    frame_size(self,sel,x,y,width,height,scale);frame_scope=previous;
}
const void *ws_atlas_texcoords(int size,unsigned type,int stride,const void *coordinates){
    /* These two FrameLayer methods use exactly four float2 vertices, confirmed
     * in the pinned APK. Never read arbitrary game client arrays as quads. */
    if(frame_scope&&size==2&&type==0x1406&&!stride&&coordinates){
        const float *adjusted=inset_texture(coordinates,frame_scope);
        if(adjusted!=coordinates){memcpy(frame_uv,adjusted,sizeof(frame_uv));return frame_uv;}
    }
    return coordinates;
}
int ws_atlas_attach(so_module *image,so_module *objc){
    if(!image||!objc||quad_color)return -1;
    object (*get_class)(const char *)=(void *)so_symbol(objc,"objc_getClass");
    object (*selector)(const char *)=(void *)so_symbol(objc,"sel_registerName");
    object (*method)(object,object)=(void *)so_symbol(objc,"class_getInstanceMethod");
    object (*imp)(object)=(void *)so_symbol(objc,"method_getImplementation");
    object (*replace)(object,object)=(void *)so_symbol(objc,"method_setImplementation");
    if(!get_class||!selector||!method||!imp||!replace)return -1;
#define IVAR(var,name) var=(void *)so_symbol(image,"OBJC_IVAR_$_" name);if(!var)return -1
    IVAR(buffer_texture,"RenderBuffer.texture");IVAR(frame_texture,"FrameLayer.texture");
    IVAR(texture_width,"TextureObject.width");IVAR(texture_height,"TextureObject.height");
    IVAR(num_tris,"RenderBuffer.numTris");IVAR(max_tris,"RenderBuffer.maxTris");
    IVAR(vertex_buffer,"RenderBuffer.vertexBuffer");IVAR(texcoord_buffer,"RenderBuffer.texCoordBuffer");
    IVAR(color_buffer,"RenderBuffer.colorBuffer");
#undef IVAR
    static const struct {const char *cls,*name;uint32_t address;} methods[]={
        {"RenderBuffer","addTexturedQuad3:Tex:Color:",0x245c1c},
        {"RenderBuffer","addTexturedQuad3:Tex:QuadColors:",0x245e84},
        {"FrameLayer","drawAtX:Y:Scale:",0x351004},
        {"FrameLayer","drawAtX:Y:Width:Height:Scale:",0x35159c},
        {"RenderBuffer","addPoly:N:Depth:Color:",0x2460a8},
        {"RenderBuffer","addPolyStrip:N:Depth:Color:",0x246128},
        {"RenderBuffer","addPoly:N:Depth:FullColor:",0x246320}
    };
    object handles[7];
    for(unsigned i=0;i<7;++i){
        handles[i]=method(get_class(methods[i].cls),selector(methods[i].name));
        if(!handles[i]||imp(handles[i])!=image->load_addr+methods[i].address)return -1;
    }
    object triangle=method(get_class("RenderBuffer"),selector("addTexturedTriangle3:T2:T3:TEX1:TEX2:TEX3:Color:"));
    object triangle_colors=method(get_class("RenderBuffer"),selector("addTexturedTriangle3:T2:T3:TEX1:TEX2:TEX3:C1:C2:C3:"));
    if(!triangle||!triangle_colors||imp(triangle)!=image->load_addr+0x245a44||
       imp(triangle_colors)!=image->load_addr+0x245cb0)return -1;
    triangle=method(get_class("RenderBuffer"),selector("addTriangle:T2:T3:Depth:Color:"));
    triangle_colors=method(get_class("RenderBuffer"),selector("addTriangle:T2:T3:Depth:C1:C2:C3:"));
    if(!triangle||!triangle_colors||imp(triangle)!=image->load_addr+0x245f48||
       imp(triangle_colors)!=image->load_addr+0x2461a4)return -1;
    quad_color=(void *)replace(handles[0],(object)draw_quad);
    quad_colors=(void *)replace(handles[1],(object)draw_quad_colors);
    frame_scale=(void *)replace(handles[2],(object)draw_frame);
    frame_size=(void *)replace(handles[3],(object)draw_frame_size);
    poly_color=(void *)replace(handles[4],(object)draw_poly);
    poly_strip=(void *)replace(handles[5],(object)draw_strip);
    poly_colors=(void *)replace(handles[6],(object)draw_poly_colors);
    return quad_color&&quad_colors&&frame_scale&&frame_size&&poly_color&&poly_strip&&poly_colors?0:-1;
}
