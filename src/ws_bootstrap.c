#include "ws_lifecycle.h"
#include "ws_bootstrap.h"
#include "ws_platform.h"
#include "ws_paths.h"
#include "ws_graphics.h"
#include "ws_env.h"
#include "ws_threads.h"
#include "ws_allocator_hooks.h"
#include "ws_runloop.h"
#include "ws_bitmap.h"
#include "ws_time.h"
#include "ws_sound.h"
#include "ws_input.h"
#include "ws_database.h"
#include "ws_online.h"
#include "ws_quality.h"
#include "ws_menu.h"
#include "ws_atlas.h"
#include "ws_profile.h"
#include <stdio.h>
#include <string.h>
static int environment(ws_bootstrap *b) {
    static const char *paths[]={"HOME","CFFIXED_USER_HOME","ANDROID_DATA_DIRECTORY","ANDROID_EXTERNAL_STORAGE"};
    static const char *cache[]={"TMP","TEMP","TMPDIR","TEMPDIR","CACHEDIR","EXTERNAL_CACHEDIR","ANDROID_DOWNLOAD_DIRECTORY"};
    for(size_t i=0;i<sizeof(paths)/sizeof(*paths);++i)if(ws_setenv(paths[i],"/wayward",1))return -1;
    for(size_t i=0;i<sizeof(cache)/sizeof(*cache);++i)if(ws_setenv(cache[i],"/wayward/tmp",1))return -1;
    const char *values[][2]={
        {"VERSION_CODE","1.12"},{"VERSION_NAME","1404952329"}, /* APK Java uses these reversed names */
        {"LOCALE","en_US"},{"LANGUAGES","en"},{"TZ","UTC"},{"PACKAGE_NAME","com.noodlecake.waywardsouls"},
        {"OBBDIR","/wayward/obb"},{"NSUnrecognizedForwardingDisabled","YES"},
        {"PACKAGE_CODE_PATH",b->apk_path},{"PACKAGE_RESOURCE_PATH",b->apk_path},
#include "ws_manifest_env.inc"
    };
    for(size_t i=0;i<sizeof(values)/sizeof(*values);++i)if(ws_setenv(values[i][0],values[i][1],1))return -1;
    ws_b_tzset();
    char process[WS_PATH_MAX];int n=snprintf(process,sizeof(process),"%s/com.noodlecake.waywardsouls",b->apk_path);
    if(n<0||(size_t)n>=sizeof(process)||ws_setenv("CFProcessPath",process,1))return -1;
    return 0;
}
static uintptr_t java_export(void *context,const char *symbol){
    ws_bootstrap *b=context;
    /* Only expose images whose initialization has begun, as System.loadLibrary does. */
    for(unsigned i=0;i<b->visible_images;++i){uintptr_t p=so_symbol(&b->loader->images[i],symbol);if(p)return p;}
    return 0;
}
static int image_stage(void *context,unsigned index,int before) {
    ws_bootstrap *b=context;ws_loader *l=b->loader;so_module *m=&l->images[index];const char *name=l->modules[index].filename;
    if(before){
        b->visible_images=index+1;
        ws_platform_visible(index+1);
        fprintf(stderr,"Wayward constructors: %s\n",name);
        if(!strcmp(name,"libv.so")&&ws_allocator_attach(m))return -1;
        if(!strcmp(name,"libsqlite_3_7_5.so")&&ws_database_attach(m,b->asset_directory,b->apk_path))return -1;
        if(!strcmp(name,"libverde.so")){
            ws_link_context context={l->modules,l->count,NULL,0,NULL};
            int system=ws_module_index(&context,"libSystem.so");
            if(system<0||ws_graphics_attach(m,&l->images[system],&b->graphics_hooks))return -1;
            if(ws_input_attach(m,&l->images[system]))return -1;
            if(ws_online_attach(m))return -1;
            if(ws_sound_attach(m))return -1;
        }
        return 0;
    }
    uintptr_t entry=so_symbol(m,"JNI_OnLoad");
    if(entry){
        fprintf(stderr,"Wayward JNI_OnLoad: %s\n",name);
        jint (*onload)(JavaVM *,void *)=(void *)entry;
        jint version=onload(b->java->handle,NULL);
        if((version!=JNI_VERSION_1_2&&version!=JNI_VERSION_1_4&&version!=JNI_VERSION_1_6)||ws_java_check(b->java,name))return -1;
        ++b->jni_count;
    }
    if(!strcmp(name,"libv.so")){
        if(environment(b))return -1;
        /* Registered by the actual libv JNI_OnLoad, not a substitute symbol. */
        uintptr_t address=ws_jni_vm_native(b->java->vm,"com/apportable/LibraryManager","nativeLibvInit","()V");
        if(!address){fputs("Wayward: libv did not register nativeLibvInit\n",stderr);return -1;}
        JNIEnv *env=ws_java_env(b->java);jclass cls=(*env)->FindClass(env,"com/apportable/LibraryManager");
        if(!cls)return -1;
        void (*initialize)(JNIEnv *,jclass)=(void *)address;initialize(env,cls);(*env)->DeleteLocalRef(env,cls);
    }
    if(!strcmp(name,"libverde.so")){
        ws_link_context context={l->modules,l->count,NULL,0,NULL};
        int system=ws_module_index(&context,"libSystem.so");
        int sqlite=ws_module_index(&context,"libsqlite_3_7_5.so");
        if(system<0||sqlite<0||ws_quality_attach(m,&l->images[system],&l->images[sqlite]))return -1;
        if(ws_menu_install())return -1;
        if(ws_lifecycle_attach(m,&l->images[system]))return -1;
        if(ws_atlas_attach(m,&l->images[system]))return -1;
        if(ws_profile_attach(m,&l->images[system]))return -1;
        uintptr_t native=so_symbol(m,"Java_com_apportable_MainThread_nativeRun");
        if(!native)return -1;
        ws_runloop_native_entry(b->java,native);
    }
    return ws_java_check(b->java,name);
}
static void missing(const char *name){fprintf(stderr,"Wayward missing import: %s\n",name);}
int ws_bootstrap_initialize(ws_bootstrap *b) {
    if(!b||!b->loader||!b->java||!b->java->vm||!b->apk_path||!b->asset_directory||b->loader->phase!=WS_LINKED)return -1;
    if(ws_platform_missing(missing))return -1;
    ws_thread_identity current;if(ws_bionic_current_thread(&current)||current.stack_size<5u*1024u*1024u)return -1;
    char path[WS_PATH_MAX];if(ws_path_resolve(b->apk_path,path,sizeof(path)))return -1;
    FILE *apk=fopen(path,"rb");if(!apk)return -1;fclose(apk);
    if(ws_path_resolve(b->asset_directory,path,sizeof(path))||ws_sound_configure(path,NULL))return -1;
    if(ws_bitmap_services(b->java)||ws_runloop_start(b->java))return -1;
    b->visible_images=0;ws_jni_vm_exports(b->java->vm,java_export,b);
    b->jni_count=b->graphics_hooks=0;
    if(ws_loader_initialize(b->loader,image_stage,b))return -1;
    return b->jni_count==4?0:-1;
}
