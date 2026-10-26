#ifndef WS_PATHS_H
#define WS_PATHS_H
#include <stddef.h>
#include <stdint.h>
#define WS_PATH_MAX 1024
int ws_paths_init(const char *native_root);
int ws_path_resolve(const char *,char *,size_t);
/* Read-only virtual ancestors of the private Android data directory. These
 * names never resolve to the host filesystem root. */
int ws_path_virtual_directory(const char *);
unsigned ws_path_creation_mode(unsigned);
uintptr_t ws_filesystem_symbol(const char *);
/* Android 4.2 public struct statfs (not the smaller kernel statfs32). */
typedef struct {
    uint32_t type,block_size;
    uint64_t blocks,free_blocks,available_blocks,files,free_files;
    uint32_t fsid[2],name_max,fragment_size,spare[5];
} ws_statfs;
_Static_assert(sizeof(ws_statfs)==88,"Bionic statfs ABI");
int ws_statfs_path(const char *,ws_statfs *);
int ws_statfs_fd(int,ws_statfs *);
#endif
