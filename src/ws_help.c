/* A CPU-only controller reference on the completed display buffer. It cannot
 * disturb the game's texture/client-array state. No loading/display thread. */
#include "ws_help.h"
#include <stdatomic.h>
#include <stddef.h>
static atomic_int active;
void ws_help_show(int value){atomic_store(&active,value!=0);}
int ws_help_active(void){return atomic_load(&active);}
static const unsigned char letters[26][7]={
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
 {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
 {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
 {7,2,2,2,2,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
 {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
 {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
 {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
};
static void box(uint32_t *p,unsigned stride,unsigned x,unsigned y,unsigned w,unsigned h,uint32_t color){
    for(unsigned j=0;j<h;++j)for(unsigned i=0;i<w;++i)p[(size_t)(y+j)*stride+x+i]=color;
}
static void text(uint32_t *p,unsigned stride,unsigned x,unsigned y,const char *s,unsigned scale,uint32_t color){
    for(;*s;++s,x+=6*scale)for(unsigned row=0;row<7;++row){
        unsigned bits=*s>='A'&&*s<='Z'?letters[*s-'A'][row]:*s=='/'?(1u<<(row*4/6)):
            *s=='-'&&row==3?14:*s==':'&&(row==2||row==5)?4:*s==','&&row>=5?4:0;
        for(unsigned col=0;col<5;++col)if(bits&(16u>>col))box(p,stride,x+col*scale,y+row*scale,scale,scale,color);
    }
}
int ws_help_draw(uint32_t *p,unsigned w,unsigned h,unsigned stride){
    if(!ws_help_active()||!p||w<900||h<500||stride<w)return 0;
    unsigned x=(w-840)/2,y=(h-456)/2;
    box(p,stride,x,y,840,456,0xff1f1714);
    box(p,stride,x,y,840,2,0xff7998b7);box(p,stride,x,y+454,840,2,0xff7998b7);
    text(p,stride,x+32,y+24,"VITA CONTROLS",3,0xffb1d4ef);
    static const char *rows[][2]={
        {"MOVE","LEFT STICK / D-PAD"},{"ATTACK","X"},
        {"POWER ATTACK","HOLD R, THEN RELEASE"},{"OFFENSIVE ABILITY","SQUARE"},
        {"DEFENSIVE ABILITY","TRIANGLE"},{"INVENTORY / PAUSE","START / SELECT"},
        {"USE AN ITEM","INVENTORY: X, THEN X AGAIN"},{"AIM AN ITEM","STICK / D-PAD, X TO CAST"},
        {"CONFIRM / BACK","X / CIRCLE"}
    };
    for(unsigned i=0;i<9;++i){
        text(p,stride,x+32,y+86+i*32,rows[i][0],2,0xff9b9d9e);
        text(p,stride,x+350,y+86+i*32,rows[i][1],2,0xffe7edf0);
    }
    box(p,stride,x+32,y+392,776,1,0xff514238);
    text(p,stride,x+32,y+416,"CIRCLE / START: BACK",2,0xffb1d4ef);
    return 1;
}
