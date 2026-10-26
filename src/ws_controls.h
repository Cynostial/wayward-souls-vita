#pragma once
#include <stdint.h>
enum {WS_UP=0x10,WS_RIGHT=0x20,WS_DOWN=0x40,WS_LEFT=0x80,WS_R=0x200,
      WS_TRIANGLE=0x1000,WS_CIRCLE=0x2000,WS_CROSS=0x4000,WS_SQUARE=0x8000,WS_START=8,WS_SELECT=1};
typedef struct {uint32_t buttons;unsigned char lx,ly;int gameplay;float seconds,width,height;int title,targeting;} ws_control_sample;
typedef struct {
    void *context;
    void (*move)(void *,float,float);
    void (*attack)(void *,int); /* 1 down, 2 held, 0 released, -1 cancelled */
    void (*ability)(void *,int);
    void (*back)(void *);
    void (*navigate)(void *,int); /* 1 up, 2 right, 3 down, 4 left */
    void (*confirm)(void *);
    void (*aim)(void *,float,float,float); /* stick axes and bounded seconds */
} ws_control_actions;
typedef struct {uint32_t buttons;int mode,attack,direction;float repeat;} ws_controls;
void ws_controls_step(ws_controls *,const ws_control_sample *,const ws_control_actions *);
