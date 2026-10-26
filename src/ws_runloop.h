#ifndef WS_RUNLOOP_H
#define WS_RUNLOOP_H
#include "ws_java.h"
int ws_runloop_init(ws_java *java);
/* The launcher calls start from its >=5 MiB game/render thread, replacing the
 * Java-created MainThread. Callbacks always execute on that same thread. */
int ws_runloop_start(ws_java *java);
void ws_runloop_native_entry(ws_java *java,uintptr_t entry);
int ws_runloop_tick(ws_java *java);
int ws_runloop_destroy(ws_java *java);
#endif
