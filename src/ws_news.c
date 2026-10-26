#include "ws_news.h"
#include <stdint.h>
#include <stdlib.h>
typedef struct {jobject id,signature;int session;} offline_ads;
static const char *owner="com/noodlecake/noodlenews/Popup";
static const char *ad_owner="com/apportable/chartboost/ChartBoostShim";
static jvalue configure_ad(void *context,JNIEnv *env,jobject cls,const jvalue *args,size_t count){
    (void)cls;(void)count;jobject *slot=context;
    jobject replacement=args[0].l?(*env)->NewGlobalRef(env,args[0].l):NULL;
    if(args[0].l&&!replacement)return (jvalue){0};
    (*env)->DeleteGlobalRef(env,*slot);*slot=replacement;return (jvalue){0};
}
static jvalue ad_session(void *context,JNIEnv *env,jobject cls,const jvalue *args,size_t count){
    (void)env;(void)cls;(void)args;(void)count;
    ((offline_ads *)context)->session=1;
    /* Configuration/lifecycle is retained locally. No network session or
     * impression is reported while this platform has no ad transport. */
    return (jvalue){0};
}
void ws_news_destroy(ws_java *java){
    offline_ads *state=java->news;if(!state)return;
    JNIEnv *env=ws_java_env(java);
    if(env){(*env)->DeleteGlobalRef(env,state->id);(*env)->DeleteGlobalRef(env,state->signature);}
    free(state);java->news=NULL;
}
static void boolean(JNIEnv *env,jclass cls,const char *name,jboolean value){
    jfieldID field=(*env)->GetStaticFieldID(env,cls,name,"Z");if(field)(*env)->SetStaticBooleanField(env,cls,field,value);
}
static void text(JNIEnv *env,jclass cls,const char *name,const char *value){
    jfieldID field=(*env)->GetStaticFieldID(env,cls,name,"Ljava/lang/String;");
    jstring string=field?(*env)->NewStringUTF(env,value):NULL;
    if(string)(*env)->SetStaticObjectField(env,cls,field,string);
    (*env)->DeleteLocalRef(env,string);
}
static jvalue lifecycle(void *context,JNIEnv *env,jobject cls,const jvalue *args,size_t count){
    (void)count;
    if((intptr_t)context){
        boolean(env,cls,"platformInitialized",1);boolean(env,cls,"isPaused",0);
        if(args[0].i==0)text(env,cls,"platform","android");
        else if(args[0].i==1)text(env,cls,"platform","amazon");
    }else boolean(env,cls,"isPaused",1);
    return (jvalue){0};
}
static jvalue empty_queue(void *context,JNIEnv *env,jobject cls,const jvalue *args,size_t count){
    (void)context;(void)env;(void)cls;(void)args;(void)count;
    /* Matches Popup's disconnected/empty-cache path. This port has no online
     * news transport or cached popup transactions. Do not claim a fetch. */
    return (jvalue){0};
}
static jvalue telemetry_disabled(void *context,JNIEnv *env,jobject cls,const jvalue *args,size_t count){
    (void)context;(void)env;(void)cls;(void)args;(void)count;
    /* Analytics are disabled in this port. Do not create worker threads,
     * intercept native crash handling, retain events, or send user data. */
    return (jvalue){0};
}
int ws_news_init(ws_java *java){
    JNIEnv *env=ws_java_env(java);if(!env)return -1;
    offline_ads *ads=calloc(1,sizeof(*ads));if(!ads)return -1;java->news=ads;
    if(ws_jni_vm_define_method(java->vm,ad_owner,"setAppID","(Ljava/lang/String;)V",1,configure_ad,&ads->id)||
       ws_jni_vm_define_method(java->vm,ad_owner,"setAppSignature","(Ljava/lang/String;)V",1,configure_ad,&ads->signature)||
       ws_jni_vm_define_method(java->vm,ad_owner,"startSession","()V",1,ad_session,ads)||
       ws_jni_vm_define_method(java->vm,ad_owner,"hasCachedInterstitial","()Z",1,empty_queue,NULL)||
       ws_jni_vm_define_method(java->vm,ad_owner,"hasCachedInterstitial","(Ljava/lang/String;)Z",1,empty_queue,NULL))return -1;
    static const struct {const char *name,*sig;int is_static;} analytics[]={
        {"<init>","()V",0},{"setVersionName","(Ljava/lang/String;)V",1},
        {"setReportLocation","(Z)V",1},{"setLogEnabled","(Z)V",1},
        {"setLogLevel","(I)V",1},{"setContinueSessionMillis","(J)V",1},
        {"setLogEvents","(Z)V",1},{"setCaptureUncaughtExceptions","(Z)V",1},
        {"onStartSession","(Landroid/content/Context;Ljava/lang/String;)V",1},
        {"onEndSession","(Landroid/content/Context;)V",1},
        {"logEvent","(Ljava/lang/String;)V",1},{"logEvent","(Ljava/lang/String;Z)V",1},
        {"logEvent","(Ljava/lang/String;Ljava/util/Map;)V",1},
        {"logEvent","(Ljava/lang/String;Ljava/util/Map;Z)V",1},
        {"endTimedEvent","(Ljava/lang/String;)V",1},
        {"endTimedEvent","(Ljava/lang/String;Ljava/util/Map;)V",1},
        {"onPageView","()V",1}
    };
    for(unsigned i=0;i<sizeof(analytics)/sizeof(*analytics);++i)
        if(ws_jni_vm_define_method(java->vm,"com/flurry/android/FlurryAgent",analytics[i].name,analytics[i].sig,analytics[i].is_static,telemetry_disabled,NULL))return -1;
    jclass cls=(*env)->FindClass(env,owner);if(!cls)return -1;
    boolean(env,cls,"platformInitialized",0);boolean(env,cls,"isPaused",1);
    text(env,cls,"platform","android");text(env,cls,"currencyName","Coins");
    (*env)->DeleteLocalRef(env,cls);
    if((*env)->ExceptionCheck(env))return -1;
    if(ws_jni_vm_define_method(java->vm,owner,"appResuming","(I)V",1,lifecycle,(void *)1)||
       ws_jni_vm_define_method(java->vm,owner,"appPausing","()V",1,lifecycle,NULL))return -1;
    const char *names[]={"pollPopupServer","checkImmediatePopup","hasImmediatePopup","checkParameterizedPopup","hasParameterizedPopup","getMoreGamesCount"};
    const char *signatures[]={"()V","()V","()Z","(I)V","(I)Z","()I"};
    for(unsigned i=0;i<sizeof(names)/sizeof(*names);++i)
        if(ws_jni_vm_define_method(java->vm,owner,names[i],signatures[i],1,empty_queue,NULL))return -1;
    return 0;
}
