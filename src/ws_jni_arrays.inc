/* JNI arrays. VM mutex protects storage and reference ownership. Element
 * accessors use private copies with explicit commit/abort lifetimes; critical
 * access is also allowed to copy by JNI, so it needs no moving-GC pin policy. */
static size_t element_size(char type) {
    switch (type) {
    case 'Z': case 'B': return 1;
    case 'C': case 'S': return 2;
    case 'I': case 'F': return 4;
    case 'J': case 'D': return 8;
    case 'L': return sizeof(object *);
    default: return 0;
    }
}
/* Held VM lock, valid component types must already exist. */
static object *array_class(environment *e, const char *name) {
    if (!name || *name != '[' || strlen(name) > 510) return NULL;
    size_t dimensions = strspn(name, "[");
    if (dimensions > 255) return NULL;
    object *existing = find_class(e->vm, name);
    if (existing) return existing->array_kind ? existing : NULL;
    char kind = name[1];
    object *component = NULL;
    if (kind == '[') { component = array_class(e, name + 1); kind = 'L'; }
    else if (kind == 'L') {
        size_t n = strlen(name);
        if (n < 4 || name[n - 1] != ';') return NULL;
        char cname[512]; memcpy(cname, name + 2, n - 3); cname[n - 3] = 0;
        component = find_class(e->vm, cname);
    } else if (!element_size(kind) || name[2]) return NULL;
    if (kind == 'L' && !component) return NULL;
    object *c = calloc(1, sizeof(*c));
    if (c) c->name = copy_string(name);
    if (!c || !c->name) {
        if (c) free(c);
        failure(e, "java/lang/OutOfMemoryError", "array class"); return NULL;
    }
    c->kind = CLASS; c->refs = 1;
    c->parent = find_class(e->vm, "java/lang/Object");
    c->component = component; c->array_kind = kind;
    c->next = e->vm->objects; e->vm->objects = c;
    return c;
}
static object *checked_array(environment *e, jarray handle, char type) {
    object *o = find_object(e->vm, handle);
    if (!o || o->kind != ARRAY || (type && o->clazz->array_kind != type)) {
        failure(e, handle ? "java/lang/IllegalArgumentException" : "java/lang/NullPointerException", "JNI array type");
        return NULL;
    }
    return o;
}
static int array_range(environment *e, object *o, jsize start, jsize length) {
    if (start < 0 || length < 0 || start > o->capacity || length > o->capacity - start) {
        failure(e, "java/lang/ArrayIndexOutOfBoundsException", "JNI array region"); return -1;
    }
    return 0;
}
static jarray allocate_array(environment *e, object *clazz, jsize length) {
    if (length < 0) { failure(e, "java/lang/NegativeArraySizeException", "JNI array length"); return NULL; }
    if (!clazz) return NULL;
    size_t width = element_size(clazz->array_kind);
    object *o = (size_t)length <= SIZE_MAX / width ? calloc(1, sizeof(*o)) : NULL;
    if (o) o->data = calloc(length ? (size_t)length : 1, width);
    if (!o || !o->data) {
        free(o); failure(e, "java/lang/OutOfMemoryError", "JNI array storage"); return NULL;
    }
    o->kind = ARRAY; o->clazz = clazz; o->capacity = length;
    o->next = e->vm->objects; e->vm->objects = o;
    jobject result = local_ref(e, o);
    if (!result) { ++o->refs; release(e->vm, o); }
    return result;
}
static jsize jni_GetArrayLength(JNIEnv *env, jarray array) {
    environment *e = state(env); pthread_mutex_lock(&e->vm->mutex);
    object *o = checked_array(e, array, 0);
    jsize result = o ? (jsize)o->capacity : 0;
    pthread_mutex_unlock(&e->vm->mutex); return result;
}
static jobjectArray jni_NewObjectArray(JNIEnv *env, jsize length, jclass element, jobject initial) {
    environment *e = state(env); pthread_mutex_lock(&e->vm->mutex);
    object *c = find_object(e->vm, element), *value = find_object(e->vm, initial);
    jobject result = NULL;
    if (!c || c->kind != CLASS) failure(e, "java/lang/IllegalArgumentException", "array element class");
    else if (initial && (!value || !subclass(object_class(e->vm, value), c)))
        failure(e, "java/lang/ArrayStoreException", "initial array element");
    else {
        char name[512];
        int n = snprintf(name, sizeof(name), c->array_kind ? "[%s" : "[L%s;", c->name);
        if (n < 0 || n >= (int)sizeof(name)) failure(e, "java/lang/IllegalArgumentException", "array class name");
        else {
            result = allocate_array(e, array_class(e, name), length);
            if (result && value) {
                object *o = result;
                for (jsize i = 0; i < length; ++i) { ((object **)o->data)[i] = value; ++value->refs; }
            }
        }
    }
    pthread_mutex_unlock(&e->vm->mutex); return result;
}
static jobject jni_GetObjectArrayElement(JNIEnv *env, jobjectArray array, jsize index) {
    environment *e = state(env); pthread_mutex_lock(&e->vm->mutex);
    object *o = checked_array(e, array, 'L');
    jobject result = o && !array_range(e, o, index, 1) ? local_ref(e, ((object **)o->data)[index]) : NULL;
    pthread_mutex_unlock(&e->vm->mutex); return result;
}
static void jni_SetObjectArrayElement(JNIEnv *env, jobjectArray array, jsize index, jobject value) {
    environment *e = state(env); pthread_mutex_lock(&e->vm->mutex);
    object *o = checked_array(e, array, 'L'), *new_value = find_object(e->vm, value);
    if (o && !array_range(e, o, index, 1)) {
        if (value && (!new_value || !subclass(object_class(e->vm, new_value), o->clazz->component)))
            failure(e, "java/lang/ArrayStoreException", "incompatible array element");
        else {
            if (new_value) ++new_value->refs;
            object *old = ((object **)o->data)[index];
            ((object **)o->data)[index] = new_value;
            if (old) release(e->vm, old);
        }
    }
    pthread_mutex_unlock(&e->vm->mutex);
}
static jarray new_primitive_array(JNIEnv *env, jsize length, char kind) {
    environment *e = state(env); pthread_mutex_lock(&e->vm->mutex);
    char name[] = {'[', kind, 0};
    jarray result = allocate_array(e, array_class(e, name), length);
    pthread_mutex_unlock(&e->vm->mutex); return result;
}
static void primitive_region(JNIEnv *env, jarray array, jsize start, jsize length, void *data, char kind, int write) {
    environment *e = state(env); pthread_mutex_lock(&e->vm->mutex);
    object *o = checked_array(e, array, kind);
    if (o && !array_range(e, o, start, length)) {
        if (length && !data) failure(e, "java/lang/NullPointerException", "array region data");
        else if (length) {
            size_t width = element_size(kind), bytes = (size_t)length * width;
            void *storage = (char *)o->data + (size_t)start * width;
            if (write) memcpy(storage, data, bytes); else memcpy(data, storage, bytes);
        }
    }
    pthread_mutex_unlock(&e->vm->mutex);
}
static void *primitive_elements(JNIEnv *env, jarray array, jboolean *is_copy, char kind, int critical) {
    environment *e = state(env); pthread_mutex_lock(&e->vm->mutex);
    object *o = checked_array(e, array, kind);
    if (o && o->clazz->array_kind == 'L') {
        failure(e, "java/lang/IllegalArgumentException", "primitive array required"); o = NULL;
    }
    void *result = NULL;
    if (o) {
        size_t bytes = (size_t)o->capacity * element_size(o->clazz->array_kind);
        array_copy *copy = calloc(1, sizeof(*copy));
        if (copy) copy->data = malloc(bytes ? bytes : 1);
        if (!copy || !copy->data) {
            free(copy); failure(e, "java/lang/OutOfMemoryError", "array elements copy");
        } else {
            memcpy(copy->data, o->data, bytes);
            copy->array = o; ++o->refs; copy->critical = critical; copy->owner = env;
            copy->next = e->vm->array_copies; e->vm->array_copies = copy;
            result = copy->data;
            if (is_copy) *is_copy = JNI_TRUE;
        }
    }
    pthread_mutex_unlock(&e->vm->mutex); return result;
}
static void release_elements(JNIEnv *env, jarray array, void *data, jint mode, char kind, int critical) {
    environment *e = state(env); pthread_mutex_lock(&e->vm->mutex);
    object *o = checked_array(e, array, kind);
    array_copy **link = &e->vm->array_copies;
    while (*link && ((*link)->data != data || (*link)->array != o || (*link)->critical != critical ||
                    (critical && (*link)->owner != env))) link = &(*link)->next;
    if (o && (!*link || (mode != 0 && mode != JNI_COMMIT && mode != JNI_ABORT)))
        failure(e, "java/lang/IllegalArgumentException", "array copy release or mode");
    else if (o) {
        array_copy *copy = *link;
        if (mode != JNI_ABORT) memcpy(o->data, copy->data, (size_t)o->capacity * element_size(o->clazz->array_kind));
        if (mode != JNI_COMMIT) {
            *link = copy->next; release(e->vm, o); free(copy->data); free(copy);
        }
    }
    pthread_mutex_unlock(&e->vm->mutex);
}
static void *jni_GetPrimitiveArrayCritical(JNIEnv *env, jarray array, jboolean *is_copy) {
    return primitive_elements(env, array, is_copy, 0, 1);
}
static void jni_ReleasePrimitiveArrayCritical(JNIEnv *env, jarray array, void *data, jint mode) {
    release_elements(env, array, data, mode, 0, 1);
}
#include "ws_jni_primitive_arrays.inc"
