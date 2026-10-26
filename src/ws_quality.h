#ifndef WS_QUALITY_H
#define WS_QUALITY_H
#include "so_util.h"
int ws_quality_attach(so_module *game,so_module *objc,so_module *sqlite);
void ws_quality_present(void);
const char *ws_tutorial_text(const char *text);
#endif
