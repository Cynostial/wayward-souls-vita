#ifndef WS_JNI_VM_H
#define WS_JNI_VM_H
#include "ws_jni_registry.h"
#include "../vendor/android-jni/jni.h"

typedef struct ws_jni_vm ws_jni_vm;
/* Native JNI compatibility environment, not a DEX interpreter. Android classes
 * and their services must be defined by the application adapter. Unsupported
 * JNI entry points terminate explicitly instead of returning fake success.
 */
ws_jni_vm *ws_jni_vm_create(size_t native_capacity);
JavaVM *ws_jni_vm_handle(ws_jni_vm *vm);
int ws_jni_vm_define_class(ws_jni_vm *vm, const char *name);
int ws_jni_vm_has_class(ws_jni_vm *vm,const char *name);
/* Returned name belongs to the VM and stays valid through VM lifetime. */
const char *ws_jni_class_name(JNIEnv *env,jclass clazz);
/* Stable for an object's lifetime and shared by all its JNI references. */
jint ws_jni_identity_hash(JNIEnv *env,jobject instance);
int ws_jni_monitor_wait(JNIEnv *env,jobject instance,jlong milliseconds,jint nanos);
int ws_jni_monitor_notify(JNIEnv *env,jobject instance,int all);
/* 1 for a VM-created exception (out may be NULL), 0 for an ordinary instance. */
int ws_jni_native_exception_message(JNIEnv *env,jobject instance,jstring *out);
int ws_jni_vm_define_subclass(ws_jni_vm *vm, const char *name, const char *parent);
/* Methods run outside the VM lock and can call JNI recursively. Object results
 * must be valid JNI references. Payloads remain owned by the adapter. */
typedef jvalue (*ws_jni_service)(void *userdata, JNIEnv *env, jobject receiver,
                                const jvalue *arguments, size_t count);
/* NULL service declares APK metadata only. Lookup succeeds; invocation throws
 * UnsupportedOperationException until an adapter supplies the implementation.
 * Defining a service later preserves the declared method ID. */
int ws_jni_vm_define_method(ws_jni_vm *vm, const char *class_name, const char *name,
                             const char *signature, int is_static,
                             ws_jni_service service, void *userdata);
jobject ws_jni_vm_new_instance(JNIEnv *env, const char *class_name, void *payload);
/* Payload cleanup runs under the VM lock when the last reference is released.
 * It must not call JNI. Ownership transfers only when creation succeeds. */
jobject ws_jni_vm_new_owned_instance(JNIEnv *env,const char *class_name,void *payload,void (*destroy)(void *));
void *ws_jni_vm_instance_data(JNIEnv *env, jobject instance);
int ws_jni_vm_define_field(ws_jni_vm *vm, const char *class_name, const char *name,
                            const char *signature, int is_static);
uintptr_t ws_jni_vm_native(ws_jni_vm *vm, const char *class_name,
                          const char *name, const char *signature);
/* Resolve conventional Java_* exports when RegisterNatives has no entry.
 * The callback runs outside the VM lock. Set before loading guest libraries. */
typedef uintptr_t (*ws_jni_export_resolver)(void *context,const char *symbol);
void ws_jni_vm_exports(ws_jni_vm *vm,ws_jni_export_resolver resolver,void *context);
/* Adapter callback form of the native registry lookup. */
uintptr_t ws_jni_vm_lookup(void *vm, const char *class_name,
                          const char *name, const char *signature);
/* Detaches caller; refuses to free a VM while another thread is attached. */
int ws_jni_vm_destroy(ws_jni_vm *vm);
/* Caller must stop/join producers before teardown; detects already-attached
 * foreign threads without discarding live adapter state on a failed destroy. */
int ws_jni_vm_exclusive(ws_jni_vm *vm);
#endif
