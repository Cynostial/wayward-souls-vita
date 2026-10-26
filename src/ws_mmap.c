#define _GNU_SOURCE
#include "ws_mmap.h"
#include "ws_memory.h"
#include "ws_io.h"
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#ifdef __vita__
#include <psp2/kernel/sysmem.h>
#include <kubridge.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#define PAGE 4096u
#define MAP_LIMIT 128
#define EXEC_LIMIT (1024u*1024u)
#define WS_MAP_FAILED ((void *)(intptr_t)-1)
typedef struct {void *base;size_t bytes;int fd,prot,flags;int32_t offset;int executable_block;} mapping;
static mapping maps[MAP_LIMIT];
static size_t executable_bytes;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static void *fail(int e){*ws_bionic_errno_location()=ws_bionic_error_from_native(e);return WS_MAP_FAILED;}
static mapping *lookup(void *p,size_t size) {
    for(unsigned i=0;i<MAP_LIMIT;++i)if(maps[i].base==p&&maps[i].bytes==size)return &maps[i];
    return NULL;
}
/* libSystem's imp_implementationWithBlock emits ARM trampolines into shared
 * anonymous RWX pages, then calls cacheflush. Keep these outside the TLSF heap:
 * changing heap-page permissions would also affect adjacent allocator blocks.
 * All callers hold lock, including the separate 1 MiB budget and cleanup. */
static void *executable_allocate(size_t bytes,int *block) {
    if(bytes>EXEC_LIMIT-executable_bytes)return NULL;
#ifdef __vita__
    SceUID uid=sceKernelAllocMemBlock("wayward objc code",SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,bytes,NULL);
    if(uid<0)return NULL;
    void *p=NULL;
    if(sceKernelGetMemBlockBase(uid,&p)<0||!p||
       kuKernelMemProtect(p,bytes,KU_KERNEL_PROT_READ|KU_KERNEL_PROT_WRITE|KU_KERNEL_PROT_EXEC)<0){
        sceKernelFreeMemBlock(uid);return NULL;
    }
    *block=uid;
#elif defined(_WIN32)
    void *p=VirtualAlloc(NULL,bytes,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
    if(!p)return NULL;
    *block=0;
#else
    void *p=mmap(NULL,bytes,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(p==MAP_FAILED)return NULL;
    *block=0;
#endif
    executable_bytes+=bytes;return p;
}
static int executable_release(mapping *m) {
#ifdef __vita__
    if(sceKernelFreeMemBlock(m->executable_block)<0)return -1;
#elif defined(_WIN32)
    if(!VirtualFree(m->base,0,MEM_RELEASE))return -1;
#else
    if(munmap(m->base,m->bytes))return -1;
#endif
    executable_bytes-=m->bytes;return 0;
}
size_t ws_mmap_executable_bytes(void) {
    pthread_mutex_lock(&lock);size_t bytes=executable_bytes;pthread_mutex_unlock(&lock);return bytes;
}
void *ws_mmap(void *hint,size_t bytes,int prot,int flags,int fd,int32_t offset) {
    (void)hint;
    if(!bytes||bytes>SIZE_MAX-(PAGE-1)||offset<0||(offset&(PAGE-1))||(prot&~7))return fail(EINVAL);
    if(!(flags&3)||(flags&3)==3)return fail(EINVAL);
    /* File mappings are snapshots: shared writable files need a writeback
     * implementation. Anonymous MAP_SHARED is valid in this single-process
     * runtime: all guest threads see the same allocation, and fork is absent.
     * libv uses it when mapping compressed Foundation resources from the APK. */
    /* MAP_NORESERVE is a Linux swap-reservation hint. Vita has no swap: these
     * pages are still eagerly committed and charged against the fixed heap. */
    if((flags&~(1|2|0x20|0x4000))||((flags&1)&&(prot&2)&&!(flags&0x20)))return fail(ENOTSUP);
    if((prot&4)&&(prot!=7||!(flags&0x20)||fd!=-1||offset))return fail(ENOTSUP);
    size_t rounded=(bytes+PAGE-1)&~(size_t)(PAGE-1);
    if(rounded>INT32_MAX||(uint64_t)(uint32_t)offset+bytes>INT32_MAX)return fail(EOVERFLOW);
    pthread_mutex_lock(&lock);mapping *m=NULL;
    for(unsigned i=0;i<MAP_LIMIT;++i)if(!maps[i].base){m=&maps[i];break;}
    if(!m){pthread_mutex_unlock(&lock);return fail(ENOMEM);}
    void *p=NULL;int block=-1,error=0;
    if(prot&4){p=executable_allocate(rounded,&block);if(!p)error=ENOMEM;}
    else error=ws_posix_memalign(&p,PAGE,rounded);
    if(error){pthread_mutex_unlock(&lock);return fail(error);}
    memset(p,0,rounded);
    if(!(flags&0x20)) {
        if(fd<0)error=EBADF;
        size_t done=0;
        while(!error&&done<bytes) {
#ifdef _WIN32
            /* Host test backend; tests serialize this descriptor. Vita uses pread. */
            off_t original=lseek(fd,0,SEEK_CUR);ssize_t got=-1;
            if(original>=0&&lseek(fd,(off_t)offset+done,SEEK_SET)>=0)got=read(fd,(char *)p+done,bytes-done);
            int saved=errno;if(original>=0)lseek(fd,original,SEEK_SET);errno=saved;
#else
            ssize_t got=pread(fd,(char *)p+done,bytes-done,(off_t)offset+done);
#endif
            if(got<0){if(errno==EINTR)continue;error=errno;break;}
            if(!got)break;
            done+=(size_t)got;
        }
    }
    if(error){ws_free(p);pthread_mutex_unlock(&lock);return fail(error);}
    *m=(mapping){p,rounded,fd,prot,flags,offset,block};pthread_mutex_unlock(&lock);return p;
}
int ws_munmap(void *p,size_t bytes) {
    if(!bytes||bytes>SIZE_MAX-(PAGE-1)){fail(EINVAL);return -1;}
    bytes=(bytes+PAGE-1)&~(size_t)(PAGE-1);
    pthread_mutex_lock(&lock);mapping *m=lookup(p,bytes);
    if(!m){pthread_mutex_unlock(&lock);fail(EINVAL);return -1;}
    if(m->prot&4){
        if(executable_release(m)){pthread_mutex_unlock(&lock);fail(EIO);return -1;}
    }else ws_free(p);
    memset(m,0,sizeof(*m));pthread_mutex_unlock(&lock);return 0;
}
static int advise(void *p,size_t bytes,int advice) {
    (void)p;(void)bytes;
    /* Read-ahead hints do not change data. DONTNEED does, and is unsupported. */
    if(advice>=0&&advice<=3)return 0;
    fail(ENOTSUP);return -1;
}
static void *remap(void *p,size_t before,size_t after,int flags,...) {
    if(!before||!after||before>SIZE_MAX-(PAGE-1)||after>SIZE_MAX-(PAGE-1)||(flags&~1))return fail(EINVAL);
    size_t old=(before+PAGE-1)&~(size_t)(PAGE-1),size=(after+PAGE-1)&~(size_t)(PAGE-1);
    pthread_mutex_lock(&lock);mapping *m=lookup(p,old);
    if(!m){pthread_mutex_unlock(&lock);return fail(EINVAL);}
    if(size==old){pthread_mutex_unlock(&lock);return p;}
    if(m->prot&4){pthread_mutex_unlock(&lock);return fail(ENOTSUP);}
    if(!(flags&1)){pthread_mutex_unlock(&lock);return fail(ENOMEM);}
    if(!(m->flags&0x20)){pthread_mutex_unlock(&lock);return fail(ENOTSUP);}
    void *replacement=NULL;int error=ws_posix_memalign(&replacement,PAGE,size);
    if(error){pthread_mutex_unlock(&lock);return fail(error);}
    size_t copy=size<old?size:old;memcpy(replacement,p,copy);
    if(size>copy)memset((char *)replacement+copy,0,size-copy);
    ws_free(p);m->base=replacement;m->bytes=size;pthread_mutex_unlock(&lock);return replacement;
}
uintptr_t ws_mmap_symbol(const char *name) {
    if(!strcmp(name,"mmap"))return (uintptr_t)ws_mmap;
    if(!strcmp(name,"munmap"))return (uintptr_t)ws_munmap;
    if(!strcmp(name,"mremap"))return (uintptr_t)remap;
    if(!strcmp(name,"madvise"))return (uintptr_t)advise;
    return 0;
}
