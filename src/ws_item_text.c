#include "ws_item_text.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
static void append(char *out,size_t cap,size_t *used,const char *s,size_t n){
    size_t room=cap-1-*used;if(n>room)n=room;
    memcpy(out+*used,s,n);*used+=n;out[*used]=0;
}
void ws_item_text(char *out,size_t cap,const char *description,const char *stats,
                  const float values[3],int quantity,int ammo,int tier){
    if(!out||!cap)return;
    out[0]=0;size_t used=0;
    static const char *tokens[]={"<magf0>","<magp0>","<magf1>","<magp1>","<magf2>","<magp2>","<magi>","<ammo>"};
    char replacements[8][64],heading[32];
    for(unsigned i=0;i<6;++i){
        double v=values[i/2];if(i&1)v*=100;
        if(isfinite(v))snprintf(replacements[i],64,(i&1)?"%.0f%%":"%.2f",v);
        else strcpy(replacements[i],"?");
    }
    snprintf(replacements[6],64,"%d",quantity);snprintf(replacements[7],64,"%d",ammo);
    if(tier>0){snprintf(heading,sizeof(heading),"Tier %d\n\n",tier);append(out,cap,&used,heading,strlen(heading));}
    const char *parts[]={stats,description};
    for(unsigned p=0;p<2;++p){
        const char *s=parts[p];if(!s||!*s)continue;
        if(p&&stats&&*stats)append(out,cap,&used,"\n\n",2);
        while(*s&&used+1<cap){
            unsigned t;for(t=0;t<8;++t){
                size_t n=strlen(tokens[t]);
                if(!strncmp(s,tokens[t],n)){append(out,cap,&used,replacements[t],strlen(replacements[t]));s+=n;break;}
            }
            if(t==8){append(out,cap,&used,s,1);++s;}
        }
    }
}
