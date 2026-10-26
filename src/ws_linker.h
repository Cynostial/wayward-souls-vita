#ifndef WS_LINKER_H
#define WS_LINKER_H
#include <stddef.h>
#include <stdint.h>
#include "so_util.h"

#define WS_MAX_MODULES 32
typedef struct { const char *filename; so_module *image; } ws_module;
typedef struct {
    ws_module *modules;
    size_t module_count;
    const so_default_dynlib *platform;
    size_t platform_count;
    void (*error)(const char *module, const char *symbol, unsigned relocation);
} ws_link_context;

/* Apportable libraries have no DT_SONAME and contain build-machine paths in
 * DT_NEEDED. Library identity therefore comes from the validated APK filename. */
int ws_module_index(const ws_link_context *, const char *needed);
/* Modules must be in the manifest's Java load order and mapped before calling
 * this. Dependencies are visible even when later in that order; unrelated
 * later libraries are not visible to earlier relocations. On validation failure no
 * relocation is written. Missing strong imports are never replaced by stubs. */
int ws_link_all(const ws_link_context *);
#endif
