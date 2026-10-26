#include "ws_sound.h"
#include "ws_audio_data.h"
#include "ws_audio_cache.h"
#include "ws_audio_stream_mix.h"
#include "ws_paths.h"
#include <pthread.h>
#include <stdatomic.h>
#include <math.h>
#include <float.h>
#include <stdio.h>
#include <string.h>

#define EFFECTS 96
#define TRACKS 8
typedef struct {ws_audio_effect pcm;ws_pcm_cursor cursor;uint32_t id,revision;float pitch,level,x,y,z;int looping;} effect;
static effect effects[EFFECTS];
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER,loads=PTHREAD_MUTEX_INITIALIZER;
static ws_audio_output *output;
static ws_audio_sink sink;
static int custom_sink;
static atomic_int initialized;
static uint32_t next_id=1,music_generation;
static float master=1,effects_volume=1,listener_gain=1,music_volume=1;
static float listener[3],reference_distance=1,maximum_distance=FLT_MAX;
static char asset_root[WS_PATH_MAX],playlist[TRACKS][WS_PATH_MAX];
static unsigned track_count;
static int music_playing,music_at_end,music_finished;
/* Only the output worker touches these decoding/cursor fields. */
static ws_audio_stream *music_stream;
static ws_stream_mixer music_mixer;
static unsigned current_track;
static uint32_t playing_generation;
static int gain_valid(float value){return isfinite(value)&&value>=0&&value<=4;}
static float bounded_gain(float value){return value>4?4:value;}
static effect *find_effect(uint32_t id){for(unsigned i=0;i<EFFECTS;++i)if(id&&effects[i].id==id)return &effects[i];return NULL;}
static int asset_path(const char *guest,char *out){
    if(!guest||!*asset_root)return 1;
    const char *name=strrchr(guest,'/');name=name?name+1:guest;
    if(!*name||strstr(name,"..")||strchr(name,'\\')||strchr(name,':'))return 1;
    const char *extension=strrchr(name,'.');
    if(!extension||(strcmp(extension,".caf")&&strcmp(extension,".mp3")))return 1;
    int n=snprintf(out,WS_PATH_MAX,"%s/%s",asset_root,name);return n<0||n>=WS_PATH_MAX;
}
static int read_music(void *ctx,int16_t *pcm,size_t frames){return ws_audio_stream_read(ctx,pcm,frames,0);}
static void close_music(void){ws_audio_stream_close(music_stream);music_stream=NULL;memset(&music_mixer,0,sizeof(music_mixer));}
static void effect_gains(const effect *e,float *left,float *right){
    float gain=bounded_gain(master*effects_volume*listener_gain*e->level);
    *left=*right=gain;
    if(e->pcm.channels==2)return; /* OpenAL stereo sources are not spatialized. */
    float x=e->x-listener[0],y=e->y-listener[1],z=e->z-listener[2];
    float distance=sqrtf(x*x+y*y+z*z),clamped=distance;
    if(clamped<reference_distance)clamped=reference_distance;
    if(clamped>maximum_distance)clamped=maximum_distance;
    if(clamped>0)gain*=reference_distance/clamped;
    float pan=distance>0?x/distance:0;
    /* Equal-power mono panning with unit center gain. */
    *left=bounded_gain(gain*sqrtf(1-pan));*right=bounded_gain(gain*sqrtf(1+pan));
}
static int render(void *ctx,int16_t *pcm,size_t frames){
    (void)ctx;int32_t mix[WS_AUDIO_BLOCK_FRAMES*2]={0};
    struct {effect voice;float left,right;unsigned slot;} voices[EFFECTS];
    unsigned count=0;
    /* Keep PCM alive with the resource lock, but do not hold the control
     * lock while resampling thousands of samples. Gameplay changes to pitch,
     * position, volume, start and stop can proceed during a busy audio block. */
    pthread_mutex_lock(&loads);
    pthread_mutex_lock(&lock);
    for(unsigned i=0;i<EFFECTS;++i)if(effects[i].id&&effects[i].cursor.playing){
        voices[count].voice=effects[i];voices[count].slot=i;
        effect_gains(&effects[i],&voices[count].left,&voices[count].right);++count;
    }
    uint32_t generation=music_generation;int playing=music_playing;float volume=bounded_gain(master*music_volume);
    pthread_mutex_unlock(&lock);
    int error=0;
    for(unsigned i=0;i<count;++i){
        effect *v=&voices[i].voice;
        if(ws_pcm_mix(&v->pcm,&v->cursor,mix,frames,voices[i].left,voices[i].right)<0){error=-1;break;}
    }
    pthread_mutex_lock(&lock);
    for(unsigned i=0;i<count;++i){
        effect *v=&voices[i].voice,*e=&effects[voices[i].slot];
        if(!error&&e->id==v->id&&e->revision==v->revision){
            uint64_t position=v->cursor.position;
            /* A stop-at-end command that arrived during a looping block must
             * not make the next block play another complete loop. */
            if(!e->cursor.loop&&v->cursor.loop&&
               e->cursor.position+(uint64_t)frames*v->cursor.step>=((uint64_t)e->pcm.frames<<32))
                position=(uint64_t)e->pcm.frames<<32;
            e->cursor.position=position;
            e->cursor.playing=v->cursor.playing&&(e->cursor.loop||position<((uint64_t)e->pcm.frames<<32));
            /* step/loop belong to the latest commands, not the snapshot. */
        }
    }
    pthread_mutex_unlock(&lock);pthread_mutex_unlock(&loads);
    if(error)return error;
    if(generation!=playing_generation){close_music();current_track=0;playing_generation=generation;}
    size_t made=0;unsigned transitions=0;
    while(playing&&made<frames){
        if(!music_stream){
            char path[WS_PATH_MAX];pthread_mutex_lock(&lock);
            int valid=generation==music_generation&&current_track<track_count;
            if(valid)memcpy(path,playlist[current_track],sizeof(path));
            pthread_mutex_unlock(&lock);if(!valid)break;
            music_stream=ws_audio_stream_open(path);
            if(!music_stream||ws_stream_mixer_init(&music_mixer,ws_audio_stream_rate(music_stream),ws_audio_stream_channels(music_stream),read_music,music_stream)){
                fprintf(stderr,"Wayward: music decode failed: %s\n",path);close_music();return -1;
            }
        }
        int count=ws_stream_mixer_mix(&music_mixer,mix+made*2,frames-made,volume,volume);
        if(count<0){fputs("Wayward: music stream failed\n",stderr);return -1;}made+=(size_t)count;
        if(!music_mixer.ended)break;
        close_music();pthread_mutex_lock(&lock);
        if(generation!=music_generation||!track_count)playing=0;
        else if(current_track+1>=track_count&&music_at_end){music_playing=0;music_finished=1;playing=0;}
        else current_track=(current_track+1)%track_count;
        pthread_mutex_unlock(&lock);
        if(++transitions>TRACKS){fputs("Wayward: invalid short music playlist\n",stderr);return -1;}
    }
    ws_pcm_finish(mix,pcm,frames);return 0;
}
int ws_sound_configure(const char *root,const ws_audio_sink *device){
    if(atomic_load(&initialized)||!root||!*root||strlen(root)>=sizeof(asset_root))return -1;
    strcpy(asset_root,root);custom_sink=device!=NULL;if(device)sink=*device;
    /* This worker only decodes PCM; it never touches graphics or game objects. */
    if(ws_audio_cache_start(root,!custom_sink))fputs("Wayward: effect prefetch unavailable; using ordinary loads.\n",stderr);
    return 0;
}
int ws_sound_shutdown(void){
    atomic_store(&initialized,0);
    int error=output?ws_audio_output_stop(output):0;output=NULL;
    close_music();pthread_mutex_lock(&loads);pthread_mutex_lock(&lock);
    for(unsigned i=0;i<EFFECTS;++i){ws_audio_cache_release(&effects[i].pcm);memset(&effects[i],0,sizeof(effects[i]));}
    track_count=0;music_playing=music_finished=music_at_end=0;++music_generation;
    pthread_mutex_unlock(&lock);pthread_mutex_unlock(&loads);
    int cache_error=ws_audio_cache_stop();return error?error:cache_error;
}
static int initialize(float rate){
    if(!isfinite(rate)||rate<0||rate>192000||!*asset_root)return 1;
    if(atomic_load(&initialized)){
        ws_sound_shutdown();
        if(ws_audio_cache_start(asset_root,!custom_sink))fputs("Wayward: effect prefetch restart unavailable.\n",stderr);
    }
    master=effects_volume=listener_gain=music_volume=1;reference_distance=1;maximum_distance=FLT_MAX;memset(listener,0,sizeof(listener));
    output=ws_audio_output_start(render,NULL,custom_sink?&sink:NULL);if(!output)return 1;
    atomic_store(&initialized,1);return 0;
}
static int load_effect(const char *path,uint32_t *id,int loop){
    if(!id)return 2;
    *id=0;
    if(!atomic_load(&initialized)&&initialize(48000))return 1;
    char native[WS_PATH_MAX];if(asset_path(path,native))return 2;
    pthread_mutex_lock(&loads);ws_audio_effect decoded={0};
    int error=ws_audio_cache_load(native,&decoded);if(error){pthread_mutex_unlock(&loads);return 2;}
    pthread_mutex_lock(&lock);unsigned slot;for(slot=0;slot<EFFECTS;++slot)if(!effects[slot].id)break;
    if(slot<EFFECTS&&next_id){
        effect *e=&effects[slot];*e=(effect){.pcm=decoded,.id=next_id++,.pitch=1,.level=1,.looping=loop};
        *id=e->id;memset(&decoded,0,sizeof(decoded));
    }
    pthread_mutex_unlock(&lock);ws_audio_cache_release(&decoded);pthread_mutex_unlock(&loads);return *id?0:2;
}
static int load_once(const char *path,uint32_t *id){return load_effect(path,id,0);}
static int load_loop(const char *path,const char *attack,const char *decay,uint32_t *id){
    /* The pinned game's looping effects pass NULL attack/decay. Unsupported
     * three-part sequences must fail explicitly rather than lose their audio. */
    if(attack||decay){if(id)*id=0;return 2;}return load_effect(path,id,1);
}
static int unload(uint32_t id){
    pthread_mutex_lock(&loads);pthread_mutex_lock(&lock);effect *e=find_effect(id);ws_audio_effect old={0};
    if(e){old=e->pcm;memset(e,0,sizeof(*e));}
    pthread_mutex_unlock(&lock);ws_audio_cache_release(&old);pthread_mutex_unlock(&loads);return e?0:2;
}
static int start(uint32_t id){pthread_mutex_lock(&lock);effect *e=find_effect(id);int result=e?ws_pcm_start(&e->cursor,e->pcm.rate,e->pitch,e->looping):2;if(e&&!result)++e->revision;pthread_mutex_unlock(&lock);return result;}
static int stop(uint32_t id,unsigned char at_end){pthread_mutex_lock(&lock);effect *e=find_effect(id);if(e){if(at_end)e->cursor.loop=0;else{e->cursor.playing=0;++e->revision;}}pthread_mutex_unlock(&lock);return e?0:2;}
static int pitch(uint32_t id,float value){pthread_mutex_lock(&lock);effect *e=find_effect(id);int result=e?ws_pcm_pitch(&e->cursor,e->pcm.rate,value):2;if(!result)e->pitch=value;pthread_mutex_unlock(&lock);return result;}
static int level(uint32_t id,float value){if(!gain_valid(value))return 2;pthread_mutex_lock(&lock);effect *e=find_effect(id);if(e)e->level=value;pthread_mutex_unlock(&lock);return e?0:2;}
static int position(uint32_t id,float x,float y,float z){
    if(!isfinite(x)||!isfinite(y)||!isfinite(z)||fabsf(x)>1e8f||fabsf(y)>1e8f||fabsf(z)>1e8f)return 2;
    pthread_mutex_lock(&lock);effect *e=find_effect(id);if(e){e->x=x;e->y=y;e->z=z;}pthread_mutex_unlock(&lock);return e?0:2;
}
static int listener_position(float x,float y,float z){
    if(!isfinite(x)||!isfinite(y)||!isfinite(z)||fabsf(x)>1e8f||fabsf(y)>1e8f||fabsf(z)>1e8f)return 2;
    pthread_mutex_lock(&lock);listener[0]=x;listener[1]=y;listener[2]=z;pthread_mutex_unlock(&lock);return 0;
}
static int set_volume(float *target,float value){if(!gain_valid(value))return 2;pthread_mutex_lock(&lock);*target=value;pthread_mutex_unlock(&lock);return 0;}
static int set_master(float v){return set_volume(&master,v);}static int set_effects(float v){return set_volume(&effects_volume,v);}
static int set_listener(float v){return set_volume(&listener_gain,v);}static int set_music(float v){return set_volume(&music_volume,v);}
static int reference(float v){if(!isfinite(v)||v<=0)return 2;pthread_mutex_lock(&lock);int error=v>maximum_distance;if(!error)reference_distance=v;pthread_mutex_unlock(&lock);return error?2:0;}
static int maximum(float v){if(!isfinite(v)||v<=0)return 2;pthread_mutex_lock(&lock);int error=v<reference_distance;if(!error)maximum_distance=v;pthread_mutex_unlock(&lock);return error?2:0;}
static int load_track(const char *path,unsigned char add,unsigned char load_all){
    (void)load_all; /* Streaming preserves playback without unbounded PCM RAM. */
    char native[WS_PATH_MAX];if(asset_path(path,native))return 2;
    FILE *file=fopen(native,"rb");if(!file)return 2;fclose(file);
    pthread_mutex_lock(&lock);
    if(!add){track_count=0;music_playing=music_finished=music_at_end=0;++music_generation;}
    int error=track_count==TRACKS;if(!error)strcpy(playlist[track_count++],native);
    pthread_mutex_unlock(&lock);return error?2:0;
}
static int unload_music(void){pthread_mutex_lock(&lock);track_count=0;music_playing=music_finished=music_at_end=0;++music_generation;pthread_mutex_unlock(&lock);return 0;}
static int start_music(void){pthread_mutex_lock(&lock);int error=!track_count;if(!error){if(music_finished)++music_generation;music_finished=music_at_end=0;music_playing=1;}pthread_mutex_unlock(&lock);return error?1:0;}
static int stop_music(int at_end){pthread_mutex_lock(&lock);if(at_end)music_at_end=1;else music_playing=0;pthread_mutex_unlock(&lock);return 0;}
typedef struct {const char *name;uint32_t offset;uintptr_t function;} sound_hook;
#define H(n,o,f) {"SoundEngine_" #n,o,(uintptr_t)f}
static const sound_hook hooks[]={
    H(Initialize,0x36fba8,initialize),H(Teardown,0x36fe54,ws_sound_shutdown),H(SetMasterVolume,0x36fefc,set_master),
    H(SetListenerPosition,0x36ff90,listener_position),H(SetListenerGain,0x36ffec,set_listener),
    H(LoadBackgroundMusicTrack,0x370034,load_track),H(UnloadBackgroundMusicTrack,0x3702c8,unload_music),
    H(StartBackgroundMusic,0x370324,start_music),H(StopBackgroundMusic,0x3703c0,stop_music),H(SetBackgroundMusicVolume,0x370418,set_music),
    H(LoadEffect,0x37048c,load_once),H(LoadLoopingEffect,0x370614,load_loop),H(UnloadEffect,0x3707c0,unload),H(StartEffect,0x370854,start),
    H(StopEffect,0x3708e0,stop),H(SetEffectPitch,0x370978,pitch),H(SetEffectLevel,0x370a20,level),H(SetEffectPosition,0x370ae8,position),
    H(SetEffectsVolume,0x370bc8,set_effects),H(SetMaxDistance,0x370ca0,maximum),H(SetReferenceDistance,0x370d54,reference)
};
#undef H
uintptr_t ws_sound_symbol(const char *name){for(size_t i=0;i<sizeof(hooks)/sizeof(*hooks);++i)if(!strcmp(name,hooks[i].name))return hooks[i].function;return 0;}
int ws_sound_attach(so_module *game){
    if(!game||!*asset_root)return -1;
    for(size_t i=0;i<sizeof(hooks)/sizeof(*hooks);++i)if(so_symbol(game,hooks[i].name)!=game->load_addr+hooks[i].offset)return -1;
    for(size_t i=0;i<sizeof(hooks)/sizeof(*hooks);++i)hook_addr(game->load_addr+hooks[i].offset,hooks[i].function);
    so_flush_caches(game);return 0;
}
