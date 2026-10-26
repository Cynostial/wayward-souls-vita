#define _GNU_SOURCE 1
#include "ws_libc.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
static int *(*cell)(void);
static char ctype_data[257];static int16_t lower_data[257],upper_data[257];
static const char *ctype_pointer=ctype_data;static const int16_t *lower_pointer=lower_data,*upper_pointer=upper_data;
/* Bionic 4.4 exports pointer VARIABLES, not the arrays exported by newlib. */
static unsigned classify(int c){return c==-1?0:(unsigned char)ctype_data[(unsigned char)c+1];}
static int b_isalnum(int c){return classify(c)&7;}static int b_isalpha(int c){return classify(c)&3;}
static int b_isspace(int c){return classify(c)&8;}static int b_isupper(int c){return classify(c)&1;}
static int b_isxdigit(int c){return classify(c)&0x44;}static int b_isblank(int c){return c==' '||c=='\t';}
static int b_tolower(int c){return c>=-1&&c<=255?lower_data[c+1]:c;}static int b_toupper(int c){return c>=-1&&c<=255?upper_data[c+1]:c;}
static void math_error(int e){if(e)*cell()=e==EDOM?33:e==ERANGE?34:e==EINVAL?22:5;}
#define UNARY(name,type) static type b_##name(type x){int saved=errno;errno=0;type result=name(x);int error=errno;errno=saved;math_error(error);return result;}
#define BINARY(name,type) static type b_##name(type x,type y){int saved=errno;errno=0;type result=name(x,y);int error=errno;errno=saved;math_error(error);return result;}
UNARY(acos,double) UNARY(acosf,float) UNARY(acosh,double) UNARY(asin,double) UNARY(asinf,float) UNARY(asinh,double)
UNARY(atan,double) UNARY(atanf,float) UNARY(atanh,double) BINARY(atan2,double) BINARY(atan2f,float)
UNARY(cbrt,double) UNARY(ceil,double) UNARY(ceilf,float) UNARY(cosf,float) UNARY(cosh,double)
UNARY(erf,double) UNARY(erfc,double) UNARY(exp,double) UNARY(exp2,double) UNARY(expf,float) UNARY(expm1,double)
UNARY(floor,double) UNARY(floorf,float) BINARY(fmod,double) BINARY(fmodf,float) BINARY(hypot,double)
UNARY(lgamma,double) UNARY(log,double) UNARY(logf,float) UNARY(log10,double) UNARY(log1p,double) UNARY(logb,double)
UNARY(nearbyint,double) BINARY(nextafter,double) BINARY(nextafterf,float) BINARY(pow,double) BINARY(powf,float)
BINARY(remainder,double) UNARY(rint,double) UNARY(round,double) UNARY(sinf,float) UNARY(sinh,double)
UNARY(sqrt,double) UNARY(sqrtf,float) UNARY(tan,double) UNARY(tanf,float) UNARY(tanh,double) UNARY(tgamma,double) UNARY(trunc,double)
#undef UNARY
#undef BINARY
/* Particle circles/stars repeatedly evaluate identical double angles. Keep
 * exact libm results, never an approximation or rounded key. Per-thread
 * storage avoids a shared lock/data race; there is one TLS lookup per call.
 * The pinned game does not change its floating-point rounding mode. Cache
 * only finite normal results from bounded angles without errno side effects;
 * exceptional inputs/results retain the original numerical/error path. */
typedef struct {uint64_t key;double value[2];unsigned valid;} trig_entry;
typedef struct {trig_entry entries[256][2];unsigned char next[256];} trig_cache;
static _Thread_local trig_cache trig;
static double cached_trig(double x,unsigned op){
    trig_entry *entry=NULL;
    if(isfinite(x)&&x>=-25.132741228718345&&x<=25.132741228718345){
        uint64_t bits;memcpy(&bits,&x,sizeof(bits));
        uint32_t hash=(uint32_t)bits^(uint32_t)(bits>>32);
        hash^=hash>>16;hash*=0x7feb352du;hash^=hash>>15;
        unsigned set=hash&255;trig_cache *cache=&trig;
        for(unsigned i=0;i<2;++i){
            trig_entry *candidate=&cache->entries[set][i];
            if(candidate->valid&&candidate->key==bits){
                if(candidate->valid&(1u<<op))return candidate->value[op];
                entry=candidate;break;
            }
            if(!candidate->valid)entry=candidate;
        }
        if(!entry){entry=&cache->entries[set][cache->next[set]];cache->next[set]^=1;}
        if(!entry->valid||entry->key!=bits){entry->key=bits;entry->valid=0;}
    }
    int saved=errno;errno=0;double result=op?cos(x):sin(x);int error=errno;errno=saved;
    math_error(error);
    if(entry&&!error&&(isnormal(result)||result==0.0)){entry->value[op]=result;entry->valid|=1u<<op;}
    return result;
}
static double b_sin(double x){return cached_trig(x,0);}
static double b_cos(double x){return cached_trig(x,1);}
#define SCALE(name,type) static type b_##name(type x,int y){int saved=errno;errno=0;type result=name(x,y);int error=errno;errno=saved;math_error(error);return result;}
SCALE(ldexp,double) SCALE(scalbn,double) SCALE(scalbnl,long double)
#undef SCALE
static long b_lrint(double x){int saved=errno;errno=0;long n=lrint(x);int error=errno;errno=saved;math_error(error);return n;}
static long long b_llrint(double x){int saved=errno;errno=0;long long n=llrint(x);int error=errno;errno=saved;math_error(error);return n;}
static double b_remquo(double x,double y,int *q){int saved=errno;errno=0;double n=remquo(x,y,q);int error=errno;errno=saved;math_error(error);return n;}
static int fpclass_double(double x){uint64_t bits;memcpy(&bits,&x,8);uint64_t exponent=(bits>>52)&2047,mantissa=bits&UINT64_C(0xfffffffffffff);return exponent==2047?(mantissa?2:1):!exponent?(mantissa?8:16):4;}
static int fpclass_float(float x){uint32_t bits;memcpy(&bits,&x,4);unsigned exponent=(bits>>23)&255,mantissa=bits&0x7fffff;return exponent==255?(mantissa?2:1):!exponent?(mantissa?8:16):4;}
static int is_finite(double x){return fpclass_double(x)>2;}static int is_nan(double x){return fpclass_double(x)==2;}
static int sign_bit(double x){uint64_t bits;memcpy(&bits,&x,8);return (int)(bits>>63);}
#define PARSE_INT(name,type) static type b_##name(const char *text,char **end,int base){int saved=errno;errno=0;type n=name(text,end,base);int error=errno;errno=saved;math_error(error);return n;}
PARSE_INT(strtol,long) PARSE_INT(strtoll,long long) PARSE_INT(strtoul,unsigned long) PARSE_INT(strtoull,unsigned long long)
#undef PARSE_INT
static double b_strtod(const char *text,char **end){int saved=errno;errno=0;double n=strtod(text,end);int error=errno;errno=saved;math_error(error);return n;}
static size_t b_strlcpy(char *out,const char *in,size_t n){size_t size=strlen(in);if(n){size_t copy=size<n-1?size:n-1;memcpy(out,in,copy);out[copy]=0;}return size;}
static char *b_strsep(char **text,const char *separators){if(!text||!*text)return NULL;char *first=*text,*end=first+strcspn(first,separators);if(*end){*end++=0;*text=end;}else *text=NULL;return first;}
static void *b_memmem(const void *bytes,size_t count,const void *needle,size_t size){
    if(!size)return (void *)bytes;
    if(size>count)return NULL;
    const unsigned char *p=bytes;
    for(size_t i=0;i<=count-size;i++)if(!memcmp(p+i,needle,size))return (void *)(p+i);
    return NULL;
}
static int b_strcasecmp(const char *a,const char *b){for(;;a++,b++){int x=b_tolower((unsigned char)*a),y=b_tolower((unsigned char)*b);if(x!=y||!x)return x-y;}}
static int b_strncasecmp(const char *a,const char *b,size_t n){while(n--){int x=b_tolower((unsigned char)*a++),y=b_tolower((unsigned char)*b++);if(x!=y||!x)return x-y;}return 0;}
static char *b_strcasestr(const char *text,const char *needle){size_t n=strlen(needle);if(!n)return (char *)text;for(;*text;text++)if(!b_strncasecmp(text,needle,n))return (char *)text;return NULL;}
static size_t b_strxfrm(char *out,const char *in,size_t n){size_t len=strlen(in),copy=len<n?len+1:n;if(copy)memcpy(out,in,copy);return len;}
static char *b_setlocale(int category,const char *locale){static char c[]="C";if(category<0||category>6)return NULL;return !locale||!*locale||!strcmp(locale,"C")||!strcmp(locale,"POSIX")?c:NULL;}
static void aeabi_set(void *p,size_t n,int value){memset(p,value,n);}
static size_t b_wcslen(const uint32_t *s){size_t n=0;while(s[n])n++;return n;}
static int b_wcscmp(const uint32_t *a,const uint32_t *b){while(*a&&*a==*b){a++;b++;}return *a==*b?0:*a<*b?-1:1;}
static int b_wmemcmp(const uint32_t *a,const uint32_t *b,size_t n){for(size_t i=0;i<n;i++)if(a[i]!=b[i])return a[i]<b[i]?-1:1;return 0;}
static uint32_t *b_wmemchr(const uint32_t *s,uint32_t c,size_t n){for(size_t i=0;i<n;i++)if(s[i]==c)return (uint32_t *)(s+i);return NULL;}
static uint32_t *b_wmemcpy(uint32_t *d,const uint32_t *s,size_t n){memcpy(d,s,n*4);return d;}
static uint32_t *b_wmemmove(uint32_t *d,const uint32_t *s,size_t n){memmove(d,s,n*4);return d;}
static uint32_t *b_wmemset(uint32_t *d,uint32_t c,size_t n){for(size_t i=0;i<n;i++)d[i]=c;return d;}
static size_t b_wcsxfrm(uint32_t *d,const uint32_t *s,size_t n){size_t len=b_wcslen(s),copy=len<n?len+1:n;if(copy)b_wmemcpy(d,s,copy);return len;}
int ws_libc_install(int *(*android_errno)(void)){
    if(!android_errno||cell)return -1;
    cell=android_errno;lower_data[0]=upper_data[0]=-1;
    for(int c=0;c<256;c++){
        unsigned bits=0;if(c<32||c==127)bits|=0x20;if(c==' '||(c>=9&&c<=13))bits|=8;if(c==' ')bits|=0x80;
        if(c>='A'&&c<='Z')bits|=1;
        if(c>='a'&&c<='z')bits|=2;
        if(c>='0'&&c<='9')bits|=4;
        if((c>='A'&&c<='F')||(c>='a'&&c<='f'))bits|=0x40;
        if(c>=33&&c<=126&&!(bits&7))bits|=0x10;
        ctype_data[c+1]=(char)bits;lower_data[c+1]=(int16_t)(c>='A'&&c<='Z'?c+32:c);upper_data[c+1]=(int16_t)(c>='a'&&c<='z'?c-32:c);
    }return 0;
}
uintptr_t ws_libc_resolve(const char *name){
    if(!name||!cell)return 0;
    #define ENTRY(n,f) if(!strcmp(name,n))return (uintptr_t)(f)
    #define WRAP(n) ENTRY(#n,b_##n)
    #define DIRECT(n) ENTRY(#n,n)
    ENTRY("_ctype_",&ctype_pointer);ENTRY("_tolower_tab_",&lower_pointer);ENTRY("_toupper_tab_",&upper_pointer);
    WRAP(isalnum);WRAP(isalpha);WRAP(isspace);WRAP(isupper);WRAP(isxdigit);WRAP(isblank);WRAP(tolower);WRAP(toupper);
    WRAP(acos);WRAP(acosf);WRAP(acosh);WRAP(asin);WRAP(asinf);WRAP(asinh);WRAP(atan);WRAP(atanf);WRAP(atanh);WRAP(atan2);WRAP(atan2f);
    WRAP(cbrt);WRAP(ceil);WRAP(ceilf);WRAP(cos);WRAP(cosf);WRAP(cosh);WRAP(erf);WRAP(erfc);WRAP(exp);WRAP(exp2);WRAP(expf);WRAP(expm1);
    WRAP(floor);WRAP(floorf);WRAP(fmod);WRAP(fmodf);WRAP(hypot);WRAP(lgamma);WRAP(log);WRAP(logf);WRAP(log10);WRAP(log1p);WRAP(logb);
    WRAP(nearbyint);WRAP(nextafter);WRAP(nextafterf);WRAP(pow);WRAP(powf);WRAP(remainder);WRAP(remquo);WRAP(rint);WRAP(round);
    WRAP(sin);WRAP(sinf);WRAP(sinh);WRAP(sqrt);WRAP(sqrtf);WRAP(tan);WRAP(tanf);WRAP(tanh);WRAP(tgamma);WRAP(trunc);
    WRAP(ldexp);WRAP(scalbn);WRAP(scalbnl);WRAP(lrint);WRAP(llrint);DIRECT(frexp);DIRECT(modf);DIRECT(modff);
    ENTRY("__fpclassifyd",fpclass_double);ENTRY("__fpclassifyf",fpclass_float);ENTRY("__isfinite",is_finite);ENTRY("isnan",is_nan);ENTRY("__signbit",sign_bit);
    WRAP(strtol);WRAP(strtoll);WRAP(strtoul);WRAP(strtoull);WRAP(strtod);DIRECT(atoi);DIRECT(atol);DIRECT(div);DIRECT(qsort);DIRECT(bsearch);
    DIRECT(memchr);DIRECT(memcmp);DIRECT(memcpy);DIRECT(memmove);DIRECT(memset);WRAP(memmem);
    DIRECT(strcat);DIRECT(strchr);DIRECT(strcmp);DIRECT(strcpy);DIRECT(strlen);DIRECT(strncat);DIRECT(strncmp);DIRECT(strncpy);DIRECT(strpbrk);DIRECT(strrchr);DIRECT(strstr);DIRECT(strtok);DIRECT(strtok_r);
    WRAP(strlcpy);WRAP(strsep);WRAP(strcasecmp);WRAP(strncasecmp);WRAP(strcasestr);WRAP(strxfrm);ENTRY("strcoll",strcmp);WRAP(setlocale);
    DIRECT(snprintf);DIRECT(vsnprintf);DIRECT(sprintf);DIRECT(vsprintf);DIRECT(sscanf);DIRECT(vsscanf);
    ENTRY("__aeabi_memcpy",memcpy);ENTRY("__aeabi_memmove",memmove);ENTRY("__aeabi_memset",aeabi_set);
    WRAP(wcslen);WRAP(wcscmp);WRAP(wmemcmp);WRAP(wmemchr);WRAP(wmemcpy);WRAP(wmemmove);WRAP(wmemset);WRAP(wcsxfrm);ENTRY("wcscoll",b_wcscmp);
    #undef DIRECT
    #undef WRAP
    #undef ENTRY
    return 0;
}
