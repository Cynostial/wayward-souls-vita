#include "ws_env.h"
#include "ws_io.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#define LIMIT 128
static char *entries[LIMIT+1];
static char **environment=entries;
static size_t count,allocated;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static int index_of(const char *name,size_t len) {
    for(size_t i=0;i<count;++i)if(!strncmp(entries[i],name,len)&&entries[i][len]=='=')return (int)i;
    return -1;
}
char *ws_getenv(const char *name) {
    if(!name||!*name||strchr(name,'='))return NULL;
    size_t len=strlen(name);pthread_mutex_lock(&lock);int i=index_of(name,len);
    char *p=i>=0?entries[i]+len+1:NULL;pthread_mutex_unlock(&lock);return p;
}
int ws_setenv(const char *name,const char *value,int overwrite) {
    if(!name||!*name||strchr(name,'=')||!value){*ws_bionic_errno_location()=22;return -1;}
    size_t n=strlen(name),v=strlen(value);
    if(n>16384||v>16384){*ws_bionic_errno_location()=7;return -1;}
    pthread_mutex_lock(&lock);int i=index_of(name,n);
    if(i>=0&&!overwrite){pthread_mutex_unlock(&lock);return 0;}
    size_t bytes=n+v+2;
    if((i<0&&count==LIMIT)||bytes>256u*1024u-allocated){pthread_mutex_unlock(&lock);*ws_bionic_errno_location()=12;return -1;}
    char *p=malloc(bytes);if(!p){pthread_mutex_unlock(&lock);*ws_bionic_errno_location()=12;return -1;}
    memcpy(p,name,n);p[n]='=';memcpy(p+n+1,value,v+1);allocated+=bytes;
    if(i<0)i=(int)count++;
    /* getenv results may outlive a concurrent replacement. Retain old values
     * within the explicit 256 KiB process-lifetime budget. */
    entries[i]=p;entries[count]=NULL;pthread_mutex_unlock(&lock);return 0;
}
int ws_unsetenv(const char *name) {
    if(!name||!*name||strchr(name,'=')){*ws_bionic_errno_location()=22;return -1;}
    pthread_mutex_lock(&lock);int i=index_of(name,strlen(name));
    if(i>=0){for(size_t j=(size_t)i;j<count;++j)entries[j]=entries[j+1];--count;}
    pthread_mutex_unlock(&lock);return 0;
}
static int put(char *entry) {
    if(!entry){*ws_bionic_errno_location()=22;return -1;}
    char *equal=strchr(entry,'=');if(!equal)return ws_unsetenv(entry);
    size_t n=(size_t)(equal-entry);if(n>=1024){*ws_bionic_errno_location()=7;return -1;}
    char name[1024];memcpy(name,entry,n);name[n]=0;
    /* Bionic putenv retains the supplied storage. Keep that contract. */
    if(!n){*ws_bionic_errno_location()=22;return -1;}
    pthread_mutex_lock(&lock);int i=index_of(name,n);
    if(i<0&&count==LIMIT){pthread_mutex_unlock(&lock);*ws_bionic_errno_location()=12;return -1;}
    if(i<0)i=(int)count++;
    entries[i]=entry;entries[count]=NULL;pthread_mutex_unlock(&lock);return 0;
}
uintptr_t ws_env_symbol(const char *name) {
    if(!strcmp(name,"environ"))return (uintptr_t)&environment;
    if(!strcmp(name,"getenv"))return (uintptr_t)ws_getenv;
    if(!strcmp(name,"setenv"))return (uintptr_t)ws_setenv;
    if(!strcmp(name,"unsetenv"))return (uintptr_t)ws_unsetenv;
    if(!strcmp(name,"putenv"))return (uintptr_t)put;
    return 0;
}
