#include "ws_audio_data.h"
#include <vorbis/vorbisfile.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <limits.h>

struct ws_audio_stream {
    OggVorbis_File file;
    unsigned channels,rate;
};
static atomic_uint stream_count;
static atomic_size_t effect_bytes;

static int reserve(atomic_size_t *used,size_t bytes,size_t limit) {
    size_t old=atomic_load_explicit(used,memory_order_relaxed);
    do {
        if(bytes>limit || old>limit-bytes)return -1;
    } while(!atomic_compare_exchange_weak_explicit(used,&old,old+bytes,
              memory_order_acq_rel,memory_order_relaxed));
    return 0;
}
ws_audio_stream *ws_audio_stream_open(const char *path) {
    if(!path)return NULL;
    unsigned count=atomic_load_explicit(&stream_count,memory_order_relaxed);
    do {
        if(count>=WS_AUDIO_STREAM_LIMIT)return NULL;
    } while(!atomic_compare_exchange_weak_explicit(&stream_count,&count,count+1,
              memory_order_acq_rel,memory_order_relaxed));
    ws_audio_stream *stream=calloc(1,sizeof(*stream));
    if(!stream)goto failed;
    if(ov_fopen(path,&stream->file)<0) { free(stream);goto failed; }
    vorbis_info *info=ov_info(&stream->file,-1);
    /* The pinned files are single-bitstream, mono/stereo, 22.05--96 kHz. Reject
     * unsupported formats instead of silently using the wrong output stride. */
    if(!info || (info->channels!=1 && info->channels!=2) ||
       (info->rate!=22050 && info->rate!=44100 && info->rate!=48000 && info->rate!=96000) ||
       ov_streams(&stream->file)!=1 || ov_pcm_total(&stream->file,-1)<=0) {
        ov_clear(&stream->file);free(stream);goto failed;
    }
    stream->channels=(unsigned)info->channels;stream->rate=(unsigned)info->rate;
    return stream;
failed:
    atomic_fetch_sub_explicit(&stream_count,1,memory_order_release);
    return NULL;
}
unsigned ws_audio_stream_channels(const ws_audio_stream *stream) { return stream?stream->channels:0; }
unsigned ws_audio_stream_rate(const ws_audio_stream *stream) { return stream?stream->rate:0; }
int ws_audio_stream_read(ws_audio_stream *stream,int16_t *out,size_t frames,int loop) {
    if(!stream || !out || frames>WS_AUDIO_CHUNK_FRAMES)return -1;
    size_t bytes=frames*stream->channels*sizeof(*out),done=0;
    unsigned holes=0,empty_rewinds=0;
    while(done<bytes) {
        int bitstream=0;
        long count=ov_read(&stream->file,(char *)out+done,(int)(bytes-done),0,2,1,&bitstream);
        if(count==OV_HOLE && ++holes<=8)continue;
        if(count<0 || bitstream!=0)return -1;
        if(!count) {
            if(!loop || empty_rewinds++)break;
            if(ov_pcm_seek(&stream->file,0)<0)return -1;
            continue;
        }
        empty_rewinds=0;done+=(size_t)count;
    }
    if(done%(stream->channels*sizeof(*out)))return -1;
    return (int)(done/(stream->channels*sizeof(*out)));
}
void ws_audio_stream_close(ws_audio_stream *stream) {
    if(!stream)return;
    ov_clear(&stream->file);free(stream);
    atomic_fetch_sub_explicit(&stream_count,1,memory_order_release);
}
int ws_audio_effect_load(const char *path,ws_audio_effect *effect) {
    if(!effect || effect->samples)return -1;
    ws_audio_stream *stream=ws_audio_stream_open(path);
    if(!stream)return -1;
    ogg_int64_t frames=ov_pcm_total(&stream->file,-1);
    size_t frame_bytes=stream->channels*sizeof(int16_t);
    if(frames<=0 || (uint64_t)frames>WS_EFFECT_BUDGET/frame_bytes) {
        ws_audio_stream_close(stream);return -1;
    }
    size_t bytes=(size_t)frames*frame_bytes;
    if(reserve(&effect_bytes,bytes,WS_EFFECT_BUDGET)) { ws_audio_stream_close(stream);return -1; }
    int16_t *samples=malloc(bytes);
    if(!samples)goto failed;
    size_t done=0;
    while(done<(size_t)frames) {
        size_t count=(size_t)frames-done;
        if(count>WS_AUDIO_CHUNK_FRAMES)count=WS_AUDIO_CHUNK_FRAMES;
        int read=ws_audio_stream_read(stream,samples+done*stream->channels,count,0);
        if(read<=0) { free(samples);goto failed; }
        done+=(size_t)read;
    }
    *effect=(ws_audio_effect){samples,done,bytes,stream->channels,stream->rate};
    ws_audio_stream_close(stream);return 0;
failed:
    atomic_fetch_sub_explicit(&effect_bytes,bytes,memory_order_release);
    ws_audio_stream_close(stream);return -1;
}
void ws_audio_effect_release(ws_audio_effect *effect) {
    if(!effect || !effect->samples)return;
    free(effect->samples);
    atomic_fetch_sub_explicit(&effect_bytes,effect->bytes,memory_order_release);
    memset(effect,0,sizeof(*effect));
}
size_t ws_audio_effect_bytes(void) { return atomic_load_explicit(&effect_bytes,memory_order_acquire); }
