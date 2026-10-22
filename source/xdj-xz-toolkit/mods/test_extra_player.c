// SPDX-License-Identifier: GPL-2.0-only
/* DECK 3 / DECK 4 players against a fake transport and synthetic analysis files.
   Usage: test-extra-player FIXTURE_DIR (holding 44100.wav and 48000.wav, 2 s stereo sine each). */
#define _GNU_SOURCE
#ifdef NDEBUG
#error "Extra player acceptance requires active assertions"
#endif
#include "extra_player.h"
#include "anlz_extra.h"
#include "ui/layered_wave_source.h"
#include "sync_native.h"
#ifdef __linux__
#include "clock_compat.h"
#endif
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef __linux__
#include <sys/syscall.h>
#include <unistd.h>
#endif

/* ---- fake transport: queues per deck, generations applied like the USB worker applies them ---- */
#define FAKE_BLOCKS 32
struct block { uint32_t generation, frames; uint64_t after; };
static struct {
 pthread_mutex_t lock;
 uint32_t link, queue_generation[2];
 xz_usb_position position;
 struct block queue[2][FAKE_BLOCKS]; unsigned read[2], write[2];
 unsigned submits[2], flushes[2], stale;
 uint64_t last_after[2]; uint32_t last_frames[2];
 double credit[2];
 double wire[2];
 uint64_t activate_at[2];
 float peak[2];
 int capture[2]; uint32_t captured_frames[2]; float captured[2][XZ_USB_BLOCK_FRAMES*2];
} f;
/* ui/layered_wave_source.c links the stem loader; the players read no stems. */
int xz_oc_bundle(const char *source, char *folder, size_t capacity) { (void)source; (void)folder; (void)capacity; return -1; }
static uint32_t clock_ms = 1000;
static int threaded_clock;
static int hold_flush;
static unsigned position_interval,position_calls[2];
static int position_blocked,position_noise,stepping_side;
static unsigned wire_horizon;
static unsigned entered_worker[2];
static pthread_t owner_thread;
static void worker_enter_test(int deck) {
 assert(!pthread_equal(pthread_self(), owner_thread));
 __atomic_fetch_add(&entered_worker[deck], 1, __ATOMIC_RELAXED);
}

static uint32_t fake_link(void *ctx) { (void)ctx; return __atomic_load_n(&f.link, __ATOMIC_ACQUIRE); }
static xz_usb_result fake_submit_locked(void *ctx, int d, uint32_t generation, const float *pcm, uint32_t frames, uint64_t after) {
 (void)ctx;
 xz_usb_result r = XZ_USB_OK;
 assert(frames >= 1 && frames <= XZ_USB_BLOCK_FRAMES);
 if (!f.link) r = XZ_USB_STOPPED;
 else if (generation != f.queue_generation[d]) { r = XZ_USB_STALE; f.stale++; }
 else if (f.write[d] - f.read[d] >= FAKE_BLOCKS) r = XZ_USB_AGAIN;
 else {
  for (uint32_t i = 0; i < frames * 2; ++i) {
   assert(isfinite(pcm[i]) && fabsf(pcm[i]) <= 1.0f);
   if (fabsf(pcm[i]) > f.peak[d]) f.peak[d] = fabsf(pcm[i]);
  }
  f.queue[d][f.write[d]++ % FAKE_BLOCKS] = (struct block){generation, frames, after};
  if(f.capture[d]){
   memcpy(f.captured[d],pcm,frames*2*sizeof(float));f.captured_frames[d]=frames;f.capture[d]=0;
  }
  f.submits[d]++; f.last_after[d] = after; f.last_frames[d] = frames;
 }
 return r;
}
static xz_usb_result fake_submit(void *ctx, uint32_t epoch, int d, uint32_t generation, const float *pcm, uint32_t frames, uint64_t after) {
 pthread_mutex_lock(&f.lock);
 xz_usb_result result = epoch && epoch == f.link ? fake_submit_locked(ctx,d,generation,pcm,frames,after) : XZ_USB_STOPPED;
 pthread_mutex_unlock(&f.lock);return result;
}
static xz_usb_result fake_transition(void *ctx,uint32_t epoch,int d,uint32_t expected,const float *pcm,uint32_t frames,uint64_t after,uint32_t *generation) {
 pthread_mutex_lock(&f.lock);
 xz_usb_result result = !epoch || epoch != f.link ? XZ_USB_STOPPED : expected != f.queue_generation[d] ? XZ_USB_STALE
  : f.write[d]-f.read[d]>=FAKE_BLOCKS ? XZ_USB_AGAIN : XZ_USB_OK;
 if(result==XZ_USB_OK) {
  uint32_t next=expected+1;f.queue_generation[d]=next;
  f.activate_at[d]=(uint64_t)f.wire[d]+wire_horizon;
  result=fake_submit_locked(ctx,d,next,pcm,frames,after);assert(result==XZ_USB_OK);
  f.flushes[d]++;*generation=next;
 }
 pthread_mutex_unlock(&f.lock);return result;
}
static xz_usb_result fake_flush(void *ctx, uint32_t epoch, int d, uint32_t *generation) {
 if (hold_flush) return XZ_USB_BUSY;
 (void)ctx;
 pthread_mutex_lock(&f.lock);
 if (!epoch || epoch != f.link) { pthread_mutex_unlock(&f.lock); return XZ_USB_STOPPED; }
 *generation = ++f.queue_generation[d]; f.flushes[d]++;
 f.activate_at[d]=(uint64_t)f.wire[d]+wire_horizon;
 pthread_mutex_unlock(&f.lock);
 return XZ_USB_OK;
}
static xz_usb_result fake_position(void *ctx, uint32_t epoch, xz_usb_position *out) {
 (void)ctx;
 pthread_mutex_lock(&f.lock);
 if (!epoch || epoch != f.link) { pthread_mutex_unlock(&f.lock); return XZ_USB_STOPPED; }
 unsigned call=++position_calls[stepping_side];
 if(position_blocked||(position_interval&&call%position_interval)){pthread_mutex_unlock(&f.lock);return XZ_USB_BUSY;}
 *out = f.position; out->state = XZ_USB_RUNNING;out->requested_generation[0]=f.queue_generation[0];out->requested_generation[1]=f.queue_generation[1];
 out->wire_submitted_frames=(uint64_t)f.wire[stepping_side]+wire_horizon;
 if(position_noise)for(int i=0;i<2;i++)if(out->deck[i].source_after_completed>1)
  out->deck[i].source_after_completed+=(i+(int)call)&1?1:UINT64_MAX;
 pthread_mutex_unlock(&f.lock);
 return XZ_USB_OK;
}
/* USB time passing: whole blocks of the applied generation complete, older ones are discarded. */
static void consume_chunk(int d,double frames,int apply){
 xz_usb_deck_position *p = &f.position.deck[d];
 if (apply&&p->applied_generation != f.queue_generation[d]) {
  memset(p, 0, sizeof *p); p->applied_generation = f.queue_generation[d];
  if(wire_horizon)f.credit[d]=0; /* old partial wire PCM ends at the committed generation boundary */
 }
 f.credit[d] += frames;
 f.wire[d]+=frames;f.position.wire_completed_frames=(uint64_t)f.wire[d];
 while (f.read[d] != f.write[d]) {
  struct block *b = &f.queue[d][f.read[d] % FAKE_BLOCKS];
  if (b->generation < p->applied_generation) { f.read[d]++; continue; }
  if (b->generation > p->applied_generation || f.credit[d] < b->frames) break;
  f.credit[d] -= b->frames; f.read[d]++;
  p->staged_frames += b->frames; p->completed_frames += b->frames;
  p->source_after_staged = p->source_after_completed = b->after; p->source_valid = 3;
  p->source_staged_output_frames=p->source_completed_output_frames=p->completed_frames;
  p->source_staged_wire_frame=p->source_completed_wire_frame=(uint64_t)(f.wire[d]-f.credit[d]+.00001);
 }
 if (f.read[d] == f.write[d]) {p->underrun_frames+=(uint64_t)f.credit[d];f.credit[d] = 0;}
}
static void consume(int d,double frames){
 pthread_mutex_lock(&f.lock);
 while(frames>.000001){
  int pending=f.position.deck[d].applied_generation!=f.queue_generation[d];
  double until=(double)f.activate_at[d]-f.wire[d];
  double part=pending&&wire_horizon&&until>0&&until<frames?until:frames;
  int apply=!pending||!wire_horizon||until<=0;
  consume_chunk(d,part,apply);frames-=part;
 }
 pthread_mutex_unlock(&f.lock);
}
static void fake_route(uint32_t link) {
 pthread_mutex_lock(&f.lock);
 f.link = link;
 for (int d = 0; d < 2; ++d) {
  f.queue_generation[d] = XZ_USB_INITIAL_GENERATION; f.read[d] = f.write[d] = 0; f.credit[d] = f.wire[d] = 0;f.activate_at[d]=0;
  memset(&f.position.deck[d], 0, sizeof f.position.deck[d]); f.position.deck[d].applied_generation = XZ_USB_INITIAL_GENERATION;
 }
 pthread_mutex_unlock(&f.lock);
}

static uint32_t reconnect_on_render;
void xz_xp_test_render_hook(void) {
 if (reconnect_on_render) {
  uint32_t epoch = reconnect_on_render; reconnect_on_render = 0;
  fake_route(epoch);
 }
}

static uint32_t real_ms(void) {
 struct timespec t;
#ifdef __linux__
 assert(!xz_clock_gettime_compat(CLOCK_MONOTONIC,&t));
#else
 timespec_get(&t, TIME_UTC);
#endif
 return (uint32_t)((uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u);
}
static uint32_t now_ms(void) { return threaded_clock ? real_ms() : clock_ms; }

/* ---- tracks and analysis ---- */
static char fixtures[380], anlz_dir[400];
static int32_t master;
static int32_t master_bpm(void) { return master; }
static int resolve(int source, uint32_t id, struct xz_extra_track *out) {
 memset(out, 0, sizeof *out);
 out->source = source; out->track_id = id;
 switch (id) {
 case 1:
  snprintf(out->path, sizeof out->path, "%s/44100.wav", fixtures);
  snprintf(out->anlz_dir, sizeof out->anlz_dir, "%s", anlz_dir);
  snprintf(out->title, sizeof out->title, "Track A"); snprintf(out->artist, sizeof out->artist, "Artist A");
  snprintf(out->key, sizeof out->key, "Fm"); out->track_bpm_x100 = 12000;
  return 0;
 case 2:
  snprintf(out->path, sizeof out->path, "%s/48000.wav", fixtures);
  snprintf(out->title, sizeof out->title, "Track B");
  return 0;
 case 3:
  snprintf(out->path, sizeof out->path, "%s/missing.wav", fixtures);
  snprintf(out->title, sizeof out->title, "Missing");
  return 0;
 case 4:
  snprintf(out->path, sizeof out->path, "%s/44100.aiff", fixtures);
  snprintf(out->title, sizeof out->title, "AIFF");
  out->track_bpm_x100 = 12000;
  return 0;
 default: return -1;
 }
}

struct buf { uint8_t b[65536]; uint32_t n; };
static void u8(struct buf *b, uint8_t v) { b->b[b->n++] = v; }
static void u16(struct buf *b, uint32_t v) { u8(b, (uint8_t)(v >> 8)); u8(b, (uint8_t)v); }
static void u32(struct buf *b, uint32_t v) { u16(b, v >> 16); u16(b, v & 0xffff); }
static void set32(struct buf *b, uint32_t at, uint32_t v) { b->b[at] = (uint8_t)(v >> 24); b->b[at+1] = (uint8_t)(v >> 16); b->b[at+2] = (uint8_t)(v >> 8); b->b[at+3] = (uint8_t)v; }
static uint32_t open_section(struct buf *b, const char tag[4], uint32_t head) {
 uint32_t at = b->n;
 memcpy(b->b + b->n, tag, 4); b->n += 4; u32(b, head); u32(b, 0);
 return at;
}
static void close_section(struct buf *b, uint32_t at) { set32(b, at + 8, b->n - at); }
static void pad_to(struct buf *b, uint32_t at) { while (b->n < at) u8(b, 0); }
static void file_start(struct buf *b) { b->n = 0; memcpy(b->b, "PMAI", 4); b->n = 4; u32(b, 28); u32(b, 0); pad_to(b, 28); }
static void file_end(struct buf *b) { set32(b, 8, b->n); }
static void pcpt(struct buf *b, uint32_t hot, uint8_t type, uint32_t ms) {
 uint32_t e = open_section(b, "PCPT", 28);
 u32(b, hot); u32(b, 4); u32(b, 0x10000); u16(b, 0xffff); u16(b, 0xffff);
 u8(b, type); u8(b, 0); u16(b, 1000); u32(b, ms); u32(b, 0xffffffffu); pad_to(b, e + 56);
 close_section(b, e);
}
static void pcob(struct buf *b, uint32_t list, uint32_t hot, uint8_t type, uint32_t ms) {
 uint32_t s = open_section(b, "PCOB", 24);
 u32(b, list); u16(b, 0); u16(b, 1); u32(b, 0xffffffffu);
 pcpt(b, hot, type, ms);
 close_section(b, s);
}
static void pcp2(struct buf *b, uint32_t hot, uint8_t type, uint32_t ms, uint32_t rgb) {
 uint32_t e = open_section(b, "PCP2", 16);
 u32(b, hot); u8(b, type); u8(b, 0); u16(b, 0); u32(b, ms); u32(b, 0xffffffffu);
 u8(b, 0); pad_to(b, e + 40);
 u32(b, 4); u16(b, 'A'); u16(b, 0);
 u8(b, 1); u8(b, (uint8_t)(rgb >> 16)); u8(b, (uint8_t)(rgb >> 8)); u8(b, (uint8_t)rgb);
 pad_to(b, b->n + 4);
 close_section(b, e);
}
static void write_file(const char *name, const struct buf *b) {
 char path[1100]; snprintf(path, sizeof path, "%s/%s", anlz_dir, name);
 FILE *out = fopen(path, "wb"); assert(out);
 assert(fwrite(b->b, 1, b->n, out) == b->n); fclose(out);
}
static struct buf dat, ext, two;
static uint8_t columns[300 * 3];
static void build_analysis(void) {
 file_start(&dat);
 uint32_t s = open_section(&dat, "PQTZ", 24);
 u32(&dat, 0); u32(&dat, 0x80000); u32(&dat, 4);
 for (uint32_t i = 0; i < 4; ++i) { u16(&dat, i + 1); u16(&dat, 12000); u32(&dat, 100 + i * 500); }
 close_section(&dat, s);
 pcob(&dat, 1, 2, 1, 1200);
 pcob(&dat, 0, 0, 1, 500);
 file_end(&dat);
 file_start(&ext);
 s = open_section(&ext, "PCO2", 20);
 u32(&ext, 1); u16(&ext, 2); u16(&ext, 0);
 pcp2(&ext, 1, 1, 300, 0x305aff);
 pcp2(&ext, 8, 2, 1500, 0xff8000);
 close_section(&ext, s);
 file_end(&ext);
 file_start(&two);
 s = open_section(&two, "PWV7", 24);
 u32(&two, 3); u32(&two, 300); u32(&two, 0);
 for (unsigned i = 0; i < 300; ++i) {
  columns[i*3] = (uint8_t)(i % 200); columns[i*3+1] = (uint8_t)(i % 7 * 9); columns[i*3+2] = (uint8_t)(255 - i % 50);
  u8(&two, columns[i*3]); u8(&two, columns[i*3+1]); u8(&two, columns[i*3+2]);
 }
 close_section(&two, s);
 file_end(&two);
 write_file("ANLZ0000.DAT", &dat); write_file("ANLZ0000.EXT", &ext); write_file("ANLZ0000.2EX", &two);
}

static void test_parsers(void) {
 struct xz_extra_cue hot[XZ_EXTRA_HOT_CUES];
 assert(xz_anlz_hot_cues(dat.b, dat.n, hot) == 1);
 assert(hot[1].set && hot[1].ms == 1200 && hot[1].rgb == 0 && !hot[0].set);
 assert(xz_anlz_hot_cues(ext.b, ext.n, hot) == 2);
 assert(hot[0].set && hot[0].ms == 300 && hot[0].rgb == 0x305aff && hot[7].set && hot[7].ms == 1500 && !hot[1].set);
 assert(xz_anlz_hot_cues(two.b, two.n, hot) == -1);
 struct xz_extra_beat *grid; uint32_t count; int32_t bpm;
 assert(!xz_anlz_beat_grid(dat.b, dat.n, &grid, &count, &bpm));
 assert(count == 4 && bpm == 12000 && grid[0].ms == 100 && grid[0].beat_in_bar == 1 && grid[3].ms == 1600 && grid[3].beat_in_bar == 4);
 free(grid);
 assert(xz_anlz_beat_grid(ext.b, ext.n, &grid, &count, &bpm) == -1 && !grid);
 for (uint32_t cut = 0; cut < dat.n; cut += 7) {
  (void)xz_anlz_hot_cues(dat.b, cut, hot);
  if (!xz_anlz_beat_grid(dat.b, cut, &grid, &count, &bpm)) free(grid);
 }
 struct buf broken = dat;
 set32(&broken, 28 + 20, 100000000u);
 assert(xz_anlz_beat_grid(broken.b, broken.n, &grid, &count, &bpm) == -1);
}

/* ---- driving the players ---- */
static struct xz_xp_players *p;
static struct xz_extra_decks view;
static void step(void) { stepping_side=0;xz_xp_step(p, 0);stepping_side=1;xz_xp_step(p, 1);xz_xp_read(p, &view); }
static void advance(uint32_t ms) {
 for (uint32_t t = 0; t < ms; t += 10) {
  clock_ms += 10;
  consume(0, 441); consume(1, 441);
  step();
 }
}
static struct xz_xp_debug debug(int d) { struct xz_xp_debug out; xz_xp_debug(p, d, &out); return out; }
static struct xz_extra_deck_event event(int d, enum xz_extra_input kind, int pressed) {
 struct xz_extra_deck_event e; memset(&e, 0, sizeof e);
 e.deck = d; e.kind = kind; e.pressed = pressed;
 return e;
}
static int input(struct xz_extra_deck_event e) { return xz_xp_input(p, &e); }
static void press(int d, enum xz_extra_input kind) { assert(input(event(d, kind, 1))); assert(input(event(d, kind, 0))); step(); }
static void hold(int d, enum xz_extra_input kind, int down) { assert(input(event(d, kind, down))); step(); }
/* LOAD is one-shot: producers send it once, pressed or not, and never a release. */
static void load(int d, uint32_t id) {
 struct xz_extra_deck_event e = event(d, XZ_EXTRA_IN_LOAD, (int)(id & 1));
 e.load.source = 1; e.load.track_id = id;
 assert(input(e)); step();
}
static void seek(int d, int32_t ms) { struct xz_extra_deck_event e = event(d, XZ_EXTRA_IN_SEEK, 1); e.seek_ms = ms; assert(input(e)); step(); }
static void jog(int d, int32_t delta) { struct xz_extra_deck_event e = event(d, XZ_EXTRA_IN_JOG_TURN, 1); e.jog.delta = delta; assert(input(e)); }
static void tempo(int d, int32_t at) { struct xz_extra_deck_event e = event(d, XZ_EXTRA_IN_TEMPO, 1); e.tempo_position = at; assert(input(e)); step(); }
static uint64_t ms_frames(int d, int32_t ms) { return (uint64_t)ms * debug(d).source_rate / 1000u; }
static uint64_t completed_tag(int d) { pthread_mutex_lock(&f.lock); uint64_t t = f.position.deck[d].source_after_completed; pthread_mutex_unlock(&f.lock); return t; }
static unsigned flushes(int d) { return f.flushes[d]; }
/* After a jump: exactly one new generation, and every later block carries it. */
static void expect_jump(int d, unsigned before) {
 assert(flushes(d) == before + 1);
 assert(debug(d).generation == f.queue_generation[d]);
}
static void expect_notice(const char *text, uint32_t *seq) {
 xz_xp_read(p, &view);
 assert(view.notice_seq != *seq && !strcmp(view.notice, text));
 *seq = view.notice_seq;
}

static void test_players(void) {
 struct xz_xp_env env = {{NULL, fake_link, fake_submit, fake_flush, fake_position, fake_transition}, now_ms, resolve, master_bpm, NULL, NULL};
 p = xz_xp_create(&env); assert(p);
 uint32_t seq = 0;
 fake_route(0);
 assert(!input(event(0, XZ_EXTRA_IN_PLAY, 1)));
 struct xz_extra_deck_event l = event(0, XZ_EXTRA_IN_LOAD, 1); l.load.source = 1; l.load.track_id = 1;
 assert(!input(l));
 fake_route(1); step();
 assert(!input(event(0, XZ_EXTRA_IN_PLAY, 1)));   /* empty deck */

 /* load -> CUED at the first memory cue, analysis published */
 load(0, 1);
 struct xz_extra_deck *a = &view.deck[0];
 assert(debug(0).state == XZ_XP_CUED && debug(0).source_rate == 44100);
 assert(debug(0).position == ms_frames(0, 500) && debug(0).cue == ms_frames(0, 500));
 assert(a->loaded && !a->playing && !strcmp(a->title, "Track A") && !strcmp(a->artist, "Artist A") && !strcmp(a->key, "Fm"));
 assert(a->source == 1 && a->total_ms == 2000 && a->cue_ms == 500 && a->elapsed_ms == 500 && a->elapsed_at_ms == clock_ms);
 assert(a->track_bpm_x100 == 12000 && a->bpm_x100 == 12000 && a->tempo_x100 == 0 && a->master == 0 && !a->loop.active);
 assert(a->hot_cue[0].set && a->hot_cue[0].ms == 300 && a->hot_cue[0].rgb == 0x305aff && a->hot_cue[7].set && a->hot_cue[7].ms == 1500);
 assert(!a->hot_cue[1].set);
 assert(a->grid_count == 4 && a->grid[2].ms == 1100 && a->grid[2].beat_in_bar == 3);
 assert(a->wave.count == 300 && a->wave.span_ms == 2000 && !memcmp(a->wave.columns, columns, sizeof columns));
 assert(a->wave.normalization == xz_wave_normalization(columns, 300, 0) && a->wave.normalization > 0);
 for (int k = 0; k < XZ_EXTRA_FIELD_COUNT; ++k) assert(a->known & XZ_EXTRA_KNOWN(k));
 assert(f.submits[0] == 0);

 /* PLAY plays, PLAY pauses where the audio was heard, not at the render head */
 unsigned before = flushes(0);
 press(0, XZ_EXTRA_IN_PLAY);
 assert(debug(0).state == XZ_XP_PLAYING && view.deck[0].playing && f.submits[0] > 0);
 assert(f.peak[0] > 0.4f && f.peak[0] < 0.6f);   /* unity gain on a 0.5 sine */
 advance(200);
 uint64_t heard = completed_tag(0);
 assert(heard > ms_frames(0, 650) && heard < ms_frames(0, 720));
 assert(view.deck[0].elapsed_ms == (int32_t)(heard * 1000 / 44100) && view.deck[0].elapsed_at_ms <= clock_ms);
 assert(debug(0).position > heard);
 press(0, XZ_EXTRA_IN_PLAY);
 assert(debug(0).state == XZ_XP_CUED && debug(0).position == heard && !view.deck[0].playing);
 expect_jump(0, before);
 unsigned submits = f.submits[0];
 advance(50);
 assert(f.submits[0] == submits);

 /* CUE paused away from the cue sets the cue there and stays paused */
 before = flushes(0);
 hold(0, XZ_EXTRA_IN_CUE, 1);
 assert(debug(0).state == XZ_XP_CUED && debug(0).cue == heard && f.submits[0] == submits);
 hold(0, XZ_EXTRA_IN_CUE, 0);
 assert(debug(0).state == XZ_XP_CUED && flushes(0) == before);
 /* CUE held at the cue plays; release returns to the cue and pauses */
 hold(0, XZ_EXTRA_IN_CUE, 1);
 assert(debug(0).state == XZ_XP_SAMPLING && view.deck[0].playing && f.submits[0] > submits);
 advance(100);
 assert(completed_tag(0) > heard);
 before = flushes(0);
 hold(0, XZ_EXTRA_IN_CUE, 0);
 assert(debug(0).state == XZ_XP_CUED && debug(0).position == heard);
 expect_jump(0, before);
 /* PLAY during the sampler keeps playing after CUE is released */
 hold(0, XZ_EXTRA_IN_CUE, 1);
 press(0, XZ_EXTRA_IN_PLAY);
 hold(0, XZ_EXTRA_IN_CUE, 0);
 assert(debug(0).state == XZ_XP_PLAYING);
 advance(100);
 /* CUE while playing: back to the cue, paused */
 before = flushes(0);
 hold(0, XZ_EXTRA_IN_CUE, 1);
 assert(debug(0).state == XZ_XP_CUED && debug(0).position == heard);
 expect_jump(0, before);
 hold(0, XZ_EXTRA_IN_CUE, 0);
 assert(debug(0).state == XZ_XP_CUED);

 /* hot cues: an empty pad stores the position, a stored pad jumps and plays, SHIFT clears */
 press(0, XZ_EXTRA_IN_PLAY);
 advance(100);
 heard = completed_tag(0);
 struct xz_extra_deck_event pad = event(0, XZ_EXTRA_IN_HOT_CUE, 1); pad.hot_cue = 1;
 before = flushes(0);
 assert(input(pad)); step();
 assert(view.deck[0].hot_cue[1].set && view.deck[0].hot_cue[1].ms == (int32_t)(heard * 1000 / 44100));
 assert(flushes(0) == before && debug(0).state == XZ_XP_PLAYING);
 press(0, XZ_EXTRA_IN_PLAY);
 pad.hot_cue = 0; before = flushes(0);
 assert(input(pad)); step();
 assert(debug(0).state == XZ_XP_PLAYING && debug(0).position > ms_frames(0, 300));
 expect_jump(0, before);
 advance(30);
 assert(view.deck[0].elapsed_ms >= 300 && view.deck[0].elapsed_ms < 340);
 pad.hot_cue = 1; before = flushes(0);
 assert(input(pad)); step();
 expect_jump(0, before);
 pad.shift = 1; pad.hot_cue = 7;
 assert(input(pad)); step();
 assert(!view.deck[0].hot_cue[7].set && view.deck[0].hot_cue[0].set);

 /* tempo: +-10 % at the fader ends, the consumed source rate follows */
 tempo(0, XZ_EXTRA_TEMPO_SPAN);
 assert(view.deck[0].tempo_x100 == 1000 && view.deck[0].bpm_x100 == 13200);
 advance(100);
 uint64_t a0 = f.last_after[0]; advance(100); uint64_t a1 = f.last_after[0];
 unsigned n0 = f.submits[0];
 advance(100);
 double per_block = (double)(f.last_after[0] - a1) / (f.submits[0] - n0);
 assert(a1 > a0 && fabs(per_block - 256 * 1.1) < 1.0);
 tempo(0, -XZ_EXTRA_TEMPO_SPAN / 2);
 assert(view.deck[0].tempo_x100 == -500 && view.deck[0].bpm_x100 == 11400);
 tempo(0, 0);

 /* playing jog: a bend proportional to the ticks, gone 150 ms after the last */
 jog(0, 25); step();
 assert(fabs(debug(0).speed - (1.0 + 25 * XZ_XP_BEND_PER_TICK)) < 1e-6);
 clock_ms += 75;
 assert(fabs(debug(0).speed - (1.0 + 12.5 * XZ_XP_BEND_PER_TICK)) < 1e-3);
 clock_ms += 75;
 assert(debug(0).speed == 1.0);
 jog(0, 10000); step();
 assert(fabs(debug(0).speed - (1.0 + XZ_XP_BEND_MAX)) < 1e-6);
 clock_ms += XZ_XP_BEND_DECAY_MS; step();
 assert(debug(0).speed == 1.0);
 /* paused jog searches: one turn moves XZ_XP_SEARCH_MS_PER_REV; ticks in one pass flush once */
 press(0, XZ_EXTRA_IN_PLAY);
 seek(0, 100);
 uint64_t at = debug(0).position;
 before = flushes(0);
 jog(0, XZ_XP_JOG_TICKS_PER_REV / 2); jog(0, XZ_XP_JOG_TICKS_PER_REV / 2); step();
 uint64_t per_rev = (uint64_t)44100 * XZ_XP_SEARCH_MS_PER_REV / 1000;
 assert(debug(0).position == at + per_rev && debug(0).state == XZ_XP_CUED);
 expect_jump(0, before);
 jog(0, -100000); step();
 assert(debug(0).position == 0);
 jog(0, 100000); step();
 assert(debug(0).state == XZ_XP_ENDED && debug(0).position == 88200);
 jog(0, -XZ_XP_JOG_TICKS_PER_REV); step();
 assert(debug(0).state == XZ_XP_CUED);

 /* SYNC and MASTER TEMPO */
 master = 0;
 assert(input(event(0, XZ_EXTRA_IN_SYNC, 1))); step();
 assert(view.deck[0].sync.requested&&!view.deck[0].sync.locked&&view.deck[0].sync.reason==XZ_EXTRA_SYNC_WAIT_MASTER);
 assert(view.deck[0].tempo_x100 == 0);
 master = 12600;
 assert(input(event(0, XZ_EXTRA_IN_SYNC, 1))); step(); /* off, then a new release action enables */
 assert(input(event(0, XZ_EXTRA_IN_SYNC, 1))); step();
 assert(view.deck[0].tempo_x100 == 500 && view.deck[0].bpm_x100 == 12600);
 master = 15000;
 assert(input(event(0, XZ_EXTRA_IN_SYNC, 1))); step();
 assert(input(event(0, XZ_EXTRA_IN_SYNC, 1))); step();
 assert(view.deck[0].sync.reason==XZ_EXTRA_SYNC_OUT_OF_RANGE&&!view.deck[0].sync.locked);
 assert(view.deck[0].tempo_x100 == 500);
 assert(input(event(0, XZ_EXTRA_IN_MASTER_TEMPO, 1))); step();
 assert(view.deck[0].master_tempo);
 assert(input(event(0, XZ_EXTRA_IN_MASTER_TEMPO, 1))); step();
 assert(!view.deck[0].master_tempo);
 tempo(0, 0);

 /* SEEK keeps the play state */
 press(0, XZ_EXTRA_IN_PLAY);
 before = flushes(0);
 seek(0, 1000);
 assert(debug(0).state == XZ_XP_PLAYING && debug(0).position > ms_frames(0, 1000));
 expect_jump(0, before);
 advance(20);
 assert(view.deck[0].elapsed_ms >= 1000 && view.deck[0].elapsed_ms < 1030);

 /* EOF: ENDED, silent, at the end */
 seek(0, 1800);
 for (int i = 0; i < 100 && debug(0).state != XZ_XP_ENDED; ++i) advance(10);
 assert(debug(0).state == XZ_XP_ENDED && debug(0).position == 88200 && completed_tag(0) == 88200);
 assert(!view.deck[0].playing && view.deck[0].elapsed_ms == 2000);
 submits = f.submits[0];
 advance(100);
 assert(f.submits[0] == submits);
 press(0, XZ_EXTRA_IN_CUE);
 assert(debug(0).state == XZ_XP_CUED && debug(0).position == debug(0).cue);

 /* the 48 kHz track on DECK 4 resamples at the source rate times tempo */
 load(1, 2);
 assert(debug(1).state == XZ_XP_CUED && debug(1).source_rate == 48000 && debug(1).position == 0);
 assert(!(view.deck[1].known & XZ_EXTRA_KNOWN(XZ_EXTRA_F_WAVE)) && !(view.deck[1].known & XZ_EXTRA_KNOWN(XZ_EXTRA_F_BPM)));
 assert(view.deck[1].known & XZ_EXTRA_KNOWN(XZ_EXTRA_F_HOT_CUES));
 for (int i = 0; i < XZ_EXTRA_HOT_CUES; ++i) assert(!view.deck[1].hot_cue[i].set && !view.deck[1].hot_cue[i].rgb);
 assert(view.deck[1].total_ms == 2000);
 press(1, XZ_EXTRA_IN_PLAY);
 advance(50); a1 = f.last_after[1]; n0 = f.submits[1]; advance(50);
 per_block = (double)(f.last_after[1] - a1) / (f.submits[1] - n0);
 assert(fabs(per_block - 256 * 48000.0 / 44100.0) < 1.0);

 /* load while playing: stops, flushes, retires the old arrays for the grace period only */
 press(0, XZ_EXTRA_IN_PLAY);
 advance(30);
 const struct xz_extra_column *old_columns = view.deck[0].wave.columns;
 const struct xz_extra_beat *old_grid = view.deck[0].grid;
 before = flushes(0);
 load(0, 2);
 uint32_t loaded_at = clock_ms;
 assert(debug(0).state == XZ_XP_CUED && !strcmp(view.deck[0].title, "Track B") && view.deck[0].wave.count == 0 && !view.deck[0].grid);
 assert(flushes(0) >= before + 1 && debug(0).generation == f.queue_generation[0] && debug(0).retired == 1);
 assert(!view.deck[0].hot_cue[0].set);
 clock_ms = loaded_at + XZ_XP_RETIRE_MS - 1; step();
 assert(debug(0).retired == 1 && !memcmp(old_columns, columns, sizeof columns) && old_grid[3].ms == 1600);
 clock_ms = loaded_at + XZ_XP_RETIRE_MS; step();
 assert(debug(0).retired == 0);

 /* resolver failure keeps the deck; an unreadable file is an ERROR the deck refuses play in */
 press(0, XZ_EXTRA_IN_PLAY);
 load(0, 99);
 expect_notice("TRACK NOT FOUND", &seq);
 assert(debug(0).state == XZ_XP_PLAYING && !strcmp(view.deck[0].title, "Track B"));
 load(0, 3);
 expect_notice("TRACK CANNOT BE READ", &seq);
 assert(debug(0).state == XZ_XP_ERROR && !view.deck[0].loaded && !view.deck[0].known);
 assert(!input(event(0, XZ_EXTRA_IN_PLAY, 1)));
 load(0, 1);
 assert(debug(0).state == XZ_XP_CUED);

 /* route down: input refused, a playing deck pauses; a new route restarts the generations */
 press(0, XZ_EXTRA_IN_PLAY);
 advance(50);
 heard = completed_tag(0);
 fake_route(0);
 assert(!input(event(0, XZ_EXTRA_IN_PLAY, 1)) && !input(event(1, XZ_EXTRA_IN_CUE, 1)));
 step();
 assert(debug(0).state == XZ_XP_CUED && debug(0).position == heard && debug(1).state == XZ_XP_CUED);
 fake_route(2); step();
 assert(debug(0).generation == XZ_USB_INITIAL_GENERATION);
 press(0, XZ_EXTRA_IN_PLAY);
 advance(30);
 assert(debug(0).state == XZ_XP_PLAYING && completed_tag(0) > heard);

 /* a full queue refuses instead of blocking */
 int refused = 0;
 for (int i = 0; i < XZ_XP_QUEUE + 4; ++i) refused |= !input(event(0, XZ_EXTRA_IN_SYNC, 0));
 assert(refused);
 step();
 assert(f.stale == 0);
 xz_xp_destroy(p); p = NULL;
}

static void test_loop_and_range(void) {
 struct xz_xp_env env = {{NULL, fake_link, fake_submit, fake_flush, fake_position, fake_transition}, now_ms, resolve, master_bpm, NULL, NULL};
 p = xz_xp_create(&env); assert(p);
 fake_route(7); step(); load(0, 1); load(1, 2);
 seek(0, 500); hold(0, XZ_EXTRA_IN_LOOP_IN, 1); hold(0, XZ_EXTRA_IN_LOOP_IN, 0);
 seek(0, 1000); hold(0, XZ_EXTRA_IN_LOOP_OUT, 1);
 assert(debug(0).loop_active && debug(0).loop_in == ms_frames(0, 500) && debug(0).loop_out == ms_frames(0, 1000));
 hold(1, XZ_EXTRA_IN_PLAY, 1);
 unsigned generation = debug(0).generation;
 for (int i = 0; i < 50; ++i) {
  advance(100);
  assert(debug(0).state == XZ_XP_PLAYING && debug(0).generation == generation);
  assert(debug(0).position >= debug(0).loop_in && debug(0).position < debug(0).loop_out);
  assert(view.deck[0].loop.active && view.deck[0].loop.in_ms == 500 && view.deck[0].loop.out_ms == 1000);
 }
 assert(debug(1).state == XZ_XP_ENDED);
 hold(0, XZ_EXTRA_IN_LOOP_HALF, 1);
 assert(debug(0).loop_out - debug(0).loop_in == ms_frames(0, 250));
 hold(0, XZ_EXTRA_IN_LOOP_DOUBLE, 1);
 assert(debug(0).loop_out - debug(0).loop_in == ms_frames(0, 500));
 hold(0, XZ_EXTRA_IN_LOOP_IN, 1);
 assert(debug(0).loop_edit == 1);
 hold(0, XZ_EXTRA_IN_LOOP_IN, 0);
 jog(0, -100000); step();
 assert(debug(0).loop_in == ms_frames(0, 100)); /* IN adjustment is bounded to 30 CD frames. */
 advance(10001);
 assert(!debug(0).loop_edit);
 hold(0, XZ_EXTRA_IN_RELOOP, 1); assert(!debug(0).loop_active);
 advance(2200); assert(debug(0).state == XZ_XP_ENDED);
 hold(0, XZ_EXTRA_IN_RELOOP, 1); assert(debug(0).loop_active && debug(0).state == XZ_XP_PLAYING);
 hold(0, XZ_EXTRA_IN_CUE, 1); assert(!debug(0).loop_active && debug(0).state == XZ_XP_CUED);
 seek(0, 0); hold(0, XZ_EXTRA_IN_PLAY, 1); hold(0, XZ_EXTRA_IN_LOOP_IN, 1);
 struct xz_extra_deck_event held = event(0, XZ_EXTRA_IN_LOOP_IN, 1); held.held = 1;
 assert(input(held)); step(); assert(!debug(0).loop_active);
 advance(990); assert(!debug(0).loop_active);
 advance(10);
 assert(debug(0).loop_active && debug(0).loop_in == 0 && debug(0).loop_out == ms_frames(0, 2000));
 generation = debug(0).generation;
 assert(input(held)); step(); assert(debug(0).generation == generation);
 hold(0, XZ_EXTRA_IN_LOOP_IN, 0);
 advance(4100); assert(debug(0).loop_active && debug(0).state == XZ_XP_PLAYING);
 hold(0, XZ_EXTRA_IN_MASTER_TEMPO, 1); tempo(0, XZ_EXTRA_TEMPO_SPAN);
 advance(3100); assert(debug(0).loop_active && debug(0).state == XZ_XP_PLAYING && view.deck[0].master_tempo);
 hold(0, XZ_EXTRA_IN_MASTER_TEMPO, 1);
 load(0, 1); assert(!debug(0).loop_active);
 hold(0, XZ_EXTRA_IN_RELOOP, 1); assert(!debug(0).loop_active);
 tempo(0, XZ_EXTRA_TEMPO_SPAN);
 const int ranges[] = {16, 100, 6, 10};
 for (unsigned i = 0; i < sizeof ranges / sizeof *ranges; ++i) {
  hold(0, XZ_EXTRA_IN_TEMPO_RANGE, 1);
  assert(view.deck[0].tempo_range == ranges[i] && view.deck[0].tempo_x100 == ranges[i] * 100);
  hold(0, XZ_EXTRA_IN_TEMPO_RANGE, 0);
 }
 hold(0, XZ_EXTRA_IN_TEMPO_RESET, 1); tempo(0, -XZ_EXTRA_TEMPO_SPAN / 2);
 assert(view.deck[0].tempo_x100 == 0 && debug(0).tempo_reset);
 hold(0, XZ_EXTRA_IN_TEMPO_RESET, 1);
 assert(view.deck[0].tempo_x100 == -500 && !debug(0).tempo_reset);
 hold(0, XZ_EXTRA_IN_TEMPO_RANGE, 1); hold(0, XZ_EXTRA_IN_TEMPO_RANGE, 1);
 tempo(0, -XZ_EXTRA_TEMPO_SPAN);
 assert(debug(0).tempo_range == 100 && debug(0).speed == 0.0);
 unsigned submits = f.submits[0];
 hold(0, XZ_EXTRA_IN_PLAY, 1); advance(200);
 assert(f.submits[0] == submits && debug(0).state == XZ_XP_PLAYING);
 tempo(0, XZ_EXTRA_TEMPO_SPAN); advance(1000);
 assert(debug(0).state == XZ_XP_ENDED);
 xz_xp_destroy(p); p = NULL;
 puts("PASS loop IN/OUT, bounded adjust, hold auto-loop, reloop, half/double, continuous wraps, per-deck isolation, tempo ranges/reset/WIDE zero");
}

static void test_vinyl(void) {
 struct xz_xp_env env = {{NULL, fake_link, fake_submit, fake_flush, fake_position, fake_transition}, now_ms, resolve, master_bpm, NULL, NULL};
 p=xz_xp_create(&env);assert(p);
 fake_route(8);step();load(0,4);load(1,2);
 assert(debug(0).source_rate==44100&&view.deck[0].total_ms==2000);
 seek(0,500);
 hold(0,XZ_EXTRA_IN_JOG_TOUCH,1);assert(debug(0).state==XZ_XP_CUED); /* CDJ mode ignores touch. */
 hold(0,XZ_EXTRA_IN_JOG_TOUCH,0);
 tempo(0,XZ_EXTRA_TEMPO_SPAN);hold(0,XZ_EXTRA_IN_MASTER_TEMPO,1);
 f.capture[0]=1;hold(0,XZ_EXTRA_IN_PLAY,1);
 assert(f.captured_frames[0]>=32);
 for(unsigned n=0;n<32;n++)for(unsigned ch=0;ch<2;ch++)
  assert(fabs(f.captured[0][n*2+ch]-.5*sin(6.283185307179586*440*(ms_frames(0,500)+n)/44100.0))<.001);
 hold(0,XZ_EXTRA_IN_PLAY,1);hold(0,XZ_EXTRA_IN_MASTER_TEMPO,1);tempo(0,0);seek(0,500);
 hold(0,XZ_EXTRA_IN_VINYL,1);assert(view.deck[0].vinyl);
 hold(0,XZ_EXTRA_IN_PLAY,1);hold(1,XZ_EXTRA_IN_PLAY,1);advance(200);
 hold(0,XZ_EXTRA_IN_JOG_TOUCH,1);
 assert(debug(0).state==XZ_XP_SCRATCHING&&!view.deck[0].playing);
 uint64_t at=debug(0).position;
 unsigned submits=f.submits[0],generation=debug(0).generation;
 advance(500);
 assert(f.submits[0]==submits&&debug(0).position==at&&view.deck[1].playing);
 f.capture[0]=1;jog(0,8);step();advance(80);
 assert(f.captured_frames[0]>=32);
 for(unsigned n=0;n<32;n++)
  assert(fabs(f.captured[0][n*2]-.5*sin(6.283185307179586*440*(at+2.5*n)/44100.0))<.001);
 assert(debug(0).position>at&&debug(0).position==debug(0).scratch_target);
 assert(debug(0).generation==generation);
 at=debug(0).position;
 jog(0,8);step();advance(100);assert(debug(0).position>at&&debug(0).generation==generation);
 at=debug(0).position;
 f.capture[0]=1;jog(0,-4);step();advance(100);
 assert(f.captured_frames[0]>=32);
 for(unsigned n=0;n<32;n++)
  assert(fabs(f.captured[0][n*2]-.5*sin(6.283185307179586*440*(at-.2*(n+.4))/44100.0))<.001);
 assert(debug(0).position<at&&debug(0).generation==generation); /* Continuous turn, no dropped generation. */
 at=debug(0).position;submits=f.submits[0];advance(100);
 assert(debug(0).position==at&&f.submits[0]==submits);
 hold(0,XZ_EXTRA_IN_JOG_TOUCH,0);advance(100);
 assert(debug(0).state==XZ_XP_PLAYING&&debug(0).position>at&&view.deck[0].playing);
 hold(0,XZ_EXTRA_IN_PLAY,1);
 hold(0,XZ_EXTRA_IN_JOG_TOUCH,1);at=debug(0).position;
 jog(0,6);step();advance(100);
 hold(0,XZ_EXTRA_IN_JOG_TOUCH,0);
 assert(debug(0).state==XZ_XP_CUED&&debug(0).position>at);
 seek(0,2000);assert(debug(0).state==XZ_XP_ENDED);
 hold(0,XZ_EXTRA_IN_JOG_TOUCH,1);jog(0,-20);step();advance(200);
 assert(debug(0).position<ms_frames(0,2000));
 hold(0,XZ_EXTRA_IN_JOG_TOUCH,0);assert(debug(0).state==XZ_XP_CUED);
 hold(0,XZ_EXTRA_IN_PLAY,1);advance(20);
 hold(0,XZ_EXTRA_IN_JOG_TOUCH,1);hold(0,XZ_EXTRA_IN_VINYL,1);
 assert(debug(0).state==XZ_XP_PLAYING&&!view.deck[0].vinyl);
 assert(f.stale==0);
 xz_xp_destroy(p);p=NULL;
 puts("PASS AIFF through player, vinyl hold/silence, forward/reverse scratch, reversal flush, paused/play resume, end clamp and native-side independence");
}

/* Two consumers snapshot while loads alternate: every snapshot is one load's data, never torn, and
   arrays stay readable for the frame that holds them. */
static int stop_readers;
static unsigned long snapshots, with_wave, with_b;
static void *reader(void *arg) {
 (void)arg;
 struct xz_extra_decks s;
 unsigned long n = 0, wave = 0, b = 0;
 while (!__atomic_load_n(&stop_readers, __ATOMIC_ACQUIRE)) {
  xz_xp_read(p, &s);
  for (int d = 0; d < 2; ++d) {
   const struct xz_extra_deck *k = &s.deck[d];
   if (k->wave.count) {
    assert(!strcmp(k->title, "Track A") && k->wave.count == 300 && k->grid_count == 4);
    assert(!memcmp(k->wave.columns, columns, sizeof columns) && k->grid[3].ms == 1600);
    wave++;
   } else if (k->loaded && k->total_ms) { assert(!strcmp(k->title, "Track B") && !k->grid); b++; }
   if (k->known & XZ_EXTRA_KNOWN(XZ_EXTRA_F_TOTAL)) assert(k->elapsed_ms >= 0 && k->elapsed_ms <= k->total_ms);
  }
  n++;
 }
 __atomic_add_fetch(&snapshots, n, __ATOMIC_RELAXED);
 __atomic_add_fetch(&with_wave, wave, __ATOMIC_RELAXED);
 __atomic_add_fetch(&with_b, b, __ATOMIC_RELAXED);
 return NULL;
}
static int stop_usb;
static void *usb(void *arg) {
 (void)arg;
 while (!__atomic_load_n(&stop_usb, __ATOMIC_ACQUIRE)) {
  consume(0, 88.2); consume(1, 88.2);
  struct timespec t = {0, 2000000}; nanosleep(&t, NULL);
 }
 return NULL;
}
static unsigned ordinary_resolves, checked_resolves;
static uint32_t reconnect_in_resolver;
static int ordinary_counted(int source,uint32_t id,struct xz_extra_track *out) {
 ordinary_resolves++;return resolve(source,id,out);
}
static int checked_counted(int source,uint32_t id,uint64_t generation,struct xz_extra_track *out) {
 checked_resolves++;
 if(reconnect_in_resolver){uint32_t epoch=reconnect_in_resolver;reconnect_in_resolver=0;fake_route(epoch);}
 return generation==42?resolve(source,id,out):-1;
}
static void test_checked_resolver(void) {
 for(int missing=0;missing<2;missing++) {
  ordinary_resolves=checked_resolves=0;fake_route(300+missing);
  struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,ordinary_counted,master_bpm,NULL,missing?NULL:checked_counted};
  p=xz_xp_create(&env);assert(p);step();load(0,1);press(0,XZ_EXTRA_IN_PLAY);
  assert(ordinary_resolves==1&&debug(0).source_rate==44100&&debug(0).state==XZ_XP_PLAYING);
  struct xz_extra_deck_event e=event(0,XZ_EXTRA_IN_LOAD,1);e.load.source=1;e.load.track_id=2;e.load.media_generation=41;
  e.load.route_epoch=999;assert(input(e));step();
  assert(ordinary_resolves==1&&!checked_resolves&&debug(0).source_rate==44100&&debug(0).state==XZ_XP_PLAYING);
  e.load.route_epoch=0;assert(input(e));step();
  assert(ordinary_resolves==1&&checked_resolves==(unsigned)!missing);
  assert(debug(0).source_rate==44100&&debug(0).state==XZ_XP_PLAYING);
  if(!missing) {
   e.load.media_generation=42;assert(input(e));step();
   assert(checked_resolves==2&&ordinary_resolves==1&&debug(0).source_rate==48000);
  }
  e.load.media_generation=0;e.load.track_id=1;assert(input(e));step();
  assert(ordinary_resolves==2&&checked_resolves==(missing?0u:2u)&&debug(0).source_rate==44100);
  xz_xp_destroy(p);p=NULL;
 }
 /* A valid resolution from a replaced route cannot retire the current track. */
 ordinary_resolves=checked_resolves=0;fake_route(400);
 struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,ordinary_counted,master_bpm,NULL,checked_counted};
 p=xz_xp_create(&env);assert(p);step();load(0,1);press(0,XZ_EXTRA_IN_PLAY);
 unsigned flushes=f.flushes[0];
 struct xz_extra_deck_event e=event(0,XZ_EXTRA_IN_LOAD,1);e.load.source=1;e.load.track_id=2;e.load.media_generation=42;e.load.route_epoch=400;
 reconnect_in_resolver=401;assert(input(e));step();
 assert(!reconnect_in_resolver&&checked_resolves==1&&ordinary_resolves==1);
 assert(debug(0).state==XZ_XP_PLAYING&&debug(0).source_rate==44100&&f.flushes[0]==flushes);
 xz_xp_destroy(p);p=NULL;
 puts("PASS checked resolver exact dispatch, no unchecked fallback, missing callback refusal, playing track preserved, legacy zero-token path");
}

static int sync_resolve(int source,uint32_t id,struct xz_extra_track *out){
 int rc=resolve(source,id,out);if(!rc&&id==2){snprintf(out->anlz_dir,sizeof out->anlz_dir,"%s",anlz_dir);out->track_bpm_x100=12000;}return rc;
}
static int wide_sync_resolve(int source,uint32_t id,struct xz_extra_track *out){
 int rc=resolve(source,id,out);if(!rc)out->track_bpm_x100=25000;return rc;
}
static void sync_loop_track(int deck,uint32_t id,int32_t at){
 load(deck,id);seek(deck,0);press(deck,XZ_EXTRA_IN_LOOP_IN);seek(deck,2000);press(deck,XZ_EXTRA_IN_LOOP_OUT);seek(deck,at);
}
/* Independent fixture oracle: each WAV grid has beats at 100+500*n milliseconds. */
static double actual_sync_error(int master_deck,int slave_deck){
 const xz_usb_deck_position *m=&f.position.deck[master_deck],*s=&f.position.deck[slave_deck];
 double phase_m=((double)m->source_after_completed/debug(master_deck).source_rate-.1)*2;
 double phase_s=((double)s->source_after_completed/debug(slave_deck).source_rate-.1)*2;
 double rate=1.+view.deck[master_deck].tempo_x100/10000.;
 phase_m+=((double)s->source_completed_wire_frame-(double)m->source_completed_wire_frame)/XZ_USB_RATE*rate*2;
 double error=fmod(phase_s-phase_m+.5,1.);if(error<0)error+=1.;error-=.5;
 return error/(2*rate);
}
static FILE *sync_trace_file;
static void sync_trace(const char *stage,int master_deck,int slave){
 if(!sync_trace_file)return;
 const xz_usb_deck_position *m=&f.position.deck[master_deck],*s=&f.position.deck[slave];
 fprintf(sync_trace_file,"{\"stage\":\"%s\",\"clock_ms\":%u,\"master\":%d,\"master_source\":%llu,\"slave_source\":%llu,\"master_wire\":%llu,\"slave_wire\":%llu,\"error_us\":%.3f,\"locked\":%d,\"reason\":%d,\"trim_ppm\":%d,\"committed_horizon_frames\":%u}\n",
  stage,clock_ms,master_deck,(unsigned long long)m->source_after_completed,(unsigned long long)s->source_after_completed,
  (unsigned long long)m->source_completed_wire_frame,(unsigned long long)s->source_completed_wire_frame,
  actual_sync_error(master_deck,slave)*1000000,view.deck[slave].sync.locked,view.deck[slave].sync.reason,view.deck[slave].sync.correction_ppm,wire_horizon);
}
static struct xz_sync_native_snapshot cached_native;
static unsigned native_cache_copies;
static int copy_native(struct xz_sync_native_snapshot *out){__atomic_fetch_add(&native_cache_copies,1,__ATOMIC_RELAXED);*out=cached_native;return out->available;}
static void test_native_sync_cache(void){
 struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,sync_resolve,master_bpm,NULL,NULL};
 p=xz_xp_create(&env);assert(p);xz_xp_set_native_sync_clock(p,copy_native);fake_route(990);step();load(0,1);press(0,XZ_EXTRA_IN_PLAY);
 memset(&cached_native,0,sizeof cached_native);native_cache_copies=0;advance(20);assert(!xz_xp_needs_native_sync(p)&&!native_cache_copies);
 press(0,XZ_EXTRA_IN_SYNC);assert(xz_xp_needs_native_sync(p)&&view.deck[0].sync.reason==XZ_EXTRA_SYNC_WAIT_MASTER);
 cached_native=(struct xz_sync_native_snapshot){.available=1,.native_deck=0,.source_revision=10,.track_identity=0x1234,
  .bpm=126,.speed=1.05f,.playing=1,.captured_ms=clock_ms};
 advance(60);assert(view.deck[0].tempo_x100==500&&view.deck[0].sync.requested&&!view.deck[0].sync.locked);
 assert(view.deck[0].sync.master_deck==0&&view.deck[0].sync.reason==XZ_EXTRA_SYNC_NATIVE_PHASE_UNAVAILABLE);
 assert(view.deck[0].sync.master_scope==XZ_EXTRA_MASTER_SCOPE_NATIVE);
 assert(abs(view.deck[0].timeline.rate_ppm-1050000)<5000);
 cached_native.bpm=132;cached_native.captured_ms=clock_ms;cached_native.source_revision=11;cached_native.track_identity=0x5678;cached_native.native_deck=1;
 advance(60);assert(view.deck[0].tempo_x100==1000&&view.deck[0].sync.master_deck==1&&!view.deck[0].sync.locked);
 advance(200);assert(view.deck[0].sync.reason==XZ_EXTRA_SYNC_WAIT_MASTER&&view.deck[0].sync.requested);
 cached_native.captured_ms=clock_ms+1;step();assert(view.deck[0].sync.reason==XZ_EXTRA_SYNC_WAIT_MASTER);
 press(0,XZ_EXTRA_IN_SYNC);unsigned before=native_cache_copies;advance(50);assert(!xz_xp_needs_native_sync(p)&&native_cache_copies==before);
 press(0,XZ_EXTRA_IN_MASTER);press(0,XZ_EXTRA_IN_SYNC);assert(view.deck[0].master&&view.deck[0].sync.requested);
 uint32_t flush_before=f.flushes[0];cached_native.captured_ms=clock_ms;
 assert(xz_xp_use_native_master(p));xz_xp_read(p,&view);
 assert(!view.deck[0].master&&!view.deck[1].master&&!view.deck[0].sync.locked&&view.deck[0].sync.master_deck==-1);
 uint32_t native_revision=view.snapshot_revision;assert(!xz_xp_use_native_master(p)&&xz_xp_revision(p)==native_revision);
 step();assert(view.deck[0].sync.requested&&view.deck[0].sync.master_deck==1&&view.deck[0].sync.master_scope==XZ_EXTRA_MASTER_SCOPE_NATIVE);
 assert(!view.deck[0].sync.locked&&f.flushes[0]==flush_before&&xz_xp_needs_native_sync(p));
 press(0,XZ_EXTRA_IN_SYNC);press(0,XZ_EXTRA_IN_PLAY);
 uint32_t quiet=xz_xp_revision(p);assert(view.deck[0].wave.count);advance(500);assert(xz_xp_revision(p)==quiet);
 xz_xp_destroy(p);p=NULL;
 puts("PASS demand-only cached native BPM following, identity/load revision, pending/stale/future rejection and truthful phase-unavailable");
}
static struct {
 int active,native_deck,playing,manual;
 uint32_t source_revision,track_identity,uncertainty_us,obs_age_us;
 int32_t output_latency_us;
 double bpm,anchor_phase;
 uint64_t anchor_us;
} sim_native;
static double sim_native_phase_at_us(uint64_t t_us){
 double dt=((int64_t)t_us-(int64_t)sim_native.anchor_us)/1000000.;
 double p=fmod(sim_native.anchor_phase+dt*(sim_native.bpm/60.),4.);
 return p<0?p+4.:p;
}
static void sim_native_set_bpm(double next_bpm){
 uint64_t now_u=(uint64_t)clock_ms*1000u;
 sim_native.anchor_phase=sim_native_phase_at_us(now_u);
 sim_native.anchor_us=now_u;
 sim_native.bpm=next_bpm;
}
static int copy_sim_native(struct xz_sync_native_snapshot *out){
 if(!sim_native.active){memset(out,0,sizeof *out);return 0;}
 uint64_t cap_us=(uint64_t)clock_ms*1000u-sim_native.obs_age_us;
 *out=(struct xz_sync_native_snapshot){
  .available=1,.phase_available=1,.reason=XZ_SYNC_NATIVE_OK,
  .native_deck=sim_native.native_deck,.master_id=sim_native.native_deck,
  .track_identity=sim_native.track_identity,.source_revision=sim_native.source_revision,
  .captured_us=cap_us,.captured_ms=(uint32_t)(cap_us/1000u),
  .uncertainty_us=sim_native.uncertainty_us,.output_latency_us=sim_native.output_latency_us,
  .bpm=(float)sim_native.bpm,.nominal_bpm=120.f,
  .bar_phase=(float)sim_native_phase_at_us(cap_us),
  .speed=(float)(sim_native.bpm/120.),
  .playing=sim_native.playing,.manual=sim_native.manual
 };
 return 1;
}
static double actual_native_sync_error(int slave_deck){
 const xz_usb_deck_position *s=&f.position.deck[slave_deck];
 double phase_s=fmod(((double)s->source_after_completed/debug(slave_deck).source_rate-.1)*2.,1.);
 if(phase_s<0)phase_s+=1.;
 double block_age_us=(f.wire[slave_deck]-(double)s->source_completed_wire_frame)*1000000./XZ_USB_RATE;
 uint64_t audible_us=(uint64_t)llround((double)clock_ms*1000.-block_age_us-(double)sim_native.output_latency_us);
 double phase_m=fmod(sim_native_phase_at_us(audible_us),1.);
 if(phase_m<0)phase_m+=1.;
 double err=fmod(phase_s-phase_m+.5,1.);if(err<0)err+=1.;err-=.5;
 return err/(sim_native.bpm/60.);
}
static void test_native_phase_sync(void){
 wire_horizon=882;
 for(int slave=0;slave<2;slave++)for(int source=1;source<=2;source++){
  struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,sync_resolve,master_bpm,NULL,NULL};
  p=xz_xp_create(&env);assert(p);xz_xp_set_native_sync_clock(p,copy_sim_native);fake_route(997);step();
  sync_loop_track(slave,(uint32_t)source,350);
  sim_native.active=1;sim_native.native_deck=slave;sim_native.playing=1;sim_native.manual=0;
  sim_native.source_revision=21;sim_native.track_identity=0x32000;
  sim_native.uncertainty_us=120;sim_native.obs_age_us=12350;sim_native.output_latency_us=4500;
  sim_native.bpm=126.;sim_native.anchor_us=(uint64_t)clock_ms*1000u;sim_native.anchor_phase=.25;
  advance(40);
  /* Before SYNC, waveform timeline tracks actual unsynced audio and shows real drift. */
  assert(fabs(actual_native_sync_error(slave))>.05);
  unsigned flush_before_sync=f.flushes[slave];
  press(slave,XZ_EXTRA_IN_SYNC);
  /* Pressing SYNC arms the initial jump (ACQUIRING, locked=0) and transitions audio on the next step. */
  assert(f.flushes[slave]==flush_before_sync&&!view.deck[slave].sync.locked&&view.deck[slave].sync.reason==XZ_EXTRA_SYNC_ACQUIRING);
  step();assert(f.flushes[slave]==flush_before_sync+1&&!view.deck[slave].sync.locked);
  for(unsigned elapsed=0;elapsed<2000;elapsed+=10)advance(10);
  assert(view.deck[slave].sync.locked&&view.deck[slave].sync.reason==XZ_EXTRA_SYNC_LOCKED);
  assert(view.deck[slave].sync.master_scope==XZ_EXTRA_MASTER_SCOPE_NATIVE&&view.deck[slave].sync.master_deck==slave);
  assert(fabs(actual_native_sync_error(slave))<=.001);
  /* Sparse noisy completion observations preserve <= 1 ms native phase lock. */
  position_interval=3;position_noise=1;double noisy=0;
  for(unsigned elapsed=0;elapsed<2000;elapsed+=10){
   advance(10);double e=fabs(actual_native_sync_error(slave));if(e>noisy)noisy=e;
  }
  position_interval=position_noise=0;advance(300);
  assert(noisy<=.001&&view.deck[slave].sync.locked);
  /* Native tempo step from 126 to 132 BPM recovers <= 1 ms phase lock. */
  sim_native_set_bpm(132.);
  for(unsigned elapsed=0;elapsed<1000;elapsed+=10)advance(10);
  assert(view.deck[slave].tempo_x100==1000&&view.deck[slave].sync.locked&&fabs(actual_native_sync_error(slave))<=.001);
  /* Manual jog bend on follower drops lock, waveform shows real audio drift, then recovers lock. */
  jog(slave,40);step();assert(view.deck[slave].sync.requested&&!view.deck[slave].sync.locked&&view.deck[slave].sync.reason==XZ_EXTRA_SYNC_MANUAL);
  advance(120);
  assert(view.deck[slave].timeline.source_frame==completed_tag(slave)&&fabs(actual_native_sync_error(slave))>.001);
  for(unsigned elapsed=0;elapsed<3000;elapsed+=10)advance(10);
  assert(view.deck[slave].sync.locked&&fabs(actual_native_sync_error(slave))<=.001);
  /* Native manual override (scratch/bend) suspends lock and recovers when released. */
  sim_native.manual=1;step();assert(!view.deck[slave].sync.locked&&view.deck[slave].sync.reason==XZ_EXTRA_SYNC_MANUAL);
  sim_native.manual=0;for(unsigned elapsed=0;elapsed<500;elapsed+=10)advance(10);
  assert(view.deck[slave].sync.locked&&fabs(actual_native_sync_error(slave))<=.001);
  /* Native phase discontinuity (seek) holds audio without silent jump until re-synced. */
  unsigned flushes_before=f.flushes[slave];
  sim_native.anchor_phase=fmod(sim_native.anchor_phase+.45,4.);
  advance(30);
  assert(view.deck[slave].sync.requested&&!view.deck[slave].sync.locked&&view.deck[slave].sync.reason==XZ_EXTRA_SYNC_DISCONTINUITY);
  assert(f.flushes[slave]==flushes_before);
  press(slave,XZ_EXTRA_IN_SYNC);press(slave,XZ_EXTRA_IN_SYNC);
  for(unsigned elapsed=0;elapsed<2000;elapsed+=10)advance(10);
  assert(view.deck[slave].sync.locked&&fabs(actual_native_sync_error(slave))<=.001);
  xz_xp_destroy(p);p=NULL;
 }
 wire_horizon=0;
 puts("PASS native-to-extra beat phase acquisition, horizon/age/latency compensation, tempo steps, manual recovery and discontinuity hold");
}
static void test_sync_missing_sources(void){
 for(int missing_master=0;missing_master<2;missing_master++){
  struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,sync_resolve,master_bpm,NULL,NULL};
  p=xz_xp_create(&env);assert(p);fake_route(992);step();sync_loop_track(0,missing_master?4:1,100);sync_loop_track(1,missing_master?1:4,350);
  press(0,XZ_EXTRA_IN_MASTER);advance(40);unsigned flush_before=f.flushes[1];press(1,XZ_EXTRA_IN_SYNC);advance(100);
  assert(view.deck[1].sync.requested&&!view.deck[1].sync.locked&&view.deck[1].sync.reason==XZ_EXTRA_SYNC_NO_GRID&&f.flushes[1]==flush_before);
  xz_xp_destroy(p);p=NULL;
 }
 struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,resolve,master_bpm,NULL,NULL};
 p=xz_xp_create(&env);assert(p);xz_xp_set_native_sync_clock(p,copy_native);fake_route(993);step();load(0,2);press(0,XZ_EXTRA_IN_PLAY);
 cached_native=(struct xz_sync_native_snapshot){.available=1,.native_deck=0,.source_revision=12,.track_identity=0x99,.bpm=126,.captured_ms=clock_ms};
 press(0,XZ_EXTRA_IN_SYNC);advance(30);assert(view.deck[0].sync.reason==XZ_EXTRA_SYNC_NO_SOURCE_BPM&&!view.deck[0].sync.locked&&!view.deck[0].tempo_x100);
 xz_xp_destroy(p);p=NULL;puts("PASS missing master/slave grid and native-tempo source BPM refuse phase/audio coercion");
 env.resolve=wide_sync_resolve;p=xz_xp_create(&env);assert(p);xz_xp_set_native_sync_clock(p,copy_native);fake_route(995);step();load(0,4);
 press(0,XZ_EXTRA_IN_TEMPO_RANGE);press(0,XZ_EXTRA_IN_TEMPO_RANGE);assert(view.deck[0].tempo_range==100);press(0,XZ_EXTRA_IN_PLAY);
 cached_native=(struct xz_sync_native_snapshot){.available=1,.native_deck=0,.source_revision=13,.track_identity=0x100,.bpm=500,.captured_ms=clock_ms};
 press(0,XZ_EXTRA_IN_SYNC);advance(60);assert(view.deck[0].tempo_x100==10000&&abs(view.deck[0].timeline.rate_ppm-2000000)<5000);
 cached_native.bpm=501;cached_native.captured_ms=clock_ms;step();
 assert(view.deck[0].sync.reason==XZ_EXTRA_SYNC_OUT_OF_RANGE&&view.deck[0].tempo_x100==10000&&!view.deck[0].sync.locked);
 xz_xp_destroy(p);p=NULL;puts("PASS 250-to-500 BPM WIDE native tempo ratio and explicit 501 BPM out-of-range refusal");
}
static void test_sync_nonzero_loop(void){
 struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,sync_resolve,master_bpm,NULL,NULL};
 p=xz_xp_create(&env);assert(p);fake_route(996);step();sync_loop_track(0,1,300);
 load(1,2);seek(1,500);press(1,XZ_EXTRA_IN_LOOP_IN);seek(1,1000);press(1,XZ_EXTRA_IN_LOOP_OUT);
 press(0,XZ_EXTRA_IN_MASTER);advance(40);press(1,XZ_EXTRA_IN_SYNC);
 for(unsigned elapsed=0;elapsed<2000;elapsed+=10){
  advance(10);assert(debug(1).position>=ms_frames(1,500)&&debug(1).position<ms_frames(1,1000));
 }
 assert(view.deck[1].sync.locked&&fabs(actual_sync_error(0,1))<=.001);
 xz_xp_destroy(p);p=NULL;puts("PASS initial phase acquire wraps within a nonzero integer-beat loop, never outside its PCM region");
}
static void test_sync_concurrent_groups(void){
 struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,sync_resolve,master_bpm,NULL,NULL};
 p=xz_xp_create(&env);assert(p);xz_xp_set_native_sync_clock(p,copy_native);fake_route(994);step();
 sync_loop_track(0,1,100);sync_loop_track(1,2,350);advance(40);memset(&cached_native,0,sizeof cached_native);
 threaded_clock=1;assert(!xz_xp_start(p));__atomic_store_n(&stop_usb,0,__ATOMIC_RELEASE);pthread_t wire;assert(!pthread_create(&wire,NULL,usb,NULL));
 unsigned changed=0;
 for(int round=0;round<32;round++){
  int selected=round&1;struct xz_extra_deck_event e=event(selected,XZ_EXTRA_IN_MASTER,1);assert(input(e));
  int applied=0;
  for(int tries=0;tries<100;tries++){
   xz_xp_read(p,&view);if(view.deck[selected].master){applied=1;break;}
   struct timespec pause={0,1000000};nanosleep(&pause,NULL);
  }
  assert(applied&&!view.deck[1-selected].master);
  struct xz_extra_deck_event request=event(1-selected,XZ_EXTRA_IN_SYNC,1);assert(input(request));
  assert(xz_xp_use_native_master(p));changed++;
  assert(!xz_xp_use_native_master(p));
  for(int reads=0;reads<20;reads++){
   struct xz_extra_decks snapshot={0};xz_xp_read(p,&snapshot);
   assert(!snapshot.deck[0].master&&!snapshot.deck[1].master);
   assert(!snapshot.deck[0].sync.locked&&!snapshot.deck[1].sync.locked);
  }
 }
 __atomic_store_n(&stop_usb,1,__ATOMIC_RELEASE);assert(!pthread_join(wire,NULL));xz_xp_stop(p);threaded_clock=0;
 xz_xp_destroy(p);p=NULL;assert(changed==32);
 puts("PASS two production workers and USB thread: 32 explicit master handoffs/native returns, coherent unlocked snapshots, idempotence");
}
static void test_sync_coordinator(void){
 wire_horizon=882; /* 20 ms old PCM is already committed to the wire before a new generation applies. */
 char trace_path[512];snprintf(trace_path,sizeof trace_path,"%s/sync-phase-trace.jsonl",fixtures);sync_trace_file=fopen(trace_path,"w");assert(sync_trace_file);
 for(int master_deck=0;master_deck<2;master_deck++)for(int source=1;source<=2;source++){
  int slave=1-master_deck;
  struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,sync_resolve,master_bpm,NULL,NULL};
  p=xz_xp_create(&env);assert(p);fake_route(1000);step();
  sync_loop_track(master_deck,(uint32_t)source,100);sync_loop_track(slave,(uint32_t)(3-source),350);
  press(master_deck,XZ_EXTRA_IN_MASTER);advance(40);assert(view.deck[master_deck].master&&!view.deck[slave].master);
  xz_usb_position horizon;assert(fake_position(NULL,fake_link(NULL),&horizon)==XZ_USB_OK);
  assert(horizon.wire_submitted_frames==(uint64_t)f.wire[stepping_side]+wire_horizon);
  assert(!xz_xp_needs_native_sync(p));press(slave,XZ_EXTRA_IN_SYNC);
  assert(view.deck[slave].sync.requested&&!xz_xp_needs_native_sync(p));
  assert(view.deck[master_deck].master_scope==XZ_EXTRA_MASTER_SCOPE_SOFTWARE);
  for(unsigned elapsed=0;elapsed<2000;elapsed+=10){advance(10);sync_trace("acquire",master_deck,slave);}
  fprintf(stderr,"SYNC acquire master=%d rate=%d reason=%d locked=%d error_us=%d trim=%d actual_ms=%g\n",master_deck,source,view.deck[slave].sync.reason,view.deck[slave].sync.locked,view.deck[slave].sync.phase_error_us,view.deck[slave].sync.correction_ppm,actual_sync_error(master_deck,slave)*1000);
  assert(view.deck[slave].sync.locked&&fabs(actual_sync_error(master_deck,slave))<=.001);
  double maximum=0;unsigned duration=master_deck==0&&source==1?180000:10000;
  for(unsigned elapsed=0;elapsed<duration;elapsed+=10){advance(10);double error=fabs(actual_sync_error(master_deck,slave));if(error>maximum)maximum=error;assert(view.deck[slave].sync.requested&&view.deck[slave].sync.locked);if(!(elapsed%1000))sync_trace("steady",master_deck,slave);}
  assert(maximum<=.001);
  uint64_t underrun_m=f.position.deck[master_deck].underrun_frames,underrun_s=f.position.deck[slave].underrun_frames;
  position_interval=3;position_noise=1;double noisy=0;
  for(unsigned elapsed=0;elapsed<2000;elapsed+=10){advance(10);double error=fabs(actual_sync_error(master_deck,slave));if(error>noisy)noisy=error;sync_trace("sparse_noise",master_deck,slave);}
  position_interval=position_noise=0;advance(300);
  fprintf(stderr,"SYNC sparse noisy master=%d source=%d max_ms=%g locked=%d reason=%d underrun_delta=%llu/%llu reads=%u/%u\n",master_deck,source,noisy*1000,view.deck[slave].sync.locked,view.deck[slave].sync.reason,(unsigned long long)(f.position.deck[master_deck].underrun_frames-underrun_m),(unsigned long long)(f.position.deck[slave].underrun_frames-underrun_s),position_calls[0],position_calls[1]);
  assert(noisy<=.001&&view.deck[slave].sync.locked);
  position_interval=3;position_noise=1;tempo(master_deck,XZ_EXTRA_TEMPO_SPAN);double transient=0;
  for(unsigned elapsed=0;elapsed<1000;elapsed+=10){advance(10);double error=fabs(actual_sync_error(master_deck,slave));if(error>transient)transient=error;sync_trace("tempo_step_delayed",master_deck,slave);}
  position_interval=position_noise=0;
  fprintf(stderr,"SYNC step master=%d rate=%d max_ms=%g final_ms=%g locked=%d reason=%d trim=%d\n",master_deck,source,transient*1000,actual_sync_error(master_deck,slave)*1000,view.deck[slave].sync.locked,view.deck[slave].sync.reason,view.deck[slave].sync.correction_ppm);
  assert(transient<=.010&&fabs(actual_sync_error(master_deck,slave))<=.001&&view.deck[slave].sync.locked);
  printf("{\"case\":\"common_wire_sync\",\"master\":%d,\"source_rate\":%u,\"seconds\":%u,\"max_steady_us\":%.2f,\"sparse_noise_us\":%.2f,\"step_peak_us\":%.2f,\"committed_horizon_frames\":%u}\n",master_deck,debug(master_deck).source_rate,duration/1000,maximum*1000000,noisy*1000000,transient*1000000,wire_horizon);
  /* CUE/seek on the master is an unproven phase discontinuity: hold audio, never fake a lock. */
  seek(master_deck,750);advance(50);assert(view.deck[slave].sync.requested&&!view.deck[slave].sync.locked&&view.deck[slave].sync.reason==XZ_EXTRA_SYNC_DISCONTINUITY);
  press(slave,XZ_EXTRA_IN_SYNC);press(slave,XZ_EXTRA_IN_SYNC);advance(2000);assert(view.deck[slave].sync.locked);
  jog(slave,8);step();assert(view.deck[slave].sync.requested&&!view.deck[slave].sync.locked);
  for(unsigned elapsed=0;elapsed<2000;elapsed+=10){advance(10);sync_trace("manual_bend_recovery",master_deck,slave);}
  fprintf(stderr,"manual recovery locked=%d reason=%d error_us=%g underrun=%llu/%llu\n",view.deck[slave].sync.locked,view.deck[slave].sync.reason,actual_sync_error(master_deck,slave)*1000000,(unsigned long long)f.position.deck[master_deck].underrun_frames,(unsigned long long)f.position.deck[slave].underrun_frames);
  assert(view.deck[slave].sync.requested&&view.deck[slave].sync.locked&&fabs(actual_sync_error(master_deck,slave))<=.001);
  press(slave,XZ_EXTRA_IN_VINYL);hold(slave,XZ_EXTRA_IN_JOG_TOUCH,1);jog(slave,-4);step();advance(30);
  assert(view.deck[slave].sync.requested&&!view.deck[slave].sync.locked);
  hold(slave,XZ_EXTRA_IN_JOG_TOUCH,0);advance(8000);
  assert(view.deck[slave].sync.requested&&view.deck[slave].sync.locked&&fabs(actual_sync_error(master_deck,slave))<=.001);
  press(slave,XZ_EXTRA_IN_LOOP_OUT);jog(slave,-1);step();advance(30);
  assert(view.deck[slave].sync.requested&&!view.deck[slave].sync.locked&&view.deck[slave].sync.reason==XZ_EXTRA_SYNC_LOOP_UNALIGNED);
  jog(slave,1);step();press(slave,XZ_EXTRA_IN_LOOP_OUT);advance(8000);
  fprintf(stderr,"SYNC loop restore reason=%d actual_ms=%g\n",view.deck[slave].sync.reason,actual_sync_error(master_deck,slave)*1000);
  assert(view.deck[slave].sync.locked&&fabs(actual_sync_error(master_deck,slave))<=.001);
  position_blocked=1;advance(200);assert(view.deck[slave].sync.requested&&!view.deck[slave].sync.locked);
  position_blocked=0;advance(100);assert(view.deck[slave].sync.requested&&!view.deck[slave].sync.locked&&view.deck[slave].sync.reason==XZ_EXTRA_SYNC_DISCONTINUITY);
  press(slave,XZ_EXTRA_IN_SYNC);press(slave,XZ_EXTRA_IN_SYNC);advance(2000);assert(view.deck[slave].sync.locked);
  press(slave,XZ_EXTRA_IN_MASTER);press(master_deck,XZ_EXTRA_IN_SYNC);advance(2000);
  fprintf(stderr,"handoff req=%d locked=%d reason=%d actual_us=%g underrun=%llu/%llu\n",view.deck[master_deck].sync.requested,view.deck[master_deck].sync.locked,view.deck[master_deck].sync.reason,actual_sync_error(slave,master_deck)*1000000,(unsigned long long)f.position.deck[0].underrun_frames,(unsigned long long)f.position.deck[1].underrun_frames);
  assert(view.deck[slave].master&&!view.deck[master_deck].master&&view.deck[master_deck].sync.locked);
  assert(view.deck[slave].sync.requested&&view.deck[slave].sync.reason==XZ_EXTRA_SYNC_MASTER_CLOCK);
  press(slave,XZ_EXTRA_IN_TEMPO_RESET);assert(view.deck[slave].sync.requested);
  press(slave,XZ_EXTRA_IN_TEMPO_RESET);assert(view.deck[slave].sync.requested);
  assert(fabs(actual_sync_error(slave,master_deck))<=.001);
  /* A new master load cannot silently reuse the preceding track's phase identity. */
  unsigned retained=f.flushes[master_deck];load(slave,1);press(slave,XZ_EXTRA_IN_PLAY);advance(50);
  assert(view.deck[master_deck].sync.requested&&!view.deck[master_deck].sync.locked&&f.flushes[master_deck]==retained);
  advance(2500);assert(debug(slave).state==XZ_XP_ENDED&&!view.deck[master_deck].sync.locked);
  press(master_deck,XZ_EXTRA_IN_TEMPO_RESET);assert(!view.deck[master_deck].sync.requested);
  fake_route(1001);step();assert(!view.deck[slave].sync.locked);
  xz_xp_destroy(p);p=NULL;
 }
 wire_horizon=0;
 fclose(sync_trace_file);sync_trace_file=NULL;
 puts("PASS actual PCM common-wire persistent SYNC, explicit masters, both source rates, tempo steps, cue hold, manual jog and reset");
}
static void test_completed_timeline(void){
 struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,resolve,master_bpm,NULL,NULL};
 for(int side=0;side<2;side++){
  p=xz_xp_create(&env);assert(p);fake_route(900);step();load(side,4);seek(side,700);
  assert((view.deck[side].known&XZ_EXTRA_KNOWN(XZ_EXTRA_F_TIMELINE))&&!view.deck[side].timeline.moving);
  uint32_t idle_revision=xz_xp_revision(p);advance(50);assert(xz_xp_revision(p)==idle_revision&&view.snapshot_revision==idle_revision);
  press(side,XZ_EXTRA_IN_PLAY);advance(30);
  assert(view.deck[side].known&XZ_EXTRA_KNOWN(XZ_EXTRA_F_TIMELINE));
  assert(abs(view.deck[side].timeline.rate_ppm-1000000)<5000);
  uint32_t revision=view.deck[side].timeline.revision;
  jog(side,20);step();advance(70);
  assert(view.deck[side].timeline.rate_ppm>1005000);
  hold(side,XZ_EXTRA_IN_PLAY,1);seek(side,700);hold(side,XZ_EXTRA_IN_VINYL,1);
  hold(side,XZ_EXTRA_IN_JOG_TOUCH,1);jog(side,-8);step();clock_ms++;step();
  advance(6);
  assert(view.deck[side].timeline.moving&&view.deck[side].timeline.rate_ppm<0);
  assert(debug(side).position==debug(side).scratch_target); /* render complete, queued PCM still moving */
  uint32_t queued_revision=xz_xp_revision(p);advance(6);assert(xz_xp_revision(p)!=queued_revision);
  assert(view.deck[side].timeline.revision!=revision);
  advance(100);assert(!view.deck[side].timeline.moving);
  hold(side,XZ_EXTRA_IN_JOG_TOUCH,0);assert(!view.deck[side].timeline.moving);
  uint32_t before=view.deck[side].timeline.revision;seek(side,400);
  assert(view.deck[side].timeline.revision!=before&&view.deck[side].timeline.source_frame==ms_frames(side,400));
  fake_route(901);step();assert(view.deck[side].timeline.route_epoch==901&&!view.deck[side].timeline.moving);
  hold(side,XZ_EXTRA_IN_VINYL,1);seek(side,400);press(side,XZ_EXTRA_IN_PLAY);advance(20);
  assert(view.deck[side].known&XZ_EXTRA_KNOWN(XZ_EXTRA_F_TIMELINE));
  uint32_t observed=view.deck[side].timeline.at_ms,clock_before=clock_ms;
  clock_ms=observed-1;step();assert(!(view.deck[side].timeline.flags&XZ_EXTRA_TIMELINE_RATE));
  clock_ms=observed+151;step();assert(!(view.deck[side].timeline.flags&XZ_EXTRA_TIMELINE_RATE));
  clock_ms=clock_before;
  uint64_t verified=view.deck[side].timeline.source_frame;
  f.position.deck[side].completed_frames+=256;f.position.deck[side].source_completed_output_frames=f.position.deck[side].completed_frames;f.position.deck[side].source_after_completed=UINT64_C(10000000);step();
  assert(!(view.deck[side].timeline.flags&XZ_EXTRA_TIMELINE_RATE)&&view.deck[side].timeline.source_frame==verified);
  xz_xp_destroy(p);p=NULL;
  p=xz_xp_create(&env);assert(p);fake_route(910);step();load(side,4);seek(side,500);
  press(side,XZ_EXTRA_IN_LOOP_IN);seek(side,1000);press(side,XZ_EXTRA_IN_LOOP_OUT);
  uint32_t wrap_revision=view.deck[side].timeline.revision;advance(600);
  assert(view.deck[side].timeline.revision!=wrap_revision);
  assert(view.deck[side].timeline.flags&XZ_EXTRA_TIMELINE_RATE);
  assert(abs(view.deck[side].timeline.rate_ppm-1000000)<5000);
  assert(view.deck[side].timeline.source_frame>=ms_frames(side,500)&&view.deck[side].timeline.source_frame<ms_frames(side,1000));
  /* The same end tag after many output frames admits multiple windings: hold slope unknown. */
  f.position.deck[side].completed_frames+=30000;f.position.deck[side].source_completed_output_frames=f.position.deck[side].completed_frames;step();
  assert(!(view.deck[side].timeline.flags&XZ_EXTRA_TIMELINE_RATE));
  assert(view.deck[side].timeline.flags&XZ_EXTRA_TIMELINE_ANCHOR);
  assert(view.deck[side].timeline.flags&XZ_EXTRA_TIMELINE_MOTION);
  assert(view.deck[side].timeline.flags&XZ_EXTRA_TIMELINE_SINK_FRAME);
  xz_xp_destroy(p);p=NULL;
 }
 puts("PASS completed PCM timeline signed bend/scratch, queued motion, pauses, discontinuity and route anchors");
}
static int order_stale;
static int order_resolve(int source,uint32_t id,uint64_t generation,struct xz_extra_track *out){
 return order_stale?-1:checked_counted(source,id,generation,out);
}
static void test_ordered_next(void){
 uint32_t ids[]={1,2,2,3};uint8_t kinds[]={1,1,1,1};
 for(int side=0;side<2;side++)for(int playing=0;playing<2;playing++){
  struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,resolve,master_bpm,NULL,order_resolve};
  p=xz_xp_create(&env);assert(p);fake_route(500);step();
  struct xz_extra_order *order=xz_extra_order_create(1,42,ids,kinds,4,1);assert(order);
  struct xz_extra_deck_event e=event(side,XZ_EXTRA_IN_LOAD,1);e.load.source=1;e.load.track_id=1;
  e.load.media_generation=42;e.load.route_epoch=500;e.load.order=order;e.load.order_index=0;
  assert(input(e));xz_extra_order_release(order);step();
  assert(xz_extra_order_live()==1);
  if(playing)press(side,XZ_EXTRA_IN_PLAY);
  f.capture[side]=1;f.captured_frames[side]=0;
  press(side,XZ_EXTRA_IN_TRACK_NEXT);
  assert(debug(side).source_rate==48000&&!strcmp(view.deck[side].title,"Track B"));
  assert(debug(side).state==(playing?XZ_XP_PLAYING:XZ_XP_CUED));
  if(playing){assert(f.captured_frames[side]>=32);for(int n=0;n<32;n++)assert(fabs(f.captured[side][n*2]-.5*sin(6.283185307179586*440*n/44100.0))<.001);}
  unsigned keep_flush=f.flushes[side];order_stale=1;
  press(side,XZ_EXTRA_IN_TRACK_NEXT);order_stale=0;
  assert(!strcmp(view.deck[side].title,"Track B")&&f.flushes[side]==keep_flush);
  /* Duplicate ID is a separate occurrence: NEXT must still be track 2. */
  unsigned duplicate_before=f.flushes[side];
  press(side,XZ_EXTRA_IN_TRACK_NEXT);assert(!strcmp(view.deck[side].title,"Track B")&&f.flushes[side]>duplicate_before);
  unsigned flush_before=f.flushes[side];uint64_t at=debug(side).position;
  press(side,XZ_EXTRA_IN_TRACK_NEXT); /* missing.wav: preserve current audio */
  assert(!strcmp(view.deck[side].title,"Track B")&&debug(side).position==at&&f.flushes[side]==flush_before);
  order_stale=1;press(side,XZ_EXTRA_IN_TRACK_NEXT);order_stale=0;
  assert(!strcmp(view.deck[side].title,"Track B")&&f.flushes[side]==flush_before);
  fake_route(501);step();press(side,XZ_EXTRA_IN_TRACK_NEXT);assert(!strcmp(view.deck[side].title,"Track B"));
  xz_xp_destroy(p);p=NULL;assert(!xz_extra_order_live());
 }
 /* End of list, missing context, rejected queue and shutdown ownership. */
 struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,resolve,master_bpm,NULL,order_resolve};
 p=xz_xp_create(&env);assert(p);fake_route(510);step();load(0,1);press(0,XZ_EXTRA_IN_PLAY);
 unsigned before=f.flushes[0];press(0,XZ_EXTRA_IN_TRACK_NEXT);assert(f.flushes[0]==before&&debug(0).state==XZ_XP_PLAYING);
 struct xz_extra_order *o=xz_extra_order_create(1,42,ids,kinds,1,1);assert(o);
 struct xz_extra_deck_event e=event(0,XZ_EXTRA_IN_LOAD,1);e.load.source=1;e.load.track_id=1;e.load.media_generation=42;e.load.route_epoch=510;e.load.order=o;
 assert(input(e));step();xz_extra_order_release(o);before=f.flushes[0];press(0,XZ_EXTRA_IN_TRACK_NEXT);assert(f.flushes[0]==before);
 /* Queued loads hold their references until the queue is drained or destroyed. */
 o=xz_extra_order_create(1,42,ids,kinds,4,1);assert(o);e.load.order=o;
 for(unsigned q=0;q<XZ_XP_QUEUE;q++)assert(input(e));
 assert(!input(e));xz_extra_order_release(o);assert(xz_extra_order_live()==2);
 xz_xp_destroy(p);p=NULL;assert(!xz_extra_order_live());
 struct xz_extra_order *slots[XZ_EXTRA_ORDER_SLOTS];
 for(unsigned i=0;i<XZ_EXTRA_ORDER_SLOTS;i++){slots[i]=xz_extra_order_create(1,42,ids,kinds,4,1);assert(slots[i]);}
 assert(!xz_extra_order_create(1,42,ids,kinds,4,1));
 assert(!xz_extra_order_create(1,42,ids,kinds,XZ_EXTRA_ORDER_ROWS+1,1));
 for(unsigned i=0;i<XZ_EXTRA_ORDER_SLOTS;i++)xz_extra_order_release(slots[i]);assert(!xz_extra_order_live());
 puts("PASS ordered NEXT duplicate occurrence, both decks/states, unreadable/stale/route refusal and reference cleanup");
}

#ifdef XZ_XP_TEST_RENDER_HOOK
static void test_reconnect_during_render(void) {
 struct xz_xp_env env = {{NULL, fake_link, fake_submit, fake_flush, fake_position, fake_transition}, now_ms, resolve, master_bpm, NULL, NULL};
 for (int mode = 0; mode < 2; ++mode) {
  fake_route(100 + mode * 2);
  p = xz_xp_create(&env); assert(p); step();
  if (mode == 0) {
   hold_flush = 1; load(0, 1); hold_flush = 0;
   assert(input(event(0, XZ_EXTRA_IN_PLAY, 1)));
  } else {
   load(0, 1); press(0, XZ_EXTRA_IN_PLAY);
   /* Reconnect handling resumes paused at generation 1; PLAY needs no jump. */
   fake_route(200); step(); press(0, XZ_EXTRA_IN_PLAY);
  }
  unsigned before = f.submits[0], flushes = f.flushes[0];
  reconnect_on_render = 101 + mode * 2;
  if (mode == 0) step(); /* first transition after an initially busy flush */
  else { consume(0, 10000); step(); } /* ordinary render/submit */
  assert(!reconnect_on_render);
  assert(f.submits[0] == before && f.flushes[0] == flushes);
  assert(f.write[0] == 0 && f.queue_generation[0] == XZ_USB_INITIAL_GENERATION);
  step(); assert(debug(0).state == XZ_XP_CUED);
  assert(f.submits[0] == before && f.write[0] == 0);
  xz_xp_destroy(p); p = NULL;
 }
 puts("PASS reconnect during transition and ordinary render rejects old-route PCM");
}
#endif

static void test_threads(void) {
 owner_thread = pthread_self();
 threaded_clock = 1;
 __atomic_store_n(&stop_usb,0,__ATOMIC_RELEASE);
 memset(&f.submits, 0, sizeof f.submits);
 fake_route(3);
 struct xz_xp_env env = {{NULL, fake_link, fake_submit, fake_flush, fake_position, fake_transition}, now_ms, resolve, master_bpm, NULL, NULL};
 env.worker_enter = worker_enter_test;
 p = xz_xp_create(&env); assert(p);
 xz_xp_step(p, 0); assert(!entered_worker[0] && !entered_worker[1]);
 assert(!xz_xp_start(p));
 pthread_t readers[2], u;
 for (int i = 0; i < 2; ++i) assert(!pthread_create(&readers[i], NULL, reader, NULL));
 assert(!pthread_create(&u, NULL, usb, NULL));
 struct xz_extra_deck_event e = event(0, XZ_EXTRA_IN_LOAD, 1);
 e.load.source = 1;
 for (int i = 0; i < 24; ++i) {
  for (int d = 0; d < 2; ++d) {
   e.deck = d; e.load.track_id = (uint32_t)(1 + (i + d) % 2);
   while (!xz_xp_input(p, &e)) { struct timespec t = {0, 1000000}; nanosleep(&t, NULL); }
   struct xz_extra_deck_event play = event(d, XZ_EXTRA_IN_PLAY, 1);
   (void)xz_xp_input(p, &play);
   struct xz_extra_deck_event mt = event(d, XZ_EXTRA_IN_MASTER_TEMPO, 1);
   (void)xz_xp_input(p, &mt);
   struct xz_extra_deck_event pitch = event(d, XZ_EXTRA_IN_TEMPO, 1);
   pitch.tempo_position = (i & 1) ? 5000 : -5000;
   (void)xz_xp_input(p, &pitch);
  }
  struct timespec t = {0, 40000000}; nanosleep(&t, NULL);
 }
 struct timespec settle = {0, 300000000}; nanosleep(&settle, NULL);
 __atomic_store_n(&stop_readers, 1, __ATOMIC_RELEASE);
 for (int i = 0; i < 2; ++i) pthread_join(readers[i], NULL);
 __atomic_store_n(&stop_usb, 1, __ATOMIC_RELEASE);
 pthread_join(u, NULL);
 xz_xp_stop(p);
 assert(entered_worker[0] == 1 && entered_worker[1] == 1);
 assert(snapshots > 1000 && with_wave > 100 && with_b > 100 && f.submits[0] > 0 && f.submits[1] > 0 && f.stale == 0);
 struct xz_xp_debug d0, d1; xz_xp_debug(p, 0, &d0); xz_xp_debug(p, 1, &d1);
 assert(d0.state != XZ_XP_EMPTY && d0.state != XZ_XP_ERROR && d1.state != XZ_XP_EMPTY && d1.state != XZ_XP_ERROR);
 xz_xp_destroy(p); p = NULL;
}

#include "test_extra_player_recovery.h"
#include "test_extra_player_release.h"
#include "test_extra_player_trajectory.h"

int main(int argc, char **argv) {
 assert(argc == 2);
 assert(!pthread_mutex_init(&f.lock, NULL));
 snprintf(fixtures, sizeof fixtures, "%s", argv[1]);
 snprintf(anlz_dir, sizeof anlz_dir, "%s/anlz", argv[1]);
 build_analysis();
 test_parsers();
 test_players();
 test_loop_and_range();
 test_vinyl();
 test_checked_resolver();
 test_completed_timeline();
 test_sync_coordinator();
 test_native_sync_cache();
 test_native_phase_sync();
 test_sync_missing_sources();
 test_sync_nonzero_loop();
 test_sync_concurrent_groups();
 test_ordered_next();
 test_player_recovery();
 test_player_release();
 if(!test_player_trajectory())return 1;
#ifdef XZ_XP_TEST_RENDER_HOOK
 test_reconnect_during_render();
#endif
 test_threads();
 printf("PASS anlz parsers; load/CUED, play/pause at heard, CUE set/sampler/back cue, hot cue store/jump/clear, "
        "tempo rate, jog bend decay and search, SYNC, MASTER TEMPO, SEEK, EOF, 44.1/48 kHz, load while playing, "
        "retire grace, resolver and read failures, route down, full queue, threaded snapshots (%lu)\n", snapshots);
 return 0;
}

