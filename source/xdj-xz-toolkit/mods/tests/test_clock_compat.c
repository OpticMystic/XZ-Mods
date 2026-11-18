#define _GNU_SOURCE
#ifdef NDEBUG
#error Clock ABI acceptance requires active assertions
#endif
#include "../clock_compat.h"
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

static int64_t nanos(const struct timespec *t){return(int64_t)t->tv_sec*1000000000ll+t->tv_nsec;}
static void compare(clockid_t clock){
    struct timespec before={0},actual={0},after={0};
    assert(!xz_clock_gettime_compat(clock,&before));
    assert(!clock_gettime(clock,&actual));
    assert(!xz_clock_gettime_compat(clock,&after));
    assert(before.tv_nsec>=0&&before.tv_nsec<1000000000L);
    assert(after.tv_nsec>=0&&after.tv_nsec<1000000000L);
    assert(before.tv_sec>=0&&(uint64_t)before.tv_sec<UINT32_MAX);
    assert(nanos(&before)<=nanos(&actual)&&nanos(&actual)<=nanos(&after));
    assert(nanos(&after)-nanos(&before)<5000000000ll);
    printf("clock=%d sizeof_timespec=%zu sizeof_time_t=%zu sizeof_long=%zu seconds=%lld nanoseconds=%lld delta_ns=%lld\n",clock,sizeof(struct timespec),sizeof(time_t),sizeof(long),(long long)actual.tv_sec,(long long)actual.tv_nsec,(long long)(nanos(&after)-nanos(&before)));
}
int main(void){
    compare(CLOCK_MONOTONIC);compare(CLOCK_REALTIME);compare(CLOCK_PROCESS_CPUTIME_ID);
    struct timespec preserved={123,456};errno=0;
    assert(xz_clock_gettime_compat(INT_MAX,&preserved)==-1&&errno==EINVAL);
    assert(preserved.tv_sec==123&&preserved.tv_nsec==456);
    errno=0;assert(xz_clock_gettime_compat(CLOCK_MONOTONIC,NULL)==-1&&errno==EFAULT);
    puts("PASS syscall kernel-long to libc-timespec conversion, real clock agreement, bounded values and failure preservation");return 0;
}
