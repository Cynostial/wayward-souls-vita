#pragma once
#include <stddef.h>
/* Expand the original ItemInfo description tokens, preserving their meaning. */
void ws_item_text(char *out,size_t capacity,const char *description,const char *stats,
                  const float values[3],int quantity,int ammo,int tier);
