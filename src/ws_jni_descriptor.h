#ifndef WS_JNI_DESCRIPTOR_H
#define WS_JNI_DESCRIPTOR_H
#include <stddef.h>
/* Normalize object and array arguments to L. JVM descriptors allow 255 slots. */
typedef struct ws_jni_descriptor {
    char arguments[255];
    size_t count;
    char result;
} ws_jni_descriptor;
int ws_jni_parse_descriptor(const char *text, ws_jni_descriptor *out);
#endif
