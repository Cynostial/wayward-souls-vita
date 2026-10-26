#include "ws_database_cache.h"
#include "ws_memory.h"
#include "ws_paths.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>

enum {OK=0,READONLY=8,IOERR=10,NOTFOUND=12,CANTOPEN=14,SHORT_READ=522,BUDGET=8*1024*1024};
typedef struct {char path[WS_PATH_MAX];unsigned char *data;size_t bytes;} snapshot;
typedef struct {ws_sql_file base;const snapshot *image;} cached_file;
static snapshot images[4];
static size_t used;
static int active;
static pthread_mutex_t cache_lock=PTHREAD_MUTEX_INITIALIZER;
static ws_sql_vfs *parent;
static snapshot *lookup(const char *name){
    char path[WS_PATH_MAX];if(!name||ws_path_resolve(name,path,sizeof(path)))return NULL;
    for(unsigned i=0;i<4;++i)if(!strcmp(path,images[i].path))return &images[i];
    return NULL;
}
static int close_image(ws_sql_file *f){f->methods=NULL;return OK;}
static int read_image(ws_sql_file *file,void *out,int bytes,int64_t offset){
    const snapshot *s=((cached_file *)file)->image;
    if(bytes<0||offset<0||(!out&&bytes))return IOERR;
    size_t available=(uint64_t)offset<s->bytes?s->bytes-(size_t)offset:0;
    size_t copy=(size_t)bytes<available?(size_t)bytes:available;
    if(copy)memcpy(out,s->data+(size_t)offset,copy);
    if(copy<(size_t)bytes){memset((unsigned char *)out+copy,0,(size_t)bytes-copy);return SHORT_READ;}
    return OK;
}
static int write_image(ws_sql_file *f,const void *p,int n,int64_t o){(void)f;(void)p;(void)n;(void)o;return READONLY;}
static int truncate_image(ws_sql_file *f,int64_t n){(void)f;(void)n;return READONLY;}
static int no_op(ws_sql_file *f,int flags){(void)f;(void)flags;return OK;}
static int image_size(ws_sql_file *f,int64_t *n){*n=(int64_t)((cached_file *)f)->image->bytes;return OK;}
static int reserved(ws_sql_file *f,int *value){(void)f;*value=0;return OK;}
static int control(ws_sql_file *f,int code,void *arg){(void)f;(void)code;(void)arg;return NOTFOUND;}
static int sector(ws_sql_file *f){(void)f;return 512;}
static int characteristics(ws_sql_file *f){(void)f;return 0;}
static const ws_sql_io methods={1,close_image,read_image,write_image,truncate_image,no_op,image_size,
    no_op,no_op,reserved,control,sector,characteristics};
static int open_image(ws_sql_vfs *v,const char *name,ws_sql_file *file,int flags,int *out_flags){
    (void)v;file->methods=NULL;
    /* Only these immutable main databases may enter this VFS. Temp files,
     * journals, and all saves retain the original writable filesystem. */
    snapshot *s=lookup(name);if(!s||(flags&7)!=1||!(flags&0x100))return CANTOPEN;
    pthread_mutex_lock(&cache_lock);
    if(!s->data){
        FILE *in=fopen(s->path,"rb");
        if(in){
            long n=-1;if(!fseek(in,0,SEEK_END))n=ftell(in);
            if(n>=100&&(size_t)n<=BUDGET-used&&!fseek(in,0,SEEK_SET)){
                unsigned char *data=ws_malloc((size_t)n);
                if(data){
                    size_t got=fread(data,1,(size_t)n,in);
                    if(got==(size_t)n&&!ferror(in)&&!memcmp(data,"SQLite format 3\0",16)){
                        s->data=data;s->bytes=(size_t)n;used+=s->bytes;
                    }else ws_free(data);
                }
            }
            fclose(in);
        }
    }
    int ok=s->data!=NULL;
    pthread_mutex_unlock(&cache_lock);
    if(!ok)return CANTOPEN;
    ((cached_file *)file)->image=s;file->methods=&methods;
    if(out_flags)*out_flags=1;
    return OK;
}
static int delete_image(ws_sql_vfs *v,const char *name,int sync){(void)v;(void)name;(void)sync;return READONLY;}
static int access_image(ws_sql_vfs *v,const char *name,int flags,int *out){
    (void)v;*out=flags!=1&&lookup(name)!=NULL;return OK;
}
static int full_path(ws_sql_vfs *v,const char *name,int size,char *out){
    (void)v;return size<=0||ws_path_resolve(name,out,(size_t)size)?CANTOPEN:OK;
}
static int random_bytes(ws_sql_vfs *v,int n,char *out){(void)v;return parent->randomness(parent,n,out);}
static int sleep_us(ws_sql_vfs *v,int n){(void)v;return parent->sleep(parent,n);}
static int current_time(ws_sql_vfs *v,double *out){(void)v;return parent->current_time(parent,out);}
static ws_sql_vfs vfs={.version=1,.file_bytes=sizeof(cached_file),.max_path=WS_PATH_MAX,
    .name="wayward-assets",.open=open_image,.delete_file=delete_image,.access=access_image,
    .full_path=full_path,.randomness=random_bytes,.sleep=sleep_us,.current_time=current_time};
int ws_database_cache_init(const char *assets,ws_sql_vfs *(*find)(const char *),int (*reg)(ws_sql_vfs *,int)){
    if(active||!assets||!find||!reg)return -1;
    static const char *names[]={"data.db3","maps.db3","ents.db3","gui.db3"};
    for(unsigned i=0;i<4;++i){
        char path[WS_PATH_MAX];int n=snprintf(path,sizeof(path),"%s/%s",assets,names[i]);
        if(n<0||n>=(int)sizeof(path)||ws_path_resolve(path,images[i].path,sizeof(images[i].path)))return -1;
    }
    parent=find(NULL);if(!parent||!parent->randomness||!parent->sleep||!parent->current_time)return -1;
    if(reg(&vfs,0))return -1;
    active=1;return 0;
}
int ws_database_cache_ready(void){return active;}
