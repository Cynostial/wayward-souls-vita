#include "ws_setup.h"
#include "ws_setup_screen.h"
#include "ws_setup_display.h"
#include "../src/ws_loading.h"
#include <psp2/appmgr.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/vshbridge.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static unsigned previous;
static uint64_t last_draw,last_tick;
static int last_percent=-1;
static char last_phase[32];
static unsigned buttons(void){
    SceCtrlData pad={0};if(sceCtrlPeekBufferPositive(0,&pad,1)<0)return 0;
    unsigned edge=pad.buttons&~previous;previous=pad.buttons;return edge;
}
static void present(const char *message,unsigned done,unsigned total,int mode){
    if(ws_setup_present(message,done,total,mode)<0){
        fputs("SETUP ERROR: display presentation failed.\n",stderr);sceKernelExitProcess(1);
    }
}
static int progress(const char *phase,uint64_t done,uint64_t total,void *unused){
    (void)unused;uint64_t now=sceKernelGetProcessTimeWide();
    int percent=total?(int)(done*100/total):0,changed=strcmp(phase,last_phase)!=0;
    if(changed||(percent!=last_percent&&(now-last_draw>=100000||done==total))){
        present(phase,(unsigned)percent,100,1);last_percent=percent;last_draw=now;
        if(changed){snprintf(last_phase,sizeof(last_phase),"%s",phase);fprintf(stderr,"SETUP: %s\n",phase);}
    }
    if(now-last_tick>=1000000){sceKernelPowerTick(0);last_tick=now;}
    return (buttons()&SCE_CTRL_CIRCLE)!=0;
}
static void artwork(void){
    /* CPU-only setup runs BEFORE all game/GXM threads. The original tower art
     * is already packaged for the shell; no user data is needed to show setup. */
    uint32_t *art=calloc(960u*544u,4);if(!art)return;
    for(unsigned i=0;i<960u*544u;++i)art[i]=0xff000000;
    FILE *f=fopen("app0:art/setup.png","rb");
    if(f){
        if(!fseek(f,0,SEEK_END)){
            long size=ftell(f);
            if(size>=33&&size<=512*1024&&!fseek(f,0,SEEK_SET)){
                unsigned char *png=malloc((size_t)size);
                if(png&&fread(png,1,(size_t)size,f)==(size_t)size)
                    ws_loading_image(png,(size_t)size,art+480,448,544,960);
                free(png);
            }
        }
        fclose(f);
    }
    ws_setup_screen_art(art); /* Retained only in this installer process. */
}
void ws_setup_on_vita(void){
    int module[2]={0};const char *prerequisite=NULL;
    if(_vshKernelSearchModuleByName("kubridge",module)<0)
        prerequisite="Install and enable kubridge, then restart your Vita.";
    if(!prerequisite&&ws_setup_ready(WS_SETUP_ROOT))return;
    /* Hold both CPU scanout buffers for this process. We never initialize
     * VitaGL here or release a displayed surface underneath the kernel. START
     * after completion replaces the process before game allocations begin. */
    uint32_t *screen=NULL;
    SceUID block=sceKernelAllocMemBlock("WS setup screens",SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,4u*1024u*1024u,NULL);
    if(block<0||sceKernelGetMemBlockBase(block,(void **)&screen)<0){
        fputs("SETUP ERROR: cannot allocate display buffers.\n",stderr);sceKernelExitProcess(1);
    }
    ws_setup_display_init(screen,screen+(2u*1024u*1024u)/sizeof(*screen));
    artwork();sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
    ws_setup_result result={0};int complete=0;
    for(;;){
        present(prerequisite?prerequisite:result.message,0,0,prerequisite?3:complete?2:0);
        unsigned edge=buttons();
        if(edge&SCE_CTRL_CIRCLE){fflush(stderr);sceKernelExitProcess(0);}
        if(!prerequisite&&(edge&SCE_CTRL_START)){
            if(complete){
                fflush(stderr);
                int rc=sceAppMgrLoadExec("app0:eboot.bin",NULL,NULL);
                snprintf(result.message,sizeof(result.message),"Close and reopen the game to play. (%X)",rc);
            }else if(!ws_setup_find(WS_SETUP_ROOT,&result)){
                uint64_t max,free_bytes,need=ws_setup_space(WS_SETUP_ROOT);
                if(sceAppMgrGetDevInfo("ux0:",&max,&free_bytes)<0)
                    snprintf(result.message,sizeof(result.message),"Cannot check storage space. Reopen the game and retry.");
                else if(free_bytes<need)
                    snprintf(result.message,sizeof(result.message),"Free %u MB more, then press START.",(unsigned)((need-free_bytes+1048575)/1048576));
                else{
                    last_phase[0]=0;last_percent=-1;last_draw=last_tick=0;
                    uint64_t begin=sceKernelGetProcessTimeWide();
                    complete=!ws_setup_run(WS_SETUP_ROOT,result.apk,progress,NULL,&result);
                    FILE *record=fopen(WS_SETUP_ROOT "/setup.log","a");
                    if(record){
                        fprintf(record,"00.13 complete=%d files=%u reused=%u elapsed_ms=%llu; %s\n",complete,
                            result.completed,result.reused,(unsigned long long)((sceKernelGetProcessTimeWide()-begin)/1000),result.message);
                        fclose(record);
                    }
                    fprintf(stderr,"SETUP: complete=%d files=%u reused=%u elapsed=%llu ms; %s\n",complete,
                        result.completed,result.reused,(unsigned long long)((sceKernelGetProcessTimeWide()-begin)/1000),result.message);
                }
                previous=~0u;
            }
        }
        sceDisplayWaitVblankStart();sceKernelDelayThread(16000);
    }
}
