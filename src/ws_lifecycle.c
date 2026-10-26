#include "ws_lifecycle.h"
#include <stdint.h>
#include <string.h>
typedef uintptr_t object;
static object (*send)(object,object,...),(*selector)(const char *);
static void (*pause_game)(object,object),(*full_pause)(object,object),(*unpause)(object,object,signed char);
static void (*draw_view)(object,object);
static unsigned char *game;
static uint32_t *save_data;
static int overlay,checkpointing,input_blocked;
static unsigned checkpoint_request;
static int checkpoint_done;
void ws_lifecycle_request_checkpoint(int begin){
    if(begin){if(!checkpoint_request++)checkpoint_done=0;}
    else if(checkpoint_request)--checkpoint_request;
}
static uint32_t field(unsigned offset){uint32_t v;memcpy(&v,game+offset,4);return v;}
static void checkpoint(void){
    if(!checkpoint_request||checkpoint_done||checkpointing||!game||field(0)!=11||!field(104)||!field(128)||!field(132)||!field(140))return;
    checkpoint_done=1;
    checkpointing=1;
    /* Original serializer/checksum, including its ten-second post-load guard.
     * Called on the owner thread with gameplay already paused. */
    object settings=field(104);
    send(settings,selector("saveGameState"));
    send(settings,selector("save"));send(settings,selector("doSaveNow"));
    checkpointing=0;
}
static void paused(object self,object sel){pause_game(self,sel);checkpoint();}
static void fully_paused(object self,object sel){full_pause(self,sel);checkpoint();}
void ws_lifecycle_discard_checkpoint(void){
    if(game&&field(104)){
        object settings=field(104);uint32_t data;memcpy(&data,(void *)(settings+*save_data),4);
        if(data){
            /* Match the game's applicationDidBecomeActive stale-save cleanup:
             * once play resumes, an old checkpoint must not resurrect a death. */
            send(settings,selector("setSaveStateData:"),0);
            send(settings,selector("setSaveStateChecksum:"),0);
            send(settings,selector("save"));send(settings,selector("doSaveNow"));
        }
    }
}
static void resumed(object self,object sel,signed char flag){
    unpause(self,sel,flag);
    if(game&&field(0)==11&&(field(52)==0||field(52)==3))ws_lifecycle_discard_checkpoint();
}
static void draw(object self,object sel){
    /* Handle resume before the original loop can advance enemies/projectiles. */
    input_blocked=ws_lifecycle_poll();draw_view(self,sel);
}
uintptr_t ws_lifecycle_draw_hook(void){return (uintptr_t)draw;}
int ws_lifecycle_input_blocked(void){return input_blocked;}
int ws_lifecycle_signal(unsigned events,int system_overlay){
    if(!game)return 0;
    int interrupt=events!=0||(system_overlay&&!overlay);
    overlay=system_overlay!=0;
    if(interrupt&&field(0)==11&&field(120)){
        /* Only active play or script control becomes a full pause. Inventory,
         * death, targeting and other modal screens keep their original state. */
        if(field(52)==0||field(52)==3){
            ws_lifecycle_request_checkpoint(1);
            send(field(120),selector("fullPause"));
            ws_lifecycle_request_checkpoint(0);
        }
    }
    return interrupt||overlay;
}
int ws_lifecycle_attach(so_module *image,so_module *objc){
    if(!image||!objc||game)return -1;
    object (*cls)(const char *)=(void *)so_symbol(objc,"objc_getClass");
    object (*method)(object,object)=(void *)so_symbol(objc,"class_getInstanceMethod");
    object (*imp)(object)=(void *)so_symbol(objc,"method_getImplementation");
    object (*replace)(object,object)=(void *)so_symbol(objc,"method_setImplementation");
    send=(void *)so_symbol(objc,"objc_msgSend");selector=(void *)so_symbol(objc,"sel_registerName");
    if(!cls||!method||!imp||!replace||!send||!selector)return -1;
    object global=so_symbol(image,"game");
    save_data=(void *)so_symbol(image,"OBJC_IVAR_$_Settings.saveStateData");
    if(global!=image->load_addr+0x4ec998||!save_data)return -1;
    const char *names[]={"pause","fullPause","unpause:","saveGameState","doSaveNow"};
    const uint32_t addresses[]={0x29d234,0x29d278,0x29d3a8,0x27abb4,0x278c88};
    object handles[5];
    for(unsigned i=0;i<5;++i){
        handles[i]=method(cls(i<3?"GUIManager":"Settings"),selector(names[i]));
        if(!handles[i]||imp(handles[i])!=image->load_addr+addresses[i])return -1;
    }
    object view=method(cls("EAGLView"),selector("drawView"));
    if(!view||imp(view)!=image->load_addr+0x395ef4)return -1;
    game=(void *)global;
    pause_game=(void *)replace(handles[0],(object)paused);
    full_pause=(void *)replace(handles[1],(object)fully_paused);
    unpause=(void *)replace(handles[2],(object)resumed);
    draw_view=(void *)replace(view,(object)draw);
    return pause_game&&full_pause&&unpause&&draw_view?0:-1;
}
