#include "ws_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static int image_path(char *path,size_t size,const char *root,const char *name) {
    int length=snprintf(path,size,"%s/lib/%s",root,name);
    return length<0 || (size_t)length>=size ? -1:0;
}
int ws_loader_map(ws_loader *loader,const char *version_dir) {
    if(!loader || !version_dir || loader->phase!=WS_EMPTY || loader->count)return -1;
    char path[512];
    loader->phase=WS_MAPPED;
    for(unsigned i=0;i<WS_IMAGE_COUNT;++i) {
        if(image_path(path,sizeof(path),version_dir,ws_images[i].name) ||
           so_file_load_checked(&loader->images[i],path,ws_images[i].base,ws_images[i].bytes,ws_images[i].crc32)<0) {
            ws_loader_discard(loader);return -1;
        }
        loader->modules[i]=(ws_module){ws_images[i].name,&loader->images[i]};
        ++loader->count;
    }
    return 0;
}
int ws_loader_link(ws_loader *loader,const so_default_dynlib *platform,size_t count,
                   void (*error)(const char *,const char *,unsigned)) {
    if(!loader || loader->phase!=WS_MAPPED || loader->count!=WS_IMAGE_COUNT)return -1;
    ws_link_context context={loader->modules,loader->count,platform,count,error};
    if(ws_link_all(&context))return -1;
    for(unsigned i=0;i<loader->count;++i)so_flush_caches(&loader->images[i]);
    loader->phase=WS_LINKED;return 0;
}
int ws_loader_initialize(ws_loader *loader,ws_load_callback callback,void *context) {
    if(!loader || !callback || loader->phase!=WS_LINKED || loader->count!=WS_IMAGE_COUNT)return -1;
    loader->phase=WS_INITIALIZING;
    for(unsigned i=0;i<loader->count;++i) {
        if(callback(context,i,1)) {
            loader->phase=WS_FAILED;return -1;
        }
        so_initialize(&loader->images[i]);
        if(callback(context,i,0)) {
            loader->phase=WS_FAILED;return -1;
        }
    }
    loader->phase=WS_READY;return 0;
}
so_module *ws_loader_module(ws_loader *loader,const char *filename) {
    if(!loader)return NULL;
    ws_link_context context={loader->modules,loader->count,NULL,0,NULL};
    int index=ws_module_index(&context,filename);
    return index<0?NULL:&loader->images[index];
}
uintptr_t ws_loader_find_exidx(const ws_loader *loader,uintptr_t pc,int *count) {
    if(!count)return 0;
    *count=0;
    if(!loader)return 0;
    for(unsigned i=0;i<loader->count;++i) {
        const so_module *image=&loader->images[i];
        if(pc>=image->text_base && pc-image->text_base<image->text_size) {
            *count=(int)(image->exidx_size/8);return image->exidx_base;
        }
    }
    return 0;
}
int ws_loader_discard(ws_loader *loader) {
    if(!loader || loader->phase>=WS_INITIALIZING)return -1;
    while(loader->count)so_finalize(&loader->images[--loader->count]);
    memset(loader,0,sizeof(*loader));return 0;
}
