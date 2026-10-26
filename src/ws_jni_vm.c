#include "ws_jni_vm.h"
#include "ws_mutf8.h"
#include "ws_jni_descriptor.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <time.h>

typedef struct monitor {
    pthread_cond_t enter,notify;
    void *owner;
    unsigned depth;
} monitor;

typedef struct field field;
typedef struct field_value {
    struct field_value *next;
    field *definition;
    jvalue value;
} field_value;
typedef struct object {
    struct object *next;
    unsigned refs, globals, pins;
    enum { CLASS, DIRECT_BUFFER, EXCEPTION, STRING, INSTANCE, ARRAY } kind;
    struct object *clazz, *parent;
    struct object *component;
    char array_kind;
    field_value *fields;
    char *name;
    void *data;
    void (*destroy_data)(void *);
    monitor *monitor;
    jlong capacity;
} object;
typedef struct reference_handle {
    struct reference_handle *next;
    object *target;
    int weak;
} reference_handle;
typedef struct array_copy {
    struct array_copy *next;
    object *array;
    void *data;
    int critical;
    JNIEnv *owner;
} array_copy;

typedef struct method {
    struct method *next;
    object *clazz;
    char *name, *signature;
    ws_jni_descriptor descriptor;
    int is_static;
    ws_jni_service service;
    void *userdata;
} method;
struct field {
    field *next;
    object *clazz;
    char *name, *signature;
    char kind;
    int is_static;
    jvalue value;
};

typedef struct environment {
    JNIEnv table; /* JNI ABI: the first word points at the function table. */
    ws_jni_vm *vm;
    object **locals;
    size_t count, capacity;
    size_t frames[64];
    unsigned depth;
    object *pending;
} environment;

struct ws_jni_vm {
    JavaVM table;
    pthread_mutex_t mutex;
    pthread_key_t tls;
    unsigned attached;
    object *objects;
    reference_handle *handles;
    object *oom;
    method *methods;
    field *fields;
    ws_jni_registry *registry;
    array_copy *array_copies;
    ws_jni_export_resolver exports;
    void *export_context;
};

static const struct JNINativeInterface native_table;
static const struct JNIInvokeInterface invocation_table;
static environment *state(JNIEnv *env) { return (environment *)env; }
static ws_jni_vm *machine(JavaVM *vm) { return (ws_jni_vm *)vm; }

static object *find_object(ws_jni_vm *vm, jobject handle) {
    for (object *o = vm->objects; o; o = o->next) if (o == handle) return o;
    for(reference_handle *r=vm->handles;r;r=r->next)if((jobject)r==handle)return r->target;
    return NULL;
}
static object *find_class(ws_jni_vm *vm, const char *name) {
    for (object *o = vm->objects; o; o = o->next)
        if (o->kind == CLASS && !strcmp(o->name, name)) return o;
    return NULL;
}
static object *object_class(ws_jni_vm *vm, object *o) {
    if (!o) return NULL;
    if (o->kind == INSTANCE || o->kind == ARRAY) return o->clazz;
    return find_class(vm, o->kind == CLASS ? "java/lang/Class" :
        o->kind == STRING ? "java/lang/String" : o->kind == DIRECT_BUFFER ?
        "java/nio/ByteBuffer" : o->name);
}
static int subclass(object *sub, object *super) {
    if (sub && super && sub->array_kind) {
        if (super->array_kind) {
            if (sub->array_kind == 'L' && super->array_kind == 'L')
                return subclass(sub->component, super->component);
            return sub == super;
        }
        if (!strcmp(super->name, "java/lang/Cloneable") || !strcmp(super->name, "java/io/Serializable")) return 1;
    }
    for (; sub; sub = sub->parent) if (sub == super) return 1;
    return 0;
}
static void monitor_destroy(monitor *m) {
    if(!m)return;
    pthread_cond_destroy(&m->enter);pthread_cond_destroy(&m->notify);free(m);
}
static void release(ws_jni_vm *vm, object *o) {
    if (--o->refs) return;
    for(reference_handle *r=vm->handles;r;r=r->next)if(r->weak&&r->target==o)r->target=NULL;
    object **p = &vm->objects;
    while (*p != o) p = &(*p)->next;
    *p = o->next;
    field_value *v = o->fields;
    while (v) {
        field_value *next = v->next;
        if (v->definition->kind == 'L' && v->value.l) release(vm, v->value.l);
        free(v); v = next;
    }
    free(o->name);
    if (o->kind == ARRAY && o->clazz->array_kind == 'L')
        for (jlong i = 0; i < o->capacity; ++i) {
            object *element = ((object **)o->data)[i];
            if (element) release(vm, element);
        }
    if (o->kind == EXCEPTION || o->kind == STRING || o->kind == ARRAY) free(o->data);
    if (o->kind == INSTANCE && o->destroy_data) o->destroy_data(o->data);
    monitor_destroy(o->monitor);
    free(o); /* Direct byte buffer backing storage is owned by the caller. */
}
static char *copy_string(const char *text) {
    size_t size = strlen(text) + 1;
    char *result = malloc(size);
    if (result) memcpy(result, text, size);
    return result;
}
/* The VM mutex must be held. The cached OOM object avoids recursively
 * allocating while reporting an allocation failure. */
static void failure(environment *e, const char *class_name, const char *message) {
    object *o = calloc(1, sizeof(*o));
    if (o) {
        o->name = copy_string(class_name);
        o->data = message ? copy_string(message) : NULL;
        if (!o->name || (message&&!o->data)) {
            free(o->name); free(o->data); free(o); o = NULL;
        }
    }
    if (o) {
        o->kind = EXCEPTION;
        o->refs = 1;
        o->next = e->vm->objects;
        e->vm->objects = o;
    } else { o = e->vm->oom; ++o->refs; }
    if (e->pending) release(e->vm, e->pending);
    e->pending = o;
}
static int ensure_locals(environment *e, size_t extra) {
    if (extra > SIZE_MAX - e->count) return -1;
    size_t wanted = e->count + extra;
    if (wanted <= e->capacity) return 0;
    size_t next = wanted < 64 ? 64 : wanted;
    if (next > SIZE_MAX / sizeof(object *)) return -1;
    object **p = realloc(e->locals, next * sizeof(*p));
    if (!p) return -1;
    e->locals = p;
    e->capacity = next;
    return 0;
}
static jobject local_ref(environment *e, object *o) {
    if (!o) return NULL;
    if (ensure_locals(e, 1)) {
        failure(e, "java/lang/OutOfMemoryError", "local reference"); return NULL;
    }
    e->locals[e->count++] = o;
    ++o->refs;
    return o;
}
#include "ws_jni_monitors.inc"
static void release_environment(void *ptr) {
    environment *e = ptr;
    if (!e) return;
    pthread_mutex_lock(&e->vm->mutex);
    monitor_release_owned(e);
    for (size_t i = 0; i < e->count; ++i)
        if (e->locals[i]) release(e->vm, e->locals[i]);
    if (e->pending) release(e->vm, e->pending);
    --e->vm->attached;
    pthread_mutex_unlock(&e->vm->mutex);
    free(e->locals);
    free(e);
}
static jint attach(JavaVM *handle, JNIEnv **out, void *args) {
    (void)args;
    if (!handle || !out) return JNI_EINVAL;
    ws_jni_vm *vm = machine(handle);
    environment *e = pthread_getspecific(vm->tls);
    if (!e) {
        e = calloc(1, sizeof(*e));
        if (!e) return JNI_ENOMEM;
        e->table = &native_table;
        e->vm = vm;
        if (pthread_setspecific(vm->tls, e)) { free(e); return JNI_ERR; }
        pthread_mutex_lock(&vm->mutex);
        ++vm->attached;
        pthread_mutex_unlock(&vm->mutex);
    }
    *out = &e->table;
    return JNI_OK;
}
static jint detach(JavaVM *handle) {
    if (!handle) return JNI_EINVAL;
    ws_jni_vm *vm = machine(handle);
    environment *e = pthread_getspecific(vm->tls);
    if (e) {
        if (pthread_setspecific(vm->tls, NULL)) return JNI_ERR;
        release_environment(e);
    }
    return JNI_OK;
}
static jint get_environment(JavaVM *handle, void **out, jint version) {
    if (!handle || !out) return JNI_EINVAL;
    *out = NULL;
    if (version != JNI_VERSION_1_6 && version != JNI_VERSION_1_4 && version != JNI_VERSION_1_2)
        return JNI_EVERSION;
    environment *e = pthread_getspecific(machine(handle)->tls);
    if (!e) return JNI_EDETACHED;
    *out = &e->table;
    return JNI_OK;
}
static jint destroy_machine(JavaVM *handle) { return ws_jni_vm_destroy(machine(handle)); }
static const struct JNIInvokeInterface invocation_table = {
    .DestroyJavaVM = destroy_machine, .AttachCurrentThread = attach,
    .AttachCurrentThreadAsDaemon = attach, .DetachCurrentThread = detach,
    .GetEnv = get_environment
};

static jint jni_GetVersion(JNIEnv *env) { (void)env; return JNI_VERSION_1_6; }
#include "ws_jni_arrays.inc"
static jclass jni_FindClass(JNIEnv *env, const char *name) {
    environment *e = state(env);
    pthread_mutex_lock(&e->vm->mutex);
    object *clazz = name ? (name[0] == '[' ? array_class(e, name) : find_class(e->vm, name)) : NULL;
    jobject result = local_ref(e, clazz);
    if (!clazz) failure(e, "java/lang/ClassNotFoundException", name);
    else if (!result) failure(e, "java/lang/OutOfMemoryError", "local reference");
    pthread_mutex_unlock(&e->vm->mutex);
    return result;
}
static jint jni_RegisterNatives(JNIEnv *env, jclass clazz, const JNINativeMethod *methods, jint count) {
    if (count < 0 || (!methods && count)) return JNI_ERR;
    ws_jni_method *batch = count ? calloc((size_t)count, sizeof(*batch)) : NULL;
    if (count && !batch) return JNI_ENOMEM;
    for (jint i = 0; i < count; ++i) {
        batch[i].name = methods[i].name;
        batch[i].signature = methods[i].signature;
        batch[i].address = (uintptr_t)methods[i].fnPtr;
    }
    environment *e = state(env);
    pthread_mutex_lock(&e->vm->mutex);
    object *c = find_object(e->vm, clazz);
    int result = c && c->kind == CLASS ? ws_jni_register(e->vm->registry, c->name, batch, (size_t)count) : -1;
    pthread_mutex_unlock(&e->vm->mutex);
    free(batch);
    return result == WS_JNI_OK ? JNI_OK : JNI_ERR;
}
static jint jni_UnregisterNatives(JNIEnv *env, jclass clazz) {
    environment *e = state(env);
    pthread_mutex_lock(&e->vm->mutex);
    object *c = find_object(e->vm, clazz);
    int valid = c && c->kind == CLASS;
    if (valid) ws_jni_unregister(e->vm->registry, c->name);
    pthread_mutex_unlock(&e->vm->mutex);
    return valid ? JNI_OK : JNI_ERR;
}
static jobject new_reference(JNIEnv *env,jobject handle,int weak) {
    environment *e = state(env);
    pthread_mutex_lock(&e->vm->mutex);
    object *o = find_object(e->vm, handle);
    reference_handle *ref=NULL;
    if(o){
        ref=malloc(sizeof(*ref));
        if(ref){*ref=(reference_handle){e->vm->handles,o,weak};e->vm->handles=ref;
            if(!weak){++o->globals;++o->refs;}}
        else failure(e,"java/lang/OutOfMemoryError","global reference");
    }
    pthread_mutex_unlock(&e->vm->mutex);
    return (jobject)ref;
}
static jobject jni_NewGlobalRef(JNIEnv *env,jobject handle){return new_reference(env,handle,0);}
static jweak jni_NewWeakGlobalRef(JNIEnv *env,jobject handle){return new_reference(env,handle,1);}
static void delete_reference(JNIEnv *env,jobject handle,int weak) {
    environment *e = state(env);
    pthread_mutex_lock(&e->vm->mutex);
    reference_handle **link=&e->vm->handles;
    while(*link&&(jobject)*link!=handle)link=&(*link)->next;
    reference_handle *ref=*link;
    if(ref&&ref->weak==weak){
        *link=ref->next;
        if(!weak){--ref->target->globals;release(e->vm,ref->target);}
        free(ref);
    }
    pthread_mutex_unlock(&e->vm->mutex);
}
static void jni_DeleteGlobalRef(JNIEnv *env,jobject handle){delete_reference(env,handle,0);}
static void jni_DeleteWeakGlobalRef(JNIEnv *env,jweak handle){delete_reference(env,handle,1);}
static jobjectRefType jni_GetObjectRefType(JNIEnv *env,jobject handle){
    environment *e=state(env);jobjectRefType type=JNIInvalidRefType;
    pthread_mutex_lock(&e->vm->mutex);
    if(handle){
        for(reference_handle *r=e->vm->handles;r;r=r->next)if((jobject)r==handle){type=r->weak?JNIWeakGlobalRefType:JNIGlobalRefType;break;}
        if(type==JNIInvalidRefType)for(size_t i=0;i<e->count;++i)if(e->locals[i]==handle){type=JNILocalRefType;break;}
    }
    pthread_mutex_unlock(&e->vm->mutex);return type;
}
static jobject jni_NewLocalRef(JNIEnv *env, jobject handle) {
    environment *e = state(env);
    pthread_mutex_lock(&e->vm->mutex);
    jobject result = local_ref(e, find_object(e->vm, handle));
    pthread_mutex_unlock(&e->vm->mutex);
    return result;
}
static void jni_DeleteLocalRef(JNIEnv *env, jobject handle) {
    environment *e = state(env);
    if (!handle) return;
    pthread_mutex_lock(&e->vm->mutex);
    for (size_t i = e->count; i; --i) {
        if (e->locals[i-1] == handle) {
            release(e->vm, e->locals[i-1]);
            e->locals[i-1] = NULL;
            break;
        }
    }
    while (e->count && !e->locals[e->count-1] &&
           (!e->depth || e->count > e->frames[e->depth-1])) --e->count;
    pthread_mutex_unlock(&e->vm->mutex);
}
static jboolean jni_IsSameObject(JNIEnv *env, jobject a, jobject b) {
    environment *e=state(env);pthread_mutex_lock(&e->vm->mutex);
    jboolean same=find_object(e->vm,a)==find_object(e->vm,b);
    pthread_mutex_unlock(&e->vm->mutex);return same;
}
static jint jni_EnsureLocalCapacity(JNIEnv *env, jint capacity) {
    if (capacity < 0) return JNI_ERR;
    return ensure_locals(state(env), (size_t)capacity) ? JNI_ENOMEM : JNI_OK;
}
static jint jni_PushLocalFrame(JNIEnv *env, jint capacity) {
    environment *e = state(env);
    if (e->depth == 64 || capacity < 0) return JNI_ERR;
    if (ensure_locals(e, (size_t)capacity)) return JNI_ENOMEM;
    e->frames[e->depth++] = e->count;
    return JNI_OK;
}
static jobject jni_PopLocalFrame(JNIEnv *env, jobject result) {
    environment *e = state(env);
    if (!e->depth) return NULL;
    pthread_mutex_lock(&e->vm->mutex);
    object *o = find_object(e->vm, result);
    if (o) ++o->refs;
    size_t mark = e->frames[--e->depth];
    while (e->count > mark) {
        object *drop = e->locals[--e->count];
        if (drop) release(e->vm, drop);
    }
    jobject promoted = local_ref(e, o);
    if (o) release(e->vm, o);
    pthread_mutex_unlock(&e->vm->mutex);
    return promoted;
}
static jint jni_GetJavaVM(JNIEnv *env, JavaVM **out) {
    if (!out) return JNI_ERR;
    *out = &state(env)->vm->table;
    return JNI_OK;
}
static jint jni_Throw(JNIEnv *env, jthrowable exception) {
    environment *e = state(env);
    pthread_mutex_lock(&e->vm->mutex);
    object *o = find_object(e->vm, exception);
    int valid = o && subclass(object_class(e->vm,o),find_class(e->vm,"java/lang/Throwable"));
    if (valid) {
        ++o->refs;
        if (e->pending) release(e->vm, e->pending);
        e->pending = o;
    }
    pthread_mutex_unlock(&e->vm->mutex);
    return valid ? JNI_OK : JNI_ERR;
}
static jint jni_ThrowNew(JNIEnv *env, jclass clazz, const char *message) {
    environment *e = state(env);
    pthread_mutex_lock(&e->vm->mutex);
    object *c = find_object(e->vm, clazz);
    int valid = c && c->kind == CLASS && subclass(c, find_class(e->vm, "java/lang/Throwable"));
    if (valid) failure(e, c->name, message);
    pthread_mutex_unlock(&e->vm->mutex);
    return valid ? JNI_OK : JNI_ERR;
}
static jboolean jni_ExceptionCheck(JNIEnv *env) { return state(env)->pending ? JNI_TRUE : JNI_FALSE; }
static jthrowable jni_ExceptionOccurred(JNIEnv *env) {
    environment *e = state(env);
    pthread_mutex_lock(&e->vm->mutex);
    jobject result = local_ref(e, e->pending);
    pthread_mutex_unlock(&e->vm->mutex);
    return result;
}
static void jni_ExceptionClear(JNIEnv *env) {
    environment *e = state(env);
    pthread_mutex_lock(&e->vm->mutex);
    if (e->pending) release(e->vm, e->pending);
    e->pending = NULL;
    pthread_mutex_unlock(&e->vm->mutex);
}
static void jni_ExceptionDescribe(JNIEnv *env) {
    environment *e = state(env);
    pthread_mutex_lock(&e->vm->mutex);
    object *o=e->pending;
    if(o){
        object *cls=object_class(e->vm,o);const char *message=o->kind==EXCEPTION?o->data:NULL;
        if(o->kind==INSTANCE)for(field_value *v=o->fields;v;v=v->next){
            if(v->definition->kind=='L'&&!strcmp(v->definition->name,"detailMessage")){
                object *s=v->value.l;if(s&&s->kind==STRING)message=s->name;break;
            }
        }
        fprintf(stderr,"%s%s%s\n",cls?cls->name:"Throwable",message?": ":"",message?message:"");
    }
    pthread_mutex_unlock(&e->vm->mutex);
    jni_ExceptionClear(env);
}
static jclass jni_GetObjectClass(JNIEnv *env, jobject handle) {
    environment *e = state(env);
    pthread_mutex_lock(&e->vm->mutex);
    object *o = find_object(e->vm, handle);
    jobject result = local_ref(e, object_class(e->vm, o));
    pthread_mutex_unlock(&e->vm->mutex);
    return result;
}
static jobject jni_NewDirectByteBuffer(JNIEnv *env, void *data, jlong capacity) {
    if (!data || capacity < 0) return NULL;
    environment *e = state(env);
    object *o = calloc(1, sizeof(*o));
    if (!o) return NULL;
    o->kind = DIRECT_BUFFER;
    o->data = data;
    o->capacity = capacity;
    pthread_mutex_lock(&e->vm->mutex);
    o->next = e->vm->objects;
    e->vm->objects = o;
    jobject result = local_ref(e, o);
    if (!result) { ++o->refs; release(e->vm, o); }
    pthread_mutex_unlock(&e->vm->mutex);
    return result;
}
static void *jni_GetDirectBufferAddress(JNIEnv *env, jobject handle) {
    environment *e = state(env);
    pthread_mutex_lock(&e->vm->mutex);
    object *o = find_object(e->vm, handle);
    void *result = o && o->kind == DIRECT_BUFFER ? o->data : NULL;
    pthread_mutex_unlock(&e->vm->mutex);
    return result;
}
static jlong jni_GetDirectBufferCapacity(JNIEnv *env, jobject handle) {
    environment *e = state(env);
    pthread_mutex_lock(&e->vm->mutex);
    object *o = find_object(e->vm, handle);
    jlong result = o && o->kind == DIRECT_BUFFER ? o->capacity : -1;
    pthread_mutex_unlock(&e->vm->mutex);
    return result;
}
static jstring make_string(environment *e, jchar *chars, size_t units) {
    char *encoded = NULL;
    size_t bytes;
    int status = ws_mutf8_encode(chars, units, &encoded, &bytes);
    object *o = status ? NULL : calloc(1, sizeof(*o));
    pthread_mutex_lock(&e->vm->mutex);
    if (!o) {
        free(chars); free(encoded);
        failure(e, status == WS_TEXT_INVALID ? "java/lang/IllegalArgumentException" :
                "java/lang/OutOfMemoryError", "string creation");
        pthread_mutex_unlock(&e->vm->mutex);
        return NULL;
    }
    o->kind = STRING;
    o->name = encoded;
    o->data = chars;
    o->capacity = (jlong)units;
    o->next = e->vm->objects;
    e->vm->objects = o;
    jobject result = local_ref(e, o);
    if (!result) {
        ++o->refs; release(e->vm, o);
        failure(e, "java/lang/OutOfMemoryError", "string reference");
    }
    pthread_mutex_unlock(&e->vm->mutex);
    return result;
}
static jstring jni_NewString(JNIEnv *env, const jchar *chars, jsize length) {
    environment *e = state(env);
    if (length < 0 || (!chars && length) || (size_t)length > (SIZE_MAX / sizeof(jchar)) - 1) {
        pthread_mutex_lock(&e->vm->mutex);
        failure(e, "java/lang/IllegalArgumentException", "UTF-16 string range");
        pthread_mutex_unlock(&e->vm->mutex);
        return NULL;
    }
    jchar *copy = malloc(((size_t)length + 1) * sizeof(*copy));
    if (!copy) {
        pthread_mutex_lock(&e->vm->mutex); failure(e, "java/lang/OutOfMemoryError", "UTF-16 string");
        pthread_mutex_unlock(&e->vm->mutex); return NULL;
    }
    if (length) memcpy(copy, chars, (size_t)length * sizeof(*copy));
    copy[length] = 0;
    return make_string(e, copy, (size_t)length);
}
static jstring jni_NewStringUTF(JNIEnv *env, const char *text) {
    environment *e = state(env);
    jchar *chars = NULL;
    size_t units;
    int status = ws_mutf8_decode(text, &chars, &units);
    if (status) {
        pthread_mutex_lock(&e->vm->mutex);
        failure(e, status == WS_TEXT_INVALID ? "java/lang/IllegalArgumentException" :
                "java/lang/OutOfMemoryError", "modified UTF-8 string");
        pthread_mutex_unlock(&e->vm->mutex); return NULL;
    }
    return make_string(e, chars, units);
}
static jsize jni_GetStringLength(JNIEnv *env, jstring handle) {
    environment *e = state(env);
    pthread_mutex_lock(&e->vm->mutex);
    object *o = find_object(e->vm, handle);
    jsize length = o && o->kind == STRING ? (jsize)o->capacity : 0;
    pthread_mutex_unlock(&e->vm->mutex);
    return length;
}
static jsize jni_GetStringUTFLength(JNIEnv *env, jstring handle) {
    environment *e = state(env);
    pthread_mutex_lock(&e->vm->mutex);
    object *o = find_object(e->vm, handle);
    jsize length = o && o->kind == STRING ? (jsize)strlen(o->name) : 0;
    pthread_mutex_unlock(&e->vm->mutex);
    return length;
}
static const void *string_chars(JNIEnv *env, jstring handle, jboolean *is_copy, int utf8) {
    environment *e = state(env);
    pthread_mutex_lock(&e->vm->mutex);
    object *o = find_object(e->vm, handle);
    const void *result = NULL;
    if (o && o->kind == STRING) {
        result = utf8 ? o->name : o->data;
        ++o->refs; ++o->pins;
        if (is_copy) *is_copy = JNI_FALSE;
    }
    pthread_mutex_unlock(&e->vm->mutex);
    return result;
}
static const jchar *jni_GetStringChars(JNIEnv *env, jstring s, jboolean *copy) {
    return string_chars(env, s, copy, 0);
}
static const jchar *jni_GetStringCritical(JNIEnv *env, jstring s, jboolean *copy) {
    return string_chars(env, s, copy, 0);
}
static const char *jni_GetStringUTFChars(JNIEnv *env, jstring s, jboolean *copy) {
    return string_chars(env, s, copy, 1);
}
static void release_string_chars(JNIEnv *env, jstring s, const void *chars, int utf8) {
    environment *e = state(env);
    pthread_mutex_lock(&e->vm->mutex);
    object *o = find_object(e->vm, s);
    if (o && o->kind == STRING && o->pins && chars == (utf8 ? o->name : o->data)) {
        --o->pins; release(e->vm, o);
    }
    pthread_mutex_unlock(&e->vm->mutex);
}
static void jni_ReleaseStringChars(JNIEnv *env, jstring s, const jchar *chars) {
    release_string_chars(env, s, chars, 0);
}
static void jni_ReleaseStringCritical(JNIEnv *env, jstring s, const jchar *chars) {
    release_string_chars(env, s, chars, 0);
}
static void jni_ReleaseStringUTFChars(JNIEnv *env, jstring s, const char *chars) {
    release_string_chars(env, s, chars, 1);
}
int ws_jni_native_exception_message(JNIEnv *env,jobject instance,jstring *out) {
    environment *e=state(env);*out=NULL;pthread_mutex_lock(&e->vm->mutex);
    object *o=find_object(e->vm,instance);
    if(!o||o->kind!=EXCEPTION){pthread_mutex_unlock(&e->vm->mutex);return 0;}
    ++o->refs;const char *message=o->data;pthread_mutex_unlock(&e->vm->mutex);
    if(message)*out=jni_NewStringUTF(env,message);
    pthread_mutex_lock(&e->vm->mutex);release(e->vm,o);pthread_mutex_unlock(&e->vm->mutex);return 1;
}
static void string_region(JNIEnv *env, jstring s, jsize start, jsize length, void *out, int utf8) {
    environment *e = state(env);
    pthread_mutex_lock(&e->vm->mutex);
    object *o = find_object(e->vm, s);
    if (!o || o->kind != STRING || start < 0 || length < 0 || start > o->capacity ||
        length > o->capacity - start || (!out && length)) {
        failure(e, "java/lang/StringIndexOutOfBoundsException", "string region");
    } else if (utf8) {
        char *encoded = NULL;
        size_t bytes;
        if (ws_mutf8_encode((const jchar *)o->data + start, (size_t)length, &encoded, &bytes))
            failure(e, "java/lang/OutOfMemoryError", "UTF-8 region");
        else if (bytes) memcpy(out, encoded, bytes);
        free(encoded);
    } else if (length) memcpy(out, (const jchar *)o->data + start, (size_t)length * sizeof(jchar));
    pthread_mutex_unlock(&e->vm->mutex);
}
static void jni_GetStringRegion(JNIEnv *env, jstring s, jsize start, jsize length, jchar *out) {
    string_region(env, s, start, length, out, 0);
}
static void jni_GetStringUTFRegion(JNIEnv *env, jstring s, jsize start, jsize length, char *out) {
    string_region(env, s, start, length, out, 1);
}
#include "ws_jni_dispatch.inc"
#include "ws_jni_fields.inc"
static void jni_FatalError(JNIEnv *env, const char *message) {
    (void)env;
    fprintf(stderr, "JNI fatal: %s\n", message ? message : "(null)");
    abort();
}
static _Noreturn void unsupported(JNIEnv *env, const char *method) {
    (void)env;
    fprintf(stderr, "Unimplemented JNI operation: %s\n", method);
    abort();
}
#include "ws_jni_unsupported.inc"
static const struct JNINativeInterface native_table = {
#include "ws_jni_table.inc"
};

ws_jni_vm *ws_jni_vm_create(size_t capacity) {
    ws_jni_vm *vm = calloc(1, sizeof(*vm));
    if (!vm) return NULL;
    vm->registry = ws_jni_registry_create(capacity);
    if (!vm->registry) { free(vm); return NULL; }
    if (pthread_mutex_init(&vm->mutex, NULL)) {
        ws_jni_registry_destroy(vm->registry); free(vm); return NULL;
    }
    if (pthread_key_create(&vm->tls, release_environment)) {
        pthread_mutex_destroy(&vm->mutex);
        ws_jni_registry_destroy(vm->registry); free(vm); return NULL;
    }
    vm->table = &invocation_table;
    const struct { const char *name, *parent; } builtins[] = {
        {"java/lang/Object", NULL}, {"java/lang/Class", "java/lang/Object"},
        {"java/lang/Cloneable", "java/lang/Object"}, {"java/io/Serializable", "java/lang/Object"},
        {"java/nio/Buffer", "java/lang/Object"}, {"java/nio/ByteBuffer", "java/nio/Buffer"}, {"java/lang/String", "java/lang/Object"},
        {"java/lang/Throwable", "java/lang/Object"}, {"java/lang/Exception", "java/lang/Throwable"},
        {"java/lang/RuntimeException", "java/lang/Exception"},
        {"java/lang/ClassNotFoundException", "java/lang/Exception"},
        {"java/lang/Error", "java/lang/Throwable"}, {"java/lang/VirtualMachineError", "java/lang/Error"},
        {"java/lang/OutOfMemoryError", "java/lang/VirtualMachineError"},
        {"java/lang/LinkageError", "java/lang/Error"},
        {"java/lang/IncompatibleClassChangeError", "java/lang/LinkageError"},
        {"java/lang/NoSuchMethodError", "java/lang/IncompatibleClassChangeError"},
        {"java/lang/NoSuchFieldError", "java/lang/IncompatibleClassChangeError"},
        {"java/lang/IllegalArgumentException", "java/lang/RuntimeException"},
        {"java/lang/IllegalStateException", "java/lang/RuntimeException"},
        {"java/lang/IllegalMonitorStateException", "java/lang/RuntimeException"},
        {"java/lang/IndexOutOfBoundsException", "java/lang/RuntimeException"},
        {"java/lang/StringIndexOutOfBoundsException", "java/lang/IndexOutOfBoundsException"},
        {"java/lang/ArrayIndexOutOfBoundsException", "java/lang/IndexOutOfBoundsException"},
        {"java/lang/NegativeArraySizeException", "java/lang/RuntimeException"},
        {"java/lang/ArrayStoreException", "java/lang/RuntimeException"},
        {"java/lang/NullPointerException", "java/lang/RuntimeException"},
        {"java/lang/UnsupportedOperationException", "java/lang/RuntimeException"}
    };
    for (size_t i = 0; i < sizeof(builtins) / sizeof(*builtins); ++i) {
        if (ws_jni_vm_define_subclass(vm, builtins[i].name, builtins[i].parent)) {
            ws_jni_vm_destroy(vm); return NULL;
        }
    }
    vm->oom = calloc(1, sizeof(*vm->oom));
    if (!vm->oom) { ws_jni_vm_destroy(vm); return NULL; }
    vm->oom->name = copy_string("java/lang/OutOfMemoryError");
    if (!vm->oom->name) { free(vm->oom); vm->oom = NULL; ws_jni_vm_destroy(vm); return NULL; }
    vm->oom->kind = EXCEPTION;
    vm->oom->refs = 1;
    vm->oom->next = vm->objects;
    vm->objects = vm->oom;
    return vm;
}
JavaVM *ws_jni_vm_handle(ws_jni_vm *vm) { return vm ? &vm->table : NULL; }
int ws_jni_vm_has_class(ws_jni_vm *vm,const char *name) {
    if(!vm||!name)return 0;
    pthread_mutex_lock(&vm->mutex);int yes=find_class(vm,name)!=NULL;
    pthread_mutex_unlock(&vm->mutex);return yes;
}
const char *ws_jni_class_name(JNIEnv *env,jclass clazz) {
    if(!env)return NULL;
    ws_jni_vm *vm=state(env)->vm;pthread_mutex_lock(&vm->mutex);
    object *o=find_object(vm,clazz);const char *name=o&&o->kind==CLASS?o->name:NULL;
    pthread_mutex_unlock(&vm->mutex);return name;
}
jint ws_jni_identity_hash(JNIEnv *env,jobject instance) {
    if(!env||!instance)return 0;
    ws_jni_vm *vm=state(env)->vm;pthread_mutex_lock(&vm->mutex);
    object *o=find_object(vm,instance);uintptr_t address=(uintptr_t)o;
    /* Objects never move. Fold the native pointer for 64-bit host tests too. */
    uint32_t hash=(uint32_t)address;
#if UINTPTR_MAX > UINT32_MAX
    hash^=(uint32_t)(address>>32);
#endif
    pthread_mutex_unlock(&vm->mutex);return (jint)hash;
}
int ws_jni_vm_define_class(ws_jni_vm *vm, const char *name) {
    return ws_jni_vm_define_subclass(vm, name,
                                    name && !strcmp(name, "java/lang/Object") ? NULL : "java/lang/Object");
}
int ws_jni_vm_define_subclass(ws_jni_vm *vm, const char *name, const char *parent) {
    if (!vm || !name || !*name || strlen(name) > 255) return -1;
    pthread_mutex_lock(&vm->mutex);
    object *super = parent ? find_class(vm, parent) : NULL;
    if ((parent && !super) || (!parent && strcmp(name, "java/lang/Object"))) {
        pthread_mutex_unlock(&vm->mutex); return -1;
    }
    object *existing = find_class(vm, name);
    if (existing) {
        int result = existing->parent == super ? 0 : -1;
        pthread_mutex_unlock(&vm->mutex); return result;
    }
    object *o = calloc(1, sizeof(*o));
    char *copy = malloc(strlen(name) + 1);
    if (!o || !copy) {
        free(o); free(copy); pthread_mutex_unlock(&vm->mutex); return -1;
    }
    strcpy(copy, name);
    o->name = copy;
    o->refs = 1; /* Class cache owns a permanent reference until VM shutdown. */
    o->kind = CLASS;
    o->parent = super;
    o->next = vm->objects;
    vm->objects = o;
    pthread_mutex_unlock(&vm->mutex);
    return 0;
}
void ws_jni_vm_exports(ws_jni_vm *vm,ws_jni_export_resolver resolver,void *context){
    if(!vm)return;
    pthread_mutex_lock(&vm->mutex);vm->exports=resolver;vm->export_context=context;pthread_mutex_unlock(&vm->mutex);
}
static int native_mangle(char *out,size_t cap,size_t *used,const char *text){
    uint16_t *units=NULL;size_t count=0;if(ws_mutf8_decode(text,&units,&count))return -1;
    for(size_t i=0;i<count;++i){unsigned c=units[i];char part[8];size_t n=1;
        if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9'))part[0]=(char)c;
        else if(c=='/')part[0]='_';
        else if(c=='_'||c==';'||c=='['){part[0]='_';part[1]=c=='_'?'1':c==';'?'2':'3';n=2;}
        else {snprintf(part,sizeof(part),"_0%04x",c);n=6;}
        if(n>=cap-*used){free(units);return -1;}memcpy(out+*used,part,n);*used+=n;
    }
    out[*used]=0;free(units);return 0;
}
uintptr_t ws_jni_vm_native(ws_jni_vm *vm, const char *class_name,
                          const char *name, const char *signature) {
    if (!vm||!class_name||!name||!signature) return 0;
    pthread_mutex_lock(&vm->mutex);
    uintptr_t result = ws_jni_lookup(vm->registry, class_name, name, signature);
    ws_jni_export_resolver resolver=vm->exports;void *context=vm->export_context;
    pthread_mutex_unlock(&vm->mutex);
    if(!result&&resolver){
        char symbol[8192]="Java_",parameters[1024];size_t used=5;
        if(native_mangle(symbol,sizeof(symbol),&used,class_name)||used+1>=sizeof(symbol))return 0;
        symbol[used++]='_';symbol[used]=0;
        if(native_mangle(symbol,sizeof(symbol),&used,name))return 0;
        result=resolver(context,symbol);if(result)return result;
        const char *end=strchr(signature,')');if(*signature!='('||!end||(size_t)(end-signature-1)>=sizeof(parameters)||used+2>=sizeof(symbol))return 0;
        size_t n=(size_t)(end-signature-1);memcpy(parameters,signature+1,n);parameters[n]=0;
        symbol[used++]='_';symbol[used++]='_';symbol[used]=0;
        if(!native_mangle(symbol,sizeof(symbol),&used,parameters))result=resolver(context,symbol);
    }
    return result;
}
int ws_jni_vm_exclusive(ws_jni_vm *vm) {
    if(!vm)return 0;
    pthread_mutex_lock(&vm->mutex);
    int yes=vm->attached==(pthread_getspecific(vm->tls)?1u:0u);
    pthread_mutex_unlock(&vm->mutex);return yes;
}
int ws_jni_vm_destroy(ws_jni_vm *vm) {
    if (!vm) return JNI_EINVAL;
    detach(&vm->table);
    pthread_mutex_lock(&vm->mutex);
    if (vm->attached) { pthread_mutex_unlock(&vm->mutex); return JNI_ERR; }
    array_copy *copy = vm->array_copies;
    while (copy) {
        array_copy *next = copy->next;
        free(copy->data); free(copy); copy = next;
    }
    object *o = vm->objects;
    reference_handle *ref=vm->handles;
    while(ref){reference_handle *next=ref->next;free(ref);ref=next;}
    vm->handles=NULL;
    while (o) {
        object *next = o->next;
        free(o->name);
        if (o->kind == EXCEPTION || o->kind == STRING || o->kind == ARRAY) free(o->data);
        if (o->kind == INSTANCE && o->destroy_data) o->destroy_data(o->data);
        monitor_destroy(o->monitor);
        field_value *v = o->fields;
        while (v) { field_value *next_value = v->next; free(v); v = next_value; }
        free(o); o = next;
    }
    ws_jni_registry_destroy(vm->registry);
    method *m = vm->methods;
    while (m) { method *next = m->next; free(m->name); free(m->signature); free(m); m = next; }
    field *f = vm->fields;
    while (f) { field *next = f->next; free(f->name); free(f->signature); free(f); f = next; }
    pthread_mutex_unlock(&vm->mutex);
    pthread_key_delete(vm->tls);
    pthread_mutex_destroy(&vm->mutex);
    free(vm);
    return JNI_OK;
}
uintptr_t ws_jni_vm_lookup(void *vm, const char *class_name, const char *name, const char *signature) {
    return ws_jni_vm_native(vm, class_name, name, signature);
}
