#ifndef WS_STDIO_H
#define WS_STDIO_H
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
typedef struct {
    uint32_t position;int32_t read_count,write_count;
    uint16_t flags;int16_t descriptor;
    uint32_t buffer;int32_t buffer_size,line_buffer_size;
    uint32_t cookie,close_fn,read_fn,seek_fn,write_fn;
    uint32_t extension;int32_t extension_size;
    uint32_t saved_position;int32_t saved_read;
    unsigned char unget_buffer[3],tiny_buffer[1];
    uint32_t line_buffer;int32_t line_size,block_size,offset;
} ws_file;
_Static_assert(sizeof(ws_file)==84,"Bionic ARM FILE stride");
_Static_assert(offsetof(ws_file,cookie)==28,"Bionic FILE cookie");
extern ws_file ws_standard_files[3];
int ws_stdio_init(void);
ws_file *ws_fopen(const char *,const char *);
ws_file *ws_funopen(const void *,int (*)(void *,char *,int),
    int (*)(void *,const char *,int),int32_t (*)(void *,int32_t,int),int (*)(void *));
int ws_fclose(ws_file *);
size_t ws_fread(void *,size_t,size_t,ws_file *);
size_t ws_fwrite(const void *,size_t,size_t,ws_file *);
int ws_fgetc(ws_file *);
int ws_fputc(int,ws_file *);
int ws_fseek(ws_file *,int32_t,int);
int32_t ws_ftell(ws_file *);
int ws_ungetc(int,ws_file *);
int ws_fflush(ws_file *);
int ws_vfprintf(ws_file *,const char *,va_list);
uintptr_t ws_stdio_symbol(const char *);
#endif
