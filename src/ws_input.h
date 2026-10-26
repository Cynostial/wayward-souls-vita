#ifndef WS_INPUT_H
#define WS_INPUT_H
#include "so_util.h"
int ws_input_attach(so_module *game,so_module *objc);
/* Called on the render thread immediately before presentation. */
void ws_input_frame(void);
#endif
