#include "ws_focus.h"
#include <float.h>
#include <math.h>
int ws_focus_initial(const ws_focus_item *items,unsigned count){
    int best=-1;float score=FLT_MAX;
    for(unsigned i=0;i<count;++i){
        float rank=(float)-items[i].preferred*100000+items[i].y*2+items[i].x;
        if(rank<score){score=rank;best=(int)i;}
    }
    return best;
}
int ws_focus_next(const ws_focus_item *items,unsigned count,int current,int direction){
    if(current<0||(unsigned)current>=count)return ws_focus_initial(items,count);
    const ws_focus_item *a=&items[current];float best_score=FLT_MAX;int best=current;
    for(unsigned i=0;i<count;++i){
        const ws_focus_item *b=&items[i];if((int)i==current)continue;
        float dx=(b->x+b->w/2)-(a->x+a->w/2),dy=(b->y+b->h/2)-(a->y+a->h/2);
        float forward=direction==1?-dy:direction==2?dx:direction==3?dy:-dx;
        if(forward<=1)continue;
        float side=(direction==1||direction==3)?fabsf(dx):fabsf(dy);
        float overlap=(direction==1||direction==3)?(a->w+b->w)/2:(a->h+b->h)/2;
        float score=forward+fmaxf(0,side-overlap)*6+side*.25f;
        if(a->group&&a->group==b->group)score*=.2f;
        if(score<best_score){best=(int)i;best_score=score;}
    }
    return best;
}
