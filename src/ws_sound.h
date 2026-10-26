#ifndef WS_SOUND_H
#define WS_SOUND_H
#include "ws_audio_output.h"
#include "so_util.h"
/* Configure on the game thread before SoundEngine_Initialize. The path is
 * the pinned preparation's native assets directory. Lifecycle operations
 * (configure/initialize/teardown) are serialized by the game thread. */
int ws_sound_configure(const char *,const ws_audio_sink *);
uintptr_t ws_sound_symbol(const char *);
int ws_sound_attach(so_module *);
int ws_sound_shutdown(void);
#endif
