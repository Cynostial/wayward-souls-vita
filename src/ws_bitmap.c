#include "ws_bitmap.h"
#include "ws_memory.h"
#include <stdatomic.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#define BITMAP_MAGIC 0x57424d50u
typedef struct pixel_pin {struct pixel_pin *next;jobject reference;} pixel_pin;
typedef struct {uint32_t magic;pthread_mutex_t lock;ws_bitmap_info info;void *pixels;size_t bytes;unsigned locks;int recycled;pixel_pin *pins;} bitmap;
static atomic_size_t total;
static void exception(JNIEnv *e,const char *type,const char *message){jclass c=(*e)->FindClass(e,type);if(c){(*e)->ThrowNew(e,c,message);(*e)->DeleteLocalRef(e,c);}}
static void release_pixels(bitmap *b){if(b->pixels){ws_free(b->pixels);b->pixels=NULL;atomic_fetch_sub(&total,b->bytes);}}
static void destroy(void *payload){bitmap *b=payload;release_pixels(b);while(b->pins){pixel_pin *next=b->pins->next;free(b->pins);b->pins=next;}pthread_mutex_destroy(&b->lock);b->magic=0;free(b);}
static bitmap *get(JNIEnv *e,jobject object){
    if(!e||!object)return NULL;
    jclass c=(*e)->FindClass(e,"android/graphics/Bitmap");if(!c)return NULL;
    int valid=(*e)->IsInstanceOf(e,object,c);(*e)->DeleteLocalRef(e,c);if(!valid)return NULL;
    bitmap *b=ws_jni_vm_instance_data(e,object);return b&&b->magic==BITMAP_MAGIC?b:NULL;
}
static jobject create(JNIEnv *e,int width,int height){
    if(width<=0||height<=0){exception(e,"java/lang/IllegalArgumentException","Bitmap dimensions must be positive");return NULL;}
    uint64_t bytes=(uint64_t)(unsigned)width*(unsigned)height*4;
    if(bytes>WS_BITMAP_MAX_BYTES)goto too_large;
    size_t old=atomic_load(&total);
    do {if(old>WS_BITMAP_TOTAL_BYTES-(size_t)bytes)goto too_large;}
    while(!atomic_compare_exchange_weak(&total,&old,old+(size_t)bytes));
    bitmap *b=calloc(1,sizeof(*b));if(!b){atomic_fetch_sub(&total,(size_t)bytes);goto too_large;}
    if(pthread_mutex_init(&b->lock,NULL)){free(b);atomic_fetch_sub(&total,(size_t)bytes);goto too_large;}
    b->bytes=(size_t)bytes;b->pixels=ws_calloc(1,b->bytes);
    if(!b->pixels){pthread_mutex_destroy(&b->lock);free(b);atomic_fetch_sub(&total,(size_t)bytes);goto too_large;}
    b->magic=BITMAP_MAGIC;b->info=(ws_bitmap_info){(uint32_t)width,(uint32_t)height,(uint32_t)width*4,1,2}; /* RGBA_8888, premultiplied */
    jobject object=ws_jni_vm_new_owned_instance(e,"android/graphics/Bitmap",b,destroy);if(!object)destroy(b);return object;
too_large:exception(e,"java/lang/OutOfMemoryError","Bitmap memory budget exceeded");return NULL;
}
int ws_android_bitmap_get_info(JNIEnv *e,jobject object,ws_bitmap_info *info){
    if(!e||!info)return -1;
    if((*e)->ExceptionCheck(e))return -2;
    bitmap *b=get(e,object);if(!b)return -1;pthread_mutex_lock(&b->lock);int result=-1;
    if(!b->recycled){*info=b->info;result=0;}pthread_mutex_unlock(&b->lock);return result;
}
int ws_android_bitmap_lock(JNIEnv *e,jobject object,void **pixels){
    if(!e||!pixels)return -1;
    *pixels=NULL;
    if((*e)->ExceptionCheck(e))return -2;
    bitmap *b=get(e,object);if(!b)return -1;
    pixel_pin *pin=malloc(sizeof(*pin));if(!pin)return -1;
    pin->reference=(*e)->NewGlobalRef(e,object);if(!pin->reference){free(pin);return -2;}
    pthread_mutex_lock(&b->lock);int result=-1;
    if(!b->recycled&&b->pixels){++b->locks;pin->next=b->pins;b->pins=pin;*pixels=b->pixels;result=0;}
    pthread_mutex_unlock(&b->lock);if(result){(*e)->DeleteGlobalRef(e,pin->reference);free(pin);}return result;
}
int ws_android_bitmap_unlock(JNIEnv *e,jobject object){
    if(!e)return -1;
    /* Allow unlocking while a Java exception is pending, to release the pin. */
    bitmap *b=get(e,object);if(!b)return -1;pthread_mutex_lock(&b->lock);int result=-1;pixel_pin *pin=NULL;
    if(b->locks){--b->locks;pin=b->pins;b->pins=pin->next;if(b->recycled&&!b->locks)release_pixels(b);result=0;}
    pthread_mutex_unlock(&b->lock);if(pin){(*e)->DeleteGlobalRef(e,pin->reference);free(pin);}return result;
}
static jvalue from_bytes(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)ctx;(void)self;(void)n;jvalue out={0};int w=a[3].i,h=a[4].i;
    if(!w||!h)return out; /* Matches ImageUtils.BitmapFromBytes in this APK. */
    if(w<0||h<0||!a[0].l){exception(e,"java/lang/IllegalArgumentException","Invalid bitmap source");return out;}
    uint64_t bytes=(uint64_t)(unsigned)w*(unsigned)h*4;
    if(bytes>(uint32_t)(*e)->GetArrayLength(e,a[0].l)){exception(e,"java/lang/IllegalArgumentException","Pixel array is too small");return out;}
    out.l=create(e,w,h);if(out.l){bitmap *b=get(e,out.l);(*e)->GetByteArrayRegion(e,a[0].l,0,(jsize)bytes,b->pixels);}
    return out;
}
static jvalue draw_bitmap(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){(void)ctx;(void)self;(void)n;jvalue v={.l=create(e,a[0].i,a[1].i)};return v;}
static jvalue recycle(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)ctx;(void)a;(void)n;bitmap *b=get(e,self);if(b){pthread_mutex_lock(&b->lock);b->recycled=1;if(!b->locks)release_pixels(b);pthread_mutex_unlock(&b->lock);}jvalue v={0};return v;
}
static jvalue recycled(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){(void)ctx;(void)a;(void)n;bitmap *b=get(e,self);jvalue v={0};if(b){pthread_mutex_lock(&b->lock);v.z=b->recycled;pthread_mutex_unlock(&b->lock);}return v;}
static jvalue dimension(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)a;(void)n;bitmap *b=get(e,self);jvalue v={0};if(b){switch((intptr_t)ctx){case 0:v.i=(jint)b->info.width;break;case 1:v.i=(jint)b->info.height;break;case 2:v.i=(jint)b->info.stride;break;case 3:v.i=(jint)b->bytes;break;}}return v;
}
int ws_bitmap_services(ws_java *j){
    if(!ws_jni_vm_has_class(j->vm,"android/graphics/Bitmap")&&ws_jni_vm_define_class(j->vm,"android/graphics/Bitmap"))return -1;
#define M(c,n,s,t,f,p) if(ws_jni_vm_define_method(j->vm,c,n,s,t,f,p))return -1
    M("com/apportable/utils/ImageUtils","BitmapFromBytes","([BIIII)Landroid/graphics/Bitmap;",1,from_bytes,NULL);
    M("com/apportable/utils/DrawBitmap","createDrawBitmap","(II)Landroid/graphics/Bitmap;",1,draw_bitmap,NULL);
    M("android/graphics/Bitmap","recycle","()V",0,recycle,NULL);
    M("android/graphics/Bitmap","isRecycled","()Z",0,recycled,NULL);
    M("android/graphics/Bitmap","getWidth","()I",0,dimension,(void *)0);
    M("android/graphics/Bitmap","getHeight","()I",0,dimension,(void *)1);
    M("android/graphics/Bitmap","getRowBytes","()I",0,dimension,(void *)2);
    M("android/graphics/Bitmap","getByteCount","()I",0,dimension,(void *)3);
#undef M
    return 0;
}
uintptr_t ws_bitmap_symbol(const char *name){
    if(!strcmp(name,"AndroidBitmap_getInfo"))return (uintptr_t)ws_android_bitmap_get_info;
    if(!strcmp(name,"AndroidBitmap_lockPixels"))return (uintptr_t)ws_android_bitmap_lock;
    if(!strcmp(name,"AndroidBitmap_unlockPixels"))return (uintptr_t)ws_android_bitmap_unlock;
    return 0;
}
size_t ws_bitmap_bytes(void){return atomic_load(&total);}
