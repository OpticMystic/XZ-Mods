/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "core_use.h"
#include <dirent.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define SLOTS 96u
#define SAMPLES 512u
#define PERIOD_US 20000u
#define REFRESH_US 1000000u
#define REUSE_US 30000000u /* a slot is given to another thread only after the trail has forgotten the last one */
#define TICKS 6u           /* user (with nice), system, interrupt, soft interrupt, idle, waiting for a disk */

struct slot { int tid, last_tid, policy, priority, roams; uint64_t gone_us; char name[17]; };
struct sample { uint64_t at_us; uint32_t cpu_us[SLOTS], ticks[TICKS]; };
static struct slot slots[SLOTS];           /* writer: the sampling thread; tid 0 is a free slot */
static struct sample ring[SAMPLES];
static uint32_t written;                   /* samples kept; stored last */
static uint64_t sampled_us, refreshed_us;
static uint32_t sampling;                 /* one canary at a time */
static int stat_fd = -1;

/* Processor time a thread of this process has used (its scheduler clock, as kernel_guard.c reads it); 0 when
   the thread no longer exists. */
static uint64_t thread_cpu_us(int tid)
{
    struct timespec t;
    clockid_t clock = (clockid_t)((~(unsigned)tid << 3) | 6u);
    if (syscall(SYS_clock_gettime, clock, &t)) return 0;
    return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
}
static void refresh(uint64_t now)
{
    unsigned char seen[SLOTS] = {0};
    DIR *tasks = opendir("/proc/self/task");
    if (!tasks) return;
    struct dirent *entry;
    while ((entry = readdir(tasks))) {
        int tid = atoi(entry->d_name);
        cpu_set_t set;
        CPU_ZERO(&set);
        if (tid <= 0 || sched_getaffinity(tid, sizeof(set), &set) || !CPU_ISSET(XZ_CORE_USE_CPU, &set)) continue;
        unsigned at = SLOTS, spare = SLOTS;
        for (unsigned i = 0; i < SLOTS; i++) {
            if (slots[i].tid == tid) { at = i; break; }
            if (spare == SLOTS && !slots[i].tid && (!slots[i].gone_us || now - slots[i].gone_us >= REUSE_US)) spare = i;
        }
        if (at == SLOTS) {
            if (spare == SLOTS) continue;
            at = spare;
            struct slot *slot = &slots[at];
            char path[64];
            snprintf(path, sizeof(path), "/proc/self/task/%d/comm", tid);
            int fd = open(path, O_RDONLY | O_CLOEXEC);
            ssize_t length = fd >= 0 ? read(fd, slot->name, sizeof(slot->name) - 1) : 0;
            if (fd >= 0) close(fd);
            if (length < 0) length = 0;
            slot->name[length] = 0;
            char *end = strchr(slot->name, '\n');
            if (end) *end = 0;
            if (!slot->name[0]) snprintf(slot->name, sizeof(slot->name), "tid%d", tid);
            slot->gone_us = 0; slot->last_tid = tid;
            __atomic_store_n(&slot->tid, tid, __ATOMIC_RELEASE);
        }
        struct sched_param parameter = {0};
        int policy = sched_getscheduler(tid);
        slots[at].policy = policy < 0 ? SCHED_OTHER : policy;
        slots[at].priority = sched_getparam(tid, &parameter) ? 0 : parameter.sched_priority;
        /* Counted by hand: CPU_COUNT needs a C library symbol newer than the unit's verified set. */
        unsigned allowed = 0;
        for (unsigned cpu = 0; cpu < 8; cpu++) allowed += CPU_ISSET(cpu, &set) ? 1u : 0u;
        slots[at].roams = allowed > 1;
        seen[at] = 1;
    }
    closedir(tasks);
    for (unsigned i = 0; i < SLOTS; i++)
        if (slots[i].tid && !seen[i]) { slots[i].gone_us = now; __atomic_store_n(&slots[i].tid, 0, __ATOMIC_RELEASE); }
}
/* The core's line of /proc/stat. The kernel counts in hundredths of a second. */
static void core_ticks(uint32_t ticks[TICKS])
{
    char text[1024], label[8];
    memset(ticks, 0, TICKS * sizeof(*ticks));
    if (stat_fd < 0) stat_fd = open("/proc/stat", O_RDONLY | O_CLOEXEC);
    if (stat_fd < 0) return;
    ssize_t length = pread(stat_fd, text, sizeof(text) - 1, 0);
    if (length <= 0) return;
    text[length] = 0;
    snprintf(label, sizeof(label), "cpu%u ", XZ_CORE_USE_CPU);
    char *row = strstr(text, label);
    if (!row) return;
    row += strlen(label);
    unsigned long value[7] = {0};
    for (unsigned i = 0; i < 7; i++) value[i] = strtoul(row, &row, 10);
    ticks[0] = (uint32_t)(value[0] + value[1]); ticks[1] = (uint32_t)value[2]; ticks[2] = (uint32_t)value[5];
    ticks[3] = (uint32_t)value[6]; ticks[4] = (uint32_t)value[3]; ticks[5] = (uint32_t)value[4];
}
void xz_core_use_sample(uint64_t now)
{
    if (__atomic_exchange_n(&sampling, 1u, __ATOMIC_ACQUIRE)) return;
    if (sampled_us && now - sampled_us < PERIOD_US) { __atomic_store_n(&sampling, 0u, __ATOMIC_RELEASE); return; }
    sampled_us = now;
    if (!refreshed_us || now - refreshed_us >= REFRESH_US) { refreshed_us = now; refresh(now); }
    struct sample *sample = &ring[written % SAMPLES];
    sample->at_us = now;
    for (unsigned i = 0; i < SLOTS; i++) sample->cpu_us[i] = slots[i].tid ? (uint32_t)thread_cpu_us(slots[i].tid) : 0;
    core_ticks(sample->ticks);
    __atomic_store_n(&written, written + 1u, __ATOMIC_RELEASE);
    __atomic_store_n(&sampling, 0u, __ATOMIC_RELEASE);
}
int xz_core_use_report(uint64_t from_us, uint64_t to_us, char *text, size_t size)
{
    uint32_t count = __atomic_load_n(&written, __ATOMIC_ACQUIRE);
    if (!count || !text || size < 64 || to_us < from_us) return count ? -1 : 0;
    uint32_t held = count < SAMPLES ? count : SAMPLES - 1u; /* the slot being written next is never read */
    if (ring[(count - 1u) % SAMPLES].at_us < to_us) return 0;
    uint32_t after = count - 1u, before = count;
    for (uint32_t back = 1; back <= held; back++) {
        uint32_t index = count - back;
        uint64_t at = ring[index % SAMPLES].at_us;
        if (at >= to_us) after = index;
        if (at <= from_us) { before = index; break; }
    }
    if (before == count) return -1;
    static struct sample first, last; /* one reader: the thread that logs stalls */
    first = ring[before % SAMPLES]; last = ring[after % SAMPLES];
    if (__atomic_load_n(&written, __ATOMIC_ACQUIRE) - before >= SAMPLES - 1u) return -1;
    uint32_t used[SLOTS], total = 0;
    for (unsigned i = 0; i < SLOTS; i++) {
        /* A thread first seen inside the span has no starting value: its whole life is not this span's use. */
        used[i] = last.cpu_us[i] && first.cpu_us[i] ? last.cpu_us[i] - first.cpu_us[i] : 0;
        total += used[i];
    }
    uint32_t span = (uint32_t)(last.at_us - first.at_us);
    int length = snprintf(text, size, "core %u over %u.%u ms:", XZ_CORE_USE_CPU, (unsigned)(span / 1000u), (unsigned)(span % 1000u / 100u));
    for (unsigned listed = 0; listed < 8 && length > 0 && (size_t)length < size - 56; listed++) {
        unsigned top = SLOTS;
        for (unsigned i = 0; i < SLOTS; i++) if (used[i] >= 1000u && (top == SLOTS || used[i] > used[top])) top = i;
        if (top == SLOTS) break;
        const struct slot *slot = &slots[top];
        length += snprintf(text + length, size - (size_t)length, " %s%s/%d %c%d %u.%u ms,", slot->name, slot->roams ? "*" : "",
                           slot->tid ? slot->tid : slot->last_tid,
                           slot->policy == SCHED_FIFO ? 'F' : slot->policy == SCHED_RR ? 'R' : 'n', slot->priority,
                           (unsigned)(used[top] / 1000u), (unsigned)(used[top] % 1000u / 100u));
        used[top] = 0;
    }
    if (length > 0 && (size_t)length < size - 110)
        length += snprintf(text + length, size - (size_t)length,
                           " all threads %u.%u ms; kernel ticks of 10 ms: user %u system %u interrupt %u soft %u idle %u disk wait %u",
                           (unsigned)(total / 1000u), (unsigned)(total % 1000u / 100u),
                           (unsigned)(last.ticks[0] - first.ticks[0]), (unsigned)(last.ticks[1] - first.ticks[1]),
                           (unsigned)(last.ticks[2] - first.ticks[2]), (unsigned)(last.ticks[3] - first.ticks[3]),
                           (unsigned)(last.ticks[4] - first.ticks[4]), (unsigned)(last.ticks[5] - first.ticks[5]));
    return length;
}
