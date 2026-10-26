#define _POSIX_C_SOURCE 200809L
#include "ws_events.h"
#include "ws_io.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <time.h>

#define RING_SIZE 8192u
#define WATCHES 32
#define IN 1u
#define OUT 4u
#define ERR 8u
#define HUP 16u
#define EDGE (1u<<31)
#define ONESHOT (1u<<30)
typedef struct {unsigned char bytes[RING_SIZE];size_t start,used;} ring;
typedef struct {ring input[2];unsigned refs;int closed[2],read_closed[2],write_closed[2],socket;} channel;
typedef struct {int fd;uint32_t mask,previous,pending;uint64_t data;int active,used;} watch;
typedef struct {unsigned generation;int type,side,nonblock,readable,writable;channel *pipe;watch watches[WATCHES];int clock,semaphore;int64_t due,interval;uint64_t counter;} descriptor;
static descriptor table[WS_EVENT_FD_COUNT];
static unsigned next_generation=1;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static int64_t clock_ns(int clock) {
    struct timespec t;if(clock_gettime(clock?CLOCK_MONOTONIC:CLOCK_REALTIME,&t))return -1;
    return (int64_t)t.tv_sec*1000000000+t.tv_nsec;
}
static int timer_refresh(descriptor *d) {
    if(d->type!=3||!d->due)return 0;
    int64_t now=clock_ns(d->clock);if(now<d->due)return 0;
    uint64_t elapsed=d->interval?(uint64_t)((now-d->due)/d->interval)+1:1;
    d->counter+=elapsed;
    d->due=d->interval?d->due+(int64_t)elapsed*d->interval:0;
    return 1;
}
/* Sleep until either an IO notification, the caller's timeout, or the
 * nearest synthetic timer. Timer expiry is not a caller timeout. */
static int wait_changed(const struct timespec *caller) {
    struct timespec until;int timer=0,have=caller!=NULL;
    if(caller)until=*caller;
    int64_t real=clock_ns(0);
    for(int i=0;i<WS_EVENT_FD_COUNT;++i)if(table[i].type==3) {
        descriptor *d=&table[i];if(timer_refresh(d))return 0;
        if(!d->due)continue;
        int64_t end=real+d->due-clock_ns(d->clock);
        if(!have||end<(int64_t)until.tv_sec*1000000000+until.tv_nsec) {
            if(end<real)end=real;
            until.tv_sec=(time_t)(end/1000000000);until.tv_nsec=(long)(end%1000000000);have=timer=1;
        }
    }
    int e=have?pthread_cond_timedwait(&changed,&lock,&until):pthread_cond_wait(&changed,&lock);
    return e==ETIMEDOUT&&timer?0:e;
}
static int fail(int e){*ws_bionic_errno_location()=ws_bionic_error_from_native(e);return -1;}
int ws_event_owned(int fd){return fd>=WS_EVENT_FD_BASE&&fd<WS_EVENT_FD_BASE+WS_EVENT_FD_COUNT;}
static descriptor *lookup(int fd){return ws_event_owned(fd)&&table[fd-WS_EVENT_FD_BASE].type?&table[fd-WS_EVENT_FD_BASE]:NULL;}
static int reserve(int type) {
    for(int i=0;i<WS_EVENT_FD_COUNT;++i)if(!table[i].type) {
        memset(&table[i],0,sizeof(table[i]));table[i].type=type;table[i].generation=next_generation++;
        return WS_EVENT_FD_BASE+i;
    }
    return fail(EMFILE);
}
static uint32_t ready(descriptor *d) {
    if(!d)return ERR|HUP;
    if(d->type==3){timer_refresh(d);return d->counter?IN:0;}
    if(d->type==4)return (d->counter?IN:0)|(d->counter<UINT64_MAX-1?OUT:0);
    if(d->type==2) {
        for(int i=0;i<WATCHES;++i)if(d->watches[i].used&&d->watches[i].active) {
            watch *w=&d->watches[i];
            descriptor *other=lookup(w->fd);
            /* epoll nesting is explicitly rejected by ctl. */
            uint32_t r=ready(other)&(w->mask|ERR|HUP);
            w->pending|=r&~w->previous;w->previous=r;
            if((w->mask&EDGE)?w->pending:r)return IN;
        }
        return 0;
    }
    channel *c=d->pipe;int side=d->side;uint32_t r=0;
    if(d->readable&&(c->input[side].used||c->closed[1-side]||c->write_closed[1-side]||c->read_closed[side]))r|=IN;
    if(d->writable&&!c->closed[1-side]&&!c->read_closed[1-side]&&!c->write_closed[side]&&c->input[1-side].used<RING_SIZE)r|=OUT;
    if(c->closed[1-side])r|=HUP|(d->writable?ERR:0);
    return r;
}
static void notify(void) {
    for(int i=0;i<WS_EVENT_FD_COUNT;++i)if(table[i].type==2)for(int j=0;j<WATCHES;++j) {
        watch *w=&table[i].watches[j];if(!w->used)continue;
        uint32_t r=ready(lookup(w->fd))&(w->mask|ERR|HUP);
        if(w->active)w->pending|=r&~w->previous;
        w->previous=r;
    }
    pthread_cond_broadcast(&changed);
}
int ws_socketpair(int domain,int type,int protocol,int out[2]) {
    if(!out)return fail(EFAULT);
    if(domain!=1||type!=1||protocol)return fail(ENOTSUP);
    channel *c=calloc(1,sizeof(*c));if(!c)return fail(ENOMEM);
    pthread_mutex_lock(&lock);int a=reserve(1),b=a<0?-1:reserve(1);
    if(b<0){if(a>=0)memset(lookup(a),0,sizeof(descriptor));pthread_mutex_unlock(&lock);free(c);return -1;}
    c->refs=2;c->socket=1;descriptor *x=lookup(a),*y=lookup(b);
    x->pipe=y->pipe=c;x->side=0;y->side=1;x->readable=x->writable=y->readable=y->writable=1;
    out[0]=a;out[1]=b;pthread_mutex_unlock(&lock);return 0;
}
int ws_pipe(int out[2]) {
    if(ws_socketpair(1,1,0,out))return -1;
    pthread_mutex_lock(&lock);lookup(out[0])->writable=0;lookup(out[1])->readable=0;lookup(out[0])->pipe->socket=0;
    pthread_mutex_unlock(&lock);return 0;
}
int ws_event_close(int fd) {
    pthread_mutex_lock(&lock);descriptor *d=lookup(fd);
    if(!d){pthread_mutex_unlock(&lock);return fail(EBADF);}
    if(d->type==1){channel *c=d->pipe;c->closed[d->side]=1;if(!--c->refs)free(c);}
    memset(d,0,sizeof(*d));
    /* Linux removes watches for a closed descriptor, including before reuse. */
    for(int i=0;i<WS_EVENT_FD_COUNT;++i)if(table[i].type==2)for(int j=0;j<WATCHES;++j)
        if(table[i].watches[j].used&&table[i].watches[j].fd==fd)memset(&table[i].watches[j],0,sizeof(watch));
    notify();pthread_mutex_unlock(&lock);return 0;
}
static ssize_t transfer(int fd,void *data,size_t bytes,int writing,int flags,int socket) {
    if(bytes>INT_MAX||(!data&&bytes))return fail(EINVAL);
    pthread_mutex_lock(&lock);descriptor *d=lookup(fd);
    if(!d){pthread_mutex_unlock(&lock);return fail(EBADF);}
    if(socket&&(d->type!=1||!d->pipe->socket)){pthread_mutex_unlock(&lock);return fail(ENOTSOCK);}
    if(d->type!=1||!(writing?d->writable:d->readable)){pthread_mutex_unlock(&lock);return fail(EBADF);}
    unsigned generation=d->generation;size_t done=0;int e=0;
    while(done<bytes) {
        channel *c=d->pipe;ring *r=&c->input[writing?1-d->side:d->side];
        if(writing&&(c->closed[1-d->side]||c->read_closed[1-d->side]||c->write_closed[d->side])){e=EPIPE;break;}
        if(!writing&&c->read_closed[d->side])break;
        size_t available=writing?RING_SIZE-r->used:r->used;
        if(!available) {
            if(!writing&&(c->closed[1-d->side]||c->write_closed[1-d->side]))break;
            if(done&&!(flags&0x100))break;
            if(d->nonblock||(flags&0x40)){e=EAGAIN;break;}
            e=pthread_cond_wait(&changed,&lock);if(e)break;
            d=lookup(fd);if(!d||d->generation!=generation){e=EBADF;break;}
            continue;
        }
        size_t n=bytes-done;if(n>available)n=available;
        size_t at=writing?(r->start+r->used)%RING_SIZE:r->start,first=RING_SIZE-at;
        if(first>n)first=n;
        if(writing){memcpy(r->bytes+at,(char *)data+done,first);memcpy(r->bytes,(char *)data+done+first,n-first);r->used+=n;}
        else{memcpy((char *)data+done,r->bytes+at,first);memcpy((char *)data+done+first,r->bytes,n-first);if(!(flags&2)){r->start=(r->start+n)%RING_SIZE;r->used-=n;}}
        done+=n;notify();
        if(flags&2)break; /* MSG_PEEK never consumes or repeats the same bytes. */
    }
    pthread_mutex_unlock(&lock);return done?(ssize_t)done:e?fail(e):0;
}
static ssize_t ws_recv(int fd,void *data,size_t bytes,int flags){
    if(flags&~(2|0x40|0x100))return fail(ENOTSUP);
    if((flags&2)&&(flags&0x100))return fail(ENOTSUP);
    return transfer(fd,data,bytes,0,flags,1);
}
static ssize_t ws_send(int fd,const void *data,size_t bytes,int flags){
    if(flags&~(0x40|0x4000))return fail(ENOTSUP);
    return transfer(fd,(void *)data,bytes,1,flags,1);
}
static ssize_t ws_sendto(int fd,const void *data,size_t bytes,int flags,const void *address,uint32_t size){
    if(address||size)return fail(EISCONN);
    return ws_send(fd,data,bytes,flags);
}
static ssize_t ws_recvfrom(int fd,void *data,size_t bytes,int flags,void *address,uint32_t *size){
    if(address&&!size)return fail(EFAULT);
    ssize_t n=ws_recv(fd,data,bytes,flags);
    if(n>=0&&address){uint16_t family=1;size_t copy=*size<2?*size:2;memcpy(address,&family,copy);*size=2;}
    return n;
}
static descriptor *socket_lookup(int fd,int *error){
    descriptor *d=lookup(fd);*error=!d?EBADF:d->type!=1||!d->pipe->socket?ENOTSOCK:0;return *error?NULL:d;
}
static int ws_shutdown(int fd,int how){
    if(how<0||how>2)return fail(EINVAL);
    pthread_mutex_lock(&lock);int e;descriptor *d=socket_lookup(fd,&e);
    if(d){
        if(how!=1){d->pipe->read_closed[d->side]=1;d->pipe->input[d->side].used=0;}
        if(how!=0)d->pipe->write_closed[d->side]=1;
        notify();
    }
    pthread_mutex_unlock(&lock);return e?fail(e):0;
}
static int ws_getsockname(int fd,void *address,uint32_t *size){
    if(!address||!size)return fail(EFAULT);
    pthread_mutex_lock(&lock);int e;socket_lookup(fd,&e);
    if(!e){uint16_t family=1;memcpy(address,&family,*size<2?*size:2);*size=2;}
    pthread_mutex_unlock(&lock);return e?fail(e):0;
}
static int ws_getsockopt(int fd,int level,int option,void *out,uint32_t *size){
    if(!out||!size)return fail(EFAULT);
    pthread_mutex_lock(&lock);int e;socket_lookup(fd,&e);int value=0;
    if(!e){
        if(level!=1)e=ENOPROTOOPT;
        else switch(option){case 3:value=1;break;case 4:case 30:case 38:break;
            case 7:case 8:value=RING_SIZE;break;case 39:value=1;break;default:e=ENOPROTOOPT;}
        if(!e){size_t n=*size<sizeof(value)?*size:sizeof(value);memcpy(out,&value,n);*size=(uint32_t)n;}
    }
    pthread_mutex_unlock(&lock);return e?fail(e):0;
}
static int ws_setsockopt(int fd,int level,int option,const void *value,uint32_t size){
    (void)level;(void)option;
    if(size&&!value)return fail(EFAULT);
    pthread_mutex_lock(&lock);int e;socket_lookup(fd,&e);pthread_mutex_unlock(&lock);
    /* No adjustable socket options are claimed by the fixed-capacity local
     * channel backend. Callers can query its actual size with getsockopt. */
    return fail(e?e:ENOPROTOOPT);
}
static int ws_ioctl(int fd,unsigned request,void *out){
    if(!out)return fail(EFAULT);
    pthread_mutex_lock(&lock);descriptor *d=lookup(fd);int e=!d?EBADF:0;
    if(d){
        if(request==0x5421){d->nonblock=*(int *)out!=0;notify();} /* FIONBIO */
        else if(request==0x541b&&d->type==1)*(int *)out=(int)d->pipe->input[d->side].used;
        else e=ENOTTY;
    }
    pthread_mutex_unlock(&lock);return e?fail(e):0;
}
static int message_size(const ws_msghdr *msg,size_t *size,int writing){
    if(!msg||(!msg->vectors&&msg->count))return fail(EFAULT);
    if(msg->count>1024)return fail(EMSGSIZE);
    if(msg->control_size&&(writing||!msg->control))return fail(writing?ENOTSUP:EFAULT);
    size_t total=0;
    for(uint32_t i=0;i<msg->count;++i){
        if(msg->vectors[i].size&&!msg->vectors[i].base)return fail(EFAULT);
        if(msg->vectors[i].size>INT_MAX-total)return fail(EMSGSIZE);
        total+=msg->vectors[i].size;
    }
    *size=total;return 0;
}
static ssize_t ws_sendmsg(int fd,const ws_msghdr *msg,int flags){
    size_t total;if(message_size(msg,&total,1))return -1;
    if(msg->name||msg->name_size)return fail(EISCONN);
    /* The stream may legally return a short write. A bounded staging buffer
     * avoids allocating in proportion to an untrusted iovec length. */
    unsigned char buffer[RING_SIZE];size_t at=0;
    for(uint32_t i=0;i<msg->count&&at<sizeof(buffer);++i){size_t n=msg->vectors[i].size;if(n>sizeof(buffer)-at)n=sizeof(buffer)-at;if(n)memcpy(buffer+at,msg->vectors[i].base,n);at+=n;}
    return ws_send(fd,buffer,at,flags);
}
static ssize_t ws_recvmsg(int fd,ws_msghdr *msg,int flags){
    size_t total;if(message_size(msg,&total,0))return -1;
    if(msg->name_size&&!msg->name)return fail(EFAULT);
    if(total>RING_SIZE&&(flags&0x100))return fail(ENOTSUP);
    unsigned char buffer[RING_SIZE];if(total>sizeof(buffer))total=sizeof(buffer);
    ssize_t result=ws_recv(fd,buffer,total,flags);if(result<0)return result;
    size_t at=0;
    for(uint32_t i=0;i<msg->count&&at<(size_t)result;++i){size_t n=msg->vectors[i].size;if(n>(size_t)result-at)n=(size_t)result-at;if(n)memcpy(msg->vectors[i].base,buffer+at,n);at+=n;}
    msg->flags=0;msg->control_size=0;
    if(msg->name){uint16_t family=1;memcpy(msg->name,&family,msg->name_size<2?msg->name_size:2);msg->name_size=2;}
    return result;
}
static ssize_t counter_io(int fd,void *data,size_t n,int writing) {
    pthread_mutex_lock(&lock);descriptor *d=lookup(fd);
    if(!d||(d->type!=3&&d->type!=4)){pthread_mutex_unlock(&lock);return -2;}
    if(!data||n<8||(writing&&d->type==3)){pthread_mutex_unlock(&lock);return fail(EINVAL);}
    unsigned generation=d->generation;uint64_t value=0;int e=0;
    if(writing){memcpy(&value,data,8);if(value==UINT64_MAX){pthread_mutex_unlock(&lock);return fail(EINVAL);}}
    for(;;) {
        timer_refresh(d);
        int available=writing?value<=UINT64_MAX-1-d->counter:d->counter!=0;
        if(available)break;
        if(d->nonblock){e=EAGAIN;break;}
        e=wait_changed(NULL);if(e)break;
        d=lookup(fd);if(!d||d->generation!=generation){e=EBADF;break;}
    }
    if(!e){if(writing)d->counter+=value;else{value=d->semaphore?1:d->counter;d->counter-=value;memcpy(data,&value,8);}notify();}
    pthread_mutex_unlock(&lock);return e?fail(e):8;
}
ssize_t ws_event_read(int fd,void *data,size_t n){ssize_t r=counter_io(fd,data,n,0);return r==-2?transfer(fd,data,n,0,0,0):r;}
ssize_t ws_event_write(int fd,const void *data,size_t n){ssize_t r=counter_io(fd,(void *)data,n,1);return r==-2?transfer(fd,(void *)data,n,1,0,0):r;}
int ws_eventfd(unsigned initial,int flags) {
    if(flags&~(1|0x800|0x80000))return fail(EINVAL);
    pthread_mutex_lock(&lock);int fd=reserve(4);
    if(fd>=0){descriptor *d=lookup(fd);d->counter=initial;d->nonblock=(flags&0x800)!=0;d->semaphore=flags&1;d->readable=d->writable=1;}
    pthread_mutex_unlock(&lock);return fd;
}
int ws_timerfd_create(int clock,int flags) {
    if((clock!=0&&clock!=1)||(flags&~(0x800|0x80000)))return fail(EINVAL);
    pthread_mutex_lock(&lock);int fd=reserve(3);
    if(fd>=0){descriptor *d=lookup(fd);d->clock=clock;d->nonblock=(flags&0x800)!=0;d->readable=1;}
    pthread_mutex_unlock(&lock);return fd;
}
static void timer_value(descriptor *d,ws_itimerspec *out) {
    timer_refresh(d);int64_t left=d->due?d->due-clock_ns(d->clock):0;if(left<0)left=0;
    *out=(ws_itimerspec){{(int32_t)(d->interval/1000000000),(int32_t)(d->interval%1000000000)},
        {(int32_t)(left/1000000000),(int32_t)(left%1000000000)}};
}
static int valid_time(ws_bionic_timespec t){return t.seconds>=0&&t.nanoseconds>=0&&t.nanoseconds<1000000000;}
int ws_timerfd_settime(int fd,int flags,const ws_itimerspec *value,ws_itimerspec *old) {
    if(!value||(flags&~1)||!valid_time(value->value)||!valid_time(value->interval))return fail(EINVAL);
    pthread_mutex_lock(&lock);descriptor *d=lookup(fd);
    if(!d||d->type!=3){pthread_mutex_unlock(&lock);return fail(EBADF);}
    if(old)timer_value(d,old);
    d->due=(int64_t)value->value.seconds*1000000000+value->value.nanoseconds;
    if(d->due&&!(flags&1))d->due+=clock_ns(d->clock);
    d->interval=(int64_t)value->interval.seconds*1000000000+value->interval.nanoseconds;d->counter=0;
    notify();pthread_mutex_unlock(&lock);return 0;
}
int ws_timerfd_gettime(int fd,ws_itimerspec *value) {
    if(!value)return fail(EFAULT);
    pthread_mutex_lock(&lock);descriptor *d=lookup(fd);
    if(!d||d->type!=3){pthread_mutex_unlock(&lock);return fail(EBADF);}
    timer_value(d,value);pthread_mutex_unlock(&lock);return 0;
}
int ws_event_fcntl(int fd,int command,int value) {
    pthread_mutex_lock(&lock);descriptor *d=lookup(fd);int r=0;
    if(!d)r=fail(EBADF);
    else if(command==3)r=(d->nonblock?0x800:0)|(d->readable?(d->writable?2:0):1);
    else if(command==4){if(value&~0x803)r=fail(ENOTSUP);else d->nonblock=(value&0x800)!=0;}
    else if(command==1)r=0;
    else if(command!=2||value&~1)r=fail(ENOTSUP); /* FD_CLOEXEC: no exec in this process. */
    pthread_mutex_unlock(&lock);return r;
}
int ws_epoll_create(int size) {
    if(size<=0)return fail(EINVAL);
    pthread_mutex_lock(&lock);int fd=reserve(2);pthread_mutex_unlock(&lock);return fd;
}
int ws_epoll_ctl(int epfd,int op,int fd,const ws_epoll_event *event) {
    pthread_mutex_lock(&lock);descriptor *ep=lookup(epfd),*d=lookup(fd);int e=0;watch *found=NULL,*empty=NULL;
    if(!ep||!d)e=EBADF;
    else if(ep->type!=2||d->type==2||op<1||op>3)e=EINVAL;
    else {
        for(int i=0;i<WATCHES;++i){watch *w=&ep->watches[i];if(w->used&&w->fd==fd)found=w;else if(!w->used&&!empty)empty=w;}
        if(op==1&&found)e=EEXIST;
        else if(op!=1&&!found)e=ENOENT;
        else if(op==2)memset(found,0,sizeof(*found));
        else if(!event)e=EFAULT;
        else if(event->events&~(IN|OUT|ERR|HUP|EDGE|ONESHOT))e=ENOTSUP;
        else {
            watch *w=op==1?empty:found;
            if(!w)e=ENOSPC;
            else {*w=(watch){.fd=fd,.mask=event->events,.data=event->data,.active=1,.used=1};w->pending=ready(d)&(w->mask|ERR|HUP);w->previous=w->pending;}
        }
    }
    notify();pthread_mutex_unlock(&lock);return e?fail(e):0;
}
static int deadline(int timeout,struct timespec *until) {
    if(clock_gettime(CLOCK_REALTIME,until))return errno;
    until->tv_sec+=timeout/1000;until->tv_nsec+=(timeout%1000)*1000000L;
    if(until->tv_nsec>=1000000000){++until->tv_sec;until->tv_nsec-=1000000000;}
    return 0;
}
int ws_epoll_wait(int epfd,ws_epoll_event *out,int maximum,int timeout) {
    if(!out||maximum<=0||timeout< -1)return fail(EINVAL);
    struct timespec until;int e=timeout>0?deadline(timeout,&until):0;if(e)return fail(e);
    pthread_mutex_lock(&lock);descriptor *ep=lookup(epfd);int n=0;
    if(!ep||ep->type!=2){pthread_mutex_unlock(&lock);return fail(EBADF);}
    unsigned generation=ep->generation;
    for(;;) {
        for(int i=0;i<WATCHES&&n<maximum;++i) {
            watch *w=&ep->watches[i];if(!w->used||!w->active)continue;
            uint32_t current=ready(lookup(w->fd))&(w->mask|ERR|HUP);
            w->pending|=current&~w->previous;w->previous=current;
            uint32_t r=(w->mask&EDGE)?w->pending:current;
            if(r){out[n++]=(ws_epoll_event){r,0,w->data};w->pending=0;if(w->mask&ONESHOT)w->active=0;}
        }
        if(n||!timeout)break;
        e=wait_changed(timeout<0?NULL:&until);
        if(e==ETIMEDOUT){e=0;break;}if(e)break;
        ep=lookup(epfd);if(!ep||ep->generation!=generation){e=EBADF;break;}
    }
    pthread_mutex_unlock(&lock);return e?fail(e):n;
}
int ws_event_select(int nfds,uint32_t *readset,uint32_t *writeset,uint32_t *exceptset,const ws_bionic_timespec *time) {
    if(nfds<0||nfds>1024)return fail(EINVAL);
    if(time&&(time->seconds<0||time->nanoseconds<0||time->nanoseconds>=1000000000))return fail(EINVAL);
    uint32_t requested[3][32]={{0}},*sets[]={readset,writeset,exceptset};
    for(int j=0;j<3;++j)if(sets[j])memcpy(requested[j],sets[j],128);
    struct timespec until;
    if(time){if(clock_gettime(CLOCK_REALTIME,&until))return fail(errno);until.tv_sec+=time->seconds;until.tv_nsec+=time->nanoseconds;if(until.tv_nsec>=1000000000){++until.tv_sec;until.tv_nsec-=1000000000;}}
    pthread_mutex_lock(&lock);int e=0,n=0;
    for(;;){
        for(int j=0;j<3;++j)if(sets[j])memset(sets[j],0,128);
        for(int fd=0;fd<nfds;++fd)for(int j=0;j<3;++j)if(requested[j][fd/32]&(1u<<(fd%32))) {
            descriptor *d=lookup(fd);if(!d){e=EBADF;break;}
            uint32_t mask=j==0?IN|HUP:j==1?OUT|ERR:ERR;
            if(ready(d)&mask){sets[j][fd/32]|=1u<<(fd%32);++n;}
        }
        if(e||n||(time&&!time->seconds&&!time->nanoseconds))break;
        e=wait_changed(time?&until:NULL);
        if(e==ETIMEDOUT){e=0;break;}if(e)break;
    }
    pthread_mutex_unlock(&lock);return e?fail(e):n;
}
static int ws_pselect(int nfds,uint32_t *r,uint32_t *w,uint32_t *e,const ws_bionic_timespec *t,const uint32_t *mask) {
    if(mask)return fail(ENOTSUP);
    return ws_event_select(nfds,r,w,e,t);
}
static int ws_select(int nfds,uint32_t *r,uint32_t *w,uint32_t *e,int32_t *timeval) {
    ws_bionic_timespec timeout,*p=NULL;
    if(timeval){
        if(timeval[0]<0||timeval[1]<0||timeval[1]>=1000000)return fail(EINVAL);
        timeout=(ws_bionic_timespec){timeval[0],timeval[1]*1000};p=&timeout;
    }
    int64_t start=clock_ns(1);
    int result=ws_event_select(nfds,r,w,e,p);
    if(timeval){
        int64_t remaining=(int64_t)timeout.seconds*1000000000+timeout.nanoseconds-(clock_ns(1)-start);
        if(remaining<0)remaining=0;
        timeval[0]=(int32_t)(remaining/1000000000);timeval[1]=(int32_t)(remaining%1000000000/1000);
    }
    return result;
}
int ws_poll(ws_pollfd *fds,uint32_t count,int timeout) {
    if(count>1024||timeout< -1)return fail(EINVAL);
    if(count&&!fds)return fail(EFAULT);
    struct timespec until;int e=timeout>0?deadline(timeout,&until):0;if(e)return fail(e);
    pthread_mutex_lock(&lock);int found=0;
    for(;;){
        found=0;
        for(uint32_t i=0;i<count;++i){
            ws_pollfd *p=&fds[i];p->revents=0;if(p->fd<0)continue;
            descriptor *d=lookup(p->fd);
            if(!d)p->revents=0x20; /* POLLNVAL, even when events is zero. */
            else {
                uint32_t bits=ready(d);
                if(bits&IN)p->revents|=p->events&(1|0x40); /* IN/RDNORM */
                if(bits&OUT)p->revents|=p->events&(4|0x100); /* OUT/WRNORM */
                if(bits&ERR)p->revents|=8;
                if(bits&HUP)p->revents|=0x10;
            }
            if(p->revents)++found;
        }
        if(found||!timeout)break;
        e=wait_changed(timeout<0?NULL:&until);
        if(e==ETIMEDOUT){e=0;break;}if(e)break;
    }
    pthread_mutex_unlock(&lock);return e?fail(e):found;
}
uintptr_t ws_events_symbol(const char *name) {
#define S(n) if(!strcmp(name,#n))return (uintptr_t)ws_##n
    S(socketpair);S(pipe);S(epoll_create);S(epoll_ctl);S(epoll_wait);S(pselect);S(select);S(poll);
    S(send);S(recv);S(sendto);S(recvfrom);
    S(shutdown);S(getsockname);S(getsockopt);S(setsockopt);S(sendmsg);S(recvmsg);S(ioctl);
    if(!strcmp(name,"getpeername"))return (uintptr_t)ws_getsockname;
#undef S
    return 0;
}
