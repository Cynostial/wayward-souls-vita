#include "ws_profile.h"
#include "ws_log.h"
#include "ws_lifecycle.h"
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <string.h>
typedef uintptr_t object;
typedef void (*method_fn)(object,object);
static unsigned char *game;
enum {VIEW,LOOP,LEVEL_UPDATE,LEVEL_DRAW,GUI_DRAW,ROOT_UPDATE,PARTICLES,COUNT};
static method_fn original[COUNT];
static uint64_t totals[COUNT],frame_cost[COUNT];
static unsigned peaks[COUNT],frames;
static int active;
void ws_profile_mark(const char *phase){
    static int first=1;
    FILE *f=fopen("ux0:data/wayward/startup.log",first?"w":"a");
    if(f){fprintf(f,"%llu ms %s\n",(unsigned long long)(sceKernelGetProcessTimeWide()/1000),phase);fclose(f);first=0;}
}
static void measured(unsigned index,object self,object sel){
    if(!active){original[index](self,sel);return;}
    uint64_t start=sceKernelGetProcessTimeWide();original[index](self,sel);
    frame_cost[index]+=sceKernelGetProcessTimeWide()-start;
}
static void frame(object self,object sel){
    static uint32_t last_scene=UINT32_MAX;
    uint32_t scene;memcpy(&scene,game,4);
    if(scene!=last_scene){char mark[48];snprintf(mark,sizeof(mark),"scene %u",scene);ws_profile_mark(mark);last_scene=scene;}
    active=scene==11;memset(frame_cost,0,sizeof(frame_cost));
    measured(VIEW,self,sel);
    if(active){
        for(unsigned i=0;i<COUNT;++i){totals[i]+=frame_cost[i];if(frame_cost[i]>peaks[i])peaks[i]=(unsigned)frame_cost[i];}
        if(++frames==600){
            ws_log_print(4,"Vita CPU","n=600 inclusive_us avg/peak frame=%llu/%u loop=%llu/%u level_update=%llu/%u level_draw=%llu/%u gui=%llu/%u root=%llu/%u particles=%llu/%u",
                (unsigned long long)(totals[VIEW]/600),peaks[VIEW],(unsigned long long)(totals[LOOP]/600),peaks[LOOP],
                (unsigned long long)(totals[LEVEL_UPDATE]/600),peaks[LEVEL_UPDATE],(unsigned long long)(totals[LEVEL_DRAW]/600),peaks[LEVEL_DRAW],
                (unsigned long long)(totals[GUI_DRAW]/600),peaks[GUI_DRAW],(unsigned long long)(totals[ROOT_UPDATE]/600),peaks[ROOT_UPDATE],
                (unsigned long long)(totals[PARTICLES]/600),peaks[PARTICLES]);
            frames=0;memset(totals,0,sizeof(totals));memset(peaks,0,sizeof(peaks));
        }
    }
    active=0;
}
#define WRAPPER(name,index) static void name(object self,object sel){measured(index,self,sel);}
WRAPPER(loop,LOOP)
WRAPPER(update,LEVEL_UPDATE)
WRAPPER(draw,LEVEL_DRAW)
WRAPPER(gui,GUI_DRAW)
WRAPPER(root,ROOT_UPDATE)
static void particles(object self,object sel,int layer){
    void (*draw_particles)(object,object,int)=(void *)original[PARTICLES];
    if(!active){draw_particles(self,sel,layer);return;}
    uint64_t start=sceKernelGetProcessTimeWide();draw_particles(self,sel,layer);
    frame_cost[PARTICLES]+=sceKernelGetProcessTimeWide()-start;
}
int ws_profile_attach(so_module *image,so_module *objc){
    object (*cls)(const char *)=(void *)so_symbol(objc,"objc_getClass");
    object (*sel)(const char *)=(void *)so_symbol(objc,"sel_registerName");
    object (*method)(object,object)=(void *)so_symbol(objc,"class_getInstanceMethod");
    object (*imp)(object)=(void *)so_symbol(objc,"method_getImplementation");
    object (*replace)(object,object)=(void *)so_symbol(objc,"method_setImplementation");
    if(!cls||!sel||!method||!imp||!replace)return -1;
    static const struct {const char *cls,*name;uint32_t offset;object hook;} hooks[]={
        {"EAGLView","drawView",0x395ef4,(object)frame},{"EAGLView","drawGameLoop",0x396688,(object)loop},
        {"Level","update",0x3898ac,(object)update},{"Level","draw",0x382870,(object)draw},
        {"GUIManager","draw",0x29c154,(object)gui},{"Root","update",0x24750c,(object)root},
        {"ParticleEngine","draw:",0x2c7568,(object)particles}
    };
    object methods[COUNT];
    for(unsigned i=0;i<COUNT;++i){
        methods[i]=method(cls(hooks[i].cls),sel(hooks[i].name));
        object expected=i==VIEW?ws_lifecycle_draw_hook():image->load_addr+hooks[i].offset;
        if(!methods[i]||imp(methods[i])!=expected)return -1;
    }
    game=(void *)so_symbol(image,"game");if(!game)return -1;
    for(unsigned i=0;i<COUNT;++i)original[i]=(method_fn)replace(methods[i],(object)hooks[i].hook);
    return 0;
}
