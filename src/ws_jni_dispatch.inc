/* Included in jni_vm.c: uses the VM lock for metadata, releases it before
 * invoking a service. Methods and classes live until VM shutdown. */
static method *find_method(ws_jni_vm *vm, object *clazz, const char *name,
                           const char *signature, int is_static) {
    for (object *c = clazz; c; c = c->parent) {
        for (method *m = vm->methods; m; m = m->next)
            if (m->clazz == c && m->is_static == is_static &&
                !strcmp(m->name, name) && !strcmp(m->signature, signature)) return m;
        if (!strcmp(name, "<init>")) break;
    }
    return NULL;
}
int ws_jni_vm_define_method(ws_jni_vm *vm, const char *class_name, const char *name,
                             const char *signature, int is_static,
                             ws_jni_service service, void *userdata) {
    ws_jni_descriptor descriptor;
    if (!vm || !class_name || !name || !*name || strlen(name) > 127 ||
        ws_jni_parse_descriptor(signature, &descriptor) ||
        (!strcmp(name, "<init>") && (is_static || descriptor.result != 'V'))) return -1;
    pthread_mutex_lock(&vm->mutex);
    object *clazz = find_class(vm, class_name);
    method *previous = clazz ? find_method(vm, clazz, name, signature, !!is_static) : NULL;
    if (!clazz) {
        pthread_mutex_unlock(&vm->mutex); return -1;
    }
    if (previous && previous->clazz == clazz) {
        int result = 0;
        if (service) {
            if (previous->service) result = -1;
            else { previous->userdata = userdata; previous->service = service; }
        }
        pthread_mutex_unlock(&vm->mutex); return result;
    }
    method *m = calloc(1, sizeof(*m));
    if (m) { m->name = copy_string(name); m->signature = copy_string(signature); }
    if (!m || !m->name || !m->signature) {
        if (m) { free(m->name); free(m->signature); free(m); }
        pthread_mutex_unlock(&vm->mutex); return -1;
    }
    m->clazz = clazz; m->descriptor = descriptor; m->is_static = !!is_static;
    m->service = service; m->userdata = userdata;
    m->next = vm->methods; vm->methods = m;
    pthread_mutex_unlock(&vm->mutex);
    return 0;
}
static jmethodID get_method_id(JNIEnv *env, jclass handle, const char *name,
                                const char *signature, int is_static) {
    environment *e = state(env);
    pthread_mutex_lock(&e->vm->mutex);
    object *clazz = find_object(e->vm, handle);
    method *m = clazz && clazz->kind == CLASS && name && signature ?
        find_method(e->vm, clazz, name, signature, is_static) : NULL;
    if (!m) failure(e, "java/lang/NoSuchMethodError", name);
    pthread_mutex_unlock(&e->vm->mutex);
    return (jmethodID)m;
}
static jmethodID jni_GetMethodID(JNIEnv *env, jclass c, const char *n, const char *s) {
    return get_method_id(env, c, n, s, 0);
}
static jmethodID jni_GetStaticMethodID(JNIEnv *env, jclass c, const char *n, const char *s) {
    return get_method_id(env, c, n, s, 1);
}
static jclass jni_GetSuperclass(JNIEnv *env, jclass c) {
    environment *e = state(env);
    pthread_mutex_lock(&e->vm->mutex);
    object *o = find_object(e->vm, c);
    jobject result = o && o->kind == CLASS ? local_ref(e, o->parent) : NULL;
    pthread_mutex_unlock(&e->vm->mutex);
    return result;
}
static jboolean jni_IsAssignableFrom(JNIEnv *env, jclass sub, jclass super) {
    environment *e = state(env);
    pthread_mutex_lock(&e->vm->mutex);
    object *a = find_object(e->vm, sub), *b = find_object(e->vm, super);
    int result = a && b && a->kind == CLASS && b->kind == CLASS && subclass(a, b);
    pthread_mutex_unlock(&e->vm->mutex);
    return result ? JNI_TRUE : JNI_FALSE;
}
static jboolean jni_IsInstanceOf(JNIEnv *env, jobject obj, jclass clazz) {
    environment *e = state(env);
    pthread_mutex_lock(&e->vm->mutex);
    object *c = find_object(e->vm, clazz), *o = find_object(e->vm, obj);
    int result = c && c->kind == CLASS && (!o || subclass(object_class(e->vm, o), c));
    pthread_mutex_unlock(&e->vm->mutex);
    return result ? JNI_TRUE : JNI_FALSE;
}
static jobject allocate_instance(environment *e, object *clazz, void *payload) {
    object *o = clazz ? calloc(1, sizeof(*o)) : NULL;
    if (!o) { failure(e, "java/lang/OutOfMemoryError", "instance allocation"); return NULL; }
    o->kind = INSTANCE; o->clazz = clazz; o->data = payload;
    o->next = e->vm->objects; e->vm->objects = o;
    jobject result = local_ref(e, o);
    if (!result) {
        ++o->refs; release(e->vm, o);
        failure(e, "java/lang/OutOfMemoryError", "instance reference");
    }
    return result;
}
static jobject jni_AllocObject(JNIEnv *env, jclass handle) {
    environment *e = state(env);
    pthread_mutex_lock(&e->vm->mutex);
    object *c = find_object(e->vm, handle);
    jobject result = NULL;
    if (c && c->kind == CLASS) result = allocate_instance(e, c, NULL);
    else failure(e, "java/lang/IllegalArgumentException", "AllocObject class");
    pthread_mutex_unlock(&e->vm->mutex);
    return result;
}
jobject ws_jni_vm_new_instance(JNIEnv *env, const char *class_name, void *payload) {
    return ws_jni_vm_new_owned_instance(env,class_name,payload,NULL);
}
jobject ws_jni_vm_new_owned_instance(JNIEnv *env,const char *class_name,void *payload,void (*destroy)(void *)) {
    if (!env || !class_name) return NULL;
    environment *e = state(env);
    pthread_mutex_lock(&e->vm->mutex);
    object *c = find_class(e->vm, class_name);
    jobject result = NULL;
    if (c) result = allocate_instance(e, c, payload);
    else failure(e, "java/lang/ClassNotFoundException", class_name);
    if(result)((object *)result)->destroy_data=destroy;
    pthread_mutex_unlock(&e->vm->mutex);
    return result;
}
void *ws_jni_vm_instance_data(JNIEnv *env, jobject handle) {
    environment *e = state(env);
    pthread_mutex_lock(&e->vm->mutex);
    object *o = find_object(e->vm, handle);
    void *result = o && o->kind == INSTANCE ? o->data : NULL;
    pthread_mutex_unlock(&e->vm->mutex);
    return result;
}
/* mode: 0 virtual, 1 static, 2 nonvirtual. Validate before reading varargs. */
static method *checked_method(JNIEnv *env, jobject receiver, jclass nonvirtual,
                               jmethodID id, int mode, char result) {
    environment *e = state(env);
    pthread_mutex_lock(&e->vm->mutex);
    method *m;
    for (m = e->vm->methods; m; m = m->next) if ((jmethodID)m == id) break;
    object *o = find_object(e->vm, receiver);
    object *c = mode == 1 ? (o && o->kind == CLASS ? o : NULL) : object_class(e->vm, o);
    int valid = m && c && m->is_static == (mode == 1) &&
        m->descriptor.result == result && subclass(c, m->clazz);
    if (valid && mode == 2) {
        object *n = find_object(e->vm, nonvirtual);
        valid = n && n->kind == CLASS && subclass(c, n) && subclass(n, m->clazz);
        if (valid) m = find_method(e->vm, n, m->name, m->signature, 0);
    } else if (valid && !mode) m = find_method(e->vm, c, m->name, m->signature, 0);
    if (!valid) {
        failure(e, receiver ? "java/lang/IllegalArgumentException" : "java/lang/NullPointerException",
                "JNI method receiver, ID or return type mismatch");
        m = NULL;
    }
    pthread_mutex_unlock(&e->vm->mutex);
    return m;
}
static jvalue invoke_service(JNIEnv *env, jobject receiver, method *m, const jvalue *args) {
    environment *e = state(env);
    pthread_mutex_lock(&e->vm->mutex);
    ws_jni_service service = m->service;
    void *userdata = m->userdata;
    if (!service) {
        char message[512];
        snprintf(message, sizeof(message), "Unimplemented service: %s.%s%s", m->clazz->name, m->name, m->signature);
        failure(e, "java/lang/UnsupportedOperationException", message);
    }
    pthread_mutex_unlock(&e->vm->mutex);
    jvalue zero = {0};
    return service ? service(userdata, env, receiver, args, m->descriptor.count) : zero;
}
static jvalue call_a(JNIEnv *env, jobject receiver, jclass clazz, jmethodID id,
                     int mode, char result, const jvalue *args) {
    jvalue zero = {0};
    if (state(env)->pending) return zero;
    method *m = checked_method(env, receiver, clazz, id, mode, result);
    if (!m) return zero;
    if (!args && m->descriptor.count) {
        environment *e = state(env);
        pthread_mutex_lock(&e->vm->mutex);
        failure(e, "java/lang/IllegalArgumentException", "missing JNI arguments");
        pthread_mutex_unlock(&e->vm->mutex); return zero;
    }
    return invoke_service(env, receiver, m, args);
}
static jvalue call_v(JNIEnv *env, jobject receiver, jclass clazz, jmethodID id,
                     int mode, char result, va_list input) {
    jvalue zero = {0};
    if (state(env)->pending) return zero;
    method *m = checked_method(env, receiver, clazz, id, mode, result);
    if (!m) return zero;
    jvalue args[255];
    va_list ap;
    va_copy(ap, input);
    for (size_t i = 0; i < m->descriptor.count; ++i) {
        switch (m->descriptor.arguments[i]) {
        case 'Z': args[i].z = (jboolean)va_arg(ap, int); break;
        case 'B': args[i].b = (jbyte)va_arg(ap, int); break;
        case 'C': args[i].c = (jchar)va_arg(ap, int); break;
        case 'S': args[i].s = (jshort)va_arg(ap, int); break;
        case 'I': args[i].i = va_arg(ap, jint); break;
        case 'J': args[i].j = va_arg(ap, jlong); break;
        case 'F': args[i].f = (jfloat)va_arg(ap, double); break;
        case 'D': args[i].d = va_arg(ap, jdouble); break;
        case 'L': args[i].l = va_arg(ap, jobject); break;
        }
    }
    va_end(ap);
    return invoke_service(env, receiver, m, args);
}
#include "ws_jni_calls.inc"
static int constructor(JNIEnv *env, jclass c, jmethodID id) {
    environment *e = state(env);
    pthread_mutex_lock(&e->vm->mutex);
    method *m;
    for (m = e->vm->methods; m; m = m->next) if ((jmethodID)m == id) break;
    object *clazz = find_object(e->vm, c);
    int valid = m && clazz && clazz->kind == CLASS && m->clazz == clazz &&
        !m->is_static && !strcmp(m->name, "<init>");
    if (!valid) {
        char message[512];
        snprintf(message, sizeof(message), "constructor: class=%s method=%s.%s%s",
            clazz && clazz->kind == CLASS ? clazz->name : "<invalid>",
            m ? m->clazz->name : "<invalid>", m ? m->name : "", m ? m->signature : "");
        failure(e, "java/lang/NoSuchMethodError", message);
    }
    pthread_mutex_unlock(&e->vm->mutex);
    return valid;
}
static jobject jni_NewObjectA(JNIEnv *env, jclass clazz, jmethodID method_id, const jvalue *args) {
    if (!constructor(env, clazz, method_id)) return NULL;
    jobject o = jni_AllocObject(env, clazz);
    if (!o) return NULL;
    call_a(env, o, clazz, method_id, 2, 'V', args);
    if (state(env)->pending) { jni_DeleteLocalRef(env, o); return NULL; }
    return o;
}
static jobject jni_NewObjectV(JNIEnv *env, jclass clazz, jmethodID method_id, va_list args) {
    if (!constructor(env, clazz, method_id)) return NULL;
    jobject o = jni_AllocObject(env, clazz);
    if (!o) return NULL;
    call_v(env, o, clazz, method_id, 2, 'V', args);
    if (state(env)->pending) { jni_DeleteLocalRef(env, o); return NULL; }
    return o;
}
static jobject jni_NewObject(JNIEnv *env, jclass clazz, jmethodID method_id, ...) {
    va_list ap;
    va_start(ap, method_id);
    jobject result = jni_NewObjectV(env, clazz, method_id, ap);
    va_end(ap);
    return result;
}
