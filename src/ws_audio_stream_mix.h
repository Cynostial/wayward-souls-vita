#ifndef WS_AUDIO_STREAM_MIX_H
#define WS_AUDIO_STREAM_MIX_H
#include "ws_audio_mix.h"
typedef int (*ws_pcm_reader)(void *,int16_t *,size_t);
/* One audio-worker owner. Fixed storage, including lookahead, so song length
 * never affects memory use. The reader returns interleaved source frames. */
typedef struct {
    ws_pcm_reader read;
    void *context;
    uint64_t phase,step;
    unsigned channels,at,count;
    int ready,last,ended,error;
    int16_t pair[2][2];
    int16_t buffer[WS_AUDIO_CHUNK_FRAMES*2];
} ws_stream_mixer;
int ws_stream_mixer_init(ws_stream_mixer *,unsigned,unsigned,ws_pcm_reader,void *);
int ws_stream_mixer_mix(ws_stream_mixer *,int32_t *,size_t,float,float);
#endif
