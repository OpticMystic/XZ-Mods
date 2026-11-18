#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
static int create_calls,thread_fail_remaining,always_allocate_fail;
static int allocation,fail_allocation,thread_failure,active,closed,format_fail,foreign_mode,pin_fail,pin_supported,pin_calls;
static void *test_calloc(size_t n,size_t b){if(++allocation==fail_allocation||always_allocate_fail){errno=ENOMEM;return NULL;}void *p=calloc(n,b);if(p)++active;return p;}
static void *test_malloc(size_t n){if(++allocation==fail_allocation||always_allocate_fail){errno=ENOMEM;return NULL;}void *p=malloc(n);if(p)++active;return p;}
static void test_free(void *p){if(p)--active;free(p);errno=EIO;}
static int test_create(pthread_t *t,const pthread_attr_t *a,void *(*fn)(void *),void *v){++create_calls;if(thread_failure&&thread_fail_remaining!=0){if(thread_fail_remaining>0)--thread_fail_remaining;errno=ERANGE;return thread_failure;}return pthread_create(t,a,fn,v);}
#define calloc test_calloc
#define malloc test_malloc
#define free test_free
#define pthread_create test_create
#include "../overcue_stream.c"
#include "../stem_pool.c"
#undef calloc
#undef malloc
#undef free
#undef pthread_create
static struct xz_oc_assets *mock_assets;
static int16_t reference_sample(int64_t n,unsigned c){uint32_t x=(uint32_t)n*2654435761u+c*2246822519u;x^=x>>16;x*=2246822519u;x^=x>>13;return (int16_t)((int)(x%40001)-20000);}
float xz_oc_mix_gain(const struct xz_oc_assets *a,unsigned m){(void)a;(void)m;return 1;}
static uint64_t single_frames_read;
static int test_hold_loop,test_loop_parked;
struct xz_oc_file *xz_oc_mix_file(struct xz_oc_assets *a,unsigned m){assert(m==1||m==2);return &a->role[m==1?0:1];}
/* Drums and harmonics each carry half of the selected mix. */
int xz_oc_read(struct xz_oc_file *f,int64_t first,size_t n,int16_t *out){if(f!=&mock_assets->reference&&first>=290400&&first<300000&&__atomic_load_n(&test_hold_loop,__ATOMIC_ACQUIRE)){__atomic_store_n(&test_loop_parked,1,__ATOMIC_RELEASE);while(__atomic_load_n(&test_hold_loop,__ATOMIC_ACQUIRE)){struct timespec t={0,1000000};nanosleep(&t,NULL);}}if(f!=&mock_assets->reference)__atomic_fetch_add(&single_frames_read,n,__ATOMIC_RELAXED);for(size_t i=0;i<n;i++)for(unsigned c=0;c<2;c++)out[i*2+c]=f==&mock_assets->reference?reference_sample(first+(int64_t)i,c):(first+(int64_t)i<500000?2048:6144);return 0;}
int xz_oc_open(const char *p,struct xz_oc_assets *a,char *e,size_t n){(void)p;if(foreign_mode){snprintf(e,n,"OC FORMAT NOT SELECTED");return -1;}if(format_fail){snprintf(e,n,"EXACT FORMAT FAILURE");return -1;}memset(a,0,sizeof(*a));a->sample_rate=96000;a->frames=100000;e[0]=0;return 0;}
int xz_oc_bundle(const char *p,char *e,size_t n){(void)p;(void)e;(void)n;return foreign_mode?-1:0;}
int xz_oc_assets_pin(struct xz_oc_assets *a){(void)a;++pin_calls;return pin_fail?-1:0;}
/* The RAM copy of the stem files is no longer made by open: the stream's worker makes it (xz_oc_assets_pin_polled)
   when the RAM ledger supports it. pin_supported stands for the ledger's xz_ram_files_pin being present. */
int xz_oc_assets_pin_supported(void){return pin_supported;}
int xz_oc_assets_pin_polled(struct xz_oc_assets *a,int (*poll)(void *),void *context){(void)poll;(void)context;return xz_oc_assets_pin(a);}
void xz_oc_close(struct xz_oc_assets *a){(void)a;++closed;}
size_t xz_oc_assets_ram_bytes(const struct xz_oc_assets *a){(void)a;return 0;}
struct xz_foreign_stems *xz_foreign_open(const char *p,char *e,size_t n){(void)p;(void)n;assert(foreign_mode);e[0]=0;return (struct xz_foreign_stems *)(uintptr_t)1;}
void xz_foreign_close(struct xz_foreign_stems *f){(void)f;}
void xz_foreign_release_audio(struct xz_foreign_stems *f){(void)f;}
void xz_oc_assets_release_audio(struct xz_oc_assets *a){(void)a;}
unsigned xz_foreign_stem_count(const struct xz_foreign_stems *f){(void)f;return 4;}
uint32_t xz_foreign_rate(const struct xz_foreign_stems *f){(void)f;return 96000;}
uint64_t xz_foreign_frames(const struct xz_foreign_stems *f){(void)f;return 100000;}
enum xz_foreign_format xz_foreign_format(const struct xz_foreign_stems *f){(void)f;abort();}
int xz_foreign_read(struct xz_foreign_stems *f,unsigned m,int64_t p,size_t n,float *b){(void)f;(void)m;(void)p;(void)n;(void)b;abort();}
int xz_foreign_reference(struct xz_foreign_stems *f,int64_t p,size_t n,float *b){(void)f;(void)p;(void)n;(void)b;abort();}

static void reset(void){assert(active==0);allocation=fail_allocation=thread_failure=closed=format_fail=foreign_mode=pin_fail=pin_supported=pin_calls=0;}
int old_diagnostics_main(void){
 char error[160];for(int fail=1;fail<=9;fail++){reset();fail_allocation=fail;memset(error,0xa5,sizeof(error));struct xz_oc_stream *s=xz_oc_stream_open("fixture",44100,error,sizeof(error));assert(!s&&active==0);assert(closed==(fail>1));assert(memchr(error,0,sizeof(error)));assert(strstr(error,"ALLOCATION FAILED"));assert(strstr(error,"errno=12"));if(fail>1){char target[48];snprintf(target,sizeof(target),"window=%u role=%u",(unsigned)(fail-2)/2,(unsigned)(fail-2)%2);assert(strstr(error,target));assert(strstr(error,"bytes=1536000"));}puts(error);}
 for(int i=0;i<2;i++){reset();thread_failure=i?EPERM:EAGAIN;memset(error,0xa5,sizeof(error));assert(!xz_oc_stream_open("fixture",44100,error,sizeof(error)));assert(active==0&&closed==1);char expected[80];snprintf(expected,sizeof(expected),"STEM WORKER CREATE FAILED: rc=%d",thread_failure);assert(!strcmp(error,expected));puts(error);}
 reset();format_fail=1;assert(!xz_oc_stream_open("fixture",44100,error,sizeof(error)));assert(!strcmp(error,"EXACT FORMAT FAILURE")&&active==0);puts("PASS exact format error retained");
 reset();fail_allocation=2;char small[10];memset(small,0xa5,sizeof(small));assert(!xz_oc_stream_open("fixture",44100,small,sizeof(small)));assert(small[9]==0&&active==0);puts("PASS bounded diagnostic truncation");
 reset();memset(error,0xa5,sizeof(error));struct xz_oc_stream *s=xz_oc_stream_open("fixture",44100,error,sizeof(error));assert(s&&error[0]==0&&active==9);printf("PASS success opens9 allocations: stream=%zu window=%zu total_windows=%zu\n",sizeof(*s),(size_t)WINDOW*2*sizeof(float),(size_t)8*WINDOW*2*sizeof(float));xz_oc_stream_close(s);assert(active==0&&closed==1);
 for(int fail=2;fail<=13;fail++){reset();foreign_mode=1;fail_allocation=fail;assert(!xz_oc_stream_open("fixture",44100,error,sizeof(error)));assert(active==0&&closed==1);char target[48];snprintf(target,sizeof(target),"window=%u role=%u",(unsigned)(fail-2)/3,(unsigned)(fail-2)%3);assert(strstr(error,target)&&strstr(error,"errno=12"));}puts("PASS all12 four-stem window failures diagnostic and leak-free");
 reset();pin_supported=1;pin_fail=1;memset(error,0xa5,sizeof(error));s=xz_oc_stream_open("fixture",44100,error,sizeof(error));assert(s&&error[0]==0&&pin_calls==0&&active==9);xz_oc_stream_close(s);assert(active==0&&closed==1);puts("PASS open makes no RAM copy: a failing copy cannot fail the open, and the stream closes clean");
 puts("PASS all allocation/thread failures diagnostic and leak-free, success cleanup");return 0;
}

static struct xz_oc_open_diagnostics diagnostics;
static unsigned cancel_calls,cancel_at,generation,expected_generation;
static int test_cancel(void *context){(void)context;++cancel_calls;if(cancel_at&&cancel_calls==cancel_at)++generation;return generation!=expected_generation;}
static void retry_reset(void){reset();create_calls=always_allocate_fail=0;thread_fail_remaining=-1;cancel_calls=cancel_at=0;generation=expected_generation=17;}
int main(void){
 thread_fail_remaining=-1;old_diagnostics_main();char error[160];struct xz_oc_stream *s;
 for(int fail=1;fail<=9;fail++){
  retry_reset();fail_allocation=fail;s=xz_oc_stream_open_retry("fixture",44100,error,sizeof error,test_cancel,NULL,&diagnostics);assert(s&&error[0]==0&&diagnostics.failures==1&&strstr(diagnostics.failure[0],"errno=12"));xz_oc_stream_close(s);assert(!active);}
 puts("PASS every transient stream/window allocation retries successfully with full cleanup");
 for(int i=0;i<2;i++){retry_reset();thread_failure=i?ENOMEM:EAGAIN;thread_fail_remaining=1;s=xz_oc_stream_open_retry("fixture",44100,error,sizeof error,test_cancel,NULL,&diagnostics);assert(s&&error[0]==0&&create_calls==2&&closed==1&&diagnostics.failures==1&&strstr(diagnostics.failure[0],"WORKER CREATE FAILED"));xz_oc_stream_close(s);assert(!active&&closed==2);}
 puts("PASS transient EAGAIN/ENOMEM pthread retries successfully");
 retry_reset();thread_failure=EAGAIN;s=xz_oc_stream_open_retry("fixture",44100,error,sizeof error,test_cancel,NULL,&diagnostics);assert(!s&&diagnostics.failures==3&&create_calls==3&&closed==3&&!active&&strstr(error,"rc=11")&&strstr(error,"3 attempts"));
 retry_reset();always_allocate_fail=1;s=xz_oc_stream_open_retry("fixture",44100,error,sizeof error,test_cancel,NULL,&diagnostics);assert(!s&&diagnostics.failures==3&&allocation==3&&!active&&strstr(error,"ALLOCATION FAILED")&&strstr(error,"3 attempts"));
 puts("PASS permanent resource failures stop at3 attempts with exact visible diagnostic");
 retry_reset();thread_failure=EPERM;s=xz_oc_stream_open_retry("fixture",44100,error,sizeof error,test_cancel,NULL,&diagnostics);assert(!s&&create_calls==1&&!active&&!strstr(error,"attempts"));
 retry_reset();format_fail=1;s=xz_oc_stream_open_retry("fixture",44100,error,sizeof error,test_cancel,NULL,&diagnostics);assert(!s&&allocation==1&&!active&&!strcmp(error,"EXACT FORMAT FAILURE"));
 retry_reset();pin_supported=1;pin_fail=1;s=xz_oc_stream_open_retry("fixture",44100,error,sizeof error,test_cancel,NULL,&diagnostics);assert(s&&error[0]==0&&!pin_calls&&!diagnostics.failures&&create_calls==1);xz_oc_stream_close(s);assert(!active);
 retry_reset();s=xz_oc_stream_open_retry("fixture",123,error,sizeof error,test_cancel,NULL,&diagnostics);assert(!s&&!allocation&&!active&&!strcmp(error,"Unsupported XZ reader rate"));
 puts("PASS format/identity/corruption, rate and permission errors are never retried; a failing RAM copy is not an open error");
 retry_reset();cancel_at=1;s=xz_oc_stream_open_retry("fixture",44100,error,sizeof error,test_cancel,NULL,&diagnostics);assert(!s&&!allocation&&!active);
 retry_reset();cancel_at=2;s=xz_oc_stream_open_retry("fixture",44100,error,sizeof error,test_cancel,NULL,&diagnostics);assert(!s&&closed==1&&!active&&create_calls==1);
 retry_reset();thread_failure=EAGAIN;cancel_at=4;s=xz_oc_stream_open_retry("fixture",44100,error,sizeof error,test_cancel,NULL,&diagnostics);assert(!s&&closed==1&&!active&&create_calls==1);
 puts("PASS disable/track-generation replacement before open, after success and during backoff cancel without stale stream or retry");
 return 0;
}
