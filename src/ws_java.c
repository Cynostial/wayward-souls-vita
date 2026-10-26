#include "ws_java.h"
#include "ws_runloop.h"
#include "ws_android.h"
#include "ws_preferences.h"
#include "ws_news.h"
#include "ws_reachability.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef struct {char kind;const char *owner,*name,*signature;int is_static;} declaration;
static const declaration declarations[]={
#define WS_EXTERNAL(c) {'E',c,NULL,NULL,0},
#define WS_CLASS(c,p) {'C',c,p,NULL,0},
#define WS_METHOD(c,n,s,t) {'M',c,n,s,t},
#define WS_FIELD(c,n,s,t) {'F',c,n,s,t},
#include "ws_android_schema.inc"
#include "ws_android_internal.inc"
#include "ws_java_schema.inc"
#undef WS_EXTERNAL
#undef WS_CLASS
#undef WS_METHOD
#undef WS_FIELD
};
static int define_declarations(ws_jni_vm *vm) {
    for(size_t i=0;i<sizeof(declarations)/sizeof(*declarations);++i){
        const declaration *d=&declarations[i];int error;
        switch(d->kind){
        case 'E':error=ws_jni_vm_has_class(vm,d->owner)?0:ws_jni_vm_define_class(vm,d->owner);break;
        case 'C':error=ws_jni_vm_define_subclass(vm,d->owner,d->name);break;
        case 'M':error=ws_jni_vm_define_method(vm,d->owner,d->name,d->signature,d->is_static,NULL,NULL);break;
        default:error=ws_jni_vm_define_field(vm,d->owner,d->name,d->signature,d->is_static);break;
        }
        if(error){fprintf(stderr,"Wayward declaration failed: %c %s %s %s\n",d->kind,d->owner,d->name?d->name:"",d->signature?d->signature:"");return -1;}
    }
    return 0;
}
JNIEnv *ws_java_env(ws_java *j) {
    if(!j||!j->handle)return NULL;
    JNIEnv *env=NULL;
    if((*j->handle)->GetEnv(j->handle,(void **)&env,JNI_VERSION_1_4)==JNI_EDETACHED)
        if((*j->handle)->AttachCurrentThread(j->handle,&env,NULL))return NULL;
    return env;
}
int ws_java_check(ws_java *j,const char *where) {
    JNIEnv *env=ws_java_env(j);if(!env)return -1;
    if(!(*env)->ExceptionCheck(env))return 0;
    fprintf(stderr,"Wayward JNI exception at %s\n",where);
    (*env)->ExceptionDescribe(env);return -1;
}
static jvalue loader(void *context,JNIEnv *env,jobject self,const jvalue *a,size_t n) {
    (void)self;(void)a;(void)n;ws_java *j=context;
    jvalue out={.l=(*env)->NewLocalRef(env,j->loader)};return out;
}
static jvalue class_for_name(void *context,JNIEnv *env,jobject self,const jvalue *a,size_t n) {
    (void)context;(void)self;(void)n;jvalue out={0};
    const char *utf=(*env)->GetStringUTFChars(env,a[0].l,NULL);if(!utf)return out;
    char name[256];size_t len=strlen(utf);
    if(len<sizeof(name)){for(size_t i=0;i<=len;++i)name[i]=utf[i]=='.'?'/':utf[i];out.l=(*env)->FindClass(env,name);}
    else {jclass e=(*env)->FindClass(env,"java/lang/ClassNotFoundException");(*env)->ThrowNew(env,e,utf);}
    (*env)->ReleaseStringUTFChars(env,a[0].l,utf);return out;
}
static jvalue class_name(void *context,JNIEnv *env,jobject self,const jvalue *a,size_t n) {
    (void)context;(void)a;(void)n;jvalue out={0};
    const char *name=ws_jni_class_name(env,self);if(!name)return out;
    char java[256];size_t len=strlen(name);if(len>=sizeof(java))return out;
    for(size_t i=0;i<=len;++i)java[i]=name[i]=='/'?'.':name[i];
    out.l=(*env)->NewStringUTF(env,java);return out;
}
static jvalue neon(void *context,JNIEnv *env,jobject self,const jvalue *a,size_t n) {
    (void)context;(void)env;(void)self;(void)a;(void)n;jvalue out={.z=JNI_TRUE};return out;
}
static jvalue object_init(void *context,JNIEnv *env,jobject self,const jvalue *a,size_t n) {
    (void)context;(void)env;(void)self;(void)a;(void)n;return (jvalue){0};
}
static jvalue object_class(void *context,JNIEnv *env,jobject self,const jvalue *a,size_t n) {
    (void)context;(void)a;(void)n;return (jvalue){.l=(*env)->GetObjectClass(env,self)};
}
static jvalue object_hash(void *context,JNIEnv *env,jobject self,const jvalue *a,size_t n) {
    (void)context;(void)a;(void)n;return (jvalue){.i=ws_jni_identity_hash(env,self)};
}
static jvalue object_equals(void *context,JNIEnv *env,jobject self,const jvalue *a,size_t n) {
    (void)context;(void)n;return (jvalue){.z=(*env)->IsSameObject(env,self,a[0].l)};
}
static jvalue object_wait(void *context,JNIEnv *env,jobject self,const jvalue *a,size_t n) {
    (void)context;ws_jni_monitor_wait(env,self,n?a[0].j:0,n>1?a[1].i:0);return (jvalue){0};
}
static jvalue object_notify(void *context,JNIEnv *env,jobject self,const jvalue *a,size_t n) {
    (void)context;(void)a;(void)n;ws_jni_monitor_notify(env,self,0);return (jvalue){0};
}
static jvalue object_notify_all(void *context,JNIEnv *env,jobject self,const jvalue *a,size_t n) {
    (void)context;(void)a;(void)n;ws_jni_monitor_notify(env,self,1);return (jvalue){0};
}
static jvalue object_string(void *context,JNIEnv *env,jobject self,const jvalue *a,size_t n) {
    (void)context;(void)a;(void)n;
    jclass cls=(*env)->GetObjectClass(env,self);const char *name=ws_jni_class_name(env,cls);
    char text[280];jvalue out={0};
    if(name){
        jmethodID method=(*env)->GetMethodID(env,cls,"hashCode","()I");
        jint hash=method?(*env)->CallIntMethod(env,self,method):0;
        if((*env)->ExceptionCheck(env)){(*env)->DeleteLocalRef(env,cls);return out;}
        snprintf(text,sizeof(text),"%s@%x",name,(unsigned)hash);
        for(char *p=text;*p;++p)if(*p=='/')*p='.';
        out.l=(*env)->NewStringUTF(env,text);
    }
    (*env)->DeleteLocalRef(env,cls);return out;
}
static jvalue string_self(void *context,JNIEnv *env,jobject self,const jvalue *a,size_t n) {
    (void)context;(void)a;(void)n;return (jvalue){.l=(*env)->NewLocalRef(env,self)};
}
static jfieldID throwable_field(JNIEnv *env,const char *name,const char *signature) {
    jclass cls=(*env)->FindClass(env,"java/lang/Throwable");
    jfieldID id=cls?(*env)->GetFieldID(env,cls,name,signature):NULL;
    (*env)->DeleteLocalRef(env,cls);return id;
}
static jvalue throwable_init(void *context,JNIEnv *env,jobject self,const jvalue *a,size_t n) {
    (void)context;jfieldID field=throwable_field(env,"detailMessage","Ljava/lang/String;");
    if(field)(*env)->SetObjectField(env,self,field,n?a[0].l:NULL);
    return (jvalue){0};
}
static jvalue throwable_message(void *context,JNIEnv *env,jobject self,const jvalue *a,size_t n) {
    (void)context;(void)a;(void)n;jvalue out={0};
    if(ws_jni_native_exception_message(env,self,&out.l))return out;
    jfieldID field=throwable_field(env,"detailMessage","Ljava/lang/String;");
    if(field)out.l=(*env)->GetObjectField(env,self,field);
    return out;
}
static jvalue throwable_localized(void *context,JNIEnv *env,jobject self,const jvalue *a,size_t n) {
    (void)context;(void)a;(void)n;jvalue out={0};jclass cls=(*env)->GetObjectClass(env,self);
    jmethodID method=(*env)->GetMethodID(env,cls,"getMessage","()Ljava/lang/String;");
    if(method)out.l=(*env)->CallObjectMethod(env,self,method);
    (*env)->DeleteLocalRef(env,cls);return out;
}
static jvalue throwable_cause(void *context,JNIEnv *env,jobject self,const jvalue *a,size_t n) {
    (void)context;(void)a;(void)n;jvalue out={0};jfieldID field=throwable_field(env,"cause","Ljava/lang/Throwable;");
    if(field)out.l=(*env)->GetObjectField(env,self,field);
    return out;
}
static jvalue throwable_string(void *context,JNIEnv *env,jobject self,const jvalue *a,size_t n) {
    (void)context;(void)a;(void)n;jvalue out={0};jclass cls=(*env)->GetObjectClass(env,self);
    const char *name=ws_jni_class_name(env,cls);
    jmethodID method=(*env)->GetMethodID(env,cls,"getLocalizedMessage","()Ljava/lang/String;");
    jstring message=method?(*env)->CallObjectMethod(env,self,method):NULL;
    const char *text=message?(*env)->GetStringUTFChars(env,message,NULL):NULL;
    if(name&&!(*env)->ExceptionCheck(env)){
        size_t bytes=strlen(name)+(text?strlen(text)+2:0)+1;char *result=malloc(bytes);
        if(result){
            size_t length=strlen(name);memcpy(result,name,length+1);
            for(size_t i=0;i<length;++i)if(result[i]=='/')result[i]='.';
            if(text){memcpy(result+length,": ",2);strcpy(result+length+2,text);}
            out.l=(*env)->NewStringUTF(env,result);free(result);
        }else{
            jclass error=(*env)->FindClass(env,"java/lang/OutOfMemoryError");
            (*env)->ThrowNew(env,error,"Throwable.toString");(*env)->DeleteLocalRef(env,error);
        }
    }
    if(text)(*env)->ReleaseStringUTFChars(env,message,text);
    (*env)->DeleteLocalRef(env,message);(*env)->DeleteLocalRef(env,cls);return out;
}
static jvalue string_hash(void *context,JNIEnv *env,jobject self,const jvalue *a,size_t n) {
    (void)context;(void)a;(void)n;jsize length=(*env)->GetStringLength(env,self);
    const jchar *text=(*env)->GetStringChars(env,self,NULL);uint32_t hash=0;
    if(text){for(jsize i=0;i<length;++i)hash=31*hash+text[i];(*env)->ReleaseStringChars(env,self,text);}
    return (jvalue){.i=(jint)hash};
}
static jvalue string_equals(void *context,JNIEnv *env,jobject self,const jvalue *a,size_t n) {
    (void)context;(void)n;jvalue out={0};if(!a[0].l)return out;
    jclass cls=(*env)->FindClass(env,"java/lang/String");
    int valid=(*env)->IsInstanceOf(env,a[0].l,cls);(*env)->DeleteLocalRef(env,cls);
    if(!valid)return out;
    jsize length=(*env)->GetStringLength(env,self);
    if(length!=(*env)->GetStringLength(env,a[0].l))return out;
    const jchar *left=(*env)->GetStringChars(env,self,NULL);
    const jchar *right=left?(*env)->GetStringChars(env,a[0].l,NULL):NULL;
    if(right)out.z=!memcmp(left,right,(size_t)length*sizeof(jchar));
    if(left)(*env)->ReleaseStringChars(env,self,left);
    if(right)(*env)->ReleaseStringChars(env,a[0].l,right);
    return out;
}
static jvalue cpuinfo(void *context,JNIEnv *env,jobject self,const jvalue *a,size_t n) {
    (void)context;(void)self;(void)a;(void)n;
    jvalue out={.l=(*env)->NewStringUTF(env,"Processor: ARMv7 Cortex-A9\nFeatures: vfp neon vfpv3\n")};return out;
}
int ws_java_init(ws_java *j) {
    if(!j||j->vm)return -1;
    j->vm=ws_jni_vm_create(4096);if(!j->vm)return -1;
    j->handle=ws_jni_vm_handle(j->vm);
    /* Android SDK/APK inheritance and signatures; declarations are not bodies. */
    if(define_declarations(j->vm))goto bad;
    if(ws_jni_vm_define_class(j->vm,"java/lang/ClassLoader"))goto bad;
    if(ws_jni_vm_define_field(j->vm,"java/lang/Throwable","detailMessage","Ljava/lang/String;",0)||
       ws_jni_vm_define_field(j->vm,"java/lang/Throwable","cause","Ljava/lang/Throwable;",0))goto bad;
#define METHOD(c,n,s,t,f) if(ws_jni_vm_define_method(j->vm,c,n,s,t,f,j))goto bad
    METHOD("java/lang/Object","<init>","()V",0,object_init);
    METHOD("java/lang/Object","toString","()Ljava/lang/String;",0,object_string);
    METHOD("java/lang/Object","hashCode","()I",0,object_hash);
    METHOD("java/lang/Object","equals","(Ljava/lang/Object;)Z",0,object_equals);
    METHOD("java/lang/Object","getClass","()Ljava/lang/Class;",0,object_class);
    METHOD("java/lang/Object","wait","()V",0,object_wait);
    METHOD("java/lang/Object","wait","(J)V",0,object_wait);
    METHOD("java/lang/Object","wait","(JI)V",0,object_wait);
    METHOD("java/lang/Object","notify","()V",0,object_notify);
    METHOD("java/lang/Object","notifyAll","()V",0,object_notify_all);
    METHOD("java/lang/String","toString","()Ljava/lang/String;",0,string_self);
    METHOD("java/lang/String","hashCode","()I",0,string_hash);
    METHOD("java/lang/String","equals","(Ljava/lang/Object;)Z",0,string_equals);
    METHOD("java/lang/Throwable","<init>","()V",0,throwable_init);
    METHOD("java/lang/Throwable","<init>","(Ljava/lang/String;)V",0,throwable_init);
    METHOD("java/lang/Throwable","getMessage","()Ljava/lang/String;",0,throwable_message);
    METHOD("java/lang/Throwable","getLocalizedMessage","()Ljava/lang/String;",0,throwable_localized);
    METHOD("java/lang/Throwable","getCause","()Ljava/lang/Throwable;",0,throwable_cause);
    METHOD("java/lang/Throwable","toString","()Ljava/lang/String;",0,throwable_string);
    METHOD("java/lang/Class","getClassLoader","()Ljava/lang/ClassLoader;",0,loader);
    METHOD("java/lang/Class","forName","(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;",1,class_for_name);
    METHOD("java/lang/Class","getName","()Ljava/lang/String;",0,class_name);
    METHOD("java/lang/ClassLoader","loadClass","(Ljava/lang/String;)Ljava/lang/Class;",0,class_for_name);
    METHOD("com/apportable/LibraryManager","neonSupported","()Z",1,neon);
    METHOD("com/apportable/LibraryManager","getCPUInfo","()Ljava/lang/String;",1,cpuinfo);
#undef METHOD
    JNIEnv *env=ws_java_env(j);if(!env)goto bad;
    jobject local=ws_jni_vm_new_instance(env,"java/lang/ClassLoader",NULL);
    j->loader=(*env)->NewGlobalRef(env,local);(*env)->DeleteLocalRef(env,local);
    if(!j->loader||ws_java_check(j,"java initialization"))goto bad;
    if(ws_runloop_init(j)||ws_android_init(j)||ws_preferences_init(j)||ws_news_init(j)||ws_reachability_init(j))goto bad;
    return 0;
bad:
    ws_java_destroy(j);return -1;
}
int ws_java_destroy(ws_java *j) {
    if(!j||!j->vm)return 0;
    if(!ws_jni_vm_exclusive(j->vm))return JNI_ERR;
    int pending=ws_runloop_destroy(j);if(pending)return pending;
    ws_preferences_destroy(j);
    ws_news_destroy(j);
    ws_android_destroy(j);
    JNIEnv *env=ws_java_env(j);
    if(env&&j->loader){(*env)->DeleteGlobalRef(env,j->loader);j->loader=NULL;}
    int e=ws_jni_vm_destroy(j->vm);if(!e)memset(j,0,sizeof(*j));return e;
}
