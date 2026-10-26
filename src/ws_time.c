#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "ws_time.h"
#include "ws_io.h"
#include "ws_env.h"
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
static int32_t guest_timezone;
static char names[2][64]={"UTC","UTC"};
static char *guest_tzname[2]={names[0],names[1]};
static pthread_mutex_t timezone_lock=PTHREAD_MUTEX_INITIALIZER;
void ws_b_tzset(void) {
    const char *zone=ws_getenv("TZ");if(!zone||!*zone)zone="UTC0";
    pthread_mutex_lock(&timezone_lock);
#ifdef _WIN32
    if(_putenv_s("TZ",zone)){pthread_mutex_unlock(&timezone_lock);return;}
    _tzset();guest_timezone=(int32_t)_timezone;
#else
    if(setenv("TZ",zone,1)){ws_errno_publish();pthread_mutex_unlock(&timezone_lock);return;}
    tzset();
#ifdef __vita__
    guest_timezone=(int32_t)_timezone;
#else
    extern long timezone;guest_timezone=(int32_t)timezone;
#endif
#endif
    for(int i=0;i<2;++i){const char *s=tzname[i]?tzname[i]:"";size_t n=strlen(s);if(n>63)n=63;memcpy(names[i],s,n);names[i][n]=0;}
    pthread_mutex_unlock(&timezone_lock);
}
uintptr_t ws_time_symbol(const char *name) {
#ifndef _WIN32
    if(!strcmp(name,"strptime"))return (uintptr_t)ws_b_strptime;
#endif
    if(!strcmp(name,"tzset"))return (uintptr_t)ws_b_tzset;
    if(!strcmp(name,"timezone"))return (uintptr_t)&guest_timezone;
    if(!strcmp(name,"tzname"))return (uintptr_t)guest_tzname;
    return 0;
}
static _Thread_local ws_tm result_tm;
static int fail(int error) { errno=error; ws_errno_publish(); return -1; }
static int clock_id(int android,clockid_t *native) {
    switch(android) {
    case 0:*native=CLOCK_REALTIME; return 0;
    case 1:*native=CLOCK_MONOTONIC; return 0;
#ifdef CLOCK_PROCESS_CPUTIME_ID
    case 2:*native=CLOCK_PROCESS_CPUTIME_ID; return 0;
#endif
#ifdef CLOCK_THREAD_CPUTIME_ID
    case 3:*native=CLOCK_THREAD_CPUTIME_ID; return 0;
#endif
    default:return EINVAL;
    }
}
static int convert(const struct timespec *native,ws_bionic_timespec *out) {
    if(native->tv_sec<INT32_MIN || native->tv_sec>INT32_MAX || native->tv_nsec<0 || native->tv_nsec>=1000000000)
        return fail(EOVERFLOW);
    *out=(ws_bionic_timespec){(int32_t)native->tv_sec,(int32_t)native->tv_nsec}; return 0;
}
int ws_b_clock_gettime(int id,ws_bionic_timespec *out) {
    if(!out) return fail(EFAULT);
    clockid_t native; int error=clock_id(id,&native); if(error) return fail(error);
    struct timespec value; if(clock_gettime(native,&value)<0) { ws_errno_publish(); return -1; }
    return convert(&value,out);
}
int ws_b_clock_getres(int id,ws_bionic_timespec *out) {
    clockid_t native; int error=clock_id(id,&native); if(error) return fail(error);
    struct timespec value; if(clock_getres(native,&value)<0) { ws_errno_publish(); return -1; }
    return out ? convert(&value,out) : 0;
}
int ws_b_gettimeofday(ws_timeval *out,void *timezone) {
    if(timezone) return fail(ENOTSUP);
    struct timeval value;
    if(gettimeofday(&value,NULL)<0) { ws_errno_publish(); return -1; }
    if(value.tv_sec<INT32_MIN || value.tv_sec>INT32_MAX) return fail(EOVERFLOW);
    if(out) *out=(ws_timeval){(int32_t)value.tv_sec,(int32_t)value.tv_usec};
    return 0;
}
int ws_b_nanosleep(const ws_bionic_timespec *request,ws_bionic_timespec *remaining) {
    if(!request) return fail(EFAULT);
    if(request->seconds<0 || request->nanoseconds<0 || request->nanoseconds>=1000000000) return fail(EINVAL);
    struct timespec native={request->seconds,request->nanoseconds},rest={0};
    int error=nanosleep(&native,&rest);
    if(error<0) {
        int saved=errno;
        if(saved==EINTR && remaining && convert(&rest,remaining)) return -1;
        return fail(saved);
    }
    return 0;
}
int ws_b_usleep(uint32_t us) {
    ws_bionic_timespec request={(int32_t)(us/1000000),(int32_t)(us%1000000)*1000};
    return ws_b_nanosleep(&request,NULL);
}
unsigned ws_b_sleep(unsigned seconds) {
    if(seconds>INT32_MAX) { fail(EINVAL); return seconds; }
    ws_bionic_timespec request={(int32_t)seconds,0},remaining={0};
    if(!ws_b_nanosleep(&request,&remaining)) return 0;
    return *ws_bionic_errno_location()==4 ? (unsigned)remaining.seconds+(remaining.nanoseconds!=0) : seconds;
}
int32_t ws_b_clock(void) {
    clock_t ticks=clock(); if(ticks==(clock_t)-1) { ws_errno_publish(); return -1; }
    uint64_t us=(uint64_t)ticks*1000000/CLOCKS_PER_SEC;
    if(us>INT32_MAX) return fail(EOVERFLOW);
    return (int32_t)us;
}
int32_t ws_b_time(int32_t *out) {
    time_t value=time(NULL);
    if(value==(time_t)-1) { ws_errno_publish(); return -1; }
    if(value<INT32_MIN || value>INT32_MAX) return fail(EOVERFLOW);
    if(out) *out=(int32_t)value;
    return (int32_t)value;
}
/* Calendar arithmetic is used only to derive local UTC offset. The native
 * conversion remains responsible for time-zone and daylight-saving rules. */
static int64_t calendar_seconds(const struct tm *t) {
    int64_t year=(int64_t)t->tm_year+1900,days=0;
    for(int64_t y=1970;y<year;++y) days+=365+(y%4==0 && (y%100!=0 || y%400==0));
    for(int64_t y=year;y<1970;++y) days-=365+(y%4==0 && (y%100!=0 || y%400==0));
    static const int lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};
    for(int m=0;m<t->tm_mon;++m) days+=lengths[m]+(m==1 && year%4==0 && (year%100!=0 || year%400==0));
    return (days+t->tm_mday-1)*86400+(int64_t)t->tm_hour*3600+t->tm_min*60+t->tm_sec;
}
static void export_tm(const struct tm *t,int32_t stamp,int local,ws_tm *out) {
    *out=(ws_tm){t->tm_sec,t->tm_min,t->tm_hour,t->tm_mday,t->tm_mon,t->tm_year,
        t->tm_wday,t->tm_yday,t->tm_isdst,(int32_t)(calendar_seconds(t)-stamp),"GMT"};
    if(local) {
#ifdef _WIN32
        out->zone=guest_tzname[t->tm_isdst>0];
#else
        out->zone=guest_tzname[t->tm_isdst>0];
#endif
    }
}
static struct tm import_tm(const ws_tm *t) {
    struct tm out={0}; out.tm_sec=t->second; out.tm_min=t->minute; out.tm_hour=t->hour;
    out.tm_mday=t->day; out.tm_mon=t->month; out.tm_year=t->year; out.tm_wday=t->weekday;
    out.tm_yday=t->yearday; out.tm_isdst=t->dst; return out;
}
static ws_tm *break_time(const int32_t *stamp,int local) {
    if(!stamp) { fail(EFAULT); return NULL; }
    time_t value=*stamp; struct tm native;
    pthread_mutex_lock(&timezone_lock);
    if(!(local ? localtime_r(&value,&native) : gmtime_r(&value,&native))) { ws_errno_publish();pthread_mutex_unlock(&timezone_lock);return NULL; }
    export_tm(&native,*stamp,local,&result_tm);pthread_mutex_unlock(&timezone_lock);return &result_tm;
}
ws_tm *ws_b_gmtime(const int32_t *stamp) { return break_time(stamp,0); }
ws_tm *ws_b_localtime(const int32_t *stamp) { return break_time(stamp,1); }
int32_t ws_b_mktime(ws_tm *t) {
    if(!t) return fail(EFAULT);
    pthread_mutex_lock(&timezone_lock);
    struct tm native=import_tm(t); errno=0; time_t value=mktime(&native);
    if(value==(time_t)-1 && errno) { ws_errno_publish();pthread_mutex_unlock(&timezone_lock);return -1; }
    if(value<INT32_MIN || value>INT32_MAX){pthread_mutex_unlock(&timezone_lock);return fail(EOVERFLOW);}
    export_tm(&native,(int32_t)value,1,t);pthread_mutex_unlock(&timezone_lock);return (int32_t)value;
}
size_t ws_b_strftime(char *out,size_t capacity,const char *format,const ws_tm *t) {
    if(!t) { fail(EFAULT); return 0; }
    struct tm native=import_tm(t);pthread_mutex_lock(&timezone_lock);size_t result=strftime(out,capacity,format,&native);pthread_mutex_unlock(&timezone_lock);return result;
}
#ifndef _WIN32
char *ws_b_strptime(const char *input,const char *format,ws_tm *t){
    if(!input||!format||!t){fail(EFAULT);return NULL;}
    /* Newlib has no tm_gmtoff/tm_zone storage in this SDK. Reject those
     * conversions instead of parsing a zone and discarding its meaning. */
    for(const char *p=format;*p;++p)if(*p=='%'){
        ++p;if(!*p)break;if(*p=='E'||*p=='O'){++p;if(!*p)break;}
        if(*p=='z'||*p=='Z'){fail(ENOTSUP);return NULL;}
    }
    struct tm native=import_tm(t);char *end=strptime(input,format,&native);
    if(end){t->second=native.tm_sec;t->minute=native.tm_min;t->hour=native.tm_hour;t->day=native.tm_mday;t->month=native.tm_mon;t->year=native.tm_year;t->weekday=native.tm_wday;t->yearday=native.tm_yday;t->dst=native.tm_isdst;}
    return end;
}
#endif
