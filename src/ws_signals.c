#include "ws_signals.h"
#include "ws_io.h"
#include "ws_threads.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
typedef void (*handler)(int);
typedef struct {handler function;uint32_t mask,flags;void (*restorer)(void);} action;
#if UINTPTR_MAX==UINT32_MAX
_Static_assert(sizeof(action)==16,"Android ARM sigaction");
#endif
static action actions[32];
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static _Thread_local uint32_t mask,pending;
static _Thread_local int dispatching;
static int fail(int e){*ws_bionic_errno_location()=e;return -1;}
static int valid(int sig){return sig>0&&sig<32;}
static uint32_t clean_mask(uint32_t m){return m&~((1u<<8)|(1u<<18));}
static void dispatch(void) {
    if(dispatching)return;
    dispatching=1;
    while(pending&~mask){
        int sig=1;while(!(pending&~mask&(1u<<(sig-1))))++sig;
        pending&=~(1u<<(sig-1));pthread_mutex_lock(&lock);action a=actions[sig];
        if(a.flags&0x80000000u)memset(&actions[sig],0,sizeof(a));
        pthread_mutex_unlock(&lock);
        if((uintptr_t)a.function==1)continue;
        if(!a.function){
            /* POSIX's ignored defaults among the supported signal range. */
            if(sig==17||sig==23||sig==28)continue;
            fprintf(stderr,"Wayward: default guest signal %d terminates process\n",sig);abort();
        }
        uint32_t saved=mask;mask=clean_mask(mask|a.mask|((a.flags&0x40000000u)?0:1u<<(sig-1)));
        a.function(sig);mask=saved;
    }
    dispatching=0;
}
uint32_t ws_signal_mask_get(void){return mask;}
void ws_signal_leave_handler(void){dispatching=0;}
void ws_signal_mask_restore(uint32_t value){mask=clean_mask(value);dispatching=0;dispatch();}
void ws_jump_invalid(void){fputs("Wayward: invalid ARM jump buffer\n",stderr);abort();}
static int b_sigaction(int sig,const action *set,action *old) {
    if(!valid(sig)||sig==9||sig==19)return fail(22);
    /* SA_SIGINFO, alternate stacks and custom kernel restorers require a
     * different calling convention and cannot be silently accepted. */
    if(set&&((set->flags&~0xd0000000u)||set->restorer))return fail(95);
    pthread_mutex_lock(&lock);if(old)*old=actions[sig];if(set){actions[sig]=*set;actions[sig].mask=clean_mask(set->mask);}pthread_mutex_unlock(&lock);return 0;
}
static handler b_bsd_signal(int sig,handler function){action a={function,0,0x10000000u,NULL},old;if(b_sigaction(sig,&a,&old))return (handler)(intptr_t)-1;return old.function;}
static int b_sigprocmask(int how,const uint32_t *set,uint32_t *old){
    if(set&&(how<0||how>2))return fail(22);
    uint32_t value=set?*set:0;if(old)*old=mask;
    if(set){mask=clean_mask(how==0?mask|value:how==1?mask&~value:value);dispatch();}return 0;
}
static int b_pthread_sigmask(int how,const uint32_t *set,uint32_t *old){int saved=*ws_bionic_errno_location();int r=b_sigprocmask(how,set,old),e=*ws_bionic_errno_location();*ws_bionic_errno_location()=saved;return r?e:0;}
static int b_raise(int sig){if(!valid(sig))return fail(22);pending|=1u<<(sig-1);dispatch();return 0;}
static int b_kill(int pid,int sig){if(pid!=getpid()&&pid!=0)return fail(3);return sig?b_raise(sig):0;}
static int b_pthread_kill(uint32_t thread,int sig){
    if(sig<0||sig>=32)return 22;
    if(thread==ws_bionic_pthread_self())return sig?(b_raise(sig)?*ws_bionic_errno_location():0):0;
    ws_bionic_thread_attr a;if(ws_bionic_pthread_getattr_np(thread,&a))return 3;
    return sig?95:0; /* no asynchronous cross-thread injection */
}
static int b_sigsuspend(const uint32_t *set){(void)set;return fail(95);}
static const char *b_strsignal(int sig){static _Thread_local char out[32];snprintf(out,sizeof(out),"Signal %d",sig);return out;}
uintptr_t ws_signals_symbol(const char *name){
#define S(n) if(!strcmp(name,#n))return (uintptr_t)b_##n
    S(sigaction);S(bsd_signal);S(sigprocmask);S(pthread_sigmask);S(raise);S(kill);S(pthread_kill);S(sigsuspend);S(strsignal);
#undef S
#ifdef __arm__
    extern int ws_setjmp(void *),ws_sigsetjmp(void *,int);
    extern void ws_longjmp(void *,int),ws_siglongjmp(void *,int);
    if(!strcmp(name,"setjmp"))return (uintptr_t)ws_setjmp;
    if(!strcmp(name,"sigsetjmp"))return (uintptr_t)ws_sigsetjmp;
    if(!strcmp(name,"longjmp"))return (uintptr_t)ws_longjmp;
    if(!strcmp(name,"siglongjmp"))return (uintptr_t)ws_siglongjmp;
#endif
    return 0;
}
