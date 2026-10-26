#include "ws_lifecycle.h"
#include <psp2/appmgr.h>
#include <psp2/power.h>
#include <psp2/kernel/threadmgr.h>
#include <stdatomic.h>
static atomic_uint notifications;
static int power_callback(int id,int count,int flags,void *context){
    (void)id;(void)count;(void)context;
    unsigned event=0;
    if(flags&(SCE_POWER_CB_SYSTEM_SUSPEND|SCE_POWER_CB_APP_SUSPEND))event|=WS_LIFE_INTERRUPT;
    if(flags&(SCE_POWER_CB_SYSTEM_RESUME|SCE_POWER_CB_APP_RESUME))event|=WS_LIFE_RESUME;
    atomic_fetch_or(&notifications,event);return 0;
}
int ws_lifecycle_poll(void){
    static int registered,suspend_pending,overlay;
    if(!registered){
        registered=1;
        int cb=sceKernelCreateCallback("Wayward lifecycle",0,power_callback,NULL);
        if(cb>=0&&scePowerRegisterCallback(cb)<0)sceKernelDeleteCallback(cb);
    }
    /* Check on the game owner. The callback only queues flags, never enters
     * ObjC, touches GL or saves from another thread. */
    sceKernelCheckCallback();
    unsigned events=atomic_exchange(&notifications,0);
    int suspending=scePowerIsSuspendRequired();
    if(suspending>0&&!suspend_pending)events|=WS_LIFE_INTERRUPT;
    suspend_pending=suspending>0;
    SceAppMgrAppState state={0};
    if(_sceAppMgrGetAppState(&state,sizeof(state),0)>=0){
        overlay=state.isSystemUiOverlaid!=0;
        for(unsigned i=0;i<state.systemEventNum&&i<8;++i){
            SceAppMgrSystemEvent event={0};
            if(sceAppMgrReceiveSystemEvent(&event)<0)break;
            if(event.systemEvent==SCE_APPMGR_SYSTEMEVENT_ON_RESUME)events|=WS_LIFE_RESUME;
        }
    }
    return ws_lifecycle_signal(events,overlay);
}
