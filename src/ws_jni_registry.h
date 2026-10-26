#ifndef WS_JNI_REGISTRY_H
#define WS_JNI_REGISTRY_H

#include <stddef.h>
#include <stdint.h>

/* This is a registration component, not a Java VM or a Unity implementation.
 * Serialize calls from multiple threads in the eventual JNI adapter.
 * Function pointers remain valid only while their owning library is mapped.
 */
typedef struct ws_jni_registry ws_jni_registry;
typedef struct ws_jni_method {
    const char *name;
    const char *signature;
    uintptr_t address;
} ws_jni_method;

enum {
    WS_JNI_OK = 0,
    WS_JNI_INVALID = -1,
    WS_JNI_CAPACITY = -2,
    WS_JNI_NO_MEMORY = -3
};

ws_jni_registry *ws_jni_registry_create(size_t capacity);
void ws_jni_registry_destroy(ws_jni_registry *registry);
int ws_jni_register(ws_jni_registry *registry, const char *class_name,
                     const ws_jni_method *methods, size_t count);
uintptr_t ws_jni_lookup(const ws_jni_registry *registry, const char *class_name,
                          const char *name, const char *signature);
size_t ws_jni_unregister(ws_jni_registry *registry, const char *class_name);
size_t ws_jni_count(const ws_jni_registry *registry);

#endif
