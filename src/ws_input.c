#include "ws_input.h"
#include "ws_controls.h"
#include "ws_menu.h"
#include "ws_help.h"
#include "ws_lifecycle.h"
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <stdint.h>
#include <string.h>
typedef uintptr_t object;
typedef struct {float x,y;} point;
static unsigned char *game;
static object (*send)(object,object,...),(*selector)(const char *);
static object back_class,player;
static ws_controls controls;
static uint64_t last_sample;
static int attack_started;
enum {RETAIN,RELEASE,RUN,STOP,DOWN,HELD,UP,SWIPE,SHARED_BACK,GO_BACK,STATE,NEAR,SET_NEAR,SELECTORS};
static object sel[SELECTORS];
static const char *names[SELECTORS]={"retain","release","startRunning:","stopRunning","actionDown",
    "actionHeld:","actionUp:","actionSwipe:","sharedNoodleBackHandler","goBack","state","nearInteract","setNearInteract:"};
static object field(unsigned offset){uint32_t value;memcpy(&value,game+offset,4);return value;}
static void move(void *unused,float x,float y){
    (void)unused;if(!player)return;
    if(x||y){void (*run)(object,object,point)=(void *)send;run(player,sel[RUN],(point){x,y});}
    else send(player,sel[STOP]);
}
static void attack(void *unused,int phase){
    (void)unused;if(!player)return;
    if(phase==1){
        /* The verified original actionDown only checks nearInteract before
         * writing attackReady/didAttack. Suppress that branch for attacks;
         * Circle uses the original interaction branch below. */
        object near=send(player,sel[NEAR]);
        send(player,sel[SET_NEAR],0);
        attack_started=send(player,sel[DOWN])!=0;
        send(player,sel[SET_NEAR],near);
    }
    else if(phase==2){if(attack_started)attack_started=send(player,sel[HELD],1)!=0;}
    else {if(attack_started)send(player,sel[UP],phase==0);attack_started=0;}
}
static void ability(void *unused,int kind){
    /* actionSwipe: takes "swipe upward", not an ability index. The native
     * action pad passes true when dy <= 0; Square is the upward/offense swipe. */
    (void)unused;if(player){send(player,sel[SWIPE],kind==0);attack_started=0;}
}
static void back(void *unused){
    (void)unused;if(ws_menu_close_details())return;
    object handler=send(back_class,sel[SHARED_BACK]);
    if(handler&&send(handler,sel[STATE])){
        int manual=field(0)==11&&field(52)==0;
        if(manual)ws_lifecycle_request_checkpoint(1);
        send(handler,sel[GO_BACK]);
        if(manual)ws_lifecycle_request_checkpoint(0);
    }
}
static void interact(void *unused){
    (void)unused;
    if(player&&send(player,sel[NEAR]))send(player,sel[DOWN]);
}
static void quick_items(void *unused){
    (void)unused;
    if(ws_menu_quick_items()==2)back(NULL);
}
static const ws_control_actions actions={NULL,move,attack,ability,back,ws_menu_navigate,ws_menu_confirm,ws_menu_aim,interact,quick_items,ws_menu_details};
void ws_input_frame(void){
    if(!game)return;
    if(!sel[0])for(unsigned i=0;i<SELECTORS;++i)sel[i]=selector(names[i]);
    SceCtrlData pad={0};if(sceCtrlPeekBufferPositive(0,&pad,1)<=0)return;
    if(ws_lifecycle_input_blocked()){
        attack(NULL,-1);move(NULL,0,0);memset(&controls,0,sizeof(controls));
        controls.buttons=pad.buttons;last_sample=sceKernelGetProcessTimeWide();return;
    }
    if(ws_help_active()){
        uint32_t pressed=pad.buttons&~controls.buttons;
        if(pressed&(WS_CIRCLE|WS_START|WS_SELECT))ws_help_show(0);
        controls.buttons=pad.buttons;last_sample=sceKernelGetProcessTimeWide();return;
    }
    uint64_t now=sceKernelGetProcessTimeWide();
    float seconds=last_sample?(float)(now-last_sample)*.000001f:1.0f/60;last_sample=now;
    if(!field(120)){controls.buttons=pad.buttons;return;}
    object current=field(132);
    if(current!=player){
        attack(NULL,-1);move(NULL,0,0);controls.attack=0;controls.mode=0;
        if(player)send(player,sel[RELEASE]);
        player=current;
        if(player)send(player,sel[RETAIN]);
    }
    int gameplay=field(0)==11&&field(52)==0&&player!=0;
    ws_menu_frame(field(120),gameplay);
    ws_control_sample sample={pad.buttons,pad.lx,pad.ly,gameplay,seconds,(float)field(72),(float)field(76),ws_menu_title(),ws_menu_targeting()};
    ws_controls_step(&controls,&sample,&actions);
    /* A pause/confirm can change the active layer during this input sample. */
    ws_menu_frame(field(120),field(0)==11&&field(52)==0&&field(132));
    ws_menu_draw();
}
int ws_input_attach(so_module *image,so_module *objc){
    if(!image||!objc||game)return -1;
    object global=so_symbol(image,"game");if(global!=image->load_addr+0x4ec998)return -1;
    send=(void *)so_symbol(objc,"objc_msgSend");selector=(void *)so_symbol(objc,"sel_registerName");
    back_class=so_symbol(image,"OBJC_CLASS_$_NoodleBackHandler");
    if(!send||!selector||!back_class||ws_menu_bind(image,objc))return -1;
    game=(void *)global;return 0;
}
