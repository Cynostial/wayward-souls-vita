#ifndef WS_BIONIC_SYNC_H
#define WS_BIONIC_SYNC_H
#include <stdint.h>
#include <stddef.h>

/* Android ARM objects occupy four bytes, regardless of the host pthread ABI. */
typedef uint32_t ws_bionic_mutex;
typedef uint32_t ws_bionic_cond;
typedef uint32_t ws_bionic_sem;
typedef uint32_t ws_bionic_once;
typedef struct { int32_t seconds, nanoseconds; } ws_bionic_timespec;
int ws_bionic_mutexattr_init(uint32_t *attr);
int ws_bionic_mutexattr_destroy(uint32_t *attr);
int ws_bionic_mutexattr_settype(uint32_t *attr, int type);
int ws_bionic_mutex_init(ws_bionic_mutex *mutex, const uint32_t *attr);
int ws_bionic_mutex_destroy(ws_bionic_mutex *mutex);
int ws_bionic_mutex_lock(ws_bionic_mutex *mutex);
int ws_bionic_mutex_trylock(ws_bionic_mutex *mutex);
int ws_bionic_mutex_unlock(ws_bionic_mutex *mutex);
int ws_bionic_condattr_init(uint32_t *attr);
int ws_bionic_condattr_destroy(uint32_t *attr);
int ws_bionic_condattr_setclock(uint32_t *attr, int clock);
int ws_bionic_condattr_getclock(const uint32_t *attr, int *clock);
int ws_bionic_cond_init(ws_bionic_cond *cond, const uint32_t *attr);
int ws_bionic_cond_destroy(ws_bionic_cond *cond);
int ws_bionic_cond_signal(ws_bionic_cond *cond);
int ws_bionic_cond_broadcast(ws_bionic_cond *cond);
int ws_bionic_cond_wait(ws_bionic_cond *cond, ws_bionic_mutex *mutex);
int ws_bionic_cond_timedwait(ws_bionic_cond *cond, ws_bionic_mutex *mutex,
                            const ws_bionic_timespec *absolute);
int ws_bionic_cond_timedwait_monotonic_np(ws_bionic_cond *cond,
    ws_bionic_mutex *mutex, const ws_bionic_timespec *absolute);
int ws_bionic_sem_init(ws_bionic_sem *sem, int shared, unsigned value);
int ws_bionic_sem_destroy(ws_bionic_sem *sem);
int ws_bionic_sem_wait(ws_bionic_sem *sem);
int ws_bionic_sem_trywait(ws_bionic_sem *sem);
int ws_bionic_sem_timedwait(ws_bionic_sem *sem, const ws_bionic_timespec *absolute);
int ws_bionic_sem_post(ws_bionic_sem *sem);
int ws_bionic_sem_getvalue(ws_bionic_sem *sem, int *value);
int ws_bionic_pthread_once(ws_bionic_once *once, void (*initialize)(void));
uintptr_t ws_bionic_sync_symbol(const char *name);
#endif
