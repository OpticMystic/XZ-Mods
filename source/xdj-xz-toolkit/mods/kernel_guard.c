/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#define _GNU_SOURCE
#include "kernel_guard.h"
#include "core_use.h"
#pragma weak xz_core_use_sample
#include "runtime.h"
#include "ram/lowzone.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

/* Kernel-work guard.
 *
 * The XZ runs with real-time throttling switched off, so a real-time thread that never sleeps owns its core
 * completely. The kernel's own per-core workers run at normal priority. Several kernel jobs wait for a worker
 * on every core: removing a USB disk and locking memory both do. One busy real-time thread on one core
 * therefore stops USB removal for the whole unit, and everything that waits on it, until power is cycled.
 *
 * Each guarded core gets a canary: a normal-priority thread that sleeps 10 ms and records when it ran. It gets
 * the processor exactly when a kernel worker on that core would. A small real-time thread watches the three
 * records. When a canary has not run for XZ_GUARD_STARVED_US, the guard finds the real-time threads of this
 * process that used that core during the episode, lowers them to normal priority for at most
 * XZ_GUARD_RELIEF_US so the canary and the kernel workers run, and restores each thread's policy and priority
 * exactly. It names the threads in the MODS log.
 *
 * The real-time path makes no allocation and reads no files: a normal-priority housekeeper keeps a table of
 * this process's real-time threads with their processor time, and the guard compares against it.
 *
 * Core 3 carries only the audio threads and is never watched or touched; threads at priority 90 and above
 * (control surface, network clock) are never lowered. A stock track load keeps normal-priority work off a core
 * for up to 320 ms, so the guard waits 600 ms: it acts only in a state that otherwise ends in a frozen unit. */
#define XZ_GUARD_CORES 3u
#define XZ_GUARD_CANARY_US 10000u
#define XZ_GUARD_PERIOD_US 25000u
#define XZ_GUARD_STARVED_US 600000u
#define XZ_GUARD_GRACE_US 60000000u /* the player's own start-up starves core 0 for over a second */
#define XZ_GUARD_RELIEF_US 4000u
#define XZ_GUARD_PRIORITY 40
#define XZ_GUARD_CEILING 90
#define XZ_GUARD_THREADS 192u
#define XZ_GUARD_HELD 96u
#define XZ_GUARD_STACK_BYTES (1280u * 1024u) /* above the runtime's static thread-local block */
#define XZ_GUARD_STATS "/dev/shm/xz-guard-stats"
#define XZ_GUARD_CONTROL "/dev/shm/xz-guard-control"

struct subject { int tid, policy, priority; unsigned cores; uint64_t cpu_us; char name[17]; };
struct roster { unsigned count; struct subject thread[XZ_GUARD_THREADS]; };
static struct roster rosters[2];
static volatile uint32_t roster_live;                      /* index of the published roster */
static volatile uint64_t beat_us[XZ_GUARD_CORES];          /* when each canary last ran */
static volatile uint32_t late_window_us[XZ_GUARD_CORES];   /* worst canary lateness since the last report */
static volatile uint32_t late_worst_us[XZ_GUARD_CORES];    /* worst since start */
static volatile uint32_t reliefs[XZ_GUARD_CORES];
static volatile uint32_t blocked[XZ_GUARD_CORES];          /* late canary that was waiting, not starved: no relief */
static volatile int canary_tid[XZ_GUARD_CORES];
static int canary_stat[XZ_GUARD_CORES] = {-1, -1, -1};     /* guard thread only */
static volatile uint32_t acting = 1, stopping, started;
static volatile int guard_tid;
static uint64_t guard_began_us;
static volatile uint64_t kept_us;      /* when housekeeping last ran */
static volatile uint32_t keeping;
static char last_relief[176] = "none";
static pthread_t guard_thread, canary_thread[XZ_GUARD_CORES];

static uint64_t now_us(void)
{
    struct timespec t;
    /* glibc 2.13 keeps clock_gettime in librt; the runtime links only libc. */
    syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
}
/* Processor time a thread of this process has used; 0 when it no longer exists. */
static uint64_t thread_cpu_us(int tid)
{
    struct timespec t;
    clockid_t clock = (clockid_t)((~(unsigned)tid << 3) | 6u); /* per-thread scheduler clock */
    if (syscall(SYS_clock_gettime, clock, &t)) return 0;
    return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
}
static void nap_us(uint32_t microseconds)
{
    struct timespec t = {microseconds / 1000000u, (long)(microseconds % 1000000u) * 1000L};
    while (nanosleep(&t, &t) && errno == EINTR) {}
}
static void pin(unsigned mask)
{
    cpu_set_t set;
    CPU_ZERO(&set);
    for (unsigned cpu = 0; cpu < XZ_GUARD_CORES; cpu++) if (mask & (1u << cpu)) CPU_SET(cpu, &set);
    sched_setaffinity(0, sizeof(set), &set);
}
static void raise_max(volatile uint32_t *slot, uint64_t value)
{
    uint32_t clipped = value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
    if (clipped > *slot) *slot = clipped;
}

/* Housekeeper: the real-time threads of this process that may run on a guarded core. */
static void roster_refresh(void)
{
    const struct roster *old = &rosters[__atomic_load_n(&roster_live, __ATOMIC_ACQUIRE) & 1u];
    struct roster *next = &rosters[(__atomic_load_n(&roster_live, __ATOMIC_ACQUIRE) + 1u) & 1u];
    DIR *tasks = opendir("/proc/self/task");
    if (!tasks) return;
    unsigned count = 0;
    struct dirent *entry;
    while (count < XZ_GUARD_THREADS && (entry = readdir(tasks))) {
        int tid = atoi(entry->d_name);
        if (tid <= 0 || tid == guard_tid) continue;
        int policy = sched_getscheduler(tid);
        struct sched_param parameter;
        if ((policy != SCHED_FIFO && policy != SCHED_RR) || sched_getparam(tid, &parameter) ||
            parameter.sched_priority < 1 || parameter.sched_priority >= XZ_GUARD_CEILING) continue;
        cpu_set_t set;
        CPU_ZERO(&set);
        if (sched_getaffinity(tid, sizeof(set), &set)) continue;
        unsigned cores = 0;
        for (unsigned cpu = 0; cpu < XZ_GUARD_CORES; cpu++) if (CPU_ISSET(cpu, &set)) cores |= 1u << cpu;
        if (!cores || CPU_ISSET(3, &set)) continue; /* never an audio-core thread */
        struct subject *subject = &next->thread[count];
        subject->tid = tid; subject->policy = policy; subject->priority = parameter.sched_priority; subject->cores = cores;
        subject->cpu_us = thread_cpu_us(tid);
        subject->name[0] = 0;
        for (unsigned i = 0; i < old->count; i++)
            if (old->thread[i].tid == tid) { memcpy(subject->name, old->thread[i].name, sizeof(subject->name)); break; }
        if (!subject->name[0]) {
            char path[64];
            snprintf(path, sizeof(path), "/proc/self/task/%d/comm", tid);
            int fd = open(path, O_RDONLY | O_CLOEXEC);
            ssize_t length = fd >= 0 ? read(fd, subject->name, sizeof(subject->name) - 1) : 0;
            if (fd >= 0) close(fd);
            if (length < 0) length = 0;
            subject->name[length] = 0;
            char *end = strchr(subject->name, '\n');
            if (end) *end = 0;
            if (!subject->name[0]) snprintf(subject->name, sizeof(subject->name), "tid%d", tid);
        }
        count++;
    }
    closedir(tasks);
    next->count = count;
    __atomic_fetch_add(&roster_live, 1u, __ATOMIC_RELEASE);
}

/* Self-test only: a real-time thread that never sleeps, as the stock interface thread did. */
struct hog { unsigned cpu, priority, milliseconds; };
static void *hog_main(void *argument)
{
    struct hog plan = *(struct hog *)argument;
    free(argument);
    prctl(PR_SET_NAME, "xz-guard-hog", 0, 0, 0);
    pin(1u << plan.cpu);
    struct sched_param parameter = {.sched_priority = (int)plan.priority};
    if (sched_setscheduler(0, SCHED_FIFO, &parameter)) return NULL;
    nap_us(2600000u); /* a real hog exists before it starts to spin; let the housekeeper list this one */
    uint64_t end = now_us() + (uint64_t)plan.milliseconds * 1000u;
    volatile uint32_t spin = 0;
    while (now_us() < end) spin++;
    return NULL;
}
static void control_tick(void)
{
    char text[64];
    int fd = open(XZ_GUARD_CONTROL, O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return;
    ssize_t length = read(fd, text, sizeof(text) - 1);
    close(fd);
    unlink(XZ_GUARD_CONTROL);
    if (length <= 0) return;
    text[length] = 0;
    if (!strncmp(text, "hog ", 4)) {
        /* strtoul, not sscanf: glibc redirects sscanf to a symbol newer than the unit's C library. */
        char *cursor = text + 4;
        unsigned cpu = (unsigned)strtoul(cursor, &cursor, 10);
        unsigned priority = (unsigned)strtoul(cursor, &cursor, 10);
        unsigned milliseconds = (unsigned)strtoul(cursor, &cursor, 10);
        /* Bounded so a mistyped test cannot hold a core: cores 0-2, below the control threads, 5 s at most. */
        if (cpu >= XZ_GUARD_CORES || priority < 1 || priority > 60 || milliseconds < 1 || milliseconds > 5000) return;
        struct hog *plan = malloc(sizeof(*plan));
        pthread_attr_t attributes;
        pthread_t thread;
        if (!plan || pthread_attr_init(&attributes)) { free(plan); return; }
        *plan = (struct hog){cpu, priority, milliseconds};
        pthread_attr_setstacksize(&attributes, XZ_GUARD_STACK_BYTES);
        pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&thread, &attributes, hog_main, plan)) free(plan);
        pthread_attr_destroy(&attributes);
    } else if (text[0] == '0' || text[0] == '1') {
        __atomic_store_n(&acting, text[0] == '1', __ATOMIC_RELEASE);
    }
}
static void report(void)
{
    char line[420];
    int length = snprintf(line, sizeof(line),
        "acting=%u late_ms=%.1f/%.1f/%.1f worst_ms=%.1f/%.1f/%.1f reliefs=%u/%u/%u blocked=%u/%u/%u watched=%u last=%s\n",
        (unsigned)acting, late_window_us[0] / 1000.0, late_window_us[1] / 1000.0, late_window_us[2] / 1000.0,
        late_worst_us[0] / 1000.0, late_worst_us[1] / 1000.0, late_worst_us[2] / 1000.0,
        (unsigned)reliefs[0], (unsigned)reliefs[1], (unsigned)reliefs[2],
        (unsigned)blocked[0], (unsigned)blocked[1], (unsigned)blocked[2],
        rosters[__atomic_load_n(&roster_live, __ATOMIC_ACQUIRE) & 1u].count, last_relief);
    for (unsigned cpu = 0; cpu < XZ_GUARD_CORES; cpu++) late_window_us[cpu] = 0;
    int fd = open(XZ_GUARD_STATS ".next", O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return;
    ssize_t written = write(fd, line, (size_t)length);
    close(fd);
    if (written == length) rename(XZ_GUARD_STATS ".next", XZ_GUARD_STATS);
}

static void *canary_main(void *argument)
{
    unsigned cpu = (unsigned)(uintptr_t)argument;
    char name[16];
    snprintf(name, sizeof(name), "xz-canary%u", cpu);
    prctl(PR_SET_NAME, name, 0, 0, 0);
    __atomic_store_n(&canary_tid[cpu], (int)syscall(SYS_gettid), __ATOMIC_RELEASE);
    pin(1u << cpu);
    /* The same scheduling class and nice value as a kernel worker. */
    sched_setscheduler(0, SCHED_OTHER, &(struct sched_param){0});
    setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 0);
    /* Housekeeping stays at normal priority, never on the real-time guard. Core 0's canary does it each
     * second; the others take over if core 0 is the starved one. */
    uint64_t due = cpu == 0 ? 1000000u : 2500000u;
    while (!__atomic_load_n(&stopping, __ATOMIC_ACQUIRE)) {
        uint64_t slept = now_us();
        nap_us(XZ_GUARD_CANARY_US);
        uint64_t now = now_us();
        uint64_t late = now - slept > XZ_GUARD_CANARY_US ? now - slept - XZ_GUARD_CANARY_US : 0;
        raise_max(&late_window_us[cpu], late);
        raise_max(&late_worst_us[cpu], late);
        __atomic_store_n(&beat_us[cpu], now, __ATOMIC_RELEASE);
        /* Every canary keeps the core use trail. Core 0's runs while the DECK 3/4 core is held, unless a
         * track load holds core 0 as well; core 2's runs just before and just after a hold of its own core. */
        if (xz_core_use_sample) xz_core_use_sample(now);
        if (now - kept_us >= due && !__atomic_exchange_n(&keeping, 1u, __ATOMIC_ACQUIRE)) {
            kept_us = now;
            control_tick(); roster_refresh(); report();
            __atomic_store_n(&keeping, 0u, __ATOMIC_RELEASE);
        }
    }
    return NULL;
}

static void relieve(unsigned cpu, uint64_t starved_us)
{
    struct subject held[XZ_GUARD_HELD];
    uint64_t used_us[XZ_GUARD_HELD];
    unsigned count = 0, candidates = 0;
    uint64_t began = now_us();
    /* The housekeeper rewrites the unpublished roster only. Copy what is needed and take the copy only if no
     * roster was published meanwhile, so a policy is never restored from a half-written entry. */
    for (unsigned attempt = 0; attempt < 3; attempt++) {
        uint32_t live = __atomic_load_n(&roster_live, __ATOMIC_ACQUIRE);
        const struct roster *roster = &rosters[live & 1u];
        count = candidates = 0;
        for (unsigned i = 0; i < roster->count && i < XZ_GUARD_THREADS && count < XZ_GUARD_HELD; i++) {
            struct subject subject = roster->thread[i];
            if (!(subject.cores & (1u << cpu))) continue;
            candidates++;
            uint64_t cpu_now = thread_cpu_us(subject.tid);
            uint64_t used = cpu_now > subject.cpu_us ? cpu_now - subject.cpu_us : 0;
            /* Every real-time thread of this core is lowered, not only the busy one: on the unit the display
             * thread spun, the stock interface thread below it was starved too, and when only the spinner was
             * lowered the starved thread took the core at real-time priority and the kernel workers still
             * did not run (1123 reliefs, no effect). The processor time is kept for the log. */
            held[count] = subject; used_us[count] = used; count++;
        }
        if (__atomic_load_n(&roster_live, __ATOMIC_ACQUIRE) == live) break;
        count = 0;
    }
    unsigned lowered = 0;
    for (unsigned i = 0; i < count; i++) {
        if (held[i].policy != SCHED_FIFO && held[i].policy != SCHED_RR) continue;
        if (held[i].priority < 1 || held[i].priority >= XZ_GUARD_CEILING) continue;
        if (sched_setscheduler(held[i].tid, SCHED_OTHER, &(struct sched_param){0})) continue;
        held[lowered] = held[i]; used_us[lowered] = used_us[i]; lowered++;
    }
    count = lowered;
    if (count) {
        /* Wait until the canary has run, then one more millisecond for the kernel workers queued beside it. */
        uint64_t limit = now_us() + XZ_GUARD_RELIEF_US;
        while (now_us() < limit && __atomic_load_n(&beat_us[cpu], __ATOMIC_ACQUIRE) < began) nap_us(250);
        nap_us(1000);
        for (unsigned i = 0; i < count; i++)
            sched_setscheduler(held[i].tid, held[i].policy, &(struct sched_param){.sched_priority = held[i].priority});
    }
    /* Whatever happened, do not measure the same episode again at once. */
    __atomic_store_n(&beat_us[cpu], now_us(), __ATOMIC_RELEASE);
    uint32_t number = ++reliefs[cpu];
    char line[sizeof(last_relief)];
    int used = snprintf(line, sizeof(line), "cpu%u starved %llu ms; %u of %u real-time threads lowered; busy:", cpu,
                        (unsigned long long)(starved_us / 1000u), count, candidates);
    for (unsigned i = 0; i < count && used > 0 && (size_t)used < sizeof(line) - 36; i++)
        if (used_us[i] * 4u >= starved_us) /* name the busy ones */
        used += snprintf(line + used, sizeof(line) - (size_t)used, " %s/%d(%llums)", held[i].name, held[i].priority,
                         (unsigned long long)(used_us[i] / 1000u));
    memcpy(last_relief, line, sizeof(last_relief));
    if (number <= 12u || number % 64u == 0) {
        char message[sizeof(line) + 40];
        snprintf(message, sizeof(message), "KERNEL_GUARD relief %u: %s", (unsigned)number, line);
        xz_log(message);
    }
}
/* A canary is late for two different reasons. Starved: it is runnable and real-time threads keep its core,
 * which is the state that freezes the unit. Waiting: it sleeps in the kernel behind something else, such as a
 * page fault while another thread holds the memory-map lock. Lowering real-time threads cures only the first;
 * in the second it disturbed 47 stock threads fifteen times in two minutes for nothing. The scheduler state in
 * the canary's stat file tells them apart: R is runnable. One small read from an already open file; unknown
 * counts as starved, so the guard never becomes weaker than it was. */
static int canary_starved(unsigned cpu)
{
    char text[320];
    if (canary_stat[cpu] < 0) {
        int tid = __atomic_load_n(&canary_tid[cpu], __ATOMIC_ACQUIRE);
        if (tid <= 0) return 1;
        char path[64];
        snprintf(path, sizeof(path), "/proc/self/task/%d/stat", tid);
        canary_stat[cpu] = open(path, O_RDONLY | O_CLOEXEC);
        if (canary_stat[cpu] < 0) return 1;
    }
    ssize_t length = pread(canary_stat[cpu], text, sizeof(text) - 1, 0);
    if (length <= 0) return 1;
    text[length] = 0;
    const char *end = strrchr(text, ')');
    if (!end || end[1] != ' ' || !end[2]) return 1;
    return end[2] == 'R';
}
static void *guard_main(void *unused)
{
    (void)unused;
    prctl(PR_SET_NAME, "xz-guard", 0, 0, 0);
    guard_tid = (int)syscall(SYS_gettid);
    /* Any of the three guarded cores: a hog on one of them cannot keep the guard itself from running. */
    pin((1u << XZ_GUARD_CORES) - 1u);
    struct sched_param parameter = {.sched_priority = XZ_GUARD_PRIORITY};
    if (sched_setscheduler(0, SCHED_FIFO, &parameter)) { xz_log("KERNEL_GUARD unavailable: real-time priority refused"); return NULL; }
    while (!__atomic_load_n(&stopping, __ATOMIC_ACQUIRE)) {
        nap_us(XZ_GUARD_PERIOD_US);
        uint64_t now = now_us();
        for (unsigned cpu = 0; cpu < XZ_GUARD_CORES; cpu++) {
            uint64_t beat = __atomic_load_n(&beat_us[cpu], __ATOMIC_ACQUIRE);
            if (!beat || now <= beat + XZ_GUARD_CANARY_US) continue;
            uint64_t age = now - beat;
            raise_max(&late_window_us[cpu], age - XZ_GUARD_CANARY_US);
            raise_max(&late_worst_us[cpu], age - XZ_GUARD_CANARY_US);
            if (age >= XZ_GUARD_STARVED_US && now - guard_began_us >= XZ_GUARD_GRACE_US &&
                __atomic_load_n(&acting, __ATOMIC_ACQUIRE)) {
                if (canary_starved(cpu)) relieve(cpu, age);
                else {
                    /* Count the episode once, then measure from now. */
                    blocked[cpu]++;
                    __atomic_store_n(&beat_us[cpu], now, __ATOMIC_RELEASE);
                }
            }
        }
    }
    return NULL;
}

static int spawn(pthread_t *thread, void *(*entry)(void *), void *argument)
{
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes)) return -1;
    /* A private low-zone stack: the kernel's own mapping range is nearly full of stock thread stacks. */
    void *stack = xz_lowzone_alloc(XZ_GUARD_STACK_BYTES);
    int error = !stack || pthread_attr_setstack(&attributes, stack, XZ_GUARD_STACK_BYTES) ||
        pthread_create(thread, &attributes, entry, argument);
    pthread_attr_destroy(&attributes);
    if (error) xz_lowzone_free(stack, XZ_GUARD_STACK_BYTES);
    return error ? -1 : 0;
}
int xz_kernel_guard_start(void)
{
    if (started) return 0;
    const char *choice = getenv("XZ_KERNEL_GUARD");
    if (choice && !strcmp(choice, "off")) return 0;
    if (choice && !strcmp(choice, "watch")) acting = 0;
    uint64_t now = now_us();
    guard_began_us = now;
    for (unsigned cpu = 0; cpu < XZ_GUARD_CORES; cpu++) beat_us[cpu] = now;
    for (unsigned cpu = 0; cpu < XZ_GUARD_CORES; cpu++)
        if (spawn(&canary_thread[cpu], canary_main, (void *)(uintptr_t)cpu)) return -1;
    if (spawn(&guard_thread, guard_main, NULL)) return -1;
    started = 1;
    xz_log(acting ? "KERNEL_GUARD watching cores 0-2; relief after 600 ms without normal-priority time"
                  : "KERNEL_GUARD watching cores 0-2; reporting only");
    return 0;
}
void xz_kernel_guard_stop(void)
{
    /* The threads own no state outside this file; they end at their next wake-up. */
    __atomic_store_n(&stopping, 1u, __ATOMIC_RELEASE);
}
