#include "ws_audio_mix.h"
#include <math.h>
#include <limits.h>
int ws_pcm_pitch(ws_pcm_cursor *cursor,unsigned rate,float pitch){
    if(!cursor||rate<8000||rate>192000||!isfinite(pitch)||pitch<0.125f||pitch>8.0f)return -1;
    cursor->step=(uint64_t)((double)rate*(double)pitch*4294967296.0/WS_AUDIO_OUTPUT_RATE+0.5);return 0;
}
int ws_pcm_start(ws_pcm_cursor *cursor,unsigned rate,float pitch,int loop){
    if(ws_pcm_pitch(cursor,rate,pitch))return -1;
    cursor->position=0;cursor->playing=1;cursor->loop=!!loop;return 0;
}
static int32_t interpolate(const int16_t *pcm,size_t first,size_t next,unsigned channel,unsigned channels,uint32_t fraction){
    int32_t a=pcm[first*channels+channel],b=pcm[next*channels+channel];
    /* 16 fractional bits are ample for PCM16. The signed product needs 64
     * bits at a full-scale transition; a 32-bit product overflows there. */
    return a+(int32_t)(((int64_t)(b-a)*(fraction>>16))/65536);
}
int ws_pcm_mix(const ws_audio_effect *effect,ws_pcm_cursor *cursor,int32_t *mix,size_t frames,float left,float right){
    if(!effect||!cursor||!mix||frames>WS_AUDIO_CHUNK_FRAMES||!effect->samples||
       !effect->frames||(effect->channels!=1&&effect->channels!=2)||
       effect->frames>WS_EFFECT_BUDGET/(effect->channels*sizeof(int16_t))||
       effect->bytes<effect->frames*effect->channels*sizeof(int16_t)||
       !isfinite(left)||!isfinite(right)||left<0||right<0||left>4||right>4||!cursor->step)return -1;
    if(!cursor->playing)return 0;
    const uint64_t end=(uint64_t)effect->frames<<32;
    const int32_t gl=(int32_t)(left*65536.0f+0.5f),gr=(int32_t)(right*65536.0f+0.5f);
    size_t made=0;
    for(;made<frames;++made){
        if(cursor->position>=end){if(!cursor->loop){cursor->playing=0;break;}cursor->position%=end;}
        size_t first=(size_t)(cursor->position>>32),next=first+1;
        if(next==effect->frames)next=cursor->loop?0:first;
        int32_t l=interpolate(effect->samples,first,next,0,effect->channels,(uint32_t)cursor->position);
        int32_t r=effect->channels==1?l:interpolate(effect->samples,first,next,1,2,(uint32_t)cursor->position);
        /* At most 96 voices with gain <=4 fit comfortably in an int32 mix. */
        mix[2*made]+=(int32_t)((int64_t)l*gl/65536);
        mix[2*made+1]+=(int32_t)((int64_t)r*gr/65536);
        cursor->position+=cursor->step;
    }
    if(!cursor->loop&&cursor->position>=end)cursor->playing=0;
    return (int)made;
}
void ws_pcm_finish(const int32_t *mix,int16_t *out,size_t frames){
    if(!mix||!out||frames>WS_AUDIO_CHUNK_FRAMES)return;
    for(size_t i=0;i<frames*2;++i)out[i]=(int16_t)(mix[i]>32767?32767:mix[i]<-32768?-32768:mix[i]);
}
