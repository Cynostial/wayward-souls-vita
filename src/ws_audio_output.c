#define _POSIX_C_SOURCE 200809L
#include "ws_audio_output.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#ifdef __vita__
#include <psp2/audioout.h>
static int device_open(void *ctx,unsigned frames,unsigned rate){
    (void)ctx;return sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN,(int)frames,(int)rate,SCE_AUDIO_OUT_MODE_STEREO);
}
static int device_write(void *ctx,int port,const int16_t *pcm){(void)ctx;return sceAudioOutOutput(port,pcm);}
static int device_close(void *ctx,int port){(void)ctx;return sceAudioOutReleasePort(port);}
static const ws_audio_sink vita_sink={NULL,device_open,device_write,device_close};
#endif
struct ws_audio_output {
    pthread_t thread;
    ws_audio_sink sink;
    ws_audio_render render;
    void *context;
    int port;
    atomic_int stop,paused,error;
    /* Double buffering: keep the submitted block unchanged while the next
     * block is mixed, matching sceAudioOutOutput's queued buffer lifetime. */
    int16_t pcm[2][WS_AUDIO_BLOCK_FRAMES*2];
};
static void *worker(void *argument){
    ws_audio_output *out=argument;unsigned block=0;
    while(!atomic_load_explicit(&out->stop,memory_order_acquire)){
        int16_t *pcm=out->pcm[block];memset(pcm,0,sizeof(out->pcm[block]));
        int error=0;
        if(!atomic_load_explicit(&out->paused,memory_order_acquire))error=out->render(out->context,pcm,WS_AUDIO_BLOCK_FRAMES);
        if(error>=0)error=out->sink.write(out->sink.context,out->port,pcm);
        if(error<0){atomic_store_explicit(&out->error,error,memory_order_release);break;}
        block^=1;
    }
    int error=out->sink.write(out->sink.context,out->port,NULL);
    if(error<0&&!atomic_load(&out->error))atomic_store(&out->error,error);
    error=out->sink.close(out->sink.context,out->port);
    if(error<0&&!atomic_load(&out->error))atomic_store(&out->error,error);
    return NULL;
}
ws_audio_output *ws_audio_output_start(ws_audio_render render,void *context,const ws_audio_sink *sink){
#ifdef __vita__
    if(!sink)sink=&vita_sink;
#endif
    if(!render||!sink||!sink->open||!sink->write||!sink->close)return NULL;
    ws_audio_output *out=calloc(1,sizeof(*out));if(!out)return NULL;
    out->sink=*sink;out->render=render;out->context=context;
    atomic_init(&out->stop,0);atomic_init(&out->paused,0);atomic_init(&out->error,0);
    out->port=sink->open(sink->context,WS_AUDIO_BLOCK_FRAMES,48000);
    if(out->port<0){free(out);return NULL;}
    pthread_attr_t attr;int error=pthread_attr_init(&attr);
    if(!error){
        error=pthread_attr_setstacksize(&attr,128u*1024u);
        if(!error)error=pthread_create(&out->thread,&attr,worker,out);
        pthread_attr_destroy(&attr);
    }
    if(error){sink->close(sink->context,out->port);free(out);return NULL;}
    return out;
}
void ws_audio_output_pause(ws_audio_output *out,int pause){if(out)atomic_store_explicit(&out->paused,!!pause,memory_order_release);}
int ws_audio_output_error(ws_audio_output *out){return out?atomic_load_explicit(&out->error,memory_order_acquire):-1;}
int ws_audio_output_stop(ws_audio_output *out){
    if(!out||pthread_equal(pthread_self(),out->thread))return -1;
    atomic_store_explicit(&out->stop,1,memory_order_release);
    if(pthread_join(out->thread,NULL))return -1;
    int result=atomic_load(&out->error);free(out);return result;
}
