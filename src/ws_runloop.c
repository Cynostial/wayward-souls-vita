#define _POSIX_C_SOURCE 200809L
#include "ws_runloop.h"
#include "ws_views.h"
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#define JOBS 128
#define TICKS 32
typedef struct {jobject runnable,handler;uint64_t order;int64_t due;int native;} job;
typedef struct {
    ws_java *java;pthread_mutex_t lock;pthread_t owner;
    jobject handler,thread,looper,actions[TICKS];job jobs[JOBS];
    uint64_t order;uintptr_t native_entry;int started,in_native,processing,ticking;
} loop;
static int64_t now(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static jvalue nothing(void){jvalue v={0};return v;}
static jvalue empty_constructor(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){(void)ctx;(void)e;(void)self;(void)a;(void)n;return nothing();}
static void exception(JNIEnv *e,const char *type,const char *text){jclass cls=(*e)->FindClass(e,type);if(cls){(*e)->ThrowNew(e,cls,text);(*e)->DeleteLocalRef(e,cls);}}
static int on_owner(loop *l){pthread_mutex_lock(&l->lock);int yes=l->started&&pthread_equal(l->owner,pthread_self());pthread_mutex_unlock(&l->lock);return yes;}
static int require_owner(loop *l,JNIEnv *e){if(on_owner(l))return 1;exception(e,"java/lang/IllegalStateException","MainThread operation called on another thread or before start");return 0;}
static int append(loop *l,JNIEnv *e,jobject handler,jobject runnable,int64_t due,int native) {
    if(!native&&!runnable){exception(e,"java/lang/NullPointerException","Runnable is null");return 0;}
    jobject keep=runnable?(*e)->NewGlobalRef(e,runnable):NULL;
    jobject h=handler?(*e)->NewGlobalRef(e,handler):NULL;
    if((runnable&&!keep)||(handler&&!h)){if(keep)(*e)->DeleteGlobalRef(e,keep);if(h)(*e)->DeleteGlobalRef(e,h);return 0;}
    pthread_mutex_lock(&l->lock);int i;
    for(i=0;i<JOBS;++i)if(!l->jobs[i].order)break;
    if(i<JOBS&&l->started)l->jobs[i]=(job){keep,h,++l->order,due,native};else i=JOBS;
    pthread_mutex_unlock(&l->lock);
    if(i==JOBS){if(keep)(*e)->DeleteGlobalRef(e,keep);if(h)(*e)->DeleteGlobalRef(e,h);return 0;}
    return 1;
}
static void run(JNIEnv *e,jobject object) {
    jclass cls=(*e)->GetObjectClass(e,object);if(!cls)return;
    jmethodID method=(*e)->GetMethodID(e,cls,"run","()V");
    if(method)(*e)->CallVoidMethod(e,object,method);
    (*e)->DeleteLocalRef(e,cls);
}
static void process(loop *l,JNIEnv *e) {
    if(!require_owner(l,e))return;
    /* The APK appends a STOP message before draining. A sequence cutoff has
     * the same bounded-pass property: callbacks posted by callbacks wait until
     * the next pass. No lock is held while invoking guest Java/native code. */
    pthread_mutex_lock(&l->lock);uint64_t cutoff=l->order;++l->processing;pthread_mutex_unlock(&l->lock);
    int64_t at=now();
    for(;;){
        pthread_mutex_lock(&l->lock);int best=-1;
        for(int i=0;i<JOBS;++i){job *j=&l->jobs[i];if(!j->order||j->order>cutoff||j->due>at)continue;
            if(best<0||j->due<l->jobs[best].due||(j->due==l->jobs[best].due&&j->order<l->jobs[best].order))best=i;}
        job next={0};if(best>=0){next=l->jobs[best];memset(&l->jobs[best],0,sizeof(job));}
        uintptr_t native_entry=l->native_entry;
        pthread_mutex_unlock(&l->lock);if(best<0)break;
        if(next.native){
            if(!native_entry)exception(e,"java/lang/IllegalStateException","Native main entry has not been bound");
            else {jclass cls=(*e)->FindClass(e,"com/apportable/MainThread");
                if(cls){((void (*)(JNIEnv *,jclass))native_entry)(e,cls);(*e)->DeleteLocalRef(e,cls);}}
        }else run(e,next.runnable);
        if(next.runnable)(*e)->DeleteGlobalRef(e,next.runnable);
        if(next.handler)(*e)->DeleteGlobalRef(e,next.handler);
        if((*e)->ExceptionCheck(e))break;
    }
    pthread_mutex_lock(&l->lock);--l->processing;pthread_mutex_unlock(&l->lock);
}
static void tick_actions(loop *l,JNIEnv *e) {
    if(!require_owner(l,e))return;
    jobject snapshot[TICKS]={0};pthread_mutex_lock(&l->lock);
    if(l->ticking){pthread_mutex_unlock(&l->lock);return;}
    l->ticking=1;
    for(int i=0;i<TICKS;++i)if(l->actions[i])snapshot[i]=(*e)->NewGlobalRef(e,l->actions[i]);
    pthread_mutex_unlock(&l->lock);
    for(int i=0;i<TICKS;++i)if(snapshot[i]){if(!(*e)->ExceptionCheck(e))run(e,snapshot[i]);(*e)->DeleteGlobalRef(e,snapshot[i]);}
    pthread_mutex_lock(&l->lock);l->ticking=0;pthread_mutex_unlock(&l->lock);
}
static jvalue started(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){(void)e;(void)self;(void)a;(void)n;loop *l=ctx;jvalue v={0};pthread_mutex_lock(&l->lock);v.z=l->started;pthread_mutex_unlock(&l->lock);return v;}
static jvalue get_handler(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){(void)self;(void)a;(void)n;loop *l=ctx;pthread_mutex_lock(&l->lock);jvalue v={.l=l->started?(*e)->NewLocalRef(e,l->handler):NULL};pthread_mutex_unlock(&l->lock);return v;}
static jvalue get_thread(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){(void)self;(void)a;(void)n;loop *l=ctx;pthread_mutex_lock(&l->lock);jvalue v={.l=l->started?(*e)->NewLocalRef(e,l->thread):NULL};pthread_mutex_unlock(&l->lock);return v;}
static jvalue get_looper(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){(void)self;(void)a;(void)n;loop *l=ctx;jvalue v={.l=on_owner(l)?(*e)->NewLocalRef(e,l->looper):NULL};return v;}
static jvalue prepare(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){(void)self;(void)a;(void)n;loop *l=ctx;require_owner(l,e);return nothing();}
static jvalue start(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){(void)self;(void)a;(void)n;loop *l=ctx;if(ws_runloop_start(l->java))exception(e,"java/lang/IllegalStateException","MainThread is already started");return nothing();}
static jvalue process_method(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){(void)self;(void)a;(void)n;process(ctx,e);return nothing();}
static jvalue actions_method(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){(void)self;(void)a;(void)n;tick_actions(ctx,e);return nothing();}
static jvalue tick_method(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){(void)self;(void)a;(void)n;loop *l=ctx;tick_actions(l,e);if(!(*e)->ExceptionCheck(e))process(l,e);if(!(*e)->ExceptionCheck(e))ws_views_layout(l->java);return nothing();}
static jvalue post(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    loop *l=ctx;int64_t delay=n>1?a[1].j:0;if(delay<0)delay=0;int64_t at=now();jvalue v={0};
    if(at<0||delay>INT64_MAX-at)return v;
    v.z=append(l,e,self,a[0].l,at+delay,0);return v;
}
static jvalue remove_callbacks(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)n;loop *l=ctx;pthread_mutex_lock(&l->lock);
    for(int i=0;i<JOBS;++i){job *j=&l->jobs[i];if(j->order&&!j->native&&(*e)->IsSameObject(e,self,j->handler)&&(*e)->IsSameObject(e,a[0].l,j->runnable)){
        (*e)->DeleteGlobalRef(e,j->runnable);(*e)->DeleteGlobalRef(e,j->handler);memset(j,0,sizeof(*j));}}
    pthread_mutex_unlock(&l->lock);return nothing();
}
static jvalue has_callbacks(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)n;loop *l=ctx;jvalue v={0};pthread_mutex_lock(&l->lock);
    for(int i=0;i<JOBS;++i){job *j=&l->jobs[i];if(j->order&&!j->native&&(*e)->IsSameObject(e,self,j->handler)&&(*e)->IsSameObject(e,a[0].l,j->runnable))v.z=1;}
    pthread_mutex_unlock(&l->lock);return v;
}
static jvalue add_action(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)self;(void)n;loop *l=ctx;if(!a[0].l){exception(e,"java/lang/NullPointerException","Tick action is null");return nothing();}
    pthread_mutex_lock(&l->lock);int i;for(i=0;i<TICKS;++i)if(!l->actions[i])break;
    if(i<TICKS)l->actions[i]=(*e)->NewGlobalRef(e,a[0].l);
    pthread_mutex_unlock(&l->lock);
    if(i==TICKS)exception(e,"java/lang/IllegalStateException","Tick action capacity exceeded");
    return nothing();
}
static jvalue remove_action(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)self;(void)n;loop *l=ctx;pthread_mutex_lock(&l->lock);
    for(int i=0;i<TICKS;++i)if(l->actions[i]&&(*e)->IsSameObject(e,l->actions[i],a[0].l)){
        (*e)->DeleteGlobalRef(e,l->actions[i]);memmove(&l->actions[i],&l->actions[i+1],(TICKS-i-1)*sizeof(jobject));l->actions[TICKS-1]=NULL;break;}
    pthread_mutex_unlock(&l->lock);return nothing();
}
static jvalue go_native(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)self;(void)a;(void)n;loop *l=ctx;
    pthread_mutex_lock(&l->lock);int valid=l->started&&!l->in_native&&l->native_entry;if(valid)l->in_native=1;pthread_mutex_unlock(&l->lock);
    if(!valid)exception(e,"java/lang/IllegalStateException","Native entry is unavailable, already scheduled, or MainThread is not started");
    else if(!append(l,e,NULL,NULL,now(),1)){
        pthread_mutex_lock(&l->lock);l->in_native=0;pthread_mutex_unlock(&l->lock);exception(e,"java/lang/IllegalStateException","MainThread task queue is full");}
    return nothing();
}
static jvalue block_run(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)a;(void)n;loop *l=ctx;uintptr_t entry=ws_jni_vm_native(l->java->vm,"com/apportable/BlockRunnable","run","()V");
    if(entry)((void (*)(JNIEnv *,jobject))entry)(e,self);
    else exception(e,"java/lang/UnsupportedOperationException","BlockRunnable native method has not been registered");
    return nothing();
}
int ws_runloop_start(ws_java *java){
    loop *l=java?java->runloop:NULL;if(!l)return EINVAL;pthread_mutex_lock(&l->lock);
    if(l->started){pthread_mutex_unlock(&l->lock);return EBUSY;}
    l->owner=pthread_self();l->started=1;pthread_mutex_unlock(&l->lock);return 0;
}
void ws_runloop_native_entry(ws_java *java,uintptr_t entry){loop *l=java?java->runloop:NULL;if(l){pthread_mutex_lock(&l->lock);l->native_entry=entry;pthread_mutex_unlock(&l->lock);}}
int ws_runloop_tick(ws_java *java){loop *l=java?java->runloop:NULL;JNIEnv *e=ws_java_env(java);if(!l||!e||!on_owner(l))return EINVAL;tick_actions(l,e);if(!(*e)->ExceptionCheck(e))process(l,e);if(!(*e)->ExceptionCheck(e))ws_views_layout(java);return (*e)->ExceptionCheck(e)?EIO:0;}
int ws_runloop_init(ws_java *java){
    if(!java||!java->vm||java->runloop)return -1;
    loop *l=calloc(1,sizeof(*l));if(!l)return -1;
    if(pthread_mutex_init(&l->lock,NULL)){free(l);return -1;}l->java=java;java->runloop=l;
    JNIEnv *e=ws_java_env(java);if(!e)goto bad;
    const char *classes[]={"android/os/Handler","android/os/Looper","java/lang/Thread"};
    jobject *refs[]={&l->handler,&l->looper,&l->thread};
    for(unsigned i=0;i<3;++i){if(!ws_jni_vm_has_class(java->vm,classes[i])&&ws_jni_vm_define_class(java->vm,classes[i]))goto bad;
        jobject object=ws_jni_vm_new_instance(e,classes[i],NULL);*refs[i]=(*e)->NewGlobalRef(e,object);(*e)->DeleteLocalRef(e,object);if(!*refs[i])goto bad;}
#define M(c,n,s,t,f) if(ws_jni_vm_define_method(java->vm,c,n,s,t,f,l))goto bad
    M("com/apportable/MainThread","start","()V",1,start);
    M("com/apportable/MainThread","isStarted","()Z",1,started);
    M("com/apportable/MainThread","getHandler","()Landroid/os/Handler;",1,get_handler);
    M("com/apportable/MainThread","getThread","()Ljava/lang/Thread;",1,get_thread);
    M("com/apportable/MainThread","goNative","()V",1,go_native);
    M("com/apportable/MainThread","addTickAction","(Ljava/lang/Runnable;)V",1,add_action);
    M("com/apportable/MainThread","removeTickAction","(Ljava/lang/Runnable;)V",1,remove_action);
    M("com/apportable/MainThread","runTickActions","()V",1,actions_method);
    M("com/apportable/MainThread","tick","()V",1,tick_method);
    M("com/apportable/utils/RunLoop","prepare","()V",1,prepare);
    M("com/apportable/utils/RunLoop","isUnifiedThreadModel","()Z",1,started);
    M("com/apportable/utils/RunLoop","processMessages","()V",1,process_method);
    M("android/os/Looper","myLooper","()Landroid/os/Looper;",1,get_looper);
    M("android/os/Handler","<init>","()V",0,prepare);
    M("android/os/Handler","post","(Ljava/lang/Runnable;)Z",0,post);
    M("android/os/Handler","postDelayed","(Ljava/lang/Runnable;J)Z",0,post);
    M("android/os/Handler","removeCallbacks","(Ljava/lang/Runnable;)V",0,remove_callbacks);
    M("android/os/Handler","hasCallbacks","(Ljava/lang/Runnable;)Z",0,has_callbacks);
    M("com/apportable/BlockRunnable","run","()V",0,block_run);
    M("com/apportable/BlockRunnable","<init>","()V",0,empty_constructor);
#undef M
    return 0;
bad:ws_runloop_destroy(java);return -1;
}
int ws_runloop_destroy(ws_java *java){
    loop *l=java?java->runloop:NULL;if(!l)return 0;
    JNIEnv *e=ws_java_env(java);if(!e)return EINVAL;pthread_mutex_lock(&l->lock);
    if(l->processing||l->ticking){pthread_mutex_unlock(&l->lock);return EBUSY;}
    l->started=0;
    for(int i=0;i<JOBS;++i){if(l->jobs[i].runnable)(*e)->DeleteGlobalRef(e,l->jobs[i].runnable);if(l->jobs[i].handler)(*e)->DeleteGlobalRef(e,l->jobs[i].handler);}
    for(int i=0;i<TICKS;++i)if(l->actions[i])(*e)->DeleteGlobalRef(e,l->actions[i]);
    if(l->handler)(*e)->DeleteGlobalRef(e,l->handler);
    if(l->thread)(*e)->DeleteGlobalRef(e,l->thread);
    if(l->looper)(*e)->DeleteGlobalRef(e,l->looper);
    pthread_mutex_unlock(&l->lock);pthread_mutex_destroy(&l->lock);free(l);java->runloop=NULL;return 0;
}
