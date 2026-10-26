#include "ws_eagl.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
static ws_eagl_runtime rt;
static uintptr_t live, current;
static uintptr_t *field(uintptr_t self,const uint32_t *offset){return (uintptr_t *)(self+*offset);}
static uintptr_t sel(const char *name){return rt.selector(name);}
static uintptr_t send0(uintptr_t object,const char *name){return object?rt.send(object,sel(name)):0;}
static int owner(void){return rt.render_thread && rt.render_thread();}
int ws_eagl_bind(const ws_eagl_runtime *runtime) {
    if(live||current||!runtime||!runtime->context_class||!runtime->group_class||
       !runtime->api||!runtime->group||!runtime->internal||!runtime->invocations||
       !runtime->send||!runtime->super2||!runtime->selector||!runtime->get_class||
       !runtime->render_thread||!runtime->swap)return -1;
    rt=*runtime;return 0;
}
uintptr_t ws_eagl_init(uintptr_t self,uintptr_t selector,int api,uintptr_t group) {
    (void)selector;
    if(!self)return 0;
    if(!owner()||api!=1||live||group){
        fputs("Wayward: unsupported EAGL context/API/thread\n",stderr);
        send0(self,"release");return 0;
    }
    ws_objc_super parent={self,rt.context_class};
    self=rt.super2(&parent,sel("init"));if(!self)return 0;
    *(int32_t *)(self+*rt.api)=api;
    /* EAGLSharegroup's original init retains [context _internal]. Supply a
     * normal ObjC object as an opaque VitaGL lifetime token, never an Android
     * EGL object. Its original dealloc releases that token and its allocation. */
    uintptr_t token=send0(send0(rt.get_class("NSObject"),"alloc"),"init");
    if(!token){send0(self,"release");return 0;}
    *field(self,rt.internal)=token;
    uintptr_t shared=send0(rt.group_class,"alloc");
    if(shared)shared=rt.send(shared,sel("initWithContext:"),self);
    if(!shared){send0(self,"release");return 0;}
    *field(self,rt.group)=shared;live=self;return self;
}
void ws_eagl_dealloc(uintptr_t self,uintptr_t selector) {
    (void)selector;if(!self)return;
    if(current==self)current=0;
    if(live==self)live=0;
    uintptr_t group=*field(self,rt.group), token=*field(self,rt.internal), pending=*field(self,rt.invocations);
    *field(self,rt.group)=*field(self,rt.internal)=*field(self,rt.invocations)=0;
    send0(pending,"release");send0(group,"release");send0(token,"release");
    ws_objc_super parent={self,rt.context_class};rt.super2(&parent,sel("dealloc"));
}
uintptr_t ws_eagl_internal(uintptr_t self,uintptr_t selector) {
    (void)selector;return self?*field(self,rt.internal):0;
}
int ws_eagl_set(uintptr_t cls,uintptr_t selector,uintptr_t context) {
    (void)cls;(void)selector;
    if(!owner()||(context&&context!=live))return 0;
    if(current!=context){uintptr_t old=current;send0(context,"retain");current=context;send0(old,"release");}
    return 1;
}
uintptr_t ws_eagl_get(uintptr_t cls,uintptr_t selector) {
    (void)cls;(void)selector;return owner()?current:0;
}
int ws_eagl_present(uintptr_t self,uintptr_t selector,unsigned target) {
    (void)selector;
    if(!owner()||!self||self!=current||target!=0x8d41)return 0; /* GL_RENDERBUFFER_OES */
    /* Invocations may remove themselves; enumerate a retained snapshot. Keep
     * registered invocations for subsequent frames, as the APK does. */
    uintptr_t snapshot=send0(*field(self,rt.invocations),"copy");
    uintptr_t count=send0(snapshot,"count");
    for(uintptr_t i=0;i<count;++i)send0(rt.send(snapshot,sel("objectAtIndex:"),i),"invoke");
    send0(snapshot,"release");rt.swap();return 1;
}
void ws_eagl_add(uintptr_t self,uintptr_t selector,uintptr_t invocation) {
    (void)selector;if(!owner()||!self||self!=live||!invocation)return;
    uintptr_t *pending=field(self,rt.invocations);
    if(!*pending)*pending=send0(send0(rt.get_class("NSMutableArray"),"alloc"),"init");
    if(*pending)rt.send(*pending,sel("addObject:"),invocation);
}
void ws_eagl_remove(uintptr_t self,uintptr_t selector,uintptr_t invocation) {
    (void)selector;if(!owner()||!self||self!=live||!invocation)return;
    uintptr_t pending=*field(self,rt.invocations);
    if(pending)rt.send(pending,sel("removeObject:"),invocation);
}
