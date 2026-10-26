#include "ws_jni_registry.h"
#include <stdlib.h>
#include <string.h>

/* Bounded copied names avoid references to temporary Java class objects. */
#define WS_CLASS_MAX 255
#define WS_NAME_MAX 127
#define WS_SIGNATURE_MAX 511

typedef struct entry {
    char class_name[WS_CLASS_MAX + 1];
    char name[WS_NAME_MAX + 1];
    char signature[WS_SIGNATURE_MAX + 1];
    uintptr_t address;
} entry;

struct ws_jni_registry {
    size_t count;
    size_t capacity;
    entry *entries;
};

static int bounded_string(const char *s, size_t maximum) {
    if (!s || !*s) return 0;
    for (size_t i = 0; i <= maximum; ++i) if (!s[i]) return 1;
    return 0;
}

static const char *type_end(const char *s, int allow_void) {
    if (*s == 'V') return allow_void ? s + 1 : NULL;
    unsigned dimensions = 0;
    while (*s == '[') {
        if (++dimensions > 255) return NULL;
        ++s;
    }
    if (*s && strchr("BCDFIJSZ", *s)) return s + 1;
    if (*s++ != 'L') return NULL;
    const char *start = s;
    while (*s && *s != ';') {
        if (*s == '[' || *s == '(' || *s == ')' || *s == '.') return NULL;
        ++s;
    }
    return *s == ';' && s != start ? s + 1 : NULL;
}

static int valid_signature(const char *signature) {
    if (!bounded_string(signature, WS_SIGNATURE_MAX) || *signature != '(') return 0;
    const char *p = signature + 1;
    while (*p && *p != ')') {
        p = type_end(p, 0);
        if (!p) return 0;
    }
    if (*p++ != ')') return 0;
    p = type_end(p, 1);
    return p && !*p;
}

static entry *find_entry(const ws_jni_registry *r, const char *c,
                          const char *name, const char *signature) {
    for (size_t i = 0; i < r->count; ++i) {
        entry *e = &r->entries[i];
        if (!strcmp(e->class_name, c) && !strcmp(e->name, name) &&
            !strcmp(e->signature, signature)) return e;
    }
    return NULL;
}

ws_jni_registry *ws_jni_registry_create(size_t capacity) {
    if (!capacity || capacity > SIZE_MAX / sizeof(entry)) return NULL;
    ws_jni_registry *r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->entries = calloc(capacity, sizeof(entry));
    if (!r->entries) { free(r); return NULL; }
    r->capacity = capacity;
    return r;
}

void ws_jni_registry_destroy(ws_jni_registry *r) {
    if (r) { free(r->entries); free(r); }
}

int ws_jni_register(ws_jni_registry *r, const char *c,
                     const ws_jni_method *methods, size_t count) {
    if (!r || !bounded_string(c, WS_CLASS_MAX) || (!methods && count))
        return WS_JNI_INVALID;
    if (count > r->capacity) return WS_JNI_CAPACITY;
    size_t additional = 0;
    /* Validate the entire batch before changing any registration. */
    for (size_t i = 0; i < count; ++i) {
        const ws_jni_method *m = &methods[i];
        if (!bounded_string(m->name, WS_NAME_MAX) || !valid_signature(m->signature) ||
            !m->address) return WS_JNI_INVALID;
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(m->name, methods[j].name) && !strcmp(m->signature, methods[j].signature))
                return WS_JNI_INVALID;
        if (!find_entry(r, c, m->name, m->signature)) ++additional;
    }
    if (additional > r->capacity - r->count) return WS_JNI_CAPACITY;
    for (size_t i = 0; i < count; ++i) {
        const ws_jni_method *m = &methods[i];
        entry *e = find_entry(r, c, m->name, m->signature);
        if (!e) {
            e = &r->entries[r->count++];
            strcpy(e->class_name, c);
            strcpy(e->name, m->name);
            strcpy(e->signature, m->signature);
        }
        e->address = m->address; /* Keep the ARM/Thumb bit exactly as registered. */
    }
    return WS_JNI_OK;
}

uintptr_t ws_jni_lookup(const ws_jni_registry *r, const char *c,
                          const char *name, const char *signature) {
    if (!r || !c || !name || !signature) return 0;
    entry *e = find_entry(r, c, name, signature);
    return e ? e->address : 0;
}

size_t ws_jni_unregister(ws_jni_registry *r, const char *c) {
    if (!r || !c) return 0;
    size_t old_count = r->count, keep = 0;
    for (size_t i = 0; i < old_count; ++i)
        if (strcmp(r->entries[i].class_name, c)) r->entries[keep++] = r->entries[i];
    r->count = keep;
    return old_count - keep;
}

size_t ws_jni_count(const ws_jni_registry *r) { return r ? r->count : 0; }
