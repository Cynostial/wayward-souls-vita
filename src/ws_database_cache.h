#ifndef WS_DATABASE_CACHE_H
#define WS_DATABASE_CACHE_H
/* Stable public SQLite VFS version-1 ABI, used by the APK's SQLite 3.7.5. */
#include <stdint.h>
#include <stddef.h>
typedef struct ws_sql_file ws_sql_file;
typedef struct ws_sql_io ws_sql_io;
typedef struct ws_sql_vfs ws_sql_vfs;
struct ws_sql_file {const ws_sql_io *methods;};
struct ws_sql_io {
    int version;
    int (*close)(ws_sql_file *);
    int (*read)(ws_sql_file *,void *,int,int64_t);
    int (*write)(ws_sql_file *,const void *,int,int64_t);
    int (*truncate)(ws_sql_file *,int64_t);
    int (*sync)(ws_sql_file *,int);
    int (*size)(ws_sql_file *,int64_t *);
    int (*lock)(ws_sql_file *,int);
    int (*unlock)(ws_sql_file *,int);
    int (*reserved)(ws_sql_file *,int *);
    int (*control)(ws_sql_file *,int,void *);
    int (*sector)(ws_sql_file *);
    int (*characteristics)(ws_sql_file *);
};
struct ws_sql_vfs {
    int version,file_bytes,max_path;
    ws_sql_vfs *next;
    const char *name;
    void *context;
    int (*open)(ws_sql_vfs *,const char *,ws_sql_file *,int,int *);
    int (*delete_file)(ws_sql_vfs *,const char *,int);
    int (*access)(ws_sql_vfs *,const char *,int,int *);
    int (*full_path)(ws_sql_vfs *,const char *,int,char *);
    void *(*dlopen)(ws_sql_vfs *,const char *);
    void (*dlerror)(ws_sql_vfs *,int,char *);
    void (*(*dlsym)(ws_sql_vfs *,void *,const char *))(void);
    void (*dlclose)(ws_sql_vfs *,void *);
    int (*randomness)(ws_sql_vfs *,int,char *);
    int (*sleep)(ws_sql_vfs *,int);
    int (*current_time)(ws_sql_vfs *,double *);
    int (*last_error)(ws_sql_vfs *,int,char *);
};
#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(ws_sql_file)==4,"SQLite ARM file ABI");
_Static_assert(sizeof(ws_sql_io)==52,"SQLite ARM v1 I/O ABI");
_Static_assert(sizeof(ws_sql_vfs)==72,"SQLite ARM v1 VFS ABI");
_Static_assert(offsetof(ws_sql_vfs,open)==24,"SQLite ARM xOpen ABI");
#endif
int ws_database_cache_init(const char *,ws_sql_vfs *(*)(const char *),int (*)(ws_sql_vfs *,int));
int ws_database_cache_ready(void);
#endif
