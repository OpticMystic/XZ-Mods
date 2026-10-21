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
 float peak[2];
 int capture[2]; uint32_t captured_frames[2]; float captured[2][XZ_USB_BLOCK_FRAMES*2];
} f;
/* ui/layered_wave_source.c links the stem loader; the players read no stems. */
int xz_oc_bundle(const char *source, char *folder, size_t capacity) { (void)source; (void)folder; (void)capacity; return -1; }
static uint32_t clock_ms = 1000;
static int threaded_clock;
static int hold_flush;

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
 pthread_mutex_unlock(&f.lock);
 return XZ_USB_OK;
}
static xz_usb_result fake_position(void *ctx, uint32_t epoch, xz_usb_position *out) {
 (void)ctx;
 pthread_mutex_lock(&f.lock);
 if (!epoch || epoch != f.link) { pthread_mutex_unlock(&f.lock); return XZ_USB_STOPPED; }
 *out = f.position; out->state = XZ_USB_RUNNING;out->requested_generation[0]=f.queue_generation[0];out->requested_generation[1]=f.queue_generation[1];
 pthread_mutex_unlock(&f.lock);
 return XZ_USB_OK;
}
/* USB time passing: whole blocks of the applied generation complete, older ones are discarded. */
static void consume(int d, double frames) {
 pthread_mutex_lock(&f.lock);
 xz_usb_deck_position *p = &f.position.deck[d];
 if (p->applied_generation != f.queue_generation[d]) {
  memset(p, 0, sizeof *p); p->applied_generation = f.queue_generation[d];
 }
 f.credit[d] += frames;
 while (f.read[d] != f.write[d]) {
  struct block *b = &f.queue[d][f.read[d] % FAKE_BLOCKS];
  if (b->generation < p->applied_generation) { f.read[d]++; continue; }
  if (b->generation > p->applied_generation || f.credit[d] < b->frames) break;
  f.credit[d] -= b->frames; f.read[d]++;
  p->staged_frames += b->frames; p->completed_frames += b->frames;
  p->source_after_staged = p->source_after_completed = b->after; p->source_valid = 3;
 }
 if (f.read[d] == f.write[d]) f.credit[d] = 0;
 pthread_mutex_unlock(&f.lock);
}
static void fake_route(uint32_t link) {
 pthread_mutex_lock(&f.lock);
 f.link = link;
 for (int d = 0; d < 2; ++d) {
  f.queue_generation[d] = XZ_USB_INITIAL_GENERATION; f.read[d] = f.write[d] = 0; f.credit[d] = 0;
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
 syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &t);
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
static void step(void) { xz_xp_step(p, 0); xz_xp_step(p, 1); xz_xp_read(p, &view); }
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
 struct xz_xp_env env = {{NULL, fake_link, fake_submit, fake_flush, fake_position, fake_transition}, now_ms, resolve, master_bpm};
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
 int64_t per_tick = 44100 * XZ_XP_SEARCH_MS_PER_REV / 1000 / XZ_XP_JOG_TICKS_PER_REV;
 assert(debug(0).position == at + (uint64_t)(per_tick * XZ_XP_JOG_TICKS_PER_REV) && debug(0).state == XZ_XP_CUED);
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
 expect_notice("NO MASTER TO SYNC", &seq);
 assert(view.deck[0].tempo_x100 == 0);
 master = 12600;
 assert(input(event(0, XZ_EXTRA_IN_SYNC, 1))); step();
 assert(view.deck[0].tempo_x100 == 500 && view.deck[0].bpm_x100 == 12600);
 master = 15000;
 assert(input(event(0, XZ_EXTRA_IN_SYNC, 1))); step();
 expect_notice("SYNC OUT OF RANGE", &seq);
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
 struct xz_xp_env env = {{NULL, fake_link, fake_submit, fake_flush, fake_position, fake_transition}, now_ms, resolve, master_bpm};
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
 struct xz_xp_env env = {{NULL, fake_link, fake_submit, fake_flush, fake_position, fake_transition}, now_ms, resolve, master_bpm};
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
  assert(fabs(f.captured[0][n*2]-.5*sin(6.283185307179586*440*(at-1.25*n)/44100.0))<.001);
 assert(debug(0).position<at&&debug(0).generation==generation+1);
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
static void test_reconnect_during_render(void) {
 struct xz_xp_env env = {{NULL, fake_link, fake_submit, fake_flush, fake_position, fake_transition}, now_ms, resolve, master_bpm};
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

static void test_threads(void) {
 threaded_clock = 1;
 memset(&f.submits, 0, sizeof f.submits);
 fake_route(3);
 struct xz_xp_env env = {{NULL, fake_link, fake_submit, fake_flush, fake_position, fake_transition}, now_ms, resolve, master_bpm};
 p = xz_xp_create(&env); assert(p);
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
 assert(snapshots > 1000 && with_wave > 100 && with_b > 100 && f.submits[0] > 0 && f.submits[1] > 0 && f.stale == 0);
 struct xz_xp_debug d0, d1; xz_xp_debug(p, 0, &d0); xz_xp_debug(p, 1, &d1);
 assert(d0.state != XZ_XP_EMPTY && d0.state != XZ_XP_ERROR && d1.state != XZ_XP_EMPTY && d1.state != XZ_XP_ERROR);
 xz_xp_destroy(p); p = NULL;
}

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
 test_reconnect_during_render();
 test_threads();
 printf("PASS anlz parsers; load/CUED, play/pause at heard, CUE set/sampler/back cue, hot cue store/jump/clear, "
        "tempo rate, jog bend decay and search, SYNC, MASTER TEMPO, SEEK, EOF, 44.1/48 kHz, load while playing, "
        "retire grace, resolver and read failures, route down, full queue, threaded snapshots (%lu)\n", snapshots);
 return 0;
}
