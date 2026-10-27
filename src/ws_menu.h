#pragma once
#include "so_util.h"
int ws_menu_bind(so_module *,so_module *);
int ws_menu_install(void);
void ws_menu_frame(uintptr_t manager,int gameplay);
int ws_menu_title(void);
int ws_menu_targeting(void);
void ws_menu_aim(void *,float,float,float);
void ws_menu_navigate(void *,int);
void ws_menu_confirm(void *);
void ws_menu_draw(void);
int ws_menu_quick_items(void);
void ws_menu_details(void *);
int ws_menu_close_details(void);
