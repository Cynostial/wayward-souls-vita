#ifndef WS_EAGL_H
#define WS_EAGL_H
#include <stdint.h>
typedef struct { uintptr_t receiver, current_class; } ws_objc_super;
typedef struct {
    uintptr_t context_class, group_class;
    const uint32_t *api, *group, *internal, *invocations;
    uintptr_t (*send)(uintptr_t,uintptr_t,...);
    uintptr_t (*super2)(ws_objc_super *,uintptr_t,...);
    uintptr_t (*selector)(const char *);
    uintptr_t (*get_class)(const char *);
    int (*render_thread)(void);
    void (*swap)(void);
} ws_eagl_runtime;
/* Binds runtime entrypoints without messaging classes before ObjC constructors.
 * Supports the game's GLES1 context on the VitaGL render thread. Independent
 * GL contexts and background context ownership are deliberately rejected. */
int ws_eagl_bind(const ws_eagl_runtime *runtime);
uintptr_t ws_eagl_init(uintptr_t self,uintptr_t selector,int api,uintptr_t group);
void ws_eagl_dealloc(uintptr_t self,uintptr_t selector);
uintptr_t ws_eagl_internal(uintptr_t self,uintptr_t selector);
int ws_eagl_set(uintptr_t cls,uintptr_t selector,uintptr_t context);
uintptr_t ws_eagl_get(uintptr_t cls,uintptr_t selector);
int ws_eagl_present(uintptr_t self,uintptr_t selector,unsigned target);
void ws_eagl_add(uintptr_t self,uintptr_t selector,uintptr_t invocation);
void ws_eagl_remove(uintptr_t self,uintptr_t selector,uintptr_t invocation);
#endif
