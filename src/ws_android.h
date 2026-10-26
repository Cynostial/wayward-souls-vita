#ifndef WS_ANDROID_H
#define WS_ANDROID_H
#include "ws_java.h"
int ws_android_init(ws_java *java);
void ws_android_destroy(ws_java *java);
int ws_android_files_init(ws_java *java);
jobject ws_android_file(JNIEnv *env,const char *path);
#endif
