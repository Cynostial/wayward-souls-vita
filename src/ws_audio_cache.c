#include "ws_audio_cache.h"
#include "ws_paths.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

/* All short effects in the pinned APK. Their total decoded PCM is 4,143,052
 * bytes; the decoder's existing global 8 MiB limit remains authoritative.
 * Music and unknown filenames are never retained here. */
static const char *names[]={
    "pop.caf","slash0.caf","heart_heal.caf","shatter_rock.caf","shatter_wood.caf","urn.caf",
    "deco_bush.caf","deco_flowers.caf","deco_veg.caf","deco_torch.caf","deco_barrel.caf",
    "hc_crush.caf","hw_rocket.caf","midboss_die.caf","stab5.caf","puff.caf","chest.caf",
    "deco_book.caf","dash.caf","deco_metal.caf","equip_hat.caf","equip_hook.caf","explode.caf",
    "door_move.caf","door_stop.caf","spell_fireball.caf","deco_glass.caf","sear.caf","addspell.caf",
    "spell_shield.caf","shieldbreak.caf","spell_lightbeam.caf","spell_lightning.caf","hc_jump.caf",
    "hc_land.caf","slide_rock.caf","chatblip2.caf","lastmonster.caf","spell_frost.caf","throw.caf",
    "warmup.caf","warmup2.caf","swing.caf","multiswing.caf","bullcharge.caf","flame.caf",
    "parry.caf","throwice.caf","voidarea.caf","electric.caf","mobdash.caf","hc_squish.caf",
    "slime.caf","teleport.caf","spell_silence.caf","button4.caf","button5.caf","lexipain.caf",
    "spell_haste.caf","spell_lucky.caf","newcoin1.caf","coin1b.caf","step1.caf","ash1.caf"
};
enum {COUNT=sizeof(names)/sizeof(*names)};
typedef struct {ws_audio_effect pcm;unsigned refs;} entry;
static entry entries[COUNT];
static char root[WS_PATH_MAX];
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER,decode=PTHREAD_MUTEX_INITIALIZER;
static pthread_t worker;
static atomic_int stopping;
static int configured,worker_started;
static entry *lookup(const char *path){
    if(!configured||!path)return NULL;
    size_t n=strlen(root);
    if(strncmp(path,root,n)||path[n]!='/')return NULL;
    for(unsigned i=0;i<COUNT;++i)if(!strcmp(path+n+1,names[i]))return &entries[i];
    return NULL;
}
int ws_audio_cache_load(const char *path,ws_audio_effect *out){
    if(!path||!out||out->samples)return -1;
    pthread_mutex_lock(&lock);entry *e=lookup(path);
    if(e&&e->pcm.samples){*out=e->pcm;++e->refs;pthread_mutex_unlock(&lock);return 0;}
    pthread_mutex_unlock(&lock);
    /* At most one effects decoder plus the music decoder can be active.
     * Never hold the cache/control mutex while reading or decoding a file. */
    pthread_mutex_lock(&decode);
    pthread_mutex_lock(&lock);
    if(e&&e->pcm.samples){*out=e->pcm;++e->refs;pthread_mutex_unlock(&lock);pthread_mutex_unlock(&decode);return 0;}
    pthread_mutex_unlock(&lock);
    ws_audio_effect pcm={0};int result=ws_audio_effect_load(path,&pcm);
    if(!result){
        pthread_mutex_lock(&lock);
        if(e){e->pcm=pcm;++e->refs;}
        *out=pcm;pthread_mutex_unlock(&lock);
    }
    pthread_mutex_unlock(&decode);return result;
}
void ws_audio_cache_release(ws_audio_effect *pcm){
    if(!pcm||!pcm->samples)return;
    pthread_mutex_lock(&lock);
    for(unsigned i=0;i<COUNT;++i)if(entries[i].pcm.samples==pcm->samples){
        if(entries[i].refs)--entries[i].refs;
        memset(pcm,0,sizeof(*pcm));pthread_mutex_unlock(&lock);return;
    }
    pthread_mutex_unlock(&lock);ws_audio_effect_release(pcm);
}
static void *warmup(void *unused){
    (void)unused;
    for(unsigned i=0;i<COUNT&&!atomic_load(&stopping);++i){
        char path[WS_PATH_MAX];int n=snprintf(path,sizeof(path),"%s/%s",root,names[i]);
        if(n<0||n>=(int)sizeof(path))continue;
        ws_audio_effect pcm={0};if(!ws_audio_cache_load(path,&pcm))ws_audio_cache_release(&pcm);
    }
    unsigned loaded=0;size_t bytes=0;
    pthread_mutex_lock(&lock);
    for(unsigned i=0;i<COUNT;++i)if(entries[i].pcm.samples){++loaded;bytes+=entries[i].pcm.bytes;}
    pthread_mutex_unlock(&lock);
    fprintf(stderr,"Wayward effect cache: %u/%u ready, %u PCM bytes (8 MiB decoder limit).\n",loaded,COUNT,(unsigned)bytes);
    return NULL;
}
int ws_audio_cache_start(const char *assets,int warm){
    if(configured||!assets||!*assets||strlen(assets)>=sizeof(root))return -1;
    strcpy(root,assets);configured=1;atomic_store(&stopping,0);
    if(!warm)return 0;
    pthread_attr_t attr;int error=pthread_attr_init(&attr);
    if(!error){
        error=pthread_attr_setstacksize(&attr,256*1024);
        if(!error)error=pthread_create(&worker,&attr,warmup,NULL);
        pthread_attr_destroy(&attr);
    }
    if(error){configured=0;root[0]=0;return error;}
    worker_started=1;return 0;
}
int ws_audio_cache_stop(void){
    atomic_store(&stopping,1);
    if(worker_started){if(pthread_join(worker,NULL))return -1;worker_started=0;}
    pthread_mutex_lock(&lock);
    for(unsigned i=0;i<COUNT;++i)if(entries[i].refs){pthread_mutex_unlock(&lock);return -1;}
    for(unsigned i=0;i<COUNT;++i)ws_audio_effect_release(&entries[i].pcm);
    configured=0;root[0]=0;pthread_mutex_unlock(&lock);return 0;
}
