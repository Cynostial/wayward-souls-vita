#define _GNU_SOURCE
#include "ws_platform.h"
#include "ws_libc.h"
#include "ws_io.h"
#include "ws_memory.h"
#include "ws_mmap.h"
#include "ws_stdio.h"
#include "ws_sync.h"
#include "ws_threads.h"
#include "ws_futex.h"
#include "ws_events.h"
#include "ws_time.h"
#include "ws_env.h"
#include "ws_paths.h"
#include "ws_system.h"
#include "ws_signals.h"
#include "ws_graphics.h"
#include "ws_bitmap.h"
#include "ws_inet.h"
#include "ws_network.h"
#include "ws_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <wctype.h>
#include <sched.h>

static ws_loader *images;
static unsigned visible;
static int initialized;
static int page_size=4096;
static uintptr_t stack_guard=0x6af1b39d;
static _Thread_local char dl_error[256];
static uintptr_t global_handle=0x5753444c;
static uintptr_t library_symbol(so_module *m,const char *name) {
    for(unsigned i=0;i<m->num_dynsym;++i) {
        Elf32_Sym *s=&m->dynsym[i];
        if(s->st_shndx!=SHN_UNDEF&&ELF32_ST_BIND(s->st_info)!=STB_LOCAL&&!strcmp(m->dynstr+s->st_name,name))
            return s->st_value+(s->st_shndx==SHN_ABS?0:m->load_addr);
    }
    return 0;
}
static void *b_dlopen(const char *path,int mode) {
    (void)mode;
    if(!path)return &global_handle;
    if(!images)return NULL;
    ws_link_context ctx={images->modules,visible,NULL,0,NULL};int i=ws_module_index(&ctx,path);
    if(i>=0)return &images->images[i];
    snprintf(dl_error,sizeof(dl_error),"Library unavailable: %s",path);return NULL;
}
static void *b_dlsym(void *handle,const char *name) {
    if(!name)return NULL;
    if(!handle||handle==&global_handle||(uintptr_t)handle==UINT32_MAX) {
        uintptr_t p=ws_platform_symbol(name);if(p)return (void *)p;
        if(images)for(unsigned i=0;i<visible;++i){p=library_symbol(&images->images[i],name);if(p)return (void *)p;}
    } else if(images)for(unsigned i=0;i<visible;++i)if(handle==&images->images[i]) {
        /* A library handle includes its dependency closure. Android searches
         * breadth first; looking only in the handle's own exports loses symbols
         * supplied by Foundation/CoreFoundation to their dependents. */
        unsigned queue[WS_MAX_MODULES],head=0,tail=1;uint32_t seen=1u<<i;queue[0]=i;
        ws_link_context context={images->modules,visible,NULL,0,NULL};
        while(head<tail){
            so_module *module=&images->images[queue[head++]];
            uintptr_t p=library_symbol(module,name);if(p)return (void *)p;
            for(unsigned d=0;d<module->num_dynamic;++d)if(module->dynamic[d].d_tag==DT_NEEDED){
                int next=ws_module_index(&context,module->dynstr+module->dynamic[d].d_un.d_val);
                if(next>=0&&!(seen&(1u<<(unsigned)next))){seen|=1u<<(unsigned)next;queue[tail++]=(unsigned)next;}
            }
        }
        break;
    }
    snprintf(dl_error,sizeof(dl_error),"Symbol unavailable: %s",name);return NULL;
}
static int b_dlclose(void *handle) {
    if(handle==&global_handle)return 0;
    if(images)for(unsigned i=0;i<visible;++i)if(handle==&images->images[i])return 0;
    snprintf(dl_error,sizeof(dl_error),"Invalid library handle");return -1;
}
static char *b_dlerror(void){static _Thread_local char result[256];if(!*dl_error)return NULL;memcpy(result,dl_error,sizeof(result));*dl_error=0;return result;}
typedef struct {const char *filename;void *base;const char *symbol;void *address;} dl_info;
static int b_dladdr(const void *pointer,dl_info *out) {
    if(!out||!images)return 0;
    uintptr_t p=(uintptr_t)pointer;
    for(unsigned i=0;i<visible;++i) {
        so_module *m=&images->images[i];int contains=p>=m->text_base&&p-m->text_base<m->text_size;
        for(int j=0;!contains&&j<m->n_data;++j)contains=p>=m->data_base[j]&&p-m->data_base[j]<m->data_size[j];
        if(!contains)continue;
        *out=(dl_info){images->modules[i].filename,(void *)m->load_addr,NULL,NULL};uintptr_t nearest=0;
        for(unsigned n=0;n<m->num_dynsym;++n){Elf32_Sym *s=&m->dynsym[n];uintptr_t a=m->load_addr+s->st_value;if(s->st_shndx!=SHN_UNDEF&&s->st_value&&a<=p&&a>=nearest){nearest=a;out->symbol=m->dynstr+s->st_name;out->address=(void *)a;}}
        return 1;
    }
    return 0;
}
static int b_log_vprint(int priority,const char *tag,const char *fmt,va_list a) {
    return ws_log_vprint(priority,tag,fmt,a);
}
static int b_log_print(int priority,const char *tag,const char *fmt,...) {
    va_list a;va_start(a,fmt);int n=b_log_vprint(priority,tag,fmt,a);va_end(a);return n;
}
static int b_log_write(int priority,const char *tag,const char *text){return b_log_print(priority,tag,"%s",text);}
static int b_property(const char *name,char *out) {
    if(!name||!out)return 0;
    const char *value=!strcmp(name,"ro.product.cpu.abi")?"armeabi-v7a":!strcmp(name,"ro.build.version.sdk")?"17":!strcmp(name,"ro.product.model")?"PS Vita":"";
    strcpy(out,value);return (int)strlen(value);
}
static void b_stack_fail(void){fputs("Wayward: stack protector failed\n",stderr);abort();}
typedef struct {uint32_t current,maximum;} limit32;
static int b_getrlimit(int resource,limit32 *out) {
    if(!out){*ws_bionic_errno_location()=14;return -1;}
    if(resource==7){*out=(limit32){1024,1024};return 0;}
    if(resource==3){ws_thread_identity identity;if(ws_bionic_current_thread(&identity))return -1;*out=(limit32){(uint32_t)identity.stack_size,(uint32_t)identity.stack_size};return 0;}
    *ws_bionic_errno_location()=95;return -1;
}
static int b_setrlimit(int resource,const limit32 *value){
    if(!value){*ws_bionic_errno_location()=14;return -1;}
    if(value->current>value->maximum){*ws_bionic_errno_location()=22;return -1;}
    limit32 current;if(b_getrlimit(resource,&current))return -1;
    if(value->current==current.current&&value->maximum==current.maximum)return 0;
    /* Descriptor tables and pthread stack budgets are fixed when initialized. */
    *ws_bionic_errno_location()=95;return -1;
}
static int b_fcntl(int fd,int command,...) {
    va_list a;va_start(a,command);int value=(command==2||command==4||command==0)?va_arg(a,int):0;va_end(a);
    if(ws_event_owned(fd))return ws_event_fcntl(fd,command,value);
#ifdef _WIN32
    /* Only the native-fd backend is unavailable to the Windows catalog test. */
    *ws_bionic_errno_location()=95;return -1;
#else
    int r=-1;
    if(command==1)r=fcntl(fd,F_GETFD);
    else if(command==2)r=fcntl(fd,F_SETFD,value&1?FD_CLOEXEC:0);
    else if(command==0)r=fcntl(fd,F_DUPFD,value);
    else if(command==3){r=fcntl(fd,F_GETFL);if(r>=0)r=(r&O_APPEND?0x400:0)|(r&O_NONBLOCK?0x800:0)|(r&O_ACCMODE);}
    else if(command==4) {
        if(value&~0xc03){*ws_bionic_errno_location()=95;return -1;}
        int old=fcntl(fd,F_GETFL);if(old>=0)r=fcntl(fd,F_SETFL,(old&~(O_APPEND|O_NONBLOCK))|(value&0x400?O_APPEND:0)|(value&0x800?O_NONBLOCK:0));
    } else {*ws_bionic_errno_location()=95;return -1;}
    if(r<0)ws_errno_publish();
    return r;
#endif
}
static uint64_t random_state=UINT64_C(0x1234abcd330e);
static pthread_mutex_t random_lock=PTHREAD_MUTEX_INITIALIZER;
static void b_srand48(int32_t seed){pthread_mutex_lock(&random_lock);random_state=((uint64_t)(uint32_t)seed<<16)|0x330e;pthread_mutex_unlock(&random_lock);}
static int32_t b_lrand48(void){pthread_mutex_lock(&random_lock);random_state=(random_state*UINT64_C(0x5deece66d)+11)&UINT64_C(0xffffffffffff);int32_t r=(int32_t)(random_state>>17);pthread_mutex_unlock(&random_lock);return r;}
static ws_tm *b_localtime_r(const int32_t *stamp,ws_tm *out){ws_tm *p=ws_b_localtime(stamp);if(p&&out)*out=*p;return p?out:NULL;}
static int b_isinf(double x){return isinf(x)!=0;}
static int b_isinff(float x){return isinf(x)!=0;}
static int b_isnanf(float x){return isnan(x)!=0;}
static size_t b_strlcat(char *out,const char *in,size_t n){size_t a=strnlen(out,n),b=strlen(in);if(a<n){size_t c=b<n-a-1?b:n-a-1;memcpy(out+a,in,c);out[a+c]=0;}return a+b;}

uintptr_t ws_platform_symbol(const char *name) {
    uintptr_t p;
    if((p=ws_bionic_io_symbol(name))||(p=ws_bionic_sync_symbol(name))||(p=ws_bionic_threads_symbol(name))||
       (p=ws_events_symbol(name))||(p=ws_mmap_symbol(name))||(p=ws_env_symbol(name))||(p=ws_time_symbol(name))||(p=ws_inet_symbol(name))||(p=ws_network_symbol(name))||(p=ws_filesystem_symbol(name))||(p=ws_system_symbol(name))||(p=ws_signals_symbol(name))||(p=ws_libc_resolve(name))||(p=ws_bitmap_symbol(name))||(p=ws_graphics_symbol(name)))return p;
#define B(n,f) if(!strcmp(name,n))return (uintptr_t)(f)
#define D(n) B(#n,n)
#define T(n) B(#n,ws_b_##n)
    B("malloc",ws_malloc);B("free",ws_free);B("calloc",ws_calloc);B("realloc",ws_realloc);B("strdup",ws_strdup);
    B("__futex_wait",ws_futex_wait);B("__futex_wake",ws_futex_wake);
    B("__page_size",&page_size);B("__stack_chk_guard",&stack_guard);B("__stack_chk_fail",b_stack_fail);
    B("__android_log_print",b_log_print);B("__android_log_vprint",b_log_vprint);B("__android_log_write",b_log_write);
    B("__system_property_get",b_property);B("getrlimit",b_getrlimit);B("setrlimit",b_setrlimit);B("fcntl",b_fcntl);
    B("dlopen",b_dlopen);B("dlclose",b_dlclose);B("dlsym",b_dlsym);B("dlerror",b_dlerror);B("dladdr",b_dladdr);
    T(clock_gettime);T(gettimeofday);T(nanosleep);T(usleep);T(sleep);T(clock);T(time);T(gmtime);T(localtime);T(mktime);T(strftime);
    B("localtime_r",b_localtime_r);B("__isinf",b_isinf);B("__isinff",b_isinff);B("isnanf",b_isnanf);B("strlcat",b_strlcat);
    D(exp2f);D(fmaxf);D(fminf);D(lround);D(lroundf);D(roundf);D(strcspn);D(strspn);D(strnlen);
    D(iswalnum);D(iswdigit);D(iswspace);D(iswxdigit);D(towlower);D(towupper);
    B("lrand48",b_lrand48);B("srand48",b_srand48);D(sched_yield);D(abort);D(exit);B("_exit",_exit);
#undef T
#undef D
#undef B
    return 0;
}
typedef struct {const char *name;int weak;} required;
static const required requirements[]={
#include "ws_platform_names.inc"
};
static so_default_dynlib bindings[sizeof(requirements)/sizeof(*requirements)];
static size_t binding_count;
int ws_platform_init(ws_loader *loader,void *arena,size_t bytes) {
    if(initialized||!loader)return -1;
    if(ws_memory_init(arena,bytes)||ws_libc_install(ws_bionic_errno_location)||ws_stdio_init())return -1;
    if(!ws_bionic_pthread_self())return -1;
    images=loader;visible=0;
    for(size_t i=0;i<sizeof(requirements)/sizeof(*requirements);++i){uintptr_t p=ws_platform_symbol(requirements[i].name);if(p)bindings[binding_count++]=(so_default_dynlib){requirements[i].name,p};}
    initialized=1;return 0;
}
void ws_platform_visible(unsigned count){visible=images&&count<=images->count?count:0;}
const so_default_dynlib *ws_platform_bindings(size_t *count){if(count)*count=binding_count;return bindings;}
unsigned ws_platform_missing(void (*report)(const char *)) {
    unsigned missing=0;
    for(size_t i=0;i<sizeof(requirements)/sizeof(*requirements);++i)if(!requirements[i].weak&&!ws_platform_symbol(requirements[i].name)){++missing;if(report)report(requirements[i].name);}
    return missing;
}
