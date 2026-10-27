#include "ws_controls.h"
#include <math.h>
static float clamp(float x,float a,float b){return x<a?a:x>b?b:x;}
static float axis(unsigned char x){int v=(int)x-128;return v>=-24&&v<=24?0:clamp((v+(v<0?24:-24))/103.0f,-1,1);}
void ws_controls_step(ws_controls *s,const ws_control_sample *p,const ws_control_actions *a){
    if(!s||!p||!a)return;
    uint32_t pressed=p->buttons&~s->buttons;
    int mode=p->gameplay?1:p->targeting?2:0;
    if(mode!=s->mode){
        if(s->attack&&a->attack)a->attack(a->context,-1);
        if(s->mode&&a->move)a->move(a->context,0,0);
        s->attack=0;s->direction=0;s->repeat=0;
    }
    float x=axis(p->lx),y=axis(p->ly);
    if(p->buttons&(WS_LEFT|WS_RIGHT))x=((p->buttons&WS_RIGHT)!=0)-((p->buttons&WS_LEFT)!=0);
    if(p->buttons&(WS_UP|WS_DOWN))y=((p->buttons&WS_DOWN)!=0)-((p->buttons&WS_UP)!=0);
    if(mode==1){
        if(pressed&(WS_START|WS_SELECT|WS_L)){
            if(s->attack&&a->attack)a->attack(a->context,-1);
            s->attack=0;if(a->move)a->move(a->context,0,0);
            if((pressed&WS_L)&&!(pressed&(WS_START|WS_SELECT))){if(a->quick_items)a->quick_items(a->context);}
            else if(a->back)a->back(a->context);
        }else{
            float length=sqrtf(x*x+y*y);if(length>1){x/=length;y/=length;}
            if(a->move)a->move(a->context,x,y);
            if(pressed&WS_CIRCLE){
                if(s->attack&&a->attack)a->attack(a->context,-1);
                s->attack=0;if(a->interact)a->interact(a->context);
            }else if(pressed&(WS_SQUARE|WS_TRIANGLE)){
                if(s->attack&&a->attack)a->attack(a->context,-1);
                s->attack=0;
                if(a->ability)a->ability(a->context,(pressed&WS_SQUARE)?0:1);
            }else if((pressed&WS_R)&&a->attack){a->attack(a->context,1);s->attack=1;}
            else if(s->attack){
                if(a->attack)a->attack(a->context,(p->buttons&WS_R)?2:0);
                if(!(p->buttons&WS_R))s->attack=0;
            }else if((pressed&WS_CROSS)&&a->attack){a->attack(a->context,1);a->attack(a->context,0);}
        }
    }else if(mode==2){
        if(a->aim)a->aim(a->context,x,y,isfinite(p->seconds)?clamp(p->seconds,0,.05f):0);
        if(pressed&(WS_CIRCLE|WS_START|WS_SELECT)){if(a->back)a->back(a->context);}
        else if((pressed&WS_CROSS)&&a->confirm)a->confirm(a->context);
    }else if(p->title){
        if((pressed&WS_START)&&a->confirm)a->confirm(a->context);
    }else if(pressed&(WS_CIRCLE|WS_START|WS_SELECT)){
        if(a->back)a->back(a->context);
    }else if(pressed&WS_L){
        if(a->quick_items)a->quick_items(a->context);
    }else if(pressed&WS_TRIANGLE){
        if(a->details)a->details(a->context);
    }else{
        int dir=fabsf(x)>=fabsf(y)?(x>.55f?2:x<-.55f?4:0):(y>.55f?3:y<-.55f?1:0);
        float dt=isfinite(p->seconds)?clamp(p->seconds,0,.05f):0;
        if(dir!=s->direction){s->direction=dir;s->repeat=.32f;if(dir&&a->navigate)a->navigate(a->context,dir);}
        else if(dir){s->repeat-=dt;if(s->repeat<=0){s->repeat=.12f;if(a->navigate)a->navigate(a->context,dir);}}
        if((pressed&WS_CROSS)&&a->confirm)a->confirm(a->context);
    }
    s->buttons=p->buttons;s->mode=mode;
}
