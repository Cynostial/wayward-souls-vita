#define _GNU_SOURCE
#include "ws_system.h"
#include "ws_events.h"
#include "ws_memory.h"
#include "ws_io.h"
#include "ws_threads.h"
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include "ws_time.h"
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/resource.h>
#endif
#ifdef __vita__
#include <kubridge.h>
#endif
static int fail(int android){*ws_bionic_errno_location()=android;return -1;}
/* ARM Linux syscall numbers, confirmed against the callers in libSystem.
 * These are guest descriptor operations; no host syscall number is reused. */
long ws_syscall(long number,...) {
    va_list a;va_start(a,number);long result;
    switch(number) {
    case 350:{int clock=va_arg(a,int),flags=va_arg(a,int);result=ws_timerfd_create(clock,flags);break;}
    case 351:{unsigned initial=va_arg(a,unsigned);result=ws_eventfd(initial,0);break;}
    case 353:{int fd=va_arg(a,int),flags=va_arg(a,int);const ws_itimerspec *value=va_arg(a,const ws_itimerspec *);ws_itimerspec *old=va_arg(a,ws_itimerspec *);result=ws_timerfd_settime(fd,flags,value,old);break;}
    case 354:{int fd=va_arg(a,int);ws_itimerspec *value=va_arg(a,ws_itimerspec *);result=ws_timerfd_gettime(fd,value);break;}
    case 356:{unsigned initial=va_arg(a,unsigned);int flags=va_arg(a,int);result=ws_eventfd(initial,flags);break;}
    case 224:{ws_thread_identity id;result=ws_bionic_current_thread(&id)?fail(3):(long)id.vita_uid;break;}
    default:fprintf(stderr,"Wayward: unsupported ARM syscall %ld\n",number);result=fail(38);break;
    }
    va_end(a);return result;
}
#define DESTRUCTORS 4096
typedef struct {void (*function)(void *);void *argument,*dso;uint64_t order;} destructor;
static destructor destructors[DESTRUCTORS];
static uint64_t sequence;
static pthread_mutex_t destructor_lock=PTHREAD_MUTEX_INITIALIZER;
int ws_cxa_atexit(void (*function)(void *),void *argument,void *dso) {
    if(!function)return fail(22);
    pthread_mutex_lock(&destructor_lock);int i;
    for(i=0;i<DESTRUCTORS;++i)if(!destructors[i].function)break;
    if(i<DESTRUCTORS)destructors[i]=(destructor){function,argument,dso,++sequence};
    pthread_mutex_unlock(&destructor_lock);return i==DESTRUCTORS?fail(12):0;
}
void ws_cxa_finalize(void *dso) {
    /* Remove before invoking: callbacks may recurse, register a destructor,
     * or finalize a second DSO. Never hold the registry lock in guest code. */
    for(;;){
        pthread_mutex_lock(&destructor_lock);int newest=-1;uint64_t order=0;
        for(int i=0;i<DESTRUCTORS;++i)if(destructors[i].function&&(!dso||destructors[i].dso==dso)&&destructors[i].order>order){newest=i;order=destructors[i].order;}
        destructor call={0};if(newest>=0){call=destructors[newest];memset(&destructors[newest],0,sizeof(call));}
        pthread_mutex_unlock(&destructor_lock);if(newest<0)break;call.function(call.argument);
    }
}
static int b_sysconf(int name) {
    ws_memory_stats m;
    switch(name) {
    case 0x06:return 100; /* Bionic clock tick units, distinct from clock() */
    case 0x0b:return 1024;
    case 0x1b:return 128;
    case 0x25:return DESTRUCTORS;
    case 0x26:return 1024;
    case 0x27:case 0x28:return 4096;
    case 0x4a:return 4;
    case 0x4b:return 256;
    case 0x4c:return 16384;
    case 0x4d:return 256;
    case 0x60:case 0x61:return 3; /* Vita application CPUs */
    case 0x62:m=ws_memory_get_stats();return (int)(m.capacity/4096);
    case 0x63:m=ws_memory_get_stats();return (int)((m.capacity-m.live_bytes)/4096);
    default:return fail(38);
    }
}
typedef struct {char system[65],node[65],release[65],version[65],machine[65],domain[65];} utsname32;
static int b_uname(utsname32 *name){if(!name)return fail(14);memset(name,0,sizeof(*name));strcpy(name->system,"PS Vita");strcpy(name->node,"vita");strcpy(name->release,"Wayward");strcpy(name->version,"1");strcpy(name->machine,"armv7l");return 0;}
static int b_gethostname(char *name,size_t bytes){if(!name)return fail(14);if(bytes<5)return fail(36);memcpy(name,"vita",5);return 0;}
static int b_cacheflush(uintptr_t start,uintptr_t end,int flags) {
    if(flags||end<start||end-start>INT32_MAX)return fail(22);
#ifdef __vita__
    if(end!=start)kuKernelFlushCaches((void *)start,(unsigned)(end-start));
#else
    (void)start;(void)end;
#endif
    return 0;
}
static int b_getpid(void){return getpid();}
/* The guest has a single unprivileged identity and no child processes. */
static int b_getuid(void){return 10000;}
static int b_getppid(void){return 0;}
static int b_issetugid(void){return 0;}
static int b_fork(void){return fail(38);}
static int b_execve(const char *p,char *const a[],char *const e[]){(void)p;(void)a;(void)e;return fail(38);}
static int b_execv(const char *p,char *const a[]){return b_execve(p,a,NULL);}
static int b_waitpid(int pid,int *status,int options){(void)pid;(void)status;(void)options;return fail(10);}
static _Thread_local int host_error;
static int *b_h_errno(void){return &host_error;}
static int b_priority(int policy){return policy==0?0:fail(95);}
static int b_initgroups(const char *name,unsigned group){(void)name;(void)group;return fail(1);}
typedef struct {ws_timeval user,system;int32_t counters[14];} rusage32;
_Static_assert(sizeof(rusage32)==72,"Android ARM rusage layout");
static int b_getrusage(int who,rusage32 *out){
    if(!out)return fail(14);
    if(who!=0&&who!=-1)return fail(22);
    rusage32 result={0};
    /* fork/exec are unavailable: this process can have no child usage. */
    if(who==0){
#ifdef _WIN32
        FILETIME created,exited,kernel,user;
        if(!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user))return fail(5);
        uint64_t u=((uint64_t)user.dwHighDateTime<<32)|user.dwLowDateTime,k=((uint64_t)kernel.dwHighDateTime<<32)|kernel.dwLowDateTime;
        if(u/10000000>INT32_MAX||k/10000000>INT32_MAX)return fail(75);
        result.user=(ws_timeval){(int32_t)(u/10000000),(int32_t)(u%10000000/10)};
        result.system=(ws_timeval){(int32_t)(k/10000000),(int32_t)(k%10000000/10)};
#else
        struct rusage native;if(getrusage(RUSAGE_SELF,&native)){ws_errno_publish();return -1;}
        if(native.ru_utime.tv_sec>INT32_MAX||native.ru_stime.tv_sec>INT32_MAX)return fail(75);
        result.user=(ws_timeval){native.ru_utime.tv_sec,native.ru_utime.tv_usec};
        result.system=(ws_timeval){native.ru_stime.tv_sec,native.ru_stime.tv_usec};
#endif
        /* VitaSDK reports CPU times only. Untracked kernel counters remain 0. */
    }
    *out=result;return 0;
}
/* Missing kernel facilities are explicit failures. libSystem's UUID generator
 * already falls back if a persistent clock file cannot be locked, and kqueue
 * rejects a vnode watch if inotify_init fails. Neither path gets fake events. */
static int b_inotify_init(void){return fail(38);}
static int b_inotify_add_watch(int fd,const char *path,uint32_t mask){(void)fd;(void)path;(void)mask;return fail(38);}
static int b_flock(int fd,int operation){
    int mode=operation&~4;if((operation&~15)||(mode!=1&&mode!=2&&mode!=8))return fail(22);
    struct stat status;if(fstat(fd,&status)){ws_errno_publish();return -1;}return fail(95);
}
static int b_fchdir(int fd){
    struct stat status;if(fstat(fd,&status)){ws_errno_publish();return -1;}
    if(!(status.st_mode&S_IFDIR))return fail(20);
    /* libv owns the guest cwd; changing the process cwd would escape its VFS. */
    return fail(95);
}
typedef struct {ws_timeval interval,value;} itimerval32;
static int b_getitimer(int which,itimerval32 *out){
    if(which<0||which>2)return fail(22);
    if(!out)return fail(14);
    return fail(38);
}
static int b_setitimer(int which,const itimerval32 *value,itimerval32 *old){
    (void)old;if(which<0||which>2)return fail(22);if(!value)return fail(14);
    if(value->interval.seconds<0||value->value.seconds<0||value->interval.microseconds<0||value->interval.microseconds>=1000000||value->value.microseconds<0||value->value.microseconds>=1000000)return fail(22);
    /* Asynchronous signal injection is not provided by the Vita signal layer. */
    return fail(38);
}
static _Thread_local char guest_thread_name[16]="Wayward";
static int b_prctl(int option,...){
    if(option!=15&&option!=16)return fail(22);
    va_list args;va_start(args,option);char *name=va_arg(args,char *);va_end(args);
    if(!name)return fail(14);
    if(option==16)memcpy(name,guest_thread_name,sizeof(guest_thread_name));
    else {size_t size=strnlen(name,15);memset(guest_thread_name,0,sizeof(guest_thread_name));memcpy(guest_thread_name,name,size);}
    return 0;
}
/* Error text uses guest numbers; native strerror() numbers differ. */
static const char *b_strerror(int e) {
    switch(e){
    case 0:return "Success";case 1:return "Operation not permitted";case 2:return "No such file or directory";
    case 3:return "No such process";case 4:return "Interrupted system call";case 5:return "Input/output error";
    case 9:return "Bad file descriptor";case 10:return "No child processes";case 11:return "Resource temporarily unavailable";
    case 12:return "Cannot allocate memory";case 13:return "Permission denied";case 14:return "Bad address";
    case 16:return "Device or resource busy";case 17:return "File exists";case 20:return "Not a directory";
    case 21:return "Is a directory";case 22:return "Invalid argument";case 24:return "Too many open files";
    case 28:return "No space left on device";case 29:return "Illegal seek";case 30:return "Read-only file system";
    case 32:return "Broken pipe";case 34:return "Result too large";case 36:return "File name too long";
    case 38:return "Function not implemented";case 75:return "Value too large";case 95:return "Operation not supported";
    case 110:return "Connection timed out";default:{static _Thread_local char unknown[48];snprintf(unknown,sizeof(unknown),"Error %d",e);return unknown;}}
}
static int b_strerror_r(int e,char *out,size_t bytes){if(!out)return 22;const char *s=b_strerror(e);size_t n=strlen(s);if(!bytes)return 34;size_t copy=n<bytes-1?n:bytes-1;memcpy(out,s,copy);out[copy]=0;return n>=bytes?34:0;}
static void b_perror(const char *prefix){int e=*ws_bionic_errno_location();fprintf(stderr,"%s%s%s\n",prefix?prefix:"",prefix&&*prefix?": ":"",b_strerror(e));}
static void b_openlog(const char *ident,int option,int facility){(void)ident;(void)option;(void)facility;}
static void b_closelog(void){}
static void b_vsyslog(int priority,const char *format,va_list a){fprintf(stderr,"[syslog:%d] ",priority);vfprintf(stderr,format,a);fputc('\n',stderr);}
uintptr_t ws_system_symbol(const char *name) {
#define B(n,f) if(!strcmp(name,n))return (uintptr_t)f
#define S(n) B(#n,b_##n)
    B("syscall",ws_syscall);B("__cxa_atexit",ws_cxa_atexit);B("__cxa_finalize",ws_cxa_finalize);
    S(sysconf);S(uname);S(gethostname);S(cacheflush);S(getpid);S(getppid);S(getuid);S(issetugid);
    B("geteuid",b_getuid);B("getgid",b_getuid);B("getegid",b_getuid);B("__get_h_errno",b_h_errno);
    B("sched_get_priority_max",b_priority);B("sched_get_priority_min",b_priority);
    S(fork);S(execv);S(execve);S(waitpid);S(initgroups);S(strerror);S(strerror_r);S(perror);
    S(openlog);S(closelog);S(vsyslog);
    S(prctl);
    S(getrusage);S(fchdir);S(flock);S(inotify_init);S(inotify_add_watch);S(getitimer);S(setitimer);
#undef S
#undef B
    return 0;
}
