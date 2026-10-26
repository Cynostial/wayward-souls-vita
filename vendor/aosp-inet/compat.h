#ifndef WS_AOSP_INET_COMPAT_H
#define WS_AOSP_INET_COMPAT_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <assert.h>
#define ANDROID_CHANGES 1
/* Android address families, irrespective of the host's socket ABI. */
#define AF_INET 2
#define AF_INET6 10
#define NS_INADDRSZ 4
#define NS_IN6ADDRSZ 16
#define NS_INT16SZ 2
#define IN6ADDRSZ NS_IN6ADDRSZ
#define INT16SZ NS_INT16SZ
typedef unsigned char u_char;
typedef unsigned int u_int;
typedef uint32_t u_int32_t;
static inline uint32_t ws_inet_htonl(uint32_t n) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap32(n);
#else
    return n;
#endif
}
#define htonl ws_inet_htonl
static inline size_t ws_inet_strlcpy(char *to,const char *from,size_t size) {
    size_t length=strlen(from);
    if(size){size_t n=length<size-1?length:size-1;memcpy(to,from,n);to[n]=0;}
    return length;
}
#define strlcpy ws_inet_strlcpy
#ifndef isascii
#define isascii(c) ((unsigned)(c)<128)
#endif
int ws_aosp_inet_pton(int,const char *,void *);
const char *ws_aosp_inet_ntop(int,const void *,char *,size_t);
unsigned ws_aosp_inet_nsap_addr(const char *,unsigned char *,int);
char *ws_aosp_inet_nsap_ntoa(int,const unsigned char *,char *);
#endif
