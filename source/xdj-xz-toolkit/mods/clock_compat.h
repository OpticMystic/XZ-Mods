#ifndef XZ_CLOCK_COMPAT_H
#define XZ_CLOCK_COMPAT_H
#include <errno.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

/* SYS_clock_gettime writes kernel long fields. A 32-bit libc may instead use
 * 64-bit time_t/timespec fields. Convert explicitly without a clock_gettime
 * libc symbol, preserving the older ARM GLIBC dependency boundary. */
static inline int xz_clock_gettime_compat(clockid_t clock,struct timespec *out) {
    struct { long seconds,nanoseconds; } kernel;
    if(!out){errno=EFAULT;return -1;}
    if(syscall(SYS_clock_gettime,clock,&kernel))return -1;
    if(kernel.nanoseconds<0||kernel.nanoseconds>=1000000000L){errno=EIO;return -1;}
    out->tv_sec=(time_t)kernel.seconds;
    out->tv_nsec=kernel.nanoseconds;
    return 0;
}
#endif
