/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error The core use test requires active assertions
#endif
#define _GNU_SOURCE
#include "../core_use.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <time.h>
#include <unistd.h>

static volatile int go;
static uint64_t now_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
}
static void pin(unsigned cpu)
{
    cpu_set_t set;
    CPU_ZERO(&set); CPU_SET(cpu, &set);
    assert(!sched_setaffinity(0, sizeof(set), &set));
}
static void nap(unsigned us) { struct timespec t = {0, (long)us * 1000L}; nanosleep(&t, NULL); }
static void *busy(void *unused)
{
    (void)unused;
    prctl(PR_SET_NAME, "busy-core", 0, 0, 0);
    pin(XZ_CORE_USE_CPU);
    while (!go) nap(1000);
    uint64_t end = now_us() + 100000u;
    volatile unsigned spin = 0;
    while (now_us() < end) spin++;
    while (go) nap(50000); /* long naps: the quiet span must not see this thread use a millisecond */
    return NULL;
}
static void sample_for(unsigned us)
{
    for (uint64_t end = now_us() + us; now_us() < end; nap(5000)) xz_core_use_sample(now_us());
}
int main(void)
{
    if (sysconf(_SC_NPROCESSORS_ONLN) <= (long)XZ_CORE_USE_CPU) { puts("SKIP core use: this machine has no core 2"); return 0; }
    char text[520];
    assert(xz_core_use_report(1, 2, text, sizeof(text)) == 0);
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, busy, NULL));
    pin(0);
    nap(30000);
    sample_for(100000);
    uint64_t from = now_us();
    go = 1;
    sample_for(60000);
    /* The span is not over: the newest sample is older than its end. */
    assert(xz_core_use_report(from, from + 100000u, text, sizeof(text)) == 0);
    sample_for(120000);
    int length = xz_core_use_report(from, from + 100000u, text, sizeof(text));
    assert(length > 0 && (size_t)length == strlen(text));
    puts(text);
    assert(!strncmp(text, "core 2 over ", 12));
    const char *row = strstr(text, " busy-core/");
    assert(row);
    row = strstr(row, " n0 ");
    assert(row);
    double used = strtod(row + 4, NULL);
    assert(used >= 70.0 && used <= 135.0);
    assert(strstr(text, " all threads ") && strstr(text, "kernel ticks of 10 ms: user "));
    /* A quiet span names no thread at a millisecond or more. */
    sample_for(150000);
    uint64_t quiet = now_us() - 100000u;
    assert(xz_core_use_report(quiet, quiet + 40000u, text, sizeof(text)) > 0 && !strstr(text, "busy-core"));
    /* Older than the trail holds. */
    assert(xz_core_use_report(1, 2, text, sizeof(text)) == -1);
    go = 0;
    assert(!pthread_join(thread, NULL));
    puts("PASS core use trail: names the thread that held the core, waits for a later sample, refuses a forgotten span");
    return 0;
}
