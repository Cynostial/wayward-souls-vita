#ifndef WS_DATABASE_H
#define WS_DATABASE_H
#include <stdint.h>
typedef int (*ws_database_open_v2)(const char *,void **,int,const char *);
int ws_database_configure(const char *assets,const char *apk,ws_database_open_v2 open_v2);
int ws_database_open(const char *filename,void **database);
#ifdef __vita__
#include "so_util.h"
int ws_database_attach(so_module *sqlite,const char *assets,const char *apk);
#endif
#endif
