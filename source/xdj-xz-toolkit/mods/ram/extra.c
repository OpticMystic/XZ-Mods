/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include "extra.h"
#include "file.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
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
};
struct xz_ram_extra {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    pthread_t thread;
    struct session *deck[2], *retired;
    unsigned retired_count;
    uint64_t serial;
    int stop;
    void *context;
    uint32_t (*route)(void *);
    uint64_t (*generation)(void *, unsigned);
};
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
    if (sched_setscheduler(0, SCHED_IDLE, &(struct sched_param){0})) setpriority(PRIO_PROCESS, 0, 10);
    for (;;) {
        pthread_mutex_lock(&p->lock);
        struct session *s = p->retired;
        if (s) {
            p->retired = s->next;
            pthread_mutex_unlock(&p->lock);
            xz_stem_media_close(s->media); dispose_candidate(s); free(s);
            pthread_mutex_lock(&p->lock); --p->retired_count; pthread_cond_broadcast(&p->changed);
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
            pthread_cond_wait(&p->changed, &p->lock); pthread_mutex_unlock(&p->lock); continue;
        }
        s->busy = 1;
        if (task == 3) { s->queued = 0; s->status.phase = XZ_RAM_COPYING; }
        pthread_mutex_unlock(&p->lock);
        enum xz_ram_reason error = XZ_RAM_OK;
        if (task == 3) {
            struct xz_ram_copy copy = {s, read_source, cancelled, progress};
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
        pthread_cond_broadcast(&p->changed); pthread_mutex_unlock(&p->lock);
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
    pthread_cond_signal(&p->changed); pthread_mutex_unlock(&p->lock);
}
static void retire(void *context, unsigned deck, struct xz_stem_media *media) {
    struct xz_ram_extra *p = context;
    pthread_mutex_lock(&p->lock);
    struct session *s = p->deck[deck];
    if(s){++s->request;s->cancelled=1;s->queued=0;}
    /* Lifecycle only: bound the retired queue even if a source read is blocked. */
    while ((s && s->busy) || p->retired_count >= 8) pthread_cond_wait(&p->changed, &p->lock);
    if (s) {
        s->retired = 1; ++s->request; s->media = media;
        s->next = p->retired; p->retired = s; ++p->retired_count; p->deck[deck] = NULL;
        pthread_cond_signal(&p->changed); pthread_mutex_unlock(&p->lock);
    } else { pthread_mutex_unlock(&p->lock); xz_stem_media_close(media); }
}
static void bind(void *context, unsigned deck, struct xz_stem_media *media, unsigned source,
                 uint32_t track, uint64_t generation, const char *path, unsigned assets) {
    struct xz_ram_extra *p = context;
    struct session *s = calloc(1, sizeof(*s));
    if (!s) return;
    s->owner = p; s->media = media; s->file = (struct xz_ram_file)XZ_RAM_FILE_INIT;
    s->source_fd = s->old_fd = s->restore_fd = -1; s->assets = assets;
    int fd = -1;
    (void)xz_stem_media_source_snapshot(media, &s->original, &fd); close_owned(&fd);
    snprintf(s->path, sizeof(s->path), "%s", path);
    s->status.key = (struct xz_ram_track_key){0, generation, track, source};
    s->status.bytes = s->original.bytes <= UINT32_MAX ? (uint32_t)s->original.bytes : UINT32_MAX;
    state(s, XZ_RAM_EMPTY, XZ_RAM_OK, "This track is playing from USB.");
    if (!s->original.bytes) state(s, XZ_RAM_ERROR, XZ_RAM_NO_SOURCE, "Load a USB track on this deck first.");
    else if (s->original.bytes > XZ_RAM_TRACK_LIMIT) state(s, XZ_RAM_ERROR, XZ_RAM_TOO_LARGE, failure(XZ_RAM_TOO_LARGE));
    pthread_mutex_lock(&p->lock); s->status.key.session = ++p->serial; p->deck[deck] = s;
    pthread_mutex_unlock(&p->lock);
}
static void request(struct session *s, int manual) {
    ++s->request; s->active_request = s->request; s->cancelled = 0;
    s->queued = 1; s->manual = manual; s->route = s->owner->route(s->owner->context);
    s->status.copied = 0; s->status.phase = XZ_RAM_QUEUED;
    state(s, XZ_RAM_LOADING, XZ_RAM_OK, "Keep the music USB connected until copying finishes.");
}
static int step(void *context, unsigned deck, struct xz_stem_media *media, int playing) {
    struct xz_ram_extra *p = context;
    if (pthread_mutex_trylock(&p->lock)) return 0;
    struct session *s = p->deck[deck];
    if (!s || s->media != media) { pthread_mutex_unlock(&p->lock); return 0; }
    refresh(s);
    if (playing && !s->blocked && s->status.state == XZ_RAM_EMPTY && s->status.can_request) request(s, 0);
    if (!s->attached && (s->queued || s->prepared) && (p->route(p->context) != s->route || !current(s))) {
        ++s->request; s->queued = s->prepared = 0; s->cleanup = 1;
        state(s, XZ_RAM_ERROR, XZ_RAM_INTERRUPTED, failure(XZ_RAM_INTERRUPTED));
    }
    if (!s->busy && !s->cleanup && s->queued && s->source_fd < 0) {
        struct xz_media_identity id;
        if (!xz_stem_media_source_snapshot(media, &id, &s->source_fd) || !xz_media_same(&id, &s->original)) {
            s->queued = 0; s->cleanup = 1;
            state(s, XZ_RAM_ERROR, XZ_RAM_SOURCE_CHANGED, failure(XZ_RAM_SOURCE_CHANGED));
        }
    }
    if (!s->busy && !s->cleanup && s->prepared) {
        if (s->releasing && (!current(s) || s->status.state == XZ_RAM_DETACHED)) {
            s->prepared = 0; s->cleanup = 1;
        } else {
            int result = xz_stem_media_replace_file(media, s->attached ? &s->file.identity : &s->original,
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
    pthread_cond_signal(&p->changed); pthread_mutex_unlock(&p->lock); return result;
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
    pthread_cond_signal(&p->changed); pthread_mutex_unlock(&p->lock); return result;
}
struct xz_ram_player_service xz_ram_extra_player_service(struct xz_ram_extra *p) {
    return (struct xz_ram_player_service){p, invalidate, bind, step, retire, status};
}
struct xz_ram_extra *xz_ram_extra_create(void *context, uint32_t (*route)(void *), uint64_t (*generation)(void *, unsigned)) {
    if (!route || !generation) return NULL;
    struct xz_ram_extra *p = calloc(1, sizeof(*p)); if (!p) return NULL;
    p->context = context; p->route = route; p->generation = generation;
    pthread_mutex_init(&p->lock, NULL); pthread_cond_init(&p->changed, NULL);
    struct rlimit limit = {RLIM_INFINITY, RLIM_INFINITY}; (void)setrlimit(RLIMIT_MEMLOCK, &limit);
    if (pthread_create(&p->thread, NULL, work, p)) {
        pthread_mutex_destroy(&p->lock); pthread_cond_destroy(&p->changed); free(p); return NULL;
    }
    return p;
}
void xz_ram_extra_destroy(struct xz_ram_extra *p) {
    if (!p) return;
    /* Player has stopped and transferred every loaded reader before this call. */
    pthread_mutex_lock(&p->lock); p->stop = 1; pthread_cond_signal(&p->changed); pthread_mutex_unlock(&p->lock);
    pthread_join(p->thread, NULL);
    pthread_cond_destroy(&p->changed); pthread_mutex_destroy(&p->lock); free(p);
}
