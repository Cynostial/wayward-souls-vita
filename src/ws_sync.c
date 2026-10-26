#define _POSIX_C_SOURCE 200809L
#include "ws_sync.h"
#include "ws_io.h"
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>

/* The bounded table keeps sem_post's lookup free of registry locks and avoids
 * truncating native 64-bit pointers during host testing. Handles include a
 * generation so a stale handle cannot alias a recycled object. As with POSIX,
 * destroying/reinitializing an object concurrently with its use is invalid.
 * Once controls retain one slot for process lifetime. No signal support is
 * implied by the use of the native semaphore backend. */
#define SLOT_COUNT 2048u
#define SLOT_MASK (SLOT_COUNT - 1u)
enum { K_MUTEX = 1, K_COND, K_SEM, K_ONCE };
typedef struct {
    _Atomic uint32_t token;
    unsigned kind;
    union {
        pthread_mutex_t mutex;
        struct { pthread_cond_t realtime, monotonic; int clock; } cond;
        sem_t sem;
        pthread_once_t once;
    } value;
} sync_slot;
static sync_slot slots[SLOT_COUNT];
static pthread_mutex_t table_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t generation = 1;
static int result(int error) { return ws_bionic_error_from_native(error); }
static int sem_error(int error) {
    *ws_bionic_errno_location() = result(error);
    return -1;
}
static uint32_t word_load(const uint32_t *word) {
    return __atomic_load_n(word, __ATOMIC_ACQUIRE);
}
static void word_store(uint32_t *word, uint32_t value) {
    __atomic_store_n(word, value, __ATOMIC_RELEASE);
}
static sync_slot *lookup(uint32_t token, unsigned kind) {
    if (!(token & 0x80000000u)) return NULL;
    sync_slot *slot = &slots[token & SLOT_MASK];
    if (atomic_load_explicit(&slot->token, memory_order_acquire) != token ||
        slot->kind != kind) return NULL;
    return slot;
}
static int initialize(uint32_t *word, unsigned kind, uint32_t options) {
    /* Caller holds table_lock. Do not read *word: pthread_*_init accepts
     * uninitialized storage. Publish only after every native object exists. */
    unsigned i;
    for (i = 0; i < SLOT_COUNT; ++i) if (!atomic_load(&slots[i].token)) break;
    if (i == SLOT_COUNT) return EAGAIN;
    sync_slot *s = &slots[i];
    int error = 0;
    if (kind == K_MUTEX) {
        pthread_mutexattr_t attr;
        error = pthread_mutexattr_init(&attr);
        if (error) return error;
        int type = options == 1 ? PTHREAD_MUTEX_RECURSIVE :
                   options == 2 ? PTHREAD_MUTEX_ERRORCHECK : PTHREAD_MUTEX_NORMAL;
        error = pthread_mutexattr_settype(&attr, type);
        if (!error) error = pthread_mutex_init(&s->value.mutex, &attr);
        pthread_mutexattr_destroy(&attr);
    } else if (kind == K_COND) {
        pthread_condattr_t attr;
        error = pthread_condattr_init(&attr);
        if (error) return error;
        error = pthread_condattr_setclock(&attr, CLOCK_REALTIME);
        if (!error) error = pthread_cond_init(&s->value.cond.realtime, &attr);
        if (!error) {
#ifndef _WIN32
            error = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
#endif
            if (!error) error = pthread_cond_init(&s->value.cond.monotonic, &attr);
            if (error) pthread_cond_destroy(&s->value.cond.realtime);
        }
        pthread_condattr_destroy(&attr);
        s->value.cond.clock = (int)options;
    } else if (kind == K_SEM) {
        if (sem_init(&s->value.sem, 0, options)) error = errno;
    } else {
        pthread_once_t initial = PTHREAD_ONCE_INIT;
        memcpy(&s->value.once, &initial, sizeof(initial));
    }
    if (error) return error;
    /* Never wrap a generation into a still-valid old handle. */
    if (generation >= 0x100000u) {
        if (kind == K_MUTEX) pthread_mutex_destroy(&s->value.mutex);
        if (kind == K_COND) {
            pthread_cond_destroy(&s->value.cond.realtime);
            pthread_cond_destroy(&s->value.cond.monotonic);
        }
        if (kind == K_SEM) sem_destroy(&s->value.sem);
        return EAGAIN;
    }
    uint32_t token = 0x80000000u | (generation++ << 11) | i;
    s->kind = kind;
    atomic_store_explicit(&s->token, token, memory_order_release);
    word_store(word, token);
    return 0;
}
static sync_slot *get(uint32_t *word, unsigned kind, int *error) {
    if (!word) { *error = EINVAL; return NULL; }
    uint32_t value = word_load(word);
    sync_slot *s = lookup(value, kind);
    if (s) return s;
    /* Lazy materialization of Android's static initializers. */
    int valid = (kind == K_MUTEX && (value == 0 || value == 0x4000 || value == 0x8000)) ||
                ((kind == K_COND || kind == K_ONCE) && value == 0);
    if (!valid) { *error = EINVAL; return NULL; }
    pthread_mutex_lock(&table_lock);
    value = word_load(word);
    s = lookup(value, kind);
    if (!s) {
        valid = (kind == K_MUTEX && (value == 0 || value == 0x4000 || value == 0x8000)) ||
                ((kind == K_COND || kind == K_ONCE) && value == 0);
        *error = valid ? initialize(word, kind, kind == K_MUTEX ? value >> 14 : 0) : EINVAL;
        if (!*error) s = lookup(word_load(word), kind);
    }
    pthread_mutex_unlock(&table_lock);
    return s;
}
static int init(uint32_t *word, unsigned kind, uint32_t options) {
    if (!word) return EINVAL;
    pthread_mutex_lock(&table_lock);
    int error = initialize(word, kind, options);
    pthread_mutex_unlock(&table_lock);
    return error;
}
static int destroy(uint32_t *word, unsigned kind) {
    int error = 0;
    sync_slot *s = get(word, kind, &error);
    if (!s) return error;
    pthread_mutex_lock(&table_lock);
    if (kind == K_MUTEX) error = pthread_mutex_destroy(&s->value.mutex);
    if (kind == K_COND) {
        error = pthread_cond_destroy(&s->value.cond.realtime);
        if (!error) error = pthread_cond_destroy(&s->value.cond.monotonic);
    }
    if (kind == K_SEM && sem_destroy(&s->value.sem)) error = errno;
    if (!error) {
        word_store(word, 0xffffffffu);
        atomic_store_explicit(&s->token, 0, memory_order_release);
    }
    pthread_mutex_unlock(&table_lock);
    return error;
}
int ws_bionic_mutexattr_init(uint32_t *a) { if (!a) return 22; *a = 0; return 0; }
int ws_bionic_mutexattr_destroy(uint32_t *a) { if (!a) return 22; *a = UINT32_MAX; return 0; }
int ws_bionic_mutexattr_settype(uint32_t *a, int type) {
    if (!a || *a > 2 || type < 0 || type > 2) return 22;
    *a = (uint32_t)type; return 0;
}
int ws_bionic_mutex_init(ws_bionic_mutex *m, const uint32_t *a) {
    if (a && *a > 2) return 22;
    return result(init(m, K_MUTEX, a ? *a : 0));
}
int ws_bionic_mutex_destroy(ws_bionic_mutex *m) { return result(destroy(m, K_MUTEX)); }
#define MUTEX_OP(name) \
int ws_bionic_mutex_##name(ws_bionic_mutex *m) { \
    int e = 0; sync_slot *s = get(m, K_MUTEX, &e); \
    return result(s ? pthread_mutex_##name(&s->value.mutex) : e); \
}
MUTEX_OP(lock)
MUTEX_OP(trylock)
MUTEX_OP(unlock)
int ws_bionic_condattr_init(uint32_t *a) { if (!a) return 22; *a = 0; return 0; }
int ws_bionic_condattr_destroy(uint32_t *a) { if (!a) return 22; *a = UINT32_MAX; return 0; }
int ws_bionic_condattr_setclock(uint32_t *a, int clock) {
    /* Android clock ids are 0 and 1; these differ from newlib's constants. */
    if (!a || (*a != 0 && *a != 2) || (clock != 0 && clock != 1)) return 22;
    *a = (uint32_t)clock << 1; return 0;
}
int ws_bionic_condattr_getclock(const uint32_t *a, int *clock) {
    if (!a || !clock || (*a != 0 && *a != 2)) return 22;
    *clock = (int)(*a >> 1); return 0;
}
int ws_bionic_cond_init(ws_bionic_cond *c, const uint32_t *a) {
    if (a && *a != 0 && *a != 2) return 22;
    return result(init(c, K_COND, a ? *a >> 1 : 0));
}
int ws_bionic_cond_destroy(ws_bionic_cond *c) { return result(destroy(c, K_COND)); }
static int wake(ws_bionic_cond *c, int all) {
    int e = 0; sync_slot *s = get(c, K_COND, &e);
    if (!s) return result(e);
    int (*fn)(pthread_cond_t *) = all ? pthread_cond_broadcast : pthread_cond_signal;
    int a = fn(&s->value.cond.realtime), b = fn(&s->value.cond.monotonic);
    /* Waking one waiter on each clock may give a spurious wakeup, permitted
     * by pthread_cond_signal. Each waiter atomically releases its mutex. */
    return result(a ? a : b);
}
int ws_bionic_cond_signal(ws_bionic_cond *c) { return wake(c, 0); }
int ws_bionic_cond_broadcast(ws_bionic_cond *c) { return wake(c, 1); }
static int convert_time(const ws_bionic_timespec *t, struct timespec *out) {
    if (!t || t->nanoseconds < 0 || t->nanoseconds >= 1000000000) return EINVAL;
    out->tv_sec = t->seconds; out->tv_nsec = t->nanoseconds;
    return 0;
}
static int wait_cond(ws_bionic_cond *c, ws_bionic_mutex *m,
                     const ws_bionic_timespec *t, int timed, int force_monotonic) {
    struct timespec native;
    if (timed && convert_time(t, &native)) return 22;
    int e = 0; sync_slot *cs = get(c, K_COND, &e), *ms = get(m, K_MUTEX, &e);
    if (!cs || !ms) return result(e);
    pthread_cond_t *cv = (force_monotonic || cs->value.cond.clock) ?
        &cs->value.cond.monotonic : &cs->value.cond.realtime;
#ifdef _WIN32
    /* mingw-w64's pthread_condattr_setclock rejects CLOCK_MONOTONIC.
     * Its relative wait uses a Windows timeout, independent of wall time. */
    if (timed && (force_monotonic || cs->value.cond.clock)) {
        struct timespec now, relative;
        if (clock_gettime(CLOCK_MONOTONIC, &now)) return result(errno);
        relative.tv_sec = native.tv_sec - now.tv_sec;
        relative.tv_nsec = native.tv_nsec - now.tv_nsec;
        if (relative.tv_nsec < 0) { --relative.tv_sec; relative.tv_nsec += 1000000000; }
        if (relative.tv_sec < 0) relative.tv_sec = relative.tv_nsec = 0;
        return result(pthread_cond_timedwait_relative_np(cv, &ms->value.mutex, &relative));
    }
#endif
    return result(timed ? pthread_cond_timedwait(cv, &ms->value.mutex, &native) :
                         pthread_cond_wait(cv, &ms->value.mutex));
}
int ws_bionic_cond_wait(ws_bionic_cond *c, ws_bionic_mutex *m) {
    return wait_cond(c, m, NULL, 0, 0);
}
int ws_bionic_cond_timedwait(ws_bionic_cond *c, ws_bionic_mutex *m,
                            const ws_bionic_timespec *t) { return wait_cond(c, m, t, 1, 0); }
int ws_bionic_cond_timedwait_monotonic_np(ws_bionic_cond *c, ws_bionic_mutex *m,
                            const ws_bionic_timespec *t) { return wait_cond(c, m, t, 1, 1); }
static int relative_wait(ws_bionic_cond *c,ws_bionic_mutex *m,const ws_bionic_timespec *t) {
    if(!t || t->seconds<0 || t->nanoseconds<0 || t->nanoseconds>=1000000000)return 22;
    struct timespec now;
    if(clock_gettime(CLOCK_MONOTONIC,&now))return result(errno);
    int64_t s=(int64_t)now.tv_sec+t->seconds;
    long n=now.tv_nsec+t->nanoseconds;
    if(n>=1000000000){++s;n-=1000000000;}
    if(s>INT32_MAX)return 75;
    ws_bionic_timespec until={(int32_t)s,(int32_t)n};
    return wait_cond(c,m,&until,1,1);
}
static int mutexattr_shared(uint32_t *a,int shared) {
    if(!a || *a>2 || (shared!=0&&shared!=1))return 22;
    return shared?95:0;
}
static int condattr_shared(uint32_t *a,int shared) {
    if(!a || (*a!=0&&*a!=2) || (shared!=0&&shared!=1))return 22;
    return shared?95:0;
}
int ws_bionic_sem_init(ws_bionic_sem *s, int shared, unsigned value) {
    if (shared) return sem_error(ENOSYS);
    int e = value > 0x7fffffffu ? EINVAL : init(s, K_SEM, value);
    return e ? sem_error(e) : 0;
}
int ws_bionic_sem_destroy(ws_bionic_sem *s) {
    int e = destroy(s, K_SEM); return e ? sem_error(e) : 0;
}
#define SEM_OP(name) \
int ws_bionic_sem_##name(ws_bionic_sem *word) { \
    int e = 0; sync_slot *s = get(word, K_SEM, &e); \
    if (!s) return sem_error(e); \
    return sem_##name(&s->value.sem) ? sem_error(errno) : 0; \
}
SEM_OP(wait)
SEM_OP(trywait)
SEM_OP(post)
int ws_bionic_sem_getvalue(ws_bionic_sem *word, int *value) {
    int e = 0; sync_slot *s = get(word, K_SEM, &e);
    if (!s || !value) return sem_error(s ? EINVAL : e);
    return sem_getvalue(&s->value.sem, value) ? sem_error(errno) : 0;
}
int ws_bionic_sem_timedwait(ws_bionic_sem *word, const ws_bionic_timespec *t) {
    struct timespec native;
    if (convert_time(t, &native)) return sem_error(EINVAL);
    int e = 0; sync_slot *s = get(word, K_SEM, &e);
    if (!s) return sem_error(e);
    return sem_timedwait(&s->value.sem, &native) ? sem_error(errno) : 0;
}
int ws_bionic_pthread_once(ws_bionic_once *word, void (*fn)(void)) {
    if (!fn) return 22;
    int e = 0; sync_slot *s = get(word, K_ONCE, &e);
    return result(s ? pthread_once(&s->value.once, fn) : e);
}
uintptr_t ws_bionic_sync_symbol(const char *name) {
    if (!name) return 0;
    if(!strcmp(name,"pthread_mutexattr_setpshared"))return (uintptr_t)mutexattr_shared;
    if(!strcmp(name,"pthread_condattr_setpshared"))return (uintptr_t)condattr_shared;
    if(!strcmp(name,"pthread_cond_timedwait_relative_np"))return (uintptr_t)relative_wait;
    if(!strcmp(name,"pthread_cond_timedwait_monotonic"))return (uintptr_t)ws_bionic_cond_timedwait_monotonic_np;
#define S(prefix, n) if (!strcmp(name, #prefix #n)) return (uintptr_t)ws_bionic_##n
    S(pthread_, mutexattr_init); S(pthread_, mutexattr_destroy); S(pthread_, mutexattr_settype);
    S(pthread_, mutex_init); S(pthread_, mutex_destroy); S(pthread_, mutex_lock);
    S(pthread_, mutex_trylock); S(pthread_, mutex_unlock);
    S(pthread_, condattr_init); S(pthread_, condattr_destroy);
    S(pthread_, condattr_setclock); S(pthread_, condattr_getclock);
    S(pthread_, cond_init); S(pthread_, cond_destroy); S(pthread_, cond_signal);
    S(pthread_, cond_broadcast); S(pthread_, cond_wait); S(pthread_, cond_timedwait);
    S(pthread_, cond_timedwait_monotonic_np);
#undef S
#define S(n) if (!strcmp(name, #n)) return (uintptr_t)ws_bionic_##n
    S(pthread_once); S(sem_init); S(sem_destroy); S(sem_wait); S(sem_trywait);
    S(sem_timedwait); S(sem_post); S(sem_getvalue);
#undef S
    return 0;
}
