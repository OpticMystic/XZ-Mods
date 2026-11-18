/* SPDX-License-Identifier: MIT */
/* Shared-memory contract between xz-link-peer (the Ableton Link helper) and the XZ mod library.
 *
 * One 256-byte file, normally /dev/shm/xz-link/state. The player creates it, writes the layout and sets
 * `magic` last; the helper only maps a file whose magic, version and size match, and never creates one.
 *
 * Writers:
 *   header.helper_pid, header.heartbeat_us, state   the helper
 *   cmd                                             the player
 * `state` and `cmd` each use a sequence lock: an odd `seq` means a write is in progress. A reader copies
 * and retries a bounded number of times; nobody waits on the other side.
 *
 * Times are CLOCK_MONOTONIC microseconds. The helper runs Link on that clock, so Link times and the
 * player's monotonic times are the same numbers.
 *
 * This file is the whole interface between the GPL helper and the MIT player: a small numeric record any
 * program can implement. */
#ifndef XZ_LINK_SHM_H
#define XZ_LINK_SHM_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define XZ_LINK_SHM_MAGIC 0x4B4E4C58u /* "XLNK" */
#define XZ_LINK_SHM_VERSION 2u
#define XZ_LINK_QUANTUM 4.0

enum {
    XZ_LINK_STATE_ENABLED = 1u << 0,
    XZ_LINK_STATE_PLAYING = 1u << 1,         /* Link transport; the XZ never sets it */
    XZ_LINK_STATE_START_STOP_SYNC = 1u << 2
};

enum {
    XZ_LINK_OP_SET_TEMPO = 1u << 0,
    XZ_LINK_OP_REQUEST_BEAT = 1u << 1,
    XZ_LINK_OP_FORCE_BEAT = 1u << 2,
    XZ_LINK_OP_SET_PLAYING = 1u << 3,
    XZ_LINK_OP_SET_ENABLED = 1u << 4
};

struct xz_link_state {
    uint32_t seq;
    uint32_t peers;
    uint32_t flags;
    uint32_t applied_serial; /* the last cmd.serial the helper applied; this record already reflects it */
    int64_t link_host_us;    /* Link clock (CLOCK_MONOTONIC) at which beat and phase hold */
    int64_t monotonic_us;    /* clock_gettime(CLOCK_MONOTONIC) at the same instant */
    double tempo_bpm;
    double beat;             /* beatAtTime(link_host_us, quantum) */
    double phase;            /* phaseAtTime(link_host_us, quantum) */
    double quantum;
    uint64_t updates;
};

struct xz_link_cmd {
    uint32_t seq;
    uint32_t serial;         /* a new value submits; the helper echoes it in state.applied_serial */
    uint32_t ops;            /* XZ_LINK_OP_*; SET_TEMPO is applied before a beat op at the same time */
    uint32_t playing;
    uint32_t enabled;
    uint32_t reserved;
    double tempo_bpm;
    double beat;
    double quantum;          /* 0 means XZ_LINK_QUANTUM */
    int64_t at_monotonic_us; /* 0 means the helper's now */
};

struct xz_link_shm {
    uint32_t magic;          /* written last by the player */
    uint32_t version;
    uint32_t size;
    int32_t helper_pid;      /* written by the helper once it has accepted the layout */
    uint64_t heartbeat_us;   /* helper's CLOCK_MONOTONIC at its latest publish; 0 before the first */
    uint64_t reserved;
    struct xz_link_state state __attribute__((aligned(64)));
    struct xz_link_cmd cmd __attribute__((aligned(64)));
};

#ifdef __cplusplus
#define XZ_LINK_STATIC_ASSERT(c, m) static_assert(c, m)
#else
#define XZ_LINK_STATIC_ASSERT(c, m) _Static_assert(c, m)
#endif
XZ_LINK_STATIC_ASSERT(sizeof(struct xz_link_state) == 72, "state layout");
XZ_LINK_STATIC_ASSERT(sizeof(struct xz_link_cmd) == 56, "cmd layout");
XZ_LINK_STATIC_ASSERT(offsetof(struct xz_link_shm, heartbeat_us) == 16, "heartbeat offset");
XZ_LINK_STATIC_ASSERT(offsetof(struct xz_link_shm, state) == 64, "state offset");
XZ_LINK_STATIC_ASSERT(offsetof(struct xz_link_shm, cmd) == 192, "cmd offset");
XZ_LINK_STATIC_ASSERT(sizeof(struct xz_link_shm) == 256, "shm size");

static inline void xz_link_seq_write_begin(uint32_t *seq)
{
    uint32_t s = __atomic_load_n(seq, __ATOMIC_RELAXED);
    __atomic_store_n(seq, s + 1u, __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_RELEASE);
}

static inline void xz_link_seq_write_end(uint32_t *seq)
{
    uint32_t s = __atomic_load_n(seq, __ATOMIC_RELAXED);
    __atomic_store_n(seq, s + 1u, __ATOMIC_RELEASE);
}

/* Copies a seqlocked record of `size` bytes whose first field is the uint32_t seq.
 * Returns 1 on a consistent copy, 0 if the writer kept it busy for `tries` attempts. */
static inline int xz_link_seq_read(const volatile void *src, void *dst, size_t size, int tries)
{
    const uint32_t *seq = (const uint32_t *)src;
    while (tries-- > 0) {
        uint32_t a = __atomic_load_n(seq, __ATOMIC_ACQUIRE);
        if (a & 1u)
            continue;
        memcpy(dst, (const void *)src, size);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        uint32_t b = __atomic_load_n(seq, __ATOMIC_RELAXED);
        if (a == b)
            return 1;
    }
    return 0;
}

/* Writes a whole record of `size` bytes under its sequence lock; the record's own seq is kept. */
static inline void xz_link_seq_write(volatile void *dst, const void *src, size_t size)
{
    uint32_t *seq = (uint32_t *)dst;
    xz_link_seq_write_begin(seq);
    memcpy((char *)dst + sizeof(uint32_t), (const char *)src + sizeof(uint32_t), size - sizeof(uint32_t));
    xz_link_seq_write_end(seq);
}

/* Beat at an arbitrary CLOCK_MONOTONIC time, extrapolated from a published state. */
static inline double xz_link_beat_at(const struct xz_link_state *s, int64_t monotonic_us)
{
    return s->beat + (double)(monotonic_us - s->link_host_us) * s->tempo_bpm / 60e6;
}

#endif
