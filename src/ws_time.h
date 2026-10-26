#ifndef WS_TIME_H
#define WS_TIME_H
#include "ws_sync.h"
#include <stddef.h>
typedef struct { int32_t seconds,microseconds; } ws_timeval;
typedef struct {
    int32_t second,minute,hour,day,month,year,weekday,yearday,dst,utc_offset;
    const char *zone;
} ws_tm;
#if defined(__arm__)
_Static_assert(sizeof(ws_tm)==44,"Android ARM struct tm ABI");
#endif
int ws_b_clock_gettime(int,ws_bionic_timespec *);
int ws_b_clock_getres(int,ws_bionic_timespec *);
int ws_b_gettimeofday(ws_timeval *,void *timezone);
int ws_b_nanosleep(const ws_bionic_timespec *,ws_bionic_timespec *);
int ws_b_usleep(uint32_t);
unsigned ws_b_sleep(unsigned);
int32_t ws_b_clock(void);
int32_t ws_b_time(int32_t *);
ws_tm *ws_b_gmtime(const int32_t *);
ws_tm *ws_b_localtime(const int32_t *);
int32_t ws_b_mktime(ws_tm *);
size_t ws_b_strftime(char *,size_t,const char *,const ws_tm *);
#ifndef _WIN32
char *ws_b_strptime(const char *,const char *,ws_tm *);
#endif
void ws_b_tzset(void);
uintptr_t ws_time_symbol(const char *);
#endif
