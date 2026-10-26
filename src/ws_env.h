#ifndef WS_ENV_H
#define WS_ENV_H
#include <stdint.h>
char *ws_getenv(const char *);
int ws_setenv(const char *,const char *,int);
int ws_unsetenv(const char *);
uintptr_t ws_env_symbol(const char *);
#endif
