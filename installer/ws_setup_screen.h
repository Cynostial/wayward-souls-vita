#pragma once
#include <stdint.h>
void ws_setup_screen_art(const uint32_t *);
/* 0: instructions/error, 1: progress, 2: ready, 3: prerequisite error. */
void ws_setup_screen(uint32_t *,const char *,unsigned,unsigned,int);
