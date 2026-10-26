#include "ws_database.h"
#include "ws_paths.h"
#include <stdio.h>
#include <string.h>
#ifdef __vita__
#include "ws_database_cache.h"
#include <pthread.h>
static ws_sql_vfs *(*find_vfs)(const char *);
static int (*register_vfs)(ws_sql_vfs *,int),(*close_database)(void *);
static int (*initialize_sqlite)(void);
static pthread_once_t cache_once=PTHREAD_ONCE_INIT;
static void initialize_cache(void);
#endif
static char bundled[WS_PATH_MAX],archive[WS_PATH_MAX],guest_assets[WS_PATH_MAX];
static ws_database_open_v2 native_open;
int ws_database_configure(const char *assets,const char *apk,ws_database_open_v2 open_v2){
    if(!assets||!apk||!open_v2||native_open||strlen(assets)>=sizeof(guest_assets)||
       ws_path_resolve(assets,bundled,sizeof(bundled))||ws_path_resolve(apk,archive,sizeof(archive)))return -1;
    strcpy(guest_assets,assets);
    native_open=open_v2;return 0;
}
int ws_database_open(const char *filename,void **database){
    if(!native_open||!filename||!database){if(database)*database=NULL;return 21;}
    char path[WS_PATH_MAX],redirected[WS_PATH_MAX];int immutable=0;
    if(!ws_path_resolve(filename,path,sizeof(path))){
        size_t n=strlen(bundled),a=strlen(archive);int apk_alias=!strncmp(path,archive,a)&&path[a]=='/';
        if(apk_alias||(!strncmp(path,bundled,n)&&path[n]=='/')){
            if(apk_alias)n=a;
            const char *name=path+n+1;
            immutable=!strcmp(name,"data.db3")||!strcmp(name,"maps.db3")||
                      !strcmp(name,"ents.db3")||!strcmp(name,"gui.db3");
            /* Apportable exposes APK resources as source.apk/name. SQLite
             * needs the verified extracted file, not the ZIP's shared fd. */
            if(immutable&&apk_alias){
                int count=snprintf(redirected,sizeof(redirected),"%s/%s",guest_assets,name);
                if(count<0||count>=(int)sizeof(redirected)){*database=NULL;return 14;}
                filename=redirected;
            }
        }
    }
    /* These four files are immutable APK resources, not save databases.
     * SQLite's built-in unix-none VFS is safe here only with READONLY enforced.
     * Other paths retain sqlite3_open's READWRITE|CREATE/default-VFS behavior. */
    const char *vfs=immutable?"unix-none":NULL;
#ifdef __vita__
    if(immutable){
        pthread_once(&cache_once,initialize_cache);
        if(ws_database_cache_ready())vfs="wayward-assets";
    }
#endif
    int result=native_open(filename,database,immutable?1:6,vfs);
#ifdef __vita__
    if(result==14&&immutable&&vfs&&!strcmp(vfs,"wayward-assets")){
        if(*database)close_database(*database);
        *database=NULL;
        result=native_open(filename,database,1,"unix-none");
    }
#endif
    return result;
}
#ifdef __vita__
static void initialize_cache(void){
    if(!initialize_sqlite())ws_database_cache_init(guest_assets,find_vfs,register_vfs);
}
int ws_database_attach(so_module *sqlite,const char *assets,const char *apk){
    uintptr_t open=so_symbol(sqlite,"sqlite3_open"),open_v2=so_symbol(sqlite,"sqlite3_open_v2");
    if(open!=sqlite->load_addr+0x173c4||open_v2!=sqlite->load_addr+0x180d8)return -1;
    if(ws_database_configure(assets,apk,(ws_database_open_v2)open_v2))return -1;
    find_vfs=(void *)so_symbol(sqlite,"sqlite3_vfs_find");
    register_vfs=(void *)so_symbol(sqlite,"sqlite3_vfs_register");
    close_database=(void *)so_symbol(sqlite,"sqlite3_close");
    initialize_sqlite=(void *)so_symbol(sqlite,"sqlite3_initialize");
    if(!find_vfs||!register_vfs||!close_database||!initialize_sqlite)return -1;
    hook_addr(open,(uintptr_t)ws_database_open);so_flush_caches(sqlite);return 0;
}
#endif
