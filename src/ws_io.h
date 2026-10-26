#ifndef WS_BIONIC_IO_H
#define WS_BIONIC_IO_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

/* Android ARM EABI stat/stat64, including 8-byte alignment padding. Do not
 * replace this with VitaSDK's stat or the packed x86 layout. */
typedef struct ws_bionic_stat {
    uint64_t device;
    uint32_t pad0, legacy_inode, mode, links, uid, gid;
    uint64_t rdevice;
    uint32_t pad1, align_size;
    int64_t size;
    uint32_t block_size, align_blocks;
    uint64_t blocks;
    int32_t access_seconds, access_nanoseconds;
    int32_t modify_seconds, modify_nanoseconds;
    int32_t change_seconds, change_nanoseconds;
    uint64_t inode;
} ws_bionic_stat;
int *ws_bionic_errno_location(void);
void ws_errno_publish(void);
int ws_bionic_error_from_native(int error);
int ws_bionic_open_flags(int flags, int *native_flags);
int ws_bionic_stat_path(const char *path, ws_bionic_stat *out);
int ws_bionic_fstat(int fd, ws_bionic_stat *out);
int ws_bionic_open(const char *path, int flags, ...);
int ws_bionic_close(int fd);
ssize_t ws_bionic_read(int fd, void *data, size_t bytes);
ssize_t ws_bionic_write(int fd, const void *data, size_t bytes);
int32_t ws_bionic_lseek(int fd, int32_t offset, int whence);
FILE *ws_bionic_fopen(const char *path, const char *mode);
int ws_bionic_fclose(FILE *file);
size_t ws_bionic_fread(void *data, size_t size, size_t count, FILE *file);
size_t ws_bionic_fwrite(const void *data, size_t size, size_t count, FILE *file);
int ws_bionic_fseek(FILE *file, int32_t offset, int whence);
int32_t ws_bionic_ftell(FILE *file);
int ws_bionic_fflush(FILE *file);
/* Public stdio imports resolve through ws_stdio, never native FILE layout. */
uintptr_t ws_bionic_io_symbol(const char *name);
#endif
