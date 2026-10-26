#ifndef WS_FUTEX_H
#define WS_FUTEX_H
#include "ws_sync.h"
/* Bionic private futex helpers return a negative Linux errno directly. */
int ws_futex_wait(volatile int *,int,const ws_bionic_timespec *);
int ws_futex_wake(volatile int *,int);
#endif
