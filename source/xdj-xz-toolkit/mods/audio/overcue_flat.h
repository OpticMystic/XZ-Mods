/* SPDX-License-Identifier: MIT */
#ifndef XZ_OVERCUE_FLAT_H
#define XZ_OVERCUE_FLAT_H
/* Segmented stem store ("flat stems").
 *
 * The windowed stream decodes two seconds of the selected singles on demand, so every beat jump, loop exit,
 * seek and stem change waits for a fresh decode (13 ms a page on the XZ, six to seven pages a single). This
 * store decodes the singles once, in the background, into 16-bit PCM at the deck's own reader rate with the
 * measured alignment already applied. The audio thread then indexes it by deck frame: no decode, no window and
 * no wait wherever a chunk is built.
 *
 * A track is cut into segments of 32 chunks. The memory behind a segment is one extent of the shared stem pool
 * (stem_pool.h); the pool's arbiter says how many extents this store may hold. A store granted the whole track
 * holds every segment; a shorter grant holds a bubble around the playhead and the anchors (track start, loop),
 * and slides it as the playhead moves. Inside an extent, chunk k of the segment keeps single r at byte
 * (3k + r) * 65536.
 *
 * Reader protocol. One writer (the stream worker), any number of readers. A segment can lose its extent, so a
 * ready flag is cleared again, and a reader must hold the store's busy count for as long as it reads samples:
 *   reader: busy += 1; per chunk: base = load(seg[c / 32]), covered only if base and ready[c]; read from the
 *           loaded bases only; busy -= 1 after the last sample.
 *   worker giving up segment s: store(seg[s], NULL); wait until busy is seen at 0; clear the segment's ready
 *           flags; only then reuse or give back the extent. A reader that loaded the base before the store is
 *           counted in busy; one that starts after it sees NULL.
 *   worker taking segment s: its flags are clear; store(seg[s], extent); per chunk write the samples, then set
 *           ready[c] with release.
 * Compiled into overcue_stream.c only: that object is built with the unit's floating-point unit enabled. */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "stem_pool.h"
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

#define XZ_OC_FLAT_CHUNK 16384u
#define XZ_OC_FLAT_SEGMENT 32u                                   /* chunks per segment */
#define XZ_OC_FLAT_SEGMENT_FRAMES (XZ_OC_FLAT_CHUNK * XZ_OC_FLAT_SEGMENT)
#define XZ_OC_FLAT_SINGLE (XZ_OC_FLAT_CHUNK * 2u)                /* samples of one single in one chunk */
#define XZ_OC_FLAT_ZEROS 16
#define XZ_OC_FLAT_MAX_PHASES 1024u
#define XZ_OC_FLAT_MAX_TAPS 96u
#define XZ_OC_FLAT_MAX_SINGLES 3u
#ifndef XZ_OC_FLAT_FLOOR /* a test may lower it to see a bubble with a short fixture */
#define XZ_OC_FLAT_FLOOR 8u        /* extents a stream asks for at least: 95 s at 44.1 kHz */
#endif
#define XZ_OC_FLAT_ANCHORS 12u
#define XZ_OC_FLAT_ANCHOR_TAIL_S 3u /* an anchor this close to its segment's end also wants the next segment */
/* Readers still inside after this long: the segment stays held and is tried again on a later pass. */
#define XZ_OC_FLAT_QUIET_MS 200u
/* A grant below this is no store at all, unless it holds the whole track: one segment cannot hold both sides of a
 * playhead near an edge, and the stream would flap between store and windows. */
#define XZ_OC_FLAT_MIN_GRANT 2u

/* Fills stem frames [first, first + frames) of one single; frames outside the file are zero. 0 on success. */
typedef int (*xz_oc_flat_reader)(void *context, unsigned single, int64_t first, size_t frames, int16_t *stereo);

struct xz_oc_flat {
    uint32_t deck_rate, stem_rate, phases, taps;
    uint64_t frames;            /* deck frames held for every single */
    unsigned singles;
    int direct;                 /* same rate and a whole-frame shift: pages are copied, bit for bit */
    int64_t direct_shift;
    float *kernel;              /* phases x taps x 2: each coefficient twice, for the left and right sample */
    int32_t *phase_base, base_low, base_high;
    uint32_t chunks, segments, built; /* built: ready chunks now */
    int16_t **seg;              /* [segments], atomic: the extent behind each segment, or NULL */
    uint8_t *ready;             /* [chunks], atomic: set after the samples are written, cleared when the segment goes */
    uint16_t held;              /* segments with an extent; worker writes, any thread reads */
    int holder;                 /* pool holder */
    int busy;                   /* readers inside the store (reader protocol) */
    uint32_t drops;             /* segments given up so far (diagnostic) */
    uint32_t *rank, *place;     /* worker scratch: segments most wanted first, and each segment's place in that list */
    int16_t *scratch;
    size_t scratch_frames;
    xz_oc_flat_reader read;
    void *context;
};

static inline double xz_oc_flat_bessel(double x) {
    double sum = 1, term = 1;
    for (int k = 1; k < 64; k++) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < sum * 1e-14) break;
    }
    return sum;
}

static inline const int16_t *xz_oc_flat_segment(const struct xz_oc_flat *f, uint32_t s) {
    return __atomic_load_n(&f->seg[s], __ATOMIC_SEQ_CST);
}

/* Readers wait for nothing: a writer that wants an extent back waits for them (xz_oc_flat_quiet). */
static inline void xz_oc_flat_enter(struct xz_oc_flat *f) { __atomic_add_fetch(&f->busy, 1, __ATOMIC_SEQ_CST); }
static inline void xz_oc_flat_leave(struct xz_oc_flat *f) { __atomic_sub_fetch(&f->busy, 1, __ATOMIC_SEQ_CST); }

/* Worker: has every reader that could hold a base loaded before the caller cleared a pointer left? */
static inline int xz_oc_flat_quiet(struct xz_oc_flat *f) {
    for (unsigned waited = 0;; waited++) {
        if (!__atomic_load_n(&f->busy, __ATOMIC_SEQ_CST)) return 1;
        if (waited >= XZ_OC_FLAT_QUIET_MS) return 0;
        struct timespec moment = {0, 1000000};
        nanosleep(&moment, NULL);
    }
}

/* Worker: segment s loses its extent, which goes back to the pool. 0 when readers stayed inside too long. */
static inline int xz_oc_flat_drop(struct xz_oc_flat *f, uint32_t s) {
    int16_t *extent = f->seg[s];
    if (!extent) return 1;
    __atomic_store_n(&f->seg[s], NULL, __ATOMIC_SEQ_CST);
    if (!xz_oc_flat_quiet(f)) { __atomic_store_n(&f->seg[s], extent, __ATOMIC_SEQ_CST); return 0; }
    uint32_t first = s * XZ_OC_FLAT_SEGMENT, end = first + XZ_OC_FLAT_SEGMENT < f->chunks ? first + XZ_OC_FLAT_SEGMENT : f->chunks, cleared = 0;
    for (uint32_t c = first; c < end; c++)
        if (f->ready[c]) { __atomic_store_n(&f->ready[c], 0, __ATOMIC_RELAXED); cleared++; }
    __atomic_sub_fetch(&f->built, cleared, __ATOMIC_RELEASE);
    __atomic_sub_fetch(&f->held, 1, __ATOMIC_RELEASE);
    __atomic_add_fetch(&f->drops, 1, __ATOMIC_RELAXED);
    xz_stem_pool_give(f->holder, extent);
    return 1;
}

static inline void xz_oc_flat_destroy(struct xz_oc_flat *f) {
    if (!f) return;
    if (f->holder >= 0) {
        struct xz_stem_want none = {XZ_STEM_IDLE, 0, 0, 0};
        xz_stem_pool_want(f->holder, &none);
        if (f->seg) for (uint32_t s = 0; s < f->segments; s++) xz_oc_flat_drop(f, s);
        xz_stem_pool_leave(f->holder);
    }
    free(f->kernel); free(f->phase_base); free(f->seg); free(f->ready); free(f->rank); free(f->place); free(f->scratch); free(f);
}

/* Deck frames a track of this length needs, 0 when it cannot be described. */
static inline uint64_t xz_oc_flat_span(uint32_t deck_rate, uint32_t stem_rate, uint64_t stem_frames, double offset) {
    if (!deck_rate || !stem_rate || !stem_frames || !isfinite(offset)) return 0;
    double last = (double)stem_frames * deck_rate / stem_rate - offset;
    return last < 1 || last > 2147483647.0 ? 0 : (uint64_t)ceil(last);
}

/* Extents the whole track needs, 0 when it cannot be described. */
static inline uint32_t xz_oc_flat_need(uint32_t deck_rate, uint32_t stem_rate, uint64_t stem_frames, double offset) {
    uint64_t frames = xz_oc_flat_span(deck_rate, stem_rate, stem_frames, offset);
    return (uint32_t)((frames + XZ_OC_FLAT_SEGMENT_FRAMES - 1) / XZ_OC_FLAT_SEGMENT_FRAMES);
}

/* NULL when the track does not qualify or the pool has no holder free: the caller keeps the windowed stream.
 * Holds no audio memory yet. Deck frame i holds the stem at stem time (i + offset) * stem_rate / deck_rate. */
static inline struct xz_oc_flat *xz_oc_flat_create(uint32_t deck_rate, uint32_t stem_rate, uint64_t stem_frames, double offset,
                                            unsigned singles, xz_oc_flat_reader read, void *context) {
    uint64_t frames = xz_oc_flat_span(deck_rate, stem_rate, stem_frames, offset);
    if (!frames || !read || !singles || singles > XZ_OC_FLAT_MAX_SINGLES) return NULL;
    struct xz_oc_flat *f = calloc(1, sizeof(*f));
    if (!f) return NULL;
    f->holder = -1;
    f->deck_rate = deck_rate; f->stem_rate = stem_rate; f->singles = singles; f->read = read; f->context = context;
    f->frames = frames;
    f->chunks = (uint32_t)((f->frames + XZ_OC_FLAT_CHUNK - 1) / XZ_OC_FLAT_CHUNK);
    f->segments = (f->chunks + XZ_OC_FLAT_SEGMENT - 1) / XZ_OC_FLAT_SEGMENT;
    double ratio = (double)stem_rate / deck_rate, shift = offset * ratio, whole = floor(shift + 0.5);
    f->direct = stem_rate == deck_rate && fabs(shift - whole) < 0.002;
    f->direct_shift = (int64_t)whole;
    if (!f->direct) {
        uint32_t a = deck_rate, b = stem_rate;
        while (b) { uint32_t t = a % b; a = b; b = t; }
        f->phases = deck_rate / a;
        /* Cut-off in cycles per stem frame: the lower Nyquist limit, with room for the transition band
           when the stems run faster than the deck. */
        double cutoff = stem_rate > deck_rate ? 0.5 * deck_rate / stem_rate * 0.92 : 0.5;
        double half = XZ_OC_FLAT_ZEROS / (2.0 * cutoff), beta = 8.6, bottom = xz_oc_flat_bessel(beta);
        f->taps = 2u * (uint32_t)ceil(half);
        if (f->phases > XZ_OC_FLAT_MAX_PHASES || f->taps > XZ_OC_FLAT_MAX_TAPS) { xz_oc_flat_destroy(f); return NULL; }
        f->kernel = malloc((size_t)f->phases * f->taps * 2 * sizeof(float));
        f->phase_base = malloc((size_t)f->phases * sizeof(int32_t));
        if (!f->kernel || !f->phase_base) { xz_oc_flat_destroy(f); return NULL; }
        for (uint32_t j = 0; j < f->phases; j++) {
            double point = (double)(((uint64_t)j * stem_rate) % deck_rate) / deck_rate + shift, base = floor(point);
            double fraction = point - base, sum = 0, row[XZ_OC_FLAT_MAX_TAPS];
            f->phase_base[j] = (int32_t)base;
            if (!j || f->phase_base[j] < f->base_low) f->base_low = f->phase_base[j];
            if (!j || f->phase_base[j] > f->base_high) f->base_high = f->phase_base[j];
            for (uint32_t k = 0; k < f->taps; k++) {
                double x = (double)((int)k - (int)(f->taps / 2) + 1) - fraction, edge = x / half;
                double window = fabs(edge) < 1 ? xz_oc_flat_bessel(beta * sqrt(1 - edge * edge)) / bottom : 0;
                double angle = 3.14159265358979323846 * 2.0 * cutoff * x;
                row[k] = (fabs(angle) < 1e-9 ? 1.0 : sin(angle) / angle) * window;
                sum += row[k];
            }
            for (uint32_t k = 0; k < f->taps; k++)
                f->kernel[((size_t)j * f->taps + k) * 2] = f->kernel[((size_t)j * f->taps + k) * 2 + 1] = (float)(row[k] / sum);
        }
    }
    f->scratch_frames = (size_t)ceil(XZ_OC_FLAT_CHUNK * ratio) + f->taps + 16;
    f->scratch = malloc(f->scratch_frames * 2 * sizeof(int16_t));
    f->ready = calloc(f->chunks, 1);
    f->seg = calloc(f->segments, sizeof(*f->seg));
    f->rank = malloc(f->segments * sizeof(*f->rank));
    f->place = malloc(f->segments * sizeof(*f->place));
    if (!f->scratch || !f->ready || !f->seg || !f->rank || !f->place || f->segments > UINT16_MAX) { xz_oc_flat_destroy(f); return NULL; }
    f->holder = xz_stem_pool_join();
    if (f->holder < 0) { xz_oc_flat_destroy(f); return NULL; }
    return f;
}

/* Worker: what this store asks of the pool. need is the whole track; order is when the class last rose. */
static inline void xz_oc_flat_want(struct xz_oc_flat *f, uint8_t klass, uint32_t order) {
    struct xz_stem_want want = {klass, (uint16_t)f->segments, XZ_OC_FLAT_FLOOR, order};
    xz_stem_pool_want(f->holder, &want);
}

static inline int16_t xz_oc_flat_round(float value) {
    return value >= 32767.f ? 32767 : value <= -32768.f ? -32768 : (int16_t)(int32_t)(value + (value >= 0 ? 0.5f : -0.5f));
}

/* Worker: writes chunk `chunk` into its segment's extent `extent`, then marks it ready. */
static inline int xz_oc_flat_build(struct xz_oc_flat *f, uint32_t chunk, int16_t *extent) {
    uint64_t begin = (uint64_t)chunk * XZ_OC_FLAT_CHUNK, end = begin + XZ_OC_FLAT_CHUNK;
    if (end > f->frames) end = f->frames;
    int16_t *base = extent + (size_t)(chunk % XZ_OC_FLAT_SEGMENT) * XZ_OC_FLAT_MAX_SINGLES * XZ_OC_FLAT_SINGLE;
    for (unsigned r = 0; r < f->singles; r++) {
        int16_t *out = base + (size_t)r * XZ_OC_FLAT_SINGLE;
        if (f->direct) {
            if (f->read(f->context, r, (int64_t)begin + f->direct_shift, (size_t)(end - begin), out)) return -1;
            continue;
        }
        int64_t low = (int64_t)(begin * f->stem_rate / f->deck_rate) + f->base_low - (int64_t)(f->taps / 2) + 1;
        int64_t high = (int64_t)((end - 1) * f->stem_rate / f->deck_rate) + f->base_high + (int64_t)(f->taps / 2) + 1;
        if (high - low > (int64_t)f->scratch_frames) return -1;
        if (f->read(f->context, r, low, (size_t)(high - low), f->scratch)) return -1;
        /* Stem frame of deck frame i is floor(i * stem_rate / deck_rate): carried by addition, exactly. */
        uint32_t phase = (uint32_t)(begin % f->phases), rest = (uint32_t)(begin * f->stem_rate % f->deck_rate);
        uint32_t step = f->stem_rate / f->deck_rate, step_rest = f->stem_rate % f->deck_rate;
        int32_t whole = (int32_t)((int64_t)(begin * f->stem_rate / f->deck_rate) - (int64_t)(f->taps / 2) + 1 - low);
        for (uint64_t i = begin; i < end; i++) {
            const int16_t *in = f->scratch + 2 * (whole + f->phase_base[phase]);
            const float *k = f->kernel + (size_t)phase * f->taps * 2;
            float left, right;
#if defined(__ARM_NEON)
            float32x4_t sum = vdupq_n_f32(0);
            for (uint32_t t = 0; t < f->taps * 2; t += 4)
                sum = vmlaq_f32(sum, vcvtq_f32_s32(vmovl_s16(vld1_s16(in + t))), vld1q_f32(k + t));
            left = vgetq_lane_f32(sum, 0) + vgetq_lane_f32(sum, 2); right = vgetq_lane_f32(sum, 1) + vgetq_lane_f32(sum, 3);
#else
            float l0 = 0, r0 = 0, l1 = 0, r1 = 0;
            for (uint32_t t = 0; t < f->taps * 2; t += 4) {
                l0 += k[t] * in[t]; r0 += k[t + 1] * in[t + 1]; l1 += k[t + 2] * in[t + 2]; r1 += k[t + 3] * in[t + 3];
            }
            left = l0 + l1; right = r0 + r1;
#endif
            out[(i - begin) * 2] = xz_oc_flat_round(left); out[(i - begin) * 2 + 1] = xz_oc_flat_round(right);
            whole += (int32_t)step; rest += step_rest;
            if (rest >= f->deck_rate) { rest -= f->deck_rate; whole++; }
            if (++phase == f->phases) phase = 0;
        }
    }
    __atomic_store_n(&f->ready[chunk], 1, __ATOMIC_RELEASE);
    __atomic_fetch_add(&f->built, 1, __ATOMIC_RELEASE);
    return 0;
}

/* Every chunk of the track is held and built. */
static inline int xz_oc_flat_complete(const struct xz_oc_flat *f) {
    return __atomic_load_n(&f->built, __ATOMIC_ACQUIRE) >= f->chunks;
}

static inline uint32_t xz_oc_flat_segment_of(const struct xz_oc_flat *f, int64_t frame) {
    if (frame <= 0) return 0;
    uint64_t s = (uint64_t)frame / XZ_OC_FLAT_SEGMENT_FRAMES;
    return s >= f->segments ? f->segments - 1 : (uint32_t)s;
}

/* Which segments to hold, most wanted first (pure; fills rank and place, each `segments` long):
 *  1. the playhead's segment and the next one in the playing direction;
 *  2. each anchor's segment, and the one after it when the anchor lies in the last 3 s of its segment;
 *  3. the rest by cost: d segments ahead cost d, d behind cost 3d (about three quarters of a bubble lies ahead).
 * A grant of g extents wants the first g. With the whole track granted this is only the build order. */
static inline void xz_oc_flat_rank(const struct xz_oc_flat *f, int64_t near, int reverse, const int32_t *anchors, unsigned count,
                            uint32_t *rank, uint32_t *place) {
    uint32_t n = 0, segments = f->segments, p = xz_oc_flat_segment_of(f, near);
    for (uint32_t s = 0; s < segments; s++) place[s] = UINT32_MAX;
#define XZ_OC_FLAT_RANK(s) do { uint32_t at_ = (s); if (at_ < segments && place[at_] == UINT32_MAX) { place[at_] = n; rank[n++] = at_; } } while (0)
    XZ_OC_FLAT_RANK(p);
    if (reverse) { if (p) XZ_OC_FLAT_RANK(p - 1); } else XZ_OC_FLAT_RANK(p + 1);
    uint64_t tail = (uint64_t)XZ_OC_FLAT_ANCHOR_TAIL_S * f->deck_rate;
    for (unsigned i = 0; i < count && i < XZ_OC_FLAT_ANCHORS; i++) {
        if (anchors[i] < 0 || (uint64_t)anchors[i] >= f->frames) continue;
        uint32_t s = (uint32_t)((uint64_t)anchors[i] / XZ_OC_FLAT_SEGMENT_FRAMES);
        XZ_OC_FLAT_RANK(s);
        if ((uint64_t)anchors[i] % XZ_OC_FLAT_SEGMENT_FRAMES + tail >= XZ_OC_FLAT_SEGMENT_FRAMES) XZ_OC_FLAT_RANK(s + 1);
    }
    for (uint32_t forward = 1, backward = 1; n < segments;) {
        /* Equal costs go ahead first. */
        int ahead = (uint64_t)forward <= 3u * (uint64_t)backward;
        uint32_t d = ahead ? forward++ : backward++;
        int up = ahead != !!reverse;
        if (up) { if (p + d < segments) XZ_OC_FLAT_RANK(p + d); }
        else if (d <= p) XZ_OC_FLAT_RANK(p - d);
        if (forward > segments && backward > segments) break;
    }
#undef XZ_OC_FLAT_RANK
}

/* Worker: move the held segments toward the first `grant` places of the ranking (rank and place as filled by
 * xz_oc_flat_rank). Gives up at most one segment and takes at most one extent per call. The margin of one place on
 * each side keeps a playhead rocking across a segment edge from swapping segments back and forth. The playhead's
 * own segment needs no margin: held segments in places 1 to grant would otherwise keep it out for good (grant 2
 * holding the next segment and the track start after a jump back across an edge).
 * keep_near: do not give up the playhead's segment or the next one (places 0 and 1). A grant of 2 or more never
 * asks for that; a smaller one takes the whole store, and the stream sets keep_near until its windows hold the
 * playhead, so the deck plays on from the windows instead of starving. */
static inline void xz_oc_flat_slide(struct xz_oc_flat *f, unsigned grant, int keep_near) {
    uint32_t held = __atomic_load_n(&f->held, __ATOMIC_RELAXED), worst = UINT32_MAX, best_missing = UINT32_MAX;
    if (grant < XZ_OC_FLAT_MIN_GRANT && grant < f->segments) grant = 0;
    for (uint32_t s = 0; s < f->segments; s++) {
        if (f->seg[s]) { if (worst == UINT32_MAX || f->place[s] > f->place[worst]) worst = s; }
        else if (best_missing == UINT32_MAX || f->place[s] < f->place[best_missing]) best_missing = s;
    }
    if (held > grant) { if (!(keep_near && f->place[worst] < 2u) && xz_oc_flat_drop(f, worst)) held--; }
    else if (best_missing != UINT32_MAX && f->place[best_missing] + 2 <= grant && worst != UINT32_MAX &&
             f->place[worst] >= grant + (f->place[best_missing] ? 1u : 0u)) {
        if (xz_oc_flat_drop(f, worst)) held--;
    }
    if (held < grant && best_missing != UINT32_MAX) {
        int16_t *extent = xz_stem_pool_take(f->holder);
        if (extent) {
            __atomic_store_n(&f->seg[best_missing], extent, __ATOMIC_SEQ_CST);
            __atomic_add_fetch(&f->held, 1, __ATOMIC_RELEASE);
        }
    }
}

/* Worker: builds one missing chunk: the best-ranked held segment first, inside it the chunk nearest the playhead
 * (at the playhead, then ahead in the playing direction, then behind). 1 built one, 0 nothing to build, -1 the
 * reader failed. */
static inline int xz_oc_flat_build_next(struct xz_oc_flat *f, int64_t near, int reverse) {
    uint32_t pc = near <= 0 ? 0 : (uint64_t)near / XZ_OC_FLAT_CHUNK >= f->chunks ? f->chunks - 1 : (uint32_t)((uint64_t)near / XZ_OC_FLAT_CHUNK);
    for (uint32_t n = 0; n < f->segments; n++) {
        uint32_t s = f->rank[n];
        int16_t *extent = f->seg[s];
        if (!extent) continue;
        uint32_t first = s * XZ_OC_FLAT_SEGMENT, end = first + XZ_OC_FLAT_SEGMENT < f->chunks ? first + XZ_OC_FLAT_SEGMENT : f->chunks;
        uint32_t start = pc < first ? first : pc >= end ? end - 1 : pc;
        if (!f->ready[start]) return xz_oc_flat_build(f, start, extent) ? -1 : 1;
        if (reverse) for (uint32_t c = start; c-- > first;) if (!f->ready[c]) return xz_oc_flat_build(f, c, extent) ? -1 : 1;
        for (uint32_t c = start; c < end; c++) if (!f->ready[c]) return xz_oc_flat_build(f, c, extent) ? -1 : 1;
        for (uint32_t c = start; c-- > first;) if (!f->ready[c]) return xz_oc_flat_build(f, c, extent) ? -1 : 1;
    }
    return 0;
}

/* Worker, each pass: rank, slide toward the pool's grant, build one chunk. As xz_oc_flat_build_next. keep_near as
 * xz_oc_flat_slide. */
static inline int xz_oc_flat_pass_keep(struct xz_oc_flat *f, int64_t near, int reverse, const int32_t *anchors, unsigned count,
                                       int keep_near) {
    xz_oc_flat_rank(f, near, reverse, anchors, count, f->rank, f->place);
    xz_oc_flat_slide(f, xz_stem_pool_grant(f->holder), keep_near);
    return xz_oc_flat_build_next(f, near, reverse);
}
static inline int xz_oc_flat_pass(struct xz_oc_flat *f, int64_t near, int reverse, const int32_t *anchors, unsigned count) {
    return xz_oc_flat_pass_keep(f, near, reverse, anchors, count, 0);
}

/* Reader, inside xz_oc_flat_enter/leave: the base of chunk c (single r at + r * XZ_OC_FLAT_SINGLE), or NULL when
 * it is not held or not built. Read samples only through a base this returned during the same visit. */
static inline const int16_t *xz_oc_flat_chunk(const struct xz_oc_flat *f, uint64_t c) {
    const int16_t *extent = xz_oc_flat_segment(f, (uint32_t)(c / XZ_OC_FLAT_SEGMENT));
    if (!extent || !__atomic_load_n(&f->ready[c], __ATOMIC_ACQUIRE)) return NULL;
    return extent + (size_t)(c % XZ_OC_FLAT_SEGMENT) * XZ_OC_FLAT_MAX_SINGLES * XZ_OC_FLAT_SINGLE;
}

/* Most chunks a block of up to 65536 frames touches. */
#define XZ_OC_FLAT_BLOCK_CHUNKS (65536u / XZ_OC_FLAT_CHUNK + 1u)
/* Reader, inside enter/leave: every deck frame of [first, first + frames) is built; fills base[] with the bases of
 * the chunks it touches (frames past the end read as silence and need none). */
static inline int xz_oc_flat_bases(const struct xz_oc_flat *f, int64_t first, size_t frames, const int16_t **base) {
    if (first < 0 || !frames || frames > 65536) return 0;
    if ((uint64_t)first >= f->frames) return 1;
    uint64_t last = (uint64_t)first + frames - 1;
    if (last >= f->frames) last = f->frames - 1;
    for (uint64_t c = (uint64_t)first / XZ_OC_FLAT_CHUNK, i = 0; c <= last / XZ_OC_FLAT_CHUNK; c++, i++)
        if (!(base[i] = xz_oc_flat_chunk(f, c))) return 0;
    return 1;
}

/* Reader or worker: every deck frame of [first, first + frames) is built. Frames past the end read as silence.
 * A worker may ask it without entering: it only decides, it reads no samples. */
static inline int xz_oc_flat_covers(const struct xz_oc_flat *f, int64_t first, size_t frames) {
    if (first < 0 || !frames) return 0;
    if ((uint64_t)first >= f->frames) return 1;
    uint64_t last = (uint64_t)first + frames - 1;
    if (last >= f->frames) last = f->frames - 1;
    for (uint64_t c = (uint64_t)first / XZ_OC_FLAT_CHUNK; c <= last / XZ_OC_FLAT_CHUNK; c++)
        if (!xz_oc_flat_chunk(f, c)) return 0;
    return 1;
}
/* `span` frames from `at` in the playing direction are built (reverse: the span ending at `at`). */
static inline int xz_oc_flat_covers_toward(const struct xz_oc_flat *f, int64_t at, size_t span, int reverse) {
    if (!reverse) return xz_oc_flat_covers(f, at, span);
    int64_t first = at - (int64_t)span + 1;
    if (first < 0) { span -= (size_t)-first; first = 0; }
    return at >= 0 && xz_oc_flat_covers(f, first, span);
}

/* Worker: built frames without a gap ahead of `at` in the playing direction, and behind it. */
static inline void xz_oc_flat_reach(const struct xz_oc_flat *f, int64_t at, int reverse, uint64_t *lead, uint64_t *behind) {
    *lead = *behind = 0;
    if (at < 0 || (uint64_t)at >= f->frames) return;
    uint64_t c0 = (uint64_t)at / XZ_OC_FLAT_CHUNK, up = 0, down = 0;
    if (!xz_oc_flat_chunk(f, c0)) return;
    uint64_t c = c0;
    while (c + 1 < f->chunks && xz_oc_flat_chunk(f, c + 1)) c++;
    up = (c + 1) * XZ_OC_FLAT_CHUNK; if (up > f->frames) up = f->frames;
    c = c0;
    while (c > 0 && xz_oc_flat_chunk(f, c - 1)) c--;
    down = c * XZ_OC_FLAT_CHUNK;
    uint64_t above = up - (uint64_t)at, below = (uint64_t)at - down;
    *lead = reverse ? below : above; *behind = reverse ? above : below;
}
#endif
