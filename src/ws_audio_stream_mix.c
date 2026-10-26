#include "ws_audio_stream_mix.h"
#include <math.h>
#include <string.h>
int ws_stream_mixer_init(ws_stream_mixer *m,unsigned rate,unsigned channels,ws_pcm_reader read,void *context){
    if(!m||!read||(channels!=1&&channels!=2))return -1;
    ws_pcm_cursor cursor={0};if(ws_pcm_pitch(&cursor,rate,1.0f))return -1;
    memset(m,0,sizeof(*m));m->step=cursor.step;m->channels=channels;m->read=read;m->context=context;return 0;
}
static int frame(ws_stream_mixer *m,int16_t *out){
    if(m->at==m->count){
        int n=m->read(m->context,m->buffer,WS_AUDIO_CHUNK_FRAMES);
        if(n<0||n>WS_AUDIO_CHUNK_FRAMES){m->error=1;return -1;}
        if(!n)return 0;
        m->at=0;m->count=(unsigned)n;
    }
    out[0]=m->buffer[m->at*m->channels];
    out[1]=m->buffer[m->at*m->channels+m->channels-1];++m->at;return 1;
}
int ws_stream_mixer_mix(ws_stream_mixer *m,int32_t *out,size_t frames,float left,float right){
    if(!m||!out||!m->read||!m->step||frames>WS_AUDIO_CHUNK_FRAMES||
       !isfinite(left)||!isfinite(right)||left<0||right<0||left>4||right>4||m->error)return -1;
    if(m->ended||!frames)return 0;
    if(!m->ready){
        int n=frame(m,m->pair[0]);if(n<=0){m->ended=1;return n;}
        n=frame(m,m->pair[1]);if(n<0)return -1;
        if(!n){memcpy(m->pair[1],m->pair[0],sizeof(m->pair[0]));m->last=1;}
        m->ready=1;
    }
    int32_t gains[2]={(int32_t)(left*65536.0f+0.5f),(int32_t)(right*65536.0f+0.5f)};
    size_t made=0;
    for(;made<frames&&!m->ended;++made){
        for(unsigned c=0;c<2;++c){
            int32_t a=m->pair[0][c],b=m->pair[1][c];
            int32_t sample=a+(int32_t)((int64_t)(b-a)*(uint32_t)(m->phase>>16)/65536);
            out[made*2+c]+=(int32_t)((int64_t)sample*gains[c]/65536);
        }
        m->phase+=m->step;
        while(m->phase>=UINT64_C(0x100000000)){
            m->phase-=UINT64_C(0x100000000);
            if(m->last){m->ended=1;break;}
            memcpy(m->pair[0],m->pair[1],sizeof(m->pair[0]));
            int n=frame(m,m->pair[1]);if(n<0)return -1;
            if(!n){memcpy(m->pair[1],m->pair[0],sizeof(m->pair[0]));m->last=1;}
        }
    }
    return (int)made;
}
