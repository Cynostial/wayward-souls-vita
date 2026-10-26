#include "ws_graphics.h"
#include "ws_eagl.h"
#include "ws_input.h"
#include "ws_quality.h"
#include "ws_fps.h"
#include "ws_render_rules.h"
#include "ws_loading.h"
#include "ws_atlas.h"
#include "ws_log.h"
#include <vitaGL.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <psp2/kernel/processmgr.h>

enum { DISPLAY_W=960, DISPLAY_H=544, VIRTUAL_W=1136, VIRTUAL_H=640 };
static int initialized;
static GLuint target_framebuffer;
static GLint virtual_viewport[4]={0,0,VIRTUAL_W,VIRTUAL_H};
static GLint virtual_scissor[4]={0,0,VIRTUAL_W,VIRTUAL_H};
static pthread_t render_thread;
static const unsigned char *game_data;
static uint32_t *ivar_width,*ivar_height,*ivar_framebuffer,*ivar_renderbuffer,*ivar_depth;
static unsigned draw_calls,draw_vertices;
static void ws_glDrawArrays(GLenum mode,GLint first,GLsizei count){
    ++draw_calls;if(count>0)draw_vertices+=(unsigned)count;glDrawArrays(mode,first,count);
}
static void ws_glDrawElements(GLenum mode,GLsizei count,GLenum type,const GLvoid *indices){
    ++draw_calls;if(count>0)draw_vertices+=(unsigned)count;glDrawElements(mode,count,type,indices);
}

static void screen_rectangle(const GLint in[4], GLint out[4]) {
    /* Preserve the game's 568:320 aspect ratio and logical coordinates.
     * Render directly to the display; no 1136x640 intermediate color buffer. */
    const int height=DISPLAY_W*VIRTUAL_H/VIRTUAL_W;
    const int bottom=(DISPLAY_H-height)/2;
    int x0=(int)((int64_t)in[0]*DISPLAY_W/VIRTUAL_W);
    int y0=(int)((int64_t)in[1]*height/VIRTUAL_H);
    int x1=(int)(((int64_t)in[0]+in[2])*DISPLAY_W/VIRTUAL_W);
    int y1=(int)(((int64_t)in[1]+in[3])*height/VIRTUAL_H);
    out[0]=x0;out[1]=bottom+y0;out[2]=x1-x0;out[3]=y1-y0;
}
static void apply_viewport(void) {
    GLint box[4];
    if(target_framebuffer) memcpy(box,virtual_viewport,sizeof(box));
    else screen_rectangle(virtual_viewport,box);
    glViewport(box[0],box[1],box[2],box[3]);
}
static void apply_scissor(void) {
    GLint box[4];
    if(target_framebuffer) memcpy(box,virtual_scissor,sizeof(box));
    else screen_rectangle(virtual_scissor,box);
    glScissor(box[0],box[1],box[2],box[3]);
}
static void ws_glViewport(GLint x,GLint y,GLsizei width,GLsizei height) {
    if(width<0 || height<0) { glViewport(x,y,width,height);return; }
    virtual_viewport[0]=x;virtual_viewport[1]=y;
    virtual_viewport[2]=width;virtual_viewport[3]=height;apply_viewport();
}
static void ws_glScissor(GLint x,GLint y,GLsizei width,GLsizei height) {
    if(width<0 || height<0) { glScissor(x,y,width,height);return; }
    virtual_scissor[0]=x;virtual_scissor[1]=y;
    virtual_scissor[2]=width;virtual_scissor[3]=height;apply_scissor();
}
static void ws_glBindFramebuffer(GLenum target,GLuint framebuffer) {
    glBindFramebuffer(target,framebuffer);
    if(target==GL_FRAMEBUFFER) {
        /* Query the actual binding: invalid GL names must not change state. */
        GLint actual=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&actual);
        target_framebuffer=(GLuint)actual;apply_viewport();apply_scissor();
    }
}
static void ws_glGetIntegerv(GLenum name,GLint *out) {
    if(!out)return;
    if(name==GL_VIEWPORT) memcpy(out,virtual_viewport,sizeof(virtual_viewport));
    else if(name==GL_SCISSOR_BOX) memcpy(out,virtual_scissor,sizeof(virtual_scissor));
    else glGetIntegerv(name,out);
}
static void ws_glTexParameteri(GLenum target,GLenum parameter,GLint value){
    glTexParameteri(target,parameter,ws_pixel_filter(target,parameter,value));
}
static void ws_glTexParameterf(GLenum target,GLenum parameter,GLfloat value){
    if(target==GL_TEXTURE_2D&&(parameter==GL_TEXTURE_MIN_FILTER||parameter==GL_TEXTURE_MAG_FILTER)){
        if(value>=GL_NEAREST&&value<=GL_LINEAR_MIPMAP_LINEAR)value=(GLfloat)ws_pixel_filter(target,parameter,(int)value);
    }
    glTexParameterf(target,parameter,value);
}
static void ws_glTexCoordPointer(GLint size,GLenum type,GLsizei stride,const GLvoid *pointer){
    glTexCoordPointer(size,type,stride,ws_atlas_texcoords(size,type,stride,pointer));
}
static int view_create_framebuffer(uintptr_t self,uintptr_t selector) {
    (void)selector;
    if(!initialized || !self)return 0;
    *(uint32_t *)(self+*ivar_framebuffer)=0;
    *(uint32_t *)(self+*ivar_renderbuffer)=0;
    *(uint32_t *)(self+*ivar_depth)=0;
    *(uint32_t *)(self+*ivar_width)=VIRTUAL_W;
    *(uint32_t *)(self+*ivar_height)=VIRTUAL_H;
    ws_glBindFramebuffer(GL_FRAMEBUFFER,0);
    ws_glViewport(0,0,VIRTUAL_W,VIRTUAL_H);
    return 1;
}
static void view_destroy_framebuffer(uintptr_t self,uintptr_t selector) {
    (void)selector;
    if(!self)return;
    /* The display surface belongs to VitaGL, not to the Android view. */
    *(uint32_t *)(self+*ivar_framebuffer)=0;
    *(uint32_t *)(self+*ivar_renderbuffer)=0;
    *(uint32_t *)(self+*ivar_depth)=0;
}
static int on_render_thread(void){return initialized&&pthread_equal(render_thread,pthread_self());}
void ws_graphics_loading_tick(void){
    static uint64_t last;
    if(!on_render_thread()||!game_data||target_framebuffer)return;
    uint32_t scene;memcpy(&scene,game_data,4);
    if(scene>=6||!ws_loading_active())return;
    uint64_t now=sceKernelGetProcessTimeWide();if(now-last<100000)return;
    last=now;
    /* A synchronous database load can block normal presentation for seconds.
     * Yield a completed frame from the SAME render owner before the title.
     * No input/ObjC callbacks or GL state changes are introduced here. The
     * CPU completion callback covers the whole frame with the loading image. */
    glClear(GL_COLOR_BUFFER_BIT);vglSwapBuffers(GL_FALSE);
}
static void present(void){
    static uint64_t previous,total_work,total_swap;
    static unsigned frames,buckets[5],peak_work,peak_swap,calls,vertices;
    static ws_atlas_stats prior_atlas;
    uint32_t scene=0;if(game_data)memcpy(&scene,game_data,4);
    ws_loading_handoff(scene>=6);
    ws_input_frame();ws_quality_present();
    uint64_t before=sceKernelGetProcessTimeWide();
    vglSwapBuffers(GL_FALSE);
    uint64_t after=sceKernelGetProcessTimeWide();
    if(scene==11&&previous){
        unsigned work=(unsigned)(before-previous),swap=(unsigned)(after-before),frame=(unsigned)(after-previous);
        total_work+=work;total_swap+=swap;++frames;
        if(work>peak_work)peak_work=work;
        if(swap>peak_swap)peak_swap=swap;
        ++buckets[frame<=17500?0:frame<=20000?1:frame<=25000?2:frame<=33334?3:4];
        calls+=draw_calls;vertices+=draw_vertices;
        if(frames==600){
            ws_atlas_stats current;ws_atlas_counters(&current);
            /* Swap includes submit/wait and is not an isolated GPU timing.
             * One queued record per 600 gameplay frames, no storage I/O here. */
            ws_log_print(4,"Vita frames","n=%u work_us avg=%llu peak=%u swap_us avg=%llu peak=%u bins17.5/20/25/33.3/over=%u/%u/%u/%u/%u draws_avg=%u vertices_avg=%u atlas_hit=%u miss=%u direct_quads=%u fallback=%u log_dropped=%u",
                frames,(unsigned long long)(total_work/frames),peak_work,(unsigned long long)(total_swap/frames),peak_swap,
                buckets[0],buckets[1],buckets[2],buckets[3],buckets[4],calls/frames,vertices/frames,
                current.hits-prior_atlas.hits,current.misses-prior_atlas.misses,current.quads-prior_atlas.quads,
                current.fallbacks-prior_atlas.fallbacks,ws_log_dropped());
            prior_atlas=current;frames=calls=vertices=peak_work=peak_swap=0;
            total_work=total_swap=0;memset(buckets,0,sizeof(buckets));
        }
    }
    previous=scene==11?after:0;draw_calls=draw_vertices=0;
}
int ws_graphics_init(void) {
    if(initialized)return 0;
    /* VitaGL returns whether resolution fallback occurred, not success/failure.
     * Reserve fixed pools instead of letting it consume all remaining RAM. */
    /* Install the CPU overlay before VitaGL starts its existing splash thread.
     * The renderer keeps its normal initialization and display ownership. */
    ws_fps_install();
    vglInitWithCustomSizes(2*1024*1024,DISPLAY_W,DISPLAY_H,
                          24*1024*1024,96*1024*1024,0,0,SCE_GXM_MULTISAMPLE_NONE);
    if(!glGetString(GL_VERSION))return -1;
    initialized=1;target_framebuffer=0;render_thread=pthread_self();
    ws_glViewport(0,0,VIRTUAL_W,VIRTUAL_H);
    ws_glScissor(0,0,VIRTUAL_W,VIRTUAL_H);
    /* Do not draw/clear yet: the first draw terminates VitaGL's own splash.
     * Keep its normal display updates alive through VM/bootstrap work. */
    glClearColor(0,0,0,1);
    return 0;
}
uintptr_t ws_graphics_symbol(const char *name) {
    if(!name || strncmp(name,"gl",2))return 0;
    if(!strcmp(name,"glDrawArrays"))return (uintptr_t)ws_glDrawArrays;
    if(!strcmp(name,"glDrawElements"))return (uintptr_t)ws_glDrawElements;
    if(!strcmp(name,"glViewport"))return (uintptr_t)ws_glViewport;
    if(!strcmp(name,"glScissor"))return (uintptr_t)ws_glScissor;
    if(!strcmp(name,"glGetIntegerv"))return (uintptr_t)ws_glGetIntegerv;
    if(!strcmp(name,"glTexParameteri"))return (uintptr_t)ws_glTexParameteri;
    if(!strcmp(name,"glTexParameterf"))return (uintptr_t)ws_glTexParameterf;
    if(!strcmp(name,"glTexCoordPointer"))return (uintptr_t)ws_glTexCoordPointer;
    if(!strcmp(name,"glBindFramebuffer") || !strcmp(name,"glBindFramebufferOES"))
        return (uintptr_t)ws_glBindFramebuffer;
    /* These OES functions have identical scalar/pointer signatures to the
     * corresponding core entrypoints. Do not strip arbitrary extension names. */
    static const char *aliases[]={"glGenFramebuffers","glDeleteFramebuffers",
        "glCheckFramebufferStatus","glFramebufferTexture2D","glFramebufferRenderbuffer",
        "glGenRenderbuffers","glDeleteRenderbuffers","glBindRenderbuffer",
        "glRenderbufferStorage","glGetRenderbufferParameteriv","glGenerateMipmap"};
    for(unsigned i=0;i<sizeof(aliases)/sizeof(*aliases);++i) {
        size_t len=strlen(aliases[i]);
        if(!strncmp(name,aliases[i],len) && !strcmp(name+len,"OES"))
            return (uintptr_t)vglGetProcAddress(aliases[i]);
    }
    return (uintptr_t)vglGetProcAddress(name);
}
int ws_graphics_attach(so_module *game,so_module *objc,unsigned *wrapped_functions) {
    if(!game || !objc || !game->dynsym || !game->dynstr)return -1;
    /* These symbol offsets fingerprint the inspected game version before any
     * patch is written. The APK importer also checks its complete SHA-256. */
    if(so_symbol(game,"main")!=game->load_addr+0x358f24 ||
       so_symbol(game,"drawTextureRect")!=game->load_addr+0x34fc44||
       so_symbol(game,"endMultiTex")!=game->load_addr+0x34e0d8)return -1;
    if(so_symbol(game,"game")!=game->load_addr+0x4ec998)return -1;
    game_data=(void *)(game->load_addr+0x4ec998);
    ivar_width=(void *)so_symbol(game,"OBJC_IVAR_$_EAGLView.backingWidth");
    ivar_height=(void *)so_symbol(game,"OBJC_IVAR_$_EAGLView.backingHeight");
    ivar_framebuffer=(void *)so_symbol(game,"OBJC_IVAR_$_EAGLView.viewFramebuffer");
    ivar_renderbuffer=(void *)so_symbol(game,"OBJC_IVAR_$_EAGLView.viewRenderbuffer");
    ivar_depth=(void *)so_symbol(game,"OBJC_IVAR_$_EAGLView.depthRenderbuffer");
    if(!ivar_width || !ivar_height || !ivar_framebuffer || !ivar_renderbuffer || !ivar_depth)return -1;
    ws_eagl_runtime runtime={
        .context_class=so_symbol(game,"OBJC_CLASS_$_EAGLContext"),
        .group_class=so_symbol(game,"OBJC_CLASS_$_EAGLSharegroup"),
        .api=(void *)so_symbol(game,"OBJC_IVAR_$_EAGLContext._api"),
        .group=(void *)so_symbol(game,"OBJC_IVAR_$_EAGLContext._sharegroup"),
        .internal=(void *)so_symbol(game,"OBJC_IVAR_$_EAGLContext._internal"),
        /* Private ivar offset variable, recovered from all three methods. */
        .invocations=(void *)(game->load_addr+0x4524e0),
        .send=(void *)so_symbol(objc,"objc_msgSend"),
        .super2=(void *)so_symbol(objc,"objc_msgSendSuper2"),
        .selector=(void *)so_symbol(objc,"sel_registerName"),
        .get_class=(void *)so_symbol(objc,"objc_getClass"),
        .render_thread=on_render_thread,.swap=present
    };
    if(ws_eagl_bind(&runtime))return -1;
    unsigned count=0;
    for(unsigned i=0;i<game->num_dynsym;++i) {
        const Elf32_Sym *symbol=&game->dynsym[i];
        const char *name=game->dynstr+symbol->st_name;
        if(symbol->st_shndx==SHN_UNDEF || ELF32_ST_TYPE(symbol->st_info)!=STT_FUNC ||
           strncmp(name,"__wrap_gl",9))continue;
        uintptr_t replacement=ws_graphics_symbol(name+7);
        if(replacement) { hook_addr(game->load_addr+symbol->st_value,replacement);++count; }
    }
    hook_addr(game->load_addr+0x3978b8,(uintptr_t)view_create_framebuffer);
    hook_addr(game->load_addr+0x398088,(uintptr_t)view_destroy_framebuffer);
    hook_addr(game->load_addr+0x10e624,(uintptr_t)ws_eagl_get);
    hook_addr(game->load_addr+0x10e824,(uintptr_t)ws_eagl_set);
    hook_addr(game->load_addr+0x10f05c,(uintptr_t)ws_eagl_present);
    hook_addr(game->load_addr+0x10ea60,(uintptr_t)ws_eagl_init);
    hook_addr(game->load_addr+0x10ef40,(uintptr_t)ws_eagl_dealloc);
    hook_addr(game->load_addr+0x10e68c,(uintptr_t)ws_eagl_internal);
    hook_addr(game->load_addr+0x10f270,(uintptr_t)ws_eagl_add);
    hook_addr(game->load_addr+0x10f3fc,(uintptr_t)ws_eagl_remove);
    hook_addr(game->load_addr+0x34e0d8,(uintptr_t)ws_end_multitex);
    so_flush_caches(game);
    if(wrapped_functions)*wrapped_functions=count;
    return 0;
}
