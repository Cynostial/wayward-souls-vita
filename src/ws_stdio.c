#define _GNU_SOURCE
#include "ws_stdio.h"
#include "ws_io.h"
#include "ws_paths.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <unistd.h>

/* libv stdio_init memcpy's funopen FILEs into __sF. The cookie identifies
 * shared backend state, independent of the address of the 84-byte facade.
 * Counts stay zero to route Android's inline getc/putc through __srget/swbuf.
 * Concurrent fclose/use remains invalid just as in POSIX stdio. */
#define STREAMS 128u
typedef struct {
    uint32_t token;FILE *native;void *cookie;
    int (*read)(void *,char *,int),(*write)(void *,const char *,int),(*close)(void *);
    int32_t (*seek)(void *,int32_t,int);
    int pushed;ws_file facade;
} stream;
static stream streams[STREAMS];
static uint32_t generation=1;
static pthread_mutex_t registry=PTHREAD_MUTEX_INITIALIZER;
ws_file ws_standard_files[3];
static int ready;
static int cookie_read(void *,char *,int);
static int cookie_write(void *,const char *,int);
static int32_t cookie_seek(void *,int32_t,int);
static int cookie_close(void *);
static int fail(int e) { *ws_bionic_errno_location()=ws_bionic_error_from_native(e);return -1; }
static stream *lookup(ws_file *f) {
    if(!f){fail(EBADF);return NULL;}
    stream *s=&streams[f->cookie&(STREAMS-1)];
    if(!f->cookie || s->token!=f->cookie){fail(EBADF);return NULL;}
    return s;
}
static stream *reserve(FILE *native,const char *mode) {
    stream *s=NULL;
    pthread_mutex_lock(&registry);
    if(generation<0x01000000u)for(unsigned i=0;i<STREAMS;++i)if(!streams[i].token) {
        s=&streams[i];memset(s,0,sizeof(*s));s->token=(generation++<<7)|i;
        s->native=native;s->pushed=-1;
        s->facade.cookie=s->token;s->facade.descriptor=native?(int16_t)fileno(native):-1;
        s->facade.flags=2|(strchr(mode,'+')?0x10:mode[0]=='r'?4:8);
        /* libv's bundled wide-stdio routines invoke these fields directly. */
        s->facade.read_fn=(uint32_t)(uintptr_t)cookie_read;
        s->facade.write_fn=(uint32_t)(uintptr_t)cookie_write;
        s->facade.seek_fn=(uint32_t)(uintptr_t)cookie_seek;
        s->facade.close_fn=(uint32_t)(uintptr_t)cookie_close;
        break;
    }
    pthread_mutex_unlock(&registry);
    if(!s)fail(EMFILE);
    return s;
}
int ws_stdio_init(void) {
    if(ready)return EBUSY;
    FILE *standard[]={stdin,stdout,stderr};
    for(unsigned i=0;i<3;++i) {
        stream *s=reserve(standard[i],i?"w":"r");
        if(!s)return EMFILE;
        ws_standard_files[i]=s->facade;
    }
    ready=1;return 0;
}
/* libv's file_io_init unconditionally reads /proc/<getpid()>/cmdline and
 * aborts if it cannot open it. Expose the real guest package identity through
 * a read-only stream; the Vita has no Linux proc filesystem. */
static const char process_command[]="com.noodlecake.waywardsouls";
typedef struct {int32_t position;} command_stream;
static int command_read(void *cookie,char *out,int bytes) {
    command_stream *s=cookie;
    if(s->position>=(int32_t)sizeof(process_command))return 0;
    int left=(int)sizeof(process_command)-s->position;if(bytes>left)bytes=left;
    memcpy(out,process_command+s->position,(size_t)bytes);s->position+=bytes;return bytes;
}
static int32_t command_seek(void *cookie,int32_t offset,int whence) {
    command_stream *s=cookie;
    if(whence!=SEEK_SET&&whence!=SEEK_CUR&&whence!=SEEK_END)return fail(EINVAL);
    int64_t position=(int64_t)offset+(whence==SEEK_CUR?s->position:whence==SEEK_END?(int32_t)sizeof(process_command):0);
    if(position<0||position>INT32_MAX)return fail(EINVAL);
    return s->position=(int32_t)position;
}
static int command_close(void *cookie){free(cookie);return 0;}
static int is_command_path(const char *path) {
    if(!strcmp(path,"/proc/self/cmdline"))return 1;
    char expected[64];snprintf(expected,sizeof(expected),"/proc/%d/cmdline",getpid());return !strcmp(path,expected);
}
ws_file *ws_fopen(const char *path,const char *mode) {
    if(!path||!mode){fail(EINVAL);return NULL;}
    if(is_command_path(path)) {
        if(strcmp(mode,"r")&&strcmp(mode,"rb")){fail(EACCES);return NULL;}
        command_stream *cookie=calloc(1,sizeof(*cookie));if(!cookie){fail(ENOMEM);return NULL;}
        ws_file *f=ws_funopen(cookie,command_read,NULL,command_seek,command_close);
        if(!f)free(cookie);
        return f;
    }
    char resolved[WS_PATH_MAX];int e=ws_path_resolve(path,resolved,sizeof(resolved));if(e){fail(e);return NULL;}
    FILE *n=fopen(resolved,mode);
    if(!n){fail(errno);return NULL;}
    stream *s=reserve(n,mode);
    if(!s){fclose(n);return NULL;}
    return &s->facade;
}
ws_file *ws_funopen(const void *cookie,int (*r)(void *,char *,int),
    int (*w)(void *,const char *,int),int32_t (*seek)(void *,int32_t,int),int (*close)(void *)) {
    if(!r&&!w){fail(EINVAL);return NULL;}
    stream *s=reserve(NULL,r?(w?"r+":"r"):"w");
    if(!s)return NULL;
    s->cookie=(void *)cookie;s->read=r;s->write=w;s->seek=seek;s->close=close;
    return &s->facade;
}
int ws_fclose(ws_file *f) {
    stream *s=lookup(f);if(!s)return EOF;
    int r=s->native?fclose(s->native):s->close?s->close(s->cookie):0;
    if(r && s->native)fail(errno);
    pthread_mutex_lock(&registry);s->token=0;pthread_mutex_unlock(&registry);
    memset(f,0,sizeof(*f));f->descriptor=-1;return r;
}
static int readable(ws_file *f){return (f->flags&(4|0x10))!=0;}
static int writable(ws_file *f){return (f->flags&(8|0x10))!=0;}
size_t ws_fread(void *out,size_t size,size_t count,ws_file *f) {
    stream *s=lookup(f);if(!s)return 0;
    if(!size||!count)return 0;
    if(!out||count>SIZE_MAX/size||!readable(f)){fail(EINVAL);f->flags|=0x40;return 0;}
    size_t bytes=size*count,done=0;
    unsigned char *p=out;
    if(s->pushed>=0){p[done++]=(unsigned char)s->pushed;s->pushed=-1;}
    if(done<bytes&&f->read_count>0) {
        /* __srefill exposes exactly one inline byte. On ARM its cursor may
         * already have advanced, so honor the count before consuming it. */
        if(f->read_count!=1){fail(EIO);return 0;}
        p[done++]=f->tiny_buffer[0];f->read_count=0;
    }
    while(done<bytes) {
        size_t n=bytes-done;if(n>INT_MAX)n=INT_MAX;
        int got;
        if(s->native) {
            got=(int)fread(p+done,1,n,s->native);
            if(!got&&ferror(s->native)){fail(errno);got=-1;}
        } else got=s->read?s->read(s->cookie,(char *)p+done,(int)n):fail(EBADF);
        if(got<=0){f->flags|=got<0?0x40:0x20;break;}
        if((size_t)got>n){fail(EIO);f->flags|=0x40;break;}
        done+=(size_t)got;
    }
    f->write_count=0;return done/size;
}
size_t ws_fwrite(const void *data,size_t size,size_t count,ws_file *f) {
    stream *s=lookup(f);if(!s)return 0;
    if(!size||!count)return 0;
    if(!data||count>SIZE_MAX/size||!writable(f)){fail(EINVAL);f->flags|=0x40;return 0;}
    size_t bytes=size*count,done=0;
    while(done<bytes) {
        size_t n=bytes-done;if(n>INT_MAX)n=INT_MAX;
        int put;
        if(s->native){put=(int)fwrite((const char *)data+done,1,n,s->native);if(!put)fail(errno);}
        else put=s->write?s->write(s->cookie,(const char *)data+done,(int)n):fail(EBADF);
        if(put<=0||(size_t)put>n){f->flags|=0x40;break;}
        done+=(size_t)put;
    }
    f->read_count=f->write_count=0;return done/size;
}
int ws_fgetc(ws_file *f){unsigned char c;return ws_fread(&c,1,1,f)==1?c:EOF;}
int ws_fputc(int v,ws_file *f){unsigned char c=(unsigned char)v;return ws_fwrite(&c,1,1,f)==1?c:EOF;}
int ws_ungetc(int v,ws_file *f) {
    stream *s=lookup(f);if(!s||v==EOF||s->pushed>=0)return EOF;
    s->pushed=(unsigned char)v;f->flags&=~0x20u;return s->pushed;
}
int ws_fseek(ws_file *f,int32_t offset,int whence) {
    stream *s=lookup(f);if(!s)return -1;
    if(whence<0||whence>2)return fail(EINVAL);
    int pending=(s->pushed>=0)+(f->read_count>0?f->read_count:0);
    if(whence==SEEK_CUR){if((int64_t)offset-pending<INT32_MIN)return fail(EOVERFLOW);offset-=pending;}
    int r=s->native?fseek(s->native,offset,whence):s->seek?(s->seek(s->cookie,offset,whence)<0?-1:0):fail(ESPIPE);
    if(r && s->native)fail(errno);
    if(!r){s->pushed=-1;f->flags&=~0x20u;f->read_count=f->write_count=0;}
    return r;
}
int32_t ws_ftell(ws_file *f) {
    stream *s=lookup(f);if(!s)return -1;
    int64_t p=s->native?ftell(s->native):s->seek?s->seek(s->cookie,0,SEEK_CUR):fail(ESPIPE);
    if(p<0){if(s->native)fail(errno);return -1;}
    if(s->pushed>=0)--p;
    if(f->read_count>0)p-=f->read_count;
    return p<0||p>INT32_MAX?fail(EOVERFLOW):(int32_t)p;
}
int ws_fflush(ws_file *f) {
    if(!f){int r=fflush(NULL);return r?fail(errno):0;}
    stream *s=lookup(f);if(!s)return EOF;
    if(f->buffer&&f->position>=f->buffer&&writable(f)) {
        uint32_t n=f->position-f->buffer;
        if(n>(uint32_t)f->buffer_size)return fail(EIO);
        if(n&&cookie_write((void *)(uintptr_t)f->cookie,(const char *)(uintptr_t)f->buffer,(int)n)!=(int)n)return EOF;
        f->position=f->buffer;f->write_count=f->buffer_size;
    }
    if(!s->native)return 0; /* callbacks are unbuffered */
    int r=fflush(s->native);return r?fail(errno):0;
}
int ws_vfprintf(ws_file *f,const char *format,va_list args) {
    char small[1024];va_list copy;va_copy(copy,args);
    int n=vsnprintf(small,sizeof(small),format,copy);va_end(copy);
    if(n<0)return fail(errno);
    if((unsigned)n>16u*1024u*1024u)return fail(EOVERFLOW);
    char *text=small;
    if((size_t)n>=sizeof(small)) {
        text=malloc((size_t)n+1);if(!text)return fail(ENOMEM);
        va_copy(copy,args);int actual=vsnprintf(text,(size_t)n+1,format,copy);va_end(copy);
        if(actual!=n){free(text);return fail(EIO);}
    }
    size_t written=ws_fwrite(text,1,(size_t)n,f);if(text!=small)free(text);
    return written==(size_t)n?n:-1;
}
static int ws_fprintf(ws_file *f,const char *fmt,...) {
    va_list a;va_start(a,fmt);int r=ws_vfprintf(f,fmt,a);va_end(a);return r;
}
static int ws_vprintf(const char *fmt,va_list a){return ws_vfprintf(&ws_standard_files[1],fmt,a);}
static int ws_fputs(const char *t,ws_file *f){size_t n=strlen(t);return ws_fwrite(t,1,n,f)==n?0:EOF;}
static int ws_puts(const char *t){return ws_fputs(t,&ws_standard_files[1])<0?EOF:ws_fputc('\n',&ws_standard_files[1]);}
static int ws_putchar(int c){return ws_fputc(c,&ws_standard_files[1]);}
static char *ws_fgets(char *out,int n,ws_file *f) {
    if(!out||n<=0){fail(EINVAL);return NULL;}
    int i=0,c;while(i<n-1&&(c=ws_fgetc(f))!=EOF){out[i++]=(char)c;if(c=='\n')break;}
    out[i]=0;return i||n==1?out:NULL;
}
static void ws_clearerr(ws_file *f){stream *s=lookup(f);if(s){f->flags&=~0x60u;if(s->native)clearerr(s->native);}}
static void ws_rewind(ws_file *f){ws_fseek(f,0,SEEK_SET);ws_clearerr(f);}
static int ws_setvbuf(ws_file *f,char *b,int mode,size_t size) {
    stream *s=lookup(f);if(!s)return -1;
    if(mode<0||mode>2)return fail(EINVAL);
    if(!s->native)return mode==2?0:fail(ENOTSUP);
    int modes[]={_IOFBF,_IOLBF,_IONBF};int r=setvbuf(s->native,b,modes[mode],size);
    return r?fail(errno):0;
}
static void ws_setbuf(ws_file *f,char *b){(void)ws_setvbuf(f,b,b?0:2,b?1024:0);}
static ws_file *ws_freopen(const char *p,const char *m,ws_file *f) {
    if(!lookup(f))return NULL;
    ws_fclose(f);ws_file *replacement=ws_fopen(p,m);
    if(!replacement)return NULL;
    if(f!=replacement)*f=*replacement;
    return f;
}
static int ws_fscanf(ws_file *f,const char *fmt,...) {
    stream *s=lookup(f);if(!s)return EOF;
    if(!s->native)return fail(ENOTSUP);
    if(s->pushed>=0){ungetc(s->pushed,s->native);s->pushed=-1;}
    va_list a;va_start(a,fmt);int r=vfscanf(s->native,fmt,a);va_end(a);
    if(ferror(s->native)){f->flags|=0x40;fail(errno);}if(feof(s->native))f->flags|=0x20;
    return r;
}
static int ws_srefill(ws_file *f) {
    int c=ws_fgetc(f);if(c==EOF)return EOF;
    f->tiny_buffer[0]=(unsigned char)c;f->position=(uint32_t)(uintptr_t)f->tiny_buffer;
    f->read_count=1;return 0;
}
static int ws_swsetup(ws_file *f){
    if(!lookup(f)||!writable(f))return fail(EBADF);
    f->flags|=8|2;f->write_count=f->line_buffer_size=0;return 0;
}
static stream *cookie_lookup(void *cookie) {
    uint32_t id=(uint32_t)(uintptr_t)cookie;stream *s=&streams[id&(STREAMS-1)];
    if(!id||s->token!=id){fail(EBADF);return NULL;}return s;
}
static int cookie_read(void *cookie,char *out,int bytes) {
    stream *s=cookie_lookup(cookie);if(!s||bytes<0)return -1;
    if(s->native){size_t n=fread(out,1,(size_t)bytes,s->native);return !n&&ferror(s->native)?fail(errno):(int)n;}
    return s->read?s->read(s->cookie,out,bytes):fail(EBADF);
}
static int cookie_write(void *cookie,const char *in,int bytes) {
    stream *s=cookie_lookup(cookie);if(!s||bytes<0)return -1;
    if(s->native){size_t n=fwrite(in,1,(size_t)bytes,s->native);return !n&&bytes?fail(errno):(int)n;}
    return s->write?s->write(s->cookie,in,bytes):fail(EBADF);
}
static int32_t cookie_seek(void *cookie,int32_t offset,int origin) {
    stream *s=cookie_lookup(cookie);if(!s)return -1;
    if(!s->native)return s->seek?s->seek(s->cookie,offset,origin):fail(ESPIPE);
    if(fseek(s->native,offset,origin))return fail(errno);
    long p=ftell(s->native);return p<0?fail(errno):p>INT32_MAX?fail(EOVERFLOW):(int32_t)p;
}
static int cookie_close(void *cookie) {
    stream *s=cookie_lookup(cookie);return s?ws_fclose(&s->facade):EOF;
}
uintptr_t ws_stdio_symbol(const char *name) {
    if(!name)return 0;
    if(!strcmp(name,"__sF"))return (uintptr_t)ws_standard_files;
    if(!strcmp(name,"__srget")||!strcmp(name,"getc"))return (uintptr_t)ws_fgetc;
    if(!strcmp(name,"__swbuf")||!strcmp(name,"putc"))return (uintptr_t)ws_fputc;
#define S(n) if(!strcmp(name,#n))return (uintptr_t)ws_##n
    S(fopen);S(funopen);S(fclose);S(fread);S(fwrite);S(fgetc);S(fputc);S(fseek);S(ftell);
    S(ungetc);S(fflush);S(vfprintf);S(fprintf);S(vprintf);S(fputs);S(puts);S(putchar);
    S(fgets);S(clearerr);S(rewind);S(setvbuf);S(setbuf);S(freopen);S(fscanf);
#undef S
    if(!strcmp(name,"__srefill"))return (uintptr_t)ws_srefill;
    if(!strcmp(name,"__swsetup"))return (uintptr_t)ws_swsetup;
    return 0;
}
