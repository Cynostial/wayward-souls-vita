#pragma once
#include <stdint.h>
typedef struct {uintptr_t object,group;float x,y,w,h;unsigned index;int preferred;} ws_focus_item;
int ws_focus_initial(const ws_focus_item *,unsigned);
int ws_focus_next(const ws_focus_item *,unsigned,int,int);
