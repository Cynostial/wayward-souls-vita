#ifndef WS_INET_H
#define WS_INET_H
#include <stdint.h>
#include <stddef.h>
int ws_inet_pton(int,const char *,void *);
const char *ws_inet_ntop(int,const void *,char *,size_t);
char *ws_inet_ntoa(uint32_t);
unsigned ws_inet_nsap_addr(const char *,unsigned char *,int);
char *ws_inet_nsap_ntoa(int,const unsigned char *,char *);
uintptr_t ws_inet_symbol(const char *);
#endif
