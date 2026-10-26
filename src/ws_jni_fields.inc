static field *find_field(ws_jni_vm *vm, object *clazz, const char *name,
                         const char *signature, int is_static) {
    for (object *c = clazz; c; c = c->parent)
        for (field *f = vm->fields; f; f = f->next)
            if (f->clazz == c && f->is_static == is_static &&
                !strcmp(f->name, name) && !strcmp(f->signature, signature)) return f;
    return NULL;
}
int ws_jni_vm_define_field(ws_jni_vm *vm, const char *class_name, const char *name,
                            const char *signature, int is_static) {
    if (!vm || !class_name || !name || !*name || strlen(name) > 127 || !signature ||
        strlen(signature) > 508) return -1;
    char descriptor[512];
    snprintf(descriptor, sizeof(descriptor), "()%s", signature);
    ws_jni_descriptor parsed;
    if (ws_jni_parse_descriptor(descriptor, &parsed) || parsed.result == 'V') return -1;
    pthread_mutex_lock(&vm->mutex);
    object *c = find_class(vm, class_name);
    field *existing = c ? find_field(vm, c, name, signature, !!is_static) : NULL;
    if (!c || (existing && existing->clazz == c)) { pthread_mutex_unlock(&vm->mutex); return -1; }
    field *f = calloc(1, sizeof(*f));
    if (f) { f->name = copy_string(name); f->signature = copy_string(signature); }
    if (!f || !f->name || !f->signature) {
        if (f) { free(f->name); free(f->signature); free(f); }
        pthread_mutex_unlock(&vm->mutex); return -1;
    }
    f->clazz = c; f->is_static = !!is_static; f->kind = parsed.result;
    f->next = vm->fields; vm->fields = f;
    pthread_mutex_unlock(&vm->mutex);
    return 0;
}
static jfieldID get_field_id(JNIEnv *env, jclass handle, const char *name,
                              const char *signature, int is_static) {
    environment *e = state(env);
    pthread_mutex_lock(&e->vm->mutex);
    object *c = find_object(e->vm, handle);
    field *f = c && c->kind == CLASS && name && signature ?
        find_field(e->vm, c, name, signature, is_static) : NULL;
    if (!f) failure(e, "java/lang/NoSuchFieldError", name);
    pthread_mutex_unlock(&e->vm->mutex);
    return (jfieldID)f;
}
static jfieldID jni_GetFieldID(JNIEnv *env, jclass c, const char *n, const char *s) {
    return get_field_id(env, c, n, s, 0);
}
static jfieldID jni_GetStaticFieldID(JNIEnv *env, jclass c, const char *n, const char *s) {
    return get_field_id(env, c, n, s, 1);
}
/* VM mutex held. */
static field *checked_field(environment *e, jobject receiver, jfieldID id,
                             int is_static, char kind, object **owner) {
    field *f;
    for (f = e->vm->fields; f; f = f->next) if ((jfieldID)f == id) break;
    object *o = find_object(e->vm, receiver);
    object *c = o ? (is_static ? (o->kind == CLASS ? o : NULL) : object_class(e->vm,o)) : NULL;
    if (!f || !c || f->is_static != is_static || f->kind != kind || !subclass(c, f->clazz)) {
        failure(e, receiver ? "java/lang/IllegalArgumentException" : "java/lang/NullPointerException",
                "JNI field receiver, ID or type mismatch");
        return NULL;
    }
    *owner = o;
    return f;
}
static jvalue read_field(JNIEnv *env, jobject receiver, jfieldID id, int is_static, char kind) {
    environment *e = state(env);
    pthread_mutex_lock(&e->vm->mutex);
    object *o = NULL;
    field *f = checked_field(e, receiver, id, is_static, kind, &o);
    jvalue result = {0};
    if (f) {
        if (is_static) result = f->value;
        else for (field_value *v = o->fields; v; v = v->next)
            if (v->definition == f) { result = v->value; break; }
        if (kind == 'L') result.l = local_ref(e, find_object(e->vm, result.l));
    }
    pthread_mutex_unlock(&e->vm->mutex);
    return result;
}
static void write_field(JNIEnv *env, jobject receiver, jfieldID id, int is_static, char kind, jvalue value) {
    environment *e = state(env);
    pthread_mutex_lock(&e->vm->mutex);
    object *o = NULL;
    field *f = checked_field(e, receiver, id, is_static, kind, &o);
    if (!f) { pthread_mutex_unlock(&e->vm->mutex); return; }
    object *new_reference = NULL;
    if (kind == 'L' && value.l) {
        new_reference = find_object(e->vm, value.l);
        char name[512];
        size_t length = strlen(f->signature);
        object *expected = NULL;
        if (f->signature[0] == 'L') {
            memcpy(name, f->signature + 1, length - 2); name[length - 2] = 0;
            expected = find_class(e->vm, name);
        }
        else if (f->signature[0] == '[') expected = array_class(e, f->signature);
        if (!new_reference || !expected || !subclass(object_class(e->vm, new_reference), expected)) {
            failure(e, "java/lang/IllegalArgumentException", "incompatible object field value");
            pthread_mutex_unlock(&e->vm->mutex); return;
        }
    }
    jvalue *destination = &f->value;
    if (!is_static) {
        field_value *v;
        for (v = o->fields; v; v = v->next) if (v->definition == f) break;
        if (!v) {
            v = calloc(1, sizeof(*v));
            if (!v) {
                failure(e, "java/lang/OutOfMemoryError", "instance field");
                pthread_mutex_unlock(&e->vm->mutex); return;
            }
            v->definition = f; v->next = o->fields; o->fields = v;
        }
        destination = &v->value;
    }
    if (new_reference) ++new_reference->refs;
    object *old_reference = kind == 'L' ? destination->l : NULL;
    if(kind=='L')value.l=new_reference; /* Store the object, never a disposable JNI handle. */
    *destination = value;
    if (old_reference) release(e->vm, old_reference);
    pthread_mutex_unlock(&e->vm->mutex);
}
#include "ws_jni_field_access.inc"
