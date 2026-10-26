#include "ws_network.h"
#include "ws_inet.h"
#include "ws_io.h"
#include "ws_events.h"
#include "ws_memory.h"
#include "ws_system.h"
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
/* Android 4.2 uses BSD-positive EAI codes, unlike glibc's negatives.
 * Source: android-4.2.2_r1/libc/include/netdb.h, retained in research. */
enum {AGAIN=2,BADFLAGS=3,FAMILY=5,MEMORY=6,NONAME=8,SERVICE=9,SOCKTYPE=10,SYSTEM=11,OVERFLOW=14};
static int fail(int error){*ws_bionic_errno_location()=error;return -1;}
static int descriptor_error(int fd){
    if(ws_event_owned(fd)){
        int value=0;uint32_t size=4;int (*query)(int,int,int,void *,uint32_t *)=(void *)ws_events_symbol("getsockopt");
        return query(fd,1,3,&value,&size)?*ws_bionic_errno_location():0;
    }
    struct stat st;return fstat(fd,&st)?9:88;
}
static int socket_offline(int family,int type,int protocol){
    if(family!=1&&family!=2&&family!=10)return fail(97);
    if(type&~(0x80000|0x800|15))return fail(22);
    int base=type&15;if(base!=1&&base!=2)return fail(93);
    if(protocol&&protocol!=(base==1?6:17))return fail(93);
    return fail(family==1?95:100);
}
static int connect_offline(int fd,const void *address,uint32_t size){(void)address;(void)size;int e=descriptor_error(fd);return fail(e?e:106);}
static int bind_offline(int fd,const void *address,uint32_t size){(void)address;(void)size;int e=descriptor_error(fd);return fail(e?e:22);}
static int listen_offline(int fd,int backlog){(void)backlog;int e=descriptor_error(fd);return fail(e?e:95);}
static int accept_offline(int fd,void *address,uint32_t *size){(void)address;(void)size;int e=descriptor_error(fd);return fail(e?e:22);}
static unsigned if_nametoindex_offline(const char *name){fail(name?6:14);return 0;}
static char *if_indextoname_offline(unsigned index,char *name){(void)index;fail(name?6:14);return NULL;}
static int port_number(const char *service,int flags,int *port){
    *port=0;if(!service)return 0;
    if(!*service)return SERVICE;
    unsigned value=0;const char *p=service;
    for(;*p>='0'&&*p<='9';++p){value=value*10+(unsigned)(*p-'0');if(value>65535)return SERVICE;}
    if(!*p){*port=(int)value;return 0;}
    if(flags&8)return NONAME;
    if(!strcmp(service,"http"))*port=80;else if(!strcmp(service,"https"))*port=443;else return SERVICE;
    return 0;
}
void ws_network_free_addresses(ws_addrinfo *node){while(node){ws_addrinfo *next=node->next;ws_free(node->address);ws_free(node->canonical);ws_free(node);node=next;}}
static int get_addresses(const char *host,const char *service,const ws_addrinfo *hint,ws_addrinfo **out){
    if(!out){fail(14);return SYSTEM;}*out=NULL;
    ws_addrinfo zero={0};if(!hint)hint=&zero;
    if(hint->flags&~0x40f)return BADFLAGS;
    if(hint->family!=0&&hint->family!=2&&hint->family!=10)return FAMILY;
    if(hint->type<0||hint->type>2)return SOCKTYPE;
    if(hint->protocol!=0&&hint->protocol!=6&&hint->protocol!=17)return SERVICE;
    if((hint->type==1&&hint->protocol==17)||(hint->type==2&&hint->protocol==6))return SERVICE;
    if(!host&&!service)return NONAME;
    int port,error=port_number(service,hint->flags,&port);if(error)return error;
    int family=hint->family?hint->family:2;unsigned char address[16]={0};
    if(host){
        if(!strcmp(host,"localhost")&&!(hint->flags&4)){if(family==2){address[0]=127;address[3]=1;}else address[15]=1;}
        else if(ws_inet_pton(family,host,address)!=1){
            if(!hint->family&&ws_inet_pton(10,host,address)==1)family=10;
            else return hint->flags&4?NONAME:AGAIN;
        }
    }else if(!(hint->flags&1)){if(family==2){address[0]=127;address[3]=1;}else address[15]=1;}
    if(hint->flags&0x400)return NONAME; /* No external interfaces configured. */
    int first=hint->type?hint->type:hint->protocol==6?1:hint->protocol==17?2:1;
    int last=hint->type||hint->protocol?first:2;ws_addrinfo **tail=out;
    for(int type=first;type<=last;++type){
        ws_addrinfo *node=ws_calloc(1,sizeof(*node));if(!node)goto oom;
        *tail=node;tail=&node->next;node->flags=hint->flags;node->family=family;node->type=type;node->protocol=type==1?6:17;node->size=family==2?16:28;
        unsigned char *raw=ws_calloc(1,node->size);if(!raw)goto oom;node->address=raw;
        raw[0]=(unsigned char)family;raw[2]=(unsigned char)(port>>8);raw[3]=(unsigned char)port;memcpy(raw+(family==2?4:8),address,family==2?4:16);
        if((hint->flags&2)&&node==*out&&host){node->canonical=ws_strdup(host);if(!node->canonical)goto oom;}
    }
    return 0;
oom:ws_network_free_addresses(*out);*out=NULL;return MEMORY;
}
static int get_names(const void *address,uint32_t size,char *host,uint32_t host_size,char *service,uint32_t service_size,int flags){
    if(!address){fail(14);return SYSTEM;}
    if(flags&~0x3f)return BADFLAGS;
    if(size<2)return FAMILY;
    const unsigned char *raw=address;unsigned family=raw[0]|(unsigned)raw[1]<<8;
    if((family!=2&&family!=10)||size<(family==2?16u:28u))return FAMILY;
    if(!host&&!service)return NONAME;
    const unsigned char *binary=raw+(family==2?4:8);char numeric[64],port[8];
    if(host){
        int loopback=family==2?binary[0]==127:!memcmp(binary,"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\1",16);
        if(!(flags&2)&&loopback)strcpy(numeric,"localhost");
        else {if(flags&4)return AGAIN;if(!ws_inet_ntop((int)family,binary,numeric,sizeof(numeric)))return FAMILY;}
        if(strlen(numeric)>=host_size)return OVERFLOW;
    }
    if(service){snprintf(port,sizeof(port),"%u",(unsigned)raw[2]*256+raw[3]);if(strlen(port)>=service_size)return OVERFLOW;}
    if(host)strcpy(host,numeric);
    if(service)strcpy(service,port);
    return 0;
}
static void *host_failure(void){int *(*location)(void)=(void *)ws_system_symbol("__get_h_errno");*location()=2;fail(100);return NULL;}
static void *get_host_name(const char *name,int family){(void)name;(void)family;return host_failure();}
static void *get_host_address(const void *address,uint32_t size,int family){(void)address;(void)size;(void)family;return host_failure();}
uintptr_t ws_network_symbol(const char *name){
#define B(n,f) if(!strcmp(name,n))return (uintptr_t)f
    B("socket",socket_offline);B("connect",connect_offline);B("bind",bind_offline);B("listen",listen_offline);B("accept",accept_offline);
    B("getaddrinfo",get_addresses);B("getnameinfo",get_names);B("gethostbyname2",get_host_name);B("gethostbyaddr",get_host_address);
    B("if_nametoindex",if_nametoindex_offline);B("if_indextoname",if_indextoname_offline);
#undef B
    return 0;
}
