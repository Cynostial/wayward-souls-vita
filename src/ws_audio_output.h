#ifndef WS_AUDIO_OUTPUT_H
#define WS_AUDIO_OUTPUT_H
#include <stddef.h>
#include <stdint.h>
#define WS_AUDIO_BLOCK_FRAMES 512u
typedef struct ws_audio_output ws_audio_output;
/* Called only by the output worker. Fill interleaved stereo PCM16 at 48 kHz;
 * return zero on success or a negative error to stop the worker. */
typedef int (*ws_audio_render)(void *,int16_t *,size_t);
typedef struct {
    void *context;
    int (*open)(void *,unsigned,unsigned);
    int (*write)(void *,int,const int16_t *);
    int (*close)(void *,int);
} ws_audio_sink;
/* NULL sink selects the actual Vita audio device. A supplied sink allows
 * deterministic worker/ownership tests without claiming hardware output. */
ws_audio_output *ws_audio_output_start(ws_audio_render,void *,const ws_audio_sink *);
void ws_audio_output_pause(ws_audio_output *,int);
int ws_audio_output_error(ws_audio_output *);
/* Joins the worker, drains and closes the device, then releases storage.
 * Must be called by the owner, never by the render callback itself. */
int ws_audio_output_stop(ws_audio_output *);
#endif
