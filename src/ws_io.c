#include "ws_io.h"
#include "ws_events.h"
#include "ws_stdio.h"
#include "ws_paths.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

_Static_assert(sizeof(ws_bionic_stat) == 104, "ARM Bionic stat size");
_Static_assert(offsetof(ws_bionic_stat, mode) == 16, "ARM stat mode");
_Static_assert(offsetof(ws_bionic_stat, size) == 48, "ARM stat size field");
_Static_assert(offsetof(ws_bionic_stat, blocks) == 64, "ARM stat block count");
_Static_assert(offsetof(ws_bionic_stat, inode) == 96, "ARM stat inode");
static _Thread_local int android_errno;
int *ws_bionic_errno_location(void) { return &android_errno; }
int ws_bionic_error_from_native(int e) {
    /* Explicit translation: newlib values diverge from Linux after ERANGE. */
    const struct { int native, android; } errors[] = {
        {0,0},{EPERM,1},{ENOENT,2},{ESRCH,3},{EINTR,4},{EIO,5},{ENXIO,6},
        {E2BIG,7},{ENOEXEC,8},{EBADF,9},{ECHILD,10},{EAGAIN,11},{ENOMEM,12},
        {EACCES,13},{EFAULT,14},{EBUSY,16},{EEXIST,17},{EXDEV,18},{ENODEV,19},
        {ENOTDIR,20},{EISDIR,21},{EINVAL,22},{ENFILE,23},{EMFILE,24},{ENOTTY,25},
        {EFBIG,27},{ENOSPC,28},{ESPIPE,29},{EROFS,30},{EMLINK,31},{EPIPE,32},
        {EDOM,33},{ERANGE,34},{EDEADLK,35},{ENAMETOOLONG,36},{ENOLCK,37},
        {ENOSYS,38},{ENOTEMPTY,39},{ELOOP,40},{EOVERFLOW,75},{ENOTSUP,95},
        {ENOTSOCK,88},{EDESTADDRREQ,89},{EMSGSIZE,90},{EPROTOTYPE,91},{ENOPROTOOPT,92},
        {EPROTONOSUPPORT,93},{EAFNOSUPPORT,97},{EADDRINUSE,98},{EADDRNOTAVAIL,99},
        {ENETDOWN,100},{ENETUNREACH,101},{ECONNRESET,104},{ENOBUFS,105},{EISCONN,106},
        {ENOTCONN,107},{ETIMEDOUT,110},{ECONNREFUSED,111},{EHOSTUNREACH,113},
        {EALREADY,114},{EINPROGRESS,115},{ECANCELED,125}
    };
    for (size_t i = 0; i < sizeof(errors) / sizeof(*errors); ++i)
        if (e == errors[i].native) return errors[i].android;
    return 5; /* Unknown platform failure remains an error (EIO). */
}
static int failed(void) { android_errno = ws_bionic_error_from_native(errno); return -1; }
void ws_errno_publish(void){android_errno=ws_bionic_error_from_native(errno);}
static int invalid(int e) { errno = e; return failed(); }
int ws_bionic_open_flags(int flags, int *out) {
    /* ARM O_LARGEFILE is 0400000, unlike several other Linux architectures. */
    const int supported = 3 | 00000100 | 00000200 | 00001000 | 00002000 | 0400000;
    if (!out || flags < 0 || (flags & ~supported) || (flags & 3) == 3) return invalid(EINVAL);
    int result = (flags & 3) == 2 ? O_RDWR : (flags & 3) == 1 ? O_WRONLY : O_RDONLY;
    if (flags & 00000100) result |= O_CREAT;
    if (flags & 00000200) result |= O_EXCL;
    if (flags & 00001000) result |= O_TRUNC;
    if (flags & 00002000) result |= O_APPEND;
#ifdef O_BINARY
    result |= O_BINARY;
#endif
    *out = result;
    return 0;
}
static int convert_stat(const struct stat *s, ws_bionic_stat *out) {
    if (s->st_atime < INT32_MIN || s->st_atime > INT32_MAX ||
        s->st_mtime < INT32_MIN || s->st_mtime > INT32_MAX ||
        s->st_ctime < INT32_MIN || s->st_ctime > INT32_MAX) return invalid(EOVERFLOW);
    ws_bionic_stat result = {0};
    result.device = s->st_dev; result.legacy_inode = (uint32_t)s->st_ino;
    result.mode = s->st_mode; result.links = s->st_nlink;
    result.uid = 10000; result.gid = 10000; result.rdevice = s->st_rdev;
    result.size = s->st_size;
    result.access_seconds = (int32_t)s->st_atime;
    result.modify_seconds = (int32_t)s->st_mtime;
    result.change_seconds = (int32_t)s->st_ctime;
    result.inode = s->st_ino;
#if defined(__vita__)
    result.block_size = s->st_blksize;
    result.blocks = s->st_blocks;
#elif defined(_WIN32)
    result.block_size = 4096;
    result.blocks = ((uint64_t)s->st_size + 511) / 512;
#else
    result.block_size = s->st_blksize;
    result.blocks = s->st_blocks;
    result.access_nanoseconds = s->st_atim.tv_nsec;
    result.modify_nanoseconds = s->st_mtim.tv_nsec;
    result.change_nanoseconds = s->st_ctim.tv_nsec;
#endif
    *out = result;
    return 0;
}
int ws_bionic_stat_path(const char *path, ws_bionic_stat *out) {
    if (!path || !out) return invalid(EFAULT);
    if(ws_path_virtual_directory(path)){
        *out=(ws_bionic_stat){0};out->mode=0040555;out->links=2;
        out->uid=out->gid=10000;out->block_size=4096;return 0;
    }
    struct stat status;
    char resolved[WS_PATH_MAX];int e=ws_path_resolve(path,resolved,sizeof(resolved));if(e)return invalid(e);
    if (stat(resolved, &status)) return failed();
    return convert_stat(&status, out);
}
int ws_bionic_fstat(int fd, ws_bionic_stat *out) {
    if (!out) return invalid(EFAULT);
    struct stat status;
    if (fstat(fd, &status)) return failed();
    return convert_stat(&status, out);
}
int ws_bionic_open(const char *path, int flags, ...) {
    int native_flags;
    if (ws_bionic_open_flags(flags, &native_flags)) return -1;
    if (!path) return invalid(EFAULT);
    int mode = 0;
    if (flags & 00000100) {
        va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap);
    }
    char resolved[WS_PATH_MAX];int e=ws_path_resolve(path,resolved,sizeof(resolved));if(e)return invalid(e);
    int result = open(resolved, native_flags, (int)ws_path_creation_mode((unsigned)mode));
    if(result>=WS_EVENT_FD_BASE&&result<WS_EVENT_FD_BASE+WS_EVENT_FD_COUNT){close(result);return invalid(EMFILE);}
    return result < 0 ? failed() : result;
}
int ws_bionic_close(int fd) { if(ws_event_owned(fd))return ws_event_close(fd);int result = close(fd); return result < 0 ? failed() : result; }
ssize_t ws_bionic_read(int fd, void *data, size_t bytes) {
    if(ws_event_owned(fd))return ws_event_read(fd,data,bytes);
    if (bytes > INT32_MAX) return invalid(EINVAL);
    ssize_t result = read(fd, data, bytes); return result < 0 ? failed() : result;
}
ssize_t ws_bionic_write(int fd, const void *data, size_t bytes) {
    if(ws_event_owned(fd))return ws_event_write(fd,data,bytes);
    if (bytes > INT32_MAX) return invalid(EINVAL);
    ssize_t result = write(fd, data, bytes); return result < 0 ? failed() : result;
}
int32_t ws_bionic_lseek(int fd, int32_t offset, int whence) {
    off_t result = lseek(fd, (off_t)offset, whence);
    if (result < 0) return failed();
    if ((uint64_t)result > INT32_MAX) return invalid(EOVERFLOW);
    return (int32_t)result;
}
FILE *ws_bionic_fopen(const char *path, const char *mode) {
    FILE *f = fopen(path, mode);
    if (!f) failed();
    return f;
}
int ws_bionic_fclose(FILE *file) { int r = fclose(file); return r ? failed() : r; }
size_t ws_bionic_fread(void *data, size_t size, size_t count, FILE *file) {
    size_t r = fread(data, size, count, file); if (r < count && ferror(file)) failed(); return r;
}
size_t ws_bionic_fwrite(const void *data, size_t size, size_t count, FILE *file) {
    size_t r = fwrite(data, size, count, file); if (r < count) failed(); return r;
}
int ws_bionic_fseek(FILE *file, int32_t offset, int whence) {
    int r = fseek(file, offset, whence); return r ? failed() : r;
}
int32_t ws_bionic_ftell(FILE *file) {
    long r = ftell(file); if (r < 0) return failed();
    if ((uint64_t)r > INT32_MAX) return invalid(EOVERFLOW);
    return (int32_t)r;
}
int ws_bionic_fflush(FILE *file) { int r = fflush(file); return r ? failed() : r; }
uintptr_t ws_bionic_io_symbol(const char *name) {
    if (!name) return 0;
    uintptr_t stdio=ws_stdio_symbol(name);
    if(stdio)return stdio;
    struct { const char *name; uintptr_t address; } const entries[] = {
        {"__errno",(uintptr_t)ws_bionic_errno_location},
        {"stat",(uintptr_t)ws_bionic_stat_path},{"stat64",(uintptr_t)ws_bionic_stat_path},
        /* The Vita data filesystem has no symbolic links. */
        {"lstat",(uintptr_t)ws_bionic_stat_path},
        {"fstat",(uintptr_t)ws_bionic_fstat},{"fstat64",(uintptr_t)ws_bionic_fstat},
        {"open",(uintptr_t)ws_bionic_open},{"close",(uintptr_t)ws_bionic_close},
        {"read",(uintptr_t)ws_bionic_read},{"write",(uintptr_t)ws_bionic_write},
        {"lseek",(uintptr_t)ws_bionic_lseek},{"fopen",(uintptr_t)ws_bionic_fopen},
        {"fclose",(uintptr_t)ws_bionic_fclose},{"fread",(uintptr_t)ws_bionic_fread},
        {"fwrite",(uintptr_t)ws_bionic_fwrite},{"fseek",(uintptr_t)ws_bionic_fseek},
        {"ftell",(uintptr_t)ws_bionic_ftell},{"fflush",(uintptr_t)ws_bionic_fflush},
    };
    for (size_t i = 0; i < sizeof(entries) / sizeof(*entries); ++i)
        if (!strcmp(name, entries[i].name)) return entries[i].address;
    return 0;
}
