#define _POSIX_C_SOURCE 200809L
/* First-launch streaming importer. The entire APK is authenticated before any
 * payload is installed. Offsets and destinations come only from compiled
 * metadata; ZIP names never become output paths. Completed files survive a
 * cancelled/interrupted run. Documents and Library are never written here. */
#include "ws_setup.h"
#include "../src/ws_sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define MAKE_DIR(p) _mkdir(p)
#define SYNC_FILE(f) _commit(_fileno(f))
#else
#define MAKE_DIR(p) mkdir(p,0777)
#define SYNC_FILE(f) fsync(fileno(f))
#endif
#ifdef WS_SETUP_TEST_IO
/* Fault-injection endpoint; production continues to use the platform sync. */
int ws_setup_test_sync(int);
#undef SYNC_FILE
#define SYNC_FILE(f) ws_setup_test_sync(fileno(f))
#endif
#define CHUNK 65536u
typedef struct { ws_setup_progress callback;void *arg;uint64_t base,total; } progress_state;
static int report(progress_state *p,const char *label,uint64_t n,uint64_t total){
    return p->callback&&p->callback(label,n,total,p->arg)?-2:0;
}
static int error(ws_setup_result *r,const char *message){snprintf(r->message,sizeof(r->message),"%s",message);return -1;}
static int join(char out[512],const char *root,const char *rel){
    if(!root||!*root||!rel||!*rel||*rel=='/'||strchr(rel,':')||strchr(rel,'\\')||strstr(rel,".."))return -1;
    int n=snprintf(out,512,"%s/%s",root,rel);return n<0||n>=512?-1:0;
}
static int sized(const char *path,uint64_t size){
    struct stat s;return !stat(path,&s)&&S_ISREG(s.st_mode)&&s.st_size>=0&&(uint64_t)s.st_size==size;
}
static int parents(const char *path){
    char p[512];if(strlen(path)>=sizeof(p))return -1;strcpy(p,path);
    char *start=strchr(p,':');start=start?start+1:p;if(*start=='/')++start;
    for(char *s=start;*s;++s)if(*s=='/'){
        *s=0;struct stat st;
        if(stat(p,&st)){if(MAKE_DIR(p))return -1;}
        else if(!S_ISDIR(st.st_mode))return -1;
        *s='/';
    }
    return 0;
}
static FILE *bulk(const char *path,const char *mode){
    FILE *f=fopen(path,mode);
    /* Avoid Vita newlib's default 1 KiB transfers (the old Wonder Boy stall). */
    if(f&&setvbuf(f,NULL,_IOFBF,CHUNK)){fclose(f);return NULL;}
    return f;
}
static int finish(FILE *f){
    int result=fflush(f);if(!result)result=SYNC_FILE(f);
    if(result)fprintf(stderr,"SETUP flush/sync failed: errno=%d\n",errno);
    if(fclose(f))result=-1;
    return result?-1:0;
}
static int publish(const char *temp,const char *path){
    if(!rename(temp,path))return 0;
    /* Windows and some Vita filesystems cannot rename over an existing file.
     * This only replaces a compiled immutable payload/marker, after temp has
     * been fully verified and synced. A crash leaves a repairable missing file. */
    struct stat st;
    if(stat(path,&st)||!S_ISREG(st.st_mode)){
        fprintf(stderr,"SETUP rename failed: %s -> %s errno=%d\n",temp,path,errno);return -1;
    }
    if(remove(path)){fprintf(stderr,"SETUP replace failed: %s errno=%d\n",path,errno);return -1;}
    int rc=rename(temp,path);
    if(rc)fprintf(stderr,"SETUP final rename failed: %s errno=%d\n",path,errno);
    return rc;
}
static int marker(const char *root){
    char path[512];if(join(path,root,".setup-ready"))return 0;
    FILE *f=fopen(path,"rb");if(!f)return 0;
    unsigned char data[73];size_t n=fread(data,1,sizeof(data),f);fclose(f);
    return n==72&&!memcmp(data,"WSSETUP1",8)&&!memcmp(data+8,ws_setup_identity,32)&&!memcmp(data+40,ws_setup_apk_hash,32);
}
static int write_marker(const char *root){
    char path[512],temp[512];if(join(path,root,".setup-ready")||join(temp,root,".setup-ready.part"))return -1;
    unsigned char data[72];memcpy(data,"WSSETUP1",8);memcpy(data+8,ws_setup_identity,32);memcpy(data+40,ws_setup_apk_hash,32);
    FILE *f=bulk(temp,"wb");if(!f)return -1;
    int ok=fwrite(data,1,sizeof(data),f)==sizeof(data);if(finish(f))ok=0;
    if(!ok||publish(temp,path)){remove(temp);return -1;}return 0;
}
int ws_setup_ready(const char *root){
    if(!marker(root))return 0;
    char path[512];if(join(path,root,"source.apk")||!sized(path,ws_setup_apk_size))return 0;
    for(unsigned i=0;i<ws_setup_count;++i){
        char rel[512];int n=snprintf(rel,sizeof(rel),WS_SETUP_VERSION "/%s",ws_setup_files[i].path);
        if(n<0||n>=(int)sizeof(rel)||join(path,root,rel)||!sized(path,ws_setup_files[i].bytes))return 0;
    }
    return 1;
}
uint64_t ws_setup_space(const char *root){
    /* Conservative bound includes replacement scratch and retained source.apk.
     * Existing same-size files may still be damaged, so don't assume reuse. */
    char path[512];uint64_t n=ws_setup_output_bytes+8u*1024u*1024u;
    if(join(path,root,"source.apk")||!sized(path,ws_setup_apk_size))n+=ws_setup_apk_size;
    return n;
}
int ws_setup_find(const char *root,ws_setup_result *r){
    r->apk[0]=0;DIR *dir=opendir(root);if(!dir)return error(r,"Cannot open the game folder.");
    struct dirent *e;unsigned count=0;char canonical[512]={0};
    while((e=readdir(dir))){
        const char *dot=strrchr(e->d_name,'.');if(!dot||strcasecmp(dot,".apk"))continue;
        char path[512];struct stat st;
        if(join(path,root,e->d_name)||stat(path,&st)||!S_ISREG(st.st_mode))continue;
        if(!strcasecmp(e->d_name,"source.apk"))strcpy(canonical,path);
        else{++count;strcpy(r->apk,path);}
    }
    closedir(dir);
    if(count>1)return error(r,"Keep only one APK in this folder.");
    if(!count&&*canonical)strcpy(r->apk,canonical);
    if(!*r->apk)return error(r,"No APK found. Copy your Android 1.12 ARMv7 APK here.");
    r->message[0]=0;return 0;
}
static int hash_stream(FILE *f,uint32_t bytes,uint8_t hash[32],unsigned char *buffer,
                       progress_state *p,const char *label,uint64_t base,uint64_t total){
    if(fseek(f,0,SEEK_SET))return -1;
    ws_sha256_state state;ws_sha256_init(&state);
    uint32_t done=0;
    while(done<bytes){
        if(report(p,label,base+done,total))return -2;
        unsigned n=bytes-done;if(n>CHUNK)n=CHUNK;
        if(fread(buffer,1,n,f)!=n)return -1;
        ws_sha256_update(&state,buffer,n);done+=n;
    }
    if(ferror(f)||fgetc(f)!=EOF||ferror(f))return -1;
    ws_sha256_final(&state,hash);return report(p,label,base+done,total);
}
static int matches(const char *path,const ws_setup_file *e,unsigned char *buffer,progress_state *p){
    if(!sized(path,e->bytes))return 0;
    FILE *f=bulk(path,"rb");if(!f)return 0;
    uint8_t hash[32];int rc=hash_stream(f,e->bytes,hash,buffer,p,"Preparing",p->base,p->total);
    fclose(f);if(rc==-2)return -2;
    return !rc&&!memcmp(hash,e->hash,32);
}
static int extract(FILE *apk,FILE *out,const ws_setup_file *e,unsigned char *in,unsigned char *buffer,progress_state *p){
    if((uint64_t)e->offset+e->packed>ws_setup_apk_size||fseek(apk,e->offset,SEEK_SET))return -1;
    if(e->method!=0&&e->method!=8)return -1;
    if(e->method==0&&e->bytes!=e->packed)return -1;
    ws_sha256_state hash;ws_sha256_init(&hash);uLong crc=crc32(0,NULL,0);
    z_stream z={0};int result=-1,code=Z_OK;
    if(e->method==8&&inflateInit2(&z,-MAX_WBITS)!=Z_OK)return -1;
    uint32_t left=e->packed,done=0;
    for(;;){
        if(report(p,"Preparing",p->base+done,p->total)){result=-2;break;}
        unsigned n=0;
        if(e->method==0){
            if(!left){result=0;break;}
            n=left>CHUNK?CHUNK:left;
            if(fread(buffer,1,n,apk)!=n)break;
            left-=n;
        }else{
            if(!z.avail_in&&left){
                unsigned take=left>CHUNK?CHUNK:left;
                if(fread(in,1,take,apk)!=take)break;
                left-=take;z.next_in=in;z.avail_in=take;
            }
            z.next_out=buffer;z.avail_out=CHUNK;
            uLong before=z.total_in;code=inflate(&z,Z_NO_FLUSH);n=CHUNK-z.avail_out;
            if((code!=Z_OK&&code!=Z_STREAM_END)||(!n&&before==z.total_in))break;
        }
        if(n>e->bytes-done||fwrite(buffer,1,n,out)!=n)break;
        ws_sha256_update(&hash,buffer,n);crc=crc32(crc,buffer,n);done+=n;
        if(e->method==8&&code==Z_STREAM_END){result=!left&&!z.avail_in?0:-1;break;}
    }
    if(e->method==8)inflateEnd(&z);
    uint8_t digest[32];ws_sha256_final(&hash,digest);
    if(!result&&(done!=e->bytes||(uint32_t)crc!=e->crc||memcmp(digest,e->hash,32)))result=-1;
    if(result==-1)fprintf(stderr,"SETUP extract failed: %s bytes=%u/%u compressed_left=%u z=%d crc=%08lx/%08x\n",
        e->path,done,e->bytes,left,code,(unsigned long)crc,e->crc);
    return result;
}
static int copy_apk(FILE *apk,const char *path,unsigned char *buffer,progress_state *p){
    char temp[512];int n=snprintf(temp,sizeof(temp),"%s.part",path);
    if(n<0||n>=(int)sizeof(temp)||fseek(apk,0,SEEK_SET))return -1;
    FILE *out=bulk(temp,"wb");if(!out)return -1;
    int result=0;uint32_t done=0;ws_sha256_state h;ws_sha256_init(&h);
    while(done<ws_setup_apk_size){
        if(report(p,"Preparing",p->base+done,p->total)){result=-2;break;}
        unsigned take=ws_setup_apk_size-done;if(take>CHUNK)take=CHUNK;
        if(fread(buffer,1,take,apk)!=take||fwrite(buffer,1,take,out)!=take){result=-1;break;}
        ws_sha256_update(&h,buffer,take);done+=take;
    }
    uint8_t hash[32];ws_sha256_final(&h,hash);
    if(!result&&memcmp(hash,ws_setup_apk_hash,32))result=-1;
    if(finish(out)&&!result)result=-1;
    if(!result&&publish(temp,path))result=-1;
    if(result)remove(temp);
    return result;
}
int ws_setup_run(const char *root,const char *input,ws_setup_progress callback,void *arg,ws_setup_result *r){
    r->completed=r->reused=0;r->message[0]=0;
    if(!root||!input||strlen(input)>=sizeof(r->apk))return error(r,"The APK path is too long.");
    memmove(r->apk,input,strlen(input)+1);input=r->apk;
    if(!sized(input,ws_setup_apk_size))return error(r,"Unsupported APK. Use Android 1.12 ARMv7.");
    char canonical[512],ready[512];
    if(join(canonical,root,"source.apk")||join(ready,root,".setup-ready"))return error(r,"The game folder path is too long.");
    int copy=strcasecmp(input,canonical)!=0;
    progress_state progress={callback,arg,0,ws_setup_output_bytes+(copy?ws_setup_apk_size:0)};
    FILE *apk=bulk(input,"rb");unsigned char *buffers=malloc(2*CHUNK);
    if(!apk||!buffers){if(apk)fclose(apk);free(buffers);return error(r,"Cannot read the APK. Check storage and try again.");}
    uint8_t digest[32];int rc=hash_stream(apk,ws_setup_apk_size,digest,buffers,&progress,"Checking APK",0,ws_setup_apk_size);
    if(!rc&&memcmp(digest,ws_setup_apk_hash,32)){
        free(buffers);fclose(apk);return error(r,"Unsupported APK. Use the original Android 1.12 ARMv7 build.");
    }
    /* A failed or cancelled verification never invalidates a working install.
     * Once repair starts, a missing marker prevents booting partial outputs. */
    if(!rc&&remove(ready)&&errno!=ENOENT)rc=-1;
    for(unsigned i=0;!rc&&i<ws_setup_count;++i){
        const ws_setup_file *e=&ws_setup_files[i];char path[512],relative[512],temp[512];
        int n=snprintf(relative,sizeof(relative),WS_SETUP_VERSION "/%s",e->path);
        if(n<0||n>=(int)sizeof(relative)||join(path,root,relative)){rc=-1;break;}
        int reuse=matches(path,e,buffers,&progress);
        if(reuse<0){rc=reuse;break;}
        if(reuse)++r->reused;
        else{
            n=snprintf(temp,sizeof(temp),"%s.part",path);
            if(n<0||n>=(int)sizeof(temp)||parents(temp)){rc=-1;break;}
            FILE *out=bulk(temp,"wb");if(!out){rc=-1;break;}
            rc=extract(apk,out,e,buffers,buffers+CHUNK,&progress);
            if(finish(out)&&!rc)rc=-1;
            if(!rc&&publish(temp,path))rc=-1;
            if(rc){remove(temp);break;}
        }
        ++r->completed;progress.base+=e->bytes;
        rc=report(&progress,"Preparing",progress.base,progress.total);
    }
    if(!rc&&copy){
        ws_setup_file source={.bytes=ws_setup_apk_size};memcpy(source.hash,ws_setup_apk_hash,32);
        int reuse=matches(canonical,&source,buffers,&progress);
        if(reuse<0)rc=reuse;
        else if(!reuse)rc=copy_apk(apk,canonical,buffers,&progress);
    }
    if(fclose(apk)&&!rc)rc=-1;
    free(buffers);
    if(!rc)rc=report(&progress,"Preparing",progress.total,progress.total);
    if(!rc)rc=write_marker(root);
    if(rc==-2){error(r,"Paused. Press START to resume.");return -2;}
    if(rc)return error(r,"Preparation failed. Check free space and storage, then retry.");
    snprintf(r->message,sizeof(r->message),"Ready to play.");return 0;
}
