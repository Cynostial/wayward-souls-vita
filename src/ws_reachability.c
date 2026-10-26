#include "ws_reachability.h"
#include <stdint.h>

static const char owner[]="com/apportable/net/Reachability";
static jvalue construct(void *context,JNIEnv *env,jobject self,const jvalue *args,size_t count){
    (void)context;(void)count;
    jclass cls=(*env)->GetObjectClass(env,self);
    jfieldID ctx=(*env)->GetFieldID(env,cls,"mContext","Landroid/content/Context;");
    jfieldID states=(*env)->GetFieldID(env,cls,"mState","[I");
    jintArray array=(*env)->NewIntArray(env,20);
    if(ctx&&states&&array){
        (*env)->SetObjectField(env,self,ctx,args[0].l);
        (*env)->SetObjectField(env,self,states,array);
    }
    (*env)->DeleteLocalRef(env,array);(*env)->DeleteLocalRef(env,cls);
    return (jvalue){0};
}
static jvalue disconnected(void *context,JNIEnv *env,jobject self,const jvalue *args,size_t count){
    (void)args;(void)count;ws_java *java=context;
    uintptr_t entry=ws_jni_vm_native(java->vm,owner,"stateChanged","(III)V");
    if(!entry){
        jclass error=(*env)->FindClass(env,"java/lang/IllegalStateException");
        (*env)->ThrowNew(env,error,"Reachability native callback is not registered");
        (*env)->DeleteLocalRef(env,error);return (jvalue){0};
    }
    jclass cls=(*env)->GetObjectClass(env,self);
    jfieldID field=(*env)->GetFieldID(env,cls,"mState","[I");
    jintArray array=field?(*env)->GetObjectField(env,self,field):NULL;
    if(array&&(*env)->GetArrayLength(env,array)==20){
        /* Android mobile(0) and Wi-Fi(1) are unavailable to this offline port.
         * Match the APK's disconnected state (6), without AVAILABLE/CONNECTED
         * bits. Notify the ORIGINAL native reachability implementation so it
         * updates SCNetworkReachability targets instead of hanging CharSelect.
         * Repeated starts/broadcasts do not emit duplicate transitions. */
        jint state[2];(*env)->GetIntArrayRegion(env,array,0,2,state);
        for(jint type=0;type<2&&!(*env)->ExceptionCheck(env);++type){
            if(state[type]==6)continue;
            ((void (*)(JNIEnv *,jobject,jint,jint,jint))entry)(env,self,type,state[type],6);
            if(!(*env)->ExceptionCheck(env)){
                state[type]=6;(*env)->SetIntArrayRegion(env,array,type,1,&state[type]);
            }
        }
    }
    (*env)->DeleteLocalRef(env,array);(*env)->DeleteLocalRef(env,cls);
    return (jvalue){0};
}
int ws_reachability_init(ws_java *java){
    JNIEnv *env=ws_java_env(java);if(!env)return -1;
    jclass cls=(*env)->FindClass(env,owner);if(!cls)return -1;
    static const struct {const char *name;jint value;} constants[]={
        {"AUTHENTICATING",1},{"AVAILABLE",16},{"BLOCKED",2},{"CAPTIVE_PORTAL_CHECK",3},
        {"CONNECTED",4},{"CONNECTING",5},{"DISCONNECTED",6},{"DISCONNECTING",7},
        {"FAILED",8},{"FAILOVER",32},{"IDLE",9},{"OBTAINING_IPADDR",10},
        {"ROAMING",64},{"SCANNING",11},{"STATE_MASK",240},{"STATUS_MASK",15},
        {"SUSPENDED",12},{"VERIFYING_POOR_LINK",13}
    };
    for(unsigned i=0;i<sizeof(constants)/sizeof(*constants);++i){
        jfieldID field=(*env)->GetStaticFieldID(env,cls,constants[i].name,"I");
        if(field)(*env)->SetStaticIntField(env,cls,field,constants[i].value);
    }
    (*env)->DeleteLocalRef(env,cls);
    if((*env)->ExceptionCheck(env))return -1;
    return ws_jni_vm_define_method(java->vm,owner,"<init>","(Landroid/content/Context;)V",0,construct,NULL)||
        ws_jni_vm_define_method(java->vm,owner,"start","()V",0,disconnected,java)||
        ws_jni_vm_define_method(java->vm,owner,"onReceive","(Landroid/content/Context;Landroid/content/Intent;)V",0,disconnected,java);
}
