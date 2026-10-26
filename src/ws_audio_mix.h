#ifndef WS_AUDIO_MIX_H
#define WS_AUDIO_MIX_H
#include "ws_audio_data.h"
#define WS_AUDIO_OUTPUT_RATE 48000u
typedef struct {uint64_t position,step;int playing,loop;} ws_pcm_cursor;
/* Immutable PCM is borrowed until the voice stops. The audio owner serializes
 * cursor changes. No allocation, file access or decoder work occurs here. */
int ws_pcm_start(ws_pcm_cursor *,unsigned source_rate,float pitch,int loop);
int ws_pcm_pitch(ws_pcm_cursor *,unsigned source_rate,float pitch);
int ws_pcm_mix(const ws_audio_effect *,ws_pcm_cursor *,int32_t *stereo,size_t frames,float left,float right);
void ws_pcm_finish(const int32_t *stereo,int16_t *output,size_t frames);
#endif
