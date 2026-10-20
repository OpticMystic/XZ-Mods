// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include "extra_player.h"
#include "anlz_extra.h"
#include "shell_analysis.h"
#include "audio/stem_media.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CACHE 4096u
#define PWV7_PER_SECOND 150u
#define ONE ((uint64_t)1 << 32)

struct reader {
 struct xz_stem_media *media;
 uint32_t rate;
 uint64_t frames, begin;
 int cached;
 float pcm[(CACHE + 1) * 2];
};
/* Immutable once published; a consumer may hold these pointers until XZ_XP_RETIRE_MS after the next load. */
struct analysis { struct xz_extra_column *columns; uint32_t count, span_ms, normalization; struct xz_extra_beat *grid; uint32_t grid_count; };
struct hot { int set; uint64_t at; uint32_t rgb; };

struct deck {
 int index;
 pthread_mutex_t queue_lock;
 struct xz_extra_deck_event queue[XZ_XP_QUEUE];
 unsigned head, count;
 atomic_int published_state;
 /* Worker only below. */
 enum xz_xp_state state;
 struct reader *r;
 uint64_t pos;                      /* render head, source frames x 2^32 */
 uint64_t cue;
 struct hot hot[XZ_EXTRA_HOT_CUES];
 int32_t tempo_x100;
 float bend; uint32_t bend_at;
 uint32_t epoch, generation; int need_flush;
 uint64_t submitted;                /* output frames accepted in this generation */
 uint32_t pending; uint64_t pending_after, block_start; float pcm[XZ_USB_BLOCK_FRAMES * 2];
 uint64_t heard; uint32_t heard_at; /* last completed source tag, or the position a jump set */
 struct xz_extra_track track;
 struct analysis *analysis;
 struct { struct analysis *a; uint32_t at; } retired[XZ_XP_RETIRE_SLOTS];
 unsigned retired_count;
};

struct xz_xp_players {
 struct xz_xp_env env;
 struct deck deck[XZ_EXTRA_DECKS];
 pthread_mutex_t publish_lock;
 struct xz_extra_deck view[XZ_EXTRA_DECKS];
 char notice[64]; uint32_t notice_at, notice_seq;
 pthread_t thread[XZ_EXTRA_DECKS];
 struct worker { struct xz_xp_players *p; int index; } worker[XZ_EXTRA_DECKS];
 int threads;
 atomic_int quit;
};

static int streaming(enum xz_xp_state s) { return s == XZ_XP_PLAYING || s == XZ_XP_SAMPLING; }
static int loaded(enum xz_xp_state s) { return s == XZ_XP_CUED || streaming(s) || s == XZ_XP_ENDED; }
static int32_t to_ms(const struct deck *d, uint64_t frames) {
 uint64_t ms = d->r->rate ? frames * 1000u / d->r->rate : 0;
 return ms > INT32_MAX ? INT32_MAX : (int32_t)ms;
}
static uint64_t to_frames(const struct deck *d, int32_t ms) {
 uint64_t f = ms > 0 ? (uint64_t)ms * d->r->rate / 1000u : 0;
 return f > d->r->frames ? d->r->frames : f;
}
static double nominal(const struct deck *d) { return 1.0 + d->tempo_x100 / 10000.0; }
static float bend_now(const struct deck *d, uint32_t now) {
 uint32_t since = now - d->bend_at;
 if (d->bend == 0.0f || since >= XZ_XP_BEND_DECAY_MS) return 0.0f;
 return d->bend * (float)(XZ_XP_BEND_DECAY_MS - since) / (float)XZ_XP_BEND_DECAY_MS;
}
static double speed(const struct deck *d, uint32_t now) {
 double s = nominal(d) + bend_now(d, now);
 return s < 0.05 ? 0.05 : s;
}

void xz_xp_notice(struct xz_xp_players *p, const char *text) {
 if (!p || !text) return;
 pthread_mutex_lock(&p->publish_lock);
 snprintf(p->notice, sizeof p->notice, "%s", text);
 p->notice_at = p->env.now_ms(); p->notice_seq++;
 pthread_mutex_unlock(&p->publish_lock);
}

/* ---- reader: POC cache and linear interpolation, at a varying step ---- */
static int fill_cache(struct reader *r, uint64_t at) {
 if (r->cached && at >= r->begin && at < r->begin + CACHE) return 1;
 uint64_t begin = at / CACHE * CACHE;
 if (r->cached && begin == r->begin + CACHE) {
  r->pcm[0] = r->pcm[CACHE * 2]; r->pcm[1] = r->pcm[CACHE * 2 + 1];
  if (xz_stem_media_read(r->media, (int64_t)(begin + 1), CACHE, r->pcm + 2)) return 0;
 } else if (xz_stem_media_read(r->media, (int64_t)begin, CACHE + 1, r->pcm)) return 0;
 r->begin = begin; r->cached = 1; return 1;
}
static int render(struct deck *d, double rate) {
 struct reader *r = d->r;
 uint64_t end = r->frames << 32, step = (uint64_t)((double)r->rate / XZ_USB_RATE * rate * (double)ONE);
 unsigned n = 0;
 d->block_start = d->pos;
 while (n < XZ_USB_BLOCK_FRAMES && d->pos < end) {
  uint64_t at = d->pos >> 32;
  if (!fill_cache(r, at)) return 0;
  unsigned i = (unsigned)(at - r->begin) * 2;
  float fraction = (float)(uint32_t)d->pos * (1.0f / 4294967296.0f);
  for (unsigned ch = 0; ch < 2; ++ch)
   d->pcm[n * 2 + ch] = (r->pcm[i + ch] + fraction * (r->pcm[i + 2 + ch] - r->pcm[i + ch])) * XZ_XP_GAIN;
  d->pos += step; ++n;
 }
 if (d->pos > end) d->pos = end;
 d->pending = n; d->pending_after = d->pos >> 32;
 return 1;
}

/* ---- the deck machine ---- */
static uint64_t heard_now(const struct deck *d, uint32_t now) {
 uint64_t head = d->pos >> 32;
 if (!streaming(d->state)) return head;
 uint64_t h = d->heard + (uint64_t)((double)(uint32_t)(now - d->heard_at) * nominal(d) * d->r->rate / 1000.0);
 return h > head ? head : h;
}
/* Every jump discards what the transport holds and starts a new generation before the next submit. */
static void jump(struct deck *d, uint64_t frame, uint32_t now) {
 if (frame > d->r->frames) frame = d->r->frames;
 d->pos = frame << 32; d->need_flush = 1; d->pending = 0; d->submitted = 0;
 d->heard = frame; d->heard_at = now;
}
static void pause_at_heard(struct deck *d, uint32_t now) {
 jump(d, heard_now(d, now), now);
 d->state = XZ_XP_CUED;
}
static void start(struct deck *d, uint32_t now) {
 d->heard = d->pos >> 32; d->heard_at = now;
 d->state = XZ_XP_PLAYING;
}

static void flush(struct xz_xp_players *p, struct deck *d) {
 const struct xz_xp_sink *s = &p->env.sink;
 if (!d->need_flush) return;
 uint32_t next;
 xz_usb_result r = s->link(s->ctx) ? s->flush(s->ctx, d->index, &next) : XZ_USB_STOPPED;
 if (r == XZ_USB_OK) { d->generation = next; d->submitted = 0; }
 /* BUSY: retry next pass and submit nothing old meanwhile. Down: the next route restarts generations. */
 if (r != XZ_USB_BUSY) d->need_flush = 0;
}

static void retire(struct deck *d, uint32_t now) {
 if (d->analysis) {
  d->retired[d->retired_count].a = d->analysis; d->retired[d->retired_count].at = now; d->retired_count++;
 }
 d->analysis = NULL;
}
static void free_analysis(struct analysis *a) { if (a) { free(a->columns); free(a->grid); free(a); } }
static void collect(struct deck *d, uint32_t now) {
 unsigned kept = 0;
 for (unsigned i = 0; i < d->retired_count; ++i) {
  if (now - d->retired[i].at >= XZ_XP_RETIRE_MS) free_analysis(d->retired[i].a);
  else d->retired[kept++] = d->retired[i];
 }
 d->retired_count = kept;
}

static void publish(struct xz_xp_players *p, struct deck *d, uint32_t now) {
 struct xz_extra_deck v;
 memset(&v, 0, sizeof v);
 enum xz_xp_state s = d->state;
 if (s != XZ_XP_EMPTY && s != XZ_XP_ERROR) {
  v.loaded = 1;
  snprintf(v.title, sizeof v.title, "%s", d->track.title);
  snprintf(v.artist, sizeof v.artist, "%s", d->track.artist);
  snprintf(v.key, sizeof v.key, "%s", d->track.key);
  v.source = d->track.source;
  v.tempo_x100 = d->tempo_x100;
  v.known = XZ_EXTRA_KNOWN(XZ_EXTRA_F_TITLE) | XZ_EXTRA_KNOWN(XZ_EXTRA_F_ARTIST) | XZ_EXTRA_KNOWN(XZ_EXTRA_F_SOURCE) |
            XZ_EXTRA_KNOWN(XZ_EXTRA_F_TEMPO) | XZ_EXTRA_KNOWN(XZ_EXTRA_F_MASTER) | XZ_EXTRA_KNOWN(XZ_EXTRA_F_PLAYING) |
            XZ_EXTRA_KNOWN(XZ_EXTRA_F_HOT_CUES) | (v.key[0] ? XZ_EXTRA_KNOWN(XZ_EXTRA_F_KEY) : 0);
  for (int i = 0; i < XZ_EXTRA_HOT_CUES; ++i)
   if (d->hot[i].set) v.hot_cue[i] = (struct xz_extra_cue){1, to_ms(d, d->hot[i].at), d->hot[i].rgb};
  if (d->track.track_bpm_x100 > 0) {
   v.track_bpm_x100 = d->track.track_bpm_x100;
   v.bpm_x100 = (int32_t)((int64_t)d->track.track_bpm_x100 * (10000 + d->tempo_x100) / 10000);
   v.known |= XZ_EXTRA_KNOWN(XZ_EXTRA_F_BPM);
  }
 }
 if (loaded(s)) {
  v.playing = streaming(s);
  v.total_ms = to_ms(d, d->r->frames);
  if (streaming(s)) { v.elapsed_ms = to_ms(d, d->heard); v.elapsed_at_ms = d->heard_at; }
  else { v.elapsed_ms = to_ms(d, d->pos >> 32); v.elapsed_at_ms = now; }
  v.cue_ms = to_ms(d, d->cue);
  v.known |= XZ_EXTRA_KNOWN(XZ_EXTRA_F_ELAPSED) | XZ_EXTRA_KNOWN(XZ_EXTRA_F_TOTAL) | XZ_EXTRA_KNOWN(XZ_EXTRA_F_CUE) |
             XZ_EXTRA_KNOWN(XZ_EXTRA_F_LOOP);
  const struct analysis *a = d->analysis;
  if (a && a->grid_count) { v.grid = a->grid; v.grid_count = a->grid_count; v.known |= XZ_EXTRA_KNOWN(XZ_EXTRA_F_GRID); }
  if (a && a->count) {
   v.wave = (struct xz_extra_wave){a->columns, a->count, a->span_ms, a->normalization};
   v.known |= XZ_EXTRA_KNOWN(XZ_EXTRA_F_WAVE);
  }
 }
 pthread_mutex_lock(&p->publish_lock);
 p->view[d->index] = v;
 pthread_mutex_unlock(&p->publish_lock);
 atomic_store(&d->published_state, (int)s);
}

static struct analysis *load_analysis(const char *dir, struct xz_anlz_extra *extra) {
 struct analysis *a = calloc(1, sizeof *a);
 memset(extra, 0, sizeof *extra);
 if (!a || !dir[0]) return a;
 struct xz_wave_load w;
 if (!xz_shana_three_band(dir, &w)) {
  /* PWV7 bytes are low, mid, high: the column layout. */
  a->columns = (struct xz_extra_column *)w.bands; a->count = w.count;
  a->span_ms = (uint32_t)((uint64_t)w.count * 1000u / PWV7_PER_SECOND);
  a->normalization = w.normalization;
 }
 xz_anlz_extra_load(dir, extra);
 a->grid = extra->grid; a->grid_count = extra->grid_count;
 extra->grid = NULL;
 return a;
}

static void load(struct xz_xp_players *p, struct deck *d, const struct xz_extra_deck_event *e, uint32_t now) {
 struct xz_extra_track t;
 memset(&t, 0, sizeof t);
 if (!p->env.resolve || p->env.resolve(e->load.source, e->load.track_id, &t)) { xz_xp_notice(p, "TRACK NOT FOUND"); return; }
 t.path[sizeof t.path - 1] = t.anlz_dir[sizeof t.anlz_dir - 1] = t.title[sizeof t.title - 1] = 0;
 t.artist[sizeof t.artist - 1] = t.key[sizeof t.key - 1] = 0;
 if (d->r->media) { jump(d, 0, now); flush(p, d); }
 d->state = XZ_XP_LOADING; d->track = t;
 memset(d->hot, 0, sizeof d->hot);
 publish(p, d, now);
 xz_stem_media_close(d->r->media);
 d->r->media = NULL; d->r->cached = 0;
 retire(d, now);
 struct xz_stem_media *media = xz_stem_media_open(t.path, 0);
 if (!media) { d->state = XZ_XP_ERROR; xz_xp_notice(p, "TRACK CANNOT BE READ"); return; }
 d->r->media = media; d->r->rate = xz_stem_media_rate(media); d->r->frames = xz_stem_media_frames(media);
 struct xz_anlz_extra extra;
 d->analysis = load_analysis(t.anlz_dir, &extra);
 if (d->track.track_bpm_x100 <= 0) d->track.track_bpm_x100 = extra.grid_bpm_x100;
 for (int i = 0; i < XZ_EXTRA_HOT_CUES; ++i)
  if (extra.hot_cue[i].set) d->hot[i] = (struct hot){1, to_frames(d, extra.hot_cue[i].ms), extra.hot_cue[i].rgb};
 xz_anlz_extra_free(&extra);
 struct xz_shell_cue memory[XZ_SHELL_MEMORY_CUES];
 d->cue = t.anlz_dir[0] && xz_shana_memory_cues(t.anlz_dir, memory) > 0 ? to_frames(d, memory[0].ms) : 0;
 jump(d, d->cue, now);
 d->state = XZ_XP_CUED;
}

static void cue_button(struct deck *d, int pressed, uint32_t now) {
 if (!pressed) {
  if (d->state == XZ_XP_SAMPLING) { jump(d, d->cue, now); d->state = XZ_XP_CUED; }
  return;
 }
 if (d->state == XZ_XP_PLAYING || d->state == XZ_XP_ENDED) { jump(d, d->cue, now); d->state = XZ_XP_CUED; }
 else if (d->state == XZ_XP_CUED) {
  if (d->pos == d->cue << 32) { start(d, now); d->state = XZ_XP_SAMPLING; }
  else { d->cue = d->pos >> 32; d->pos = d->cue << 32; }
 }
}

static void hot_cue(struct deck *d, const struct xz_extra_deck_event *e, uint32_t now) {
 if (!e->pressed || e->hot_cue < 0 || e->hot_cue >= XZ_EXTRA_HOT_CUES) return;
 struct hot *h = &d->hot[e->hot_cue];
 if (e->shift) { memset(h, 0, sizeof *h); return; }
 if (!h->set) { *h = (struct hot){1, heard_now(d, now), 0}; return; }
 jump(d, h->at, now);
 start(d, now);
}

static void jog(struct deck *d, int32_t delta, uint32_t now) {
 if (streaming(d->state)) {
  float b = bend_now(d, now) + (float)delta * XZ_XP_BEND_PER_TICK;
  d->bend = b > XZ_XP_BEND_MAX ? XZ_XP_BEND_MAX : b < -XZ_XP_BEND_MAX ? -XZ_XP_BEND_MAX : b;
  d->bend_at = now;
  return;
 }
 int64_t per_tick = (int64_t)d->r->rate * XZ_XP_SEARCH_MS_PER_REV / 1000 / XZ_XP_JOG_TICKS_PER_REV;
 int64_t to = (int64_t)(d->pos >> 32) + (int64_t)delta * per_tick;
 if (to < 0) to = 0;
 jump(d, (uint64_t)to, now);
 d->state = (uint64_t)to >= d->r->frames ? XZ_XP_ENDED : XZ_XP_CUED;
}

static void sync_tempo(struct xz_xp_players *p, struct deck *d) {
 int32_t master = p->env.master_bpm ? p->env.master_bpm() : 0;
 if (d->track.track_bpm_x100 <= 0 || master <= 0) { xz_xp_notice(p, "NO MASTER TO SYNC"); return; }
 int64_t tempo = ((int64_t)master * 10000 + d->track.track_bpm_x100 / 2) / d->track.track_bpm_x100 - 10000;
 if (tempo > XZ_XP_TEMPO_RANGE_X100 || tempo < -XZ_XP_TEMPO_RANGE_X100) { xz_xp_notice(p, "SYNC OUT OF RANGE"); return; }
 d->tempo_x100 = (int32_t)tempo;
}

static void command(struct xz_xp_players *p, struct deck *d, const struct xz_extra_deck_event *e, uint32_t now) {
 if (e->kind == XZ_EXTRA_IN_LOAD) { load(p, d, e, now); return; }
 if (e->kind == XZ_EXTRA_IN_TEMPO) {
  int32_t at = e->tempo_position;
  if (at > XZ_EXTRA_TEMPO_SPAN) at = XZ_EXTRA_TEMPO_SPAN;
  if (at < -XZ_EXTRA_TEMPO_SPAN) at = -XZ_EXTRA_TEMPO_SPAN;
  d->tempo_x100 = (int32_t)((int64_t)at * XZ_XP_TEMPO_RANGE_X100 / XZ_EXTRA_TEMPO_SPAN);
  return;
 }
 if (!loaded(d->state)) return;
 switch (e->kind) {
 case XZ_EXTRA_IN_PLAY:
  if (!e->pressed) break;
  if (d->state == XZ_XP_CUED) start(d, now);
  else if (d->state == XZ_XP_PLAYING) pause_at_heard(d, now);
  else if (d->state == XZ_XP_SAMPLING) d->state = XZ_XP_PLAYING;
  break;
 case XZ_EXTRA_IN_CUE: cue_button(d, e->pressed, now); break;
 case XZ_EXTRA_IN_HOT_CUE: hot_cue(d, e, now); break;
 case XZ_EXTRA_IN_JOG_TURN: jog(d, e->jog.delta, now); break;
 case XZ_EXTRA_IN_SYNC: if (e->pressed) sync_tempo(p, d); break;
 case XZ_EXTRA_IN_SEEK: {
  uint64_t to = to_frames(d, e->seek_ms);
  jump(d, to, now);
  if (!streaming(d->state)) d->state = to >= d->r->frames ? XZ_XP_ENDED : XZ_XP_CUED;
  break;
 }
 default: break;   /* JOG_TOUCH: no vinyl mode yet */
 }
}

static int take(struct deck *d, struct xz_extra_deck_event *e) {
 pthread_mutex_lock(&d->queue_lock);
 int got = d->count > 0;
 if (got) {
  *e = d->queue[d->head];
  /* A load needs a free retire slot; it waits at the head until the oldest arrays expire. */
  if (e->kind == XZ_EXTRA_IN_LOAD && d->retired_count >= XZ_XP_RETIRE_SLOTS) got = 0;
  else { d->head = (d->head + 1) % XZ_XP_QUEUE; d->count--; }
 }
 pthread_mutex_unlock(&d->queue_lock);
 return got;
}

static void stream(struct xz_xp_players *p, struct deck *d, uint32_t now) {
 const struct xz_xp_sink *s = &p->env.sink;
 xz_usb_position position;
 uint64_t staged = 0;
 int applied = 0;
 if (s->position(s->ctx, &position) == XZ_USB_OK) {
  const xz_usb_deck_position *q = &position.deck[d->index];
  applied = q->applied_generation == d->generation;
  if (applied) {
   staged = q->staged_frames;
   if ((q->source_valid & 2) && q->source_after_completed != d->heard) { d->heard = q->source_after_completed; d->heard_at = now; }
   if (!d->pending && d->pos >> 32 >= d->r->frames && q->completed_frames >= d->submitted) {
    d->state = XZ_XP_ENDED; d->pos = d->r->frames << 32; d->heard = d->r->frames;
    return;
   }
  }
 }
 for (int blocks = 0; blocks < 16; ++blocks) {
  if (!d->pending) {
   if (d->pos >> 32 >= d->r->frames || d->submitted - (applied ? staged : 0) >= XZ_XP_AHEAD_FRAMES) return;
   if (!render(d, speed(d, now))) {
    pause_at_heard(d, now); xz_xp_notice(p, "TRACK CANNOT BE READ");
    return;
   }
   if (!d->pending) return;
  }
  xz_usb_result r = s->submit(s->ctx, d->index, d->generation, d->pcm, d->pending, d->pending_after);
  if (r == XZ_USB_OK) { d->submitted += d->pending; d->pending = 0; continue; }
  if (r == XZ_USB_STALE) { d->pos = d->block_start; d->pending = 0; d->need_flush = 1; }
  else if (r != XZ_USB_AGAIN && r != XZ_USB_BUSY) { d->pos = d->block_start; d->pending = 0; }
  return;
 }
}

void xz_xp_step(struct xz_xp_players *p, int index) {
 if (!p || index < 0 || index >= XZ_EXTRA_DECKS) return;
 struct deck *d = &p->deck[index];
 const struct xz_xp_sink *s = &p->env.sink;
 uint32_t now = p->env.now_ms();
 collect(d, now);
 uint32_t link = s->link(s->ctx);
 if (link != d->epoch) {
  /* A new route instance starts its queues at the initial generation; a lost one took the audio. */
  if (streaming(d->state)) pause_at_heard(d, now);
  d->epoch = link; d->generation = XZ_USB_INITIAL_GENERATION;
  d->need_flush = 0; d->pending = 0; d->submitted = 0;
 }
 struct xz_extra_deck_event e;
 while (take(d, &e)) command(p, d, &e, now);
 flush(p, d);
 if (streaming(d->state) && link && !d->need_flush) stream(p, d, now);
 publish(p, d, now);
}

int xz_xp_input(struct xz_xp_players *p, const struct xz_extra_deck_event *e) {
 if (!p || !e || e->deck < 0 || e->deck >= XZ_EXTRA_DECKS || (unsigned)e->kind >= XZ_EXTRA_INPUT_COUNT) return 0;
 if (!p->env.sink.link(p->env.sink.ctx)) return 0;
 if (e->kind == XZ_EXTRA_IN_MASTER_TEMPO) {
  if (e->pressed) xz_xp_notice(p, "MASTER TEMPO IS NOT AVAILABLE ON DECK 3/4");
  return 0;
 }
 if (e->kind == XZ_EXTRA_IN_LOAD && !p->env.resolve) { xz_xp_notice(p, "TRACK NOT FOUND"); return 0; }
 struct deck *d = &p->deck[e->deck];
 int state = atomic_load(&d->published_state);
 int button = e->kind == XZ_EXTRA_IN_PLAY || e->kind == XZ_EXTRA_IN_CUE || e->kind == XZ_EXTRA_IN_SYNC ||
              e->kind == XZ_EXTRA_IN_HOT_CUE || e->kind == XZ_EXTRA_IN_JOG_TOUCH;
 int release = button && !e->pressed;
 if ((state == XZ_XP_EMPTY || state == XZ_XP_ERROR) && !release && e->kind != XZ_EXTRA_IN_LOAD && e->kind != XZ_EXTRA_IN_TEMPO)
  return 0;
 pthread_mutex_lock(&d->queue_lock);
 int taken = d->count < XZ_XP_QUEUE;
 if (taken) { d->queue[(d->head + d->count) % XZ_XP_QUEUE] = *e; d->count++; }
 pthread_mutex_unlock(&d->queue_lock);
 return taken;
}

void xz_xp_read(struct xz_xp_players *p, struct xz_extra_decks *out) {
 if (!p || !out) return;
 pthread_mutex_lock(&p->publish_lock);
 memcpy(out->deck, p->view, sizeof out->deck);
 memcpy(out->notice, p->notice, sizeof out->notice);
 out->notice_at_ms = p->notice_at; out->notice_seq = p->notice_seq;
 pthread_mutex_unlock(&p->publish_lock);
}

void xz_xp_debug(struct xz_xp_players *p, int index, struct xz_xp_debug *out) {
 const struct deck *d = &p->deck[index];
 *out = (struct xz_xp_debug){d->state, d->pos >> 32, d->cue, d->generation, d->r->rate, d->retired_count,
                             speed(d, p->env.now_ms())};
}

struct xz_xp_players *xz_xp_create(const struct xz_xp_env *env) {
 if (!env || !env->sink.link || !env->sink.submit || !env->sink.flush || !env->sink.position || !env->now_ms) return NULL;
 struct xz_xp_players *p = calloc(1, sizeof *p);
 if (!p) return NULL;
 p->env = *env;
 int mutexes = 0;
 if (pthread_mutex_init(&p->publish_lock, NULL)) { free(p); return NULL; }
 for (; mutexes < XZ_EXTRA_DECKS; ++mutexes) {
  struct deck *d = &p->deck[mutexes];
  d->index = mutexes; d->generation = XZ_USB_INITIAL_GENERATION;
  atomic_init(&d->published_state, XZ_XP_EMPTY);
  if (!(d->r = calloc(1, sizeof *d->r)) || pthread_mutex_init(&d->queue_lock, NULL)) { free(d->r); break; }
 }
 if (mutexes < XZ_EXTRA_DECKS) {
  while (mutexes--) { pthread_mutex_destroy(&p->deck[mutexes].queue_lock); free(p->deck[mutexes].r); }
  pthread_mutex_destroy(&p->publish_lock); free(p); return NULL;
 }
 atomic_init(&p->quit, 0);
 return p;
}

static void pause_ms(unsigned ms) { struct timespec t = {0, (long)ms * 1000000L}; nanosleep(&t, NULL); }
static void *worker(void *arg) {
 const struct worker *w = arg;
 while (!atomic_load(&w->p->quit)) {
  xz_xp_step(w->p, w->index);
  /* Without the route a deck only waits for it; a cable-less XZ should not pay for the players. */
  pause_ms(streaming(w->p->deck[w->index].state) ? 2 : w->p->env.sink.link(w->p->env.sink.ctx) ? 5 : 20);
 }
 return NULL;
}

int xz_xp_start(struct xz_xp_players *p) {
 if (!p || p->threads) return -1;
 atomic_store(&p->quit, 0);
 for (; p->threads < XZ_EXTRA_DECKS; ++p->threads)
{
  p->worker[p->threads] = (struct worker){p, p->threads};
  if (pthread_create(&p->thread[p->threads], NULL, worker, &p->worker[p->threads])) { xz_xp_stop(p); return -1; }
 }
 return 0;
}

void xz_xp_stop(struct xz_xp_players *p) {
 if (!p) return;
 atomic_store(&p->quit, 1);
 while (p->threads) pthread_join(p->thread[--p->threads], NULL);
}

void xz_xp_destroy(struct xz_xp_players *p) {
 if (!p) return;
 xz_xp_stop(p);
 for (int i = 0; i < XZ_EXTRA_DECKS; ++i) {
  struct deck *d = &p->deck[i];
  xz_stem_media_close(d->r->media);
  free_analysis(d->analysis);
  for (unsigned k = 0; k < d->retired_count; ++k) free_analysis(d->retired[k].a);
  pthread_mutex_destroy(&d->queue_lock);
  free(d->r);
 }
 pthread_mutex_destroy(&p->publish_lock);
 free(p);
}
