// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include "extra_player.h"
#include "anlz_extra.h"
#include "shell_analysis.h"
#include "audio/stem_media.h"
#include "audio/stretch.h"
#include <pthread.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CACHE 4096u
#define PWV7_PER_SECOND 150u
#define ONE ((uint64_t)1 << 32)

struct xz_extra_order { atomic_uint refs; int source; uint64_t media_generation; uint32_t count; uint32_t id[]; };
static atomic_uint order_live;
unsigned xz_extra_order_live(void){return atomic_load(&order_live);}
struct xz_extra_order *xz_extra_order_create(int source,uint64_t media_generation,
 const uint32_t *ids,const uint8_t *kinds,uint32_t count,uint8_t track_kind){
 if(source<1||source>2||!media_generation||!ids||!kinds||!count||count>XZ_EXTRA_ORDER_ROWS)return NULL;
 unsigned live=atomic_load(&order_live);
 do {if(live>=XZ_EXTRA_ORDER_SLOTS)return NULL;}while(!atomic_compare_exchange_weak(&order_live,&live,live+1));
 struct xz_extra_order *o=malloc(sizeof *o+(size_t)count*sizeof *ids);
 if(!o){atomic_fetch_sub(&order_live,1);return NULL;}
 atomic_init(&o->refs,1);o->source=source;o->media_generation=media_generation;o->count=count;
 for(uint32_t i=0;i<count;i++)o->id[i]=kinds[i]==track_kind?ids[i]:0;
 return o;
}
void xz_extra_order_retain(struct xz_extra_order *o){if(o)atomic_fetch_add(&o->refs,1);}
void xz_extra_order_release(struct xz_extra_order *o){if(o&&atomic_fetch_sub(&o->refs,1)==1){free(o);atomic_fetch_sub(&order_live,1);}}
int xz_extra_order_matches(const struct xz_extra_order *o,int source,uint64_t generation,uint32_t row,uint32_t id){
 return o&&o->source==source&&o->media_generation==generation&&row<o->count&&o->id[row]&&o->id[row]==id;
}

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
 int32_t tempo_position;
 int tempo_range, tempo_reset, vinyl, master_tempo;
 uint32_t tempo_revision;
 struct xz_stretch stretch;
 struct {
  enum xz_xp_state resume;
  uint64_t target;
  double rate;
  uint32_t at, interval;
  int64_t ticks;
 } scratch;
 struct {
  uint64_t in, out, anchor;
  int has_in, valid, active, edit, held, in_down, in_shift;
  uint32_t edit_at, in_at;
 } loop;
 float bend; uint32_t bend_at;
 uint32_t epoch, generation; int need_flush;
 uint64_t submitted;                /* output frames accepted in this generation */
 uint64_t completed;
 uint32_t pending; uint64_t pending_after, block_start; float pcm[XZ_USB_BLOCK_FRAMES * 2];
 uint64_t heard; uint32_t heard_at; /* last completed source tag, or the position a jump set */
 double heard_rate;
 int timeline_valid, timeline_anchor_valid;
 uint32_t timeline_revision, load_revision;
 uint64_t media_generation;
 uint64_t timeline_output_frames, timeline_sink_frame;
 struct xz_extra_track track;
 struct xz_extra_order *order;
 uint32_t order_index, order_epoch;
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
 atomic_uint revision;
};
uint32_t xz_xp_revision(const struct xz_xp_players *p){return p?atomic_load_explicit(&p->revision,memory_order_acquire):0;}

static int streaming(enum xz_xp_state s) { return s == XZ_XP_PLAYING || s == XZ_XP_SAMPLING || s == XZ_XP_SCRATCHING; }
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
 if(d->state==XZ_XP_SCRATCHING){
  if(d->pos==d->scratch.target)return 0;
  return (d->scratch.target>d->pos?1:-1)*fabs(d->scratch.rate);
 }
 double s = nominal(d) + bend_now(d, now);
 return s < 0.0 ? 0.0 : s;
}
static uint64_t loop_frame(const struct deck *d, uint64_t frame) {
 if (d->loop.active && d->loop.out > d->loop.in && frame >= d->loop.out)
  return d->loop.in + (frame - d->loop.in) % (d->loop.out - d->loop.in);
 return frame > d->r->frames ? d->r->frames : frame;
}

void xz_xp_notice(struct xz_xp_players *p, const char *text) {
 if (!p || !text) return;
 pthread_mutex_lock(&p->publish_lock);
 snprintf(p->notice, sizeof p->notice, "%s", text);
 p->notice_at = p->env.now_ms(); p->notice_seq++;
 atomic_fetch_add_explicit(&p->revision,1,memory_order_release);
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
static int sample_at(struct deck *d, uint64_t position, float out[2],int wrap_loop) {
 struct reader *r = d->r;
 uint64_t at = position >> 32;
 if (!fill_cache(r, at)) return 0;
 unsigned i = (unsigned)(at - r->begin) * 2;
 float next[2] = {r->pcm[i + 2], r->pcm[i + 3]};
 out[0] = r->pcm[i]; out[1] = r->pcm[i + 1];
 if (wrap_loop && d->loop.active && at + 1 >= d->loop.out) {
  if (!fill_cache(r, d->loop.in)) return 0;
  i = (unsigned)(d->loop.in - r->begin) * 2;
  next[0] = r->pcm[i]; next[1] = r->pcm[i + 1];
 }
 float fraction = (float)(uint32_t)position * (1.0f / 4294967296.0f);
 for (unsigned ch = 0; ch < 2; ++ch) out[ch] = (out[ch] + fraction * (next[ch] - out[ch])) * XZ_XP_GAIN;
 return 1;
}
static int stretch_read(void *context,int64_t first,uint32_t count,float *out) {
 struct deck *d=context;
 if(!d->loop.active)return xz_stem_media_read(d->r->media,first,count,out);
 uint64_t length=d->loop.out-d->loop.in;
 int64_t relative=(first-(int64_t)d->loop.in)%(int64_t)length;
 if(relative<0)relative+=(int64_t)length;
 uint64_t at=d->loop.in+(uint64_t)relative;
 uint32_t done=(uint32_t)(d->loop.out-at);
 if(done>count)done=count;
 if(xz_stem_media_read(d->r->media,(int64_t)at,done,out))return -1;
 if(done==count)return 0;
 uint32_t period=count-done;
 if(period>length)period=(uint32_t)length;
 uint32_t start=done;
 if(xz_stem_media_read(d->r->media,(int64_t)d->loop.in,period,out+done*2))return -1;
 done+=period;
 while(done<count){
  uint32_t n=count-done;if(n>period)n=period;
  memcpy(out+done*2,out+start*2,n*2*sizeof(float));done+=n;
 }
 return 0;
}
static uint64_t advance_position(const struct deck *d,uint64_t position,uint64_t step) {
 uint64_t end=d->r->frames<<32;
 if(d->loop.active){
  uint64_t lo=d->loop.in<<32,hi=d->loop.out<<32,span=hi-lo;
  if(position>=hi)position=lo+(position-lo)%span;
  if(step>=hi-position)return lo+(step-(hi-position))%span;
 }
 return position+(step>end-position?end-position:step);
}
static int render(struct deck *d, double rate) {
#ifdef XZ_XP_TEST_RENDER_HOOK
 extern void xz_xp_test_render_hook(void);
 xz_xp_test_render_hook();
#endif
 struct reader *r = d->r;
 uint64_t end = r->frames << 32;
 unsigned n = 0;
 d->block_start = d->pos;
 if(d->state==XZ_XP_SCRATCHING){
  uint64_t step=(uint64_t)((double)r->rate/XZ_USB_RATE*fabs(rate)*(double)ONE);
  while(step&&n<XZ_USB_BLOCK_FRAMES&&d->pos!=d->scratch.target){
   if(!sample_at(d,d->pos>=end?end-ONE:d->pos,d->pcm+n*2,0))return 0;
   if(d->pos<d->scratch.target){
    uint64_t distance=d->scratch.target-d->pos;d->pos+=step>distance?distance:step;
   }else{
    uint64_t distance=d->pos-d->scratch.target;d->pos-=step>distance?distance:step;
   }
   n++;
  }
  d->pending=n;d->pending_after=d->pos>>32;return 1;
 }
 uint64_t step = (uint64_t)((double)r->rate / XZ_USB_RATE * rate * (double)ONE);
 if (!step) { d->pending = 0; return 1; }
 if(d->master_tempo){
  uint64_t after=d->pos;
  while(n<XZ_USB_BLOCK_FRAMES&&after<end){after=advance_position(d,after,step);n++;}
  if(n&&!xz_stretch_render(&d->stretch,(double)d->pos/(double)ONE,(double)step/(double)ONE,
                           r->rate,stretch_read,d,d->pcm,n))return 0;
  for(unsigned i=0;i<n*2;i++)d->pcm[i]*=XZ_XP_GAIN;
  d->pos=after;d->pending=n;d->pending_after=after>>32;return 1;
 }
 while (n < XZ_USB_BLOCK_FRAMES && d->pos < end) {
  if (d->loop.active && d->pos >= d->loop.out << 32)
   d->pos = (d->loop.in << 32) + (d->pos - (d->loop.in << 32)) % ((d->loop.out - d->loop.in) << 32);
  if (!sample_at(d, d->pos, d->pcm + n * 2,1)) return 0;
  d->pos=advance_position(d,d->pos,step);
  ++n;
 }
 if (d->pos > end) d->pos = end;
 d->pending = n; d->pending_after = d->pos >> 32;
 return 1;
}

/* ---- the deck machine ---- */
static uint64_t heard_now(const struct deck *d, uint32_t now) {
 uint64_t head = d->pos >> 32;
 if (!streaming(d->state)) return head;
 if(!d->timeline_valid)return d->heard;
 uint32_t age=now-d->heard_at;
 if(age>150)return d->heard; /* stale or future/wrapped observation: hold its verified tag */
 double rate=d->heard_rate;
 double at=(double)d->heard+(double)age*rate*d->r->rate/1000.;
 if(at<0)at=0;
 if(d->state!=XZ_XP_SCRATCHING&&d->loop.active)return loop_frame(d,(uint64_t)at);
 if(rate<0&&at<head)at=(double)head;
 if(rate>=0&&at>head)at=(double)head;
 return (uint64_t)at;
}
/* Every jump discards what the transport holds and starts a new generation before the next submit. */
static void jump(struct deck *d, uint64_t frame, uint32_t now) {
 if (frame > d->r->frames) frame = d->r->frames;
 d->pos = frame << 32; d->need_flush = 1; d->pending = 0; d->submitted = d->completed = 0;
 d->heard = frame; d->heard_at = now;
 d->heard_rate=0;d->timeline_valid=d->timeline_anchor_valid=0;d->timeline_revision++;
 d->timeline_output_frames=d->timeline_sink_frame=0;
 if(d->state==XZ_XP_SCRATCHING){
  d->scratch.target=d->pos;d->scratch.rate=0;d->scratch.at=0;d->scratch.ticks=0;
 }
 xz_stretch_reset(&d->stretch);
}
static void pause_at_heard(struct deck *d, uint32_t now) {
 jump(d, heard_now(d, now), now);
 d->state = XZ_XP_CUED;
}
static void start(struct deck *d, uint32_t now) {
 d->heard = d->pos >> 32; d->heard_at = now;
 d->timeline_valid=0;
 d->state = XZ_XP_PLAYING;
}
static void scratch_release(struct deck *d,uint32_t now){
 if(d->state!=XZ_XP_SCRATCHING)return;
 enum xz_xp_state resume=d->scratch.resume;
 uint64_t at=loop_frame(d,heard_now(d,now));
 jump(d,at,now);
 d->state=resume;
}
static void scratch_touch(struct deck *d,int pressed,uint32_t now){
 if(!d->vinyl)return;
 if(!pressed){scratch_release(d,now);return;}
 if(d->state==XZ_XP_SCRATCHING)return;
 enum xz_xp_state resume=streaming(d->state)?d->state:XZ_XP_CUED;
 jump(d,heard_now(d,now),now);
 memset(&d->scratch,0,sizeof d->scratch);
 d->scratch.resume=resume;d->scratch.target=d->pos;d->scratch.interval=16;
 d->state=XZ_XP_SCRATCHING;
}

static void flush(struct xz_xp_players *p, struct deck *d) {
 const struct xz_xp_sink *s = &p->env.sink;
 if (!d->need_flush) return;
 uint32_t next;
 int first = streaming(d->state) && d->pending;
 xz_usb_result r = !s->link(s->ctx) ? XZ_USB_STOPPED : first
  ? s->transition(s->ctx, d->epoch, d->index, d->generation, d->pcm, d->pending, d->pending_after, &next)
  : s->flush(s->ctx, d->epoch, d->index, &next);
 if (r == XZ_USB_OK) {
  d->generation = next; d->submitted = d->completed = 0;
  if (first) { d->submitted = d->pending; d->pending = 0; }
 }
 /* Busy/full retries keep the prepared target private. A newer command can replace it. */
 if (r != XZ_USB_BUSY && r != XZ_USB_AGAIN && r != XZ_USB_STALE) d->need_flush = 0;
 if (r == XZ_USB_STALE) {
  xz_usb_position position;
  if (s->position(s->ctx, d->epoch, &position) == XZ_USB_OK && position.requested_generation[d->index])
   d->generation = position.requested_generation[d->index];
 }
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
  v.tempo_range = d->tempo_range;
  v.vinyl = d->vinyl;
  v.master_tempo = d->master_tempo;
  v.tempo_revision = d->tempo_revision;
  v.known = XZ_EXTRA_KNOWN(XZ_EXTRA_F_TITLE) | XZ_EXTRA_KNOWN(XZ_EXTRA_F_ARTIST) | XZ_EXTRA_KNOWN(XZ_EXTRA_F_SOURCE) |
            XZ_EXTRA_KNOWN(XZ_EXTRA_F_TEMPO) | XZ_EXTRA_KNOWN(XZ_EXTRA_F_MASTER) | XZ_EXTRA_KNOWN(XZ_EXTRA_F_PLAYING) |
            XZ_EXTRA_KNOWN(XZ_EXTRA_F_HOT_CUES) | XZ_EXTRA_KNOWN(XZ_EXTRA_F_TEMPO_RANGE) |
            XZ_EXTRA_KNOWN(XZ_EXTRA_F_VINYL) | XZ_EXTRA_KNOWN(XZ_EXTRA_F_MASTER_TEMPO) |
            (v.key[0] ? XZ_EXTRA_KNOWN(XZ_EXTRA_F_KEY) : 0);
  for (int i = 0; i < XZ_EXTRA_HOT_CUES; ++i)
   if (d->hot[i].set) v.hot_cue[i] = (struct xz_extra_cue){1, to_ms(d, d->hot[i].at), d->hot[i].rgb};
  if (d->track.track_bpm_x100 > 0) {
   v.track_bpm_x100 = d->track.track_bpm_x100;
   v.bpm_x100 = (int32_t)((int64_t)d->track.track_bpm_x100 * (10000 + d->tempo_x100) / 10000);
   v.known |= XZ_EXTRA_KNOWN(XZ_EXTRA_F_BPM);
  }
 }
 if (loaded(s)) {
  v.playing = streaming(s) && s != XZ_XP_SCRATCHING;
  v.total_ms = to_ms(d, d->r->frames);
  if (streaming(s)) { v.elapsed_ms = to_ms(d, d->heard); v.elapsed_at_ms = d->heard_at; }
  else { v.elapsed_ms = to_ms(d, d->pos >> 32); v.elapsed_at_ms = now; }
  v.cue_ms = to_ms(d, d->cue);
  v.loop.active = d->loop.active;
  v.loop.in_ms = to_ms(d, d->loop.in);
  v.loop.out_ms = to_ms(d, d->loop.out);
  v.timeline=(struct xz_extra_timeline){.source_frame=streaming(s)?d->heard:d->pos>>32,.sink_frame=d->timeline_sink_frame,
   .source_rate=d->r->rate,.at_ms=streaming(s)?d->heard_at:now,.revision=d->timeline_revision,
   .route_epoch=d->epoch,.load_revision=d->load_revision,.transport_generation=d->generation,
   .media_generation=d->media_generation,.rate_ppm=(int32_t)llround(d->heard_rate*1000000.),
   .moving=streaming(s)&&(d->submitted>d->completed||d->pending)};
  if(!v.timeline.moving)v.timeline.rate_ppm=0;
  v.timeline.flags=XZ_EXTRA_TIMELINE_MOTION;
  if(!streaming(s))v.timeline.flags|=XZ_EXTRA_TIMELINE_ANCHOR|XZ_EXTRA_TIMELINE_RATE;
  else if(!d->need_flush&&d->timeline_anchor_valid&&(uint32_t)(now-d->heard_at)<=150u){
   v.timeline.flags|=XZ_EXTRA_TIMELINE_ANCHOR;
   if(d->timeline_valid)v.timeline.flags|=XZ_EXTRA_TIMELINE_RATE;
   if(d->timeline_sink_frame)v.timeline.flags|=XZ_EXTRA_TIMELINE_SINK_FRAME;
  }
  v.known|=XZ_EXTRA_KNOWN(XZ_EXTRA_F_TIMELINE);
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
 const struct xz_extra_deck *before=&p->view[d->index];
 if(!v.timeline.moving&&!before->timeline.moving&&v.elapsed_ms==before->elapsed_ms&&
    v.timeline.source_frame==before->timeline.source_frame&&v.timeline.revision==before->timeline.revision){
  v.elapsed_at_ms=before->elapsed_at_ms;v.timeline.at_ms=before->timeline.at_ms;
 }
 if(memcmp(before,&v,sizeof v)){
  p->view[d->index]=v;atomic_fetch_add_explicit(&p->revision,1,memory_order_release);
 }
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

static int load(struct xz_xp_players *p, struct deck *d, const struct xz_extra_deck_event *e, uint32_t now, int next) {
 struct xz_extra_track t;
 if(e->load.route_epoch && e->load.route_epoch != p->env.sink.link(p->env.sink.ctx)) { xz_xp_notice(p,"ROUTE CHANGED / LOAD REFUSED"); return 0; }
 memset(&t, 0, sizeof t);
 int resolved;
 if (e->load.media_generation)
  resolved = p->env.resolve_checked ? p->env.resolve_checked(e->load.source, e->load.track_id, e->load.media_generation, &t) : -1;
 else resolved = p->env.resolve ? p->env.resolve(e->load.source, e->load.track_id, &t) : -1;
 if (resolved) { xz_xp_notice(p, e->load.media_generation ? "SOURCE CHANGED / LOAD REFUSED" : "TRACK NOT FOUND"); return 0; }
 if(e->load.route_epoch && e->load.route_epoch != p->env.sink.link(p->env.sink.ctx)) { xz_xp_notice(p,"ROUTE CHANGED / LOAD REFUSED"); return 0; }
 t.path[sizeof t.path - 1] = t.anlz_dir[sizeof t.anlz_dir - 1] = t.title[sizeof t.title - 1] = 0;
 t.artist[sizeof t.artist - 1] = t.key[sizeof t.key - 1] = 0;
 struct xz_stem_media *prepared=NULL;
 if(next){
  prepared=xz_stem_media_open(t.path,0);
  if(!prepared){xz_xp_notice(p,"NEXT TRACK CANNOT BE READ");return 0;}
  float first[XZ_USB_BLOCK_FRAMES*2];
  if(!xz_stem_media_frames(prepared)||xz_stem_media_read(prepared,0,XZ_USB_BLOCK_FRAMES,first)){
   xz_stem_media_close(prepared);xz_xp_notice(p,"NEXT TRACK CANNOT BE READ");return 0;
  }
  struct xz_extra_track verify;
  int stale=!p->env.resolve_checked||p->env.resolve_checked(e->load.source,e->load.track_id,e->load.media_generation,&verify);
  if(stale||e->load.route_epoch!=p->env.sink.link(p->env.sink.ctx)){
   xz_stem_media_close(prepared);xz_xp_notice(p,stale?"SOURCE CHANGED / NEXT REFUSED":"ROUTE CHANGED / NEXT REFUSED");return 0;
  }
 }
 if (d->r->media&&!next) { jump(d, 0, now); flush(p, d); }
 d->state = XZ_XP_LOADING; d->track = t;
 memset(&d->loop, 0, sizeof d->loop);
 memset(d->hot, 0, sizeof d->hot);
 publish(p, d, now);
 xz_stem_media_close(d->r->media);
 d->r->media = NULL; d->r->cached = 0;
 retire(d, now);
 struct xz_stem_media *media = prepared?prepared:xz_stem_media_open(t.path, 0);
 if (!media) { xz_extra_order_release(d->order);d->order=NULL;d->state = XZ_XP_ERROR; xz_xp_notice(p, "TRACK CANNOT BE READ"); return 0; }
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
 d->load_revision++;d->media_generation=e->load.media_generation;
 struct xz_extra_order *order=xz_extra_order_matches(e->load.order,e->load.source,e->load.media_generation,e->load.order_index,e->load.track_id)?e->load.order:NULL;
 xz_extra_order_retain(order);xz_extra_order_release(d->order);d->order=order;
 d->order_index=e->load.order_index;d->order_epoch=e->load.route_epoch;
 return 1;
}

static void cue_button(struct deck *d, int pressed, uint32_t now) {
 if(d->state==XZ_XP_SCRATCHING&&(pressed||d->scratch.resume==XZ_XP_SAMPLING))
  scratch_release(d,now);
 if (pressed) { d->loop.active = 0; d->loop.edit = 0; }
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
 d->loop.active = 0; d->loop.edit = 0;
 jump(d, h->at, now);
 start(d, now);
}

static void jog(struct deck *d, int32_t delta, uint32_t now) {
 if (d->loop.edit && d->loop.active) {
  int64_t value = (int64_t)(d->loop.edit == 1 ? d->loop.in : d->loop.out);
  value += (int64_t)delta * d->r->rate / 150;
  int64_t low = d->loop.edit == 1 ? 0 : (int64_t)d->loop.in + 1;
  int64_t high = d->loop.edit == 1 ? (int64_t)d->loop.out - 1 : (int64_t)d->r->frames;
  if (d->loop.edit == 1) {
   int64_t limit = (int64_t)d->r->rate * 30 / 75;
   if (low < (int64_t)d->loop.anchor - limit) low = (int64_t)d->loop.anchor - limit;
   if (high > (int64_t)d->loop.anchor + limit) high = (int64_t)d->loop.anchor + limit;
  }
  if (value < low) value = low;
  if (value > high) value = high;
  uint64_t heard = heard_now(d, now);
  if (d->loop.edit == 1) d->loop.in = (uint64_t)value;
  else d->loop.out = (uint64_t)value;
  d->loop.edit_at = now;
  jump(d, loop_frame(d, heard), now);
  return;
 }
 if(d->state==XZ_XP_SCRATCHING){
  if(!delta)return;
  if(d->scratch.rate*(double)delta<0){
   jump(d,heard_now(d,now),now);d->scratch.target=d->pos;
   d->scratch.at=0;d->scratch.ticks=0;
  }
  if(d->scratch.at!=now){
   uint32_t elapsed=now-d->scratch.at;
   d->scratch.interval=d->scratch.at&&elapsed<=100?elapsed:16;
   if(!d->scratch.interval)d->scratch.interval=1;
   d->scratch.at=now;d->scratch.ticks=0;
  }
  d->scratch.ticks+=delta;
  double move=(double)delta*d->r->rate*XZ_XP_SEARCH_MS_PER_REV/(1000.0*XZ_XP_JOG_TICKS_PER_REV);
  double target=(double)d->scratch.target/(double)ONE+move;
  if(target<0)target=0;
  if(target>(double)d->r->frames)target=(double)d->r->frames;
  d->scratch.target=(uint64_t)(target*(double)ONE);
  double rate=(double)d->scratch.ticks*XZ_XP_SEARCH_MS_PER_REV/
              ((double)XZ_XP_JOG_TICKS_PER_REV*d->scratch.interval);
  d->scratch.rate=rate>8?8:rate< -8?-8:rate;
  return;
 }
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
 if (tempo > d->tempo_range * 100 || tempo < -d->tempo_range * 100) { xz_xp_notice(p, "SYNC OUT OF RANGE"); return; }
 d->tempo_reset = 0;
 d->tempo_position = (int32_t)(tempo * XZ_EXTRA_TEMPO_SPAN / (d->tempo_range * 100));
 d->tempo_x100 = (int32_t)tempo;
 d->tempo_revision++;
}
static void set_tempo(struct deck *d) {
 d->tempo_x100 = d->tempo_reset ? 0 : (int32_t)((int64_t)d->tempo_position * d->tempo_range * 100 / XZ_EXTRA_TEMPO_SPAN);
}
static void loop_button(struct xz_xp_players *p, struct deck *d, const struct xz_extra_deck_event *e, uint32_t now) {
 if (!e->pressed) {
  if(e->kind==XZ_EXTRA_IN_LOOP_IN){d->loop.held=0;d->loop.in_down=0;}
  return;
 }
 if(e->kind==XZ_EXTRA_IN_LOOP_IN&&!e->held){
  d->loop.in_down=1;d->loop.in_at=now;d->loop.in_shift=e->shift;
 }
 uint64_t at = heard_now(d, now);
 if (e->kind == XZ_EXTRA_IN_LOOP_IN && e->held) {
  if(d->loop.held||!d->loop.in_down||d->loop.in_shift||now-d->loop.in_at<1000u||
     !streaming(d->state)||d->state==XZ_XP_SCRATCHING)return;
  d->loop.held = 1;
  uint32_t bpm = d->track.track_bpm_x100 > 0 ? (uint32_t)d->track.track_bpm_x100 : 12000u;
  uint64_t length = (uint64_t)d->r->rate * 6000u * 4u / bpm;
  d->loop.in = d->loop.has_in ? d->loop.in : at;
  d->loop.out = length > d->r->frames - d->loop.in ? d->r->frames : d->loop.in + length;
  d->loop.valid = d->loop.out > d->loop.in;
  d->loop.active = d->loop.valid; d->loop.edit = 0; d->loop.has_in = 1;
  if (d->loop.active) { jump(d, d->loop.in, now); start(d, now); xz_xp_notice(p,"4 BEAT LOOP"); }
  return;
 }
 if (e->kind == XZ_EXTRA_IN_LOOP_IN) {
  if (d->loop.active && e->shift) { jump(d, d->loop.in, now); start(d, now); return; }
  if (d->loop.active) {
   d->loop.edit = d->loop.edit == 1 ? 0 : 1;
   d->loop.anchor = d->loop.in; d->loop.edit_at = now;
   xz_xp_notice(p,d->loop.edit?"LOOP IN ADJUST":"LOOP ADJUST DONE");
  } else {
   d->loop.in = at; d->loop.has_in = 1; d->loop.valid = 0; d->cue = at;
   xz_xp_notice(p,"LOOP IN SET");
  }
 } else if (e->kind == XZ_EXTRA_IN_LOOP_OUT) {
  if (d->loop.active) {
   d->loop.edit = d->loop.edit == 2 ? 0 : 2; d->loop.edit_at = now;
   xz_xp_notice(p,d->loop.edit?"LOOP OUT ADJUST":"LOOP ADJUST DONE");return;
  }
  if (!d->loop.has_in) d->loop.in = d->cue;
  if (at <= d->loop.in) { xz_xp_notice(p, "LOOP OUT MUST FOLLOW LOOP IN"); return; }
  d->loop.out = at; d->loop.valid = d->loop.active = d->loop.has_in = 1; d->loop.edit = 0;
  jump(d, d->loop.in, now); start(d, now);
  xz_xp_notice(p,"LOOP ON");
 } else if (e->kind == XZ_EXTRA_IN_RELOOP) {
  d->loop.edit = 0;
  if (d->loop.active) {
   d->loop.active = 0; jump(d, at, now); xz_xp_notice(p,"LOOP EXIT");
  } else if (d->loop.valid) {
   d->loop.active = 1; jump(d, d->loop.in, now); start(d, now); xz_xp_notice(p,"RELOOP");
  } else xz_xp_notice(p, "SET A LOOP FIRST");
 } else if (d->loop.active) {
  uint64_t length = d->loop.out - d->loop.in;
  if (e->kind == XZ_EXTRA_IN_LOOP_HALF) length = length > 1 ? length / 2 : 1;
  else if (length <= (d->r->frames - d->loop.in) / 2) length *= 2;
  else length = d->r->frames - d->loop.in;
  d->loop.out = d->loop.in + length;
  d->loop.edit = 0; jump(d, loop_frame(d, at), now);
  xz_xp_notice(p,e->kind==XZ_EXTRA_IN_LOOP_HALF?"LOOP HALF":"LOOP DOUBLE");
 } else xz_xp_notice(p, "SET A LOOP FIRST");
}

static void command(struct xz_xp_players *p, struct deck *d, const struct xz_extra_deck_event *e, uint32_t now) {
 if (e->kind == XZ_EXTRA_IN_LOAD) { load(p, d, e, now, 0); return; }
 if(e->kind==XZ_EXTRA_IN_TRACK_NEXT){
  if(!e->pressed)return;
  if(!d->order){xz_xp_notice(p,"NO SAVED TRACK ORDER / NEXT UNAVAILABLE");return;}
  if(!d->order_epoch||d->order_epoch!=p->env.sink.link(p->env.sink.ctx)){xz_xp_notice(p,"ROUTE CHANGED / NEXT REFUSED");return;}
  if(d->retired_count>=XZ_XP_RETIRE_SLOTS){xz_xp_notice(p,"PLAYER BUSY / NEXT REFUSED");return;}
  uint32_t row=d->order_index+1;
  while(row<d->order->count&&!d->order->id[row])row++;
  if(row>=d->order->count){xz_xp_notice(p,"END OF TRACK LIST");return;}
  struct xz_extra_deck_event next={.kind=XZ_EXTRA_IN_LOAD,.deck=d->index};
  next.load.source=d->order->source;next.load.media_generation=d->order->media_generation;
  next.load.route_epoch=d->order_epoch;next.load.track_id=d->order->id[row];next.load.order=d->order;next.load.order_index=row;
  int playing=d->state==XZ_XP_PLAYING||d->state==XZ_XP_SAMPLING||(d->state==XZ_XP_SCRATCHING&&streaming(d->scratch.resume));
  if(load(p,d,&next,now,1)){jump(d,0,now);if(playing)start(d,now);}
  return;
 }
 if (e->kind == XZ_EXTRA_IN_TEMPO) {
  int32_t at = e->tempo_position;
  if (at > XZ_EXTRA_TEMPO_SPAN) at = XZ_EXTRA_TEMPO_SPAN;
  if (at < -XZ_EXTRA_TEMPO_SPAN) at = -XZ_EXTRA_TEMPO_SPAN;
  d->tempo_position = at; set_tempo(d);
  return;
 }
 if (e->kind == XZ_EXTRA_IN_TEMPO_RANGE) {
  if (e->pressed) {
   d->tempo_range = d->tempo_range == 6 ? 10 : d->tempo_range == 10 ? 16 : d->tempo_range == 16 ? 100 : 6;
   set_tempo(d);
  }
  return;
 }
 if (e->kind == XZ_EXTRA_IN_TEMPO_RESET) {
  if (e->pressed) {
   d->tempo_reset = !d->tempo_reset; set_tempo(d);
   xz_xp_notice(p,d->tempo_reset?"TEMPO RESET ON":"TEMPO RESET OFF");
  }
  return;
 }
 if (e->kind == XZ_EXTRA_IN_VINYL) {
  if(e->pressed){scratch_release(d,now);d->vinyl=!d->vinyl;}
  return;
 }
 if (e->kind == XZ_EXTRA_IN_MASTER_TEMPO) {
  if(e->pressed){
   if(loaded(d->state))jump(d,heard_now(d,now),now);
   d->master_tempo=!d->master_tempo;
   xz_xp_notice(p,d->master_tempo?"MASTER TEMPO ON":"MASTER TEMPO OFF");
  }
  return;
 }
 if (!loaded(d->state)) return;
 switch (e->kind) {
 case XZ_EXTRA_IN_PLAY:
  if (!e->pressed) break;
  if(d->state==XZ_XP_SCRATCHING){
   d->scratch.resume=d->scratch.resume==XZ_XP_PLAYING?XZ_XP_CUED:XZ_XP_PLAYING;
   break;
  }
  if (d->state == XZ_XP_CUED) start(d, now);
  else if (d->state == XZ_XP_PLAYING) pause_at_heard(d, now);
  else if (d->state == XZ_XP_SAMPLING) d->state = XZ_XP_PLAYING;
  break;
 case XZ_EXTRA_IN_CUE: cue_button(d, e->pressed, now); break;
 case XZ_EXTRA_IN_HOT_CUE: hot_cue(d, e, now); break;
 case XZ_EXTRA_IN_JOG_TURN: jog(d, e->jog.delta, now); break;
 case XZ_EXTRA_IN_JOG_TOUCH: scratch_touch(d,e->pressed,now); break;
 case XZ_EXTRA_IN_SYNC: if (e->pressed) sync_tempo(p, d); break;
 case XZ_EXTRA_IN_LOOP_IN: case XZ_EXTRA_IN_LOOP_OUT: case XZ_EXTRA_IN_RELOOP:
 case XZ_EXTRA_IN_LOOP_HALF: case XZ_EXTRA_IN_LOOP_DOUBLE: loop_button(p, d, e, now); break;
 case XZ_EXTRA_IN_SEEK: {
  uint64_t to = to_frames(d, e->seek_ms);
  if (d->loop.active && (to < d->loop.in || to >= d->loop.out)) d->loop.active = 0;
  jump(d, to, now);
  if (!streaming(d->state)) d->state = to >= d->r->frames ? XZ_XP_ENDED : XZ_XP_CUED;
  break;
 }
 default: break;
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

/* Source tags are a USB completion-boundary proxy, not an exact DAC-time observation. */
static void completed_timeline(struct deck *d,const xz_usb_deck_position *q,uint64_t wire_completed,uint32_t now){
 d->completed=q->completed_frames;
 if(!q->source_completed_output_frames||q->source_completed_output_frames==d->timeline_output_frames)return;
 uint64_t frames=q->source_completed_output_frames-d->timeline_output_frames;
 int valid=q->source_completed_output_frames>d->timeline_output_frames&&
  q->source_completed_output_frames<=q->completed_frames&&q->source_after_completed<=d->r->frames&&d->r->rate;
 double distance=(double)q->source_after_completed-(double)d->heard;
 double max_rate=d->state==XZ_XP_SCRATCHING?8.1:3.1;
 if(valid&&d->state!=XZ_XP_SCRATCHING&&d->loop.active){
  double length=(double)(d->loop.out-d->loop.in);
  /* More than one possible winding cannot be recovered from an opaque end tag. */
  if(!length||(double)frames*d->r->rate/XZ_USB_RATE*max_rate>=length)valid=0;
  else if(distance<0){distance+=length;d->timeline_revision++;}
 }
 double rate=valid?distance*XZ_USB_RATE/((double)frames*d->r->rate):0;
 if(!isfinite(rate)||fabs(rate)>max_rate||(d->state!=XZ_XP_SCRATCHING&&rate<0))valid=0;
 d->heard_rate=valid?rate:0;d->timeline_valid=valid;
 if(q->source_after_completed<=d->r->frames){d->heard=q->source_after_completed;d->heard_at=now;d->timeline_anchor_valid=1;}
 d->timeline_output_frames=q->source_completed_output_frames;
 d->timeline_sink_frame=q->source_completed_wire_frame<=wire_completed?q->source_completed_wire_frame:0;
}
static void stream(struct xz_xp_players *p, struct deck *d, uint32_t now) {
 const struct xz_xp_sink *s = &p->env.sink;
 xz_usb_position position;
 uint64_t staged = 0;
 int applied = 0;
 if (s->position(s->ctx, d->epoch, &position) == XZ_USB_OK) {
  const xz_usb_deck_position *q = &position.deck[d->index];
  applied = q->applied_generation == d->generation;
  if (applied) {
   staged = q->staged_frames;
   if ((q->source_valid & 2) && q->completed_frames != d->completed) {
    completed_timeline(d,q,position.wire_completed_frames,now);
   }
   if (d->state!=XZ_XP_SCRATCHING && !d->loop.active && !d->pending && d->pos >> 32 >= d->r->frames && q->completed_frames >= d->submitted) {
    d->state = XZ_XP_ENDED; d->pos = d->r->frames << 32; d->heard = d->r->frames;
    return;
   }
  }
 }
 for (int blocks = 0; blocks < 16; ++blocks) {
  if (!d->pending) {
   if ((d->state!=XZ_XP_SCRATCHING&&d->pos >> 32 >= d->r->frames) ||
       d->submitted - (applied ? staged : 0) >= XZ_XP_AHEAD_FRAMES) return;
   if (!render(d, speed(d, now))) {
    pause_at_heard(d, now); xz_xp_notice(p, "TRACK CANNOT BE READ");
    return;
   }
   if (!d->pending) return;
  }
  xz_usb_result r = s->submit(s->ctx, d->epoch, d->index, d->generation, d->pcm, d->pending, d->pending_after);
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
 if (d->loop.edit && now - d->loop.edit_at >= 10000u) d->loop.edit = 0;
 collect(d, now);
 uint32_t link = s->link(s->ctx);
 if (link != d->epoch) {
  /* A new route instance starts its queues at the initial generation; a lost one took the audio. */
  if (streaming(d->state)) pause_at_heard(d, now);
  d->epoch = link; d->generation = XZ_USB_INITIAL_GENERATION;
  d->timeline_revision++;d->timeline_valid=d->timeline_anchor_valid=0;d->heard_rate=0;
  d->need_flush = 0; d->pending = 0; d->submitted = d->completed = 0;
  d->loop.in_down=d->loop.held=0;
 }
 struct xz_extra_deck_event e;
 while (take(d, &e)) {command(p, d, &e, now);if(e.kind==XZ_EXTRA_IN_LOAD)xz_extra_order_release(e.load.order);}
 if(link&&d->loop.in_down&&!d->loop.held&&now-d->loop.in_at>=1000u){
  struct xz_extra_deck_event held={.kind=XZ_EXTRA_IN_LOOP_IN,.pressed=1,.held=1};
  if(loaded(d->state))loop_button(p,d,&held,now);
 }
 /* Prepare the first sample block at the new position before retiring the old
    generation. A cold read after flush would otherwise stage silence while the
    decoder catches up. Pausing still flushes immediately; queued old USB audio
    keeps the transport's existing bounded tail. */
 if (d->need_flush && streaming(d->state) && link && !d->pending &&
     (d->state == XZ_XP_SCRATCHING || d->pos >> 32 < d->r->frames)) {
  if (!render(d, speed(d, now))) {
   pause_at_heard(d, now); xz_xp_notice(p, "TRACK CANNOT BE READ");
  }
 }
 flush(p, d);
 if (streaming(d->state) && link && !d->need_flush) stream(p, d, now);
 publish(p, d, now);
}

int xz_xp_input(struct xz_xp_players *p, const struct xz_extra_deck_event *e) {
 if (!p || !e || e->deck < 0 || e->deck >= XZ_EXTRA_DECKS || (unsigned)e->kind >= XZ_EXTRA_INPUT_COUNT) return 0;
 if (!p->env.sink.link(p->env.sink.ctx)) return 0;
 if (e->kind == XZ_EXTRA_IN_LOAD && !p->env.resolve) { xz_xp_notice(p, "TRACK NOT FOUND"); return 0; }
 struct deck *d = &p->deck[e->deck];
 int state = atomic_load(&d->published_state);
 int button = e->kind != XZ_EXTRA_IN_TEMPO && e->kind != XZ_EXTRA_IN_JOG_TURN &&
              e->kind != XZ_EXTRA_IN_SEEK && e->kind != XZ_EXTRA_IN_LOAD;
 int release = button && !e->pressed;
 int setting = e->kind == XZ_EXTRA_IN_TEMPO || e->kind == XZ_EXTRA_IN_TEMPO_RANGE ||
               e->kind == XZ_EXTRA_IN_TEMPO_RESET || e->kind == XZ_EXTRA_IN_VINYL ||
               e->kind == XZ_EXTRA_IN_MASTER_TEMPO;
 if ((state == XZ_XP_EMPTY || state == XZ_XP_ERROR) && !release && e->kind != XZ_EXTRA_IN_LOAD && !setting)
  return 0;
 pthread_mutex_lock(&d->queue_lock);
 int taken = d->count < XZ_XP_QUEUE;
 if (taken) { if(e->kind==XZ_EXTRA_IN_LOAD)xz_extra_order_retain(e->load.order); d->queue[(d->head + d->count) % XZ_XP_QUEUE] = *e; d->count++; }
 pthread_mutex_unlock(&d->queue_lock);
 return taken;
}

void xz_xp_read(struct xz_xp_players *p, struct xz_extra_decks *out) {
 if (!p || !out) return;
 pthread_mutex_lock(&p->publish_lock);
 memcpy(out->deck, p->view, sizeof out->deck);
 memcpy(out->notice, p->notice, sizeof out->notice);
 out->notice_at_ms = p->notice_at; out->notice_seq = p->notice_seq;
 out->snapshot_revision=atomic_load_explicit(&p->revision,memory_order_acquire);
 pthread_mutex_unlock(&p->publish_lock);
}

void xz_xp_debug(struct xz_xp_players *p, int index, struct xz_xp_debug *out) {
 const struct deck *d = &p->deck[index];
 *out = (struct xz_xp_debug){d->state, d->pos >> 32, d->cue, d->generation, d->r->rate, d->retired_count,
                             speed(d, p->env.now_ms()), d->loop.in, d->loop.out, d->loop.active,
                             d->loop.edit, d->tempo_range, d->tempo_reset,d->scratch.target>>32};
}

struct xz_xp_players *xz_xp_create(const struct xz_xp_env *env) {
 if (!env || !env->sink.link || !env->sink.submit || !env->sink.flush || !env->sink.position || !env->sink.transition || !env->now_ms) return NULL;
 struct xz_xp_players *p = calloc(1, sizeof *p);
 if (!p) return NULL;
 p->env = *env;
 int mutexes = 0;
 if (pthread_mutex_init(&p->publish_lock, NULL)) { free(p); return NULL; }
 for (; mutexes < XZ_EXTRA_DECKS; ++mutexes) {
  struct deck *d = &p->deck[mutexes];
  d->index = mutexes; d->generation = XZ_USB_INITIAL_GENERATION;
  d->tempo_range = 10;
  xz_stretch_reset(&d->stretch);
  atomic_init(&d->published_state, XZ_XP_EMPTY);
  if (!(d->r = calloc(1, sizeof *d->r)) || pthread_mutex_init(&d->queue_lock, NULL)) { free(d->r); break; }
 }
 if (mutexes < XZ_EXTRA_DECKS) {
  while (mutexes--) { pthread_mutex_destroy(&p->deck[mutexes].queue_lock); free(p->deck[mutexes].r); }
  pthread_mutex_destroy(&p->publish_lock); free(p); return NULL;
 }
 atomic_init(&p->quit, 0);
 atomic_init(&p->revision,0);
 return p;
}

static void pause_ms(unsigned ms) { struct timespec t = {0, (long)ms * 1000000L}; nanosleep(&t, NULL); }
static void *worker(void *arg) {
 const struct worker *w = arg;
 if (w->p->env.worker_enter) w->p->env.worker_enter(w->index);
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
  xz_extra_order_release(d->order);
  for(unsigned q=0;q<d->count;q++){struct xz_extra_deck_event *e=&d->queue[(d->head+q)%XZ_XP_QUEUE];if(e->kind==XZ_EXTRA_IN_LOAD)xz_extra_order_release(e->load.order);}
  xz_stem_media_close(d->r->media);
  free_analysis(d->analysis);
  for (unsigned k = 0; k < d->retired_count; ++k) free_analysis(d->retired[k].a);
  pthread_mutex_destroy(&d->queue_lock);
  free(d->r);
 }
 pthread_mutex_destroy(&p->publish_lock);
 free(p);
}
