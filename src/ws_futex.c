#define _POSIX_C_SOURCE 200809L
#include "ws_futex.h"
#include "ws_io.h"
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <limits.h>

/* One wait record per blocked thread. Compare/enqueue and wake use the same
 * mutex, eliminating the missed-wakeup gap. No busy wait on Vita cores. */
typedef struct waiter {
    struct waiter *next;volatile int *address;pthread_cond_t changed;int woken;
} waiter;
static waiter *head;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static void unlink_waiter(void *arg) {
    waiter *w=arg;waiter **p=&head;
    while(*p&&*p!=w)p=&(*p)->next;
    if(*p)*p=w->next;
    pthread_mutex_unlock(&lock);pthread_cond_destroy(&w->changed);
}
int ws_futex_wait(volatile int *address,int expected,const ws_bionic_timespec *relative) {
    if(!address||((uintptr_t)address&3))return -22;
    struct timespec until={0};
    if(relative) {
        if(relative->seconds<0||relative->nanoseconds<0||relative->nanoseconds>=1000000000)return -22;
        if(clock_gettime(CLOCK_REALTIME,&until))return -ws_bionic_error_from_native(errno);
        until.tv_sec+=relative->seconds;until.tv_nsec+=relative->nanoseconds;
        if(until.tv_nsec>=1000000000){++until.tv_sec;until.tv_nsec-=1000000000;}
    }
    waiter w={.address=address};int e=pthread_cond_init(&w.changed,NULL);
    if(e)return -ws_bionic_error_from_native(e);
    pthread_mutex_lock(&lock);
    if(__atomic_load_n(address,__ATOMIC_ACQUIRE)!=expected) {
        pthread_mutex_unlock(&lock);pthread_cond_destroy(&w.changed);return -11;
    }
    w.next=head;head=&w;
    pthread_cleanup_push(unlink_waiter,&w);
    while(!w.woken&&!e)e=relative?pthread_cond_timedwait(&w.changed,&lock,&until):pthread_cond_wait(&w.changed,&lock);
    if(w.woken)e=0;
    pthread_cleanup_pop(1);
    return -ws_bionic_error_from_native(e);
}
int ws_futex_wake(volatile int *address,int count) {
    if(!address||((uintptr_t)address&3)||count<0)return -22;
    pthread_mutex_lock(&lock);int n=0;
    for(waiter *w=head;w&&n<count;w=w->next)if(w->address==address&&!w->woken) {
        w->woken=1;++n;pthread_cond_signal(&w->changed);
    }
    pthread_mutex_unlock(&lock);return n;
}
