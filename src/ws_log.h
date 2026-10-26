#pragma once
#include <stddef.h>
#include <stdarg.h>
typedef void (*ws_log_sink)(const char *,size_t,void *);
int ws_log_start(ws_log_sink sink,void *context);
void ws_log_stop(void);
int ws_log_vprint(int priority,const char *tag,const char *format,va_list args);
int ws_log_print(int priority,const char *tag,const char *format,...);
unsigned ws_log_dropped(void);
