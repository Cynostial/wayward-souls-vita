#ifndef WS_BITMAP_H
#define WS_BITMAP_H
#include "ws_java.h"
#define WS_BITMAP_MAX_BYTES (16u*1024u*1024u)
#define WS_BITMAP_TOTAL_BYTES (32u*1024u*1024u)
typedef struct {uint32_t width,height,stride;int32_t format;uint32_t flags;} ws_bitmap_info;
_Static_assert(sizeof(ws_bitmap_info)==20,"Android BitmapInfo ARM ABI");
int ws_bitmap_services(ws_java *java);
int ws_android_bitmap_get_info(JNIEnv *,jobject,ws_bitmap_info *);
int ws_android_bitmap_lock(JNIEnv *,jobject,void **);
int ws_android_bitmap_unlock(JNIEnv *,jobject);
uintptr_t ws_bitmap_symbol(const char *name);
size_t ws_bitmap_bytes(void);
#endif
