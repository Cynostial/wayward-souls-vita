#ifndef WS_LOADER_H
#define WS_LOADER_H
#include "ws_linker.h"
#include "ws_layout.h"
enum ws_load_phase { WS_EMPTY,WS_MAPPED,WS_LINKED,WS_INITIALIZING,WS_READY,WS_FAILED };
typedef struct {
    so_module images[WS_IMAGE_COUNT];
    ws_module modules[WS_IMAGE_COUNT];
    unsigned count;
    enum ws_load_phase phase;
} ws_loader;
/* Input is prepared.json's version_dir, containing the lib directory. The object must
 * initially be zeroed. Validates each image before mapping it; rolls back all
 * mappings on failure, before any constructors or game code may execute. */
int ws_loader_map(ws_loader *loader,const char *version_dir);
int ws_loader_link(ws_loader *loader,const so_default_dynlib *platform,size_t count,
                   void (*error)(const char *,const char *,unsigned));
/* before=1 exposes the current image and installs hooks before constructors;
 * before=0 performs its JNI/environment step, matching System.loadLibrary. */
typedef int (*ws_load_callback)(void *context,unsigned index,int before);
int ws_loader_initialize(ws_loader *loader,ws_load_callback callback,void *context);
so_module *ws_loader_module(ws_loader *loader,const char *filename);
uintptr_t ws_loader_find_exidx(const ws_loader *loader,uintptr_t pc,int *count);
/* Only legal before any constructors have run. Running images have process
 * lifetime: unloading them while an ObjC object/thread exists is unsafe. */
int ws_loader_discard(ws_loader *loader);
#endif
