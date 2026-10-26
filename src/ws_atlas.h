#pragma once
#include "so_util.h"
int ws_atlas_inset(float out[8],const float uv[8],int width,int height);
typedef struct {unsigned hits,misses,quads,fallbacks;} ws_atlas_stats;
const float *ws_atlas_cached(const float uv[8],int width,int height);
void ws_atlas_counters(ws_atlas_stats *out);
int ws_atlas_attach(so_module *image,so_module *objc);
const void *ws_atlas_texcoords(int size,unsigned type,int stride,const void *coordinates);
