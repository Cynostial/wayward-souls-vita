#include "ws_jni_descriptor.h"
#include <string.h>

static const char *type(const char *s, char *kind, int allow_void) {
    unsigned dimensions = 0;
    while (*s == '[') { if (++dimensions > 255) return NULL; ++s; }
    if (strchr("ZBCSIJFD", *s) && *s) {
        *kind = dimensions ? 'L' : *s;
        return s + 1;
    }
    if (*s == 'V' && !dimensions && allow_void) { *kind = 'V'; return s + 1; }
    if (*s != 'L') return NULL;
    const char *start = ++s;
    while (*s && *s != ';') {
        if (*s == '.' || *s == '[' || *s == '(' || *s == ')') return NULL;
        ++s;
    }
    if (*s != ';' || s == start) return NULL;
    *kind = 'L';
    return s + 1;
}
int ws_jni_parse_descriptor(const char *text, ws_jni_descriptor *out) {
    /* SDK methods can exceed 512 bytes while remaining far below Java's
     * 255 argument-slot limit (e.g. Apache HTTP's request director factory). */
    if (!text || !out || *text++ != '(' || strlen(text) > 8191) return -1;
    ws_jni_descriptor d = {0};
    unsigned slots = 0;
    while (*text && *text != ')') {
        if (d.count == 255) return -1;
        text = type(text, &d.arguments[d.count], 0);
        if (!text) return -1;
        char k = d.arguments[d.count++];
        slots += (k == 'J' || k == 'D') ? 2 : 1;
        if (slots > 255) return -1;
    }
    if (*text++ != ')') return -1;
    text = type(text, &d.result, 1);
    if (!text || *text) return -1;
    *out = d;
    return 0;
}
