#include "ws_log.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

enum {SLOTS=64,BYTES=2048};
typedef struct {size_t length;char data[BYTES];} record;
static record queue[SLOTS];
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ready=PTHREAD_COND_INITIALIZER;
static pthread_t thread;
static atomic_int running;
static atomic_uint dropped;
static unsigned head,count;
static int stopping;
static ws_log_sink sink;
static void *sink_context;
static void file_sink(const char *text,size_t length,void *unused){
    (void)unused;fwrite(text,1,length,stderr);
}
static void *writer(void *unused){
    (void)unused;record next;unsigned reported=0;
    for(;;){
        pthread_mutex_lock(&lock);
        while(!count&&!stopping)pthread_cond_wait(&ready,&lock);
        if(!count&&stopping){pthread_mutex_unlock(&lock);break;}
        next=queue[head];head=(head+1)%SLOTS;--count;
        pthread_mutex_unlock(&lock);
        /* Never hold the producer lock while accessing storage. */
        sink(next.data,next.length,sink_context);
        unsigned lost=atomic_load(&dropped);
        if(lost!=reported){
            char message[96];int n=snprintf(message,sizeof(message),"Wayward log: %u messages omitted (bounded queue).\n",lost-reported);
            sink(message,(size_t)n,sink_context);reported=lost;
        }
    }
    return NULL;
}
int ws_log_start(ws_log_sink output,void *context){
    if(atomic_load(&running))return -1;
    sink=output?output:file_sink;sink_context=context;
    head=count=0;stopping=0;atomic_store(&dropped,0);
    pthread_attr_t attributes;int error=pthread_attr_init(&attributes);
    if(error)return error;
    error=pthread_attr_setstacksize(&attributes,64*1024);
    if(!error)error=pthread_create(&thread,&attributes,writer,NULL);
    pthread_attr_destroy(&attributes);
    if(!error)atomic_store(&running,1);
    return error;
}
void ws_log_stop(void){
    if(!atomic_exchange(&running,0))return;
    pthread_mutex_lock(&lock);stopping=1;pthread_cond_signal(&ready);pthread_mutex_unlock(&lock);
    pthread_join(thread,NULL);fflush(stderr);
}
unsigned ws_log_dropped(void){return atomic_load(&dropped);}
int ws_log_vprint(int priority,const char *tag,const char *format,va_list args){
    if(!format)return -1;
    record item;
    int prefix=snprintf(item.data,128,"[%s] ",tag?tag:"Wayward");
    size_t offset=prefix<0?0:prefix>127?127:(size_t)prefix;
    int n=vsnprintf(item.data+offset,BYTES-offset-1,format,args);
    if(n<0)return n;
    size_t used=offset+(size_t)n;
    if(used>BYTES-2)used=BYTES-2;
    while(used&&(item.data[used-1]=='\n'||item.data[used-1]=='\r'))--used;
    item.data[used++]='\n';item.data[used]=0;item.length=used;
    if(!atomic_load(&running)||priority>=7){
        /* Fatal logs stay synchronous so a following abort cannot lose them. */
        (sink?sink:file_sink)(item.data,item.length,sink_context);return n;
    }
    /* Log pressure may discard messages, never block rendering or grow RAM. */
    if(pthread_mutex_trylock(&lock)){atomic_fetch_add(&dropped,1);return n;}
    if(count==SLOTS||stopping)atomic_fetch_add(&dropped,1);
    else{queue[(head+count)%SLOTS]=item;++count;pthread_cond_signal(&ready);}
    pthread_mutex_unlock(&lock);return n;
}
int ws_log_print(int priority,const char *tag,const char *format,...){
    va_list args;va_start(args,format);int n=ws_log_vprint(priority,tag,format,args);va_end(args);return n;
}
