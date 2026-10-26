#pragma once
#include "so_util.h"
enum {WS_LIFE_INTERRUPT=1,WS_LIFE_RESUME=2};
int ws_lifecycle_attach(so_module *game,so_module *objc);
int ws_lifecycle_signal(unsigned events,int system_overlay);
int ws_lifecycle_poll(void);
int ws_lifecycle_input_blocked(void);
void ws_lifecycle_discard_checkpoint(void);
void ws_lifecycle_request_checkpoint(int begin);
uintptr_t ws_lifecycle_draw_hook(void);
