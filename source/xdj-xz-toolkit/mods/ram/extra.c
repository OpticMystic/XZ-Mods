/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include "extra.h"
#include "file.h"
#include "budget.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include "../xz_thread.h"
#include "../xz_mutex.h"
#include "../xz_wake.h"
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

struct session {
    struct session *next;
    struct xz_ram_extra *owner;
    struct xz_stem_media *media; /* Background access only after retire transfers ownership. */
    struct xz_ram_status status;
    struct xz_media_identity original;
    struct xz_ram_file file;
    char path[1024];
    int source_fd, old_fd, restore_fd;
    unsigned route, request, active_request, assets;
    int queued, manual, busy, prepared, attached, cleanup, releasing, retired, blocked, cancelled, failed;
    int gone; /* the stick is stopped or pulled: the copy is all this session will ever play from */
    unsigned index;            /* which of the two decks */
    int playing;               /* atomic: the deck is sounding (written by step, read by both workers) */
    uint64_t lead_from, lead_to; int lead_known; /* the stretch of the file the leading thread has read */
    unsigned lead_idle;        /* leading thread: passes since a cued deck's stretch was last read */
};
struct xz_ram_extra {
    pthread_mutex_t lock;
    struct xz_wake wake; /* wakes the worker; never a condition variable, see xz_wake.h */
    pthread_t thread;
    struct session *deck[2], *retired;
    unsigned retired_count;
    uint64_t serial;
    int stop;
    void *context;
    uint32_t (*route)(void *);
    uint64_t (*generation)(void *, unsigned);
    uint32_t reading[2]; /* atomics, written by the deck workers: the reader's file offset in 4 KiB, plus one */
    struct xz_wake lead_wake; /* wakes the leading thread when a deck starts or stops sounding */
    pthread_t lead_thread;
};
/* Leading.
 *
 * A DECK 3/4 track plays from the stick until its RAM copy is attached, and for good when no copy is made (too
 * large, RAM full). The deck's worker then reads the file itself, on a real-time thread. The stick answers one
 * request at a time, in order, at about 2.7 MB/s through its FUSE daemon, so while other decks load, a read of
 * the worker's waited behind theirs: 17 to 137 ms measured on r63 with all four decks loading within ten seconds,
 * and DECK 3/4 dropped out for 4 to 43 ms at a time.
 *
 * r65 asked the kernel to read 4 MiB ahead of the playhead in one go. That made it worse: 4 MiB is a second and a
 * half of the stick's whole capacity, queued in front of every other reader, the stock decks' included.
 *
 * What stands now: the reader's next megabyte is fetched at normal priority, in small synchronous pieces, so the
 * deck's own reads find it in the file cache.
 *  - A thread of its own (lead_work) does that for every deck whose track is not in RAM yet, from the moment the
 *    track is loaded and whatever its copy is doing: a copy waits for its turn behind other copies and stands
 *    back for loads, and those are the moments the reader needs it most. The load reads the first buffer at the
 *    cue point, where the deck will start (extra_player.c), and bind() takes the reader's place from it, so the
 *    megabyte after the cue, not the head of the file, is being read when PLAY comes right after LOAD. A deck
 *    that waits cued has it read again every few seconds, in case the kernel dropped it meanwhile. It reads the stretch and throws it away. It opens the file for that moment only:
 *    no descriptor stays on the stick, and nothing is read once the stick is going (gone) or the copy is
 *    attached.
 *  - The copy takes the pieces at the reader's place first (ram/cache.h, wanted and lead) and keeps those coming
 *    even while another deck loads, so what was led is in RAM soonest and is read from the stick once.
 * The copy is asked for as soon as a track is loaded, not at the first PLAY, so a track that was cued for a few
 * seconds plays from RAM from its first sample. The reader's place is the decoder's real file offset
 * (xz_stem_media_source_offset), exact for every format. */
#define LEAD_BYTES (1u << 20)
#define LEAD_PASS (256u << 10)
#define LEAD_EVERY_MS 100u
#define LEAD_AGAIN_PASSES 32u /* a cued deck: about every eight seconds at the idle pace of 250 ms */
void (*xz_ram_extra_lead_probe)(unsigned deck, uint64_t from, uint64_t bytes);
static int current(struct session *s) {
    return s->status.key.media_generation && s->owner->generation(s->owner->context, s->status.key.source) == s->status.key.media_generation;
}
static void state(struct session *s, enum xz_ram_state value, enum xz_ram_reason reason, const char *message) {
    s->status.state = value; s->status.reason = reason;
    snprintf(s->status.message, sizeof(s->status.message), "%s", message);
}
static const char *failure(enum xz_ram_reason r) {
    switch (r) {
    case XZ_RAM_TOO_LARGE: return "This file exceeds the 64 MiB RAM limit.";
    case XZ_RAM_BUDGET_FULL: return "Other RAM tracks leave too little space for this file.";
    case XZ_RAM_FREE_LOW: return "Free RAM cannot keep the 64 MiB safety reserve.";
    case XZ_RAM_PIN_FAILED: return "The loader could not reserve and pin this RAM file.";
    case XZ_RAM_SOURCE_CHANGED: return "The music source changed before the copy was ready.";
    case XZ_RAM_HANDOFF_FAILED: return "The player could not attach the RAM file.";
    default: return "The RAM copy was interrupted before it became ready.";
    }
}
static void close_owned(int *fd) { if (*fd >= 0) close(*fd); *fd = -1; }
static void dispose_candidate(struct session *s) {
    close_owned(&s->source_fd); close_owned(&s->old_fd); close_owned(&s->restore_fd);
    xz_ram_file_dispose(&s->file);
}
/* Leading thread only: reads the next stretch, LEAD_PASS bytes at most, for each deck whose track is not in RAM.
   Returns whether any deck needs the quick pace (it sounds, or its stretch is not complete). See "Leading". */
static int lead(struct xz_ram_extra *p) {
    static unsigned char scratch[XZ_RAM_COPY_PIECE];
    int leading = 0;
    for (unsigned deck = 0; deck < 2; ++deck) {
        char path[sizeof(((struct session *)0)->path)];
        uint64_t from = 0, to = 0; int wanted = 0;
        uint32_t unit = __atomic_load_n(&p->reading[deck], __ATOMIC_RELAXED);
        pthread_mutex_lock(&p->lock);
        struct session *s = p->deck[deck];
        if (s && !s->attached && !s->gone && !s->retired && !s->failed && s->original.bytes && current(s)) {
            /* A reader that has not read yet will start at the head of the file. */
            uint64_t at = unit ? (uint64_t)(unit - 1u) * 4096u : 0, end = at + LEAD_BYTES;
            int sounding = __atomic_load_n(&s->playing, __ATOMIC_RELAXED);
            if (end > s->original.bytes) end = s->original.bytes;
            if (sounding) s->lead_idle = 0;
            else if (s->lead_known && s->lead_to >= end && ++s->lead_idle >= LEAD_AGAIN_PASSES) s->lead_known = 0;
            /* Start again at the reader after a jump, or when it has run past what was read for it. */
            if (!s->lead_known || at < s->lead_from || at > s->lead_to) { s->lead_known = 1; s->lead_idle = 0; s->lead_from = s->lead_to = at; }
            if (sounding || s->lead_to < end) leading = 1;
            if (s->lead_to < end) {
                from = s->lead_to; to = end - from > LEAD_PASS ? from + LEAD_PASS : end;
                s->lead_to = to; memcpy(path, s->path, sizeof(path)); wanted = 1; /* the same size; terminated */
            }
        }
        pthread_mutex_unlock(&p->lock);
        if (!wanted) continue;
        if (xz_ram_extra_lead_probe) xz_ram_extra_lead_probe(deck, from, to - from);
        int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0) continue;
        while (from < to) {
            size_t count = to - from > sizeof(scratch) ? sizeof(scratch) : (size_t)(to - from);
            ssize_t n; do { n = pread(fd, scratch, count, (off_t)from); } while (n < 0 && errno == EINTR);
            if (n <= 0) break;
            from += (uint64_t)n;
        }
        close(fd);
    }
    return leading;
}
static void *lead_work(void *context) {
    struct xz_ram_extra *p = context;
    setpriority(PRIO_PROCESS, 0, XZ_RAM_COPY_NICE); /* this thread only; see budget.h */
    for (;;) {
        pthread_mutex_lock(&p->lock);
        int stop = p->stop;
        uint32_t ticket = xz_wake_ticket(&p->lead_wake);
        pthread_mutex_unlock(&p->lock);
        if (stop) break;
        xz_wake_wait(&p->lead_wake, ticket, lead(p) ? LEAD_EVERY_MS : 250);
    }
    return NULL;
}
static void reading(void *context, unsigned deck, uint32_t unit) {
    struct xz_ram_extra *p = context;
    if (deck < 2) __atomic_store_n(&p->reading[deck], unit, __ATOMIC_RELAXED);
}
/* The copy's view of the reader (ram/cache.h): where it reads, and how far ahead it must not wait. */
static uint32_t copy_wanted(void *context) {
    struct session *s = context;
    uint32_t unit = __atomic_load_n(&s->owner->reading[s->index], __ATOMIC_RELAXED);
    return unit && unit - 1u < (UINT32_MAX >> 12) ? (unit - 1u) << 12 : UINT32_MAX;
}
static uint32_t copy_lead(void *context) {
    struct session *s = context;
    return __atomic_load_n(&s->playing, __ATOMIC_RELAXED) ? LEAD_BYTES : 0;
}
static int cancelled(void *context) {
    struct session *s = context; struct xz_ram_extra *p = s->owner;
    pthread_mutex_lock(&p->lock);
    int no = p->stop || s->retired || s->cancelled || s->active_request != s->request ||
        !s->route || p->route(p->context) != s->route || !current(s);
    pthread_mutex_unlock(&p->lock);
    return no;
}
static int read_source(void *context, uint32_t offset, void *out, uint32_t bytes) {
    struct session *s = context; ssize_t n;
    do { n = pread(s->source_fd, out, bytes, offset); } while (n < 0 && errno == EINTR);
    struct timespec delay = {0, 1000000}; nanosleep(&delay, NULL);
    return (int)n;
}
static void progress(void *context, uint32_t bytes) {
    struct session *s = context; struct xz_ram_extra *p = s->owner;
    pthread_mutex_lock(&p->lock);
    if (!s->retired && s->active_request == s->request) s->status.copied = bytes;
    pthread_mutex_unlock(&p->lock);
}
static void refresh(struct session *s) {
    s->status.supported = 1;
    s->status.route_up = s->owner->route(s->owner->context) != 0;
    s->status.source_current = current(s);
    if (s->attached && !s->failed && !s->status.source_current && s->status.state != XZ_RAM_LOADING)
        state(s, XZ_RAM_DETACHED, XZ_RAM_SOURCE_CHANGED, "The music USB is out; this track is retained in RAM.");
    s->status.retained_assets = s->attached ? s->assets | XZ_RAM_AUDIO : 0;
    s->status.can_request = !s->busy && !s->queued && !s->prepared && !s->cleanup && !s->attached &&
        s->status.route_up && s->status.source_current && s->original.bytes && s->original.bytes <= XZ_RAM_TRACK_LIMIT;
    s->status.can_release = s->attached && !s->busy && !s->cleanup && !s->releasing &&
        s->status.source_current && s->status.state == XZ_RAM_READY;
}
static void *work(void *context) {
    struct xz_ram_extra *p = context;
    setpriority(PRIO_PROCESS, 0, XZ_RAM_COPY_NICE); /* this thread only; see budget.h */
    for (;;) {
        pthread_mutex_lock(&p->lock);
        struct session *s = p->retired;
        if (s) {
            p->retired = s->next;
            pthread_mutex_unlock(&p->lock);
            xz_stem_media_close(s->media); dispose_candidate(s); free(s);
            pthread_mutex_lock(&p->lock); --p->retired_count;
            pthread_mutex_unlock(&p->lock); continue;
        }
        int task = 0;
        /* Cleanup before admission, manual before automatic. One slot per deck. */
        for (int priority = 0; priority < 3 && !task; ++priority) for (unsigned i = 0; i < 2; ++i) {
            s = p->deck[i]; if (!s || s->busy) continue;
            if (!priority && s->cleanup) task = 1;
            else if (!priority && s->releasing && !s->prepared) task = 2;
            else if (priority && s->queued && s->source_fd >= 0 && s->manual == (priority == 1)) task = 3;
            if (task) break;
        }
        if (!task) {
            if (p->stop) { pthread_mutex_unlock(&p->lock); break; }
            uint32_t ticket = xz_wake_ticket(&p->wake);
            pthread_mutex_unlock(&p->lock); xz_wake_wait(&p->wake, ticket, 250); continue;
        }
        s->busy = 1;
        if (task == 3) { s->queued = 0; s->status.phase = XZ_RAM_COPYING; }
        pthread_mutex_unlock(&p->lock);
        enum xz_ram_reason error = XZ_RAM_OK;
        if (task == 3) {
            struct xz_ram_copy copy = {s, read_source, cancelled, progress, copy_wanted, copy_lead};
            xz_ram_copy_lock(s->manual);
            error = cancelled(s) ? XZ_RAM_INTERRUPTED : xz_ram_file_prepare(&s->file, s->source_fd, &s->original, &copy);
            xz_ram_copy_unlock();
            if (error != XZ_RAM_OK) dispose_candidate(s);
        } else if (task == 2) {
            s->restore_fd = open(s->path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
            struct xz_media_identity id;
            if (!current(s) || s->restore_fd < 0 || !xz_media_identify(s->restore_fd, &id) || !xz_media_same(&id, &s->original)) {
                close_owned(&s->restore_fd); error = XZ_RAM_SOURCE_CHANGED;
            }
        } else {
            /* The deck plays from its RAM copy: the kernel's cached pages of the file are a second copy that it
             * would only evict under pressure, during a later load (ram/runtime.c). Clean pages only. */
            if (s->attached) {
                int stick = s->old_fd >= 0 ? s->old_fd : s->source_fd;
                if (stick >= 0) (void)posix_fadvise(stick, 0, 0, POSIX_FADV_DONTNEED);
            }
            close_owned(&s->source_fd); close_owned(&s->old_fd); close_owned(&s->restore_fd);
            if (!s->attached) xz_ram_file_dispose(&s->file);
            else xz_ram_publish(&s->file.ticket);
        }
        pthread_mutex_lock(&p->lock);
        s->busy = 0;
        if (task == 3) {
            if (s->retired || s->cancelled || s->active_request != s->request ||
                p->route(p->context) != s->route || !current(s)) {
                s->cleanup = 1;
                if (!s->cancelled) state(s, XZ_RAM_ERROR, XZ_RAM_INTERRUPTED, failure(XZ_RAM_INTERRUPTED));
            } else if (error != XZ_RAM_OK) {
                s->status.phase = XZ_RAM_IDLE; state(s, XZ_RAM_ERROR, error, failure(error));
            } else { s->prepared = 1; s->status.phase = XZ_RAM_ATTACHING;
                snprintf(s->status.message, sizeof(s->status.message), "Copy complete; attaching to the player."); }
        } else if (task == 2 && s->gone) {
            s->releasing = 0; s->cleanup = 1; /* the stick went while it was checked: keep the copy */
        } else if (task == 2) {
            if (error != XZ_RAM_OK) { s->releasing = 0; s->status.phase = XZ_RAM_IDLE;
                state(s, XZ_RAM_DETACHED, error, "The original source cannot be verified; RAM is retained."); }
            else s->prepared = 1;
        } else {
            s->cleanup = s->releasing = s->prepared = 0; s->status.phase = XZ_RAM_IDLE;
            if (s->attached && !s->failed) state(s, current(s) ? XZ_RAM_READY : XZ_RAM_DETACHED, current(s) ? XZ_RAM_OK : XZ_RAM_SOURCE_CHANGED,
                current(s) ? "Original audio is in RAM." : "The music USB is out; this track is retained in RAM.");
            else if (s->status.state != XZ_RAM_ERROR) state(s, XZ_RAM_EMPTY, XZ_RAM_OK, "This track is playing from USB.");
        }
        pthread_mutex_unlock(&p->lock);
    }
    return NULL;
}
static void invalidate(void *context, unsigned deck) {
    struct xz_ram_extra *p = context;
    pthread_mutex_lock(&p->lock);
    struct session *s = p->deck[deck];
    if (s) {
        s->status.key.session = ++p->serial;
        if (!s->attached) { ++s->request; s->cancelled = 1; s->queued = 0; s->cleanup = 1; s->status.phase = XZ_RAM_RELEASING;
            state(s, XZ_RAM_EMPTY, XZ_RAM_OK, "This track is playing from USB."); }
    }
    pthread_mutex_unlock(&p->lock); xz_wake_set(&p->wake);
}
static void retire(void *context, unsigned deck, struct xz_stem_media *media) {
    struct xz_ram_extra *p = context;
    pthread_mutex_lock(&p->lock);
    struct session *s = p->deck[deck];
    if(s){++s->request;s->cancelled=1;s->queued=0;}
    /* Lifecycle only: bound the retired queue even if a source read is blocked. */
    while (p->retired_count >= 8) {   /* the worker empties the queue; look again shortly */
        pthread_mutex_unlock(&p->lock); xz_wake_set(&p->wake);
        struct timespec nap = {0, 2000000}; nanosleep(&nap, NULL);
        pthread_mutex_lock(&p->lock);
    }
    if (s) {
        s->retired = 1; ++s->request; s->media = media;
        s->next = p->retired; p->retired = s; ++p->retired_count; p->deck[deck] = NULL;
        pthread_mutex_unlock(&p->lock); xz_wake_set(&p->wake);
    } else { pthread_mutex_unlock(&p->lock); xz_stem_media_close(media); }
}
static void bind(void *context, unsigned deck, struct xz_stem_media *media, unsigned source,
                 uint32_t track, uint64_t generation, const char *path, unsigned assets) {
    struct xz_ram_extra *p = context;
    struct session *s = calloc(1, sizeof(*s));
    if (!s) return;
    s->owner = p; s->media = media; s->file = (struct xz_ram_file)XZ_RAM_FILE_INIT;
    s->source_fd = s->old_fd = s->restore_fd = -1; s->assets = assets; s->index = deck;
    int fd = -1;
    (void)xz_stem_media_source_snapshot(media, &s->original, &fd); close_owned(&fd);
    snprintf(s->path, sizeof(s->path), "%s", path);
    s->status.key = (struct xz_ram_track_key){0, generation, track, source};
    s->status.bytes = s->original.bytes <= UINT32_MAX ? (uint32_t)s->original.bytes : UINT32_MAX;
    state(s, XZ_RAM_EMPTY, XZ_RAM_OK, "This track is playing from USB.");
    if (!s->original.bytes) state(s, XZ_RAM_ERROR, XZ_RAM_NO_SOURCE, "Load a USB track on this deck first.");
    else if (s->original.bytes > XZ_RAM_TRACK_LIMIT) state(s, XZ_RAM_ERROR, XZ_RAM_TOO_LARGE, failure(XZ_RAM_TOO_LARGE));
    /* Where the new track's reader is: the load has read its first buffer at the cue point (extra_player.c), and
       a deck starts there, often far from the head of the file. The first lead pass, woken below, reads from
       there before the deck's worker has said anything. 0 (the head) when it has not read. */
    uint64_t at = xz_stem_media_source_offset(media);
    __atomic_store_n(&p->reading[deck], at && (at - 1u) >> 12 < UINT32_MAX ? (uint32_t)((at - 1u) >> 12) + 1u : 0u,
                     __ATOMIC_RELAXED);
    pthread_mutex_lock(&p->lock); s->status.key.session = ++p->serial; p->deck[deck] = s;
    pthread_mutex_unlock(&p->lock);
    xz_wake_set(&p->wake); xz_wake_set(&p->lead_wake); /* the megabyte after the reader is read at once */
}
static void request(struct session *s, int manual) {
    ++s->request; s->active_request = s->request; s->cancelled = 0;
    s->queued = 1; s->manual = manual; s->route = s->owner->route(s->owner->context);
    s->status.copied = 0; s->status.phase = XZ_RAM_QUEUED;
    state(s, XZ_RAM_LOADING, XZ_RAM_OK, "Keep the music USB connected until copying finishes.");
}
/* What the background worker acts on. step() wakes it only when this changes. While the worker is busy it owns
   the session's descriptors and closes them outside the lock: their state is not read then. */
static unsigned work_mark(const struct session *s) {
    return (unsigned)(s->queued != 0) | (unsigned)(s->cleanup != 0) << 1 | (unsigned)(s->prepared != 0) << 2 |
           (unsigned)(s->releasing != 0) << 3 | (unsigned)(s->busy || s->source_fd >= 0) << 4 |
           (unsigned)(s->playing != 0) << 5 | s->request << 6;
}
/* Runs on the DECK 3/4 real-time thread, every pass: it must never wait. The mutex is only tried, and the
   worker is woken through xz_wake, and only when this pass left it something to do. */
static int step(void *context, unsigned deck, struct xz_stem_media *media, int use) {
    struct xz_ram_extra *p = context;
    if (pthread_mutex_trylock(&p->lock)) return 0;
    struct session *s = p->deck[deck];
    if (!s || s->media != media) { pthread_mutex_unlock(&p->lock); return 0; }
    unsigned before = work_mark(s);
    refresh(s);
    __atomic_store_n(&s->playing, (use & XZ_RAM_STEP_PLAYING) != 0, __ATOMIC_RELAXED);
    if (use && !s->blocked && s->status.state == XZ_RAM_EMPTY && s->status.can_request) request(s, 0);
    if (!s->attached && (s->queued || s->prepared) && (p->route(p->context) != s->route || !current(s))) {
        ++s->request; s->queued = s->prepared = 0; s->cleanup = 1;
        state(s, XZ_RAM_ERROR, XZ_RAM_INTERRUPTED, failure(XZ_RAM_INTERRUPTED));
    }
    if (!s->busy && !s->cleanup && s->queued && s->source_fd < 0) {
        /* Only a duplicate here. Asking the stick who the file is (fstat) can wait behind the stick's whole
           queue, and this is the deck's real-time thread. The worker compares the identity before it copies
           (ram/file.c) and reports a changed source from there. */
        s->source_fd = xz_stem_media_source_dup(media);
        if (s->source_fd < 0) {
            s->queued = 0; s->cleanup = 1;
            state(s, XZ_RAM_ERROR, XZ_RAM_SOURCE_CHANGED, failure(XZ_RAM_SOURCE_CHANGED));
        }
    }
    if (!s->busy && !s->cleanup && s->prepared) {
        if (s->releasing && (!current(s) || s->status.state == XZ_RAM_DETACHED)) {
            s->prepared = 0; s->cleanup = 1;
        } else {
            /* To RAM: the copy was made from a duplicate of this reader's descriptor (source_fd), so no
               identity is asked of the stick here. Back to the stick: the reader is on the RAM file, whose
               identity costs nothing to read. */
            int result = xz_stem_media_replace_file(media, s->attached ? &s->file.identity : NULL,
                                                     s->releasing ? s->restore_fd : s->file.fd, &s->old_fd);
            s->prepared = 0; s->cleanup = 1;
            if (result == 1) {
                s->attached = !s->releasing;
                if (!s->attached) state(s, XZ_RAM_EMPTY, XZ_RAM_OK, "RAM released; this track is playing from USB.");
            } else if (result < 0) {
                /* Keep every backing pinned until explicit replacement/unload. */
                s->attached = s->failed = 1;
                state(s, XZ_RAM_ERROR, XZ_RAM_HANDOFF_FAILED, "Reader handoff failed; load another track to recover.");
            } else if (!s->attached) state(s, XZ_RAM_ERROR, XZ_RAM_HANDOFF_FAILED, failure(XZ_RAM_HANDOFF_FAILED));
        }
    }
    refresh(s); int result = s->failed ? -1 : 0;
    int changed = work_mark(s) != before;
    pthread_mutex_unlock(&p->lock);
    if (changed) { xz_wake_set(&p->wake); xz_wake_set(&p->lead_wake); }
    return result;
}
void xz_ram_extra_status(struct xz_ram_extra *p, unsigned deck, struct xz_ram_status *out) {
    memset(out, 0, sizeof(*out)); out->reason = XZ_RAM_NO_ENGINE;
    snprintf(out->message, sizeof(out->message), "This loader does not include the RAM engine.");
    if (!p || deck >= 2) return;
    pthread_mutex_lock(&p->lock);
    struct session *s = p->deck[deck];
    if (s) { refresh(s); *out = s->status; }
    else { out->state = XZ_RAM_EMPTY; out->supported = 1; out->route_up = p->route(p->context) != 0;
        out->reason = XZ_RAM_NO_SOURCE; snprintf(out->message, sizeof(out->message), "Load a USB track on this deck first."); }
    if (!out->route_up && !(out->retained_assets & XZ_RAM_AUDIO) && out->state != XZ_RAM_LOADING) {
        out->reason = XZ_RAM_NO_ROUTE; snprintf(out->message, sizeof(out->message), "This deck needs the loopback audio connection.");
    }
    pthread_mutex_unlock(&p->lock);
}
static void status(void *p, unsigned d, struct xz_ram_status *s) { xz_ram_extra_status(p, d, s); }
/* Lock held. A session keeping its copy for good: no release back to the stick, every stick descriptor closed. A
   busy worker owns the descriptors (it closes them outside the lock): it gets a cleanup after its task. */
static void keep_for_good(struct session *s) {
    s->gone = s->blocked = 1; ++s->request; s->queued = 0;
    if (s->busy || s->releasing || s->prepared || s->source_fd >= 0 || s->old_fd >= 0 || s->restore_fd >= 0) {
        s->releasing = s->prepared = 0; s->cleanup = 1;
    }
}
void xz_ram_extra_source_gone(struct xz_ram_extra *p, unsigned slot) {
    if (!p) return;
    pthread_mutex_lock(&p->lock);
    for (unsigned i = 0; i < 2; ++i) {
        struct session *s = p->deck[i];
        if (s && s->status.key.source == slot && s->attached && !s->failed) keep_for_good(s);
    }
    pthread_mutex_unlock(&p->lock); xz_wake_set(&p->wake);
}
static int release(void *context, unsigned deck, struct xz_stem_media *media) {
    struct xz_ram_extra *p = context;
    pthread_mutex_lock(&p->lock);
    struct session *s = p->deck[deck];
    int kept = s && s->media == media && s->attached && !s->failed;
    if (kept) keep_for_good(s);
    pthread_mutex_unlock(&p->lock);
    if (kept) xz_wake_set(&p->wake);
    return kept;
}
enum xz_ram_result xz_ram_extra_command(struct xz_ram_extra *p, unsigned deck, enum xz_ram_command cmd,
                                       const struct xz_ram_track_key *expected) {
    if (!p || deck >= 2) return XZ_RAM_REFUSED;
    pthread_mutex_lock(&p->lock);
    struct session *s = p->deck[deck]; enum xz_ram_result result = XZ_RAM_REFUSED;
    if (!s) result = XZ_RAM_SOURCE_MISSING;
    else if (!expected || expected->session != s->status.key.session || expected->media_generation != s->status.key.media_generation ||
        expected->track_id != s->status.key.track_id || expected->source != s->status.key.source) result = XZ_RAM_STALE;
    else {
        refresh(s);
        if (cmd == XZ_RAM_CANCEL) {
            if (s->attached) result = XZ_RAM_ALREADY_HELD;
            else { ++s->request; s->blocked = s->cancelled = 1; s->queued = s->prepared = 0; s->cleanup = 1; s->status.phase = XZ_RAM_RELEASING;
                state(s, XZ_RAM_EMPTY, XZ_RAM_OK, "Copy cancelled; this track is playing from USB."); result = XZ_RAM_ACCEPTED; }
        } else if (cmd == XZ_RAM_RELEASE) {
            if (s->status.can_release) { s->blocked = 1; s->releasing = 1; s->status.phase = XZ_RAM_RELEASING;
                snprintf(s->status.message, sizeof(s->status.message), "Checking the original source before releasing RAM."); result = XZ_RAM_ACCEPTED; }
        } else if (cmd == XZ_RAM_LOAD) {
            if (s->attached) result = XZ_RAM_ALREADY_HELD;
            else if (!s->status.route_up) result = XZ_RAM_ROUTE_DOWN;
            else if (!s->status.source_current) result = XZ_RAM_SOURCE_MISSING;
            else if (s->status.can_request) { request(s, 1); result = XZ_RAM_ACCEPTED; }
        }
    }
    pthread_mutex_unlock(&p->lock); xz_wake_set(&p->wake); return result;
}
struct xz_ram_player_service xz_ram_extra_player_service(struct xz_ram_extra *p) {
    return (struct xz_ram_player_service){p, invalidate, bind, step, retire, status, release, reading};
}
struct xz_ram_extra *xz_ram_extra_create(void *context, uint32_t (*route)(void *), uint64_t (*generation)(void *, unsigned)) {
    if (!route || !generation) return NULL;
    struct xz_ram_extra *p = calloc(1, sizeof(*p)); if (!p) return NULL;
    p->context = context; p->route = route; p->generation = generation;
    xz_mutex_init(&p->lock);
    struct rlimit limit = {RLIM_INFINITY, RLIM_INFINITY}; (void)setrlimit(RLIMIT_MEMLOCK, &limit);
    if (XZ_WORKER_CREATE(&p->thread,work,p)) {
        pthread_mutex_destroy(&p->lock); free(p); return NULL;
    }
    if (XZ_WORKER_CREATE(&p->lead_thread,lead_work,p)) {
        pthread_mutex_lock(&p->lock); p->stop = 1; pthread_mutex_unlock(&p->lock); xz_wake_set(&p->wake);
        pthread_join(p->thread, NULL); pthread_mutex_destroy(&p->lock); free(p); return NULL;
    }
    return p;
}
void xz_ram_extra_destroy(struct xz_ram_extra *p) {
    if (!p) return;
    /* Player has stopped and transferred every loaded reader before this call. */
    pthread_mutex_lock(&p->lock); p->stop = 1; pthread_mutex_unlock(&p->lock);
    xz_wake_set(&p->wake); xz_wake_set(&p->lead_wake);
    pthread_join(p->thread, NULL); pthread_join(p->lead_thread, NULL);
    pthread_mutex_destroy(&p->lock); free(p);
}
