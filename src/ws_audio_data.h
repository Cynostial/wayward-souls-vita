#ifndef WS_AUDIO_DATA_H
#define WS_AUDIO_DATA_H
#include <stddef.h>
#include <stdint.h>

#define WS_AUDIO_STREAM_LIMIT 2
#define WS_AUDIO_CHUNK_FRAMES 4096
#define WS_EFFECT_BUDGET (8u*1024u*1024u)
typedef struct ws_audio_stream ws_audio_stream;
typedef struct {
    int16_t *samples;
    size_t frames, bytes;
    unsigned channels, rate;
} ws_audio_effect;

/* Filename extensions are deliberately ignored: all 80 inspected sound and
 * music assets contain Ogg Vorbis, including the .caf and .mp3 names. */
ws_audio_stream *ws_audio_stream_open(const char *path);
unsigned ws_audio_stream_channels(const ws_audio_stream *stream);
unsigned ws_audio_stream_rate(const ws_audio_stream *stream);
/* Read <=4096 frames into a caller-owned buffer. Returns frames, 0 at EOF,
 * or -1 on a decode error. Call from the audio worker, never the render loop.
 * A stream has one owner; only distinct streams may be accessed concurrently. */
int ws_audio_stream_read(ws_audio_stream *stream,int16_t *out,size_t frames,int loop);
void ws_audio_stream_close(ws_audio_stream *stream);
int ws_audio_effect_load(const char *path,ws_audio_effect *effect);
void ws_audio_effect_release(ws_audio_effect *effect);
size_t ws_audio_effect_bytes(void);
#endif
