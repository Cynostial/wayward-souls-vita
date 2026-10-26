#include "ws_inet.h"
#include "ws_io.h"
#include "../vendor/aosp-inet/compat.h"
int ws_inet_pton(int family,const char *text,void *address) {
    if(!text||!address){*ws_bionic_errno_location()=14;return -1;}
    int r=ws_aosp_inet_pton(family,text,address);if(r<0)ws_errno_publish();return r;
}
const char *ws_inet_ntop(int family,const void *address,char *text,size_t size) {
    if(!text||!address){*ws_bionic_errno_location()=14;return NULL;}
    const char *r=ws_aosp_inet_ntop(family,address,text,size);if(!r)ws_errno_publish();return r;
}
char *ws_inet_ntoa(uint32_t address) {
    static _Thread_local char text[16];
    return (char *)ws_inet_ntop(2,&address,text,sizeof(text));
}
unsigned ws_inet_nsap_addr(const char *text,unsigned char *address,int size) {
    if(!text||!address){*ws_bionic_errno_location()=14;return 0;}
    if(size<0){*ws_bionic_errno_location()=22;return 0;}
    return ws_aosp_inet_nsap_addr(text,address,size);
}
char *ws_inet_nsap_ntoa(int size,const unsigned char *address,char *text) {
    if(!address){*ws_bionic_errno_location()=14;return NULL;}
    if(size<0){*ws_bionic_errno_location()=22;return NULL;}
    return ws_aosp_inet_nsap_ntoa(size,address,text);
}
uintptr_t ws_inet_symbol(const char *name) {
#define B(n) if(!strcmp(name,#n))return (uintptr_t)ws_##n
    B(inet_pton);B(inet_ntop);B(inet_ntoa);B(inet_nsap_addr);B(inet_nsap_ntoa);
#undef B
    return 0;
}
