/* SPDX-License-Identifier: MIT */
#ifndef XZ_LATE_TRAIL_H
#define XZ_LATE_TRAIL_H
/* Late trail (private diagnostic).
 *
 * A DECK 3/4 dropout line says that a deck did not fill its USB packets, not why. Two periodic real-time loops
 * can cause it: the deck's own worker (it tops its queue up every 2 ms) and the USB transport (it refills a
 * packet group whenever one completes). Each keeps a trail of its own late turns: how long it waited against
 * what it asked for, how long its work took, and what the kernel charged the thread meanwhile (page faults,
 * switches, system time). The dropout line then quotes the turns of the last moments.
 *
 * One writer per trail, any reader. No lock, no allocation; three system calls a turn. */
#include <stdint.h>
#define XZ_LATE_SLOTS 8u
struct xz_late_turn {
    uint64_t at_us;
    uint32_t asked_us, waited_us, worked_us, system_us;
    uint32_t minor_faults, major_faults, yielded, preempted;
    uint32_t part_us[5];                   /* how the loop's work divided, when the loop times its parts */
    uint32_t user_us;                      /* processor time the thread spent in its own code */
};
struct xz_late_trail {
    uint32_t count;                        /* turns written; published last */
    struct xz_late_turn turn[XZ_LATE_SLOTS];
    /* the writer's own state */
    uint64_t mark_us, woke_us, system_us;
    long minor_faults, major_faults, yielded, preempted;
    uint32_t asked_us;
    int primed;
    uint32_t part_us[5];                   /* this turn's parts, set by the loop before xz_late_worked */
    uint64_t user_us;
};
/* Turn trail: what a deck worker did in each of its last turns. One writer, any reader. */
#define XZ_TURN_SLOTS 64u
struct xz_turn { uint16_t frames, lead; int16_t rate_x100; uint8_t flags, state, render, bits; int8_t submit; };
struct xz_turn_trail { uint32_t count; struct xz_turn turn[XZ_TURN_SLOTS]; };
static inline void xz_turn_note(struct xz_turn_trail *t, uint32_t frames, uint32_t lead, unsigned flags,
                                unsigned state, unsigned render, int submit, double rate, unsigned bits) {
    struct xz_turn *slot = &t->turn[t->count % XZ_TURN_SLOTS];
    slot->state = (uint8_t)state; slot->render = (uint8_t)render; slot->bits = (uint8_t)bits;
    slot->submit = submit < -127 ? -127 : submit > 127 ? 127 : (int8_t)submit;
    slot->rate_x100 = rate < -300. ? -30000 : rate > 300. ? 30000 : (int16_t)(rate * 100.);
    slot->frames = frames > 0xffffu ? 0xffffu : (uint16_t)frames;
    slot->lead = lead > 0xffffu ? 0xffffu : (uint16_t)lead;
    slot->flags = (uint8_t)flags;
    __atomic_store_n(&t->count, t->count + 1, __ATOMIC_RELEASE);
}
/* Underrun note: the transport's view when it staged a packet group a deck had not filled. Written by the
   transport thread only; the count is stored last. */
struct xz_underrun_note { uint32_t count, missing_frames, queued_blocks, since_completion_us, pause_us; };
#if defined(__linux__)
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
/* The kernel's own layout of getrusage's result: longs throughout, whatever the C library's time type is. */
struct xz_late_usage { long user_s, user_us, system_s, system_us, maxrss, ixrss, idrss, isrss, minflt, majflt,
    nswap, inblock, oublock, msgsnd, msgrcv, nsignals, nvcsw, nivcsw; };
#define XZ_LATE_RUSAGE_THREAD 1
static inline uint64_t xz_late_us(void) {
    struct { long seconds, nanoseconds; } kernel;
    if (syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &kernel)) return 0;
    return (uint64_t)kernel.seconds * 1000000u + (uint64_t)(kernel.nanoseconds / 1000);
}
/* The wait is over (asked_us is what was asked for; 0 when the loop has no fixed wait). */
static inline void xz_late_woke(struct xz_late_trail *t, uint32_t asked_us) {
    t->woke_us = xz_late_us();
    t->asked_us = asked_us;
}
/* The turn's work is done and the next wait begins. A turn whose wait overran by wait_slack_us, or whose work
   took work_limit_us, is kept. */
static inline void xz_late_worked(struct xz_late_trail *t, uint32_t wait_slack_us, uint32_t work_limit_us) {
    uint64_t now = xz_late_us();
    struct xz_late_usage use;
    if (syscall(SYS_getrusage, XZ_LATE_RUSAGE_THREAD, &use)) return;
    uint64_t system_us = (uint64_t)use.system_s * 1000000u + (uint64_t)use.system_us;
    uint64_t user_us = (uint64_t)use.user_s * 1000000u + (uint64_t)use.user_us;
    if (t->primed && t->woke_us >= t->mark_us && now >= t->woke_us) {
        uint32_t waited = (uint32_t)(t->woke_us - t->mark_us), worked = (uint32_t)(now - t->woke_us);
        if (waited >= t->asked_us + wait_slack_us || worked >= work_limit_us) {
            struct xz_late_turn *slot = &t->turn[t->count % XZ_LATE_SLOTS];
            *slot = (struct xz_late_turn){t->woke_us, t->asked_us, waited, worked, (uint32_t)(system_us - t->system_us),
                (uint32_t)(use.minflt - t->minor_faults), (uint32_t)(use.majflt - t->major_faults),
                (uint32_t)(use.nvcsw - t->yielded), (uint32_t)(use.nivcsw - t->preempted), {0}, 0};
            for (unsigned n = 0; n < 5; n++) slot->part_us[n] = t->part_us[n];
            slot->user_us = (uint32_t)(user_us - t->user_us);
            __atomic_store_n(&t->count, t->count + 1, __ATOMIC_RELEASE);
        }
    }
    for (unsigned n = 0; n < 5; n++) t->part_us[n] = 0;
    t->mark_us = now; t->system_us = system_us; t->user_us = user_us; t->primed = 1;
    t->minor_faults = use.minflt; t->major_faults = use.majflt; t->yielded = use.nvcsw; t->preempted = use.nivcsw;
}
/* The kept turns that began within within_us of now, oldest first. Returns how many. */
static inline unsigned xz_late_recent(const struct xz_late_trail *t, uint32_t within_us, struct xz_late_turn out[XZ_LATE_SLOTS]) {
    for (int attempt = 0; attempt < 3; attempt++) {
        uint32_t before = __atomic_load_n(&t->count, __ATOMIC_ACQUIRE);
        struct xz_late_turn copy[XZ_LATE_SLOTS];
        for (unsigned i = 0; i < XZ_LATE_SLOTS; i++) copy[i] = t->turn[i];
        if (__atomic_load_n(&t->count, __ATOMIC_ACQUIRE) != before) continue;
        uint64_t now = xz_late_us();
        unsigned kept = 0, held = before < XZ_LATE_SLOTS ? before : XZ_LATE_SLOTS;
        for (unsigned i = 0; i < held; i++) {
            const struct xz_late_turn *turn = &copy[(before - held + i) % XZ_LATE_SLOTS];
            if (now >= turn->at_us && now - turn->at_us <= within_us) out[kept++] = *turn;
        }
        return kept;
    }
    return 0;
}
#else
static inline uint64_t xz_late_us(void) { return 0; }
static inline void xz_late_woke(struct xz_late_trail *t, uint32_t asked_us) { (void)t; (void)asked_us; }
static inline void xz_late_worked(struct xz_late_trail *t, uint32_t wait_slack_us, uint32_t work_limit_us) { (void)t; (void)wait_slack_us; (void)work_limit_us; }
static inline unsigned xz_late_recent(const struct xz_late_trail *t, uint32_t within_us, struct xz_late_turn out[XZ_LATE_SLOTS]) { (void)t; (void)within_us; (void)out; return 0; }
#endif
#endif
