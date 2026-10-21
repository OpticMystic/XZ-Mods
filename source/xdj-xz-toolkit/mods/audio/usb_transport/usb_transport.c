// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#include "usb_transport.h"
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/vfs.h>
#include <time.h>
#include <unistd.h>

_Static_assert(ATOMIC_INT_LOCK_FREE == 2 && sizeof(unsigned) == 4, "lock-free ARM32 words required");
/* Linux 3.0 usbfs UAPI, matching the qualified diagnostic. */
struct control { uint8_t type, request; uint16_t value, index, length; unsigned timeout; void *data; };
struct interface { unsigned number, alternate; };
struct urb { unsigned char type, endpoint; int status; unsigned flags; void *buffer;
    int buffer_length, actual_length, start_frame, packets, error_count; unsigned signr; void *context; };
struct iso { unsigned length, actual_length, status; };
#define CONTROL _IOWR('U', 0, struct control)
#define SET_INTERFACE _IOR('U', 4, struct interface)
#define SUBMIT _IOR('U', 10, struct urb)
#define DISCARD _IO('U', 11)
#define REAP _IOW('U', 13, void *)
#define CLAIM _IOR('U', 15, unsigned)
#define RELEASE _IOR('U', 16, unsigned)
#define PACKETS 8u
#define DEPTH 16u
#define BLOCKS 32u
#define OUT_BYTES 36u
#define IN_BYTES 24u
#if UINTPTR_MAX == 0xffffffffu
_Static_assert(sizeof(struct urb) == 44 && sizeof(struct control) == 16, "ARM usbfs ABI mismatch");
#endif

struct block { uint32_t generation, frames; uint64_t source_after; uint8_t pcm[XZ_USB_BLOCK_FRAMES * 6]; };
struct deck_queue {
    atomic_uint write, read, generation, producer;
    uint32_t offset; /* consumer only, within the read block */
    struct block block[BLOCKS];
};
struct contribution { uint32_t generation, frames, source_valid, source_frames; uint64_t source_after, source_wire_frame; };
struct slot {
    struct urb u; struct iso iso[PACKETS]; uint8_t data[PACKETS * 1024];
    unsigned busy, clock[PACKETS], clock_generation, used_generation;
    uint64_t sequence;
    struct contribution deck[2];
};
/* Private seam: tests include this translation unit and inject fake usbfs. */
struct io_ops {
    int (*open_node)(const char *);
    int (*request)(int, unsigned long, void *);
    int (*wait)(int, int);
    int (*close_fd)(int);
    uint64_t (*now)(void);
};
struct xz_usb_transport {
    const struct io_ops *io;
    int fd, claimed, alternate, threaded;
    pthread_t worker;
    pthread_mutex_t snapshot_lock;
    atomic_uint state, stop, fault, system_errno, cleanup_errno;
    struct deck_queue queue[2];
    struct slot input[DEPTH], output[DEPTH];
    xz_usb_position live, published;
    unsigned packet_index;
    uint64_t out_sequence, completed_sequence, last_input, last_output, last_completion, last_publish;
};
static unsigned load32(const atomic_uint *p) { return atomic_load_explicit(p, memory_order_acquire); }
static void store32(atomic_uint *p, unsigned n) { atomic_store_explicit(p, n, memory_order_release); }
static uint64_t monotonic_us(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000u + (unsigned)t.tv_nsec / 1000u;
}
static int open_ram_node(const char *path) {
    if (!path || strncmp(path, "/dev/shm/", 9)) { errno = EINVAL; return -1; }
    int fd = open(path, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return -1;
    struct stat s; struct statfs fs;
    if (fstat(fd, &s) || !S_ISCHR(s.st_mode) || major(s.st_rdev) != 189 ||
        fstatfs(fd, &fs) || (unsigned long)fs.f_type != 0x01021994ul) {
        close(fd); errno = EINVAL; return -1;
    }
    return fd;
}
static int real_request(int fd, unsigned long request, void *data) { return ioctl(fd, request, data); }
static int real_wait(int fd, int ms) { struct pollfd p = {fd, POLLOUT, 0}; return poll(&p, 1, ms); }
static const struct io_ops real_io = {open_ram_node, real_request, real_wait, close, monotonic_us};
static int control(xz_usb_transport *t, uint8_t type, uint8_t request, uint16_t value,
                   uint16_t index, void *data, uint16_t length) {
    struct control c = {type, request, value, index, length, 1000, data};
    return t->io->request(t->fd, CONTROL, &c);
}
static void publish(xz_usb_transport *t, int final) {
    if (final) pthread_mutex_lock(&t->snapshot_lock);
    else if (pthread_mutex_trylock(&t->snapshot_lock)) return;
    t->published = t->live;
    pthread_mutex_unlock(&t->snapshot_lock);
    t->last_publish = t->io->now();
}
static void fail(xz_usb_transport *t, xz_usb_result result, int error) {
    store32(&t->system_errno, (unsigned)error);
    store32(&t->fault, result);
    store32(&t->state, XZ_USB_FAULTED);
}
static void pack24(uint8_t *p, int32_t v) {
    uint32_t u = (uint32_t)v; p[0] = u; p[1] = u >> 8; p[2] = u >> 16;
}
static int pack_block(struct block *b, const float *pcm, unsigned frames) {
    for (unsigned i = 0; i < frames * 2; ++i) {
        float value = pcm[i];
        if (!isfinite(value)) return 0;
        int32_t sample = value >= 1.0f ? 8388607 : value <= -1.0f ? -8388608 : (int32_t)(value * 8388608.0f);
        pack24(b->pcm + i * 3, sample);
    }
    return 1;
}
static int take_producer(struct deck_queue *q) {
    unsigned expected = 0;
    return atomic_compare_exchange_strong_explicit(&q->producer, &expected, 1, memory_order_acquire, memory_order_relaxed);
}
static int deck_index(xz_usb_deck deck) { return deck == XZ_USB_DECK_3 ? 0 : deck == XZ_USB_DECK_4 ? 1 : -1; }
xz_usb_result xz_usb_transport_submit(xz_usb_transport *t, xz_usb_deck deck, uint32_t generation,
                                      const float *pcm, uint32_t frames, uint64_t source_after) {
    int d = deck_index(deck);
    if (!t || d < 0 || !pcm || !frames || frames > XZ_USB_BLOCK_FRAMES || !generation) return XZ_USB_INVALID;
    unsigned state = load32(&t->state);
    if (state == XZ_USB_STARTING) return XZ_USB_AGAIN;
    if (state != XZ_USB_RUNNING || load32(&t->stop)) return XZ_USB_STOPPED;
    struct deck_queue *q = &t->queue[d];
    if (!take_producer(q)) return XZ_USB_BUSY;
    xz_usb_result result = XZ_USB_OK;
    unsigned write = load32(&q->write), read = load32(&q->read);
    if (generation != load32(&q->generation)) result = XZ_USB_STALE;
    else if ((unsigned)(write - read) >= BLOCKS) result = XZ_USB_AGAIN;
    else {
        struct block *b = &q->block[write % BLOCKS];
        if (!pack_block(b, pcm, frames)) result = XZ_USB_INVALID;
        else {
            b->frames = frames; b->generation = generation; b->source_after = source_after;
            store32(&q->write, write + 1);
        }
    }
    store32(&q->producer, 0);
    return result;
}
xz_usb_result xz_usb_transport_flush_and_submit(xz_usb_transport *t, xz_usb_deck deck,
                                      uint32_t expected_generation, const float *pcm, uint32_t frames,
                                      uint64_t source_after, uint32_t *generation) {
    int d = deck_index(deck);
    if (!t || d < 0 || !pcm || !frames || frames > XZ_USB_BLOCK_FRAMES || !expected_generation || !generation)
        return XZ_USB_INVALID;
    unsigned state = load32(&t->state);
    if (state == XZ_USB_STARTING) return XZ_USB_AGAIN;
    if (state != XZ_USB_RUNNING || load32(&t->stop)) return XZ_USB_STOPPED;
    struct deck_queue *q = &t->queue[d];
    if (!take_producer(q)) return XZ_USB_BUSY;
    xz_usb_result result = XZ_USB_OK;
    unsigned current = load32(&q->generation), write = load32(&q->write), read = load32(&q->read);
    if (current != expected_generation) result = XZ_USB_STALE;
    else if (current == UINT32_MAX) result = XZ_USB_INVALID;
    else if ((unsigned)(write - read) >= BLOCKS) result = XZ_USB_AGAIN;
    else {
        struct block *b = &q->block[write % BLOCKS];
        if (!pack_block(b, pcm, frames)) result = XZ_USB_INVALID;
        else {
            unsigned next = current + 1;
            b->frames = frames; b->generation = next; b->source_after = source_after;
            store32(&q->write, write + 1);
            /* An acquire of this generation also observes the queued first block. */
            store32(&q->generation, next);
            *generation = next;
        }
    }
    store32(&q->producer, 0);
    return result;
}
xz_usb_result xz_usb_transport_flush(xz_usb_transport *t, xz_usb_deck deck, uint32_t *generation) {
    int d = deck_index(deck);
    if (!t || d < 0 || !generation) return XZ_USB_INVALID;
    unsigned state = load32(&t->state);
    if ((state != XZ_USB_RUNNING && state != XZ_USB_STARTING) || load32(&t->stop)) return XZ_USB_STOPPED;
    struct deck_queue *q = &t->queue[d];
    if (!take_producer(q)) return XZ_USB_BUSY;
    unsigned next = load32(&q->generation);
    if (next == UINT32_MAX) { store32(&q->producer, 0); return XZ_USB_INVALID; }
    *generation = next + 1;
    store32(&q->generation, *generation);
    store32(&q->producer, 0);
    return XZ_USB_OK;
}
xz_usb_result xz_usb_transport_position(xz_usb_transport *t, xz_usb_position *out) {
    if (!t || !out) return XZ_USB_INVALID;
    xz_usb_result result = XZ_USB_OK;
    if (pthread_mutex_trylock(&t->snapshot_lock)) { memset(out, 0, sizeof(*out)); result = XZ_USB_AGAIN; }
    else { *out = t->published; pthread_mutex_unlock(&t->snapshot_lock); }
    out->state = (xz_usb_state)load32(&t->state);
    out->fault = (xz_usb_result)load32(&t->fault);
    out->system_errno = (int)load32(&t->system_errno);
    out->cleanup_errno = (int)load32(&t->cleanup_errno);
    for (int d = 0; d < 2; ++d) out->requested_generation[d] = load32(&t->queue[d].generation);
    return result;
}
static void apply_generation(xz_usb_transport *t, unsigned d, unsigned generation) {
    xz_usb_deck_position *p = &t->live.deck[d];
    if (p->applied_generation == generation) return;
    memset(p, 0, sizeof(*p));
    p->applied_generation = generation;
    p->prior_audio_through_wire_frame = t->live.wire_submitted_frames;
}
static void take_frames(xz_usb_transport *t, unsigned d, struct contribution *c, uint8_t *out, unsigned frames, uint64_t wire_start) {
    struct deck_queue *q = &t->queue[d];
    unsigned filled = 0, scanned = 0;
    while (filled < frames && scanned < BLOCKS) {
        unsigned read = load32(&q->read), write = load32(&q->write);
        if (read == write) break;
        struct block *b = &q->block[read % BLOCKS];
        if (b->generation > c->generation) break;
        if (b->generation < c->generation) {
            q->offset = 0; store32(&q->read, read + 1); ++scanned; continue;
        }
        unsigned count = b->frames - q->offset;
        if (count > frames - filled) count = frames - filled;
        for (unsigned n = 0; n < count; ++n)
            memcpy(out + (filled + n) * OUT_BYTES + (4 + 2*d) * 3, b->pcm + (q->offset + n) * 6, 6);
        filled += count; c->frames += count; q->offset += count;
        if (q->offset == b->frames) {
            c->source_after = b->source_after; c->source_valid = 1;
            c->source_frames=c->frames;c->source_wire_frame=wire_start+filled;
            q->offset = 0; store32(&q->read, read + 1); ++scanned;
        }
    }
}
static void prepare(xz_usb_transport *t, struct slot *s, int input, int consume) {
    memset(&s->u, 0, sizeof(s->u)); memset(s->iso, 0, sizeof(s->iso));
    memset(s->deck, 0, sizeof(s->deck));
    s->u.endpoint = input ? 0x82 : 1; s->u.flags = 2;
    s->u.buffer = s->data; s->u.packets = PACKETS; s->u.context = s;
    if (!input) for (unsigned d = 0; d < 2; ++d) {
        s->deck[d].generation = load32(&t->queue[d].generation);
        apply_generation(t, d, s->deck[d].generation);
    }
    unsigned offset = 0;
    for (unsigned i = 0; i < PACKETS; ++i) {
        if (input) s->iso[i].length = 1024;
        else {
            unsigned count = t->packet_index++ % 40 == 0 ? 12 : 11;
            if (s->clock_generation && s->clock[i] + 4 >= count && s->clock[i] <= count + 3) count = s->clock[i];
            else ++t->live.fallback_packets;
            s->iso[i].length = count * OUT_BYTES;
            memset(s->data + offset, 0, s->iso[i].length);
            if (consume) for (unsigned d = 0; d < 2; ++d) take_frames(t, d, &s->deck[d], s->data + offset, count,
                t->live.wire_submitted_frames+offset/OUT_BYTES);
        }
        offset += s->iso[i].length;
    }
    s->used_generation = s->clock_generation;
    s->u.buffer_length = (int)offset;
}
static int submit_slot(xz_usb_transport *t, struct slot *s, int input) {
    if (t->io->request(t->fd, SUBMIT, &s->u)) return 0;
    s->busy = 1;
    if (!input) {
        s->sequence = t->out_sequence++;
        unsigned frames = (unsigned)s->u.buffer_length / OUT_BYTES;
        t->live.wire_submitted_frames += frames;
        for (unsigned d = 0; d < 2; ++d) {
            xz_usb_deck_position *p = &t->live.deck[d];
            p->staged_frames += s->deck[d].frames;
            p->underrun_frames += frames - s->deck[d].frames;
            if (s->deck[d].source_valid) {
                p->source_after_staged = s->deck[d].source_after; p->source_valid |= 1;
                p->source_staged_output_frames=p->staged_frames-s->deck[d].frames+s->deck[d].source_frames;
                p->source_staged_wire_frame=s->deck[d].source_wire_frame;
            }
        }
    }
    return 1;
}
static struct slot *known(xz_usb_transport *t, void *urb, int *input) {
    for (unsigned i = 0; i < DEPTH; ++i) {
        if (urb == &t->input[i].u) { *input = 1; return &t->input[i]; }
        if (urb == &t->output[i].u) { *input = 0; return &t->output[i]; }
    }
    return NULL;
}
static void cleanup_error(xz_usb_transport *t) {
    if (!load32(&t->cleanup_errno)) store32(&t->cleanup_errno, (unsigned)(errno ? errno : EIO));
}
static void restore(xz_usb_transport *t) {
    if (t->fd < 0) return;
    for (unsigned i = 0; i < DEPTH; ++i) {
        if (t->input[i].busy && t->io->request(t->fd, DISCARD, &t->input[i].u)) cleanup_error(t);
        if (t->output[i].busy && t->io->request(t->fd, DISCARD, &t->output[i].u)) cleanup_error(t);
    }
    uint64_t until = t->io->now() + 500000;
    while (t->io->now() < until) {
        unsigned pending = 0;
        for (unsigned i = 0; i < DEPTH; ++i) pending += t->input[i].busy + t->output[i].busy;
        if (!pending) break;
        void *completed = NULL;
        if (!t->io->request(t->fd, REAP, &completed)) {
            int input; struct slot *s = known(t, completed, &input);
            if (s) s->busy = 0;
        } else if (errno == EAGAIN) t->io->wait(t->fd, 5);
        else if (errno != EINTR) { cleanup_error(t); break; }
    }
    if (t->alternate) {
        struct interface setting = {0, 0};
        if (t->io->request(t->fd, SET_INTERFACE, &setting)) cleanup_error(t);
    }
    if (t->claimed) { unsigned interface = 0; if (t->io->request(t->fd, RELEASE, &interface)) cleanup_error(t); }
    if (t->io->close_fd(t->fd)) cleanup_error(t);
    t->fd = -1;
}
static int service(xz_usb_transport *t) {
    uint64_t tick = t->io->now();
    if (tick - t->last_input > 100000 || tick - t->last_output > 100000) {
        fail(t, XZ_USB_TIMEOUT, ETIMEDOUT); return 0;
    }
    void *completed = NULL;
    if (t->io->request(t->fd, REAP, &completed)) {
        if (errno != EAGAIN && errno != EINTR) { fail(t, XZ_USB_IO, errno); return 0; }
        if (t->io->wait(t->fd, 5) < 0 && errno != EINTR) { fail(t, XZ_USB_IO, errno); return 0; }
        return 1;
    }
    int input; struct slot *s = known(t, completed, &input);
    if (!s || !s->busy) { fail(t, XZ_USB_IO, EPROTO); return 0; }
    s->busy = 0;
    if (s->u.status || s->u.error_count) { fail(t, XZ_USB_IO, EIO); return 0; }
    if (t->last_completion && tick - t->last_completion > t->live.maximum_completion_gap_us)
        t->live.maximum_completion_gap_us = tick - t->last_completion;
    t->last_completion = tick;
    if (input) {
        unsigned counts[PACKETS];
        for (unsigned i = 0; i < PACKETS; ++i) {
            unsigned bytes = s->iso[i].actual_length;
            if (s->iso[i].status || bytes % IN_BYTES || bytes / IN_BYTES < 8 || bytes / IN_BYTES > 16) {
                fail(t, XZ_USB_IO, EPROTO); return 0;
            }
            counts[i] = bytes / IN_BYTES; t->live.input_frames += counts[i];
        }
        struct slot *next = &t->output[((unsigned)(s - t->input) + 1) % DEPTH];
        if (next->clock_generation != next->used_generation) ++t->live.feedback_replaced;
        memcpy(next->clock, counts, sizeof(counts)); ++next->clock_generation;
        ++t->live.input_urbs; t->last_input = tick;
        if (t->live.input_urbs >= 8) {
            unsigned starting = XZ_USB_STARTING;
            atomic_compare_exchange_strong_explicit(&t->state, &starting, XZ_USB_RUNNING,
                                                    memory_order_release, memory_order_relaxed);
        }
    } else {
        /* A playhead is a completed prefix, never a count across a missing URB. */
        if (s->sequence != t->completed_sequence) { fail(t, XZ_USB_IO, EPROTO); return 0; }
        ++t->completed_sequence;
        for (unsigned i = 0; i < PACKETS; ++i) if (s->iso[i].status) { fail(t, XZ_USB_IO, EIO); return 0; }
        t->live.wire_completed_frames += (unsigned)s->u.buffer_length / OUT_BYTES;
        for (unsigned d = 0; d < 2; ++d) {
            xz_usb_deck_position *p = &t->live.deck[d];
            if (s->deck[d].generation != p->applied_generation) continue;
            p->completed_frames += s->deck[d].frames;
            if (s->deck[d].source_valid) {
                p->source_after_completed = s->deck[d].source_after; p->source_valid |= 2;
                p->source_completed_output_frames=p->completed_frames-s->deck[d].frames+s->deck[d].source_frames;
                p->source_completed_wire_frame=s->deck[d].source_wire_frame;
            }
        }
        ++t->live.output_urbs; t->last_output = tick;
    }
    if (load32(&t->stop)) return 1;
    prepare(t, s, input, load32(&t->state) == XZ_USB_RUNNING);
    if (!submit_slot(t, s, input)) { fail(t, XZ_USB_IO, errno); return 0; }
    if (tick - t->last_publish >= 10000) publish(t, 0);
    return 1;
}
static void *worker(void *arg) {
    xz_usb_transport *t = arg;
    while (!load32(&t->stop) && service(t)) {}
    restore(t);
    publish(t, 1);
    if (load32(&t->state) != XZ_USB_FAULTED) store32(&t->state, XZ_USB_CLOSED);
    return NULL;
}
static xz_usb_transport *allocate_transport(const struct io_ops *io) {
    xz_usb_transport *t = calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->io = io; t->fd = -1;
    if (pthread_mutex_init(&t->snapshot_lock, NULL)) { free(t); return NULL; }
    atomic_init(&t->state, XZ_USB_STARTING); atomic_init(&t->stop, 0); atomic_init(&t->fault, XZ_USB_OK);
    atomic_init(&t->system_errno, 0); atomic_init(&t->cleanup_errno, 0);
    for (unsigned d = 0; d < 2; ++d) {
        atomic_init(&t->queue[d].write, 0); atomic_init(&t->queue[d].read, 0);
        atomic_init(&t->queue[d].generation, XZ_USB_INITIAL_GENERATION); atomic_init(&t->queue[d].producer, 0);
        t->live.deck[d].applied_generation = XZ_USB_INITIAL_GENERATION;
    }
    t->published = t->live;
    return t;
}
static xz_usb_result setup(xz_usb_transport *t, const char *path) {
    t->fd = t->io->open_node(path);
    if (t->fd < 0) return errno == EINVAL ? XZ_USB_INVALID : XZ_USB_IO;
    uint8_t descriptor[18], alternate = 255; unsigned interface = 0;
    if (control(t, 0x80, 6, 0x0100, 0, descriptor, 18) != 18 || descriptor[0] != 18 ||
        descriptor[8] != 0x73 || descriptor[9] != 0x2b || descriptor[10] != 0x2d || descriptor[11] != 0) return XZ_USB_WRONG_DEVICE;
    if (t->io->request(t->fd, CLAIM, &interface)) return errno == EBUSY ? XZ_USB_BUSY : XZ_USB_IO;
    t->claimed = 1;
    if (control(t, 0x81, 10, 0, 0, &alternate, 1) != 1) return XZ_USB_IO;
    if (alternate != 0) return XZ_USB_BUSY;
    struct interface setting = {0, 1};
    t->alternate = 1;
    if (t->io->request(t->fd, SET_INTERFACE, &setting)) return XZ_USB_IO;
    uint8_t rate[] = {0x44, 0xac, 0};
    if (control(t, 0x22, 1, 0x0100, 0x0082, rate, 3) != 3) return XZ_USB_IO;
    for (unsigned i = 0; i < DEPTH; ++i) {
        prepare(t, &t->input[i], 1, 0); prepare(t, &t->output[i], 0, 0);
        if (!submit_slot(t, &t->input[i], 1) || !submit_slot(t, &t->output[i], 0)) return XZ_USB_IO;
    }
    t->last_input = t->last_output = t->last_publish = t->io->now();
    return XZ_USB_OK;
}
static xz_usb_result open_with_io(const char *path, xz_usb_transport **out, const struct io_ops *io) {
    if (!out || !path) return XZ_USB_INVALID;
    *out = NULL;
    xz_usb_transport *t = allocate_transport(io);
    if (!t) return XZ_USB_IO;
    xz_usb_result result = setup(t, path);
    if (result == XZ_USB_OK) {
        int error = pthread_create(&t->worker, NULL, worker, t);
        if (!error) { t->threaded = 1; *out = t; return XZ_USB_OK; }
        result = XZ_USB_IO;
    }
    restore(t); pthread_mutex_destroy(&t->snapshot_lock); free(t);
    return result;
}
xz_usb_result xz_usb_transport_open(const char *path, xz_usb_transport **out) { return open_with_io(path, out, &real_io); }
xz_usb_result xz_usb_transport_close(xz_usb_transport **io) {
    if (!io) return XZ_USB_INVALID;
    xz_usb_transport *t = *io;
    if (!t) return XZ_USB_OK;
    store32(&t->stop, 1);
    unsigned state = load32(&t->state);
    while ((state == XZ_USB_STARTING || state == XZ_USB_RUNNING) &&
           !atomic_compare_exchange_weak_explicit(&t->state, &state, XZ_USB_CLOSING,
                                                 memory_order_acq_rel, memory_order_acquire)) {}
    if (t->threaded) {
        uint64_t until = monotonic_us() + 2000000;
        int error;
        while ((error = pthread_tryjoin_np(t->worker, NULL)) == EBUSY && monotonic_us() < until) {
            struct timespec delay = {0, 1000000}; nanosleep(&delay, NULL);
        }
        if (error) { store32(&t->system_errno, (unsigned)error); return XZ_USB_TIMEOUT; }
    } else restore(t);
    xz_usb_result result = load32(&t->cleanup_errno) ? XZ_USB_IO : XZ_USB_OK;
    pthread_mutex_destroy(&t->snapshot_lock); free(t); *io = NULL;
    return result;
}
