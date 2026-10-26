#ifndef WS_EVENTS_H
#define WS_EVENTS_H
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include "ws_sync.h"
/* Kept below libv's getrlimit-backed fd table and its 1024-bit fd_set. */
#define WS_EVENT_FD_BASE 768
#define WS_EVENT_FD_COUNT 64
typedef struct {uint32_t events,padding;uint64_t data;} ws_epoll_event;
_Static_assert(sizeof(ws_epoll_event)==16,"ARM epoll_event ABI");
int ws_event_owned(int fd);
int ws_event_close(int fd);
ssize_t ws_event_read(int fd,void *,size_t);
ssize_t ws_event_write(int fd,const void *,size_t);
int ws_socketpair(int,int,int,int[2]);
int ws_pipe(int[2]);
int ws_epoll_create(int);
int ws_epoll_ctl(int,int,int,const ws_epoll_event *);
int ws_epoll_wait(int,ws_epoll_event *,int,int);
int ws_event_fcntl(int,int,int);
typedef struct {void *base;uint32_t size;} ws_iovec;
typedef struct {void *name;uint32_t name_size;ws_iovec *vectors;uint32_t count;void *control;uint32_t control_size;int32_t flags;} ws_msghdr;
#if defined(__arm__)
_Static_assert(sizeof(ws_iovec)==8&&sizeof(ws_msghdr)==28,"ARM message ABI");
#endif
int ws_event_select(int,uint32_t *,uint32_t *,uint32_t *,const ws_bionic_timespec *);
typedef struct {int32_t fd;int16_t events,revents;} ws_pollfd;
int ws_poll(ws_pollfd *,uint32_t,int);
uintptr_t ws_events_symbol(const char *);
typedef struct {ws_bionic_timespec interval,value;} ws_itimerspec;
int ws_timerfd_create(int,int);
int ws_timerfd_settime(int,int,const ws_itimerspec *,ws_itimerspec *);
int ws_timerfd_gettime(int,ws_itimerspec *);
int ws_eventfd(unsigned,int);
#endif
