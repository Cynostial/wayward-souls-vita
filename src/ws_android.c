#include "ws_android.h"
#include "ws_views.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
enum {WIDTH=1136,HEIGHT=640,DPI=320}; /* EAGL's 568x320 logical view at scale 2. */
enum {ACTIVITY,APPLICATION,RESOURCES,ASSETS,METRICS,CONFIGURATION,MANAGER,DISPLAY,COUNT};
typedef struct {ws_java *java;jobject objects[COUNT];} android;
static void error(JNIEnv *e,const char *name,const char *message){
    jclass c=(*e)->FindClass(e,name);if(c){(*e)->ThrowNew(e,c,message);(*e)->DeleteLocalRef(e,c);}
}
static jfieldID field(JNIEnv *e,jobject object,const char *name,const char *sig){
    jclass c=(*e)->GetObjectClass(e,object);jfieldID f=c?(*e)->GetFieldID(e,c,name,sig):NULL;
    (*e)->DeleteLocalRef(e,c);return f;
}
static int integer(JNIEnv *e,jobject object,const char *name,jint value){
    jfieldID f=field(e,object,name,"I");if(f)(*e)->SetIntField(e,object,f,value);return f?0:-1;
}
static int real(JNIEnv *e,jobject object,const char *name,jfloat value){
    jfieldID f=field(e,object,name,"F");if(f)(*e)->SetFloatField(e,object,f,value);return f?0:-1;
}
static int metrics(JNIEnv *e,jobject target){
    return integer(e,target,"widthPixels",WIDTH)||integer(e,target,"heightPixels",HEIGHT)||
        integer(e,target,"densityDpi",DPI)||real(e,target,"density",2)||real(e,target,"scaledDensity",2)||
        real(e,target,"xdpi",DPI)||real(e,target,"ydpi",DPI);
}
static jvalue get_singleton(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)self;(void)a;(void)n;jobject *slot=ctx;return (jvalue){.l=(*e)->NewLocalRef(e,*slot)};
}
static jvalue fixed_text(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)self;(void)a;(void)n;return (jvalue){.l=(*e)->NewStringUTF(e,ctx)};
}
static jvalue directory(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)self;(void)a;(void)n;return (jvalue){.l=ws_android_file(e,ctx)};
}
static jvalue fixed_int(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)e;(void)self;(void)a;(void)n;return (jvalue){.i=(jint)(intptr_t)ctx};
}
static jvalue refresh(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)ctx;(void)e;(void)self;(void)a;(void)n;return (jvalue){.f=60.0f};
}
static jvalue screen_metrics(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)ctx;(void)self;(void)n;
    if(!a[0].l)error(e,"java/lang/NullPointerException","DisplayMetrics is null");else metrics(e,a[0].l);
    return (jvalue){0};
}
static jvalue metric_defaults(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)ctx;(void)a;(void)n;
    integer(e,self,"widthPixels",0);integer(e,self,"heightPixels",0);integer(e,self,"densityDpi",DPI);
    real(e,self,"density",2);real(e,self,"scaledDensity",2);real(e,self,"xdpi",DPI);real(e,self,"ydpi",DPI);return (jvalue){0};
}
static jvalue point_size(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)ctx;(void)self;(void)n;
    if(!a[0].l)error(e,"java/lang/NullPointerException","Display size target is null");
    else {integer(e,a[0].l,"x",WIDTH);integer(e,a[0].l,"y",HEIGHT);}return (jvalue){0};
}
static jvalue pixels_to_dp(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)ctx;(void)e;(void)self;(void)n;return (jvalue){.f=a[0].f/2};
}
static jvalue system_service(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)self;(void)n;android *state=ctx;jvalue out={0};
    if(!a[0].l)return out;
    const char *name=(*e)->GetStringUTFChars(e,a[0].l,NULL);if(!name)return out;
    if(!strcmp(name,"window"))out.l=(*e)->NewLocalRef(e,state->objects[MANAGER]);
    else error(e,"java/lang/UnsupportedOperationException",name);
    (*e)->ReleaseStringUTFChars(e,a[0].l,name);return out;
}
static jvalue device_create(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)ctx;(void)self;(void)n;
    if(!a[0].l){error(e,"java/lang/NullPointerException","Device context is null");return (jvalue){0};}
    jobject result=ws_jni_vm_new_instance(e,"com/apportable/ui/Device",NULL);
    if(result)integer(e,result,"mObject",a[1].i);
    return (jvalue){.l=result};
}
static jvalue device_enabled(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)a;(void)n;jfieldID f=field(e,self,"mEnabled","Z");
    if(f)(*e)->SetBooleanField(e,self,f,ctx!=NULL);
    /* This port uses a fixed landscape display. There are no sensor-driven
     * orientation changes to report while an observer is enabled. */
    return (jvalue){0};
}
static jvalue content_size(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)ctx;(void)n;
    if(!isfinite(a[0].f)||!isfinite(a[1].f)||a[0].f!=WIDTH||a[1].f!=HEIGHT){
        error(e,"java/lang/UnsupportedOperationException","Surface size differs from the configured Vita viewport");return (jvalue){0};
    }
    integer(e,self,"mContentWidth",WIDTH);integer(e,self,"mContentHeight",HEIGHT);return (jvalue){0};
}
static jvalue content_dimension(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)a;(void)n;int height=ctx!=NULL;jfieldID f=field(e,self,height?"mContentHeight":"mContentWidth","I");
    jint value=f?(*e)->GetIntField(e,self,f):0;return (jvalue){.i=value>0?value:height?HEIGHT:WIDTH};
}
static jvalue rect_values(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)ctx;const char *names[]={"left","top","right","bottom"};
    for(int i=0;i<4;++i)real(e,self,names[i],n?a[i].f:0);
    return (jvalue){0};
}
static jvalue rect_measure(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)a;(void)n;int which=(int)(intptr_t)ctx;
    const char *names[]={"left","top","right","bottom"};jfloat v[4]={0};
    for(int i=0;i<4;++i){jfieldID f=field(e,self,names[i],"F");if(f)v[i]=(*e)->GetFloatField(e,self,f);}
    if(which==4)return (jvalue){.z=v[0]>=v[2]||v[1]>=v[3]};
    return (jvalue){.f=which==0?v[2]-v[0]:which==1?v[3]-v[1]:which==2?(v[0]+v[2])*0.5f:(v[1]+v[3])*0.5f};
}
#define EMPTY_LIST "com/wayward/EmptyNotificationList"
#define EMPTY_ITERATOR "com/wayward/EmptyNotificationIterator"
static jvalue empty_instance(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)self;(void)a;(void)n;return (jvalue){.l=ws_jni_vm_new_instance(e,ctx,NULL)};
}
static jvalue exhausted(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)self;(void)a;(void)n;error(e,ctx,"The notification list is empty");return (jvalue){0};
}
static jvalue empty_array(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)ctx;(void)self;
    if(n){if(!a[0].l){error(e,"java/lang/NullPointerException","Array is null");return (jvalue){0};}
        if((*e)->GetArrayLength(e,a[0].l))(*e)->SetObjectArrayElement(e,a[0].l,0,NULL);
        return (jvalue){.l=(*e)->NewLocalRef(e,a[0].l)};}
    jclass c=(*e)->FindClass(e,"java/lang/Object");jobject result=(*e)->NewObjectArray(e,0,c,NULL);(*e)->DeleteLocalRef(e,c);return (jvalue){.l=result};
}
static jvalue no_notification(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    /* The Vita starts from its bubble, with no Android notification payload.
     * Scheduling remains unsupported and throws before adding anything. */
    (void)ctx;(void)e;(void)self;(void)a;(void)n;return (jvalue){0};
}
static int bind_service(ws_java *j,const char *owner,const char *name,const char *sig,int is_static,ws_jni_service body,void *ctx){
    return ws_jni_vm_define_method(j->vm,owner,name,sig,is_static,body,ctx);
}
static int context_services(ws_java *j,android *state){
    /* These framework classes override Context methods in the SDK. Bind each
     * override to the same port context instead of leaving a metadata-only
     * override that masks a working superclass method. */
    const char *owners[]={"android/content/Context","android/content/ContextWrapper","android/view/ContextThemeWrapper","android/app/Activity","android/app/Application"};
    for(size_t i=0;i<sizeof(owners)/sizeof(*owners);++i){const char *owner=owners[i];
#define M(n,s,f,c) if(bind_service(j,owner,n,s,0,f,c))return -1
        M("getApplicationContext","()Landroid/content/Context;",get_singleton,&state->objects[APPLICATION]);
        M("getResources","()Landroid/content/res/Resources;",get_singleton,&state->objects[RESOURCES]);
        M("getAssets","()Landroid/content/res/AssetManager;",get_singleton,&state->objects[ASSETS]);
        M("getClassLoader","()Ljava/lang/ClassLoader;",get_singleton,&j->loader);
        M("getPackageName","()Ljava/lang/String;",fixed_text,"com.noodlecake.waywardsouls");
        M("getPackageCodePath","()Ljava/lang/String;",fixed_text,"/wayward/source.apk");
        M("getPackageResourcePath","()Ljava/lang/String;",fixed_text,"/wayward/source.apk");
        M("getFilesDir","()Ljava/io/File;",directory,"/wayward");
        M("getCacheDir","()Ljava/io/File;",directory,"/wayward/tmp");
        M("getExternalCacheDir","()Ljava/io/File;",directory,"/wayward/tmp");
        M("getObbDir","()Ljava/io/File;",directory,"/wayward/obb");
        M("getSystemService","(Ljava/lang/String;)Ljava/lang/Object;",system_service,state);
#undef M
    }
    return 0;
}
static int objects_init(ws_java *j){
    android *state=calloc(1,sizeof(*state));if(!state)return -1;
    state->java=j;j->android=state;JNIEnv *e=ws_java_env(j);
    static const char *classes[COUNT]={"com/apportable/activity/VerdeActivity","com/apportable/app/VerdeApplication",
        "android/content/res/Resources","android/content/res/AssetManager","android/util/DisplayMetrics",
        "android/content/res/Configuration","android/view/WindowManager","android/view/Display"};
    for(int i=0;i<COUNT;++i){jobject local=ws_jni_vm_new_instance(e,classes[i],NULL);
        if(local)state->objects[i]=(*e)->NewGlobalRef(e,local);
        (*e)->DeleteLocalRef(e,local);
        if(!state->objects[i])return -1;
    }
    if(metrics(e,state->objects[METRICS]))return -1;
    jobject config=state->objects[CONFIGURATION];
    if(integer(e,config,"orientation",2)||integer(e,config,"screenWidthDp",WIDTH/2)||
       integer(e,config,"screenHeightDp",HEIGHT/2)||integer(e,config,"smallestScreenWidthDp",HEIGHT/2)||integer(e,config,"densityDpi",DPI))return -1;
    for(int i=0;i<2;++i){jclass c=(*e)->FindClass(e,classes[i]);
        jfieldID f=(*e)->GetStaticFieldID(e,c,i==0?"theVerdeActivity":"sApp",i==0?"Lcom/apportable/activity/VerdeActivity;":"Lcom/apportable/app/VerdeApplication;");
        if(f)(*e)->SetStaticObjectField(e,c,f,state->objects[i]);
        (*e)->DeleteLocalRef(e,c);if(!f)return -1;
    }
    if(context_services(j,state)||ws_android_files_init(j)||ws_views_init(j))return -1;
    if(ws_jni_vm_define_subclass(j->vm,EMPTY_LIST,"java/util/List")||ws_jni_vm_define_subclass(j->vm,EMPTY_ITERATOR,"java/util/Iterator"))return -1;
#define M(c,n,s,t,f,p) if(bind_service(j,c,n,s,t,f,p))return -1
    M("com/apportable/notifications/Notifications","getScheduledLocalNotifications","()Ljava/util/List;",1,empty_instance,EMPTY_LIST);
    M("com/apportable/notifications/Notifications","getActivityLocalNotification","()Lcom/apportable/ui/LocalNotification;",1,no_notification,NULL);
    M("com/apportable/notifications/Notifications","removeActivityLocalNotification","()V",1,no_notification,NULL);
    M("com/apportable/notifications/Notifications","cancelAllLocalNotifications","()V",1,no_notification,NULL);
    M(EMPTY_LIST,"size","()I",0,fixed_int,NULL);M(EMPTY_LIST,"isEmpty","()Z",0,fixed_int,(void *)1);
    M(EMPTY_LIST,"get","(I)Ljava/lang/Object;",0,exhausted,"java/lang/IndexOutOfBoundsException");
    M(EMPTY_LIST,"iterator","()Ljava/util/Iterator;",0,empty_instance,EMPTY_ITERATOR);
    M(EMPTY_LIST,"toArray","()[Ljava/lang/Object;",0,empty_array,NULL);
    M(EMPTY_LIST,"toArray","([Ljava/lang/Object;)[Ljava/lang/Object;",0,empty_array,NULL);
    M(EMPTY_ITERATOR,"hasNext","()Z",0,fixed_int,NULL);
    M(EMPTY_ITERATOR,"next","()Ljava/lang/Object;",0,exhausted,"java/util/NoSuchElementException");
    M(classes[ACTIVITY],"getActivity","()Lcom/apportable/activity/VerdeActivity;",1,get_singleton,&state->objects[ACTIVITY]);
    M(classes[APPLICATION],"getApplication","()Lcom/apportable/app/VerdeApplication;",1,get_singleton,&state->objects[APPLICATION]);
    M("android/app/Activity","getApplication","()Landroid/app/Application;",0,get_singleton,&state->objects[APPLICATION]);
    M("android/app/Activity","getWindowManager","()Landroid/view/WindowManager;",0,get_singleton,&state->objects[MANAGER]);
    M(classes[APPLICATION],"getWindowManager","()Landroid/view/WindowManager;",0,get_singleton,&state->objects[MANAGER]);
    M(classes[MANAGER],"getDefaultDisplay","()Landroid/view/Display;",0,get_singleton,&state->objects[DISPLAY]);
    M(classes[RESOURCES],"getDisplayMetrics","()Landroid/util/DisplayMetrics;",0,get_singleton,&state->objects[METRICS]);
    M(classes[RESOURCES],"getConfiguration","()Landroid/content/res/Configuration;",0,get_singleton,&state->objects[CONFIGURATION]);
    M(classes[DISPLAY],"getWidth","()I",0,fixed_int,(void *)(intptr_t)WIDTH);
    M(classes[DISPLAY],"getHeight","()I",0,fixed_int,(void *)(intptr_t)HEIGHT);
    M(classes[DISPLAY],"getRotation","()I",0,fixed_int,NULL);M(classes[DISPLAY],"getOrientation","()I",0,fixed_int,NULL);
    M(classes[DISPLAY],"getRefreshRate","()F",0,refresh,NULL);
    M(classes[DISPLAY],"getMetrics","(Landroid/util/DisplayMetrics;)V",0,screen_metrics,NULL);
    M(classes[DISPLAY],"getRealMetrics","(Landroid/util/DisplayMetrics;)V",0,screen_metrics,NULL);
    M(classes[DISPLAY],"getSize","(Landroid/graphics/Point;)V",0,point_size,NULL);
    M(classes[DISPLAY],"getRealSize","(Landroid/graphics/Point;)V",0,point_size,NULL);
    M(classes[METRICS],"<init>","()V",0,metric_defaults,NULL);
    M(classes[METRICS],"setToDefaults","()V",0,metric_defaults,NULL);
    M("com/apportable/utils/ScreenUtils","convertPixelsToDp","(F)F",1,pixels_to_dp,NULL);
    M("com/apportable/ui/Device","create","(Landroid/content/Context;I)Lcom/apportable/ui/Device;",1,device_create,NULL);
    M("com/apportable/ui/Device","enable","()V",0,device_enabled,(void *)1);
    M("com/apportable/ui/Device","disable","()V",0,device_enabled,NULL);
    M("com/apportable/ui/Device","macAddress","()Ljava/lang/String;",1,fixed_text,"");
    M("com/apportable/ui/Device","getODIN","()Ljava/lang/String;",1,fixed_text,"");
    const char *widths[]={"getScreenWidth","getSplashScreenWidth"},*heights[]={"getScreenHeight","getSplashScreenHeight"};
    for(int i=0;i<2;++i){M(classes[ACTIVITY],widths[i],"()I",0,fixed_int,(void *)(intptr_t)WIDTH);M(classes[ACTIVITY],heights[i],"()I",0,fixed_int,(void *)(intptr_t)HEIGHT);}
    M(classes[ACTIVITY],"setContentSize","(FF)V",0,content_size,NULL);
    M(classes[ACTIVITY],"getContentWidth","()I",0,content_dimension,NULL);
    M(classes[ACTIVITY],"getContentHeight","()I",0,content_dimension,(void *)1);
    M("android/graphics/RectF","<init>","(FFFF)V",0,rect_values,NULL);
    M("android/graphics/RectF","<init>","()V",0,rect_values,NULL);
    M("android/graphics/RectF","set","(FFFF)V",0,rect_values,NULL);
    M("android/graphics/RectF","setEmpty","()V",0,rect_values,NULL);
    M("android/graphics/RectF","width","()F",0,rect_measure,NULL);
    M("android/graphics/RectF","height","()F",0,rect_measure,(void *)1);
    M("android/graphics/RectF","centerX","()F",0,rect_measure,(void *)2);
    M("android/graphics/RectF","centerY","()F",0,rect_measure,(void *)3);
    M("android/graphics/RectF","isEmpty","()Z",0,rect_measure,(void *)4);
#undef M
    return (*e)->ExceptionCheck(e)?-1:0;
}
/* The framework ABI exposed by this adapter is Android 4.2/API 17. These
 * values describe the compatibility environment; SDK stubs contain zeroes. */
static const struct {const char *owner,*name,*value;} strings[]={
    {"android/os/Build$VERSION","SDK","17"},
    {"android/os/Build$VERSION","RELEASE","4.2.2"},
    {"android/os/Build$VERSION","CODENAME","REL"},
    {"android/os/Build","MODEL","PlayStation Vita"},
    {"android/os/Build","DEVICE","vita"},
    {"android/os/Build","PRODUCT","wayward-vita"},
    {"android/os/Build","HARDWARE","cortex-a9"},
    {"android/os/Build","MANUFACTURER","Sony"},
    {"android/os/Build","BRAND","Sony"},
    {"android/os/Build","CPU_ABI","armeabi-v7a"},
    {"android/os/Build","CPU_ABI2","armeabi"}
};
int ws_android_init(ws_java *java) {
    JNIEnv *env=ws_java_env(java);if(!env)return -1;
    jclass version=(*env)->FindClass(env,"android/os/Build$VERSION");
    jfieldID sdk=(*env)->GetStaticFieldID(env,version,"SDK_INT","I");
    if(!sdk){(*env)->DeleteLocalRef(env,version);return -1;}
    (*env)->SetStaticIntField(env,version,sdk,17);(*env)->DeleteLocalRef(env,version);
    for(size_t i=0;i<sizeof(strings)/sizeof(*strings);++i){
        jclass cls=(*env)->FindClass(env,strings[i].owner);
        jfieldID field=cls?(*env)->GetStaticFieldID(env,cls,strings[i].name,"Ljava/lang/String;"):NULL;
        jstring text=field?(*env)->NewStringUTF(env,strings[i].value):NULL;
        if(text)(*env)->SetStaticObjectField(env,cls,field,text);
        (*env)->DeleteLocalRef(env,text);(*env)->DeleteLocalRef(env,cls);
        if(!field||!text||(*env)->ExceptionCheck(env))return -1;
    }
    return objects_init(java);
}
void ws_android_destroy(ws_java *java){
    ws_views_destroy(java);
    if(!java||!java->android)return;
    android *state=java->android;JNIEnv *e=ws_java_env(java);
    for(int i=0;i<COUNT;++i)if(state->objects[i])(*e)->DeleteGlobalRef(e,state->objects[i]);
    free(state);java->android=NULL;
}
