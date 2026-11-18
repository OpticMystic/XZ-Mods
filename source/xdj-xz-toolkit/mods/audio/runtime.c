/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include "runtime.h"
#include "../diary/diary.h"
#pragma weak xz_diary_busy_begin
#pragma weak xz_diary_busy_end
#include "overcue.h"
#include "native_reader.h"
#include "trace.h"
#include "../ram/runtime.h"
#include "../runtime.h"
#include <dirent.h>
#include <math.h>
#include <pthread.h>
#include "../xz_thread.h"
#include "../xz_mutex.h"
#include "../xz_wake.h"
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define DECKS 2
#define PCM_CAP ((size_t)128 * 1024 * 1024)
#define PCM_READER_VPTR UINT32_C(0x003b8330)

struct publication {
    struct xz_stem_decoded decoded;
    struct xz_oc_stream *prepared;
    uint32_t generation, reader, reader_impl, reader_frames;
    struct publication *next; /* worker only: on its deck's draining list */
};
struct deck_state {
    uint32_t generation;
    uint32_t known_reader;
    uint32_t output_epoch;
    uint32_t output_manager;
    uint32_t output_rate;
    uint32_t published_output_epoch;
    unsigned users;
    struct publication *published;
    uint32_t levels[4];
    uint32_t source_work;
    uint32_t mixed, skipped;
    /* Why a block after the native read was or was not mixed (see prove_source). moved and cue blocks
     * are mixed; the older check skipped them. miss, unproven and identity blocks are left as stock. */
    uint32_t moved, cue, miss, unproven, identity;
    struct xz126_deck_source pending;
    uint32_t pending_generation;
    int history_valid;
    int pending_kind; /* 1=source, 2=retire only; protected by control_mutex */
    struct xz_audio_status status;
    struct publication *draining; /* worker only: taken off the deck, a source callback may still hold it */
    int draining_said; /* worker only */
};
static struct deck_state decks[DECKS];
static _Thread_local struct xz_audio_batch *source_batch;
static pthread_mutex_t control_mutex = XZ_MUTEX_INITIALIZER;
static pthread_mutex_t lifecycle_mutex = XZ_MUTEX_INITIALIZER;
/* Wakes the stem worker (normal priority). The load and unload hooks run on the stock Player threads
 * (real-time, RR 35): never a condition variable, see xz_wake.h. */
static struct xz_wake changed;
static pthread_t worker;
static int running, enabled, worker_created, hooks_installed;
static int (*original_load)(void *, const void *);
static void (*original_unload)(void *);
static uint32_t (*original_stream)(void *, int32_t, void *, uint32_t);
static void set_state(struct deck_state *deck, uint32_t gen,
                       enum xz_audio_state state, size_t bytes);
static void request_source_work(struct deck_state *deck){
    uint32_t old=__atomic_load_n(&deck->source_work,__ATOMIC_RELAXED),next;
    do {next=(old+2u)|1u;} while(!__atomic_compare_exchange_n(&deck->source_work,&old,next,0,__ATOMIC_RELEASE,__ATOMIC_RELAXED));
}

static int read_worker(void *context, uint32_t address, void *out, size_t n)
{
    (void)context;
    return xz_read_memory(address, out, n);
}

static int read_word(uint32_t address, uint32_t *out)
{
    return xz_read_memory(address, out, sizeof(*out));
}

static int deck_for_reader(uint32_t reader, uint32_t *engine_out)
{
    uint32_t engine, first, last, player, candidate;
    unsigned i, count;
    if (read_word(XZ126_PLAY_ENGINE_CELL, &engine) || engine < 0x10000 ||
        engine > UINT32_MAX - 0x14 || read_word(engine + 0xc, &first) ||
        read_word(engine + 0x10, &last) || last < first ||
        ((last - first) & 3) || (count = (last - first) / 4) > DECKS)
        return -1;
    for (i = 0; i < count; ++i) {
        if (read_word(first + i * 4, &player) || player < 0x10000 ||
            player > UINT32_MAX - 0xcc || read_word(player + 0xc8, &candidate))
            continue;
        if (candidate == reader) { *engine_out = engine; return (int)i; }
    }
    return -1;
}

static int identify_reader(uint32_t reader, uint32_t *engine)
{
    int index = deck_for_reader(reader, engine);
    if (index >= 0) {
        __atomic_store_n(&decks[index].known_reader, reader, __ATOMIC_RELEASE);
        return index;
    }
    /* A transient guarded-read failure must still invalidate a reader already
     * mapped to a deck. Never keep an old stem generation across an unseen load. */
    for (unsigned i = 0; i < DECKS; ++i)
        if (__atomic_load_n(&decks[i].known_reader, __ATOMIC_ACQUIRE) == reader)
            return (int)i;
    return -1;
}

/* Increment-before-load plus retire-before-drain prevents freeing a publication
 * that any source callback could still be using. Only the worker frees PCM. */
static struct publication *acquire(struct deck_state *deck)
{
    struct publication *p;
    __atomic_fetch_add(&deck->users, 1, __ATOMIC_SEQ_CST);
    p = __atomic_load_n(&deck->published, __ATOMIC_SEQ_CST);
    if (!p) __atomic_fetch_sub(&deck->users, 1, __ATOMIC_SEQ_CST);
    return p;
}

static void release(struct deck_state *deck)
{
    __atomic_fetch_sub(&deck->users, 1, __ATOMIC_SEQ_CST);
}
void xz_audio_batch_begin(int index,int32_t position,struct xz_audio_batch *batch){
    memset(batch,0,sizeof(*batch));batch->previous=source_batch;source_batch=batch;
    if(index<0||index>=DECKS||!__atomic_load_n(&running,__ATOMIC_ACQUIRE)||
       !__atomic_load_n(&enabled,__ATOMIC_ACQUIRE))return;
    struct deck_state *deck=&decks[index];
    struct publication *p=acquire(deck);if(!p)return;
    uint32_t gen=__atomic_load_n(&deck->generation,__ATOMIC_SEQ_CST);
    if(p->generation!=gen){release(deck);return;}
    if(p->prepared)xz_oc_stream_position(p->prepared,position);
    batch->deck=deck;batch->publication=p;batch->generation=gen;
}
void xz_audio_batch_end(struct xz_audio_batch *batch){
    source_batch=batch->previous;
    if(batch->publication)release(batch->deck);
    batch->publication=NULL;
}

static void free_publication(struct publication *p)
{
    if (p) { xz_oc_stream_close(p->prepared); xz_stem_decoded_free(&p->decoded); free(p); }
}

/* Worker only. Frees the deck's draining publications once no source callback holds one: users counts every caller
 * between acquire and release, and nothing can load a publication after it was taken off the deck. Waits at most
 * `wait_ms`; returns 1 while some are still held. They are kept, never freed early (the audio thread would read
 * freed stem memory) and never dropped (with the stem pool that memory would be lost for good). */
static int drain(struct deck_state *deck, unsigned wait_ms)
{
    for (unsigned waited = 0; deck->draining; waited++) {
        if (!__atomic_load_n(&deck->users, __ATOMIC_SEQ_CST)) {
            struct publication *p = deck->draining;
            deck->draining = NULL;
            if (deck->draining_said) xz_log("stems: audio reader drained; old allocation freed");
            deck->draining_said = 0;
            while (p) { struct publication *next = p->next; free_publication(p); p = next; }
            return 0;
        }
        if (waited >= wait_ms) break;
        struct timespec delay = {0, 1000000};
        nanosleep(&delay, NULL);
    }
    if (deck->draining && !deck->draining_said) {
        xz_log("stems: audio reader did not drain; old allocation kept until it does");
        deck->draining_said = 1;
    }
    return deck->draining != NULL;
}

/* Worker only: takes the deck's publication off. A reader that stays longer than this short wait does not hold
 * the worker: the other deck is served meanwhile and the publication is freed on a later pass. */
static void retire(struct deck_state *deck)
{
    struct publication *old = __atomic_exchange_n(&deck->published, NULL, __ATOMIC_SEQ_CST);
    if (old) { old->next = deck->draining; deck->draining = old; }
    drain(deck, 100);
}

/* ---- USB STOP or a pulled stick (usb_release.h): the stem streams let go of it on their own workers ---- */
extern int xz_usb_release_holder(const char *,unsigned (*)(void *,int,uint32_t),void *) __attribute__((weak));
extern void xz_usb_release_answer(int,int,uint32_t) __attribute__((weak));
static int stems_holder = -1;
static const char *const stick_prefix[2] = {"/media/usb1/", "/media/usb2/"};
static uint32_t stick_stops[2]; /* atomics: stops begun per slot, for a stream that opens while one begins */
static int stick_of(const char *path)
{
    for (int slot = 1; slot <= 2; ++slot)
        if (!strncmp(path, stick_prefix[slot - 1], strlen(stick_prefix[slot - 1]))) return slot;
    return 0;
}
/* A stream's answer, on its worker (or in close). token: stop << 2 | (slot - 1) << 1 | deck; stop 0 answers nobody. */
static void stems_answer(uint32_t token, int outcome, const char *why)
{
    int deck = (int)(token & 1u), slot = (int)((token >> 1) & 1u) + 1;
    uint32_t stop = token >> 2;
    char line[288];
    snprintf(line, sizeof(line), "USB_RELEASE: stems of deck %d, USB %d: %s (%s)", deck + 1, slot,
             outcome == XZ_OC_GONE_KEPT ? "kept: the whole track is in RAM; the stem files are closed and never read again" :
             outcome == XZ_OC_GONE_ENDED ? "let go: the stems end for this track, levels return to unity; the stem files are closed" :
             outcome == XZ_OC_GONE_CLOSED ? "closed with the track" : "nothing to let go", why ? why : "");
    xz_log(line);
    if (stop && xz_usb_release_answer) xz_usb_release_answer(stems_holder, slot, stop);
}
static unsigned stems_let_go(void *unused, int slot, uint32_t stop)
{
    (void)unused;
    unsigned asked = 0;
    __atomic_add_fetch(&stick_stops[slot - 1], 1, __ATOMIC_ACQ_REL);
    pthread_mutex_lock(&control_mutex);
    for (int i = 0; i < DECKS; ++i) {
        struct publication *p = acquire(&decks[i]);
        if (!p) continue;
        if (p->prepared) {
            xz_oc_stream_source_gone(p->prepared, stick_prefix[slot - 1], stems_answer,
                                     stop << 2 | (uint32_t)(slot - 1) << 1 | (uint32_t)i);
            asked++;
        }
        release(&decks[i]);
    }
    pthread_mutex_unlock(&control_mutex);
    return asked;
}

static void store_levels(struct deck_state *deck,const float values[4]){
    for(unsigned role=0;role<4;role++){
        uint32_t bits;memcpy(&bits,&values[role],sizeof(bits));
        __atomic_store_n(&deck->levels[role],bits,__ATOMIC_RELAXED);
    }
    request_source_work(deck);
}
static int levels_engaged(const struct deck_state *deck){
    for(unsigned role=0;role<4;role++)if(__atomic_load_n(&deck->levels[role],__ATOMIC_RELAXED)!=UINT32_C(0x3f800000))return 1;
    return 0;
}
/* Control threads, with control_mutex: the stem pool holds a deck whose stems are in use first (overcue.h). */
static void publish_engaged(struct deck_state *deck){
    struct publication *p=acquire(deck);
    if(!p)return;
    if(p->prepared)xz_oc_stream_engaged(p->prepared,levels_engaged(deck));
    release(deck);
}
/* control_mutex owns generation publication and control writes together. */
static uint32_t advance_generation_locked(struct deck_state *deck){
    uint32_t generation=__atomic_add_fetch(&deck->generation,1,__ATOMIC_SEQ_CST);
    const float neutral[4]={1,1,1,1};store_levels(deck,neutral);
    return generation;
}

static uint32_t cancel_deck(int index, enum xz_audio_state state)
{
    struct deck_state *deck = &decks[index];
    pthread_mutex_lock(&control_mutex);
    uint32_t gen=advance_generation_locked(deck);
    request_source_work(deck);
    deck->pending_kind = 2;
    deck->pending_generation = gen;
    deck->history_valid = 0;
    memset(&deck->status, 0, sizeof(deck->status));
    deck->status.state = state == XZ_AUDIO_STOPPED ||
        __atomic_load_n(&enabled, __ATOMIC_ACQUIRE) ? state : XZ_AUDIO_DISABLED;
    xz_wake_set(&changed);
    pthread_mutex_unlock(&control_mutex);
    return gen;
}

static void queue_source(int index, uint32_t generation, uint32_t output_epoch,
                         const struct xz126_deck_source *source)
{
    struct deck_state *deck = &decks[index];
    pthread_mutex_lock(&control_mutex);
    if (__atomic_load_n(&deck->generation, __ATOMIC_SEQ_CST) == generation &&
        __atomic_load_n(&deck->output_epoch, __ATOMIC_ACQUIRE) == output_epoch &&
        __atomic_load_n(&running, __ATOMIC_ACQUIRE)) {
        deck->pending = *source;
        xz_ram_bind(index, source);
        deck->pending_generation = generation;
        deck->history_valid = 1;
        deck->pending_kind = __atomic_load_n(&enabled, __ATOMIC_ACQUIRE) ? 1 : 2;
        if (!__atomic_load_n(&enabled, __ATOMIC_ACQUIRE))
            deck->status.state = XZ_AUDIO_DISABLED;
        deck->status.reader_rate = source->reader_rate;
        deck->status.reader_frames = source->reader_frames;
        snprintf(deck->status.path, sizeof(deck->status.path), "%s", source->path);
        /* Publish the complete post-stretch owner identity last. A generation
         * change invalidates it until a successful load snapshot reaches here. */
        __atomic_store_n(&deck->output_rate, source->reader_rate, __ATOMIC_RELAXED);
        __atomic_store_n(&deck->output_manager, source->manager, __ATOMIC_RELAXED);
        __atomic_store_n(&deck->published_output_epoch, output_epoch, __ATOMIC_RELEASE);
        xz_wake_set(&changed);
    }
    pthread_mutex_unlock(&control_mutex);
}

extern void xz_ram_track_load_begin(void) __attribute__((weak));
extern int xz_ram_track_load_busy(void) __attribute__((weak));
static int load_hook(void *reader, const void *track)
{
    /* A track is loading: background stem copies leave the USB alone for a moment (ram/budget.h). */
    if (xz_ram_track_load_begin) xz_ram_track_load_begin();
    uint32_t engine = 0, generation = 0, output_epoch = 0;
    int index = -1, result;
    struct xz126_deck_source source;
    if (__atomic_load_n(&running, __ATOMIC_ACQUIRE)) {
        index = identify_reader((uint32_t)(uintptr_t)reader, &engine);
        if (index >= 0) {
            xz_ram_invalidate(index);
            output_epoch = __atomic_add_fetch(&decks[index].output_epoch, 1,
                                               __ATOMIC_ACQ_REL);
            generation = cancel_deck(index, XZ_AUDIO_LOADING_TRACK);
        }
    }
    if(xz_diary_busy_begin)xz_diary_busy_begin(3);
    result = original_load(reader, track);
    if(xz_diary_busy_end)xz_diary_busy_end(3);
    /* The reader is open; the stock player goes on loading the track for a few seconds. Keep the window open. */
    if (xz_ram_track_load_begin) xz_ram_track_load_begin();
    if (index >= 0) {
        if (result) set_state(&decks[index], generation, XZ_AUDIO_WAITING_TRACK, 0);
        else if (!xz126_deck_source_read(read_worker, NULL, engine, (unsigned)index, &source) &&
                 source.reader == (uint32_t)(uintptr_t)reader)
            queue_source(index, generation, output_epoch, &source);
        else set_state(&decks[index], generation, XZ_AUDIO_SOURCE_UNSUPPORTED, 0);
    }
    return result;
}

static void unload_hook(void *reader)
{
    uint32_t engine;
    int index;
    if (__atomic_load_n(&running, __ATOMIC_ACQUIRE)) {
        index = identify_reader((uint32_t)(uintptr_t)reader, &engine);
        if (index >= 0) {
            xz_ram_invalidate(index);
            __atomic_add_fetch(&decks[index].output_epoch, 1, __ATOMIC_ACQ_REL);
            cancel_deck(index, XZ_AUDIO_WAITING_TRACK);
        }
    }
    original_unload(reader);
}

/* Stock common::PcmReader, firmware 1.26 (object 0x140da08 bytes, Player+0xc8). Addresses and the
 * reasoning are in the jump-skip report. PcmReader::getStreamAt (0x38d28), reached through
 * TimeStretch::getStreamAt (0x8fd5c), fills the block from the first source that covers it: one of nine
 * cue buffers, the slip buffer, the ring; otherwise it zero-fills, asks for a jump and records the
 * position at PCM_MISS. Its return value is the frame count in every case and tells nothing.
 * The ring keeps frame p in slot (p - base_position + base_index) mod PCM_RING_SLOTS. The decoder
 * thread (PcmReader::run 0x37c84, core 2) moves center/behind/ahead all the time, fastest while it
 * refills after a jump, without moving any slot; only a reset (updateReadPoint 0x36510) re-bases the
 * slots, and prepare() (0x3479c, audio and Player threads) then copies the new base. */
#define PCM_RING_SLOTS UINT32_C(0x300000)
#define PCM_MISS UINT32_C(0x140da00)          /* -1 at construction, written only by getStreamAt */
#define PCM_CURRENT_CUE UINT32_C(0xb0)        /* state (3 = stored), cue position */
#define PCM_HOT_CUE UINT32_C(0x42c178)        /* eight of: state (2 = stored), cue position */
#define PCM_HOT_CUE_STRIDE UINT32_C(0x17afc8)
#define PCM_CUE_BEFORE 97020u                 /* isCueBufferedAt 0x366dc: frames kept before the cue */
#define PCM_CURRENT_CUE_AFTER 449819u
#define PCM_HOT_CUE_AFTER 97019u
#define PCM_CUES 9

struct ring_state { uint32_t center, behind, ahead, impl, center_index, base_position, base_index, miss;
                    unsigned loaded, ready; };
struct cue_state { uint32_t state[PCM_CUES], position[PCM_CUES]; };
enum source_proof { PROOF_RING, PROOF_RING_MOVED, PROOF_CUE, PROOF_MISS, PROOF_NONE };

static uint32_t native_word(uint32_t address)
{
    return __atomic_load_n((const uint32_t *)(uintptr_t)address, __ATOMIC_RELAXED);
}

static struct ring_state ring_read(uint32_t reader)
{
    struct ring_state r;
    r.impl = native_word(reader + 4);
    r.center = native_word(reader + 0x6c);
    r.base_position = native_word(reader + 0x70);
    r.center_index = native_word(reader + 0x74);
    r.base_index = native_word(reader + 0x78);
    r.behind = native_word(reader + 0x88);
    r.ahead = native_word(reader + 0x84);
    r.miss = native_word(reader + PCM_MISS);
    r.loaded = __atomic_load_n((const unsigned char *)(uintptr_t)(reader + 0x9c), __ATOMIC_RELAXED);
    r.ready = __atomic_load_n((const unsigned char *)(uintptr_t)(reader + 0x69), __ATOMIC_RELAXED);
    return r;
}

static void cue_read(uint32_t reader, struct cue_state *c)
{
    c->state[0] = native_word(reader + PCM_CURRENT_CUE);
    c->position[0] = native_word(reader + PCM_CURRENT_CUE + 4);
    for (unsigned k = 1; k < PCM_CUES; ++k) {
        uint32_t at = reader + PCM_HOT_CUE + (k - 1) * PCM_HOT_CUE_STRIDE;
        c->state[k] = native_word(at);
        c->position[k] = native_word(at + 4);
    }
}

static int ring_covers(struct ring_state r, struct xz126_source_extent span,
                        uint32_t impl)
{
    uint32_t first = r.center > r.behind ? r.center - r.behind : 0;
    uint64_t last = (uint64_t)r.center + r.ahead;
    return r.loaded && r.ready && r.impl == impl && span.frames &&
           span.source_position >= first &&
           (uint64_t)span.source_position + span.frames <= last && last <= INT32_MAX;
}

/* Slot of position 0 under the base the reads use. */
static uint32_t ring_shift(struct ring_state r)
{
    int64_t shift = ((int64_t)r.base_index - r.base_position) % PCM_RING_SLOTS;
    return (uint32_t)(shift < 0 ? shift + PCM_RING_SLOTS : shift);
}

/* The window (center, behind, ahead) and the base the reads use describe the same slots. False between
 * a reset and the next prepare(), and for a snapshot torn by a concurrent update. */
static int ring_consistent(struct ring_state r)
{
    return ((uint64_t)r.center + ring_shift(r)) % PCM_RING_SLOTS == r.center_index;
}

static int same_ring(struct ring_state a, struct ring_state b)
{
    return a.center == b.center && a.behind == b.behind && a.ahead == b.ahead &&
           a.impl == b.impl && a.loaded == b.loaded && a.ready == b.ready;
}

static int cue_covers(const struct cue_state *c, unsigned k, struct xz126_source_extent span)
{
    uint32_t cue = c->position[k];
    if (c->state[k] != (k ? 2u : 3u) || cue > INT32_MAX || !span.frames) return 0;
    uint32_t first = cue > PCM_CUE_BEFORE ? cue - PCM_CUE_BEFORE : 0;
    uint64_t last = (uint64_t)cue + (k ? PCM_HOT_CUE_AFTER : PCM_CURRENT_CUE_AFTER);
    return span.source_position >= first && (uint64_t)span.source_position + span.frames - 1 <= last;
}

/* Is the audio the native read left in the block the track's audio of span? Mixing stems into anything
 * else (silence above all) is worse than leaving the block as stock, so every answer but RING, RING_MOVED
 * and CUE leaves it as stock. The snapshots are taken just before and just after the native read on the
 * thread that makes it. A covered span stays covered while the decoder moves the window; uncovered to
 * covered and back within one read would take a whole decode pass, which lasts milliseconds. */
static enum source_proof prove_source(struct ring_state before, struct ring_state after,
                                      const struct cue_state *cue_before, const struct cue_state *cue_after,
                                      struct xz126_source_extent span, uint32_t impl)
{
    if (before.miss != after.miss) return PROOF_MISS; /* zero-filled by this read */
    if (ring_covers(before, span, impl) && ring_covers(after, span, impl) &&
        ring_consistent(before) && ring_consistent(after) && ring_shift(before) == ring_shift(after))
        return same_ring(before, after) ? PROOF_RING : PROOF_RING_MOVED;
    if (cue_before && cue_after && before.impl == impl && after.impl == impl &&
        before.loaded && before.ready && after.loaded && after.ready)
        for (unsigned k = 0; k < PCM_CUES; ++k)
            if (cue_before->state[k] == cue_after->state[k] &&
                cue_before->position[k] == cue_after->position[k] && cue_covers(cue_after, k, span))
                return PROOF_CUE;
    return after.miss == span.source_position ? PROOF_MISS : PROOF_NONE;
}

static struct xz_stem_levels get_levels(const struct deck_state *deck)
{
    struct xz_stem_levels levels;
    uint32_t bits[3];
    unsigned i;
    for (i = 0; i < 3; ++i) bits[i] = __atomic_load_n(&deck->levels[i], __ATOMIC_RELAXED);
    memcpy(&levels.drums, &bits[0], 4);
    memcpy(&levels.harmonics, &bits[1], 4);
    memcpy(&levels.vocals, &bits[2], 4);
    return levels;
}

static uint32_t stream_hook(void *self, int32_t position, void *buffer, uint32_t frames)
{
    struct publication *p = NULL;
    struct deck_state *deck = NULL;
    struct ring_state before = {0}, after;
    struct cue_state cue_before, cue_after;
    int cue_taken = 0;
    struct xz126_source_extent span = {0};
    uint32_t reader = 0, gen = 0, result;
    unsigned i;int borrowed=0;
    int tracing=xz_audio_trace_enabled()&&xz_audio_trace_source_sample();
    if(!tracing){
        if(!__atomic_load_n(&running,__ATOMIC_ACQUIRE)||!__atomic_load_n(&enabled,__ATOMIC_ACQUIRE)||
           !((__atomic_load_n(&decks[0].source_work,__ATOMIC_ACQUIRE)|__atomic_load_n(&decks[1].source_work,__ATOMIC_ACQUIRE))&1u))
            return original_stream(self,position,buffer,frames);
        if(!self||!buffer||!frames||frames>65536)return original_stream(self,position,buffer,frames);
        reader=native_word((uint32_t)(uintptr_t)self+4);
        int needed=0;
        for(i=0;i<DECKS;i++)if(reader==__atomic_load_n(&decks[i].known_reader,__ATOMIC_ACQUIRE)&&
            (__atomic_load_n(&decks[i].source_work,__ATOMIC_ACQUIRE)&1u)){needed=1;break;}
        if(!needed)return original_stream(self,position,buffer,frames);
    }
    if (__atomic_load_n(&running, __ATOMIC_ACQUIRE) &&
        __atomic_load_n(&enabled, __ATOMIC_ACQUIRE) && self && buffer &&
        frames && frames <= 65536) {
        if(!reader)reader = native_word((uint32_t)(uintptr_t)self + 4);
        if(source_batch&&source_batch->publication){
            struct publication *candidate=source_batch->publication;struct deck_state *owner=source_batch->deck;
            if(candidate->reader==reader&&candidate->generation==__atomic_load_n(&owner->generation,__ATOMIC_SEQ_CST)&&
               native_word(reader)==PCM_READER_VPTR){
                p=candidate;deck=owner;gen=source_batch->generation;borrowed=1;
                span=xz126_source_extent(position,frames,p->reader_frames);before=ring_read(reader);
            }
        }
        for (i = 0; !p && i < DECKS; ++i) {
            p = acquire(&decks[i]);
            if (!p) continue;
            gen = __atomic_load_n(&decks[i].generation, __ATOMIC_SEQ_CST);
            if (p->reader == reader && p->generation == gen &&
                native_word(reader) == PCM_READER_VPTR) {
                deck = &decks[i];
                span = xz126_source_extent(position, frames, p->reader_frames);
                before = ring_read(reader);
                break;
            }
            release(&decks[i]);
            p = NULL;
        }
    }
    /* The cue buffers matter only while the ring does not cover the span: after a jump outside it, the
       stock plays the cue buffer while the decoder refills the ring. */
    if (p && deck && !(ring_covers(before, span, p->reader_impl) && ring_consistent(before))) {
        cue_read(reader, &cue_before);
        cue_taken = 1;
    }
    uint64_t native_started=tracing?xz_audio_trace_time():0;
    result = original_stream(self, position, buffer, frames);
    uint64_t native_finished=native_started?xz_audio_trace_time():0;
    if (p && deck) {
        after = ring_read(reader);
        if (cue_taken) cue_read(reader, &cue_after);
        enum source_proof proof = prove_source(before, after, cue_taken ? &cue_before : NULL,
                                               cue_taken ? &cue_after : NULL, span, p->reader_impl);
        int current = __atomic_load_n(&running, __ATOMIC_ACQUIRE) &&
            __atomic_load_n(&enabled, __ATOMIC_ACQUIRE) &&
            gen == __atomic_load_n(&deck->generation, __ATOMIC_SEQ_CST);
        if (current && proof <= PROOF_CUE) {
            if (proof == PROOF_RING_MOVED) __atomic_fetch_add(&deck->moved, 1, __ATOMIC_RELAXED);
            else if (proof == PROOF_CUE) __atomic_fetch_add(&deck->cue, 1, __ATOMIC_RELAXED);
            uint32_t work=__atomic_load_n(&deck->source_work,__ATOMIC_ACQUIRE);
            struct xz_stem_levels levels = get_levels(deck);
            float values[4] = {levels.drums, levels.harmonics, levels.vocals, 1};
            uint32_t bass_bits=__atomic_load_n(&deck->levels[3],__ATOMIC_RELAXED);
            memcpy(values+3,&bass_bits,4);
            struct xz_audio_trace_context trace={.epoch=gen,.rate=p->reader_frames?deck->output_rate:0,.frames=(uint32_t)span.frames,
                .position=(int32_t)span.source_position,.result=(int32_t)result,.flags=0x10000u|((uint32_t)proof<<20)|(p->prepared?1u:0u),
                .duration_us=(uint32_t)(native_finished-native_started),.levels={levels.drums,levels.harmonics,levels.vocals},
                .owner=reader,.manager=__atomic_load_n(&deck->output_manager,__ATOMIC_RELAXED)};
            if(native_started){
                if(p->prepared)xz_oc_stream_trace_state(p->prepared,trace.prepared);
                xz_audio_trace_record((unsigned)(deck-decks),XZ_TRACE_DECODE,(float*)buffer+span.skip*2,&trace);
            }
            if(p->prepared){
                uint32_t object=(uint32_t)(uintptr_t)self;
                int32_t loop_in=(int32_t)native_word(object+0x14),loop_out=(int32_t)native_word(object+0x18);
                int looping=__atomic_load_n((const unsigned char *)(uintptr_t)(object+0x10),__ATOMIC_RELAXED);
                xz_oc_stream_loop_start(p->prepared,looping&&loop_in>=0&&loop_out>loop_in&&(uint32_t)loop_out<=p->reader_frames?loop_in:-1);
            }
            size_t changed_frames = p->prepared ?
                xz_oc_stream_mix4_trace(p->prepared, (float *)buffer + span.skip * 2, span.frames, span.source_position, values,native_started?trace.mix:NULL) :
                xz_stem_mix((float *)buffer + span.skip * 2,
                    span.frames, span.source_position, &p->decoded.pcm, levels);
            if(native_started){
                trace.duration_us=(uint32_t)(xz_audio_trace_time()-native_finished);trace.total_us=(uint32_t)(xz_audio_trace_time()-native_started);trace.aux0=(uint32_t)changed_frames;
                if(p->prepared)xz_oc_stream_trace_state(p->prepared,trace.prepared);
                xz_audio_trace_record((unsigned)(deck-decks),XZ_TRACE_STEMS,(float*)buffer+span.skip*2,&trace);
            }
            if (changed_frames) __atomic_fetch_add(&deck->mixed, 1, __ATOMIC_RELAXED);
            if(levels.drums==1&&levels.harmonics==1&&levels.vocals==1&&
               (!p->prepared||xz_oc_stream_neutral(p->prepared))&&
               gen==__atomic_load_n(&deck->generation,__ATOMIC_SEQ_CST)){
                uint32_t quiet=work&~1u;
                __atomic_compare_exchange_n(&deck->source_work,&work,quiet,0,__ATOMIC_RELEASE,__ATOMIC_RELAXED);
            }
        } else {
            __atomic_fetch_add(&deck->skipped, 1, __ATOMIC_RELAXED);
            __atomic_fetch_add(!current ? &deck->identity : proof == PROOF_MISS ? &deck->miss : &deck->unproven,
                               1, __ATOMIC_RELAXED);
            if(native_started){
                struct xz_audio_trace_context trace={.epoch=gen,.frames=frames,.position=position,.result=(int32_t)result,
                    .flags=0x1000u|(!same_ring(before,after)?0x2000u:0u)|(!ring_covers(after,span,p->reader_impl)?0x4000u:0u)|
                        ((uint32_t)proof<<20)|(current?0u:0x800000u),
                    .duration_us=(uint32_t)(native_finished-native_started),.owner=reader,
                    .aux0=after.center,.aux1=(after.loaded<<1)|after.ready};
                xz_audio_trace_record((unsigned)(deck-decks),XZ_TRACE_DECODE,NULL,&trace);
            }
        }
        if(!borrowed)release(deck);
    } else if(native_started&&self){
        uint32_t candidate=native_word((uint32_t)(uintptr_t)self+4);
        for(i=0;i<DECKS;i++)if(candidate==__atomic_load_n(&decks[i].known_reader,__ATOMIC_ACQUIRE)){
            struct xz_audio_trace_context trace={.frames=frames,.position=position,.result=(int32_t)result,.flags=0x8000u,
                .duration_us=(uint32_t)(native_finished-native_started),.owner=candidate};
            xz_audio_trace_record(i,XZ_TRACE_DECODE,NULL,&trace);break;
        }
    }
    return result;
}

static int is_wav_or_flac(const char *path)
{
    const char *ext = strrchr(path, '.');
    char lower[6];
    size_t i, n;
    if (!ext || (n = strlen(ext)) >= sizeof(lower)) return 0;
    for (i = 0; i <= n; ++i) lower[i] = ext[i] >= 'A' && ext[i] <= 'Z' ?
        (char)(ext[i] + 'a' - 'A') : ext[i];
    return !strcmp(lower, ".wav") || !strcmp(lower, ".flac");
}

static int discover_cache(const struct xz126_deck_source *source,
                           struct xz_stem_entry *entry)
{
    char ancestor[XZ126_READER_PATH_MAX], cache[XZ_STEM_PATH_MAX];
    const char *selected = getenv("XZ_STEM_SEPARATION_ID");
    unsigned depth = 0, candidates = 0;
    int hits = 0;
    char *slash;
    if (source->path[0] != '/') return XZ_AUDIO_SOURCE_UNSUPPORTED;
    snprintf(ancestor, sizeof(ancestor), "%s", source->path);
    slash = strrchr(ancestor, '/');
    if (!slash) return XZ_AUDIO_SOURCE_UNSUPPORTED;
    *slash = 0;
    do {
        DIR *dir;
        struct dirent *item;
        if (!ancestor[0]) strcpy(ancestor, "/");
        if (!selected || !*selected) {
            char choice[97];
            int choice_rc = xz_stem_cache_choice(ancestor, source->path,
                source->file_frames, choice, sizeof(choice));
            if (choice_rc == XZ_STEM_OK) {
                return xz_stem_cache_lookup(ancestor, choice, source->path,
                    source->file_frames, entry) == XZ_STEM_OK ?
                    XZ_AUDIO_EXPERIMENTAL_READY : XZ_AUDIO_CACHE_CHOICE_INVALID;
            }
            if (choice_rc != XZ_STEM_MISSING) return XZ_AUDIO_CACHE_CHOICE_INVALID;
        }
        if (snprintf(cache, sizeof(cache), "%s/mods/stemd-cache", ancestor) >= (int)sizeof(cache))
            return XZ_AUDIO_CACHE_MISSING;
        dir = opendir(cache);
        if (dir) {
            while ((item = readdir(dir)) != NULL) {
                struct xz_stem_entry candidate;
                if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, "..") ||
                    (selected && *selected && strcmp(selected, item->d_name))) continue;
                if (++candidates > 256) { hits = 2; break; }
                if (!xz_stem_cache_lookup(ancestor, item->d_name, source->path,
                                          source->file_frames, &candidate)) {
                    ++hits;
                    *entry = candidate;
                }
            }
            closedir(dir);
        }
        if (!strcmp(ancestor, "/")) break;
        slash = strrchr(ancestor, '/');
        if (!slash) break;
        *slash = 0;
    } while (++depth < 32);
    return hits > 1 ? XZ_AUDIO_CACHE_AMBIGUOUS :
        hits == 1 ? XZ_AUDIO_EXPERIMENTAL_READY : XZ_AUDIO_CACHE_MISSING;
}

struct cancel_context { struct deck_state *deck; uint32_t generation; };
static int cancelled(void *context)
{
    struct cancel_context *c = context;
    return !__atomic_load_n(&running, __ATOMIC_ACQUIRE) ||
        !__atomic_load_n(&enabled, __ATOMIC_ACQUIRE) ||
        c->generation != __atomic_load_n(&c->deck->generation, __ATOMIC_SEQ_CST);
}

static void set_state(struct deck_state *deck, uint32_t gen,
                       enum xz_audio_state state, size_t bytes)
{
    pthread_mutex_lock(&control_mutex);
    if (gen == __atomic_load_n(&deck->generation, __ATOMIC_SEQ_CST)) {
        deck->status.state = __atomic_load_n(&enabled, __ATOMIC_ACQUIRE) ||
            state == XZ_AUDIO_HOOK_FAILED ? state : XZ_AUDIO_DISABLED;
        deck->status.pcm_bytes = bytes;
    }
    pthread_mutex_unlock(&control_mutex);
}

static void log_preparation_recovery(int index,uint32_t gen,const struct xz_oc_open_diagnostics *diagnostics,int recovered,struct cancel_context *context)
{
    if(cancelled(context))return;
    for(unsigned i=0;i<diagnostics->failures;i++){
        char line[256];
        snprintf(line,sizeof(line),"stems: deck=%d generation=%u preparation attempt=%u %s: %s",index+1,gen,i+1,recovered?"recovered":"failed",diagnostics->failure[i]);
        xz_log(line);
    }
}

static void prepare_deck(int index, struct xz126_deck_source source, uint32_t gen)
{
    struct deck_state *deck = &decks[index];
    struct cancel_context context = {deck, gen};
    struct xz_stem_entry entry;
    struct publication *p;
    struct xz126_deck_source current;
    uint32_t engine;
    int state, rc;
    if (cancelled(&context)) return;
    /* The track comes first: no stem file is opened, hashed or copied while a track is loading on any deck. */
    while (xz_ram_track_load_busy && xz_ram_track_load_busy()) {
        if (cancelled(&context) || !__atomic_load_n(&running, __ATOMIC_ACQUIRE)) return;
        struct timespec wait = {0, 50000000}; nanosleep(&wait, NULL);
    }
    /* Enabling after a captured load may run much later. Refresh through the
     * guarded reader, and require both hook generation and source identity.
     * A source never observed by our lifecycle hooks is never queued here. */
    if (read_word(XZ126_PLAY_ENGINE_CELL, &engine) ||
        xz126_deck_source_read(read_worker, NULL, engine, (unsigned)index, &current) ||
        current.reader != source.reader || current.reader_impl != source.reader_impl ||
        current.reader_rate != source.reader_rate || current.reader_frames != source.reader_frames ||
        current.file_rate != source.file_rate || current.file_frames != source.file_frames ||
        strcmp(current.path, source.path) || cancelled(&context)) {
        set_state(deck, gen, XZ_AUDIO_SOURCE_UNSUPPORTED, 0); return;
    }
    p = calloc(1, sizeof(*p));
    if (!p) { set_state(deck, gen, XZ_AUDIO_DECODE_FAILED, 0); return; }
    char prepared_error[160]={0};
    struct xz_oc_open_diagnostics diagnostics;
    int stick = stick_of(source.path);
    uint32_t stops = stick ? __atomic_load_n(&stick_stops[stick - 1], __ATOMIC_ACQUIRE) : 0;
    p->prepared = xz_oc_stream_open_retry(source.path, source.reader_rate, prepared_error, sizeof(prepared_error), cancelled, &context, &diagnostics);
    /* Its stick began to go while the stream opened: the stop asked the streams it saw, not this one. */
    if (p->prepared && stick && __atomic_load_n(&stick_stops[stick - 1], __ATOMIC_ACQUIRE) != stops)
        xz_oc_stream_source_gone(p->prepared, stick_prefix[stick - 1], stems_answer, (uint32_t)(stick - 1) << 1 | (uint32_t)index);
    if (cancelled(&context)) { free_publication(p); return; }
    log_preparation_recovery(index,gen,&diagnostics,p->prepared!=NULL,&context);
    if (p->prepared) goto publish;
    pthread_mutex_lock(&control_mutex);
    if (!cancelled(&context)) snprintf(deck->status.error,sizeof(deck->status.error),"%s",prepared_error[0]?prepared_error:"Prepared stems open failed without diagnostic");
    pthread_mutex_unlock(&control_mutex);
    free(p);
    char declared_stems[4096];
    if(!xz_oc_bundle(source.path,declared_stems,sizeof(declared_stems))){
        set_state(deck,gen,XZ_AUDIO_PREPARED_ERROR,0);return;
    }
    if (!is_wav_or_flac(source.path) || source.file_rate != 44100 ||
        source.reader_rate != 44100 || source.file_frames != source.reader_frames) {
        set_state(deck, gen, XZ_AUDIO_SOURCE_UNSUPPORTED, 0); return;
    }
    state = discover_cache(&source, &entry);
    if (state != XZ_AUDIO_EXPERIMENTAL_READY) {
        set_state(deck, gen, (enum xz_audio_state)state, 0); return;
    }
    if (cancelled(&context)) return;
    p = calloc(1, sizeof(*p));
    if (!p) { set_state(deck, gen, XZ_AUDIO_DECODE_FAILED, 0); return; }
    set_state(deck, gen, XZ_AUDIO_DECODING, 0);
    rc = xz_stem_decode(&entry, source.reader_rate, PCM_CAP, cancelled, &context, &p->decoded);
    if (rc || cancelled(&context)) {
        if (!cancelled(&context)) set_state(deck, gen,
            rc == XZ_STEM_DECODE_RATE ? XZ_AUDIO_ALIGNMENT_BLOCKED : XZ_AUDIO_DECODE_FAILED, 0);
        free_publication(p); return;
    }
    if (p->decoded.pcm.frames != source.reader_frames) {
        free_publication(p); set_state(deck, gen, XZ_AUDIO_ALIGNMENT_BLOCKED, 0); return;
    }
publish:
    p->reader = source.reader;
    p->reader_impl = source.reader_impl;
    p->generation = gen;
    p->reader_frames = source.reader_frames;
    pthread_mutex_lock(&control_mutex);
    if (!cancelled(&context)) {
        if (p->prepared) {
            /* The track start is always worth keeping ready; cue and hot cues come through xz_audio_set_anchors. */
            const int32_t start = 0;
            xz_oc_stream_anchors(p->prepared, &start, 1);
            xz_oc_stream_engaged(p->prepared, levels_engaged(deck));
        }
        __atomic_store_n(&deck->published, p, __ATOMIC_SEQ_CST);
        deck->status.state = p->prepared ? XZ_AUDIO_PREPARED_ALIGNING : XZ_AUDIO_EXPERIMENTAL_READY;
        deck->status.stem_count = p->prepared ? xz_oc_stream_stem_count(p->prepared) : 3;
        deck->status.pcm_bytes = p->prepared ? 4u * (deck->status.stem_count-1) * XZ_OC_WINDOW_FRAMES * 2u * sizeof(float) : p->decoded.pcm_bytes;
        snprintf(deck->status.format_name,sizeof(deck->status.format_name),"%s",p->prepared?xz_oc_stream_format(p->prepared):"STEMD 3");
        deck->status.error[0]=0;
        deck->status.prepared = p->prepared != NULL;
        p = NULL;
    }
    pthread_mutex_unlock(&control_mutex);
    free_publication(p);
}

static void *worker_main(void *unused)
{
    (void)unused;
    if (xz_oc_worker_schedule()) {
        xz_log("stems: background CPU assignment unavailable; native playback retained");
        for (int i=0;i<DECKS;i++) set_state(&decks[i],decks[i].generation,XZ_AUDIO_HOOK_FAILED,0);
        return NULL;
    }
    xz_log("stems: background workers use CPUs 0 and 2; native audio affinity unchanged");
    while (__atomic_load_n(&running, __ATOMIC_ACQUIRE)) {
        struct xz126_deck_source source;
        uint32_t gen = 0;
        int index = -1, kind = 0, i;
        pthread_mutex_lock(&control_mutex);
        for (i = 0; i < DECKS; ++i) if (decks[i].pending_kind) {
            index = i; kind = decks[i].pending_kind;
            source = decks[i].pending; gen = decks[i].pending_generation;
            decks[i].pending_kind = 0; break;
        }
        int draining = 0;
        for (i = 0; i < DECKS; ++i) draining |= decks[i].draining != NULL;
        if (index < 0 && __atomic_load_n(&running, __ATOMIC_ACQUIRE))
            xz_wake_wait_locked(&changed, &control_mutex, draining ? 10 : 1000);
        pthread_mutex_unlock(&control_mutex);
        if (index >= 0) {
            retire(&decks[index]);
            if (kind == 1) prepare_deck(index, source, gen);
        }
        for (i = 0; i < DECKS; ++i) if (decks[i].draining) drain(&decks[i], 0);
    }
    /* Stopping: wait for the readers as long as it takes, a second at a time, so the log says why. */
    for (int i = 0; i < DECKS; ++i) { retire(&decks[i]); while (drain(&decks[i], 1000)) {} }
    return NULL;
}

void xz_audio_set_levels(int deck, struct xz_stem_levels levels)
{
    float values[] = {levels.drums, levels.harmonics, levels.vocals, 1};
    xz_audio_set_levels4(deck,values);
}

static int levels_valid(int deck,const float values[4]){
    if(deck<0||deck>=DECKS||!values)return 0;
    for(unsigned role=0;role<4;role++)if(!isfinite(values[role])||values[role]<0||values[role]>2)return 0;
    return 1;
}
void xz_audio_set_levels4(int deck,const float values[4]){
    if(!levels_valid(deck,values))return;
    pthread_mutex_lock(&control_mutex);store_levels(&decks[deck],values);publish_engaged(&decks[deck]);pthread_mutex_unlock(&control_mutex);
}
int xz_audio_set_levels4_for_generation(int deck,uint32_t generation,const float values[4]){
    if(!levels_valid(deck,values))return -1;
    pthread_mutex_lock(&control_mutex);
    int current=generation==__atomic_load_n(&decks[deck].generation,__ATOMIC_SEQ_CST);
    if(current){store_levels(&decks[deck],values);publish_engaged(&decks[deck]);}
    pthread_mutex_unlock(&control_mutex);return current?0:-1;
}
int xz_audio_set_anchors(int deck,uint32_t generation,const uint32_t *ms,unsigned count){
    if(deck<0||deck>=DECKS||(count&&!ms))return -1;
    pthread_mutex_lock(&control_mutex);
    struct deck_state *d=&decks[deck];
    int current=generation==__atomic_load_n(&d->generation,__ATOMIC_SEQ_CST);
    struct publication *p=current?acquire(d):NULL;
    if(p){
        if(p->prepared){
            int32_t frames[XZ_AUDIO_ANCHORS];unsigned n=0;uint32_t rate=d->status.reader_rate;
            frames[n++]=0;
            for(unsigned i=0;i<count&&n<XZ_AUDIO_ANCHORS;i++){
                uint64_t frame=(uint64_t)ms[i]*rate/1000u;
                if(frame<p->reader_frames)frames[n++]=(int32_t)frame;
            }
            xz_oc_stream_anchors(p->prepared,frames,n);
        }
        release(d);
    }
    pthread_mutex_unlock(&control_mutex);return p?0:-1;
}

void xz_audio_set_enabled(int on)
{
    int previous = __atomic_exchange_n(&enabled, !!on, __ATOMIC_ACQ_REL);
    if (previous == !!on) return;
    pthread_mutex_lock(&control_mutex);
    for (int i = 0; i < DECKS; ++i) {
        struct deck_state *deck = &decks[i];
        if (!on) {
            uint32_t gen=advance_generation_locked(deck);
            /* Preserve owned history only when no loader already invalidated it. */
            if (deck->history_valid && deck->pending_generation == gen - 1)
                deck->pending_generation = gen;
            else deck->history_valid = 0;
            deck->pending_kind = 2;
            deck->status.state = XZ_AUDIO_DISABLED;
            deck->status.pcm_bytes = 0;
        } else if (__atomic_load_n(&running, __ATOMIC_ACQUIRE) && deck->history_valid &&
                   deck->pending_generation == __atomic_load_n(&deck->generation, __ATOMIC_SEQ_CST)) {
            deck->pending_kind = 1;
            deck->status.state = XZ_AUDIO_LOADING_TRACK;
        } else deck->status.state = XZ_AUDIO_WAITING_TRACK;
    }
    xz_wake_set(&changed);
    pthread_mutex_unlock(&control_mutex);
}

int xz_audio_get_status(int index, struct xz_audio_status *out)
{
    if (index < 0 || index >= DECKS || !out) return -1;
    pthread_mutex_lock(&control_mutex);
    *out = decks[index].status;
    out->generation=__atomic_load_n(&decks[index].generation,__ATOMIC_SEQ_CST);
    pthread_mutex_unlock(&control_mutex);
    out->starve_fallback=out->starvation=out->starve_runs=out->starve_last_frames=out->starve_max_frames=out->starve_last_ms=out->starve_max_ms=0;
    out->retained_stems_bytes=0;out->flat_chunks=out->flat_built=out->flat_ms=0;out->stems_wait_ram=0;
    out->store_held=out->store_grant=out->store_need=out->store_class=out->store_lead_ms=out->store_behind_ms=0;out->stems_whole=0;
    out->mixed_blocks = __atomic_load_n(&decks[index].mixed, __ATOMIC_RELAXED);
    out->skipped_blocks = __atomic_load_n(&decks[index].skipped, __ATOMIC_RELAXED);
    out->moved_blocks = __atomic_load_n(&decks[index].moved, __ATOMIC_RELAXED);
    out->cue_blocks = __atomic_load_n(&decks[index].cue, __ATOMIC_RELAXED);
    out->miss_blocks = __atomic_load_n(&decks[index].miss, __ATOMIC_RELAXED);
    out->unproven_blocks = __atomic_load_n(&decks[index].unproven, __ATOMIC_RELAXED);
    out->identity_blocks = __atomic_load_n(&decks[index].identity, __ATOMIC_RELAXED);
    out->stream_miss_blocks = 0;
    struct publication *p = acquire(&decks[index]);
    if (p) {
        if (p->prepared) {
            out->stem_count=xz_oc_stream_stem_count(p->prepared);
            snprintf(out->format_name,sizeof(out->format_name),"%s",xz_oc_stream_format(p->prepared));
            out->retained_stems_bytes=xz_oc_stream_ram_bytes(p->prepared);
            struct xz_oc_status status;
            xz_oc_stream_status(p->prepared, &status);
            out->state = status.state == XZ_OC_READY ? XZ_AUDIO_EXPERIMENTAL_READY :
                status.state == XZ_OC_ALIGNING ? XZ_AUDIO_PREPARED_ALIGNING :
                status.state == XZ_OC_BUFFERING ? XZ_AUDIO_PREPARED_BUFFERING : XZ_AUDIO_PREPARED_ERROR;
            out->starve_fallback=status.starve_fallback;out->starvation=status.starvation;out->starve_runs=status.starve_runs;
            out->starve_last_frames=status.starve_last_frames;out->starve_max_frames=status.starve_max_frames;
            out->starve_last_ms=status.starve_last_ms;out->starve_max_ms=status.starve_max_ms;
            out->flat_chunks=status.flat_chunks;out->flat_built=status.flat_built;out->flat_ms=status.flat_ms;
            out->store_held=status.store_held;out->store_grant=status.store_grant;out->store_need=status.store_need;
            out->store_class=status.store_class;out->store_lead_ms=status.store_lead_ms;out->store_behind_ms=status.store_behind_ms;
            out->stems_whole=status.stems_whole;
            out->stems_wait_ram=status.waiting_ram;
            out->alignment_frames = status.lag;
            out->alignment_correlation = status.correlation;
            out->skipped_blocks += status.misses;
            out->stream_miss_blocks = status.misses;
        }else out->retained_stems_bytes=p->decoded.pcm_bytes;
        release(&decks[index]);
    }
    return out->generation==__atomic_load_n(&decks[index].generation,__ATOMIC_SEQ_CST)?0:-1;
}

int xz_audio_waveform(int index, unsigned role, unsigned char *bins, size_t count, float *progress)
{
    if (index < 0 || index >= DECKS || role >= 4 || !bins || !count) return 0;
    struct publication *p = acquire(&decks[index]);
    if (!p) return 0;
    int available = p->prepared && xz_oc_stream_has_wave(p->prepared,role);
    if (available) xz_oc_stream_wave(p->prepared, role, bins, count, progress);
    release(&decks[index]);
    return available;
}

uint32_t xz_audio_output_epoch(unsigned index){
    if(index>=DECKS)return 0;
    struct deck_state *d=&decks[index];
    uint32_t epoch=__atomic_load_n(&d->output_epoch,__ATOMIC_ACQUIRE);
    if(!epoch||epoch!=__atomic_load_n(&d->published_output_epoch,__ATOMIC_ACQUIRE)||
       __atomic_load_n(&d->output_rate,__ATOMIC_RELAXED)!=44100)return 0;
    return epoch==__atomic_load_n(&d->output_epoch,__ATOMIC_ACQUIRE)?epoch:0;
}
int xz_audio_output_deck(const void *manager, uint32_t *rate_out,
                         uint32_t *generation_out)
{
    uint32_t address = (uint32_t)(uintptr_t)manager;
    int i;
    if (!manager || !rate_out || !generation_out) return -1;
    for (i = 0; i < DECKS; ++i) {
        struct deck_state *deck = &decks[i];
        uint32_t published = __atomic_load_n(&deck->published_output_epoch, __ATOMIC_ACQUIRE);
        uint32_t generation = __atomic_load_n(&deck->output_epoch, __ATOMIC_ACQUIRE);
        uint32_t owner, rate;
        if (!published || published != generation) continue;
        owner = __atomic_load_n(&deck->output_manager, __ATOMIC_RELAXED);
        rate = __atomic_load_n(&deck->output_rate, __ATOMIC_RELAXED);
        if (owner != address || !rate) continue;
        if (published != __atomic_load_n(&deck->published_output_epoch, __ATOMIC_ACQUIRE) ||
            generation != __atomic_load_n(&deck->output_epoch, __ATOMIC_ACQUIRE))
            continue;
        *rate_out = rate;
        *generation_out = generation;
        return i;
    }
    return -1;
}

const char *xz_audio_state_name(enum xz_audio_state state)
{
    static const char *const names[] = {
        "stopped", "waiting for track", "loading track", "no compatible prepared stems for this source",
        "compatible cache missing", "multiple cached models; select separation ID", "decoding stems",
        "stem decode failed or exceeded memory cap", "cached timeline mismatch",
        "experimental stems ready; recorded alignment unverified", "firmware hook installation failed", "disabled",
        "preferred stem model is invalid or missing; select it again in USB builder",
        "play briefly to align prepared stems", "loading selected stem mix; playback continues", "prepared stem data or worker setup failed"
    };
    return (unsigned)state < sizeof(names) / sizeof(names[0]) ? names[state] : "unknown";
}

int xz_audio_start(void)
{
    static const unsigned char load_guard[8] = {0xf0,0x4f,0x2d,0xe9,0x5c,0xd0,0x4d,0xe2};
    static const unsigned char unload_guard[8] = {0x70,0x40,0x2d,0xe9,0x28,0x50,0x80,0xe2};
    static const unsigned char stream_guard[8] = {0xf8,0x40,0x2d,0xe9,0x00,0x40,0xa0,0xe1};
    int rc = 0, i;
    pthread_mutex_lock(&lifecycle_mutex);
    if (worker_created) { pthread_mutex_unlock(&lifecycle_mutex); return 0; }
    __atomic_store_n(&enabled, 0, __ATOMIC_RELEASE);
    if (!hooks_installed) {
        if (!original_load && xz_hook_arm(0x34ba0, load_guard, (void *)load_hook, (void **)&original_load)) rc = -1;
        if (!rc && !original_unload && xz_hook_arm(0x348dc, unload_guard, (void *)unload_hook, (void **)&original_unload)) rc = -1;
        if (!rc && !original_stream && xz_hook_arm(0x8fd5c, stream_guard, (void *)stream_hook, (void **)&original_stream)) rc = -1;
        if (!rc) hooks_installed = 1;
    }
    if (rc) {
        for (i = 0; i < DECKS; ++i) set_state(&decks[i], decks[i].generation, XZ_AUDIO_HOOK_FAILED, 0);
        pthread_mutex_unlock(&lifecycle_mutex); return -1;
    }
    for (i = 0; i < DECKS; ++i) {
        xz_audio_set_levels(i, (struct xz_stem_levels){1,1,1});
        cancel_deck(i, XZ_AUDIO_WAITING_TRACK);
    }
    __atomic_store_n(&running, 1, __ATOMIC_RELEASE);
    if (XZ_WORKER_CREATE(&worker,worker_main,NULL)) {
        __atomic_store_n(&running, 0, __ATOMIC_RELEASE);
        pthread_mutex_unlock(&lifecycle_mutex); return -1;
    }
    worker_created = 1;
    if (stems_holder < 0 && xz_usb_release_holder) stems_holder = xz_usb_release_holder("stems", stems_let_go, NULL);
    pthread_mutex_unlock(&lifecycle_mutex);
    /* Existing tracks are intentionally not harvested from a polling thread:
     * their owner could be destroyed between reads. Next load captures safely. */
    xz_log("stems: lifecycle hooks active; disabled until explicitly enabled; two internal decks");
    return 0;
}

void xz_audio_stop(void)
{
    pthread_mutex_lock(&lifecycle_mutex);
    __atomic_store_n(&running, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&enabled, 0, __ATOMIC_RELEASE);
    for (int i = 0; i < DECKS; ++i) {
        __atomic_add_fetch(&decks[i].output_epoch, 1, __ATOMIC_ACQ_REL);
        cancel_deck(i, XZ_AUDIO_STOPPED);
    }
    if (worker_created) { pthread_join(worker, NULL); worker_created = 0; }
    pthread_mutex_unlock(&lifecycle_mutex);
}

int xz_audio_lifecycle_self_test(void)
{
    struct deck_state state = {0};
    struct publication p = {0};
    struct publication *held;
    state.generation = p.generation = 1;
    state.published = &p;
    held = acquire(&state);
    if (held != &p || state.users != 1) return -1;
    __atomic_add_fetch(&state.generation, 1, __ATOMIC_SEQ_CST);
    if (held->generation == state.generation) return -1;
    if (__atomic_exchange_n(&state.published, NULL, __ATOMIC_SEQ_CST) != &p) return -1;
    if (acquire(&state) != NULL || state.users != 1) return -1;
    release(&state);
    if (state.users) return -1;
    return 0;
}
