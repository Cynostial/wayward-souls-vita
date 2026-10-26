#ifndef WS_JAVA_H
#define WS_JAVA_H
#include "ws_jni_vm.h"
typedef struct {ws_jni_vm *vm;JavaVM *handle;jobject loader;void *runloop,*android,*preferences,*views,*news;} ws_java;
int ws_java_init(ws_java *);
JNIEnv *ws_java_env(ws_java *);
int ws_java_check(ws_java *,const char *where);
int ws_java_destroy(ws_java *);
#endif
