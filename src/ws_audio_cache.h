#ifndef WS_AUDIO_CACHE_H
#define WS_AUDIO_CACHE_H
#include "ws_audio_data.h"
/* Lifecycle calls are serialized by the game owner. PCM is shared read-only;
 * every acquired effect must be released before stop can free the cache. */
int ws_audio_cache_start(const char *assets,int warm);
int ws_audio_cache_load(const char *,ws_audio_effect *);
void ws_audio_cache_release(ws_audio_effect *);
int ws_audio_cache_stop(void);
#endif
