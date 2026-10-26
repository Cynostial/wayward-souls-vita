#define _GNU_SOURCE
#include "ws_paths.h"
#include "ws_io.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <utime.h>
#include <fcntl.h>
#include <stdatomic.h>
#include "../vendor/aosp-fnmatch/ws_fnmatch.h"
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#include <io.h>
#else
#include <sys/statvfs.h>
#endif
#ifndef _WIN32
#include <sys/time.h>
#endif
static char root[WS_PATH_MAX];
static atomic_uint creation_mask=0022;
unsigned ws_path_creation_mode(unsigned mode){return mode&~atomic_load(&creation_mask);}
static unsigned fs_umask(unsigned mode){return atomic_exchange(&creation_mask,mode&0777);}
int ws_path_virtual_directory(const char *path){
    if(!path)return 0;
    size_t n=strlen(path);while(n>1&&path[n-1]=='/')--n;
    const char *names[]={"/","/data","/data/data","/data/data/com.noodlecake.waywardsouls"};
    for(size_t i=0;i<sizeof(names)/sizeof(*names);++i)if(strlen(names[i])==n&&!strncmp(path,names[i],n))return 1;
    return 0;
}
int ws_paths_init(const char *path) {
    if(!path||!*path||strlen(path)>=sizeof(root)||*root)return EINVAL;
    strcpy(root,path);size_t n=strlen(root);while(n>1&&root[n-1]=='/')root[--n]=0;return 0;
}
static const char *suffix(const char *path,const char *prefix) {
    size_t n=strlen(prefix);return !strncmp(path,prefix,n)&&(!path[n]||path[n]=='/')?path+n:NULL;
}
int ws_path_resolve(const char *path,char *out,size_t cap) {
    if(!path||!out||!*root)return EINVAL;
    const char *p=suffix(path,"/wayward");
    if(!p)p=suffix(path,"/data/data/com.noodlecake.waywardsouls/files");
    if(!p)p=suffix(path,root);
    if(!p){if(*path=='/'||strchr(path,':'))return ENOENT;p=path;}
    char relative[WS_PATH_MAX];size_t used=0,starts[128],depth=0;
    while(*p) {
        while(*p=='/')++p;
        const char *start=p;while(*p&&*p!='/')++p;size_t n=(size_t)(p-start);
        if(!n)break;
        if(n==1&&start[0]=='.')continue;
        if(n==2&&start[0]=='.'&&start[1]=='.'){if(!depth)return EACCES;used=starts[--depth];continue;}
        if(depth==128||n>=sizeof(relative)-used-1)return ENAMETOOLONG;
        if(memchr(start,'\\',n)||memchr(start,':',n))return EINVAL;
        starts[depth++]=used;relative[used++]='/';memcpy(relative+used,start,n);used+=n;
    }
    relative[used]=0;
    int len=snprintf(out,cap,"%s%s",root,relative);return len<0||(size_t)len>=cap?ENAMETOOLONG:0;
}
static int fail(int e){*ws_bionic_errno_location()=ws_bionic_error_from_native(e);return -1;}
static int result(int r){return r<0?fail(errno):r;}
#ifndef _WIN32
static void export_statfs(const struct statvfs *in,ws_statfs *out){
    *out=(ws_statfs){0};out->block_size=in->f_frsize?in->f_frsize:in->f_bsize;
    out->fragment_size=out->block_size;out->blocks=in->f_blocks;out->free_blocks=in->f_bfree;
    out->available_blocks=in->f_bavail;out->files=in->f_files;out->free_files=in->f_ffree;
    out->fsid[0]=in->f_fsid;out->name_max=in->f_namemax;
}
#else
static int disk_space(const char *path,ws_statfs *out){
    ULARGE_INTEGER available,total,free_space;
    if(!GetDiskFreeSpaceExA(path,&available,&total,&free_space))return fail(EIO);
    *out=(ws_statfs){.block_size=4096,.fragment_size=4096,.name_max=255};
    out->blocks=total.QuadPart/4096;out->free_blocks=free_space.QuadPart/4096;out->available_blocks=available.QuadPart/4096;return 0;
}
#endif
int ws_statfs_path(const char *p,ws_statfs *out){
    if(!out)return fail(EFAULT);
    char path[WS_PATH_MAX];int e=ws_path_resolve(p,path,sizeof(path));if(e)return fail(e);
#ifdef _WIN32
    struct stat s;if(stat(path,&s))return fail(errno);
    if(!(s.st_mode&S_IFDIR)){char *last=strrchr(path,'/');if(last)*last=0;else strcpy(path,".");}
    return disk_space(path,out);
#else
    struct statvfs s;if(statvfs(path,&s))return fail(errno);export_statfs(&s,out);return 0;
#endif
}
int ws_statfs_fd(int fd,ws_statfs *out){
    if(!out)return fail(EFAULT);
#ifdef _WIN32
    struct stat s;if(fstat(fd,&s))return fail(errno);
    char path[WS_PATH_MAX];HANDLE h=(HANDLE)_get_osfhandle(fd);
    DWORD size=GetFinalPathNameByHandleA(h,path,sizeof(path),FILE_NAME_NORMALIZED);
    if(!size||size>=sizeof(path))return fail(ENOTSUP);
    char *last=strrchr(path,'\\');if(last)*last=0;return disk_space(path,out);
#else
    struct statvfs s;if(fstatvfs(fd,&s))return fail(errno);export_statfs(&s,out);return 0;
#endif
}
static int fs_access(const char *p,int mode){
    if(mode&~7)return fail(EINVAL);
    if(ws_path_virtual_directory(p))return mode&2?fail(EACCES):0;
    char b[WS_PATH_MAX];int e=ws_path_resolve(p,b,sizeof(b));return e?fail(e):result(access(b,mode));
}
static int fs_mkdir(const char *p,unsigned mode){
    if(ws_path_virtual_directory(p))return fail(EEXIST);
    char b[WS_PATH_MAX];int e=ws_path_resolve(p,b,sizeof(b));if(e)return fail(e);
#ifdef _WIN32
    (void)mode;return result(_mkdir(b));
#else
    return result(mkdir(b,ws_path_creation_mode(mode)));
#endif
}
static int fs_unlink(const char *p){char b[WS_PATH_MAX];int e=ws_path_resolve(p,b,sizeof(b));return e?fail(e):result(unlink(b));}
static int fs_rmdir(const char *p){char b[WS_PATH_MAX];int e=ws_path_resolve(p,b,sizeof(b));return e?fail(e):result(rmdir(b));}
static int fs_remove(const char *p){char b[WS_PATH_MAX];int e=ws_path_resolve(p,b,sizeof(b));return e?fail(e):result(remove(b));}
static int fs_rename(const char *a,const char *b){char x[WS_PATH_MAX],y[WS_PATH_MAX];int e=ws_path_resolve(a,x,sizeof(x));if(!e)e=ws_path_resolve(b,y,sizeof(y));return e?fail(e):result(rename(x,y));}
static int fs_chmod(const char *p,unsigned mode){char b[WS_PATH_MAX];int e=ws_path_resolve(p,b,sizeof(b));return e?fail(e):result(chmod(b,mode));}
static int fs_creat(const char *p,unsigned mode){return ws_bionic_open(p,0x241,mode);}
static int fs_dup(int fd){return result(dup(fd));}
static int fs_dup2(int fd,int to){
    if(to>=768&&to<832)return fail(EBADF);
#ifdef __vita__
    if(fd==to)return fcntl(fd,F_GETFD)<0?fail(errno):fd;
    /* VitaSDK has no dup2 implementation. Do not close the requested target
     * or return an unrelated descriptor as though the operation succeeded. */
    return fail(ENOTSUP);
#else
    return result(dup2(fd,to));
#endif
}
static int fs_isatty(int fd){int r=isatty(fd);if(!r)fail(errno);return r;}
static int fs_fsync(int fd){
#ifdef _WIN32
    return result(_commit(fd));
#else
    return result(fsync(fd));
#endif
}
static int fs_ftruncate(int fd,int32_t size){if(size<0)return fail(EINVAL);return result(ftruncate(fd,size));}
static ssize_t fs_pwrite(int fd,const void *data,size_t bytes,int32_t offset){
    if(offset<0||bytes>INT32_MAX)return fail(EINVAL);
    if(bytes&&!data)return fail(EFAULT);
#ifdef _WIN32
    /* Serialized host test backend. Vita uses its native positional operation. */
    off_t saved=lseek(fd,0,SEEK_CUR);if(saved<0)return fail(errno);
    if(lseek(fd,offset,SEEK_SET)<0)return fail(errno);
    ssize_t n=write(fd,data,bytes);int e=errno;if(lseek(fd,saved,SEEK_SET)<0)return fail(errno);
    return n<0?fail(e):n;
#else
    ssize_t n=pwrite(fd,data,bytes,offset);return n<0?fail(errno):n;
#endif
}
static int fs_fchmod(int fd,unsigned mode){
#ifdef _WIN32
    (void)fd;(void)mode;return fail(ENOTSUP);
#else
    return result(fchmod(fd,mode));
#endif
}
static int fs_fchown(int fd,uint32_t owner,uint32_t group){
    struct stat st;if(fstat(fd,&st))return fail(errno);
    /* All files in the guest-private namespace belong to its unprivileged
     * identity. Changing the Vita's actual file owner is unsupported. */
    return (owner==UINT32_MAX||owner==10000)&&(group==UINT32_MAX||group==10000)?0:fail(EPERM);
}
static ssize_t fs_readlink(const char *path,char *out,size_t bytes){
    if(!path||!out)return fail(EFAULT);
    if(!bytes)return fail(EINVAL);
    ws_bionic_stat st;if(ws_bionic_stat_path(path,&st))return -1;
    return fail(EINVAL); /* Vita's private FAT namespace contains no symlinks. */
}
static int fs_symlink(const char *target,const char *link){
    if(!target||!link)return fail(EFAULT);
    char resolved[WS_PATH_MAX];int e=ws_path_resolve(link,resolved,sizeof(resolved));return fail(e?e:ENOTSUP);
}
static int fs_utimes(const char *p,const int32_t *values){
    if(values&&(values[1]<0||values[1]>=1000000||values[3]<0||values[3]>=1000000))return fail(EINVAL);
#ifdef _WIN32
    int32_t times[2];if(values){times[0]=values[0];times[1]=values[2];}
    char path[WS_PATH_MAX];int e=ws_path_resolve(p,path,sizeof(path));if(e)return fail(e);
    struct utimbuf t;if(values){t.actime=times[0];t.modtime=times[1];}return result(utime(path,values?&t:NULL));
#else
    char path[WS_PATH_MAX];int e=ws_path_resolve(p,path,sizeof(path));if(e)return fail(e);
    struct timeval t[2];if(values){t[0]=(struct timeval){values[0],values[1]};t[1]=(struct timeval){values[2],values[3]};}
    return result(utimes(path,values?t:NULL));
#endif
}
typedef struct {uint64_t inode;int64_t offset;uint16_t length;uint8_t type;char name[256];uint8_t pad[5];} guest_dirent;
_Static_assert(sizeof(guest_dirent)==280&&offsetof(guest_dirent,name)==19,"Bionic directory entry");
typedef struct {DIR *native;guest_dirent entry;int64_t index;} directory;
static directory *fs_opendir(const char *p) {
    char path[WS_PATH_MAX];int e=ws_path_resolve(p,path,sizeof(path));if(e){fail(e);return NULL;}
    DIR *d=opendir(path);if(!d){fail(errno);return NULL;}
    directory *out=calloc(1,sizeof(*out));if(!out){closedir(d);fail(ENOMEM);return NULL;}out->native=d;return out;
}
static guest_dirent *fs_readdir(directory *d) {
    if(!d){fail(EBADF);return NULL;}
    int saved=errno;errno=0;struct dirent *n=readdir(d->native);int e=errno;errno=saved;
    if(!n){if(e)fail(e);return NULL;}
    memset(&d->entry,0,sizeof(d->entry));d->entry.offset=++d->index;
#ifdef __vita__
    /* Vita dirent carries SceIoStat, not a POSIX inode/d_type. FAT has no
     * stable inode to expose; DT_UNKNOWN is valid when type is unavailable. */
    d->entry.type=(n->d_stat.st_mode&0xf000)==0x1000?4:(n->d_stat.st_mode&0xf000)==0x2000?8:0;
#else
    d->entry.inode=n->d_ino;
#endif
    d->entry.length=sizeof(d->entry);size_t size=strlen(n->d_name);if(size>=256){fail(ENAMETOOLONG);return NULL;}
    memcpy(d->entry.name,n->d_name,size+1);
#if !defined(_WIN32) && !defined(__vita__)
    d->entry.type=n->d_type;
#endif
    return &d->entry;
}
static int fs_readdir_r(directory *d,guest_dirent *entry,guest_dirent **out) {
    if(!entry||!out)return 22;
    int saved=*ws_bionic_errno_location();*ws_bionic_errno_location()=0;guest_dirent *found=fs_readdir(d);
    int e=*ws_bionic_errno_location();*ws_bionic_errno_location()=saved;
    *out=NULL;if(found){*entry=*found;*out=entry;}return e;
}
static int fs_closedir(directory *d){if(!d)return fail(EBADF);int r=closedir(d->native);free(d);return result(r);}
static int fs_dirfd(directory *d){if(!d)return fail(EBADF);
#ifdef _WIN32
    return fail(ENOTSUP);
#else
    return result(dirfd(d->native));
#endif
}
static int fs_dirname(const char *path,char *out,size_t size) {
    if(!out||!size)return fail(ERANGE);
    const char *p=path&&*path?path:".";size_t n=strlen(p);
    while(n>1&&p[n-1]=='/')--n;
    while(n&&p[n-1]!='/')--n;
    if(!n){p=".";n=1;}else while(n>1&&p[n-1]=='/')--n;
    if(n>=size)return fail(ERANGE);
    memcpy(out,p,n);out[n]=0;return (int)n;
}
static int fs_utime(const char *p,const int32_t *times) {
    char path[WS_PATH_MAX];int e=ws_path_resolve(p,path,sizeof(path));if(e)return fail(e);
    struct utimbuf t;if(times){t.actime=times[0];t.modtime=times[1];}
    return result(utime(path,times?&t:NULL));
}
typedef struct {const void *base;uint32_t bytes;} guest_iovec;
static ssize_t fs_writev(int fd,const guest_iovec *vector,int count) {
    if(!vector||count<0||count>1024)return fail(EINVAL);
    size_t total=0;for(int i=0;i<count;++i){if(vector[i].bytes>INT32_MAX-total)return fail(EINVAL);total+=vector[i].bytes;}
    total=0;for(int i=0;i<count;++i){ssize_t n=ws_bionic_write(fd,vector[i].base,vector[i].bytes);if(n<0)return total?(ssize_t)total:-1;total+=(size_t)n;if((size_t)n<vector[i].bytes)break;}return (ssize_t)total;
}
uintptr_t ws_filesystem_symbol(const char *name) {
#define S(n) if(!strcmp(name,#n))return (uintptr_t)fs_##n
    S(access);S(mkdir);S(unlink);S(rmdir);S(remove);S(rename);S(chmod);S(creat);S(dup);S(dup2);
    S(isatty);S(fsync);S(ftruncate);S(opendir);S(readdir);S(readdir_r);S(closedir);S(dirfd);S(utime);S(writev);
    S(pwrite);S(fchmod);S(utimes);
    S(umask);S(fchown);S(readlink);S(symlink);
#undef S
    if(!strcmp(name,"dirname_r"))return (uintptr_t)fs_dirname;
    if(!strcmp(name,"fnmatch"))return (uintptr_t)ws_fnmatch;
    if(!strcmp(name,"statfs"))return (uintptr_t)ws_statfs_path;
    if(!strcmp(name,"fstatfs"))return (uintptr_t)ws_statfs_fd;
    return 0;
}
