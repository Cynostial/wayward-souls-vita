#define _POSIX_C_SOURCE 200809L
#include "ws_preferences.h"
#include "ws_paths.h"
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#ifdef _WIN32
#include <io.h>
#endif

#define LIMIT (256u*1024u)
#define MAX_ENTRIES 256
#define MAX_STORES 16
enum {DELETED=0,STRING=1,INT=2,LONG=3,FLOAT=4,BOOLEAN=5};
typedef struct item {struct item *next;char *key,*text;uint64_t bits;unsigned type;} item;
typedef struct pref_store {struct pref_store *next;pthread_mutex_t lock;char *name;char path[WS_PATH_MAX];uint64_t generation;item *items;jobject object;} pref_store;
typedef struct {pthread_mutex_t lock;pref_store *stores;unsigned count;} pref_runtime;
typedef struct {pref_store *store;item *changes;int clear;} editor;
typedef struct {item *items,*current;int mode;} collection;
static void throw_error(JNIEnv *e,const char *type,const char *message){jclass c=(*e)->FindClass(e,type);if(c){(*e)->ThrowNew(e,c,message);(*e)->DeleteLocalRef(e,c);}}
static char *copy(const char *s){size_t n=strlen(s)+1;char *out=malloc(n);if(out)memcpy(out,s,n);return out;}
static void items_free(item *head){while(head){item *next=head->next;free(head->key);free(head->text);free(head);head=next;}}
static item *find(item *head,const char *key){for(;head;head=head->next)if(!strcmp(head->key,key))return head;return NULL;}
static size_t value_bytes(const item *v){return v->type==STRING?strlen(v->text):v->type==LONG?8:v->type==DELETED?0:4;}
static size_t map_bytes(const item *head,unsigned *count){size_t bytes=4;*count=0;for(;head;head=head->next){bytes+=12+strlen(head->key)+value_bytes(head);++*count;}return bytes;}
static int set(item **head,const char *key,unsigned type,uint64_t bits,const char *text){
    if(!key||strlen(key)>1024||type>BOOLEAN||(type==STRING&&(!text||strlen(text)>65536)))return -1;
    item proposed={NULL,(char *)key,(char *)text,bits,type};unsigned count;size_t bytes=map_bytes(*head,&count);
    item *old=find(*head,key);if(old)bytes-=12+strlen(old->key)+value_bytes(old);else if(count>=MAX_ENTRIES)return -1;
    if(bytes+12+strlen(key)+value_bytes(&proposed)>LIMIT)return -1;
    char *saved=type==STRING?copy(text):NULL;if(type==STRING&&!saved)return -1;
    if(!old){old=calloc(1,sizeof(*old));if(!old){free(saved);return -1;}old->key=copy(key);if(!old->key){free(saved);free(old);return -1;}old->next=*head;*head=old;}
    free(old->text);old->text=saved;old->type=type;old->bits=bits;return 0;
}
static void erase(item **head,const char *key){while(*head&&strcmp((*head)->key,key))head=&(*head)->next;if(*head){item *gone=*head;*head=gone->next;gone->next=NULL;items_free(gone);}}
static int clone(item *source,item **out){*out=NULL;for(;source;source=source->next)if(set(out,source->key,source->type,source->bits,source->text)){items_free(*out);*out=NULL;return -1;}return 0;}
static uint32_t read32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void write32(uint8_t *p,uint32_t x){for(int i=0;i<4;++i)p[i]=(uint8_t)(x>>(i*8));}
static uint32_t crc32(const uint8_t *p,size_t size){uint32_t c=~0u;while(size--){c^=*p++;for(int i=0;i<8;++i)c=c>>1^(0xedb88320u&-(c&1u));}return ~c;}
static int read_slot(const char *prefix,int slot,uint64_t *generation,item **result){
    char path[WS_PATH_MAX];if(snprintf(path,sizeof(path),"%s.%d",prefix,slot)>=(int)sizeof(path))return -1;
    FILE *f=fopen(path,"rb");if(!f)return -1;uint8_t header[24];int ok=-1;uint8_t *data=NULL;item *head=NULL;
    if(fread(header,1,sizeof(header),f)!=sizeof(header)||memcmp(header,"WSP1",4))goto done;
    uint32_t size=read32(header+4);if(size<4||size>LIMIT)goto done;
    data=malloc(size);if(!data)goto done;
    if(fread(data,1,size,f)!=size||fgetc(f)!=EOF||crc32(data,size)!=read32(header+8)||crc32(header,20)!=read32(header+20))goto done;
    uint32_t count=read32(data);size_t at=4;if(count>MAX_ENTRIES)goto done;
    for(uint32_t i=0;i<count;++i){
        if(at+12>size)goto done;
        uint32_t type=read32(data+at),key_size=read32(data+at+4),value_size=read32(data+at+8);at+=12;
        if(type<STRING||type>BOOLEAN||key_size>1024||value_size>65536||key_size+value_size>size-at)goto done;
        if(type!=STRING&&value_size!=(type==LONG?8u:4u))goto done;
        char *key=malloc(key_size+1),*text=NULL;if(!key)goto done;
        memcpy(key,data+at,key_size);key[key_size]=0;at+=key_size;
        if(strlen(key)!=key_size||find(head,key)){free(key);goto done;}
        uint64_t bits=0;
        if(type==STRING){text=malloc(value_size+1);if(!text){free(key);goto done;}memcpy(text,data+at,value_size);text[value_size]=0;
            if(strlen(text)!=value_size){free(key);free(text);goto done;}}
        else {bits=read32(data+at);if(type==LONG)bits|=(uint64_t)read32(data+at+4)<<32;}
        int error=set(&head,key,type,bits,text);free(key);free(text);if(error)goto done;at+=value_size;
    }
    if(at!=size)goto done;
    *generation=read32(header+12)|(uint64_t)read32(header+16)<<32;*result=head;head=NULL;ok=0;
done:items_free(head);free(data);fclose(f);return ok;
}
static int persist(pref_store *store,item *head){
    unsigned count;size_t size=map_bytes(head,&count);if(size>LIMIT||store->generation==UINT64_MAX)return -1;
    uint8_t *data=malloc(size);if(!data)return -1;write32(data,count);size_t at=4;
    for(item *p=head;p;p=p->next){size_t key=strlen(p->key),value=value_bytes(p);write32(data+at,p->type);write32(data+at+4,(uint32_t)key);write32(data+at+8,(uint32_t)value);at+=12;
        memcpy(data+at,p->key,key);at+=key;
        if(p->type==STRING)memcpy(data+at,p->text,value);else {write32(data+at,(uint32_t)p->bits);if(p->type==LONG)write32(data+at+4,(uint32_t)(p->bits>>32));}at+=value;
    }
    uint64_t next=store->generation+1;uint8_t header[24]={0};memcpy(header,"WSP1",4);write32(header+4,(uint32_t)size);write32(header+8,crc32(data,size));
    write32(header+12,(uint32_t)next);write32(header+16,(uint32_t)(next>>32));write32(header+20,crc32(header,20));
    char path[WS_PATH_MAX];int n=snprintf(path,sizeof(path),"%s.%u",store->path,(unsigned)(next&1));FILE *f=n>0&&n<(int)sizeof(path)?fopen(path,"wb"):NULL;int ok=-1;
    if(f){
        if(fwrite(header,1,sizeof(header),f)==sizeof(header)&&fwrite(data,1,size,f)==size&&!fflush(f)){
#ifdef _WIN32
            ok=_commit(fileno(f));
#else
            ok=fsync(fileno(f));
#endif
        }
        if(fclose(f))ok=-1;
    }
    free(data);if(!ok)store->generation=next;return ok;
}
static int make_dir(const char *guest){char path[WS_PATH_MAX];if(ws_path_resolve(guest,path,sizeof(path)))return -1;
#ifdef _WIN32
    int r=mkdir(path);
#else
    int r=mkdir(path,0700);
#endif
    return r&&errno!=EEXIST?-1:0;
}
static void editor_free(void *p){editor *ed=p;items_free(ed->changes);free(ed);}
static void collection_free(void *p){collection *c=p;items_free(c->items);free(c);}
static jobject collect(JNIEnv *e,item *items,const char *class_name,int mode){
    collection *c=calloc(1,sizeof(*c));if(!c||clone(items,c?&c->items:NULL)){free(c);throw_error(e,"java/lang/OutOfMemoryError","Preference collection");return NULL;}
    c->current=c->items;c->mode=mode;jobject result=ws_jni_vm_new_owned_instance(e,class_name,c,collection_free);if(!result)collection_free(c);return result;
}
static jvalue get_preferences(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)self;(void)n;ws_java *java=ctx;pref_runtime *runtime=java->preferences;jvalue result={0};
    if(a[1].i!=0){throw_error(e,"java/lang/UnsupportedOperationException","Only private preferences are supported");return result;}
    if(!a[0].l){throw_error(e,"java/lang/NullPointerException","Preference name is null");return result;}
    const char *name=(*e)->GetStringUTFChars(e,a[0].l,NULL);if(!name)return result;
    pthread_mutex_lock(&runtime->lock);pref_store *s;
    for(s=runtime->stores;s;s=s->next)if(!strcmp(s->name,name))break;
    if(!s){
        size_t length=strlen(name);
        if(!length||length>100||runtime->count>=MAX_STORES){throw_error(e,"java/lang/IllegalArgumentException","Preference name/store limit");goto done;}
        char guest[256]="/wayward/Library/Preferences/";size_t at=strlen(guest);static const char hex[]="0123456789abcdef";
        for(size_t i=0;i<length;++i){unsigned c=(unsigned char)name[i];guest[at++]=hex[c>>4];guest[at++]=hex[c&15];}guest[at]=0;
        s=calloc(1,sizeof(*s));if(!s){throw_error(e,"java/lang/OutOfMemoryError","Preferences");goto done;}
        s->name=copy(name);
        if(!s->name||ws_path_resolve(guest,s->path,sizeof(s->path))||pthread_mutex_init(&s->lock,NULL)){free(s->name);free(s);s=NULL;throw_error(e,"java/lang/IllegalStateException","Preference storage unavailable");goto done;}
        item *slots[2]={NULL,NULL};uint64_t generations[2]={0,0};
        int first=read_slot(s->path,0,&generations[0],&slots[0]),second=read_slot(s->path,1,&generations[1],&slots[1]);
        int chosen=first?1:second?0:generations[1]>generations[0]?1:0;
        s->items=slots[chosen];s->generation=generations[chosen];items_free(slots[1-chosen]);
        jobject local=ws_jni_vm_new_instance(e,"android/content/SharedPreferences",s);
        s->object=local?(*e)->NewGlobalRef(e,local):NULL;(*e)->DeleteLocalRef(e,local);
        if(!s->object){items_free(s->items);pthread_mutex_destroy(&s->lock);free(s->name);free(s);s=NULL;goto done;}
        s->next=runtime->stores;runtime->stores=s;++runtime->count;
    }
    result.l=(*e)->NewLocalRef(e,s->object);
done:pthread_mutex_unlock(&runtime->lock);(*e)->ReleaseStringUTFChars(e,a[0].l,name);return result;
}
static jvalue get_value(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)n;unsigned kind=(unsigned)(uintptr_t)ctx;pref_store *s=ws_jni_vm_instance_data(e,self);jvalue result=kind==99?(jvalue){0}:a[1];
    if(!s||!a[0].l){throw_error(e,"java/lang/NullPointerException","Preference key is null");return (jvalue){0};}
    const char *key=(*e)->GetStringUTFChars(e,a[0].l,NULL);if(!key)return (jvalue){0};
    pthread_mutex_lock(&s->lock);item *value=find(s->items,key);
    if(kind==99)result.z=value!=NULL;
    else if(value){
        if(value->type!=kind)throw_error(e,"java/lang/ClassCastException","Preference value has a different type");
        else if(kind==STRING)result.l=(*e)->NewStringUTF(e,value->text);
        else if(kind==LONG)result.j=(jlong)value->bits;
        else if(kind==FLOAT){uint32_t bits=(uint32_t)value->bits;memcpy(&result.f,&bits,4);}
        else result.i=(jint)value->bits;
    }else if(kind==STRING)result.l=(*e)->NewLocalRef(e,a[1].l);
    pthread_mutex_unlock(&s->lock);(*e)->ReleaseStringUTFChars(e,a[0].l,key);return result;
}
static jvalue edit(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)ctx;(void)a;(void)n;editor *ed=calloc(1,sizeof(*ed));if(!ed){throw_error(e,"java/lang/OutOfMemoryError","Preference editor");return (jvalue){0};}
    ed->store=ws_jni_vm_instance_data(e,self);jobject result=ws_jni_vm_new_owned_instance(e,"android/content/SharedPreferences$Editor",ed,editor_free);if(!result)editor_free(ed);return (jvalue){.l=result};
}
static jvalue change(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    unsigned kind=(unsigned)(uintptr_t)ctx;editor *ed=ws_jni_vm_instance_data(e,self);if(!ed)return (jvalue){0};
    const char *key=NULL,*text=NULL;uint64_t bits=0;
    if(n){if(!a[0].l){throw_error(e,"java/lang/NullPointerException","Preference key is null");return (jvalue){0};}key=(*e)->GetStringUTFChars(e,a[0].l,NULL);if(!key)return (jvalue){0};}
    if(kind==STRING){if(a[1].l)text=(*e)->GetStringUTFChars(e,a[1].l,NULL);else kind=DELETED;}
    if(kind==LONG)bits=(uint64_t)a[1].j;else if(kind==FLOAT){uint32_t raw;memcpy(&raw,&a[1].f,4);bits=raw;}else if(kind==INT)bits=(uint32_t)a[1].i;else if(kind==BOOLEAN)bits=!!a[1].z;
    pthread_mutex_lock(&ed->store->lock);
    if(kind==99)ed->clear=1;
    else if((kind==STRING&&!text)||set(&ed->changes,key,kind,bits,text))throw_error(e,"java/lang/OutOfMemoryError","Preference edit exceeds storage budget");
    pthread_mutex_unlock(&ed->store->lock);
    if(text)(*e)->ReleaseStringUTFChars(e,a[1].l,text);
    if(key)(*e)->ReleaseStringUTFChars(e,a[0].l,key);
    return (jvalue){.l=(*e)->NewLocalRef(e,self)};
}
static jvalue commit(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)ctx;(void)a;(void)n;editor *ed=ws_jni_vm_instance_data(e,self);jvalue result={0};if(!ed)return result;pref_store *s=ed->store;
    pthread_mutex_lock(&s->lock);item *merged=NULL;int error=ed->clear?0:clone(s->items,&merged);
    for(item *p=ed->changes;!error&&p;p=p->next){if(!p->type)erase(&merged,p->key);else error=set(&merged,p->key,p->type,p->bits,p->text);}
    if(error){items_free(merged);throw_error(e,"java/lang/OutOfMemoryError","Preference transaction exceeds storage budget");}
    else{
        /* Alternate checksummed generations. An interrupted write cannot
         * destroy the most recent valid slot; no unsafe overwrite rename. */
        result.z=!make_dir("/wayward/Library")&&!make_dir("/wayward/Library/Preferences")&&!persist(s,merged);
        items_free(s->items);s->items=merged;items_free(ed->changes);ed->changes=NULL;ed->clear=0;
        if(!result.z)fputs("Wayward: preferences changed in memory but could not be saved\n",stderr);
    }
    pthread_mutex_unlock(&s->lock);return result;
}
static jvalue get_all(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)ctx;(void)a;(void)n;pref_store *s=ws_jni_vm_instance_data(e,self);pthread_mutex_lock(&s->lock);
    jobject out=collect(e,s->items,"java/util/Map",0);pthread_mutex_unlock(&s->lock);return (jvalue){.l=out};
}
typedef struct {uint64_t bits;unsigned type;} boxed;
static jobject box(JNIEnv *e,const item *p){
    if(!p)return NULL;
    if(p->type==STRING)return (*e)->NewStringUTF(e,p->text);
    boxed *value=malloc(sizeof(*value));if(!value){throw_error(e,"java/lang/OutOfMemoryError","Preference value");return NULL;}
    *value=(boxed){p->bits,p->type};
    const char *name=p->type==INT?"java/lang/Integer":p->type==LONG?"java/lang/Long":p->type==FLOAT?"java/lang/Float":"java/lang/Boolean";
    jobject out=ws_jni_vm_new_owned_instance(e,name,value,free);if(!out)free(value);return out;
}
static jvalue unbox(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)a;(void)n;boxed *v=ws_jni_vm_instance_data(e,self);jvalue out={0};if(!v){throw_error(e,"java/lang/IllegalStateException","Uninitialized boxed preference value");return out;}
    unsigned kind=(unsigned)(uintptr_t)ctx;
    if(v->type==BOOLEAN){out.z=!!v->bits;return out;}
    float f=0;if(v->type==FLOAT){uint32_t bits=(uint32_t)v->bits;memcpy(&f,&bits,4);}
    double number=v->type==FLOAT?(double)f:v->type==LONG?(double)(int64_t)v->bits:(double)(int32_t)v->bits;
    if(kind==FLOAT)out.f=(float)number;
    else if(kind==9)out.d=number;
    else if(kind==LONG){out.j=v->type!=FLOAT?(v->type==LONG?(int64_t)v->bits:(int32_t)v->bits):number!=number?0:number>=9223372036854775808.0?INT64_MAX:number<=-9223372036854775808.0?INT64_MIN:(int64_t)number;}
    else out.i=v->type!=FLOAT?(int32_t)v->bits:number!=number?0:number>=2147483647.0?INT32_MAX:number<=-2147483648.0?INT32_MIN:(int32_t)number;
    return out;
}
static jvalue map_lookup(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)n;collection *c=ws_jni_vm_instance_data(e,self);jvalue out={0};if(!c||!a[0].l)return out;
    jclass string=(*e)->FindClass(e,"java/lang/String");int is_string=(*e)->IsInstanceOf(e,a[0].l,string);(*e)->DeleteLocalRef(e,string);if(!is_string)return out;
    const char *key=(*e)->GetStringUTFChars(e,a[0].l,NULL);if(!key)return out;item *p=find(c->items,key);
    if(ctx)out.z=p!=NULL;else out.l=box(e,p);(*e)->ReleaseStringUTFChars(e,a[0].l,key);return out;
}
static jvalue collection_size(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)a;(void)n;collection *c=ws_jni_vm_instance_data(e,self);unsigned count=0;if(c)map_bytes(c->items,&count);
    return ctx?(jvalue){.z=!count}:(jvalue){.i=(jint)count};
}
static jvalue collection_view(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)a;(void)n;collection *c=ws_jni_vm_instance_data(e,self);int mode=(int)(intptr_t)ctx;
    return (jvalue){.l=c?collect(e,c->items,mode==2?"java/util/Collection":"java/util/Set",mode):NULL};
}
static jvalue iterator(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)ctx;(void)a;(void)n;collection *c=ws_jni_vm_instance_data(e,self);
    return (jvalue){.l=c?collect(e,c->items,"java/util/Iterator",c->mode):NULL};
}
static jvalue has_next(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)ctx;(void)a;(void)n;collection *c=ws_jni_vm_instance_data(e,self);return (jvalue){.z=c&&c->current};
}
static jvalue next(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)ctx;(void)a;(void)n;collection *c=ws_jni_vm_instance_data(e,self);jvalue out={0};
    if(!c||!c->current){throw_error(e,"java/util/NoSuchElementException","Preference iterator exhausted");return out;}
    item *p=c->current;
    if(c->mode==1)out.l=(*e)->NewStringUTF(e,p->key);
    else if(c->mode==2)out.l=box(e,p);
    else{
        collection *entry=calloc(1,sizeof(*entry));
        if(!entry||set(&entry->items,p->key,p->type,p->bits,p->text)){if(entry)collection_free(entry);throw_error(e,"java/lang/OutOfMemoryError","Preference entry");return out;}
        out.l=ws_jni_vm_new_owned_instance(e,"java/util/Map$Entry",entry,collection_free);if(!out.l)collection_free(entry);
    }
    if(out.l)c->current=p->next;
    return out;
}
static jvalue entry_part(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)a;(void)n;collection *entry=ws_jni_vm_instance_data(e,self);
    if(!entry||!entry->items)return (jvalue){0};
    return (jvalue){.l=ctx?box(e,entry->items):(*e)->NewStringUTF(e,entry->items->key)};
}
int ws_preferences_init(ws_java *j){
    pref_runtime *r=calloc(1,sizeof(*r));if(!r)return -1;if(pthread_mutex_init(&r->lock,NULL)){free(r);return -1;}j->preferences=r;
    const char *owners[]={"android/content/Context","android/content/ContextWrapper","android/view/ContextThemeWrapper","android/app/Activity","android/app/Application"};
    for(size_t i=0;i<sizeof(owners)/sizeof(*owners);++i)if(ws_jni_vm_define_method(j->vm,owners[i],"getSharedPreferences","(Ljava/lang/String;I)Landroid/content/SharedPreferences;",0,get_preferences,j))return -1;
#define M(c,n,s,f,p) if(ws_jni_vm_define_method(j->vm,c,n,s,0,f,(void *)(uintptr_t)p)){fprintf(stderr,"Preference method declaration failed: %s.%s %s\n",c,n,s);return -1;}
#define P(n,s,f,p) M("android/content/SharedPreferences",n,s,f,p)
#define E(n,s,f,p) M("android/content/SharedPreferences$Editor",n,s,f,p)
    P("getString","(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",get_value,STRING);
    P("getInt","(Ljava/lang/String;I)I",get_value,INT);P("getLong","(Ljava/lang/String;J)J",get_value,LONG);
    P("getFloat","(Ljava/lang/String;F)F",get_value,FLOAT);P("getBoolean","(Ljava/lang/String;Z)Z",get_value,BOOLEAN);
    P("contains","(Ljava/lang/String;)Z",get_value,99);P("edit","()Landroid/content/SharedPreferences$Editor;",edit,0);P("getAll","()Ljava/util/Map;",get_all,0);
    E("putString","(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;",change,STRING);
    E("putInt","(Ljava/lang/String;I)Landroid/content/SharedPreferences$Editor;",change,INT);E("putLong","(Ljava/lang/String;J)Landroid/content/SharedPreferences$Editor;",change,LONG);
    E("putFloat","(Ljava/lang/String;F)Landroid/content/SharedPreferences$Editor;",change,FLOAT);E("putBoolean","(Ljava/lang/String;Z)Landroid/content/SharedPreferences$Editor;",change,BOOLEAN);
    E("remove","(Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;",change,DELETED);E("clear","()Landroid/content/SharedPreferences$Editor;",change,99);
    E("commit","()Z",commit,0);E("apply","()V",commit,0);
    M("java/util/Map","get","(Ljava/lang/Object;)Ljava/lang/Object;",map_lookup,0);
    M("java/util/Map","containsKey","(Ljava/lang/Object;)Z",map_lookup,1);
    M("java/util/Map","keySet","()Ljava/util/Set;",collection_view,1);
    M("java/util/Map","entrySet","()Ljava/util/Set;",collection_view,0);
    M("java/util/Map","values","()Ljava/util/Collection;",collection_view,2);
    const char *collections[]={"java/util/Map","java/util/Set","java/util/Collection"};
    for(size_t i=0;i<3;++i){M(collections[i],"size","()I",collection_size,0);M(collections[i],"isEmpty","()Z",collection_size,1);}
    M("java/util/Set","iterator","()Ljava/util/Iterator;",iterator,0);M("java/util/Collection","iterator","()Ljava/util/Iterator;",iterator,0);
    M("java/util/Iterator","hasNext","()Z",has_next,0);M("java/util/Iterator","next","()Ljava/lang/Object;",next,0);
    M("java/util/Map$Entry","getKey","()Ljava/lang/Object;",entry_part,0);M("java/util/Map$Entry","getValue","()Ljava/lang/Object;",entry_part,1);
    const char *boxed_classes[]={"java/lang/Integer","java/lang/Long","java/lang/Float"};
    for(size_t i=0;i<3;++i){M(boxed_classes[i],"intValue","()I",unbox,INT);M(boxed_classes[i],"longValue","()J",unbox,LONG);M(boxed_classes[i],"floatValue","()F",unbox,FLOAT);M(boxed_classes[i],"doubleValue","()D",unbox,9);}
    M("java/lang/Boolean","booleanValue","()Z",unbox,BOOLEAN);
#undef E
#undef P
#undef M
    return 0;
}
void ws_preferences_destroy(ws_java *j){
    if(!j||!j->preferences)return;
    pref_runtime *r=j->preferences;JNIEnv *e=ws_java_env(j);
    while(r->stores){pref_store *next=r->stores->next;(*e)->DeleteGlobalRef(e,r->stores->object);items_free(r->stores->items);pthread_mutex_destroy(&r->stores->lock);free(r->stores->name);free(r->stores);r->stores=next;}
    pthread_mutex_destroy(&r->lock);free(r);j->preferences=NULL;
}
