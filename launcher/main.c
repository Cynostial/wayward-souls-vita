#define _POSIX_C_SOURCE 200809L
#include "ws_bootstrap.h"
#include "ws_platform.h"
#include "ws_paths.h"
#include "ws_graphics.h"
#include "ws_runloop.h"
#include "ws_memory.h"
#include "ws_sound.h"
#include "ws_threads.h"
#include "ws_log.h"
#include "ws_loading.h"
#include "ws_profile.h"
#include "../installer/ws_setup.h"
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/stat.h>
#include <psp2/power.h>
#include <psp2/ctrl.h>
#include <psp2/vshbridge.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define MIB (1024u*1024u)
#define ROOT "ux0:data/wayward"
#define VERSION "versions/fc2c5b5150a40f78"
unsigned int _newlib_heap_size_user=48*MIB;
unsigned int _sceLibcHeapSize=4*MIB;
unsigned int _sceLibcHeapExtendedAlloc=0;
static ws_loader loader;
static ws_java java;
/* Native export lookup retains this address throughout the game lifetime. */
static ws_bootstrap bootstrap;

static _Noreturn void fail(const char *message) {
    fprintf(stderr,"STARTUP ERROR: %s\n",message);fflush(stderr);
    sceKernelExitProcess(1);abort();
}
static void directory(const char *path) {
    struct stat s;
    if(!stat(path,&s)){if(!S_ISDIR(s.st_mode))fail("A data directory is occupied by a file.");return;}
    if(mkdir(path,0777))fail("Cannot create the game's private data directory.");
}
static void missing(const char *image,const char *name,unsigned relocation) {
    fprintf(stderr,"Link failure: %s %s relocation=%u\n",image,name,relocation);
}
static void *arena(void) {
    SceKernelFreeMemorySizeInfo info={.size=sizeof(info)};
    if(sceKernelGetFreeMemorySize(&info)<0)fail("Cannot query available application memory.");
    fprintf(stderr,"Available main=%u CDRAM=%u contiguous=%u; guest arena=%u\n",
        (unsigned)info.size_user,(unsigned)info.size_cdram,(unsigned)info.size_phycont,192*MIB);
    /* The native 48 MiB heap is already reserved by newlib. Keep additional
     * main RAM for images (29 MiB), VitaGL (26 MiB), stacks and kernel state.
     * Never silently grow the guest arena into these reserves. */
    if((unsigned)info.size_user<192*MIB+80*MIB||(unsigned)info.size_cdram<96*MIB)
        fail("Insufficient application memory; extended-memory package is required.");
    SceUID id=sceKernelAllocMemBlock("Wayward guest arena",SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,192*MIB,NULL);
    void *memory=NULL;
    if(id<0||sceKernelGetMemBlockBase(id,&memory)<0||!memory)fail("Cannot reserve the bounded game heap.");
    return memory;
}
static void *game_thread(void *unused) {
    (void)unused;
    if(ws_paths_init(ROOT))fail("Cannot configure private paths.");
    if(ws_platform_init(&loader,arena(),192*MIB))fail("Cannot initialize the Android platform adapter.");
    ws_profile_mark("platform ready");
    if(ws_loader_map(&loader,ROOT "/" VERSION))fail("Missing, damaged or unsupported prepared game libraries.");
    ws_profile_mark("libraries mapped and verified");
    /* Reserve the fixed library addresses before graphics allocation, but show
     * the loading art before building the VM, linking and running constructors. */
    if(ws_graphics_init())fail("Cannot initialize VitaGL.");
    ws_profile_mark("graphics ready");
    fprintf(stderr,"Startup graphics ready: %llu ms\n",(unsigned long long)(sceKernelGetProcessTimeWide()/1000));
    if(ws_java_init(&java))fail("Cannot initialize the Java compatibility environment.");
    ws_profile_mark("Java ready");
    size_t count=0;const so_default_dynlib *bindings=ws_platform_bindings(&count);
    if(ws_loader_link(&loader,bindings,count,missing))fail("Cannot relocate the game libraries.");
    ws_profile_mark("libraries linked");
    bootstrap=(ws_bootstrap){.loader=&loader,.java=&java,.apk_path="/wayward/source.apk",
        .asset_directory="/wayward/" VERSION "/assets"};
    if(ws_bootstrap_initialize(&bootstrap))fail("Game framework initialization failed; see run.log.");
    ws_profile_mark("framework ready");
    fprintf(stderr,"Framework ready: JNI=%u graphics wrappers=%u. Entering original game.\n",
        bootstrap.jni_count,bootstrap.graphics_hooks);
    JNIEnv *env=ws_java_env(&java);
    jclass cls=(*env)->FindClass(env,"com/apportable/MainThread");
    jmethodID enter=cls?(*env)->GetStaticMethodID(env,cls,"goNative","()V"):NULL;
    if(!enter)fail("The native game entry is unavailable.");
    (*env)->CallStaticVoidMethod(env,cls,enter);(*env)->DeleteLocalRef(env,cls);
    if(ws_java_check(&java,"schedule original game"))fail("Cannot schedule the original game.");
    /* nativeRun is invoked from the first tick. The original UIKit/CF loop
     * owns subsequent Java tick calls; do not run a competing render thread. */
    for(;;){
        if(ws_runloop_tick(&java))fail("The native game loop failed.");
        /* This replaces Java's outer Looper if nativeRun yields back to it.
         * If UIKit owns the loop, its callbacks tick the same adapter. */
        ws_bionic_threads_reap();sceKernelDelayThread(1000);
    }
}
int main(void) {
    directory("ux0:data");directory(ROOT);
    if(!freopen(ROOT "/run.log","w",stderr))return 1;
    /* Flush every complete line, without turning each formatted fragment into
     * a separate synchronous storage write during startup. */
    static char log_buffer[4096];
    setvbuf(stderr,log_buffer,_IOLBF,sizeof(log_buffer));
    fprintf(stderr,"Wayward Souls development launcher %s %s\n",__DATE__,__TIME__);
    /* Installer owns the display only in this pre-game process. On success it
     * re-execs; subsequent launches skip it using the verified ready marker. */
    scePowerSetArmClockFrequency(444);scePowerSetBusClockFrequency(222);
    ws_setup_on_vita();
    ws_profile_mark("launch");
    if(ws_log_start(NULL,NULL))fputs("Wayward: background logging unavailable; using synchronous log.\n",stderr);
    ws_loading_start(ROOT "/" VERSION "/assets/Default-Landscape.png");
    int module[2]={0};
    if(_vshKernelSearchModuleByName("kubridge",module)<0)fail("kubridge is not installed or loaded.");
    const char *dirs[]={ROOT "/Documents",ROOT "/Library",ROOT "/tmp",ROOT "/obb"};
    for(unsigned i=0;i<sizeof(dirs)/sizeof(*dirs);++i)directory(dirs[i]);
    if(chdir(ROOT))fail("Cannot select the game's data directory.");
    scePowerSetArmClockFrequency(444);scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);scePowerSetGpuXbarClockFrequency(166);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    pthread_attr_t attributes;pthread_t game;
    int error=pthread_attr_init(&attributes);
    if(!error)error=pthread_attr_setstacksize(&attributes,6*MIB);
    if(!error)error=pthread_create(&game,&attributes,game_thread,NULL);
    if(error)fail("Cannot create the game/render thread.");
    pthread_attr_destroy(&attributes);
    if(pthread_join(game,NULL))fail("Cannot join the game thread.");
    ws_log_stop();fflush(stderr);return 0;
}
