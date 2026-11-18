#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifdef NDEBUG
#error assertions required
#endif
#include <assert.h>
#include <stdint.h>
static uint32_t probe_generation_add(uint32_t *,uint32_t,int);
#define __atomic_add_fetch probe_generation_add
#include "../runtime.c"
#undef __atomic_add_fetch
#include <sys/mman.h>
/* Stock common::PcmReader layout, firmware 1.26, from the disassembly, for this test's
 * stock model (jump-skip report). Kept apart from the hook's own constants so the model
 * does not check the hook against itself. */
#define STOCK_RING_SLOTS 0x300000u
#define STOCK_MISS 0x140da00u
#define STOCK_CURRENT_CUE 0xb0u
#define STOCK_HOT_CUE 0x42c178u
#define STOCK_HOT_CUE_STRIDE 0x17afc8u
#ifdef OLD_HOOK
#define COUNTERS(x) ((void)0) /* the hook before the fix has no proof counters */
#else
#define COUNTERS(x) x
#endif
static unsigned char test_memory[65536];
static uint32_t expect_generation;
static int probe_control,probe_deferred;
static uint32_t probe_generation;
static uint32_t probe_generation_add(uint32_t *value,uint32_t amount,int order){
 uint32_t result=__atomic_add_fetch(value,amount,order);
 if(probe_control&&value==&decks[0].generation){
  probe_generation=result;
  if(!pthread_mutex_trylock(&control_mutex)){
   pthread_mutex_unlock(&control_mutex);
   xz_audio_set_levels(0,(struct xz_stem_levels){1,1,0});
  }else probe_deferred=1;
 }
 return result;
}

static int load_result;
static unsigned hook_count, stream_calls, guarded_read_calls;
static uint32_t stream_reader;
static int stream_fill, stream_action;
enum { FILL_ONLY, CHANGE_RING, CHANGE_GENERATION, DISABLE_DURING_READ, CHANGE_READY };
static int fake_load(void *self, const void *track);
static void fake_unload(void *self);
static uint32_t (*stock_model)(void *self, int32_t position, float *pcm, uint32_t frames);
static uint32_t fake_stream(void *self, int32_t position, void *buffer, uint32_t frames)
{
    ++stream_calls;
    if (stock_model) return stock_model(self, position, buffer, frames);
    if (stream_fill) {
        float *pcm = buffer;
        for (uint32_t i = 0; i < frames; ++i) {
            int64_t source = (int64_t)position + i;
            pcm[i * 2] = pcm[i * 2 + 1] = source >= 0 && source < 2 ? 0.75f : 0;
        }
        if (stream_action == CHANGE_RING)
            __atomic_store_n((uint32_t *)(uintptr_t)(stream_reader + 0x84), 3, __ATOMIC_RELAXED);
        if (stream_action == CHANGE_GENERATION)
            __atomic_add_fetch(&decks[0].generation, 1, __ATOMIC_SEQ_CST);
        if (stream_action == DISABLE_DURING_READ) xz_audio_set_enabled(0);
        if (stream_action == CHANGE_READY)
            __atomic_store_n((unsigned char *)(uintptr_t)(stream_reader + 0x69), 0, __ATOMIC_RELAXED);
    }
    return 0x12345678;
}
static void put(uint32_t at,uint32_t value) { memcpy(test_memory+at-0x10000,&value,4); }
int xz_read_memory(uint32_t at,void*out,size_t n) {
 ++guarded_read_calls;
 if(at==XZ126_PLAY_ENGINE_CELL && n==4) { uint32_t value=0x10000;memcpy(out,&value,4);return 0; }
 if(at<0x10000 || (uint64_t)at+n>0x20000)return -1;
 memcpy(out,test_memory+at-0x10000,n);return 0;
}
static unsigned recovery_logs;
static char recovery_log[256];
void xz_log(const char *s) {if(strstr(s,"preparation attempt=")){++recovery_logs;snprintf(recovery_log,sizeof recovery_log,"%s",s);}}
int xz_hook_arm(uint32_t address, const unsigned char guard[8], void *replacement, void **original)
{
    (void)guard;
    assert(replacement && original);
    switch (address) {
    case 0x34ba0: *original = (void *)fake_load; break;
    case 0x348dc: *original = (void *)fake_unload; break;
    case 0x8fd5c: *original = (void *)fake_stream; break;
    default: assert(0);
    }
    ++hook_count;
    return 0;
}
static int fake_load(void*self,const void*track) {
 (void)track;assert((uintptr_t)self==0x11200);
 assert(decks[0].generation==expect_generation);
 if(decks[0].published)assert(decks[0].published->generation!=decks[0].generation);
 strcpy((char*)test_memory+0x1300,"/media/usb/new.flac");return load_result;
}
static void fake_unload(void*self) {(void)self;assert(decks[0].generation==expect_generation);}
static void setup(void) {
 put(0x1000c,0x10100);put(0x10010,0x10108);put(0x10100,0x11000);put(0x10104,0x12000);
 put(0x110c8,0x11200);put(0x1112c,0x11200);put(0x11204,0x11300);put(0x1120c,44100);
 put(0x11994,0x11a00);put(0x11a14,12345);put(0x11a18,44100);
 strcpy((char*)test_memory+0x1300,"/media/usb/old.flac");
}

static struct deck_state concurrent;
static unsigned test_run, observed;
static void *race_reader(void *unused) {
 (void)unused;
 while(__atomic_load_n(&test_run,__ATOMIC_ACQUIRE)) {
  struct publication *p=acquire(&concurrent);
  if(p) { assert(p->generation==42); __atomic_fetch_add(&observed,1,__ATOMIC_RELAXED); release(&concurrent); }
 }
 return NULL;
}
static void race_test(void) {
 pthread_t a,b; unsigned i;
 __atomic_store_n(&test_run,1,__ATOMIC_RELEASE);
 assert(!pthread_create(&a,NULL,race_reader,NULL));assert(!pthread_create(&b,NULL,race_reader,NULL));
 for(i=0;i<1000;++i) {
  struct publication *p=calloc(1,sizeof(*p));assert(p);p->generation=42;
  __atomic_store_n(&concurrent.published,p,__ATOMIC_SEQ_CST);
  sched_yield();retire(&concurrent);while(drain(&concurrent,1000)){}
 }
 __atomic_store_n(&test_run,0,__ATOMIC_RELEASE);pthread_join(a,NULL);pthread_join(b,NULL);
 assert(observed>0 && concurrent.users==0 && !concurrent.published);
 puts("concurrent publication retirement passed with two readers");
}

/* A source callback that stays inside does not make the worker free or drop the old publication: it is kept on
 * the deck's draining list and freed on a later pass, after the reader leaves (LeakSanitizer checks the free). */
static void lingering_reader_test(void) {
 struct deck_state deck={0};
 struct publication *first=calloc(1,sizeof(*first)),*second=calloc(1,sizeof(*second));assert(first&&second);
 __atomic_store_n(&deck.published,first,__ATOMIC_SEQ_CST);
 assert(acquire(&deck)==first&&deck.users==1);
 retire(&deck);
 assert(!deck.published&&deck.draining==first&&deck.draining_said==1);
 assert(drain(&deck,5)&&deck.draining==first);
 /* The next track's publication is taken off too while the reader still holds the first. */
 __atomic_store_n(&deck.published,second,__ATOMIC_SEQ_CST);
 retire(&deck);
 assert(deck.draining==second&&second->next==first&&deck.draining_said==1);
 release(&deck);
 assert(!drain(&deck,0)&&!deck.draining&&!deck.draining_said&&!deck.users);
 puts("PASS a reader that does not drain keeps the old publication until it leaves; nothing is leaked or freed early");
}
static void disabled_start_test(void)
{
    struct xz_audio_status status;
    float untouched[] = {1, 2, 3, 4};
    _Static_assert(DECKS == 2, "Only two standalone internal decks are supported");
    assert(!enabled);
    assert(xz_audio_start() == 0);
    assert(hook_count == 3 && !enabled && worker_created);
    assert(xz_audio_get_status(0, &status) == 0 && status.state == XZ_AUDIO_DISABLED);
    assert(xz_audio_get_status(2, &status) == -1);
    assert(xz_audio_get_status(3, &status) == -1);
    xz_audio_set_levels(2, (struct xz_stem_levels){0, 0, 0});
    /* Deliberately invalid native pointer proves disabled execution never reads
     * native memory and forwards the original result without changing PCM. */
    assert(stream_hook((void *)(uintptr_t)0xdeadbeef, 0, untouched, 2) == 0x12345678);
    assert(stream_calls == 1 && untouched[0] == 1 && untouched[3] == 4);
    xz_audio_set_enabled(1);
    assert(xz_audio_get_status(0, &status) == 0 && status.state == XZ_AUDIO_WAITING_TRACK);
    xz_audio_stop();
    assert(!running && !enabled && !worker_created);
    puts("successful startup is disabled; unknown loaded tracks require reload; decks 3/4 rejected");
}

/* ARM32 stores native pointers in four bytes. Allocate an ordinary private
 * mapping below 4 GiB so the real hook's direct loads run unchanged on a 64-bit
 * Linux host. MAP_32BIT is used where available; elsewhere a low address hint
 * is accepted only if the kernel actually returns a suitable address. No fixed
 * mapping replaces an existing mapping. Failure is a failed test, not a skip.
 * The stock PcmReader is 0x140da08 bytes and the hook reads its cue states and
 * its last zero-fill position, so the fixture spans a whole reader. */
#define FIXTURE_BYTES ((size_t)0x1410000)
static unsigned char *low_native_fixture(void)
{
    int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
    unsigned char *memory;
#ifdef MAP_32BIT
    flags |= MAP_32BIT;
#endif
    memory = mmap((void *)(uintptr_t)0x20000000, FIXTURE_BYTES, PROT_READ | PROT_WRITE, flags, -1, 0);
    assert(memory != MAP_FAILED);
    assert((uintptr_t)memory >= 0x10000 && (uintptr_t)memory <= UINT32_MAX - FIXTURE_BYTES);
    return memory;
}

static void native_put(uint32_t at, uint32_t value)
{
    __atomic_store_n((uint32_t *)(uintptr_t)at, value, __ATOMIC_RELAXED);
}

static void ready_publication(struct publication *p, unsigned char *memory)
{
    uint32_t self = (uint32_t)(uintptr_t)memory;
    stream_reader = self + 0x100;
    memset(memory, 0, 4096);
    native_put(self + 4, stream_reader);
    native_put(stream_reader, PCM_READER_VPTR);
    native_put(stream_reader + 4, self + 0x300);
    native_put(stream_reader + 0x6c, 0);
    native_put(stream_reader + 0x84, 2);
    native_put(stream_reader + 0x88, 0);
    native_put(stream_reader + STOCK_MISS, UINT32_MAX); /* PcmReader constructor */
    for (unsigned k = 1; k < 9; ++k)
        native_put(stream_reader + STOCK_HOT_CUE + (k - 1) * STOCK_HOT_CUE_STRIDE, 0);
    *(unsigned char *)(uintptr_t)(stream_reader + 0x9c) = 1;
    *(unsigned char *)(uintptr_t)(stream_reader + 0x69) = 1;
    p->reader = stream_reader;
    __atomic_store_n(&decks[0].known_reader,stream_reader,__ATOMIC_RELEASE);
    p->reader_impl = self + 0x300;
    p->reader_frames = 2;
    p->generation = 42;
    __atomic_store_n(&decks[0].generation, 42, __ATOMIC_SEQ_CST);
    __atomic_store_n(&decks[0].published, p, __ATOMIC_SEQ_CST);
    __atomic_store_n(&running, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&enabled, 1, __ATOMIC_RELEASE);
    xz_audio_set_levels(0, (struct xz_stem_levels){0.5f, 1.5f, 0});
    stream_fill = 1;
    stream_action = FILL_ONLY;
}

static void exercise_enabled_stream(unsigned char *self, int32_t position, int expect_mix,
                                      const int16_t *h, const int16_t *v)
{
    float out[8];
    unsigned calls = stream_calls, reads = guarded_read_calls;
    for (unsigned i = 0; i < 8; ++i) out[i] = -12345;
    assert(stream_hook(self, position, out, 4) == 0x12345678);
    assert(stream_calls == calls + 1);
    assert(guarded_read_calls == reads); /* no guarded pread bridge on audio path */
    assert(decks[0].users == 0 && decks[1].users == 0);
    for (unsigned frame = 0; frame < 4; ++frame) {
        int64_t source = (int64_t)position + frame;
        for (unsigned channel = 0; channel < 2; ++channel) {
            float expected = 0;
            if (source >= 0 && source < 2) {
                expected = 0.75f;
                if (expect_mix) {
                    size_t k = (size_t)source * 2 + channel;
                    expected = 0.375f + (float)h[k] / 32767.0f -
                               0.5f * (float)v[k] / 32767.0f;
                }
            }
            assert(fabsf(out[frame * 2 + channel] - expected) < 1e-6f);
        }
    }
}

static void enabled_stream_test(void)
{
    static const int16_t h[] = {16384, -16384, 8192, -8192};
    static const int16_t v[] = {4096, 4096, -4096, -4096};
    unsigned char *memory = low_native_fixture();
    struct publication p = {0};
    assert(!worker_created);
    memset(decks, 0, sizeof(decks));
    p.decoded.pcm = (struct xz_stem_pcm){h, v, 2, 1, 1};

    ready_publication(&p, memory);
    exercise_enabled_stream(memory, 0, 1, h, v); /* mixed head, untouched zero tail */
    assert(decks[0].mixed == 1);
    exercise_enabled_stream(memory, -1, 1, h, v); /* leading zero and trailing zero */
    assert(decks[0].mixed == 2);

    /* The same native fixture can represent deck 2 only when its publication
     * belongs to deck 2. Deck 1's changed levels must not bleed into its unity. */
    __atomic_store_n(&decks[0].published, NULL, __ATOMIC_SEQ_CST);
    __atomic_store_n(&decks[1].generation, 42, __ATOMIC_SEQ_CST);
    __atomic_store_n(&decks[1].published, &p, __ATOMIC_SEQ_CST);
    __atomic_store_n(&decks[1].known_reader,p.reader,__ATOMIC_RELEASE);
    xz_audio_set_levels(1, (struct xz_stem_levels){1, 1, 1});
    exercise_enabled_stream(memory, 0, 0, h, v);
    xz_audio_set_levels(1, (struct xz_stem_levels){0.5f, 1.5f, 0});
    exercise_enabled_stream(memory, 0, 1, h, v);
    assert(decks[0].mixed == 2 && decks[1].mixed == 1);
    __atomic_store_n(&decks[1].published, NULL, __ATOMIC_SEQ_CST);

    ready_publication(&p,memory);
    __atomic_store_n(&decks[1].source_work,0,__ATOMIC_RELEASE);
    xz_audio_set_levels(0,(struct xz_stem_levels){1,1,1});
    exercise_enabled_stream(memory,0,0,h,v);
    assert(!(decks[0].source_work&1u));
    /* A neutral path must never touch publication/ring metadata. A new level
     * request must restore the real adapter before returning from the setter. */
    __atomic_store_n(&decks[0].published,(struct publication*)(uintptr_t)1,__ATOMIC_SEQ_CST);
    float untouched[8];assert(stream_hook((void*)(uintptr_t)1,0,untouched,4)==0x12345678);
    assert(untouched[0]==.75f&&untouched[4]==0);
    __atomic_store_n(&decks[0].published,&p,__ATOMIC_SEQ_CST);
    xz_audio_set_levels(0,(struct xz_stem_levels){.5f,1.5f,0});
    assert(decks[0].source_work&1u);exercise_enabled_stream(memory,0,1,h,v);
    decks[0].mixed=2;

    struct xz_audio_batch batch,nested;xz_audio_batch_begin(0,0,&batch);assert(decks[0].users==1);
    float batched[8];assert(stream_hook(memory,0,batched,4)==0x12345678);assert(decks[0].users==1);
    xz_audio_batch_begin(-1,0,&nested);assert(source_batch==&nested);xz_audio_batch_end(&nested);assert(source_batch==&batch);
    __atomic_add_fetch(&decks[0].generation,1,__ATOMIC_SEQ_CST);
    assert(stream_hook(memory,0,batched,4)==0x12345678);assert(batched[0]==.75f);
    xz_audio_batch_end(&batch);assert(!source_batch&&!decks[0].users);decks[0].mixed=2;

    ready_publication(&p, memory);
    *(unsigned char *)(uintptr_t)(stream_reader + 0x69) = 0;
    exercise_enabled_stream(memory, 0, 0, h, v);
    ready_publication(&p, memory);
    *(unsigned char *)(uintptr_t)(stream_reader + 0x9c) = 0;
    exercise_enabled_stream(memory, 0, 0, h, v);
    ready_publication(&p, memory);
    native_put(stream_reader + 0x84, 1); /* only part of requested source is buffered */
    exercise_enabled_stream(memory, 0, 0, h, v);
    ready_publication(&p, memory);
    native_put(stream_reader + 4, p.reader_impl + 4); /* wrong track owner */
    exercise_enabled_stream(memory, 0, 0, h, v);
    ready_publication(&p, memory);
    native_put(stream_reader, PCM_READER_VPTR + 4); /* unexpected native reader class */
    exercise_enabled_stream(memory, 0, 0, h, v);
    ready_publication(&p, memory);
    __atomic_add_fetch(&decks[0].generation, 1, __ATOMIC_SEQ_CST);
    exercise_enabled_stream(memory, 0, 0, h, v); /* stale before original read */

    ready_publication(&p, memory); stream_action = CHANGE_RING;
    exercise_enabled_stream(memory, 0, 1, h, v); /* window grew during the read: span still covered, mixed */
    assert(decks[0].mixed == 3); COUNTERS(assert(decks[0].moved == 1));
    ready_publication(&p, memory); stream_action = CHANGE_READY;
    exercise_enabled_stream(memory, 0, 0, h, v);
    ready_publication(&p, memory); stream_action = CHANGE_GENERATION;
    exercise_enabled_stream(memory, 0, 0, h, v);
    ready_publication(&p, memory); stream_action = DISABLE_DURING_READ;
    exercise_enabled_stream(memory, 0, 0, h, v);
    assert(!enabled && decks[0].mixed == 3);

    __atomic_store_n(&decks[0].published, NULL, __ATOMIC_SEQ_CST);
    __atomic_store_n(&running, 0, __ATOMIC_RELEASE);
    stream_fill = 0;
    assert(munmap(memory, FIXTURE_BYTES) == 0);
    memset(decks, 0, sizeof(decks));
    puts("real enabled stream hook mixed expected samples; preserved return and zero fill; readiness, identity, generation and disable races bypassed");
}

/* A model of the stock read under the hook, at a beat jump. TimeStretch::getStreamAt (0x8fd5c)
 * zero-fills outside the track; PcmReader::getStreamAt (0x38d28) serves the first of: a stored
 * cue buffer (isCueBufferedAt 0x366dc), the ring (readRingBuffer 0x361dc, slot by the base at
 * +0x70/+0x78), else zero-fill and record the position at +0x140da00. The decoder thread
 * (PcmReader::run) changes the ring while the audio thread reads: stock_event runs before the
 * stock's own check (early) or after its copy (late), the two ends of that race. */
#define TRACK_FRAMES 65536u
#define JUMP_BLOCK 256u
static int16_t track_h[TRACK_FRAMES * 2], track_v[TRACK_FRAMES * 2];
enum { EVENT_NONE, EVENT_APPEND, EVENT_RECENTRE, EVENT_PREPARE, EVENT_RESET, EVENT_CUE_STORED };
static int stock_event, stock_event_late;
static uint32_t stock_event_value;
static uint32_t stock_word(uint32_t at) { return *(const uint32_t *)(uintptr_t)at; }
static void stock_put(uint32_t at, uint32_t value) { *(uint32_t *)(uintptr_t)at = value; }
static float track_sample(int64_t q, unsigned channel)
{
    return q < 0 || q >= TRACK_FRAMES ? 0 : 0.25f + 0.5f * (float)((q * 7 + channel * 3) % 101) / 101.0f;
}
static void stock_decoder_event(void)
{
    uint32_t r = stream_reader, d = stock_event_value;
    switch (stock_event) {
    case EVENT_APPEND: /* updateWritePoint 0x37bf8: ahead grows by one decode pass */
        stock_put(r + 0x84, stock_word(r + 0x84) + 10240); break;
    case EVENT_RECENTRE: /* updateReadPoint -> moveReadPoint 0x36384, forward, store order kept */
        stock_put(r + 0x74, (stock_word(r + 0x74) + d) % STOCK_RING_SLOTS);
        stock_put(r + 0x84, stock_word(r + 0x84) - d);
        stock_put(r + 0x6c, stock_word(r + 0x6c) + d);
        stock_put(r + 0x88, stock_word(r + 0x88) + d); break;
    case EVENT_PREPARE: /* prepare 0x3479c from the Player thread (waitForPcmReaderSync) */
        stock_put(r + 0x70, stock_word(r + 0x6c)); stock_put(r + 0x78, stock_word(r + 0x74)); break;
    case EVENT_RESET: /* updateReadPoint 0x36510: read point outside the window */
        stock_put(r + 0x84, 0); stock_put(r + 0x88, 0); stock_put(r + 0x74, 0); stock_put(r + 0x6c, d); break;
    case EVENT_CUE_STORED: /* hot cue A finishes its copy */
        stock_put(r + STOCK_HOT_CUE, 2); break;
    }
}
static int stock_cue_covers(uint32_t r, unsigned k, uint32_t pos, uint32_t n)
{
    uint32_t at = k ? r + STOCK_HOT_CUE + (k - 1) * STOCK_HOT_CUE_STRIDE : r + STOCK_CURRENT_CUE;
    uint32_t state = stock_word(at), cue = stock_word(at + 4);
    int32_t low = (int32_t)(cue - 97020u);
    uint32_t first = low < 0 ? 0 : (uint32_t)low, last = cue + (k ? 97019u : 449819u);
    return state == (k ? 2u : 3u) && first <= pos && last >= pos && pos - 1 + n <= last;
}
static void stock_pcm_reader(uint32_t r, uint32_t pos, float *out, uint32_t n)
{
    if (!*(unsigned char *)(uintptr_t)(r + 0x9c) || !*(unsigned char *)(uintptr_t)(r + 0x69)) {
        memset(out, 0, (size_t)n * 8); return;
    }
    for (unsigned k = 0; k < 9; ++k)
        if (stock_cue_covers(r, k, pos, n)) {
            for (uint32_t i = 0; i < n; ++i) { out[i * 2] = track_sample(pos + i, 0); out[i * 2 + 1] = track_sample(pos + i, 1); }
            return;
        }
    int32_t low = (int32_t)(stock_word(r + 0x6c) - stock_word(r + 0x88));
    uint32_t first = low < 0 ? 0 : (uint32_t)low, last = stock_word(r + 0x6c) + stock_word(r + 0x84);
    if (pos >= first && pos <= last && pos + n <= last) {
        for (uint32_t i = 0; i < n; ++i) {
            /* The slot the read uses, and the position the decoder wrote there. */
            int64_t slot = ((int64_t)pos + i - stock_word(r + 0x70) + stock_word(r + 0x78)) % STOCK_RING_SLOTS;
            if (slot < 0) slot += STOCK_RING_SLOTS;
            int64_t q = slot - (int64_t)stock_word(r + 0x74) + stock_word(r + 0x6c);
            while (q - ((int64_t)pos + i) > STOCK_RING_SLOTS / 2) q -= STOCK_RING_SLOTS;
            while (((int64_t)pos + i) - q > STOCK_RING_SLOTS / 2) q += STOCK_RING_SLOTS;
            out[i * 2] = track_sample(q, 0); out[i * 2 + 1] = track_sample(q, 1);
        }
        return;
    }
    if (stock_word(r + STOCK_MISS) != pos) stock_put(r + STOCK_MISS, pos); /* and a jumpPosition request */
    memset(out, 0, (size_t)n * 8);
}
static uint32_t stock_get_stream_at(void *self, int32_t position, float *pcm, uint32_t frames)
{
    uint32_t r = stock_word((uint32_t)(uintptr_t)self + 4);
    int64_t first = position < 0 ? 0 : position, last = (int64_t)position + frames;
    if (last > TRACK_FRAMES) last = TRACK_FRAMES;
    memset(pcm, 0, (size_t)frames * 8);
    if (!stock_event_late) stock_decoder_event();
    if (last > first) stock_pcm_reader(r, (uint32_t)first, pcm + (first - position) * 2, (uint32_t)(last - first));
    if (stock_event_late) stock_decoder_event();
    return frames;
}

enum jump_expect { EXPECT_MIXED, EXPECT_TRACK, EXPECT_ZERO, EXPECT_STALE };
struct jump_case {
    const char *name;
    uint32_t center, behind, ahead, base; /* ring window; base: the position whose slot is 0 */
    int stale;                            /* window reset (center in slot 0), reads still on base */
    uint32_t miss, cue_state, cue_position; int cue_kind; /* 0 current cue, 1 hot cue A, -1 none */
    int event, late; uint32_t event_value;
    int32_t position; enum jump_expect expect; int counter; /* 0 moved 1 cue 2 miss 3 unproven -1 none */
};

static void jump_case_run(unsigned char *memory, const struct publication *p, const struct jump_case *c)
{
    uint32_t r = stream_reader;
    stock_put(r + 0x6c, c->center); stock_put(r + 0x88, c->behind); stock_put(r + 0x84, c->ahead);
    stock_put(r + 0x70, c->base); stock_put(r + 0x78, 0);
    stock_put(r + 0x74, c->stale ? 0 :
              (uint32_t)((((int64_t)c->center - c->base) % STOCK_RING_SLOTS + STOCK_RING_SLOTS) % STOCK_RING_SLOTS));
    stock_put(r + STOCK_MISS, c->miss);
    stock_put(r + STOCK_CURRENT_CUE, 0); stock_put(r + STOCK_HOT_CUE, 0);
    if (c->cue_kind >= 0) {
        uint32_t at = c->cue_kind ? r + STOCK_HOT_CUE : r + STOCK_CURRENT_CUE;
        stock_put(at, c->cue_state); stock_put(at + 4, c->cue_position);
    }
    stock_event = c->event; stock_event_late = c->late; stock_event_value = c->event_value;
    float out[JUMP_BLOCK * 2], expected[JUMP_BLOCK * 2];
    for (uint32_t i = 0; i < JUMP_BLOCK; ++i) {
        int64_t q = (int64_t)c->position + i;
        if (c->expect == EXPECT_STALE) q += (int64_t)c->center - c->base; /* old base, new window */
        expected[i * 2] = c->expect == EXPECT_ZERO ? 0 : track_sample(q, 0);
        expected[i * 2 + 1] = c->expect == EXPECT_ZERO ? 0 : track_sample(q, 1);
    }
    if (c->expect == EXPECT_MIXED)
        assert(xz_stem_mix(expected, JUMP_BLOCK, c->position, &p->decoded.pcm, get_levels(&decks[0])) == JUMP_BLOCK);
    uint32_t mixed = decks[0].mixed, skipped = decks[0].skipped;
#ifndef OLD_HOOK
    uint32_t counters[4] = {decks[0].moved, decks[0].cue, decks[0].miss, decks[0].unproven};
#endif
    assert(stream_hook(memory, c->position, out, JUMP_BLOCK) == JUMP_BLOCK);
    int wrong = 0;
    for (uint32_t i = 0; i < JUMP_BLOCK * 2; ++i) wrong |= fabsf(out[i] - expected[i]) > 1e-6f;
    if (wrong) {
        fprintf(stderr, "FAIL jump case %s: %s\n", c->name,
                c->expect == EXPECT_MIXED ? "block not mixed" : "block changed");
        assert(0);
    }
    assert(decks[0].mixed == mixed + (c->expect == EXPECT_MIXED));
    assert(decks[0].skipped == skipped + (c->expect != EXPECT_MIXED));
#ifndef OLD_HOOK
    uint32_t now[4] = {decks[0].moved, decks[0].cue, decks[0].miss, decks[0].unproven};
    for (unsigned k = 0; k < 4; ++k)
        if (now[k] != counters[k] + ((int)k == c->counter)) {
            fprintf(stderr, "FAIL jump case %s: counter %u\n", c->name, k);
            assert(0);
        }
#endif
    printf("jump case %s: %s\n", c->name, c->expect == EXPECT_MIXED ? "mixed" : "left as stock");
}

static void jump_ring_test(void)
{
    unsigned char *memory = low_native_fixture();
    struct publication p = {0};
    memset(decks, 0, sizeof(decks));
    for (uint32_t i = 0; i < TRACK_FRAMES * 2; ++i) {
        track_h[i] = (int16_t)((int32_t)((i * 37u) % 20000u) - 10000);
        track_v[i] = (int16_t)((int32_t)((i * 53u) % 16000u) - 8000);
    }
    p.decoded.pcm = (struct xz_stem_pcm){track_h, track_v, TRACK_FRAMES, 1, 1};
    ready_publication(&p, memory);
    p.reader_frames = TRACK_FRAMES;
    xz_audio_set_levels(0, (struct xz_stem_levels){1, 1, 0}); /* the vocal stem down to 0 */
    stock_model = stock_get_stream_at;
    /* A block is 256 frames. Playing at 20000 with 5000 frames behind and 20000 ahead. */
    static const struct jump_case cases[] = {
        {"steady", 20000, 5000, 20000, 0, 0, UINT32_MAX, 0, 0, -1, EVENT_NONE, 0, 0, 20000, EXPECT_MIXED, -1},
        /* After a jump inside the window the decoder refills at full speed while the audio reads. */
        {"decoder appends during read", 30000, 15000, 10000, 0, 0, UINT32_MAX, 0, 0, -1, EVENT_APPEND, 1, 0, 30000, EXPECT_MIXED, 0},
        {"decoder recentres during read", 20000, 5000, 20000, 0, 0, UINT32_MAX, 0, 0, -1, EVENT_RECENTRE, 1, 10240, 30240, EXPECT_MIXED, 0},
        {"decoder recentres before the stock looks", 20000, 5000, 20000, 0, 0, UINT32_MAX, 0, 0, -1, EVENT_RECENTRE, 0, 10240, 30240, EXPECT_MIXED, 0},
        {"player thread prepare during read", 20000, 5000, 20000, 0, 0, UINT32_MAX, 0, 0, -1, EVENT_PREPARE, 1, 0, 20000, EXPECT_MIXED, -1},
        /* A jump outside the window: the ring was reset at the target and holds nothing yet. */
        {"hot cue serves while the ring refills", 50000, 0, 0, 50000, 0, UINT32_MAX, 2, 50000, 1, EVENT_APPEND, 1, 0, 50000, EXPECT_MIXED, 1},
        {"current cue serves a loop return", 40000, 0, 0, 40000, 0, UINT32_MAX, 3, 2000, 0, EVENT_NONE, 0, 0, 2100, EXPECT_MIXED, 1},
        {"current cue still copying", 40000, 0, 0, 40000, 0, UINT32_MAX, 1, 2000, 0, EVENT_NONE, 0, 0, 2100, EXPECT_ZERO, 2},
        {"hot cue stored during read", 50000, 0, 0, 50000, 0, UINT32_MAX, 1, 50000, 1, EVENT_CUE_STORED, 1, 0, 50000, EXPECT_ZERO, 2},
        {"nothing covers: stock zero-fills", 50000, 0, 0, 50000, 0, UINT32_MAX, 0, 0, -1, EVENT_NONE, 0, 0, 50000, EXPECT_ZERO, 2},
        {"nothing covers again at the same position", 50000, 0, 0, 50000, 0, 50000, 0, 0, -1, EVENT_NONE, 0, 0, 50000, EXPECT_ZERO, 2},
        {"earlier miss here, ring covers now", 50000, 0, 10000, 50000, 0, 50000, 0, 0, -1, EVENT_NONE, 0, 0, 50000, EXPECT_MIXED, -1},
        /* The read point left the window during the read. The stock copied valid audio before the
           reset, but the snapshots cannot show that, so the block is left as stock. */
        {"ring reset during read", 20000, 5000, 20000, 0, 0, UINT32_MAX, 0, 0, -1, EVENT_RESET, 1, 60000, 20000, EXPECT_TRACK, 3},
        /* Reset done, prepare() not yet run: the window covers but the reads use the old base and
           return other audio. Mixing stems into it would be wrong. */
        {"stale base after a reset", 30000, 0, 20000, 0, 1, UINT32_MAX, 0, 0, -1, EVENT_NONE, 0, 0, 30000, EXPECT_STALE, 3},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) jump_case_run(memory, &p, &cases[i]);
    stock_model = NULL; stock_event = EVENT_NONE;
    __atomic_store_n(&decks[0].published, NULL, __ATOMIC_SEQ_CST);
    __atomic_store_n(&running, 0, __ATOMIC_RELEASE);
    stream_fill = 0;
    assert(munmap(memory, FIXTURE_BYTES) == 0);
    memset(decks, 0, sizeof(decks));
    puts("PASS stock ring at a jump: moved window and cue buffers mixed; zero-fill, reset and stale base left as stock");
}

int main(void) {
 {struct xz_oc_open_diagnostics d={1,{"STEM WINDOW ALLOCATION FAILED: window=0 role=1 bytes=1536000 errno=12"}};
 struct cancel_context c={&decks[1],decks[1].generation};
 int old_enabled=enabled,old_running=running;enabled=running=1;
 log_preparation_recovery(1,c.generation,&d,1,&c);
 assert(recovery_logs==1&&strstr(recovery_log,"deck=2 generation=")&&strstr(recovery_log,"recovered")&&strstr(recovery_log,"errno=12"));
 ++decks[1].generation;log_preparation_recovery(1,c.generation,&d,1,&c);assert(recovery_logs==1);
 decks[1].generation=c.generation;enabled=old_enabled;running=old_running;puts("PASS worker recovery logging preserves original stage with deck/generation and suppresses cancelled-generation records");}

 disabled_start_test();
 enabled_stream_test();
 jump_ring_test();
 race_test();
 lingering_reader_test();
 struct publication p={0};struct publication *held;uint32_t output_rate,output_generation;
 assert(xz_audio_lifecycle_self_test()==0);
 setup();decks[0].generation=p.generation=7;decks[0].published=&p;
 assert(xz_audio_output_deck((void*)(uintptr_t)0x110d0,&output_rate,&output_generation)==-1);
 original_load=fake_load;original_unload=fake_unload;running=1;enabled=1;
 held=acquire(&decks[0]);assert(held==&p);
 expect_generation=8;assert(load_hook((void*)(uintptr_t)0x11200,NULL)==0);
 assert(decks[0].pending_kind==1 && decks[0].pending_generation==8);
 assert(!strcmp(decks[0].pending.path,"/media/usb/new.flac"));
 assert(xz_audio_output_deck((void*)(uintptr_t)0x110d0,&output_rate,&output_generation)==0);
 assert(output_rate==44100 && output_generation==1);
 assert(held->generation!=decks[0].generation);release(&decks[0]);
 expect_generation=9;load_result=-5;assert(load_hook((void*)(uintptr_t)0x11200,NULL)==-5);
 assert(decks[0].pending_kind==2 && decks[0].status.state==XZ_AUDIO_WAITING_TRACK);
 assert(xz_audio_output_deck((void*)(uintptr_t)0x110d0,&output_rate,&output_generation)==-1);
 expect_generation=10;unload_hook((void*)(uintptr_t)0x11200);
 assert(decks[0].pending_kind==2 && decks[0].status.state==XZ_AUDIO_WAITING_TRACK);
 assert(__atomic_exchange_n(&decks[0].published,NULL,__ATOMIC_SEQ_CST)==&p && decks[0].users==0);
 assert(acquire(&decks[0])==NULL);
 xz_audio_set_enabled(0);load_result=0;
 expect_generation=12;assert(load_hook((void*)(uintptr_t)0x11200,NULL)==0);
 assert(decks[0].history_valid && decks[0].pending_kind==2 && !decks[0].published);
 assert(decks[0].status.state==XZ_AUDIO_DISABLED);
 assert(xz_audio_output_deck((void*)(uintptr_t)0x110d0,&output_rate,&output_generation)==0);
 assert(output_rate==44100 && output_generation==4);
 xz_audio_set_enabled(1);assert(decks[0].pending_kind==1 && decks[0].pending_generation==12);
 xz_audio_set_levels(0,(struct xz_stem_levels){0,0.5f,2});
 xz_audio_set_enabled(0);assert(decks[0].pending_kind==2 && decks[0].pending_generation==13);
 assert(decks[0].history_valid && get_levels(&decks[0]).drums==1);
 assert(xz_audio_output_deck((void*)(uintptr_t)0x110d0,&output_rate,&output_generation)==0);
 assert(output_rate==44100 && output_generation==4);
 puts("disabled loads retain history without decode; enable queues known history; disable cancels and restores unity");
 puts("mocked native load/unload cancellation precedes original; captured new path; failed load never publishes");
 setup();decks[0].generation=41;original_load=fake_load;running=enabled=1;load_result=0;expect_generation=42;
 probe_control=1;probe_deferred=0;assert(load_hook((void*)(uintptr_t)0x11200,NULL)==0);probe_control=0;
 assert(probe_generation==42);
 if(probe_deferred)xz_audio_set_levels(0,(struct xz_stem_levels){1,1,0});
 if(get_levels(&decks[0]).vocals!=0){fprintf(stderr,"FAIL native load reset erased a control on newly published generation\n");assert(0);}
 const float stale_values[4]={0,0,0,0};
 assert(xz_audio_set_levels4_for_generation(0,41,stale_values)==-1);
 assert(get_levels(&decks[0]).drums==1&&get_levels(&decks[0]).vocals==0);
 assert(xz_audio_set_levels4_for_generation(0,42,stale_values)==0&&get_levels(&decks[0]).drums==0);
 puts("PASS native load generation publication cannot erase a new-generation stem control");

 return 0;
}
