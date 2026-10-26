#define _GNU_SOURCE
#include "ws_threads.h"
#include "ws_io.h"
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

_Static_assert(sizeof(ws_bionic_thread_attr) == 24, "Android ARM pthread attributes");
#define THREAD_COUNT 256u
#define KEY_COUNT 256u
#define STACK_BUDGET (16u*1024u*1024u)
typedef struct {
    uint32_t id;
    pthread_t native;
    ws_bionic_thread_attr attr;
    ws_thread_identity identity;
    void *(*start)(void *);
    void *arg;
    int ready, exiting, claimed, detached, external, creating;
    uint32_t bionic_prefix[9];
    size_t stack_charge;
} thread_record;
typedef struct { uint32_t id; pthread_key_t native; } key_record;
static thread_record threads[THREAD_COUNT];
static key_record keys[KEY_COUNT];
static pthread_mutex_t registry = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static uint32_t thread_generation = 1, key_generation = 1;
static size_t stack_bytes;
static void retire(thread_record *r) {
    stack_bytes-=r->stack_charge;
    memset(r,0,sizeof(*r));
}
size_t ws_thread_stack_bytes(void) {
    pthread_mutex_lock(&registry);size_t n=stack_bytes;
    pthread_mutex_unlock(&registry);return n;
}
/* GCC emulated TLS may be torn down before a game's pthread TLS destructors.
 * Keep identity in a native key without a destructor instead. */
static pthread_once_t identity_once = PTHREAD_ONCE_INIT;
static pthread_key_t identity_key;
static int identity_error;
static void init_identity(void) { identity_error = pthread_key_create(&identity_key, NULL); }
static thread_record *current_record(void) {
    pthread_once(&identity_once, init_identity);
    return identity_error ? NULL : pthread_getspecific(identity_key);
}
static int translated(int error) { return ws_bionic_error_from_native(error); }

int ws_bionic_attr_init(ws_bionic_thread_attr *a) {
    if (!a) return 22;
    *a = (ws_bionic_thread_attr){0, 0, 1024 * 1024, 4096, 0, 0};
#ifdef __vita__
    /* VitaSDK pthreads accepts guard size zero only. This port's default
     * does not promise an Android-style guard page. */
    a->guard_size = 0;
#endif
    return 0;
}
static int valid_attr(const ws_bionic_thread_attr *a) {
    return a && !(a->flags & ~1u) && a->stack_size >= 8192;
}
int ws_bionic_attr_destroy(ws_bionic_thread_attr *a) {
    if (!a) return 22;
    memset(a, 0x42, sizeof(*a)); return 0;
}
int ws_bionic_attr_setdetachstate(ws_bionic_thread_attr *a, int state) {
    if (!valid_attr(a) || (state != 0 && state != 1)) return 22;
    a->flags = (a->flags & ~1u) | (uint32_t)state; return 0;
}
int ws_bionic_attr_getdetachstate(const ws_bionic_thread_attr *a, int *state) {
    if (!valid_attr(a) || !state) return 22;
    *state = (int)(a->flags & 1u); return 0;
}
int ws_bionic_attr_setstacksize(ws_bionic_thread_attr *a, uint32_t size) {
    if (!valid_attr(a) || size < 8192) return 22;
    a->stack_size = size; return 0;
}
int ws_bionic_attr_getstack(const ws_bionic_thread_attr *a, uint32_t *base, uint32_t *size) {
    if (!valid_attr(a) || !base || !size) return 22;
    *base = a->stack_base; *size = a->stack_size; return 0;
}
int ws_bionic_attr_setschedpolicy(ws_bionic_thread_attr *a, int policy) {
    if (!valid_attr(a) || policy < 0 || policy > 2) return 22;
    a->sched_policy = policy; return 0;
}
int ws_bionic_attr_setschedparam(ws_bionic_thread_attr *a, const int32_t *priority) {
    if (!valid_attr(a) || !priority || *priority < 0 || *priority > 99) return 22;
    a->sched_priority = *priority; return 0;
}
static thread_record *find(uint32_t id) {
    if (!(id & 0x80000000u)) return NULL;
    thread_record *r = &threads[id & (THREAD_COUNT - 1)];
    return r->id == id ? r : NULL;
}
static thread_record *reserve(void) {
    if (thread_generation >= 0x800000u) return NULL;
    for (unsigned i = 0; i < THREAD_COUNT; ++i) if (!threads[i].id) {
        thread_record *r = &threads[i];
        memset(r, 0, sizeof(*r));
        r->id = 0x80000000u | (thread_generation++ << 8) | i;
        r->identity.thread = r->id;
        r->bionic_prefix[8] = r->id;
        return r;
    }
    return NULL;
}
static int capture_identity(thread_record *r) {
#ifdef __vita__
    SceKernelThreadInfo info;
    memset(&info, 0, sizeof(info)); info.size = sizeof(info);
    r->identity.vita_uid = sceKernelGetThreadId();
    if (sceKernelGetThreadInfo(r->identity.vita_uid, &info) < 0) return EIO;
    r->identity.stack_base = (uintptr_t)info.stack;
    r->identity.stack_size = info.stackSize;
#elif defined(_WIN32)
    const NT_TIB *tib = (const NT_TIB *)NtCurrentTeb();
    r->identity.stack_base = (uintptr_t)tib->StackLimit;
    r->identity.stack_size = (uintptr_t)tib->StackBase - (uintptr_t)tib->StackLimit;
    /* Windows StackLimit describes committed pages; this host-only value
     * must not be mistaken for Vita's full allocation or GC root bounds. */
#else
    pthread_attr_t a;
    int e = pthread_getattr_np(pthread_self(), &a);
    if (e) return e;
    void *base; size_t size;
    e = pthread_attr_getstack(&a, &base, &size);
    pthread_attr_destroy(&a);
    if (e) return e;
    r->identity.stack_base = (uintptr_t)base;
    r->identity.stack_size = size;
#endif
    return 0;
}
static void mark_exiting(void *context) {
    thread_record *r = context;
    pthread_mutex_lock(&registry);
    r->exiting = 1;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&registry);
    /* current stays valid until native TLS destructors finish. */
}
static void *run_thread(void *context) {
    thread_record *r = context;
    int identity_set_error = pthread_setspecific(identity_key, r);
    pthread_mutex_lock(&registry);
    int e = identity_set_error ? identity_set_error : capture_identity(r);
    r->ready = e ? -e : 1;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&registry);
    void *value = NULL;
    pthread_cleanup_push(mark_exiting, r);
    if (!e) value = r->start(r->arg);
    pthread_cleanup_pop(1);
    return value;
}
int ws_bionic_threads_reap(void) {
    int count = 0;
    thread_record *current = current_record();
    for (;;) {
        thread_record *r = NULL;
        pthread_mutex_lock(&registry);
        for (unsigned i = 0; i < THREAD_COUNT; ++i) {
            thread_record *candidate = &threads[i];
            if (candidate->id && candidate->detached && candidate->exiting &&
                !candidate->claimed && !candidate->external && !candidate->creating && candidate != current) {
                r = candidate; r->claimed = 1; break;
            }
        }
        pthread_mutex_unlock(&registry);
        if (!r) return count;
        /* Join outside the registry: a TLS destructor may call pthread_self
         * or use another guest thread primitive before native exit finishes. */
        int e = pthread_join(r->native, NULL);
        pthread_mutex_lock(&registry);
        if (!e) retire(r); else r->claimed = 0;
        pthread_mutex_unlock(&registry);
        if (e) return -translated(e);
        ++count;
    }
}
int ws_bionic_pthread_create(ws_bionic_thread *out, const ws_bionic_thread_attr *a,
                             void *(*start)(void *), void *arg) {
    if (!out || !start || (a && !valid_attr(a))) return 22;
    pthread_once(&identity_once, init_identity);
    if (identity_error) return translated(identity_error);
    ws_bionic_thread_attr requested;
    if (a) requested = *a; else ws_bionic_attr_init(&requested);
    /* A custom Linux scheduler/stack cannot silently become a default Vita
     * thread. Add an explicit policy mapping before accepting these values. */
    if (requested.stack_base || requested.sched_policy || requested.sched_priority) return 95;
    pthread_attr_t native;
    int e = pthread_attr_init(&native);
    if (e) return translated(e);
    e = pthread_attr_setdetachstate(&native, PTHREAD_CREATE_JOINABLE);
    if (!e) e = pthread_attr_setstacksize(&native, requested.stack_size);
#ifndef _WIN32
    if (!e) e = pthread_attr_setguardsize(&native, requested.guard_size);
#else
    /* Windows owns the guard page; winpthreads has no setter. */
    if (!e && requested.guard_size != 4096) e = ENOTSUP;
#endif
    if (e) { pthread_attr_destroy(&native); return translated(e); }
    ws_bionic_threads_reap();
    pthread_mutex_lock(&registry);
    size_t charge=((size_t)requested.stack_size+4095u)&~(size_t)4095u;
    if(charge<requested.stack_size || requested.guard_size>STACK_BUDGET ||
       charge>STACK_BUDGET-requested.guard_size ||
       charge+requested.guard_size>STACK_BUDGET-stack_bytes) {
        pthread_mutex_unlock(&registry);pthread_attr_destroy(&native);return 12;
    }
    charge+=requested.guard_size;
    thread_record *r = reserve();
    if (!r) e = EAGAIN;
    else {
        r->attr = requested; r->detached = requested.flags & 1; r->creating = 1;
        r->stack_charge=charge;stack_bytes+=charge;
        r->start = start; r->arg = arg;
        e = pthread_create(&r->native, &native, run_thread, r);
        if (e) retire(r);
        else {
            while (!r->ready) pthread_cond_wait(&changed, &registry);
            if (r->ready < 0) { e = -r->ready; r->detached = 1; }
            else *out = r->id;
            r->creating = 0;
        }
    }
    pthread_mutex_unlock(&registry);
    pthread_attr_destroy(&native);
    if (e) ws_bionic_threads_reap();
    return translated(e);
}
ws_bionic_thread ws_bionic_pthread_self(void) {
    thread_record *current = current_record();
    if (current) return current->id;
    if (identity_error) return 0;
    pthread_mutex_lock(&registry);
    /* Some pthread implementations clear key values before destructors.
     * A retained native handle still identifies this thread until join. */
    pthread_t self = pthread_self();
    for (unsigned i = 0; i < THREAD_COUNT; ++i) {
        if (threads[i].id && pthread_equal(threads[i].native, self)) {
            current = &threads[i]; break;
        }
    }
    thread_record *r = current ? NULL : reserve();
    if (r) {
        r->native = pthread_self(); r->external = 1; r->detached = 1;
        ws_bionic_attr_init(&r->attr);
        int e = capture_identity(r);
        if (e) memset(r, 0, sizeof(*r));
        else { r->ready = 1; current = r; }
    }
    if (current && pthread_setspecific(identity_key, current)) current = NULL;
    pthread_mutex_unlock(&registry);
    /* Foreign host/Vita threads retain identity records for process lifetime.
     * The launcher must register itself before loading game constructors. */
    return current ? current->id : 0;
}
int ws_bionic_pthread_equal(ws_bionic_thread a, ws_bionic_thread b) { return a == b; }
int ws_bionic_pthread_join(ws_bionic_thread id, void **value) {
    thread_record *current = current_record();
    pthread_mutex_lock(&registry);
    thread_record *r = find(id);
    int e = !r ? ESRCH : r == current ? EDEADLK :
            (r->claimed || r->detached || r->external) ? EINVAL : 0;
    if (!e) r->claimed = 1;
    pthread_mutex_unlock(&registry);
    if (e) return translated(e);
    e = pthread_join(r->native, value);
    pthread_mutex_lock(&registry);
    if (!e) retire(r); else r->claimed = 0;
    pthread_mutex_unlock(&registry);
    return translated(e);
}
int ws_bionic_pthread_detach(ws_bionic_thread id) {
    pthread_mutex_lock(&registry);
    thread_record *r = find(id);
    int e = !r ? ESRCH : (r->detached || r->claimed || r->external) ? EINVAL : 0;
    if (!e) { r->detached = 1; r->attr.flags |= 1; }
    pthread_mutex_unlock(&registry);
    if (!e) ws_bionic_threads_reap();
    return translated(e);
}
_Noreturn void ws_bionic_pthread_exit(void *value) { pthread_exit(value); abort(); }
int ws_bionic_pthread_getattr_np(ws_bionic_thread id, ws_bionic_thread_attr *a) {
    if (!a) return 22;
    pthread_mutex_lock(&registry);
    thread_record *r = find(id);
    int e = !r ? ESRCH : (r->identity.stack_base > UINT32_MAX ||
                         r->identity.stack_size > UINT32_MAX) ? EOVERFLOW : 0;
    if (!e) {
        *a = r->attr;
        a->stack_base = (uint32_t)r->identity.stack_base;
        a->stack_size = (uint32_t)r->identity.stack_size;
    }
    pthread_mutex_unlock(&registry);
    return translated(e);
}
int ws_bionic_current_thread(ws_thread_identity *out) {
    if (!out || !ws_bionic_pthread_self()) return 22;
    *out = current_record()->identity; return 0;
}
void *ws_bionic_get_thread(void) {
    if(!ws_bionic_pthread_self())abort();
    return current_record()->bionic_prefix;
}
static int attr_getstacksize(const ws_bionic_thread_attr *a,uint32_t *v) {
    if(!valid_attr(a)||!v)return 22;
    *v=a->stack_size;return 0;
}
static int attr_getstackaddr(const ws_bionic_thread_attr *a,uint32_t *v) {
    if(!valid_attr(a)||!v)return 22;
    *v=a->stack_base;return 0;
}
static int attr_setstackaddr(ws_bionic_thread_attr *a,uint32_t v) {
    if(!valid_attr(a))return 22;
    a->stack_base=v;return 0;
}
static int attr_setstack(ws_bionic_thread_attr *a,uint32_t v,uint32_t size) {
    if(!valid_attr(a)||size<8192)return 22;
    a->stack_base=v;a->stack_size=size;return 0;
}
static int attr_getguardsize(const ws_bionic_thread_attr *a,uint32_t *v) {
    if(!valid_attr(a)||!v)return 22;
    *v=a->guard_size;return 0;
}
static int attr_setguardsize(ws_bionic_thread_attr *a,uint32_t v) {
    if(!valid_attr(a))return 22;
    a->guard_size=v;return 0;
}
static int attr_getschedpolicy(const ws_bionic_thread_attr *a,int32_t *v) {
    if(!valid_attr(a)||!v)return 22;
    *v=a->sched_policy;return 0;
}
static int attr_getschedparam(const ws_bionic_thread_attr *a,int32_t *v) {
    if(!valid_attr(a)||!v)return 22;
    *v=a->sched_priority;return 0;
}
static int attr_setscope(ws_bionic_thread_attr *a,int v) {
    if(!valid_attr(a)||(v!=0&&v!=1))return 22;
    return v==0?0:95;
}
static key_record *find_key(uint32_t id) {
    if (!(id & 0x80000000u)) return NULL;
    key_record *r = &keys[id & (KEY_COUNT - 1)];
    return r->id == id ? r : NULL;
}
int ws_bionic_pthread_key_create(uint32_t *out, void (*destroy)(void *)) {
    if (!out) return 22;
    pthread_mutex_lock(&registry);
    int e = EAGAIN;
    if (key_generation < 0x800000u) for (unsigned i = 0; i < KEY_COUNT; ++i) if (!keys[i].id) {
        e = pthread_key_create(&keys[i].native, destroy);
        if (!e) *out = keys[i].id = 0x80000000u | (key_generation++ << 8) | i;
        break;
    }
    pthread_mutex_unlock(&registry);
    return translated(e);
}
int ws_bionic_pthread_key_delete(uint32_t key) {
    pthread_mutex_lock(&registry);
    key_record *r = find_key(key);
    int e = r ? pthread_key_delete(r->native) : EINVAL;
    if (!e) r->id = 0;
    pthread_mutex_unlock(&registry);
    return translated(e);
}
void *ws_bionic_pthread_getspecific(uint32_t key) {
    pthread_mutex_lock(&registry);
    key_record *r = find_key(key);
    void *value = r ? pthread_getspecific(r->native) : NULL;
    pthread_mutex_unlock(&registry);
    return value;
}
int ws_bionic_pthread_setspecific(uint32_t key, const void *value) {
    pthread_mutex_lock(&registry);
    key_record *r = find_key(key);
    int e = r ? pthread_setspecific(r->native, value) : EINVAL;
    pthread_mutex_unlock(&registry);
    return translated(e);
}
uintptr_t ws_bionic_threads_symbol(const char *name) {
    if (!name) return 0;
    if(!strcmp(name,"__get_thread"))return (uintptr_t)ws_bionic_get_thread;
#define X(n) if(!strcmp(name,"pthread_attr_" #n))return (uintptr_t)attr_##n
    X(getstacksize);X(getstackaddr);X(setstackaddr);X(setstack);X(getguardsize);
    X(setguardsize);X(getschedpolicy);X(getschedparam);X(setscope);
#undef X
#define A(n) if (!strcmp(name, "pthread_attr_" #n)) return (uintptr_t)ws_bionic_attr_##n
    A(init); A(destroy); A(setdetachstate); A(getdetachstate); A(setstacksize);
    A(getstack); A(setschedpolicy); A(setschedparam);
#undef A
#define T(n) if (!strcmp(name, "pthread_" #n)) return (uintptr_t)ws_bionic_pthread_##n
    T(create); T(self); T(equal); T(join); T(detach); T(exit); T(getattr_np);
    T(key_create); T(key_delete); T(getspecific); T(setspecific);
#undef T
    return 0;
}
