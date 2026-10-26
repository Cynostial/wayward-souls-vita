#include "ws_views.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define VIEW "com/apportable/ui/View"
#define WINDOW "com/apportable/ui/Window"
#define PARAMS "android/widget/AbsoluteLayout$LayoutParams"
#define LIMIT 512
typedef struct {ws_java *java;jweak views[LIMIT];jobject window,dialog;int laying_out;} views;
static void fail(JNIEnv *e,const char *type,const char *text){
    jclass c=(*e)->FindClass(e,type);if(c){(*e)->ThrowNew(e,c,text);(*e)->DeleteLocalRef(e,c);}
}
static jfieldID field(JNIEnv *e,jobject o,const char *name,const char *sig){
    jclass c=(*e)->GetObjectClass(e,o);jfieldID f=c?(*e)->GetFieldID(e,c,name,sig):NULL;(*e)->DeleteLocalRef(e,c);return f;
}
static jint number(JNIEnv *e,jobject o,const char *name){return (*e)->GetIntField(e,o,field(e,o,name,"I"));}
static void set_number(JNIEnv *e,jobject o,const char *name,jint v){(*e)->SetIntField(e,o,field(e,o,name,"I"),v);}
static int flag(JNIEnv *e,jobject o,const char *name){return (*e)->GetBooleanField(e,o,field(e,o,name,"Z"));}
static void set_flag(JNIEnv *e,jobject o,const char *name,int v){(*e)->SetBooleanField(e,o,field(e,o,name,"Z"),v!=0);}
static jobject params(JNIEnv *e,jobject o){return (*e)->GetObjectField(e,o,field(e,o,"_wsLayoutParams","Landroid/widget/AbsoluteLayout$LayoutParams;"));}
static jobjectArray children(JNIEnv *e,jobject o){return (jobjectArray)(*e)->GetObjectField(e,o,field(e,o,"_wsChildren","[Lcom/apportable/ui/View;"));}
static int child_count(JNIEnv *e,jobject o){jobjectArray a=children(e,o);int n=a?(*e)->GetArrayLength(e,a):0;(*e)->DeleteLocalRef(e,a);return n;}
static void attach(JNIEnv *e,jobject o,int attached,int depth){
    if(depth>=LIMIT){fail(e,"java/lang/IllegalStateException","View hierarchy is too deep");return;}
    set_flag(e,o,"mAttachedToWindow",attached);if(attached)set_flag(e,o,"mNeedsDisplay",1);
    jobjectArray array=children(e,o);int count=array?(*e)->GetArrayLength(e,array):0;
    for(int i=0;i<count;++i){jobject child=(*e)->GetObjectArrayElement(e,array,i);attach(e,child,attached,depth+1);(*e)->DeleteLocalRef(e,child);}
    (*e)->DeleteLocalRef(e,array);
}
static int contains(JNIEnv *e,jobject root,jobject needle,int depth){
    if((*e)->IsSameObject(e,root,needle))return 1;
    if(depth>=LIMIT)return 1;
    jobjectArray a=children(e,root);int n=a?(*e)->GetArrayLength(e,a):0,result=0;
    for(int i=0;i<n&&!result;++i){jobject child=(*e)->GetObjectArrayElement(e,a,i);result=contains(e,child,needle,depth+1);(*e)->DeleteLocalRef(e,child);}
    (*e)->DeleteLocalRef(e,a);return result;
}
static jobject parent(views *v,JNIEnv *e,jobject child){
    for(int i=0;i<LIMIT;++i)if(v->views[i]){
        jobject o=(*e)->NewLocalRef(e,v->views[i]);if(!o)continue;
        jobjectArray array=children(e,o);int count=array?(*e)->GetArrayLength(e,array):0,found=0;
        for(int k=0;k<count&&!found;++k){jobject item=(*e)->GetObjectArrayElement(e,array,k);found=(*e)->IsSameObject(e,item,child);(*e)->DeleteLocalRef(e,item);}
        (*e)->DeleteLocalRef(e,array);if(found)return o;(*e)->DeleteLocalRef(e,o);
    }
    return NULL;
}
static jvalue insert(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    views *v=ctx;jobject child=a[0].l;if(!child){fail(e,"java/lang/NullPointerException","Subview is null");return (jvalue){0};}
    int count=child_count(e,self),index=n>1?a[1].i:-1;if(index==-1)index=count;
    if(index<0||index>count){fail(e,"java/lang/IndexOutOfBoundsException","Subview index");return (jvalue){0};}
    jobject old_parent=parent(v,e,child);int occupied=old_parent!=NULL;(*e)->DeleteLocalRef(e,old_parent);
    if(occupied||contains(e,child,self,0)){fail(e,"java/lang/IllegalStateException","Subview already has a parent or would create a cycle");return (jvalue){0};}
    jclass c=(*e)->FindClass(e,VIEW);jobjectArray out=(*e)->NewObjectArray(e,count+1,c,NULL);(*e)->DeleteLocalRef(e,c);if(!out)return (jvalue){0};
    jobjectArray old=children(e,self);
    for(int i=0;i<count+1;++i){jobject item=i==index?(*e)->NewLocalRef(e,child):(*e)->GetObjectArrayElement(e,old,i-(i>index));(*e)->SetObjectArrayElement(e,out,i,item);(*e)->DeleteLocalRef(e,item);}
    (*e)->SetObjectField(e,self,field(e,self,"_wsChildren","[Lcom/apportable/ui/View;"),out);
    (*e)->DeleteLocalRef(e,old);(*e)->DeleteLocalRef(e,out);set_flag(e,self,"mNeedsLayout",1);
    if(flag(e,self,"mAttachedToWindow"))attach(e,child,1,0);
    return (jvalue){0};
}
static jvalue remove_child(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)ctx;(void)n;jobjectArray old=children(e,self);int count=old?(*e)->GetArrayLength(e,old):0,index=-1;
    for(int i=0;i<count;++i){jobject item=(*e)->GetObjectArrayElement(e,old,i);if((*e)->IsSameObject(e,item,a[0].l))index=i;(*e)->DeleteLocalRef(e,item);}
    if(index<0){(*e)->DeleteLocalRef(e,old);return (jvalue){0};}
    jclass c=(*e)->FindClass(e,VIEW);jobjectArray out=(*e)->NewObjectArray(e,count-1,c,NULL);(*e)->DeleteLocalRef(e,c);
    if(out){
        for(int i=0;i<count-1;++i){jobject item=(*e)->GetObjectArrayElement(e,old,i+(i>=index));(*e)->SetObjectArrayElement(e,out,i,item);(*e)->DeleteLocalRef(e,item);}
        (*e)->SetObjectField(e,self,field(e,self,"_wsChildren","[Lcom/apportable/ui/View;"),out);
        attach(e,a[0].l,0,0);set_flag(e,self,"mNeedsLayout",1);
    }
    (*e)->DeleteLocalRef(e,out);(*e)->DeleteLocalRef(e,old);return (jvalue){0};
}
static jvalue move_child(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)ctx;(void)n;int count=child_count(e,self),from=a[0].i,to=a[1].i;if(to==count)--to;
    if(from<0||from>=count||to<0||to>=count){fail(e,"java/lang/IndexOutOfBoundsException","Subview move");return (jvalue){0};}
    jobjectArray array=children(e,self);jobject moved=(*e)->GetObjectArrayElement(e,array,from);
    for(int i=from;i!=to;i+=from<to?1:-1){jobject item=(*e)->GetObjectArrayElement(e,array,i+(from<to?1:-1));(*e)->SetObjectArrayElement(e,array,i,item);(*e)->DeleteLocalRef(e,item);}
    (*e)->SetObjectArrayElement(e,array,to,moved);(*e)->DeleteLocalRef(e,moved);(*e)->DeleteLocalRef(e,array);return (jvalue){0};
}
static jvalue key_window(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)a;(void)n;views *v=ctx;
    uintptr_t focus=ws_jni_vm_native(v->java->vm,WINDOW,"nativeOnWindowFocusChanged","(Z)V");
    if(!focus){fail(e,"java/lang/UnsatisfiedLinkError","nativeOnWindowFocusChanged");return (jvalue){0};}
    jobject p=parent(v,e,self);if(p){jvalue arg={.l=self};remove_child(v,e,p,&arg,1);(*e)->DeleteLocalRef(e,p);}
    jobject keep=(*e)->NewGlobalRef(e,self);if(!keep)return (jvalue){0};
    jobject dialog=ws_jni_vm_new_instance(e,"com/apportable/ui/Window$WindowDialog",NULL);
    if(!dialog){(*e)->DeleteGlobalRef(e,keep);return (jvalue){0};}
    (*e)->SetObjectField(e,dialog,field(e,dialog,"mWindow","Lcom/apportable/ui/Window;"),self);
    set_flag(e,dialog,"_wsShowing",1);
    if(v->dialog){set_flag(e,v->dialog,"_wsShowing",0);(*e)->DeleteGlobalRef(e,v->dialog);}
    v->dialog=(*e)->NewGlobalRef(e,dialog);(*e)->DeleteLocalRef(e,dialog);
    if(v->window){attach(e,v->window,0,0);(*e)->DeleteGlobalRef(e,v->window);}
    v->window=keep;attach(e,self,1,0);
    jclass c=(*e)->FindClass(e,WINDOW);((void(*)(JNIEnv *,jclass,jboolean))focus)(e,c,1);(*e)->DeleteLocalRef(e,c);return (jvalue){0};
}
static jvalue key_dialog(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)self;(void)a;(void)n;views *v=ctx;return (jvalue){.l=(*e)->NewLocalRef(e,v->dialog)};
}
static jvalue is_showing(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){(void)ctx;(void)a;(void)n;return (jvalue){.z=flag(e,self,"_wsShowing")};}
static jobject window_attributes(JNIEnv *e,jobject self){
    jfieldID f=field(e,self,"_wsAttributes","Landroid/view/WindowManager$LayoutParams;");jobject attributes=(*e)->GetObjectField(e,self,f);
    if(!attributes){
        attributes=ws_jni_vm_new_instance(e,"android/view/WindowManager$LayoutParams",NULL);
        if(attributes){set_number(e,attributes,"width",-1);set_number(e,attributes,"height",-1);(*e)->SetObjectField(e,self,f,attributes);}
    }
    return attributes;
}
static jvalue dialog_window(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)ctx;(void)a;(void)n;jfieldID f=field(e,self,"_wsWindow","Landroid/view/Window;");jobject window=(*e)->GetObjectField(e,self,f);
    if(!window){window=ws_jni_vm_new_instance(e,"android/view/Window",NULL);if(window)(*e)->SetObjectField(e,self,f,window);}
    return (jvalue){.l=window};
}
static jvalue attributes(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)ctx;
    if(!n)return (jvalue){.l=window_attributes(e,self)};
    if(!a[0].l){fail(e,"java/lang/NullPointerException","Window attributes are null");return (jvalue){0};}
    (*e)->SetObjectField(e,self,field(e,self,"_wsAttributes","Landroid/view/WindowManager$LayoutParams;"),a[0].l);return (jvalue){0};
}
static jvalue window_flags(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)n;int mode=(int)(intptr_t)ctx;jobject p=window_attributes(e,self);if(!p)return (jvalue){0};
    uint32_t old=(uint32_t)number(e,p,"flags"),bits=(uint32_t)a[0].i;
    uint32_t updated=mode==1?old|bits:mode==2?old&~bits:(old&~(uint32_t)a[1].i)|(bits&(uint32_t)a[1].i);
    set_number(e,p,"flags",(jint)updated);(*e)->DeleteLocalRef(e,p);return (jvalue){0};
}
static jvalue system_visibility(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)ctx;(void)n;set_number(e,self,"mSystemVisibility",a[0].i);set_flag(e,self,"mKeepSystemVisibilityFlag",1);return (jvalue){0};
}
static uintptr_t native(views *v,JNIEnv *e,const char *name,const char *sig){
    uintptr_t p=ws_jni_vm_native(v->java->vm,VIEW,name,sig);
    if(!p)fail(e,"java/lang/UnsatisfiedLinkError",name);
    return p;
}
/* Match Java float-to-int conversion, including NaN and saturation. */
static jint jint_float(float f){return isnan(f)?0:f>=2147483648.0f?INT32_MAX:f<=-2147483648.0f?INT32_MIN:(jint)f;}
static jvalue set_frame(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)n;views *v=ctx;if(!a[0].l){fail(e,"java/lang/NullPointerException","View frame is null");return (jvalue){0};}
    float edges[4];const char *names[]={"left","top","right","bottom"};
    for(int i=0;i<4;++i)edges[i]=(*e)->GetFloatField(e,a[0].l,field(e,a[0].l,names[i],"F"));
    jobject p=params(e,self);int existing=p!=NULL;
    if(!p)p=ws_jni_vm_new_instance(e,PARAMS,NULL);
    if(!p)return (jvalue){0};
    jint x=jint_float(edges[0]),y=jint_float(edges[1]),w=jint_float(edges[2]-edges[0]),h=jint_float(edges[3]-edges[1]);
    set_number(e,p,"x",x);set_number(e,p,"y",y);set_number(e,p,"width",w);set_number(e,p,"height",h);
    if(!existing)(*e)->SetObjectField(e,self,field(e,self,"_wsLayoutParams","Landroid/widget/AbsoluteLayout$LayoutParams;"),p);
    (*e)->DeleteLocalRef(e,p);
    if(existing){uintptr_t f=native(v,e,"nativeSetFrame","(IIII)V");
        if(f)((void(*)(JNIEnv *,jobject,jint,jint,jint,jint))f)(e,self,x,y,(jint)((uint32_t)x+(uint32_t)w),(jint)((uint32_t)y+(uint32_t)h));
        set_flag(e,self,"mNeedsLayout",1);
    }
    return (jvalue){0};
}
static jvalue create(void *ctx,JNIEnv *e,jobject cls,const jvalue *a,size_t n){
    views *v=ctx;if(!a[0].l){fail(e,"java/lang/NullPointerException","View context is null");return (jvalue){0};}
    uintptr_t draw=native(v,e,"isDrawRectImplemented","(I)Z");if(!draw)return (jvalue){0};
    int slot;for(slot=0;slot<LIMIT;++slot)if(!v->views[slot]||(*e)->IsSameObject(e,v->views[slot],NULL))break;
    if(slot==LIMIT){fail(e,"java/lang/OutOfMemoryError","View capacity exceeded");return (jvalue){0};}
    const char *owner=ws_jni_class_name(e,cls);jobject o=ws_jni_vm_new_instance(e,owner,NULL);if(!o)return (jvalue){0};
    if(v->views[slot])(*e)->DeleteWeakGlobalRef(e,v->views[slot]);
    v->views[slot]=(*e)->NewWeakGlobalRef(e,o);
    if(!v->views[slot]){(*e)->DeleteLocalRef(e,o);return (jvalue){0};}
    set_number(e,o,"mObject",a[1].i);set_flag(e,o,"mUserInteractionEnabled",1);
    set_flag(e,o,"mNeedsLayout",1);set_flag(e,o,"mNeedsDisplay",1);
    jobject rect=n==3?a[2].l:ws_jni_vm_new_instance(e,"android/graphics/RectF",NULL);
    jvalue arg={.l=rect};set_frame(v,e,o,&arg,1);if(n!=3)(*e)->DeleteLocalRef(e,rect);
    if(!(*e)->ExceptionCheck(e))set_flag(e,o,"_wsWillNotDraw",!((jboolean(*)(JNIEnv *,jobject,jint))draw)(e,o,a[1].i));
    if((*e)->ExceptionCheck(e)){(*e)->DeleteLocalRef(e,o);return (jvalue){0};}
    return (jvalue){.l=o};
}
static jvalue object(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){(void)ctx;(void)a;(void)n;return (jvalue){.i=number(e,self,"mObject")};}
static jvalue android_frame(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)ctx;(void)n;const char *names[]={"_wsLeft","_wsTop","_wsRight","_wsBottom"};int changed=0;
    for(int i=0;i<4;++i){if(number(e,self,names[i])!=a[i].i)changed=1;set_number(e,self,names[i],a[i].i);}
    return (jvalue){.z=changed};
}
static jvalue android_edge(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)a;(void)n;const char *name=ctx;
    if(!strcmp(name,"width"))return (jvalue){.i=(jint)((uint32_t)number(e,self,"_wsRight")-(uint32_t)number(e,self,"_wsLeft"))};
    if(!strcmp(name,"height"))return (jvalue){.i=(jint)((uint32_t)number(e,self,"_wsBottom")-(uint32_t)number(e,self,"_wsTop"))};
    return (jvalue){.i=number(e,self,name)};
}
static jvalue invalidate(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){(void)ctx;(void)a;(void)n;set_number(e,self,"mObject",0);return (jvalue){0};}
static jvalue dimensions(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)self;(void)n;const char *name=ctx;if(!a[0].l){fail(e,"java/lang/NullPointerException","View is null");return (jvalue){0};}
    jobject p=params(e,a[0].l);jint value=p?number(e,p,name):0;(*e)->DeleteLocalRef(e,p);return (jvalue){.i=value};
}
static jvalue set_boolean(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){(void)n;set_flag(e,self,ctx,a[0].z);return (jvalue){0};}
static jvalue dirty(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){(void)a;(void)n;set_flag(e,self,ctx,1);return (jvalue){0};}
static jvalue alpha(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){(void)ctx;(void)n;set_flag(e,self,"mHiddenByAlpha",a[0].f<0.01);return (jvalue){0};}
static jvalue bounds(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    /* The APK implements View.setBounds as an empty method. */
    (void)ctx;(void)e;(void)self;(void)a;(void)n;return (jvalue){0};
}
static jvalue transform(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)ctx;(void)n;
    if(a[0].l){fail(e,"java/lang/UnsupportedOperationException","Android view matrix rendering is not implemented");return (jvalue){0};}
    (*e)->SetObjectField(e,self,field(e,self,"mTransform","Landroid/graphics/Matrix;"),NULL);return (jvalue){0};
}
static jvalue debug_tag(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)ctx;(void)n;(*e)->SetObjectField(e,self,field(e,self,"_wsDebugTag","Ljava/lang/String;"),a[0].l);return (jvalue){0};
}
static jvalue layout(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)a;(void)n;views *v=ctx;
    if(flag(e,self,"mNeedsLayout")&&number(e,self,"mObject")){
        uintptr_t f=native(v,e,"callLayoutSubviews","(I)V");
        if(f){set_flag(e,self,"mNeedsLayout",0);((void(*)(JNIEnv *,jobject,jint))f)(e,self,number(e,self,"mObject"));}
    }
    return (jvalue){0};
}
int ws_views_layout(ws_java *j){
    views *v=j?j->views:NULL;if(!v)return -1;JNIEnv *e=ws_java_env(j);if(!e||v->laying_out)return -1;v->laying_out=1;
    for(int i=0;i<LIMIT&&!(*e)->ExceptionCheck(e);++i)if(v->views[i]){
        jobject o=(*e)->NewLocalRef(e,v->views[i]);
        if(o&&flag(e,o,"mAttachedToWindow"))layout(v,e,o,NULL,0);
        (*e)->DeleteLocalRef(e,o);
    }
    v->laying_out=0;return (*e)->ExceptionCheck(e)?-1:0;
}
int ws_views_init(ws_java *j){
    views *v=calloc(1,sizeof(*v));if(!v)return -1;v->java=j;j->views=v;
#define F(n,s) if(ws_jni_vm_define_field(j->vm,VIEW,n,s,0))return -1
    F("_wsLayoutParams","Landroid/widget/AbsoluteLayout$LayoutParams;");F("_wsWillNotDraw","Z");F("_wsDebugTag","Ljava/lang/String;");F("_wsChildren","[Lcom/apportable/ui/View;");
#undef F
    const char *edges[]={"_wsLeft","_wsTop","_wsRight","_wsBottom"};
    for(int i=0;i<4;++i)if(ws_jni_vm_define_field(j->vm,"android/view/View",edges[i],"I",0))return -1;
    if(ws_jni_vm_define_field(j->vm,"android/app/Dialog","_wsShowing","Z",0))return -1;
    if(ws_jni_vm_define_field(j->vm,"android/app/Dialog","_wsWindow","Landroid/view/Window;",0)||ws_jni_vm_define_field(j->vm,"android/view/Window","_wsAttributes","Landroid/view/WindowManager$LayoutParams;",0))return -1;
#define M(c,n,s,t,f,p) if(ws_jni_vm_define_method(j->vm,c,n,s,t,f,p))return -1
    M("android/view/View","setFrame","(IIII)Z",0,android_frame,v);
    M("android/view/View","getLeft","()I",0,android_edge,"_wsLeft");M("android/view/View","getTop","()I",0,android_edge,"_wsTop");
    M("android/view/View","getRight","()I",0,android_edge,"_wsRight");M("android/view/View","getBottom","()I",0,android_edge,"_wsBottom");
    M("android/view/View","getWidth","()I",0,android_edge,"width");M("android/view/View","getHeight","()I",0,android_edge,"height");
    M(VIEW,"create","(Landroid/content/Context;I)Lcom/apportable/ui/View;",1,create,v);
    M(VIEW,"create","(Landroid/content/Context;ILandroid/graphics/RectF;)Lcom/apportable/ui/View;",1,create,v);
    M(WINDOW,"create","(Landroid/content/Context;I)Lcom/apportable/ui/Window;",1,create,v);
    M(VIEW,"object","()I",0,object,v);M(VIEW,"invalidateObject","()V",0,invalidate,v);
    M(VIEW,"setFrame","(Landroid/graphics/RectF;)V",0,set_frame,v);M(VIEW,"setBounds","(Landroid/graphics/RectF;)V",0,bounds,v);
    M(VIEW,"getLayoutWidth","(Lcom/apportable/ui/View;)I",1,dimensions,"width");
    M(VIEW,"getLayoutHeight","(Lcom/apportable/ui/View;)I",1,dimensions,"height");
    M(VIEW,"setHidden","(Z)V",0,set_boolean,"mHidden");M(VIEW,"setClipsToBounds","(Z)V",0,set_boolean,"mClipsToBounds");
    M(VIEW,"setUserInteractionEnabled","(Z)V",0,set_boolean,"mUserInteractionEnabled");
    M(VIEW,"_setAlpha","(F)V",0,alpha,v);M(VIEW,"setTransform","(Landroid/graphics/Matrix;)V",0,transform,v);
    M(VIEW,"setNeedsLayout","()V",0,dirty,"mNeedsLayout");M(VIEW,"layoutIfNeeded","()V",0,layout,v);
    M(VIEW,"setNeedsDisplay","()V",0,dirty,"mNeedsDisplay");M(VIEW,"setNeedsDisplayInRect","(Landroid/graphics/RectF;)V",0,dirty,"mNeedsDisplay");
    M(VIEW,"setDebugTag","(Ljava/lang/String;)V",0,debug_tag,v);
    M(VIEW,"addSubview","(Lcom/apportable/ui/View;)V",0,insert,v);M(VIEW,"insertSubview","(Lcom/apportable/ui/View;I)V",0,insert,v);
    M(VIEW,"removeSubview","(Lcom/apportable/ui/View;)V",0,remove_child,v);M(VIEW,"moveSubview","(II)V",0,move_child,v);
    M(WINDOW,"makeKeyAndOrderFront","()V",0,key_window,v);
    M(WINDOW,"getKeyWindowDialog","()Landroid/app/Dialog;",1,key_dialog,v);
    M("android/app/Dialog","isShowing","()Z",0,is_showing,v);
    M("android/app/Dialog","getWindow","()Landroid/view/Window;",0,dialog_window,v);
    M("android/view/Window","getAttributes","()Landroid/view/WindowManager$LayoutParams;",0,attributes,v);
    M("android/view/Window","setAttributes","(Landroid/view/WindowManager$LayoutParams;)V",0,attributes,v);
    M("android/view/Window","addFlags","(I)V",0,window_flags,(void *)1);M("android/view/Window","clearFlags","(I)V",0,window_flags,(void *)2);
    M("android/view/Window","setFlags","(II)V",0,window_flags,NULL);
    M(WINDOW,"_setSystemUiVisibility","(I)V",0,system_visibility,v);
#undef M
    return 0;
}
void ws_views_destroy(ws_java *j){
    views *v=j?j->views:NULL;if(!v)return;JNIEnv *e=ws_java_env(j);
    if(v->window)(*e)->DeleteGlobalRef(e,v->window);
    if(v->dialog)(*e)->DeleteGlobalRef(e,v->dialog);
    for(int i=0;i<LIMIT;++i)if(v->views[i])(*e)->DeleteWeakGlobalRef(e,v->views[i]);
    free(v);j->views=NULL;
}
