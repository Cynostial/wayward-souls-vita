#ifndef WS_BOOTSTRAP_H
#define WS_BOOTSTRAP_H
#include "ws_loader.h"
#include "ws_java.h"
/* Library initialization only. READY means constructors/JNI completed, not
 * gameplay. Call on the game thread (at least the APK's 5 MiB stack). */
typedef struct {ws_loader *loader;ws_java *java;const char *apk_path,*asset_directory;unsigned jni_count,graphics_hooks,visible_images;} ws_bootstrap;
int ws_bootstrap_initialize(ws_bootstrap *);
#endif
