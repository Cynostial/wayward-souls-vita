#ifndef WS_BIONIC_THREADS_H
#define WS_BIONIC_THREADS_H
#include <stdint.h>
#include <stddef.h>
typedef uint32_t ws_bionic_thread;
typedef struct {
    uint32_t flags, stack_base, stack_size, guard_size;
    int32_t sched_policy, sched_priority;
} ws_bionic_thread_attr;
typedef struct {
    ws_bionic_thread thread;
    int32_t vita_uid;
    uintptr_t stack_base;
    size_t stack_size;
} ws_thread_identity;
int ws_bionic_attr_init(ws_bionic_thread_attr *attr);
int ws_bionic_attr_destroy(ws_bionic_thread_attr *attr);
int ws_bionic_attr_setdetachstate(ws_bionic_thread_attr *attr, int state);
int ws_bionic_attr_getdetachstate(const ws_bionic_thread_attr *attr, int *state);
int ws_bionic_attr_setstacksize(ws_bionic_thread_attr *attr, uint32_t size);
int ws_bionic_attr_getstack(const ws_bionic_thread_attr *attr, uint32_t *base, uint32_t *size);
int ws_bionic_attr_setschedpolicy(ws_bionic_thread_attr *attr, int policy);
int ws_bionic_attr_setschedparam(ws_bionic_thread_attr *attr, const int32_t *priority);
int ws_bionic_pthread_create(ws_bionic_thread *thread, const ws_bionic_thread_attr *attr,
                             void *(*start)(void *), void *arg);
ws_bionic_thread ws_bionic_pthread_self(void);
int ws_bionic_pthread_equal(ws_bionic_thread a, ws_bionic_thread b);
int ws_bionic_pthread_join(ws_bionic_thread thread, void **value);
int ws_bionic_pthread_detach(ws_bionic_thread thread);
_Noreturn void ws_bionic_pthread_exit(void *value);
int ws_bionic_pthread_getattr_np(ws_bionic_thread thread, ws_bionic_thread_attr *attr);
int ws_bionic_pthread_key_create(uint32_t *key, void (*destroy)(void *));
int ws_bionic_pthread_key_delete(uint32_t key);
void *ws_bionic_pthread_getspecific(uint32_t key);
int ws_bionic_pthread_setspecific(uint32_t key, const void *value);
/* Capture the current thread's actual stack bounds and Vita UID. This records
 * identity only, not registers, roots or signal/suspension support. */
int ws_bionic_current_thread(ws_thread_identity *identity);
/* Reclaim completed logically detached threads after native TLS destructors.
 * A final launcher teardown may call this after all game threads have exited. */
int ws_bionic_threads_reap(void);
uintptr_t ws_bionic_threads_symbol(const char *name);
/* Only libv's bundled rwlocks use __get_thread; the recovered ARM access is
 * the nonzero thread id at +0x20. This is not a native pthread_t pointer. */
void *ws_bionic_get_thread(void);
size_t ws_thread_stack_bytes(void);
#endif
