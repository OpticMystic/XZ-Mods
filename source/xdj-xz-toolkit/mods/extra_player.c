// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include "diary/diary.h"
#pragma weak xz_diary_busy_begin
#pragma weak xz_diary_busy_end
#pragma weak xz_diary_audio
#pragma weak xz_diary_notice
#pragma weak xz_diary_notice_code
#include "extra_player.h"
#include "anlz_extra.h"
#include "shell_analysis.h"
#include "audio/stem_media.h"
#include "audio/stretch.h"
#include "key/keyshift.h"
#include "audio/render_load.h"
#ifdef _WIN32
#include <windows.h>
#else
#include "clock_compat.h"
#endif
#include "sync_native.h"
#include <pthread.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef XZ_XP_SERVICE_PROOF
#include "xp_service_proof.h"
#endif

#define CACHE 4096u
#define PWV7_PER_SECOND 150u
#define EMERGENCY_FRAMES (8u * XZ_USB_RATE)
#define ONE ((uint64_t)1 << 32)
/* A released TOP may finish accepted motion, never indefinitely chase a blocked sink. */
#define SCRATCH_RELEASE_FRAMES 2048u
#define SCRATCH_RELEASE_MS 64u
/* Two full ordinary FIFO drains can wait behind a blocked sink. Beyond this,
   cancel at completed position and require a new contact, never rewrite a turn. */
#define SCRATCH_SEGMENTS (2u * XZ_XP_QUEUE)
/* Process lifetime, independent of any player allocation. One active instance
   owns each deck; constructing another invalidates its predecessor's display. */
_Static_assert(ATOMIC_INT_LOCK_FREE==2&&sizeof(uint32_t)==sizeof(unsigned),"Jog epoch getter must be lock-free");
static _Atomic uint32_t jog_content_epoch[XZ_EXTRA_DECKS]={1,1};
uint32_t xz_xp_jog_content_epoch(int deck){
 return (unsigned)deck<XZ_EXTRA_DECKS?atomic_load_explicit(&jog_content_epoch[deck],memory_order_acquire):0;
}
static uint32_t advance_jog_content_epoch(int deck){
 uint32_t before=xz_xp_jog_content_epoch(deck);
 while(before){
  uint32_t after=before==UINT32_MAX?0:before+1u;
  if(atomic_compare_exchange_weak_explicit(&jog_content_epoch[deck],&before,after,memory_order_acq_rel,memory_order_acquire))return after;
 }
 return 0; /* Exhaustion is permanent, including across player restarts. */
}
struct scratch_segment { uint64_t begin,end; double rate,frames; int direction; };

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
#ifdef XZ_XP_SERVICE_PROOF
 struct xp_sp_private *sp; uint32_t (*sp_clock)(void);
#endif
 struct xz_stem_media *media;
 uint32_t rate;
 uint64_t frames, begin;
 int cached;
 float pcm[(CACHE + 1) * 2];
};
/* Immutable once published. Readers pin through a held snapshot; time alone is
   not a lifetime guarantee. Collection stays on the player/lifecycle owner. */
struct analysis { struct xz_extra_column *columns; uint32_t count, span_ms, normalization; struct xz_extra_beat *grid; uint32_t grid_count; atomic_uint readers; };
struct hot { int set; uint64_t at; uint32_t rgb; uint64_t end; int loop; }; /* loop: at..end */
struct memory_point { uint64_t at, end; int loop; uint32_t rgb; int32_t saved_ms,saved_end_ms; };
/* Accepted output provenance. Worker-owned, bounded, no render-time allocation. */
#define OUTPUT_SPANS 64u
struct key_output { uint32_t revision; int cents; double pitch_rate; enum xz_key_audio_state state; };
/* tempo_step: the part of step set by the tempo (fader or SYNC rate), without a jog bend or the SYNC trim. */
struct output_span { uint64_t begin,end,source,step,loop_in,loop_out,tempo_step; struct key_output key; };
struct output_history {
 struct output_span span[OUTPUT_SPANS],pending;
 unsigned head,count;
 double rate,beat_age;
 uint64_t completed,completed_source;
 double crossing;
 int32_t beat_ms;
 int32_t crossing_ms;
 int rate_valid,beat_valid,crossing_valid,key_valid;
 struct key_output key;
};
static void output_reset(struct output_history *h){
 h->head=h->count=0;h->completed=0;h->rate_valid=h->beat_valid=h->crossing_valid=h->key_valid=0;
}

static int output_source(const struct output_span *s,uint64_t frame,uint64_t *source){
 if(frame<s->begin||frame>s->end||!s->step)return 0;
 uint64_t n=frame-s->begin;
 if(n>(UINT64_MAX-s->source)/s->step)return 0;
 uint64_t at=s->source+n*s->step;
 if(s->loop_out>s->loop_in&&at>=s->loop_out)
  at=s->loop_in+(at-s->loop_in)%(s->loop_out-s->loop_in);
 *source=at;return 1;
}
static void output_accept(struct output_history *h,uint64_t begin,unsigned frames){
 if(!frames)return;
 struct output_span s=h->pending;s.begin=begin;s.end=begin+frames;
 if(h->count){
  struct output_span *last=&h->span[(h->head+h->count-1)%OUTPUT_SPANS];uint64_t at;
  if(last->end==begin&&last->step==s.step&&last->loop_in==s.loop_in&&last->loop_out==s.loop_out&&
     last->key.revision==s.key.revision&&last->key.cents==s.key.cents&&
     last->key.pitch_rate==s.key.pitch_rate&&last->key.state==s.key.state&&last->tempo_step==s.tempo_step&&
     output_source(last,begin,&at)&&at==s.source&&
     s.end-last->begin<=(UINT64_MAX-last->source)/s.step){last->end=s.end;return;}
 }
 if(h->count==OUTPUT_SPANS){h->head=(h->head+1)%OUTPUT_SPANS;h->count--;}
 h->span[(h->head+h->count++)%OUTPUT_SPANS]=s;
}
/* Invert the actual Q32 motion in a newly completed portion of one span. */
static int output_crossing(const struct output_span *s,uint64_t begin,uint64_t end,double beat,double *crossing){
  if(!s->step||begin<s->begin||end>s->end||begin>end)return 0;
  double step=(double)s->step/(double)ONE;
  double first=(double)s->source/(double)ONE;
  double last=first+(double)(end-s->begin)*step;
  double target=beat;
  if(s->loop_out>s->loop_in&&last>=(double)s->loop_out/(double)ONE){
   double lo=(double)s->loop_in/(double)ONE,hi=(double)s->loop_out/(double)ONE,length=hi-lo;
   if(beat<lo||beat>=hi)return 0;
   target+=floor((last-beat)/length)*length;
  }
  if(target>=first&&target<=last){
   *crossing=(double)s->begin+(target-first)/step;
   return isfinite(*crossing)&&*crossing>=(double)begin&&*crossing<=(double)end;
  }
 return 0;
}

#define SYNC_SEGMENTS 64u
struct sync_segment { uint64_t begin,end; double phase,beat_step,nominal_bps; };
struct sync_clock {
 int available,playing,loop_aligned,manual,loaded;
 int bending; /* manual is a side nudge (jog bend), not a platter-top scratch */
 uint32_t route,load,generation,at_ms;
 uint64_t wire_frame,underrun;
 double phase,beats_per_second,nominal_beats_per_second,period_frames,tempo_bps;
 uint64_t output_frame;
 unsigned segment_count;
 struct sync_segment segment[SYNC_SEGMENTS];
};

/* Only accepted stateful holds reserve an extra slot for their matching release.
   Ordinary traffic cannot consume these slots. All entries still use one FIFO. */
#define RELEASE_SLOTS (8u + XZ_EXTRA_HOT_CUES) /* CUE, TOUCH, LOOP_IN, REV, pads, SEARCH, 4/8BEAT, MT. */
#define MODE_SLOTS 3u
#define QUEUE_SLOTS (XZ_XP_QUEUE + RELEASE_SLOTS + MODE_SLOTS)
#define CONTROL_BUDGET (XZ_XP_QUEUE + 3u) /* Preserve the existing per-pass bound. */
struct queued_control { struct xz_extra_deck_event event; uint32_t epoch, at, master_choice, operator_epoch; struct xz_performance_settings settings; int jump_beats; };
static unsigned release_bit(const struct xz_extra_deck_event *e) {
 if(e->kind==XZ_EXTRA_IN_HOT_CUE)
  return e->hot_cue>=0&&e->hot_cue<XZ_EXTRA_HOT_CUES?1u<<(4u+(unsigned)e->hot_cue):0u;
 return e->kind==XZ_EXTRA_IN_CUE?1u:e->kind==XZ_EXTRA_IN_JOG_TOUCH?2u:
        e->kind==XZ_EXTRA_IN_LOOP_IN?4u:e->kind==XZ_EXTRA_IN_REVERSE?8u:
        e->kind==XZ_EXTRA_IN_SEARCH&&e->hot_cue>=0&&e->hot_cue<2?1u<<(4u+XZ_EXTRA_HOT_CUES+(unsigned)e->hot_cue):
        e->kind==XZ_EXTRA_IN_LOOP_4_8?1u<<(6u+XZ_EXTRA_HOT_CUES):
        e->kind==XZ_EXTRA_IN_MASTER_TEMPO?1u<<(7u+XZ_EXTRA_HOT_CUES):0u;
}
static unsigned release_count(unsigned mask) {
 unsigned count=0;for(;mask;mask&=mask-1)count++;return count;
}
enum pad_hold { PAD_HOLD_NONE, PAD_HOLD_LOOP, PAD_HOLD_CUE };

struct deck {
#ifdef XZ_XP_SERVICE_PROOF
 struct xp_sp_private sp;
#endif
 int index;
 pthread_mutex_t queue_lock;
 struct queued_control queue[QUEUE_SLOTS];
 unsigned head, count, release_owed;
 unsigned mode_pending[MODE_SLOTS];
 int mode_target[MODE_SLOTS];
 uint32_t queue_epoch;
 struct xz_performance_settings settings; /* queue_lock; copied into every accepted command */
 struct xz_jump_deck_state jump_input; int input_pad_mode; /* queue_lock */
 struct xz_performance_settings command_settings; int command_jump_beats; /* worker only */
 atomic_int published_state;
 /* Worker only below. */
 enum xz_xp_state state;
 struct reader *r;
 uint64_t pos;                      /* render head, source frames x 2^32 */
 uint64_t cue;
 struct hot hot[XZ_EXTRA_HOT_CUES];
 struct hot saved_hot[XZ_EXTRA_HOT_CUES];unsigned called_hot;
 struct { int armed; uint64_t in,out; } active_loop;
 struct { int down,extend;uint32_t at;uint64_t in; } auto_button;
 struct { int active;unsigned head,count,start,length,read;uint64_t after;
  float pcm[EMERGENCY_FRAMES*2]; } emergency;
 int32_t tempo_x100;
 int32_t tempo_position;
 int tempo_range, tempo_reset, vinyl, master_tempo;
 uint32_t tempo_revision;
 struct xz_stretch stretch;
 struct xz_render_load render_load;
 struct xz_key_shift key_probe;
 uint32_t rendered_key_us;
 struct { struct xz_key_shift shift; int desired,engaged,mt_before,target,mode,initialized,ever_rendered,mt_owned,rendered_cents;
  uint32_t revision; struct xz_key_owner target_owner; } key;
 struct {
  enum xz_xp_state resume;
  uint64_t target,group_origin;
  double rate;
  uint32_t at, interval;
  int at_valid, releasing;
  int contact,coasting,platter_known,platter_moving;
  uint32_t released_at;
  uint64_t release_end;
  int64_t ticks;
  struct scratch_segment segment[SCRATCH_SEGMENTS];
  unsigned head,count;
  int overflow,group_open;
  unsigned group_count,pending_segments;
  double group_ticks,offset,carry,pending_offset,pending_carry;
 } scratch;
 struct {
  uint64_t in, out, anchor;
  int has_in, valid, active, edit, held, in_down, in_shift;
  uint32_t edit_at, in_at;
 } loop;
 float bend; uint32_t bend_at;
 /* Pad modes, DIRECTION REV, SLIP and the VINYL SPEED ADJUST ramps (manual pp.42-62). */
 int pad_mode, active_pad, slip_pad;
 int quantize;
 int remain, auto_cue;
 int search;                        /* SEARCH held during playback: +1 forward, -1 back */
 int loop_pad;                      /* hot cue pad whose loop is playing; its edges follow edits (p.53) */
 uint64_t auto_cue_at;              /* where the track cued at load: TRACK SEARCH back returns here */
 uint32_t track_prev_at; int track_prev_known;
 struct memory_point memory[XZ_SHELL_MEMORY_CUES]; int memory_count;
 int slip_pause;                    /* paused with SLIP + VINYL: the background keeps playing */
 uint32_t sync_snap_at;             /* when SYNC last snapped to the beat (0: never) */
 uint32_t native_idle_since;
 int native_idle_pending,native_idle_deck;
 uint32_t native_idle_identity,native_idle_revision;
 uint32_t native_observed_identity,native_observed_revision,native_observed_choice;
 int native_observed_deck,native_observed_known,native_playing_seen;
 enum pad_hold pad_hold; /* Current action owner; mode selection cannot change it. */
 struct { unsigned held; int latched,latest; uint64_t at; } gate;
 unsigned control_down; /* Physical pad/REV edges, including superseded and inert holds. */
 int reverse, slip, slip_reverse;
 struct { int active; uint64_t anchor; uint32_t at; double rate; uint32_t reverse_ms; } slip_bg;
 _Atomic int32_t vinyl_level; /* Physical side setting, independent of route/load ownership. */
 struct { int active, stopping; double from; uint32_t at, ms; } ramp;
 uint32_t epoch, generation; int need_flush;
 uint64_t submitted;                /* output frames accepted in this generation */
 uint64_t completed;
 uint32_t pending; uint64_t pending_after, block_start; float pcm[XZ_USB_BLOCK_FRAMES * 2];
 double pending_phase,pending_beat_step,pending_nominal_bps;
 struct output_history output;
 unsigned render_limit;int sync_capture;
 unsigned segment_count;
 struct sync_segment segment[SYNC_SEGMENTS];
 uint64_t heard; uint32_t heard_at; /* last completed source tag, or the position a jump set */
 double heard_rate;
 int timeline_valid, timeline_anchor_valid;
 uint32_t timeline_revision, load_revision;
 int jog_load_pending;
 uint64_t media_generation;
 uint64_t timeline_output_frames, timeline_sink_frame, timeline_wire_completed;
 struct xz_extra_sync sync;
 enum { SYNC_INITIAL_NONE, SYNC_INITIAL_NOW, SYNC_INITIAL_AFTER_COOLDOWN, SYNC_INITIAL_EXPLICIT } sync_initial;
 int sync_hold,sync_rate_active,sync_have_master,sync_lock_count;
 int sync_native_gap_hold, sync_stationary;
 uint32_t master_playing_choice;int master_playing_seen;
 uint32_t handoff_intent_epoch;
 struct xz_master_observation master_actual,tempo_authority;
 int tempo_native_deck;
 double tempo_native_bpm;
 uint32_t tempo_native_identity,tempo_native_revision;
 int sync_master_id;
 enum xz_extra_sync_reason sync_hold_reason;
 uint64_t sync_last_wire;
 uint64_t sync_slave_underrun,underrun;
 uint64_t sync_wire_submitted;
 int32_t sync_legacy_bpm;
 uint32_t sync_native_identity,sync_native_source_revision;
 int sync_native_deck;
 unsigned sync_grid_seen,sync_grid_den;
 struct { int have,ready; uint64_t at; double phase,bpm; } native_phase;
 uint64_t sync_native_captured_us;
 double sync_native_bar_phase,sync_native_bpm;
 int sync_native_discontinuous;
 uint32_t sync_choice_seen;
 double sync_rate,sync_trim;
 /* Offset model (stock S4, S7, S8): after a side nudge here or on the master the measured beat offset is
    kept (beats of this grid, within one quantize step) instead of trimmed away; sync.offset shows it. */
 double sync_offset; int sync_offset_adopt;
 double sync_trim_out; uint32_t sync_trim_out_at; /* S2: the trim at SYNC off, fading out */
 /* M4: this deck, as master, held on its platter top since (VINYL). M5: it loaded a track while master. */
 uint32_t master_hold_since; int master_hold_pending,master_load_handoff,native_load_handoff;
 double render_tempo_rate; /* tempo part of the rate of the block being rendered (S8 rule 2) */
 struct sync_clock sync_master_clock;
 struct xz_extra_track track;
 struct xz_extra_order *order;
 uint32_t order_index, order_epoch;
 struct analysis *analysis;
 struct { struct analysis *a; uint32_t at; } retired[XZ_XP_RETIRE_SLOTS];
 unsigned retired_count;
};

struct xz_xp_players {
#ifdef XZ_XP_SERVICE_PROOF
 int sp_enabled;uint32_t sp_id;
#endif
 struct xz_xp_env env;
 struct xz_ram_player_service ram;
 uint64_t (*source_generation)(unsigned);
 struct deck deck[XZ_EXTRA_DECKS];
 pthread_mutex_t publish_lock;
 struct xz_extra_deck view[XZ_EXTRA_DECKS];
 struct xz_master_observation tempo_view[XZ_EXTRA_DECKS];
 struct analysis *view_analysis[XZ_EXTRA_DECKS]; /* exact view owner, publish_lock */
 struct xz_extra_deck_event duplicate_source[XZ_EXTRA_DECKS]; /* guarded by publish_lock */
 char notice[64]; uint32_t notice_at, notice_seq;
 pthread_t thread[XZ_EXTRA_DECKS];
 struct worker { struct xz_xp_players *p; int index; } worker[XZ_EXTRA_DECKS];
 int threads;
 atomic_int quit;
 atomic_uint revision;
 atomic_int sync_master; /* requested software index, or -1 for native selection */
 atomic_int native_intent;
 atomic_uint explicit_intent;
 xz_master_observer master_observer;
 int (*key_owner_reader)(int,struct xz_key_owner *);
 atomic_uint key_active; /* Includes requests and the return-to-unity tail. */
 uint32_t key_reserved_us[2]; /* publish_lock; measured allowance until actual blocks arrive. */
 uint32_t master_admitted,admitted_operator,operator_floor,intent_operator,acknowledged_intent; /* publish_lock */
 atomic_uint sync_requests;
 atomic_uint sync_choice_epoch, sync_grid;
 int (*native_sync_clock)(struct xz_sync_native_snapshot *);
 xz_cue_read_fn cue_read;xz_cue_write_fn cue_write;
 struct sync_clock clock[XZ_EXTRA_DECKS]; /* copies only, protected by publish_lock */
 xz_usb_position sync_position;
 uint32_t sync_position_route,sync_position_at;
 /* When the transport's completed wire count was current: wire frame W played at about
    wire_origin_ms + W/44.1 ms. A worker sees a new position up to a pass (plus the millisecond
    clock) after the transport published it, so the earliest observation per wire frame is the
    estimate. publish_lock. */
 double wire_origin_ms; uint64_t wire_origin_seen; uint32_t wire_origin_route,wire_origin_at; int wire_origin_valid;
};
#ifdef XZ_XP_SERVICE_PROOF
#include "xp_service_proof.inc"
#else
#define SP_ONLY(x) do{}while(0)
#define SP_POSITION(p,d,q) ((p)->env.sink.position((p)->env.sink.ctx,(d)->epoch,q))
#define SP_READ(r,at,n,pcm) xz_stem_media_read((r)->media,at,n,pcm)
#endif
uint32_t xz_xp_revision(const struct xz_xp_players *p){return p?atomic_load_explicit(&p->revision,memory_order_acquire):0;}
void xz_xp_set_performance_settings(struct xz_xp_players *p,const struct xz_performance_settings *settings){
 if(!p||!settings)return;
 /* Publish to both admission queues as a single revision. No worker acquires UI state. */
 pthread_mutex_lock(&p->deck[0].queue_lock);pthread_mutex_lock(&p->deck[1].queue_lock);
 for(int i=0;i<2;i++)p->deck[i].settings=*settings;
 pthread_mutex_unlock(&p->deck[1].queue_lock);pthread_mutex_unlock(&p->deck[0].queue_lock);
}
int xz_xp_jump_snapshot(struct xz_xp_players *p,int deck,struct xz_jump_snapshot *out){
 if(!p||!out||(unsigned)deck>=2)return 0;
 struct deck *d=&p->deck[deck];pthread_mutex_lock(&d->queue_lock);
 *out=xz_jump_snapshot(&d->settings.jump,d->jump_input.page);
 pthread_mutex_unlock(&d->queue_lock);return 1;
}
void xz_xp_set_quantize_grid(struct xz_xp_players *p,unsigned denominator,int known){
 if(!p)return;
 unsigned value=known&&(denominator==1||denominator==2||denominator==4||denominator==8)?denominator:0;
 unsigned previous=atomic_load_explicit(&p->sync_grid,memory_order_acquire);
 for(;;){
  if(previous==UINT32_MAX||(previous&15u)==value)return;
  unsigned revision=previous>>4;
  unsigned next=revision==UINT32_MAX>>4?UINT32_MAX:((revision+1)<<4)|value;
  if(atomic_compare_exchange_weak_explicit(&p->sync_grid,&previous,next,memory_order_acq_rel,memory_order_acquire))return;
 }
}
void xz_xp_set_master_observer(struct xz_xp_players *p,xz_master_observer observer){
 if(p&&!p->threads)p->master_observer=observer;
}
void xz_xp_set_native_sync_clock(struct xz_xp_players *p,int (*read)(struct xz_sync_native_snapshot *)){
 if(p&&!p->threads)p->native_sync_clock=read;
}
void xz_xp_set_ram_service(struct xz_xp_players *p,const struct xz_ram_player_service *ram){
 if(p&&!p->threads)p->ram=ram?*ram:(struct xz_ram_player_service){0};
}
void xz_xp_set_source_generation(struct xz_xp_players *p,uint64_t (*generation)(unsigned)){
 if(p&&!p->threads)p->source_generation=generation;
}
static void retire_reader(struct xz_xp_players *p,struct deck *d){
 struct xz_stem_media *media=d->r->media;d->r->media=NULL;d->r->cached=0;
 if(!media)return;
 if(p->ram.retire)p->ram.retire(p->ram.context,(unsigned)d->index,media);
 else xz_stem_media_close(media);
}
void xz_xp_set_cue_store(struct xz_xp_players *p,xz_cue_read_fn read,xz_cue_write_fn write){
 if(p&&!p->threads){p->cue_read=read;p->cue_write=write;}
}
int xz_xp_needs_native_sync(const struct xz_xp_players *p){
 return p&&(p->master_observer||atomic_load(&p->sync_master)<0)&&atomic_load(&p->sync_requests)!=0;
}
static struct xz_master_intent master_intent_locked(const struct xz_xp_players *p){
 int selected=atomic_load(&p->sync_master);
 return (struct xz_master_intent){selected>=0?selected+2:atomic_load(&p->native_intent),
  atomic_load(&p->sync_choice_epoch),atomic_load(&p->explicit_intent),p->intent_operator};
}
static uint32_t observe_operator(struct xz_xp_players *p){
 struct xz_master_observation value={.deck=XZ_MASTER_UNKNOWN};
 return p->master_observer&&p->master_observer(&value)?value.operator_epoch:0;
}
static void consume_operator_locked(struct xz_xp_players *p,uint32_t operator_epoch){
 if(operator_epoch>p->operator_floor)p->operator_floor=operator_epoch;
 if(atomic_load(&p->sync_master)>=0&&p->intent_operator<p->operator_floor){
  atomic_store(&p->sync_master,-1);atomic_store(&p->native_intent,XZ_MASTER_NONE);
  atomic_store(&p->explicit_intent,0);p->intent_operator=p->operator_floor;p->acknowledged_intent=0;
  atomic_fetch_add_explicit(&p->revision,1,memory_order_release);
 }
}
static uint32_t reserve_master_locked(struct xz_xp_players *p,uint32_t operator_epoch){
 uint32_t next=p->master_admitted,applied=atomic_load(&p->sync_choice_epoch);
 if(applied>next)next=applied;
 if(next==UINT32_MAX)return 0;
 p->master_admitted=next+1;p->admitted_operator=operator_epoch;return next+1;
}
static int request_master(struct xz_xp_players *p,int logical,int explicit_choice,uint32_t admitted,uint32_t operator_epoch){
 pthread_mutex_lock(&p->publish_lock);
 consume_operator_locked(p,operator_epoch);
 struct xz_master_intent old=master_intent_locked(p);
 if(admitted){
  if(admitted!=p->master_admitted||!operator_epoch||operator_epoch<p->operator_floor){pthread_mutex_unlock(&p->publish_lock);return 0;}
 }else{
  int pending=p->master_admitted!=old.epoch&&p->admitted_operator&&p->admitted_operator>=p->operator_floor;
  if((!explicit_choice&&(pending||old.deck==logical))||
     (logical==XZ_MASTER_NONE&&old.explicit_choice&&old.deck>=0&&old.deck<2)||
     (logical>=2&&(!operator_epoch||operator_epoch<p->operator_floor||explicit_choice))){
   pthread_mutex_unlock(&p->publish_lock);return 0;
  }
  admitted=reserve_master_locked(p,operator_epoch);
  if(!admitted){pthread_mutex_unlock(&p->publish_lock);return 0;}
 }
 atomic_store(&p->sync_master,logical>=2?logical-2:-1);
 atomic_store(&p->native_intent,logical<2?logical:XZ_MASTER_NONE);
 atomic_store(&p->explicit_intent,!!explicit_choice);atomic_store(&p->sync_choice_epoch,admitted);
 p->intent_operator=operator_epoch;p->acknowledged_intent=0;
 atomic_fetch_add_explicit(&p->revision,1,memory_order_release);
 pthread_mutex_unlock(&p->publish_lock);return 1;
}
void xz_xp_native_master_selected(struct xz_xp_players *p,unsigned native_deck){
 if(p&&native_deck<2)(void)request_master(p,(int)native_deck,1,0,observe_operator(p));
}
static int observation_source_matches(const struct xz_extra_deck *views,
 const struct xz_master_intent *intent,const struct xz_master_observation *actual){
 if(actual->deck<2)return 1;
 if(actual->deck>3||!actual->request_epoch||!actual->lease_generation)return 0;
 const struct xz_extra_deck *v=&views[actual->deck-2];
 return v->loaded&&actual->route_epoch&&actual->load_revision&&actual->transport_generation&&
  v->timeline.route_epoch==actual->route_epoch&&v->timeline.load_revision==actual->load_revision&&
  v->timeline.transport_generation==actual->transport_generation&&
  (intent->deck!=actual->deck||intent->epoch==actual->request_epoch);
}
static void copy_master(struct xz_xp_players *p,struct xz_master_observation *out){
 *out=(struct xz_master_observation){.deck=XZ_MASTER_UNKNOWN};
 struct xz_master_observation value={.deck=XZ_MASTER_UNKNOWN};
 if(!p->master_observer||!p->master_observer(&value)||!value.revision||value.deck<XZ_MASTER_UNKNOWN||value.deck>3||
    (uint32_t)(p->env.now_ms()-value.observed_ms)>XZ_MASTER_MAX_AGE_MS)return;
 pthread_mutex_lock(&p->publish_lock);
 struct xz_master_intent intent=master_intent_locked(p);
 int valid=value.operator_epoch>=p->operator_floor&&observation_source_matches(p->view,&intent,&value);
 if(valid&&value.deck==intent.deck&&value.deck>=0)p->acknowledged_intent=intent.epoch;
 pthread_mutex_unlock(&p->publish_lock);
 if(valid)*out=value;
}
static int observed_selection(const struct xz_master_observation *actual){
 return actual->deck>=2?actual->deck-2:actual->deck>=0?-1:actual->deck==XZ_MASTER_EXTERNAL?-3:-2;
}
static int sync_selection(struct xz_xp_players *p,struct deck *d,uint32_t *choice){
 if(!p->master_observer){*choice=atomic_load(&p->sync_choice_epoch);return atomic_load(&p->sync_master);}
 copy_master(p,&d->master_actual);*choice=d->master_actual.revision;return observed_selection(&d->master_actual);
}
static int current_master(struct xz_xp_players *p,const struct deck *d){
 if(!p->master_observer)return atomic_load(&p->sync_master)==d->index;
 struct xz_master_observation actual;copy_master(p,&actual);return actual.deck==d->index+2;
}
static int authority_same(const struct xz_master_observation *a,const struct xz_master_observation *b){
 return a->deck==b->deck&&a->revision==b->revision&&a->operator_epoch==b->operator_epoch&&
  a->request_epoch==b->request_epoch&&a->lease_generation==b->lease_generation&&a->route_epoch==b->route_epoch&&
  a->load_revision==b->load_revision&&a->transport_generation==b->transport_generation;
}
/* Read observer caches before taking our lock. Under the lock recheck the own
   source tuple, so neither a load nor an intent change can reuse an old grant. */
static int authority_before(struct xz_xp_players *p,const struct deck *d){
 if(!p->master_observer)return 1;
 struct xz_master_observation actual;copy_master(p,&actual);
 return actual.deck>=0&&authority_same(&actual,&d->master_actual);
}
static int authority_locked(struct xz_xp_players *p,const struct deck *d,int selected,uint32_t choice){
 if(!d->epoch||p->env.sink.link(p->env.sink.ctx)!=d->epoch)return 0;
 if(atomic_load_explicit(&p->sync_grid,memory_order_acquire)!=d->sync_grid_seen)return 0;
 if(!p->master_observer)return atomic_load(&p->sync_master)==selected&&atomic_load(&p->sync_choice_epoch)==choice;
 struct xz_master_intent intent=master_intent_locked(p);
 return d->master_actual.operator_epoch>=p->operator_floor&&d->master_actual.revision==choice&&observed_selection(&d->master_actual)==selected&&
  observation_source_matches(p->view,&intent,&d->master_actual);
}
static int select_sync_master(struct xz_xp_players *p,int selected){
 if(p->master_observer)return request_master(p,selected<0?XZ_MASTER_NONE:selected+2,0,0,observe_operator(p));
 pthread_mutex_lock(&p->publish_lock);
 if(atomic_load(&p->sync_master)==selected){pthread_mutex_unlock(&p->publish_lock);return 0;}
 atomic_store(&p->sync_master,selected);atomic_fetch_add(&p->sync_choice_epoch,1);
 for(int i=0;i<XZ_EXTRA_DECKS;i++){
  struct xz_extra_deck *v=&p->view[i];v->master=selected==i;
  v->master_scope=v->master?XZ_EXTRA_MASTER_SCOPE_SOFTWARE:0;
  if(v->sync.requested){
   v->sync.tempo_matched=0;v->sync.master_paused=0;v->sync.source=selected==i?XZ_EXTRA_SYNC_SOURCE_LIVE:XZ_EXTRA_SYNC_SOURCE_CLOCK_LOST;
   v->sync.locked=0;v->sync.correction_ppm=0;v->sync.master_deck=selected<0?-1:selected+XZ_EXTRA_FIRST_DECK;
   v->sync.master_scope=selected<0?0:XZ_EXTRA_MASTER_SCOPE_SOFTWARE;
   v->sync.reason=selected<0?XZ_EXTRA_SYNC_WAIT_MASTER:selected==i?XZ_EXTRA_SYNC_MASTER_CLOCK:XZ_EXTRA_SYNC_DISCONTINUITY;
  }
 }
 atomic_fetch_add_explicit(&p->revision,1,memory_order_release);
 pthread_mutex_unlock(&p->publish_lock);return 1;
}
int xz_xp_use_native_master(struct xz_xp_players *p){
 if(!p)return 0;
 return select_sync_master(p,-1);
}

void xz_xp_set_key_owner_reader(struct xz_xp_players *p,int (*reader)(int,struct xz_key_owner *)){
 if(p&&!p->threads)p->key_owner_reader=reader;
}
static void key_history_reset(struct deck *d){
 xz_key_shift_init(&d->key.shift);d->key.initialized=1;d->key.mode=-1;
 (void)xz_key_shift_set_cents(&d->key.shift,d->key.desired);d->key.rendered_cents=d->key.desired;
}
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
/* Stored loops are bounded by their original endpoints, never by a seek clamp. */
static int stored_loop_end(const struct deck *d, int32_t ms, int32_t end_ms, uint64_t *end) {
 if (ms < 0 || end_ms <= ms) return 0;
 uint64_t first = (uint64_t)ms * d->r->rate / 1000u;
 uint64_t last = (uint64_t)end_ms * d->r->rate / 1000u;
 if (first >= d->r->frames || last <= first || last > d->r->frames) return 0;
 *end = last; return 1;
}
static double nominal(const struct deck *d) {
 /* Retain the exact applied SYNC rate until an accepted manual tempo command. */
 return d->sync_rate_active ? d->sync_rate : 1.0 + d->tempo_x100 / 10000.0;
}
static struct xz_extra_cue saved_hot(const struct deck *d,const struct hot *h){
 return h->set?(struct xz_extra_cue){1,to_ms(d,h->at),h->rgb,h->loop?to_ms(d,h->end):0}:(struct xz_extra_cue){0};
}
static void cue_snapshot(const struct deck *d,struct xz_saved_cues *out){
 memset(out,0,sizeof *out);
 for(unsigned i=0;i<8;i++)out->hot[i]=saved_hot(d,&d->saved_hot[i]);
 for(int i=0;i<d->memory_count&&i<XZ_CUE_MEMORY;i++){
  const struct memory_point *m=&d->memory[i];
  out->memory[out->memory_count++]=(struct xz_extra_cue){1,m->saved_ms,m->rgb,m->loop?m->saved_end_ms:0};
 }
}
static void cue_save(struct xz_xp_players *p,struct deck *d,const struct xz_cue_edit *edit){
 if(!p->cue_write)return;
 if(p->source_generation&&p->source_generation((unsigned)d->track.source)!=d->media_generation){
  xz_xp_notice(p,"CUES KEPT FOR THIS SESSION");return;
 }
 struct xz_saved_cues snapshot;cue_snapshot(d,&snapshot);
 if(!p->cue_write(d->index,&d->track,d->media_generation,to_ms(d,d->r->frames),&snapshot,edit))
  xz_xp_notice(p,"CUES KEPT FOR THIS SESSION");
}
/* S2: the phase trim in effect at SYNC off, fading out. */
static double trim_release(const struct deck *d,uint32_t now){
 uint32_t since=now-d->sync_trim_out_at;
 if(d->sync_trim_out==0||since>=XZ_XP_SYNC_TRIM_RELEASE_MS)return 0;
 return d->sync_trim_out*(double)(XZ_XP_SYNC_TRIM_RELEASE_MS-since)/(double)XZ_XP_SYNC_TRIM_RELEASE_MS;
}
static float bend_now(const struct deck *d, uint32_t now) {
 uint32_t since = now - d->bend_at;
 if (d->bend == 0.0f || since >= XZ_XP_BEND_DECAY_MS) return 0.0f;
 return d->bend * (float)(XZ_XP_BEND_DECAY_MS - since) / (float)XZ_XP_BEND_DECAY_MS;
}
/* Playback direction: DIRECTION REV, inverted while SEARCH back is held. */
static int reversed(const struct deck *d) { return d->reverse ^ (d->search < 0); }
static double speed(const struct deck *d, uint32_t now) {
 if(d->state==XZ_XP_SCRATCHING){
  if(!d->scratch.count)return 0;
  return d->scratch.count>d->scratch.group_count?d->scratch.segment[d->scratch.head].rate:d->scratch.rate;
 }
 if(d->search)return XZ_XP_SCAN_RATE;
 double s = (d->sync_rate_active?d->sync_rate:nominal(d)) + d->sync_trim + trim_release(d,now) + bend_now(d, now);
 return s < 0.0 ? 0.0 : s;
}
static uint64_t loop_frame(const struct deck *d, uint64_t frame) {
 if (d->loop.active && d->loop.out > d->loop.in && frame >= d->loop.out)
  return d->loop.in + (frame - d->loop.in) % (d->loop.out - d->loop.in);
 return frame > d->r->frames ? d->r->frames : frame;
}

void xz_xp_notice(struct xz_xp_players *p, const char *text) {
 if (!p || !text) return;
 if(xz_diary_notice&&xz_diary_notice_code)xz_diary_notice(XZ_DIARY_DECK,xz_diary_notice_code(text));
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
  if (SP_READ(r, (int64_t)(begin + 1), CACHE, r->pcm + 2)) return 0;
 } else if (SP_READ(r, (int64_t)begin, CACHE + 1, r->pcm)) return 0;
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
 if(!d->loop.active)return SP_READ(d->r,first,count,out);
 uint64_t length=d->loop.out-d->loop.in;
 int64_t relative=(first-(int64_t)d->loop.in)%(int64_t)length;
 if(relative<0)relative+=(int64_t)length;
 uint64_t at=d->loop.in+(uint64_t)relative;
 uint32_t done=(uint32_t)(d->loop.out-at);
 if(done>count)done=count;
 if(SP_READ(d->r,(int64_t)at,done,out))return -1;
 if(done==count)return 0;
 uint32_t period=count-done;
 if(period>length)period=(uint32_t)length;
 uint32_t start=done;
 if(SP_READ(d->r,(int64_t)d->loop.in,period,out+done*2))return -1;
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
static int grid_phase(const struct deck *,uint64_t,double *,double *);
static double grid_bpm(const struct deck *,uint64_t);
/* A millisecond timestamp cannot identify the last packet until the clock moves.
   Only this worker seals groups. TOP release seals immediately, including when
   touch, turns and release all arrive in one bounded FIFO drain. */
static void scratch_seal(struct deck *d){
 if(!d->scratch.group_open)return;
 double rate=d->scratch.group_ticks*XZ_XP_SEARCH_MS_PER_REV/
             ((double)XZ_XP_JOG_TICKS_PER_REV*d->scratch.interval);
 if(rate>8)rate=8;
 double step=(double)d->r->rate/XZ_USB_RATE*rate;
 unsigned first=d->scratch.count-d->scratch.group_count;
 for(unsigned i=first;i<d->scratch.count;i++){
  struct scratch_segment *s=&d->scratch.segment[(d->scratch.head+i)%SCRATCH_SEGMENTS];
  uint64_t distance=s->end>s->begin?s->end-s->begin:s->begin-s->end;
  s->rate=s->end>s->begin?rate:-rate;
  s->frames=(double)distance/(double)ONE/step;
 }
 d->scratch.group_count=0;d->scratch.group_open=0;
}
static void accepted_scratch(struct deck *d){
 if(d->state!=XZ_XP_SCRATCHING)return;
 d->scratch.head=(d->scratch.head+d->scratch.pending_segments)%SCRATCH_SEGMENTS;
 d->scratch.count-=d->scratch.pending_segments;
 d->scratch.offset=d->scratch.pending_offset;
 d->scratch.carry=d->scratch.pending_carry;
 d->scratch.pending_segments=0;
}
/* Walk source legs in output-sample time. Across a boundary the unused part of
   this sample traverses the next leg, including an opposite direction. The
   fixed ring bounds total crossings in one render to SCRATCH_SEGMENTS. */
static void scratch_advance(struct deck *d,unsigned available,unsigned *retired,
                            double *offset,double *time){
 while(*time>0&&*retired<available){
  const struct scratch_segment *s=&d->scratch.segment[(d->scratch.head+*retired)%SCRATCH_SEGMENTS];
  double left=s->frames-*offset;
  if(*time>=left){
   *time-=left;d->pos=s->end;(*retired)++;*offset=0;
  }else{
   *offset+=*time;*time=0;
   uint64_t distance=s->end>s->begin?s->end-s->begin:s->begin-s->end;
   uint64_t moved=(uint64_t)((double)distance*(*offset/s->frames));
   d->pos=s->end>s->begin?s->begin+moved:s->begin-moved;
  }
 }
}
static void accepted_segment(struct deck *d){
 output_accept(&d->output,d->submitted,d->pending);
 if(!d->emergency.active){
  if(d->state!=XZ_XP_PLAYING||reversed(d)||d->search||d->ramp.active){d->emergency.count=0;}
  else if(d->pending&&d->output.pending.key.revision==d->key.revision){
   unsigned first=EMERGENCY_FRAMES-d->emergency.head;if(first>d->pending)first=d->pending;
   memcpy(d->emergency.pcm+d->emergency.head*2,d->pcm,first*2*sizeof(float));
   memcpy(d->emergency.pcm,d->pcm+first*2,(d->pending-first)*2*sizeof(float));
   d->emergency.head=(d->emergency.head+d->pending)%EMERGENCY_FRAMES;
   d->emergency.count+=d->pending;if(d->emergency.count>EMERGENCY_FRAMES)d->emergency.count=EMERGENCY_FRAMES;
   d->emergency.after=d->pending_after;
  }
 }
 if(!d->pending||!isfinite(d->pending_phase)||d->state!=XZ_XP_PLAYING)return;
 if(d->segment_count){
  struct sync_segment *last=&d->segment[d->segment_count-1];
  if(last->end==d->submitted&&fabs(last->beat_step-d->pending_beat_step)<1e-12&&
     fabs(last->nominal_bps-d->pending_nominal_bps)<1e-12&&
     fabs(remainder(last->phase+(last->end-last->begin)*last->beat_step-d->pending_phase,1.))<1e-7){
   last->end+=d->pending;return;
  }
 }
 if(d->segment_count==SYNC_SEGMENTS){
  memmove(d->segment,d->segment+1,(SYNC_SEGMENTS-1)*sizeof *d->segment);d->segment_count--;
 }
 d->segment[d->segment_count++]=(struct sync_segment){d->submitted,d->submitted+d->pending,d->pending_phase,d->pending_beat_step,d->pending_nominal_bps};
}
static int render_source(struct deck *d, double rate) {
#ifdef XZ_XP_TEST_RENDER_HOOK
 extern void xz_xp_test_render_hook(void);
 xz_xp_test_render_hook();
#endif
 struct reader *r = d->r;
 memset(&d->output.pending,0,sizeof d->output.pending);
 if(d->emergency.active){
  for(unsigned i=0;i<XZ_USB_BLOCK_FRAMES;i++){
   unsigned at=(d->emergency.start+d->emergency.read)%EMERGENCY_FRAMES;
   d->pcm[i*2]=d->emergency.pcm[at*2];d->pcm[i*2+1]=d->emergency.pcm[at*2+1];
   d->emergency.read=(d->emergency.read+1)%d->emergency.length;
  }
  uint64_t frame=d->loop.in+(uint64_t)d->emergency.read*(d->loop.out-d->loop.in)/d->emergency.length;
  d->block_start=d->pos;d->pos=frame<<32;d->pending=XZ_USB_BLOCK_FRAMES;d->pending_after=frame;d->pending_phase=NAN;return 1;
 }
 uint64_t end = r->frames << 32;
 unsigned n = 0;
 unsigned limit=XZ_USB_BLOCK_FRAMES;
 if(d->render_limit&&d->render_limit<limit)limit=d->render_limit;
 d->render_limit=0;
 if(d->active_loop.armed&&d->state==XZ_XP_PLAYING&&!reversed(d)&&!d->loop.active){
  uint64_t frame=d->pos>>32;
  if(frame>=d->active_loop.in&&frame<d->active_loop.out){
   d->loop.in=d->active_loop.in;d->loop.out=d->active_loop.out;
   d->loop.has_in=d->loop.valid=d->loop.active=1;d->loop.edit=0;
   d->active_loop.armed=0;xz_stretch_reset(&d->stretch);
  }else if(frame<d->active_loop.in&&rate>0){
   double step=(double)r->rate/XZ_USB_RATE*rate;
   unsigned left=(unsigned)fmin((double)limit,ceil(((double)d->active_loop.in-(double)d->pos/ONE)/step));
   if(left&&left<limit)limit=left;
  }
 }
 if(d->state==XZ_XP_SCRATCHING&&d->scratch.releasing){
  uint64_t left=d->scratch.release_end>d->submitted?d->scratch.release_end-d->submitted:0;
  if(left<limit)limit=(unsigned)left;
 }
 double phase,period;
 d->pending_phase=NAN;
 if(d->sync_capture&&d->state==XZ_XP_PLAYING&&!d->reverse&&grid_phase(d,d->pos>>32,&phase,&period)&&rate>0){
  double step=(double)r->rate/XZ_USB_RATE*rate;
  double remaining=(1.-phase)*period-((double)(uint32_t)d->pos/(double)ONE);
  if(remaining>0&&remaining/step<limit)limit=(unsigned)ceil(remaining/step);
  if(!limit)limit=1;
  d->pending_phase=phase+((double)(uint32_t)d->pos/(double)ONE)/period;
  d->pending_beat_step=step/period;
  d->pending_nominal_bps=nominal(d)*grid_bpm(d,d->pos>>32)/6000.;
 }
 d->block_start = d->pos;
 if(d->state==XZ_XP_SCRATCHING){
  unsigned available=d->scratch.count-d->scratch.group_count,retired=0;
  double offset=d->scratch.offset,carry=d->scratch.carry;
  scratch_advance(d,available,&retired,&offset,&carry);
  while(n<limit&&retired<available){
   if(!sample_at(d,d->pos>=end?end-ONE:d->pos,d->pcm+n*2,0))return 0;
   carry=1.;scratch_advance(d,available,&retired,&offset,&carry);n++;
  }
  d->scratch.pending_segments=retired;
  d->scratch.pending_offset=offset;d->scratch.pending_carry=carry;
  d->pending=n;d->pending_after=d->pos>>32;
  /* Sub-sample motion can finish inside time already covered by the preceding
     accepted output sample. It emits no fabricated extra sample. */
  if(!n)accepted_scratch(d);
  return 1;
 }
 uint64_t step = (uint64_t)((double)r->rate / XZ_USB_RATE * rate * (double)ONE);
 if(d->state==XZ_XP_PLAYING&&!reversed(d)&&!d->search&&!d->ramp.active){
  d->output.pending.source=d->block_start;d->output.pending.step=step;
  /* S8: a follower tracks the tempo setting, not a side nudge or the SYNC trim on top of it. */
  double tempo=d->render_tempo_rate;
  d->output.pending.tempo_step=tempo<=0||tempo==rate?step:(uint64_t)((double)r->rate/XZ_USB_RATE*tempo*(double)ONE);
  if(d->loop.active){d->output.pending.loop_in=d->loop.in<<32;d->output.pending.loop_out=d->loop.out<<32;}
 }
 if (!step) { d->pending = 0; return 1; }
 if (reversed(d)) {
  /* DIRECTION REV (p.42): the same rate toward the track start; a loop wraps from IN back to OUT.
     Pitch is not preserved in reverse or while a vinyl ramp changes speed. */
  uint64_t lo = d->loop.in << 32, hi = d->loop.out << 32;
  int looping = d->loop.active && hi > lo;
  while (n < limit) {
   if (looping && d->pos < lo) d->pos = hi - (lo - d->pos) % (hi - lo);
   if (looping && d->pos >= hi) d->pos = lo + (d->pos - lo) % (hi - lo);
   if (!looping && !d->pos) break;
   if (!sample_at(d, d->pos >= end ? end - ONE : d->pos, d->pcm + n * 2, 0)) return 0;
   if (looping) d->pos = d->pos >= lo + step ? d->pos - step : hi - (lo + step - d->pos) % (hi - lo);
   else d->pos = d->pos > step ? d->pos - step : 0;
   ++n;
  }
  d->pending = n; d->pending_after = d->pos >> 32;
  return 1;
 }
 if(d->master_tempo&&!d->ramp.active&&!d->search){
  uint64_t after=d->pos;
  while(n<limit&&after<end){after=advance_position(d,after,step);n++;}
  if(n&&!xz_stretch_render(&d->stretch,(double)d->pos/(double)ONE,(double)step/(double)ONE,
                           r->rate,stretch_read,d,d->pcm,n))return 0;
  for(unsigned i=0;i<n*2;i++)d->pcm[i]*=XZ_XP_GAIN;
  d->pos=after;d->pending=n;d->pending_after=after>>32;return 1;
 }
 while (n < limit && d->pos < end) {
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

#ifndef XZ_XP_RENDER_CLOCK_US
#define XZ_XP_RENDER_CLOCK_US render_clock_us
static uint64_t render_clock_us(void) {
#ifdef _WIN32
 LARGE_INTEGER ticks,rate;
 if(!QueryPerformanceCounter(&ticks)||!QueryPerformanceFrequency(&rate))return 0;
 return (uint64_t)(ticks.QuadPart/(double)rate.QuadPart*1000000.);
#else
 struct timespec t;
 if(xz_clock_gettime_compat(CLOCK_MONOTONIC,&t))return 0;
 return (uint64_t)t.tv_sec*1000000u+(uint32_t)t.tv_nsec/1000u;
#endif
}

#endif

/* Every fresh block visits the shifter once. Pending retries retain its PCM and provenance. */
static int render_audio(struct deck *d,double rate){
 int mode=d->emergency.active?4:d->state==XZ_XP_SCRATCHING?3:
  reversed(d)||d->search||d->ramp.active?2:d->master_tempo?1:0;
 if(!d->key.initialized||d->key.mode!=mode){key_history_reset(d);d->key.mode=mode;}
 if(!render_source(d,rate))return 0;
 if(!d->pending)return 1;
 d->key.ever_rendered=1;
 struct key_output *k=&d->output.pending.key;
 k->revision=d->key.revision;k->state=mode==4?XZ_KEY_AUDIO_EMERGENCY:mode>=2?XZ_KEY_AUDIO_TRANSIENT:XZ_KEY_AUDIO_APPLIED;
 k->pitch_rate=mode==1?1:rate;
 if(mode==4)return 1; /* The emergency ring already contains processed PCM. */
 if(d->key.rendered_cents!=d->key.desired){
  (void)xz_key_shift_set_cents(&d->key.shift,d->key.desired);d->key.rendered_cents=d->key.desired;
 }
 uint64_t begin=XZ_XP_RENDER_CLOCK_US();
 if(xz_key_shift_process(&d->key.shift,d->pcm,d->pending))return 0;
 d->rendered_key_us=xz_render_us(begin,XZ_XP_RENDER_CLOCK_US());
 k->cents=(int)lround(1200.*log2(d->key.shift.ratio));
 return 1;
}

static int render(struct deck *d,double rate) {
 uint64_t begin=XZ_XP_RENDER_CLOCK_US();d->rendered_key_us=0;
 int result=render_audio(d,rate);
 uint64_t end=XZ_XP_RENDER_CLOCK_US();
 if(begin&&end>=begin) {
  uint32_t key=d->key.desired||fabsf(d->key.shift.ratio-1.f)>.0001f?d->rendered_key_us:0;
  xz_render_load_record(&d->render_load,xz_render_us(begin,end),key,
                       d->pending?d->pending:256u,(uint32_t)(end/1000u));
 }
 return result;
}

/* Exercise a wrap at the fastest supported ratio on private state. A twofold
 * reserve covers timing variation and the fine-search boundary case. No PCM,
 * transport, or requested pitch from the live deck is changed. */
static uint32_t key_probe_us(struct deck *d) {
 uint32_t peak=0;
 for(unsigned trial=0;trial<2;trial++) {
  d->key_probe=d->key.shift;
  d->key_probe.ratio=d->key_probe.target=2.f;d->key_probe.grain=512.f;d->key_probe.phase=0.f;
  float pcm[512];memcpy(pcm,d->pcm,sizeof pcm);
  uint64_t begin=XZ_XP_RENDER_CLOCK_US();
  (void)xz_key_shift_process(&d->key_probe,pcm,256);
  uint32_t cost=xz_render_us(begin,XZ_XP_RENDER_CLOCK_US());
  if(cost>peak)peak=cost;
 }
 return peak>UINT32_MAX/2u?UINT32_MAX:peak*2u;
}
static int key_admit(struct xz_xp_players *p,struct deck *d) {
 unsigned bit=1u<<d->index;
 if(atomic_load(&p->key_active)&bit)return 1;
 uint32_t probe=key_probe_us(d);
 pthread_mutex_lock(&p->publish_lock);
 struct xz_worker_load load[2]={p->view[0].worker_load,p->view[1].worker_load};
 load[d->index]=d->render_load.latest;
 unsigned active=atomic_load(&p->key_active);
 uint64_t projected=xz_render_projected(load,p->key_reserved_us,active,(unsigned)d->index,probe);
 int allowed=projected<=XZ_RENDER_SAFE_US;
 if(allowed){p->key_reserved_us[d->index]=probe;atomic_fetch_or(&p->key_active,bit);}
 pthread_mutex_unlock(&p->publish_lock);
 return allowed;
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
 if(!d->emergency.active)d->emergency.count=0;
 if (frame > d->r->frames) frame = d->r->frames;
 d->scratch.releasing=0;d->ramp.active=0;
 d->pos = frame << 32; d->need_flush = 1; d->pending = 0; d->submitted = d->completed = 0;
 d->segment_count=0;output_reset(&d->output);key_history_reset(d);
 d->heard = frame; d->heard_at = now;
 d->heard_rate=0;d->timeline_valid=d->timeline_anchor_valid=0;d->timeline_revision++;
 d->timeline_output_frames=d->timeline_sink_frame=d->timeline_wire_completed=0;
 if(d->state==XZ_XP_SCRATCHING){
  d->scratch.target=d->pos;d->scratch.rate=0;d->scratch.at_valid=0;d->scratch.ticks=0;
  d->scratch.head=d->scratch.count=0;d->scratch.pending_segments=d->scratch.overflow=0;
  d->scratch.group_open=0;d->scratch.group_count=0;
  d->scratch.offset=d->scratch.carry=0;
 }
 xz_stretch_reset(&d->stretch);
}
static void pause_at_heard(struct deck *d, uint32_t now) {
 int scratched=d->state==XZ_XP_SCRATCHING;
 jump(d, d->scratch.releasing?d->heard:heard_now(d,now), now);
 d->state = XZ_XP_CUED;
 if(scratched)d->scratch.contact=d->scratch.coasting=d->scratch.platter_known=d->scratch.platter_moving=0;
}
static void start(struct deck *d, uint32_t now) {
 d->heard = d->pos >> 32; d->heard_at = now;
 d->timeline_valid=0;
 d->state = XZ_XP_PLAYING;
}
static void scratch_release(struct deck *d,uint32_t now){
 if(d->state!=XZ_XP_SCRATCHING)return;
 enum xz_xp_state resume=d->scratch.resume;
 /* A release tail retires only verified completed motion, including on timeout. */
 uint64_t at=loop_frame(d,d->scratch.releasing?d->heard:heard_now(d,now));
 jump(d,at,now);
 d->state=resume;
 d->scratch.contact=d->scratch.coasting=d->scratch.platter_known=d->scratch.platter_moving=0;
}
/* Once a qualified stop (or a legacy unknown-speed release) begins draining,
   return after completion or the existing fixed drain deadline. */
static int scratch_tail_done(struct xz_xp_players *p,struct deck *d,uint32_t now){
 if(d->state!=XZ_XP_SCRATCHING||!d->scratch.releasing)return 0;
 int finished=!d->scratch.count&&!d->pending&&d->completed>=d->submitted;
 int capped=d->submitted>=d->scratch.release_end&&!d->pending&&d->completed>=d->submitted;
 if(!finished&&!capped&&(uint32_t)(now-d->scratch.released_at)<SCRATCH_RELEASE_MS)return 0;
 scratch_release(d,now);
 if(!finished)xz_xp_notice(p,"SCRATCH RELEASE TRUNCATED");
 return 1;
}
static void scratch_finish(struct deck *d,uint32_t now){
 if(d->state!=XZ_XP_SCRATCHING||d->scratch.releasing)return;
 d->scratch.coasting=0;scratch_seal(d);
 /* Already-rendered motion keeps the established release-at-heard behavior.
    Defer only when release would erase motion the worker has not submitted. */
 if(!d->scratch.count&&!d->pending){scratch_release(d,now);return;}
 d->scratch.releasing=1;d->scratch.released_at=now;
 d->scratch.release_end=d->completed+SCRATCH_RELEASE_FRAMES;
 if(d->scratch.release_end<d->submitted)d->scratch.release_end=d->submitted;
 if(d->pending>d->scratch.release_end-d->submitted){d->pos=d->block_start;d->pending=0;key_history_reset(d);}
}
static void scratch_touch(struct deck *d,int pressed,uint32_t now){
 if(!pressed)d->scratch.contact=0;
 if(!d->vinyl)return;
 if(!pressed){
  if(d->state!=XZ_XP_SCRATCHING||d->scratch.releasing)return;
  /* Hardware JOG packets carry a qualified finite speed. TOP-up transfers a
     known-moving gesture to coast ownership. Legacy/unknown-speed producers
     retain the old bounded release behavior instead of guessing motion. */
  if(d->scratch.platter_known&&d->scratch.platter_moving){d->scratch.coasting=1;return;}
  scratch_finish(d,now);return;
 }
 if(d->state==XZ_XP_SCRATCHING){
  if(d->scratch.contact)return;
  /* A new TOP contact starts a fresh gesture. Old coast/drain work cannot
     overwrite its generation or inherit its arrival grouping. */
  scratch_release(d,now);
 }
 enum xz_xp_state resume=streaming(d->state)?d->state:XZ_XP_CUED;
 jump(d,heard_now(d,now),now);
 memset(&d->scratch,0,sizeof d->scratch);
 d->scratch.resume=resume;d->scratch.target=d->pos;d->scratch.interval=16;d->scratch.contact=1;
 d->state=XZ_XP_SCRATCHING;
}

static void flush(struct xz_xp_players *p, struct deck *d) {
 const struct xz_xp_sink *s = &p->env.sink;
 if (!d->need_flush) return;
 uint32_t next;
 int first = streaming(d->state) && d->pending;
#ifdef XZ_XP_SERVICE_PROOF
 uint32_t sp_begin=p->env.now_ms();
#endif
 SP_ONLY(sp_capture(d,sp_begin));
 uint32_t available=s->link(s->ctx);
 xz_usb_result r = !available ? XZ_USB_STOPPED : first
  ? s->transition(s->ctx, d->epoch, d->index, d->generation, d->pcm, d->pending, d->pending_after, &next)
  : s->flush(s->ctx, d->epoch, d->index, &next);
 SP_ONLY(if(available)sp_service(&d->sp,first?XP_SP_TRANSITION:XP_SP_FLUSH,r,sp_begin,p->env.now_ms()));
 if (r == XZ_USB_OK) {
  d->generation = next; d->submitted = d->completed = 0; d->underrun=0;
  d->segment_count=0;output_reset(&d->output);
  if (first) { accepted_segment(d);accepted_scratch(d);d->submitted = d->pending; d->pending = 0; }
 }
 /* Busy/full retries keep the prepared target private. A newer command can replace it. */
 if (r != XZ_USB_BUSY && r != XZ_USB_AGAIN && r != XZ_USB_STALE) d->need_flush = 0;
 if (r == XZ_USB_STALE) {
  xz_usb_position position;
  if (SP_POSITION(p,d,&position) == XZ_USB_OK && position.requested_generation[d->index])
   d->generation = position.requested_generation[d->index];
  if(d->scratch.releasing){scratch_release(d,p->env.now_ms());xz_xp_notice(p,"SCRATCH RELEASE TRUNCATED");}
 }
}

/* Prepare the next generation before retiring the old one, including tail-to-play resume. */
static void prepare_flush(struct xz_xp_players *p,struct deck *d,uint32_t now,uint32_t link){
 if (d->need_flush && streaming(d->state) && link && !d->pending &&
     (d->state == XZ_XP_SCRATCHING || d->pos >> 32 < d->r->frames)) {
  d->render_tempo_rate=nominal(d);
  if (!render(d, speed(d, now))) {
   pause_at_heard(d, now); xz_xp_notice(p, "TRACK CANNOT BE READ");
  }
 }
 flush(p, d);
}

static void retire(struct deck *d, uint32_t now) {
 /* LOAD must unpublish this analysis under publish_lock before retiring it.
    Existing held readers retain it; no new reader can acquire it afterward. */
 if (d->analysis) {
  d->retired[d->retired_count].a = d->analysis; d->retired[d->retired_count].at = now; d->retired_count++;
 }
 d->analysis = NULL;
}
static void free_analysis(struct analysis *a) { if (a) { free(a->columns); free(a->grid); free(a); } }
static void collect(struct deck *d, uint32_t now) {
 unsigned kept = 0;
 for (unsigned i = 0; i < d->retired_count; ++i) {
  if (now - d->retired[i].at >= XZ_XP_RETIRE_MS &&
      !atomic_load_explicit(&d->retired[i].a->readers,memory_order_acquire)) free_analysis(d->retired[i].a);
  else d->retired[kept++] = d->retired[i];
 }
 d->retired_count = kept;
}

static double wrapped_beat(double phase){phase=fmod(phase,1.);return phase<0?phase+1:phase;}
static double quantized_delta(double phase,unsigned denominator){
 return (wrapped_beat(phase*denominator+.5)-.5)/denominator;
}
static int grid_phase(const struct deck *d,uint64_t frame,double *phase,double *period){
 const struct analysis *a=d->analysis;
 if(!a||a->grid_count<2||!d->r->rate)return 0;
 double ms=(double)frame*1000/d->r->rate;unsigned lo=0,hi=a->grid_count;
 while(lo<hi){unsigned mid=lo+(hi-lo)/2;if(a->grid[mid].ms<=ms)lo=mid+1;else hi=mid;}
 unsigned index=lo?lo-1:0;
 int32_t length=index+1<a->grid_count?a->grid[index+1].ms-a->grid[index].ms:a->grid[index].ms-a->grid[index-1].ms;
 if(length<100||length>3000||a->grid[index].beat_in_bar<1||a->grid[index].beat_in_bar>4)return 0;
 *phase=wrapped_beat((double)a->grid[index].beat_in_bar-1+(ms-a->grid[index].ms)/length);
 *period=(double)length*d->r->rate/1000;return 1;
}
/* PQTZ tempo is independent of its millisecond-quantized phase coordinates.
 * Retain local tempo changes, without commanding rate changes for 348/349 ms beats. */
static double grid_bpm(const struct deck *d,uint64_t frame){
 const struct analysis *a=d->analysis;
 if(a&&a->grid_count>=2&&d->r->rate){
  double ms=(double)frame*1000/d->r->rate;unsigned lo=0,hi=a->grid_count;
  while(lo<hi){unsigned mid=lo+(hi-lo)/2;if(a->grid[mid].ms<=ms)lo=mid+1;else hi=mid;}
  unsigned index=lo?lo-1:0;
  double phase,period;
  if(grid_phase(d,frame,&phase,&period)){
   if(a->grid[index].bpm_x100)return a->grid[index].bpm_x100;
   return 6000.*d->r->rate/period;
  }
 }
 return d->track.track_bpm_x100;
}
static int aligned_loop(const struct deck *d){
 if(!d->loop.active)return 1;
 double a,b,pa,pb;
 if(!grid_phase(d,d->loop.in,&a,&pa)||!grid_phase(d,d->loop.out,&b,&pb))return 0;
 double error=fabs(a-b);if(error>.5)error=1-error;
 return error*pa<=2.;
}
static struct sync_clock sync_clock(const struct deck *d,uint32_t now){
 struct sync_clock c={.route=d->epoch,.load=d->load_revision,.generation=d->generation,
  .at_ms=d->heard_at,.wire_frame=d->timeline_sink_frame,.playing=d->state==XZ_XP_PLAYING,
  .loop_aligned=aligned_loop(d),.manual=d->state==XZ_XP_SCRATCHING||bend_now(d,now)!=0,.underrun=d->underrun,.loaded=loaded(d->state),
  .bending=d->state!=XZ_XP_SCRATCHING&&bend_now(d,now)!=0};
 if(!c.playing)c.at_ms=now;
 c.tempo_bps=nominal(d)*(double)d->track.track_bpm_x100/6000.;
 if(!grid_phase(d,d->heard,&c.phase,&c.period_frames))return c;
 c.beats_per_second=d->heard_rate*d->r->rate/c.period_frames;
 c.nominal_beats_per_second=nominal(d)*grid_bpm(d,d->heard)/6000.;
 c.tempo_bps=c.nominal_beats_per_second;
 c.output_frame=d->timeline_output_frames;c.segment_count=d->segment_count;
 memcpy(c.segment,d->segment,d->segment_count*sizeof *d->segment);
 c.available=d->timeline_valid&&d->timeline_anchor_valid&&d->timeline_sink_frame&&
  !d->need_flush&&(uint32_t)(now-d->heard_at)<=150&&c.beats_per_second>0;
 return c;
}
/* Coordinates refer to accepted PCM, never a nominal-rate prediction. */
static int segment_at_wire(const struct sync_clock *c,uint64_t wire,double *phase,double *step,uint64_t *remaining,double *nominal_bps){
 if(!c->available)return 0;
 double output=(double)c->output_frame+(double)wire-(double)c->wire_frame;
 for(unsigned i=0;i<c->segment_count;i++){
  const struct sync_segment *s=&c->segment[i];
  if(output>=s->begin&&output<s->end){
   *phase=wrapped_beat(s->phase+(output-s->begin)*s->beat_step);*step=s->beat_step;
   if(remaining)*remaining=s->end-(uint64_t)output;
   if(nominal_bps)*nominal_bps=s->nominal_bps;
   return 1;
  }
 }
 return 0;
}
static void publish(struct xz_xp_players *p, struct deck *d, uint32_t now) {
 struct xz_master_observation actual={.deck=XZ_MASTER_UNKNOWN};
 if(p->master_observer)copy_master(p,&actual);
 struct xz_extra_deck v;
 memset(&v, 0, sizeof v);
 v.worker_load=d->render_load.latest;
 if(xz_diary_audio)xz_diary_audio((unsigned)d->index+2,d->underrun,v.worker_load.key_max_us,v.worker_load.samples!=0);
 v.worker_load.key_active=!!(atomic_load(&p->key_active)&(1u<<d->index));
 enum xz_xp_state s = d->state;
 /* Deck modes hold with or without a track, as the stock lamps show them. */
 v.vinyl = d->vinyl; v.master_tempo = d->master_tempo; v.quantize = d->quantize;v.quantize_den=d->sync_grid_den;
 v.remain = d->remain; v.auto_cue = d->auto_cue;
 v.known = XZ_EXTRA_KNOWN(XZ_EXTRA_F_VINYL) | XZ_EXTRA_KNOWN(XZ_EXTRA_F_MASTER_TEMPO) | XZ_EXTRA_KNOWN(XZ_EXTRA_F_QUANTIZE) |
           XZ_EXTRA_KNOWN(XZ_EXTRA_F_TIME_MODE);
 if (s != XZ_XP_EMPTY && s != XZ_XP_ERROR) {
  v.loaded = 1;
  snprintf(v.title, sizeof v.title, "%s", d->track.title);
  snprintf(v.artist, sizeof v.artist, "%s", d->track.artist);
  snprintf(v.key, sizeof v.key, "%s", d->track.key);
  v.source = d->track.source;
  v.tempo_x100 = d->tempo_x100;
  v.tempo_range = d->tempo_range;
  v.tempo_reset = d->tempo_reset;
  v.master=atomic_load(&p->sync_master)==d->index;
  v.master_scope=v.master?XZ_EXTRA_MASTER_SCOPE_SOFTWARE:XZ_EXTRA_MASTER_SCOPE_NONE;
  v.sync=d->sync;
  v.sync.quantum_unknown=d->quantize&&!d->sync_grid_den;
  if(d->emergency.active){v.sync.source=XZ_EXTRA_SYNC_SOURCE_EMERGENCY;v.sync.tempo_matched=0;v.sync.master_paused=0;}
  if(p->master_observer&&!authority_same(&actual,&d->tempo_authority))v.sync.tempo_matched=0;
  if(s!=XZ_XP_PLAYING||d->need_flush||!d->timeline_valid){
   v.sync.locked=0;if(v.sync.requested&&v.sync.reason==XZ_EXTRA_SYNC_LOCKED)v.sync.reason=XZ_EXTRA_SYNC_WAIT_ANCHOR;
  }
  v.tempo_revision = d->tempo_revision;
  v.known |= XZ_EXTRA_KNOWN(XZ_EXTRA_F_TITLE) | XZ_EXTRA_KNOWN(XZ_EXTRA_F_ARTIST) | XZ_EXTRA_KNOWN(XZ_EXTRA_F_SOURCE) |
            XZ_EXTRA_KNOWN(XZ_EXTRA_F_TEMPO) | XZ_EXTRA_KNOWN(XZ_EXTRA_F_MASTER) | XZ_EXTRA_KNOWN(XZ_EXTRA_F_PLAYING) |
            XZ_EXTRA_KNOWN(XZ_EXTRA_F_HOT_CUES) | XZ_EXTRA_KNOWN(XZ_EXTRA_F_TEMPO_RANGE) |
            XZ_EXTRA_KNOWN(XZ_EXTRA_F_SYNC) |
            (v.key[0] ? XZ_EXTRA_KNOWN(XZ_EXTRA_F_KEY) : 0);
  for (int i = 0; i < XZ_EXTRA_HOT_CUES; ++i)
   if (d->hot[i].set) v.hot_cue[i] = (struct xz_extra_cue){1, to_ms(d, d->hot[i].at), d->hot[i].rgb,
                                                           d->hot[i].loop ? to_ms(d, d->hot[i].end) : 0};
  v.memory_count=(unsigned)d->memory_count;
  for(unsigned i=0;i<v.memory_count;i++)v.memory_cue[i]=(struct xz_extra_cue){1,d->memory[i].saved_ms,d->memory[i].rgb,d->memory[i].loop?d->memory[i].saved_end_ms:0};
  v.known|=XZ_EXTRA_KNOWN(XZ_EXTRA_F_MEMORY_CUES);
  double published_bpm=grid_bpm(d,d->heard);
  if (published_bpm > 0) {
   v.track_bpm_x100 = (int32_t)llround(published_bpm);
   v.bpm_x100 = (int32_t)llround(published_bpm*(d->sync_rate_active?d->sync_rate:nominal(d)));
   v.known |= XZ_EXTRA_KNOWN(XZ_EXTRA_F_BPM);
  }
 }
 v.reverse = d->reverse; v.slip = d->slip; v.pad_mode = d->pad_mode; v.active_pad = d->active_pad;
 if (loaded(s)) {
  v.playing = streaming(s) && s != XZ_XP_SCRATCHING;
  v.held_playing = s == XZ_XP_SCRATCHING && streaming(d->scratch.resume);
  v.platter_contact = s == XZ_XP_SCRATCHING && d->scratch.contact;
  v.slip_active = d->slip_bg.active;
  v.loop.stored = d->loop.valid; v.loop.edit = d->loop.edit;
  v.total_ms = to_ms(d, d->r->frames);
  if (streaming(s)) { v.elapsed_ms = to_ms(d, d->heard); v.elapsed_at_ms = d->heard_at; }
  else { v.elapsed_ms = to_ms(d, d->pos >> 32); v.elapsed_at_ms = now; }
  v.cue_ms = to_ms(d, d->cue);
  v.loop.active = d->loop.active;
  v.loop.in_ms = to_ms(d, d->loop.in);
  v.loop.out_ms = to_ms(d, d->loop.out);
  /* Preserve zeroed padding: the visible-state revision compares the whole published object. */
  v.timeline.source_frame=streaming(s)?d->heard:d->pos>>32;v.timeline.sink_frame=d->timeline_sink_frame;
  v.timeline.observed_wire_frame=d->timeline_wire_completed;
  v.timeline.source_rate=d->r->rate;v.timeline.at_ms=streaming(s)?d->heard_at:now;v.timeline.revision=d->timeline_revision;
  v.timeline.route_epoch=d->epoch;v.timeline.load_revision=d->load_revision;v.timeline.transport_generation=d->generation;
  v.timeline.media_generation=d->media_generation;v.timeline.rate_ppm=(int32_t)llround(d->heard_rate*1000000.);
  v.timeline.moving=streaming(s)&&(d->submitted>d->completed||d->pending);
  if(!v.timeline.moving)v.timeline.rate_ppm=0;
  v.timeline.flags=XZ_EXTRA_TIMELINE_MOTION;
  if(!streaming(s))v.timeline.flags|=XZ_EXTRA_TIMELINE_ANCHOR|XZ_EXTRA_TIMELINE_RATE;
  else if(!d->need_flush&&d->timeline_anchor_valid&&(uint32_t)(now-d->heard_at)<=150u){
   v.timeline.flags|=XZ_EXTRA_TIMELINE_ANCHOR;
   if(d->timeline_valid)v.timeline.flags|=XZ_EXTRA_TIMELINE_RATE;
   if(d->timeline_sink_frame)v.timeline.flags|=XZ_EXTRA_TIMELINE_SINK_FRAME;
   if(d->output.rate_valid){v.timeline.completed_rate=d->output.rate;v.timeline.flags|=XZ_EXTRA_TIMELINE_APPLIED_RATE;}
   if(d->output.beat_valid){
    v.timeline.completed_beat_age_frames=d->output.beat_age;v.timeline.completed_beat_ms=d->output.beat_ms;
    v.timeline.flags|=XZ_EXTRA_TIMELINE_BEAT_HISTORY;
   }
  }
  v.known|=XZ_EXTRA_KNOWN(XZ_EXTRA_F_TIMELINE)|XZ_EXTRA_KNOWN(XZ_EXTRA_F_KEY_STATE);
  struct xz_deck_key *k=&v.key_state;
  k->owner=(struct xz_key_owner){.route_epoch=d->epoch,.load_revision=d->load_revision,.media_generation=d->media_generation};
  k->metadata_valid=xz_music_key_parse(d->track.key,&k->original);k->engine_ready=d->epoch!=0;
  k->desired_cents=d->key.desired;k->request_revision=d->key.revision;k->engaged=d->key.engaged;
  k->mt_before=d->key.mt_before;k->master_tempo=d->master_tempo;k->target_deck=d->key.target;
  if(d->emergency.active)k->audio_state=XZ_KEY_AUDIO_EMERGENCY;
  else if(d->epoch&&d->output.key_valid&&(!streaming(s)||(uint32_t)(now-d->heard_at)<=150u)){
   k->audio_state=d->output.key.state;k->applied_cents=d->output.key.cents;
   k->applied_revision=d->output.key.revision;k->transport_generation=d->generation;
   struct xz_key_snapshot ks={.loaded=k->metadata_valid,.original=k->original,.tempo_ratio=d->output.key.pitch_rate};
   k->playing_valid=k->audio_state==XZ_KEY_AUDIO_APPLIED&&xz_key_playing(&ks,k->applied_cents,&k->playing);
  }else if(d->epoch&&!d->key.ever_rendered&&!d->key.desired){
   k->audio_state=XZ_KEY_AUDIO_PREPARED;k->playing.key=k->original;
   k->playing_valid=k->metadata_valid&&!reversed(d)&&!d->search&&!d->ramp.active&&
    (d->master_tempo||fabs(speed(d,now)-1.)<1e-10);
  }
  if(k->playing_valid)xz_music_key_format(k->playing.key,XZ_KEY_CLASSIC,v.playing_key);
  v.known |= XZ_EXTRA_KNOWN(XZ_EXTRA_F_ELAPSED) | XZ_EXTRA_KNOWN(XZ_EXTRA_F_TOTAL) | XZ_EXTRA_KNOWN(XZ_EXTRA_F_CUE) |
             XZ_EXTRA_KNOWN(XZ_EXTRA_F_LOOP);
  const struct analysis *a = d->analysis;
  if (a && a->grid_count) { v.grid = a->grid; v.grid_count = a->grid_count; v.known |= XZ_EXTRA_KNOWN(XZ_EXTRA_F_GRID); }
  if (a && a->count) {
   v.wave.columns=a->columns;v.wave.count=a->count;v.wave.span_ms=a->span_ms;v.wave.normalization=a->normalization;
   v.known |= XZ_EXTRA_KNOWN(XZ_EXTRA_F_WAVE);
  }
 }
 pthread_mutex_lock(&p->publish_lock);
 p->duplicate_source[d->index]=(struct xz_extra_deck_event){.kind=XZ_EXTRA_IN_LOAD,.pressed=1};
 if(loaded(s)){
  p->duplicate_source[d->index].load.source=d->track.source;
  p->duplicate_source[d->index].load.track_id=d->track.track_id;
  p->duplicate_source[d->index].load.media_generation=d->media_generation;
  p->duplicate_source[d->index].load.route_epoch=d->epoch;
 }
 int selected=p->master_observer?observed_selection(&actual):atomic_load(&p->sync_master);
 v.master=selected==d->index;v.master_scope=v.master?(XZ_EXTRA_MASTER_SCOPE_SOFTWARE|(p->master_observer?XZ_EXTRA_MASTER_SCOPE_NATIVE:0)):0;
 if(!p->master_observer){
 if(selected<0&&v.sync.master_scope==XZ_EXTRA_MASTER_SCOPE_SOFTWARE){
  v.sync.tempo_matched=0;v.sync.master_paused=0;v.sync.source=XZ_EXTRA_SYNC_SOURCE_CLOCK_LOST;
  v.sync.locked=0;v.sync.master_deck=-1;v.sync.master_scope=0;
  if(v.sync.requested)v.sync.reason=XZ_EXTRA_SYNC_WAIT_MASTER;
 }else if(selected>=0&&v.sync.requested&&v.sync.master_deck!=selected+XZ_EXTRA_FIRST_DECK){
  v.sync.tempo_matched=0;v.sync.master_paused=0;v.sync.source=selected==d->index?XZ_EXTRA_SYNC_SOURCE_LIVE:XZ_EXTRA_SYNC_SOURCE_CLOCK_LOST;
  v.sync.locked=0;v.sync.master_deck=selected+XZ_EXTRA_FIRST_DECK;v.sync.master_scope=XZ_EXTRA_MASTER_SCOPE_SOFTWARE;
  v.sync.reason=selected==d->index?XZ_EXTRA_SYNC_MASTER_CLOCK:XZ_EXTRA_SYNC_DISCONTINUITY;
 }
 if(selected>=0&&selected!=d->index&&v.sync.requested&&
    d->sync_choice_seen!=atomic_load(&p->sync_choice_epoch)&&(v.sync.locked||v.sync.correction_ppm)){
  v.sync.locked=0;v.sync.correction_ppm=0;v.sync.reason=XZ_EXTRA_SYNC_DISCONTINUITY;
 }
 }else if(v.sync.requested&&(actual.deck<0||v.sync.master_deck!=actual.deck)){
  v.sync.tempo_matched=0;v.sync.master_paused=0;
  if(v.sync.source!=XZ_EXTRA_SYNC_SOURCE_EMERGENCY)v.sync.source=actual.deck==XZ_MASTER_NONE?XZ_EXTRA_SYNC_SOURCE_NONE:XZ_EXTRA_SYNC_SOURCE_CLOCK_LOST;
  v.sync.locked=0;v.sync.correction_ppm=0;v.sync.master_deck=actual.deck>=0?actual.deck:-1;
  v.sync.master_scope=actual.deck<0?0:actual.deck<2?XZ_EXTRA_MASTER_SCOPE_NATIVE:XZ_EXTRA_MASTER_SCOPE_NATIVE|XZ_EXTRA_MASTER_SCOPE_SOFTWARE;
  v.sync.reason=actual.deck==d->index+2?XZ_EXTRA_SYNC_MASTER_CLOCK:actual.deck==XZ_MASTER_EXTERNAL?XZ_EXTRA_SYNC_NATIVE_MASTER_UNSUPPORTED:XZ_EXTRA_SYNC_WAIT_MASTER;
 }
 p->tempo_view[d->index]=d->tempo_authority;
 p->clock[d->index]=loaded(d->state)?sync_clock(d,now):(struct sync_clock){0};
 const struct xz_extra_deck *before=&p->view[d->index];
 v.jog_content_epoch=s==XZ_XP_LOADING&&d->jog_load_pending?
  advance_jog_content_epoch(d->index):before->jog_content_epoch;
 if(s==XZ_XP_LOADING)d->jog_load_pending=0;
 if(!v.timeline.moving&&!before->timeline.moving&&v.elapsed_ms==before->elapsed_ms&&
    v.timeline.source_frame==before->timeline.source_frame&&v.timeline.revision==before->timeline.revision){
  v.elapsed_at_ms=before->elapsed_at_ms;v.timeline.at_ms=before->timeline.at_ms;
 }
 p->view_analysis[d->index]=loaded(s)?d->analysis:NULL;
 if(memcmp(before,&v,sizeof v)){
  p->view[d->index]=v;atomic_fetch_add_explicit(&p->revision,1,memory_order_release);
 }
 pthread_mutex_unlock(&p->publish_lock);
 atomic_store(&d->published_state, (int)s);
}

static struct analysis *load_analysis(const char *dir, struct xz_anlz_extra *extra) {
 struct analysis *a = calloc(1, sizeof *a);
 if(a)atomic_init(&a->readers,0);
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

static int load_inner(struct xz_xp_players *p, struct deck *d, const struct xz_extra_deck_event *e, uint32_t now, int next) {
 if(p->ram.invalidate)p->ram.invalidate(p->ram.context,(unsigned)d->index);
 struct xz_extra_track t;
 if(e->load.route_epoch && e->load.route_epoch != p->env.sink.link(p->env.sink.ctx)) { xz_xp_notice(p,"ROUTE CHANGED / LOAD REFUSED"); return 0; }
 memset(&t, 0, sizeof t);
 int resolved;
#ifdef XZ_XP_SERVICE_PROOF
 uint32_t sp_begin=p->env.now_ms();
#endif
 if (e->load.media_generation)
  resolved = p->env.resolve_checked ? p->env.resolve_checked(e->load.source, e->load.track_id, e->load.media_generation, &t) : -1;
 else resolved = p->env.resolve ? p->env.resolve(e->load.source, e->load.track_id, &t) : -1;
 SP_ONLY(sp_service(&d->sp,XP_SP_RESOLVER,resolved,sp_begin,p->env.now_ms()));
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
 #ifdef XZ_XP_SERVICE_PROOF
  uint32_t sp_verify_begin=p->env.now_ms();
#endif
  int stale=!p->env.resolve_checked||p->env.resolve_checked(e->load.source,e->load.track_id,e->load.media_generation,&verify);
  SP_ONLY(sp_service(&d->sp,XP_SP_RESOLVER,stale,sp_verify_begin,p->env.now_ms()));
  if(stale||e->load.route_epoch!=p->env.sink.link(p->env.sink.ctx)){
   xz_stem_media_close(prepared);xz_xp_notice(p,stale?"SOURCE CHANGED / NEXT REFUSED":"ROUTE CHANGED / NEXT REFUSED");return 0;
  }
 }
 if (d->r->media&&!next) { jump(d, 0, now); flush(p, d); }
 if(d->key.engaged)d->master_tempo=d->key.mt_before;
 atomic_fetch_and(&p->key_active,~(1u<<d->index));
 memset(&d->render_load,0,sizeof d->render_load);
 memset(&d->key,0,sizeof d->key);d->key.target=-1;key_history_reset(d);
 d->jog_load_pending=1;
 d->state = XZ_XP_LOADING; d->track = t;
 d->emergency.active=0;d->emergency.count=0;
 memset(&d->gate,0,sizeof d->gate);
 memset(&d->loop, 0, sizeof d->loop);
 memset(d->hot, 0, sizeof d->hot);
 if(d->slip_reverse)d->reverse=0;
 d->pad_hold=PAD_HOLD_NONE;
 d->active_pad = d->slip_pad = d->loop_pad = -1; d->slip_bg.active = d->slip_reverse = 0; d->ramp.active = 0;
 d->track_prev_known=0;
 d->slip = 0; d->slip_pause = 0; d->search = 0;   /* p.60: loading a track cancels SLIP */
 publish(p, d, now);
 retire_reader(p,d);
 retire(d, now);
 struct xz_stem_media *media = prepared?prepared:xz_stem_media_open(t.path, 0);
 if (!media) { xz_extra_order_release(d->order);d->order=NULL;d->state = XZ_XP_ERROR; xz_xp_notice(p, "TRACK CANNOT BE READ"); return 0; }
 d->r->media = media; d->r->rate = xz_stem_media_rate(media); d->r->frames = xz_stem_media_frames(media);
 struct xz_anlz_extra extra;
 d->analysis = load_analysis(t.anlz_dir, &extra);
 if (d->track.track_bpm_x100 <= 0) d->track.track_bpm_x100 = extra.grid_bpm_x100;
 for (int i = 0; i < XZ_EXTRA_HOT_CUES; ++i)
  if (extra.hot_cue[i].set) {
   d->hot[i] = (struct hot){1, to_frames(d, extra.hot_cue[i].ms), extra.hot_cue[i].rgb, 0, 0};
   d->hot[i].loop = stored_loop_end(d, extra.hot_cue[i].ms, extra.hot_cue[i].end_ms, &d->hot[i].end);
  }
 xz_anlz_extra_free(&extra);
 d->called_hot=0;
 if(d->pad_mode==XZ_EXTRA_PAD_CALL)d->pad_mode=XZ_EXTRA_PAD_HOT_CUE;
 d->active_loop.armed=0;
 struct xz_shana_cue active={0};
 if(xz_shana_active_loop(t.anlz_dir,&active)&&stored_loop_end(d,active.ms,active.end_ms,&d->active_loop.out)){
  d->active_loop.in=to_frames(d,active.ms);d->active_loop.armed=1;
 }
 struct xz_shell_cue memory[XZ_SHELL_MEMORY_CUES];
 d->cue = d->auto_cue && t.anlz_dir[0] && xz_shana_memory_cues(t.anlz_dir, memory) > 0 ? to_frames(d, memory[0].ms) : 0;
 d->auto_cue_at = d->cue;
 /* CUE/LOOP CALL steps through the track's memory points (p.51). */
 d->memory_count = 0;
 if (t.anlz_dir[0]) {
  struct xz_shana_cue list[XZ_SHELL_MEMORY_CUES];
  int n = xz_shana_cue_list(t.anlz_dir, list, XZ_SHELL_MEMORY_CUES);
  for (int i = 0; i < n; ++i) {
   struct memory_point *m = &d->memory[d->memory_count];
   m->at = to_frames(d, list[i].ms); m->end = 0;m->rgb=list[i].rgb;m->saved_ms=list[i].ms;
   m->loop = list[i].loop && stored_loop_end(d, list[i].ms, list[i].end_ms, &m->end);
   m->saved_end_ms=m->loop?list[i].end_ms:0;
   d->memory_count++;
  }
 }
 if(p->cue_read){
  struct xz_saved_cues saved={0};int found=p->cue_read(&t,e->load.media_generation,to_ms(d,d->r->frames),&saved);
  if(found>0&&saved.memory_count<=XZ_CUE_MEMORY){
   memset(d->hot,0,sizeof d->hot);
   for(unsigned i=0;i<8;i++)if(saved.hot[i].set){
    d->hot[i]=(struct hot){1,to_frames(d,saved.hot[i].ms),saved.hot[i].rgb,0,0};
    d->hot[i].loop=stored_loop_end(d,saved.hot[i].ms,saved.hot[i].end_ms,&d->hot[i].end);
   }
   d->memory_count=(int)saved.memory_count;int active=0;
   for(unsigned i=0;i<saved.memory_count;i++){
    struct memory_point *m=&d->memory[i];m->at=to_frames(d,saved.memory[i].ms);m->end=0;m->rgb=saved.memory[i].rgb;m->saved_ms=saved.memory[i].ms;
    m->loop=stored_loop_end(d,saved.memory[i].ms,saved.memory[i].end_ms,&m->end);
    m->saved_end_ms=m->loop?saved.memory[i].end_ms:0;
    active|=m->loop&&m->at==d->active_loop.in&&m->end==d->active_loop.out;
   }
   if(!active)d->active_loop.armed=0;
   d->cue=d->auto_cue&&d->memory_count?d->memory[0].at:0;d->auto_cue_at=d->cue;
  }else if(found<0)xz_xp_notice(p,"SAVED CUES INVALID / USING EXPORT");
 }
 memcpy(d->saved_hot,d->hot,sizeof d->hot);
 jump(d, d->cue, now);
 d->state = XZ_XP_CUED;
 d->load_revision++;d->media_generation=e->load.media_generation;
 if(p->ram.bind){
  unsigned assets=XZ_RAM_CUES;
  if(d->analysis&&d->analysis->columns)assets|=XZ_RAM_WAVE;
  if(d->analysis&&d->analysis->grid_count)assets|=XZ_RAM_GRID;
  p->ram.bind(p->ram.context,(unsigned)d->index,media,(unsigned)e->load.source,e->load.track_id,d->media_generation,t.path,assets);
 }
 SP_ONLY(sp_newsession(d,now));
 struct xz_extra_order *order=xz_extra_order_matches(e->load.order,e->load.source,e->load.media_generation,e->load.order_index,e->load.track_id)?e->load.order:NULL;
 xz_extra_order_retain(order);xz_extra_order_release(d->order);d->order=order;
 d->order_index=e->load.order_index;d->order_epoch=e->load.route_epoch;
 return 1;
}

static int current_master(struct xz_xp_players *p,const struct deck *d);
static int load(struct xz_xp_players *p, struct deck *d, const struct xz_extra_deck_event *e, uint32_t now, int next) {
 unsigned mask=e->load.source==1?1u:e->load.source==2?2u:3u;
 if(xz_diary_busy_begin)xz_diary_busy_begin(mask);
 int was_master=current_master(p,d);
 int result=load_inner(p,d,e,now,next);
 if(result&&was_master)d->master_load_handoff=1; /* M5: master_handoff passes master on */
 if(xz_diary_busy_end)xz_diary_busy_end(mask);
 return result;
}

/* QUANTIZE (manual, Quantize): with it on, cue, loop and hot cue points land on the nearest beat of the
   track's grid, and a hot cue played mid-beat keeps the beat position. One-beat resolution: the
   QUANTIZE BEAT VALUE setting is not readable here. Without a grid or with QUANTIZE off, points stay where
   they were pressed. */
/* The beat at or before at, in source frames; it can lie before the track start (the grid's first
   interval extended backwards). */
static int beat_start(const struct deck *d, uint64_t at, double *start, double *period) {
 double phase, length;
 if (!d->quantize || !grid_phase(d, at, &phase, &length) || length <= 0) return 0;
 *start = (double)at - (phase - floor(phase)) * length;
 *period = length;
 return 1;
}
/* Nearest beat inside the track. */
static uint64_t quantize_point(const struct deck *d, uint64_t at) {
 double start, period;
 if (!beat_start(d, at, &start, &period)) return at;
 double snapped = ((double)at - start) * 2 >= period ? start + period : start;
 if (snapped < 0) snapped += period;
 if (snapped >= (double)d->r->frames) snapped -= period;
 return snapped < 0 ? at : (uint64_t)llround(snapped);
}
/* The beat at or before at (the first beat when at precedes it). */
static uint64_t quantize_floor(const struct deck *d, uint64_t at) {
 double start, period;
 if (!beat_start(d, at, &start, &period)) return at;
 if (start < 0) start += period;
 return start >= (double)d->r->frames ? at : (uint64_t)llround(start);
}
static void cue_button(struct deck *d, int pressed, uint32_t now) {
 if(d->state==XZ_XP_SCRATCHING&&(pressed||d->scratch.resume==XZ_XP_SAMPLING))
  scratch_release(d,now);
 if (pressed) {
  memset(&d->gate,0,sizeof d->gate);d->search=0;
  if(d->slip_reverse)d->reverse=0;
  d->pad_hold=PAD_HOLD_NONE;
  d->loop.active = 0; d->loop.edit = 0; d->active_pad = d->slip_pad = d->loop_pad = -1; d->slip_bg.active = d->slip_reverse = 0;
 }
 if (!pressed) {
  if (d->state == XZ_XP_SAMPLING) { jump(d, d->cue, now); d->state = XZ_XP_CUED; }
  return;
 }
 if (d->state == XZ_XP_PLAYING || d->state == XZ_XP_ENDED) { jump(d, d->cue, now); d->state = XZ_XP_CUED; }
 else if (d->state == XZ_XP_CUED) {
  if (d->pos == d->cue << 32) { start(d, now); d->state = XZ_XP_SAMPLING; }
  else { d->cue = quantize_point(d, d->pos >> 32); d->pos = d->cue << 32; }
 }
}

static void hot_cue(struct deck *d, const struct xz_extra_deck_event *e, uint32_t now) {
 if (!e->pressed || e->hot_cue < 0 || e->hot_cue >= XZ_EXTRA_HOT_CUES) return;
 struct hot *h = &d->hot[e->hot_cue];
 if (e->shift) { memset(h, 0, sizeof *h); return; }
 uint64_t at = heard_now(d, now);
 if (!h->set) {
  /* During loop playback the loop is stored on the pad (p.53). */
  if (d->loop.active && d->loop.out > d->loop.in) *h = (struct hot){1, d->loop.in, 0, d->loop.out, 1};
  else *h = (struct hot){1, quantize_point(d, at), 0, 0, 0};
  return;
 }
 if (h->loop) {
  d->loop.in = h->at; d->loop.out = h->end; d->loop.valid = d->loop.active = d->loop.has_in = 1; d->loop.edit = 0;
  d->active_pad = -1; d->loop_pad = e->hot_cue;
  jump(d, h->at, now); start(d, now);
  return;
 }
 d->loop.active = 0; d->loop.edit = 0; d->active_pad = -1; d->loop_pad = -1;
 uint64_t to = h->at;
 if (d->state == XZ_XP_PLAYING) {
  uint64_t start = quantize_floor(d, at);
  if (start != at) { to += at - start; if (to >= d->r->frames) to = h->at; }
 }
 jump(d, to, now);
 start(d, now);
}

static uint64_t jog_target(const struct deck *d,uint64_t from,int32_t delta) {
 double target=(double)from/(double)ONE+(double)delta*d->r->rate*XZ_XP_SEARCH_MS_PER_REV/
               (1000.0*XZ_XP_JOG_TICKS_PER_REV);
 if(target<=0)return 0;
 if(target>=(double)d->r->frames)return d->r->frames<<32;
 return (uint64_t)(target*(double)ONE);
}
static void jog(struct xz_xp_players *p,struct deck *d,int32_t delta,float platter_speed,
                int speed_known,uint32_t now,uint32_t arrived) {
 int qualified_speed=speed_known&&isfinite(platter_speed);
 if (d->loop.edit && d->loop.active) {
  if(!delta)return;
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
  if(qualified_speed)d->scratch.platter_known=1;
  /* Later ticks invalidate an old stopped sample even if speed is unknown.
     Unknown-only gestures still use the existing bounded release. */
  if(delta)d->scratch.platter_moving=1;
  else if(qualified_speed)d->scratch.platter_moving=platter_speed!=0.f;
  if(!delta){
   if(d->scratch.coasting&&qualified_speed&&platter_speed==0.f)scratch_finish(d,now);
   return;
  }
  if(d->scratch.overflow)return;
  if(!d->scratch.group_open||d->scratch.at!=arrived){
   scratch_seal(d);
   /* The quiet interval has elapsed before this motion arrives. Preserve an
      audible 16 ms onset, then use the current inter-arrival cadence. */
   int32_t elapsed=(int32_t)(arrived-d->scratch.at);
   d->scratch.interval=d->scratch.at_valid&&elapsed>0
    ?(elapsed>16?16u:(uint32_t)elapsed):16u;
   d->scratch.at=arrived;d->scratch.at_valid=1;
   d->scratch.group_open=1;d->scratch.group_ticks=0;
   d->scratch.group_origin=d->scratch.target;d->scratch.ticks=0;
  }
  d->scratch.group_ticks+=fabs((double)delta);
  double rate=d->scratch.group_ticks*XZ_XP_SEARCH_MS_PER_REV/
              ((double)XZ_XP_JOG_TICKS_PER_REV*d->scratch.interval);
  if(rate>8)rate=8;d->scratch.rate=delta>0?rate:-rate;
  unsigned tail=(d->scratch.head+d->scratch.count)%SCRATCH_SEGMENTS;
  struct scratch_segment *last=d->scratch.group_count?
    &d->scratch.segment[(tail+SCRATCH_SEGMENTS-1)%SCRATCH_SEGMENTS]:NULL;
  int merge=last&&((last->direction>0)==(delta>0));
  uint64_t from=merge?last->begin:d->scratch.target;
  /* Relative group counts keep packet splits and balanced legs on the same
     Q32 endpoint. Reset at a source clamp so a reversal starts at that edge. */
  d->scratch.ticks+=(int64_t)delta;
  double to=(double)d->scratch.group_origin/(double)ONE+(double)d->scratch.ticks*d->r->rate*XZ_XP_SEARCH_MS_PER_REV/
             (1000.0*XZ_XP_JOG_TICKS_PER_REV);
  uint64_t target=to<=0?0:to>=(double)d->r->frames?d->r->frames<<32:(uint64_t)(to*(double)ONE);
  if(to<=0||to>=(double)d->r->frames){d->scratch.group_origin=target;d->scratch.ticks=0;}
  if(target==d->scratch.target)return;
  if(!merge&&d->scratch.count==SCRATCH_SEGMENTS){
   SP_ONLY(sp_incident(d,p->env.now_ms()));
   jump(d,d->heard,now);d->scratch.overflow=1;
   xz_xp_notice(p,"SCRATCH BUFFER FULL / RETOUCH");return;
  }
  if(merge){last->end=target;}
  else{
   d->scratch.segment[tail]=(struct scratch_segment){.begin=from,.end=target,.direction=delta>0?1:-1};
   d->scratch.count++;d->scratch.group_count++;
  }
  d->scratch.target=target;
  return;
 }
 if(!delta)return;
 if (streaming(d->state)) {
  float b = bend_now(d, arrived) + (float)delta * XZ_XP_BEND_PER_TICK;
  d->bend = b > XZ_XP_BEND_MAX ? XZ_XP_BEND_MAX : b < -XZ_XP_BEND_MAX ? -XZ_XP_BEND_MAX : b;
  d->bend_at = arrived;
  return;
 }
 uint64_t to=jog_target(d,d->pos,delta);
 jump(d,to>>32,now);
 d->pos=to; /* Keep sub-frame displacement across paused packets, as scratch does. */
 d->state = to >= (d->r->frames<<32) ? XZ_XP_ENDED : XZ_XP_CUED;
}

static void sync_request(struct xz_xp_players *p,struct deck *d,int on,uint32_t now){
 /* S2: SYNC off keeps the phase and the synced BPM: the held rate stays the master's tempo, and the phase
    trim in effect fades out over XZ_XP_SYNC_TRIM_RELEASE_MS instead of stepping the speed. Freezing it
    into the held rate would leave the deck off the synced BPM. The tempo fader picks up from the held
    rate (soft takeover). */
 d->sync_trim_out=on?0:d->sync_trim;d->sync_trim_out_at=now;
 d->sync_offset=0;d->sync_offset_adopt=0;
 d->sync=(struct xz_extra_sync){.requested=!!on,.master_deck=-1,.reason=on?XZ_EXTRA_SYNC_WAIT_MASTER:XZ_EXTRA_SYNC_OFF};
 d->sync_initial=on&&d->quantize?SYNC_INITIAL_EXPLICIT:SYNC_INITIAL_NONE;d->sync_hold=d->sync_have_master=d->sync_lock_count=0;d->sync_stationary=0;
 d->sync_native_captured_us=0;d->sync_native_discontinuous=d->sync_native_gap_hold=0;
 d->sync_hold_reason=XZ_EXTRA_SYNC_OFF;
 d->sync_trim=0;d->tempo_native_identity=d->tempo_native_revision=0;
 if(on)atomic_fetch_or(&p->sync_requests,1u<<d->index);else atomic_fetch_and(&p->sync_requests,~(1u<<d->index));
 /* Legacy embedders can supply BPM only. It is sampled once, never advertised as phase lock. */
 d->sync_legacy_bpm=on&&atomic_load(&p->sync_master)<0&&!p->native_sync_clock&&p->env.master_bpm?p->env.master_bpm():0;
}
static int sync_rate_supported(double rate){
 /* Engine envelope: WIDE already supports nominal 0..2; leave headroom for
  * bend/phase trim inside the ordinary completed-timeline rate bound (3.1).
  * Independent of the manual fader range, not stock octave policy. */
 return isfinite(rate)&&rate>0&&rate<=2.;
}
static int sync_base(struct deck *d,double rate){
 if(!sync_rate_supported(rate))return 0;
 int32_t tempo=(int32_t)llround((rate-1)*10000);
 if(tempo!=d->tempo_x100){d->tempo_x100=tempo;d->tempo_revision++;}
 d->tempo_reset=0;d->tempo_position=(int32_t)((int64_t)tempo*XZ_EXTRA_TEMPO_SPAN/(d->tempo_range*100));
 d->sync_rate=rate;d->sync_rate_active=1;return 1;
}
static void sync_set_trim(struct deck *d,double trim){
 d->sync_trim=trim;d->sync.correction_ppm=(int32_t)llround(trim*1000000.);
}
static int sync_apply_base(struct xz_xp_players *p,struct deck *d,int selected,uint32_t *choice,double rate){
 int valid=authority_before(p,d);
 /* A revision-only publication race can converge here. Operator, source and
    route changes must be selected again by the worker before issuing a rate. */
 for(unsigned retry=0;!valid&&p->master_observer&&retry<2;retry++){
  struct xz_master_observation next,previous=d->master_actual;copy_master(p,&next);
  previous.revision=next.revision;
  if(next.deck<0||!authority_same(&previous,&next))break;
  d->master_actual=next;*choice=next.revision;valid=authority_before(p,d);
 }
 pthread_mutex_lock(&p->publish_lock);
 valid=valid&&authority_locked(p,d,selected,*choice);
 int applied=valid?sync_base(d,rate):-1;
 if(applied>0){
  d->tempo_authority=d->master_actual;d->sync.tempo_matched=1;
  if(selected<0){d->tempo_native_deck=d->sync.master_deck;
   d->tempo_native_identity=d->sync_native_identity;d->tempo_native_revision=d->sync_native_source_revision;}
 }else{d->sync.tempo_matched=0;if(applied<0)d->sync.source=XZ_EXTRA_SYNC_SOURCE_CLOCK_LOST;}
 pthread_mutex_unlock(&p->publish_lock);return applied;
}
enum {SYNC_NATIVE_GAP_NONE,SYNC_NATIVE_GAP_WAIT_PHASE,SYNC_NATIVE_GAP_HAVE_PHASE};
enum {SYNC_NATIVE_GAP_BLOCKED,SYNC_NATIVE_GAP_STAY,SYNC_NATIVE_GAP_RELEASE};
/* A forward capture gap is not an operator or identity change. Keep BPM follow,
 * anchor the first later phase sample, and require a second fresh sample to pass
 * the existing phase-progression check. Only then may the existing bounded trim
 * prove lock again. No post-gap sample is projected from the pre-gap timestamp. */
static int sync_native_gap_tempo(struct xz_xp_players *p,struct deck *d,
 const struct xz_sync_native_snapshot *native,int selected,uint32_t choice,uint32_t now){
 if(!d->sync_native_gap_hold||d->sync_hold_reason!=XZ_EXTRA_SYNC_DISCONTINUITY)return 0;
 struct sync_clock slave=sync_clock(d,now);
 uint64_t cur_us=native->captured_us?native->captured_us:(uint64_t)native->captured_ms*1000u;
 if(selected>=0||!d->sync_have_master||d->sync.master_scope!=XZ_EXTRA_MASTER_SCOPE_NATIVE||
    d->sync_master_id!=native->native_deck||d->sync_native_identity!=native->track_identity||
    d->sync_native_source_revision!=native->source_revision||d->sync_choice_seen!=choice||
    d->epoch!=d->sync_master_clock.route||d->load_revision!=d->sync_master_clock.load||
    d->generation!=d->sync_master_clock.generation||d->underrun!=d->sync_slave_underrun||
    d->state!=XZ_XP_PLAYING||!native->playing||
    !cur_us||cur_us<d->sync_native_captured_us){
  d->sync_native_gap_hold=0;return 0;
 }
 if(slave.manual||native->manual){d->sync.reason=XZ_EXTRA_SYNC_MANUAL;return 1;}
 if(d->track.track_bpm_x100<=0){d->sync.reason=XZ_EXTRA_SYNC_NO_SOURCE_BPM;return 1;}
 double source_bpm=grid_bpm(d,d->heard);
 double rate=source_bpm>0?(double)native->bpm*100./source_bpm:0.;
 int applied=sync_apply_base(p,d,selected,&choice,rate);
 if(applied<=0){d->sync.reason=applied<0?XZ_EXTRA_SYNC_ACQUIRING:XZ_EXTRA_SYNC_OUT_OF_RANGE;return 1;}
 int phase_usable=native->phase_available&&isfinite(native->bar_phase)&&native->bar_phase>=0.f&&native->bar_phase<4.f&&
  native->uncertainty_us<=XZ_SYNC_NATIVE_MAX_PHASE_UNCERTAINTY_US&&slave.available&&slave.period_frames&&slave.loop_aligned;
 if(phase_usable&&d->sync_native_gap_hold==SYNC_NATIVE_GAP_HAVE_PHASE&&cur_us>d->sync_native_captured_us){
  uint64_t elapsed_us=cur_us-d->sync_native_captured_us;
  if(elapsed_us<=150000u){
   double dt_obs=(double)elapsed_us/1000000.;
   double min_step=dt_obs*(fmin(d->sync_native_bpm,(double)native->bpm)/60.);
   double max_step=dt_obs*(fmax(d->sync_native_bpm,(double)native->bpm)/60.);
   double mid_step=.5*(min_step+max_step),tol=.5*(max_step-min_step)+.06;
   double phase_jump=wrapped_beat((double)native->bar_phase-(d->sync_native_bar_phase+mid_step)+.5)-.5;
   if(fabs(phase_jump)<=tol){
    d->sync_hold=0;d->sync_hold_reason=XZ_EXTRA_SYNC_OFF;d->sync_native_gap_hold=SYNC_NATIVE_GAP_NONE;
    d->sync_native_discontinuous=0;return SYNC_NATIVE_GAP_RELEASE;
   }
   d->sync_native_gap_hold=SYNC_NATIVE_GAP_NONE;return SYNC_NATIVE_GAP_BLOCKED;
  }
 }
 d->sync_native_captured_us=cur_us;d->sync_native_bpm=native->bpm;
 if(phase_usable){d->sync_native_bar_phase=native->bar_phase;d->sync_native_gap_hold=SYNC_NATIVE_GAP_HAVE_PHASE;}
 else d->sync_native_gap_hold=SYNC_NATIVE_GAP_WAIT_PHASE;
 d->sync.reason=XZ_EXTRA_SYNC_DISCONTINUITY;return SYNC_NATIVE_GAP_STAY;
}
/* Stock SYNC stays on until it is pressed off. A break (pause, jump, master change, clock gap, underrun)
   keeps following the tempo and finds the beat again, snapping to it when QUANTIZE is on at most once per
   XZ_XP_SYNC_RESNAP_MS; between snaps the bounded phase trim closes the gap. */
static void sync_offset_clear(struct deck *d){d->sync_offset=0;d->sync_offset_adopt=0;d->sync.offset=0;}
/* S4/S8: the first measurement after a side nudge (here or on the master) becomes the kept offset. Under
   XZ_XP_SYNC_OFFSET_MS it counts as aligned: no offset, the lamp stays steady. */
static void sync_offset_take(struct deck *d,double error,double error_seconds){
 d->sync_offset_adopt=0;d->sync_lock_count=0;
 if(fabs(error_seconds)*1000.>XZ_XP_SYNC_OFFSET_MS){d->sync_offset=error;d->sync.offset=1;}
 else{d->sync_offset=0;d->sync.offset=0;}
}
static void sync_reacquire(struct deck *d,uint32_t now){
 sync_offset_clear(d);
 d->sync_hold=0;d->sync_hold_reason=XZ_EXTRA_SYNC_OFF;d->sync_have_master=d->sync_lock_count=0;
 d->sync_native_captured_us=0;d->sync_native_discontinuous=d->sync_native_gap_hold=0;
 /* An explicit alignment survives phase gaps and bypasses the automatic cooldown. */
 d->sync_initial=!d->quantize?SYNC_INITIAL_NONE:
  d->sync_initial==SYNC_INITIAL_EXPLICIT?SYNC_INITIAL_EXPLICIT:
  d->sync_snap_at&&(uint32_t)(now-d->sync_snap_at)<XZ_XP_SYNC_RESNAP_MS?SYNC_INITIAL_AFTER_COOLDOWN:SYNC_INITIAL_NOW;
 d->sync.reason=XZ_EXTRA_SYNC_ACQUIRING;
}
static int sync_snap_ready(const struct deck *d,uint32_t now){
 return d->sync_initial==SYNC_INITIAL_EXPLICIT||d->sync_initial==SYNC_INITIAL_NOW||
  (d->sync_initial==SYNC_INITIAL_AFTER_COOLDOWN&&
   (uint32_t)(now-d->sync_snap_at)>=XZ_XP_SYNC_RESNAP_MS);
}
/* Phase acquisition needs two advancing, coherent observations. A fresh timestamp
   alone is not evidence that a broken clock has recovered. */
static int native_phase_ready(struct deck *d,const struct xz_sync_native_snapshot *n){
 uint64_t at=n->captured_us?n->captured_us:(uint64_t)n->captured_ms*1000u;
 if(!n->phase_available||!n->playing||n->manual||!isfinite(n->bar_phase)||n->bar_phase<0||
    n->bar_phase>=4||n->uncertainty_us>XZ_SYNC_NATIVE_MAX_PHASE_UNCERTAINTY_US){
  d->native_phase.have=d->native_phase.ready=0;return 0;
 }
 if(d->native_phase.have&&at==d->native_phase.at)return d->native_phase.ready;
 int coherent=0;
 if(d->native_phase.have&&at>d->native_phase.at&&at-d->native_phase.at<=150000u){
  double dt=(double)(at-d->native_phase.at)/1000000.;
  double low=dt*fmin(d->native_phase.bpm,(double)n->bpm)/60.;
  double high=dt*fmax(d->native_phase.bpm,(double)n->bpm)/60.;
  double error=wrapped_beat(n->bar_phase-d->native_phase.phase-.5*(low+high)+.5)-.5;
  coherent=fabs(error)<=.06+.5*(high-low);
 }
 d->native_phase.have=1;d->native_phase.ready=coherent;
 d->native_phase.at=at;d->native_phase.phase=n->bar_phase;d->native_phase.bpm=n->bpm;
 return coherent;
}
static double native_tempo_rate(const struct deck *d,double bpm){
 double source_bpm=grid_bpm(d,d->heard);
 return source_bpm>0?bpm*100./source_bpm:0.;
}
/* A paused deck has an exact source position but no running output clock. Apply
   the requested alignment once; retain BPM without claiming continuous phase lock. */
static int sync_snap_ready(const struct deck *,uint32_t);
static void sync_stationary(struct xz_xp_players *p,struct deck *d,int selected,uint32_t choice,
 double bpm,double master_phase,int phase_known,uint32_t now){
 (void)bpm;
 if(d->state==XZ_XP_SCRATCHING||bend_now(d,now)!=0){d->sync.reason=XZ_EXTRA_SYNC_MANUAL;return;}
 if(d->quantize&&!d->sync_grid_den){d->sync.reason=XZ_EXTRA_SYNC_NO_GRID;return;}
 if(d->quantize&&sync_snap_ready(d,now)){
  double phase,period;
  if(!phase_known){d->sync.reason=XZ_EXTRA_SYNC_NATIVE_PHASE_UNAVAILABLE;return;}
  if(!grid_phase(d,d->heard,&phase,&period)){d->sync.reason=XZ_EXTRA_SYNC_NO_GRID;return;}
  if(!aligned_loop(d)){d->sync.reason=XZ_EXTRA_SYNC_LOOP_UNALIGNED;return;}
  double delta=quantized_delta(phase-master_phase,d->sync_grid_den);
  double to=(double)d->heard-delta*period;
  if(d->loop.active){double span=d->loop.out-d->loop.in;to=d->loop.in+fmod(to-d->loop.in+span,span);}
  else {if(to<0)to+=period;if(to>=d->r->frames)to-=period;}
  if(to<0||to>=d->r->frames){d->sync.reason=XZ_EXTRA_SYNC_WAIT_ANCHOR;return;}
  int current=authority_before(p,d);
  pthread_mutex_lock(&p->publish_lock);
  current=current&&authority_locked(p,d,selected,choice);
  if(current){jump(d,(uint64_t)llround(to),now);d->sync_initial=0;d->sync_snap_at=now|1u;sync_offset_clear(d);}
  pthread_mutex_unlock(&p->publish_lock);
  if(!current){d->sync.reason=XZ_EXTRA_SYNC_WAIT_MASTER;return;}
 }
 d->sync_stationary=1;d->sync_have_master=0;d->sync.reason=XZ_EXTRA_SYNC_TEMPO_FOLLOW;
}
static unsigned sync_grid_refresh(struct xz_xp_players *p,struct deck *d){
 unsigned packed=atomic_load_explicit(&p->sync_grid,memory_order_acquire),denominator=packed&15u;
 if(denominator!=1&&denominator!=2&&denominator!=4&&denominator!=8)denominator=0;
 if(d->sync_grid_seen!=packed){
  d->sync_grid_seen=packed;d->sync_grid_den=denominator;
  if(d->sync_initial!=SYNC_INITIAL_EXPLICIT)d->sync_initial=d->quantize;
  d->sync_have_master=0;d->sync_lock_count=0;
 }
 return denominator;
}
static int wire_time_ms(struct xz_xp_players *,uint32_t,uint64_t,double *);
static void sync_update(struct xz_xp_players *p,struct deck *d,const xz_usb_position *position,uint32_t now){
 d->sync_trim=0;d->sync.correction_ppm=0;d->sync.locked=0;
 if(!d->sync.requested)return;
 unsigned grid=sync_grid_refresh(p,d);
 d->sync.tempo_matched=0;d->sync.master_paused=0;d->sync.quantum_unknown=d->quantize&&!grid;
 d->sync.source=XZ_EXTRA_SYNC_SOURCE_CLOCK_LOST;
 if(d->emergency.active){d->sync.source=XZ_EXTRA_SYNC_SOURCE_EMERGENCY;d->sync.reason=XZ_EXTRA_SYNC_NATIVE_PHASE_UNAVAILABLE;return;}
 uint32_t choice;int selected=sync_selection(p,d,&choice);
 if(selected< -1){
  d->native_phase.have=d->native_phase.ready=0;
  if(d->master_actual.deck!=XZ_MASTER_UNKNOWN)d->tempo_native_identity=d->tempo_native_revision=0;
  if(p->master_observer&&d->master_actual.deck==XZ_MASTER_NONE)d->sync.source=XZ_EXTRA_SYNC_SOURCE_NONE;
  d->sync.reason=selected==-3?XZ_EXTRA_SYNC_NATIVE_MASTER_UNSUPPORTED:XZ_EXTRA_SYNC_WAIT_MASTER;return;
 }
 if(selected>=0||(p->master_observer&&d->master_actual.deck!=d->tempo_native_deck))d->tempo_native_identity=d->tempo_native_revision=0;
 if(selected==d->index){d->sync.source=XZ_EXTRA_SYNC_SOURCE_LIVE;d->sync.master_paused=d->state!=XZ_XP_PLAYING&&d->state!=XZ_XP_SCRATCHING&&d->state!=XZ_XP_SAMPLING;d->sync.master_deck=selected+XZ_EXTRA_FIRST_DECK;d->sync.master_scope=XZ_EXTRA_MASTER_SCOPE_SOFTWARE;d->sync.reason=XZ_EXTRA_SYNC_MASTER_CLOCK;return;}
 if(selected<0){
  if(d->sync.master_scope==XZ_EXTRA_MASTER_SCOPE_SOFTWARE){
   d->sync_hold=d->sync_have_master=0;d->sync.master_scope=0;d->sync.master_deck=-1;
   d->sync_native_captured_us=0;d->sync_native_discontinuous=d->sync_native_gap_hold=0;
  }
  struct xz_sync_native_snapshot native={0};
  int32_t bpm=d->sync_legacy_bpm;
  if(p->native_sync_clock){
   int copied=p->native_sync_clock(&native);
   if(!copied&&native.reason==XZ_SYNC_NATIVE_TIME_INVALID&&
      !(native.evidence&XZ_SYNC_NATIVE_BOUNDARY_OBSERVED)&&(int32_t)(now-native.captured_ms)>150&&
      d->sync_rate_active&&d->tempo_native_identity&&d->tempo_native_deck==native.native_deck&&
      d->tempo_native_identity==native.track_identity&&d->tempo_native_revision==native.source_revision){
    /* Older cache adapters report phase expiry as unavailable. No new tempo
       is accepted from them; reuse only this exact source's applied tempo. */
    copied=native.available=1;native.phase_available=0;native.bpm=(float)d->tempo_native_bpm;
   }
   if(native.reason==XZ_SYNC_NATIVE_EXTERNAL_MASTER){d->sync.reason=XZ_EXTRA_SYNC_NATIVE_MASTER_UNSUPPORTED;d->sync.master_scope=0;return;}
   if(!copied||!native.available||native.native_deck<0||native.native_deck>1||
      (p->master_observer&&native.native_deck!=d->master_actual.deck)||
      !native.source_revision||!native.track_identity||!isfinite(native.bpm)||native.bpm<=0||native.bpm>(double)INT32_MAX/100.||
      (int32_t)(now-native.captured_ms)<0){
    if(!p->master_observer&&native.reason==XZ_SYNC_NATIVE_MASTER_UNAVAILABLE&&native.master_id==-1)d->sync.source=XZ_EXTRA_SYNC_SOURCE_NONE;
    d->sync.reason=XZ_EXTRA_SYNC_WAIT_MASTER;return;
   }
   int fresh=(uint32_t)(now-native.captured_ms)<=150u;
   int retained=d->sync_rate_active&&d->tempo_native_deck==native.native_deck&&
    d->tempo_native_identity==native.track_identity&&d->tempo_native_revision==native.source_revision;
   if(!fresh&&!retained){d->sync.reason=XZ_EXTRA_SYNC_WAIT_MASTER;return;}
   if(!fresh)native.bpm=(float)d->tempo_native_bpm;
   if(!fresh)native.phase_available=0;
   if(d->sync_native_identity!=native.track_identity||d->sync_native_source_revision!=native.source_revision||
      d->sync_native_deck!=native.native_deck){
    sync_reacquire(d,now);d->native_phase.have=d->native_phase.ready=0;
    d->sync_have_master=d->sync_lock_count=0;
    d->sync_native_captured_us=0;d->sync_native_discontinuous=d->sync_native_gap_hold=0;
    d->sync_native_identity=native.track_identity;d->sync_native_source_revision=native.source_revision;
    d->sync_native_deck=native.native_deck;
   }
   bpm=(int32_t)llround(native.bpm*100);d->sync.master_deck=native.native_deck;d->sync.master_scope=XZ_EXTRA_MASTER_SCOPE_NATIVE;
  }
  if(bpm<=0){if(!p->native_sync_clock&&!p->master_observer)d->sync.source=XZ_EXTRA_SYNC_SOURCE_NONE;d->sync.reason=XZ_EXTRA_SYNC_WAIT_MASTER;return;}
  d->sync.source=XZ_EXTRA_SYNC_SOURCE_LIVE;d->sync.master_paused=p->native_sync_clock&&!native.playing;
  double base=native_tempo_rate(d,p->native_sync_clock?native.bpm:(double)bpm/100.);
  if(base<=0){d->sync.reason=XZ_EXTRA_SYNC_NO_SOURCE_BPM;return;}
  int tempo_applied=sync_apply_base(p,d,selected,&choice,base);
  if(tempo_applied<=0){d->sync.reason=tempo_applied<0?XZ_EXTRA_SYNC_ACQUIRING:XZ_EXTRA_SYNC_OUT_OF_RANGE;
   if(tempo_applied<0)d->sync.source=XZ_EXTRA_SYNC_SOURCE_CLOCK_LOST;
   return;}
  if(p->native_sync_clock)d->tempo_native_bpm=native.bpm;
  if(native.manual||d->state==XZ_XP_SCRATCHING||bend_now(d,now)!=0){
   if(native.manual)d->native_phase.have=d->native_phase.ready=0;
   int scratch=d->state==XZ_XP_SCRATCHING,platter=native.manual&&!native.playing;
   if(d->quantize&&scratch)d->sync_initial=SYNC_INITIAL_EXPLICIT;
   /* A side nudge on this deck or on the master (playing, manual) is an alignment offset on stock (S4,
      S8): once it ends the measured offset is kept, not trimmed away or re-snapped. A hold on the native
      master's platter top (VINYL: reads manual, not playing) stops its beat, and this deck's own
      platter-top scratch moves its position: after either, this deck finds the beat again. */
   if(scratch||platter)d->sync_offset_adopt=0;else if(d->sync_have_master)d->sync_offset_adopt=1;
   d->sync_hold=1;
   d->sync_native_gap_hold=d->sync_have_master&&!platter?SYNC_NATIVE_GAP_WAIT_PHASE:SYNC_NATIVE_GAP_NONE;
   d->sync_hold_reason=XZ_EXTRA_SYNC_DISCONTINUITY;d->sync.reason=XZ_EXTRA_SYNC_MANUAL;return;
  }
  if(p->native_sync_clock&&(!native.playing||d->state==XZ_XP_CUED||d->state==XZ_XP_ENDED)){
   int phase_known=native.phase_available&&isfinite(native.bar_phase)&&native.bar_phase>=0&&
    native.bar_phase<4&&native.uncertainty_us<=XZ_SYNC_NATIVE_MAX_PHASE_UNCERTAINTY_US;
   double phase=native.bar_phase;
   if(native.playing)phase+=((double)(uint32_t)(now-native.captured_ms)/1000.-native.output_latency_us/1000000.)*native.bpm/60.;
   sync_stationary(p,d,selected,choice,native.bpm,wrapped_beat(phase),phase_known,now);return;
  }
  if(d->sync_stationary){d->sync_stationary=0;sync_reacquire(d,now);
   if(d->sync_initial!=SYNC_INITIAL_EXPLICIT)d->sync_initial=d->quantize;}
  if(d->quantize&&!grid){d->sync.reason=XZ_EXTRA_SYNC_NO_GRID;return;}
  if(!position){d->sync.reason=XZ_EXTRA_SYNC_TEMPO_FOLLOW;return;}
  if(p->native_sync_clock&&native.phase_available&&d->quantize&&!native_phase_ready(d,&native)){
   if(d->sync_have_master){d->sync_hold=1;d->sync_hold_reason=XZ_EXTRA_SYNC_DISCONTINUITY;}
   d->sync.reason=XZ_EXTRA_SYNC_TEMPO_FOLLOW;return;
  }
  if(d->sync_hold){
   int recovered=sync_native_gap_tempo(p,d,&native,selected,choice,now);
   if(recovered==SYNC_NATIVE_GAP_STAY)return;
   if(recovered!=SYNC_NATIVE_GAP_RELEASE){
    sync_reacquire(d,now);
    d->sync_native_identity=native.track_identity;d->sync_native_source_revision=native.source_revision;
   }
  }
  if(!native.phase_available||!d->quantize){
   d->native_phase.have=d->native_phase.ready=0;d->sync_have_master=0;
   d->sync_native_bpm=native.bpm;
   d->sync.reason=d->quantize?XZ_EXTRA_SYNC_NATIVE_PHASE_UNAVAILABLE:XZ_EXTRA_SYNC_TEMPO_FOLLOW;return;
  }
  struct sync_clock slave=sync_clock(d,now);
  if(!native.playing){
   if(d->sync_have_master)d->sync_native_discontinuous=1;
   d->sync_native_gap_hold=0;
   d->sync.reason=XZ_EXTRA_SYNC_WAIT_MASTER;return;
  }
  uint64_t cur_us=native.captured_us?native.captured_us:(uint64_t)native.captured_ms*1000u;
  if(d->sync_have_master&&d->sync_native_captured_us){
   int gap=!d->sync_native_discontinuous&&cur_us>=d->sync_native_captured_us&&
    cur_us-d->sync_native_captured_us>150000u;
   if(d->sync_native_discontinuous||cur_us<d->sync_native_captured_us||gap){
    d->sync_native_gap_hold=gap?SYNC_NATIVE_GAP_WAIT_PHASE:SYNC_NATIVE_GAP_NONE;
    d->sync_hold=1;d->sync_lock_count=0;
    d->sync_hold_reason=d->sync.reason=XZ_EXTRA_SYNC_DISCONTINUITY;
    if(gap)(void)sync_native_gap_tempo(p,d,&native,selected,choice,now);
    return;
   }
   double dt_obs=(double)(cur_us-d->sync_native_captured_us)/1000000.;
   double min_step=dt_obs*(fmin(d->sync_native_bpm,(double)native.bpm)/60.);
   double max_step=dt_obs*(fmax(d->sync_native_bpm,(double)native.bpm)/60.);
   double mid_step=.5*(min_step+max_step),tol=.5*(max_step-min_step)+.06;
   double phase_jump=wrapped_beat((double)native.bar_phase-(d->sync_native_bar_phase+mid_step)+.5)-.5;
   if(fabs(phase_jump)>tol){
    d->sync_native_gap_hold=0;d->sync_hold=1;d->sync_hold_reason=d->sync.reason=XZ_EXTRA_SYNC_DISCONTINUITY;return;
   }
  }
  d->sync_native_captured_us=cur_us;d->sync_native_bar_phase=native.bar_phase;d->sync_native_bpm=native.bpm;
  if(!slave.period_frames){
   d->sync.reason=d->track.track_bpm_x100<=0?XZ_EXTRA_SYNC_NO_SOURCE_BPM:XZ_EXTRA_SYNC_NO_GRID;return;
  }
  if(!slave.loop_aligned){d->sync.reason=XZ_EXTRA_SYNC_LOOP_UNALIGNED;return;}
  if(!isfinite(native.bar_phase)||native.bar_phase<0.f||native.bar_phase>=4.f||
     native.uncertainty_us>XZ_SYNC_NATIVE_MAX_PHASE_UNCERTAINTY_US){
   d->sync.reason=XZ_EXTRA_SYNC_NATIVE_PHASE_UNAVAILABLE;return;
  }
  double master_bps=(double)native.bpm/60.;
  if(native.manual||d->state!=XZ_XP_PLAYING||slave.manual){d->sync.reason=XZ_EXTRA_SYNC_MANUAL;return;}
  if(!slave.available){d->sync.reason=XZ_EXTRA_SYNC_WAIT_ANCHOR;return;}
  if(d->sync_have_master&&(d->epoch!=d->sync_master_clock.route||d->load_revision!=d->sync_master_clock.load||
     d->sync_choice_seen!=choice||d->underrun!=d->sync_slave_underrun)){
   d->sync_native_gap_hold=0;d->sync_hold=1;d->sync_hold_reason=d->sync.reason=XZ_EXTRA_SYNC_DISCONTINUITY;return;
  }
  int64_t sub_ms_us=native.captured_us?(int64_t)(native.captured_us-(uint64_t)native.captured_ms*1000u):0;
  if(sub_ms_us<-1000||sub_ms_us>1000)sub_ms_us=0;
  int64_t dt_us=(int64_t)(int32_t)(slave.at_ms-native.captured_ms)*1000-sub_ms_us-(int64_t)native.output_latency_us;
  double block_age_sec=d->timeline_wire_completed>=slave.wire_frame
   ?(double)(d->timeline_wire_completed-slave.wire_frame)/(double)XZ_USB_RATE:0.;
  double dt_sec=(double)dt_us/1000000.-block_age_sec;
  /* Time this deck's completed block from when the transport published it, not from when this
     worker happened to look: a pass of observation lag would otherwise reach the phase trim as
     noise (tempo flutter, a lock that comes and goes). */
  double block_ms;
  if(wire_time_ms(p,d->epoch,slave.wire_frame,&block_ms)){
   double est=(block_ms-(double)native.captured_ms)/1000.-((double)sub_ms_us+(double)native.output_latency_us)/1000000.;
   if(fabs(est-dt_sec)<.05)dt_sec=est;
  }
  if(fabs(dt_sec)>.25){d->sync.reason=XZ_EXTRA_SYNC_WAIT_ANCHOR;return;}
  double target_phase=wrapped_beat((double)native.bar_phase+dt_sec*master_bps);
  double error=quantized_delta(slave.phase-target_phase,grid);
  if(d->sync_offset_adopt)sync_offset_take(d,error,error/master_bps);
  /* The trim holds the kept offset (zero unless a nudge made one), never closes it (S4, S7). */
  error=quantized_delta(error-d->sync_offset,grid);
  double error_seconds=error/master_bps;
  d->sync.phase_error_us=(int32_t)llround(error_seconds*1000000.);
  d->sync_master_clock=(struct sync_clock){.route=d->epoch,.load=d->load_revision,.generation=d->generation};
  d->sync_have_master=1;d->sync_master_id=native.native_deck;d->sync_slave_underrun=d->underrun;d->sync_choice_seen=choice;
  if(sync_snap_ready(d,now)){
   uint64_t wire=position->wire_submitted_frames>slave.wire_frame?position->wire_submitted_frames:slave.wire_frame;
   double horizon_sec=(double)(wire-slave.wire_frame)/(double)XZ_USB_RATE;
   double desired=wrapped_beat(target_phase+horizon_sec*master_bps);
   double delta=quantized_delta(slave.phase-desired,grid);
   double to=(double)d->heard-delta*slave.period_frames;
   if(d->loop.active){
    double span=(double)(d->loop.out-d->loop.in),relative=fmod(to-d->loop.in,span);
    if(relative<0)relative+=span;
    to=d->loop.in+relative;
   }else{
    if(to<0)to+=slave.period_frames;
    if(to>=d->r->frames)to-=slave.period_frames;
    if(to<0||to>=d->r->frames){d->sync.reason=XZ_EXTRA_SYNC_WAIT_ANCHOR;return;}
   }
   uint64_t target=(uint64_t)llround(to);
   if(d->loop.active)target=loop_frame(d,target);
   else if(target>=d->r->frames)target=d->r->frames-1;
   int current=authority_before(p,d);
   pthread_mutex_lock(&p->publish_lock);
   current=current&&authority_locked(p,d,selected,choice);
   if(current){d->sync_initial=0;d->sync_lock_count=d->sync_have_master=0;jump(d,target,now);d->sync_snap_at=now|1u;sync_offset_clear(d);}
   pthread_mutex_unlock(&p->publish_lock);
   if(!current){d->sync.reason=XZ_EXTRA_SYNC_WAIT_MASTER;return;}
   d->sync.reason=XZ_EXTRA_SYNC_ACQUIRING;return;
  }
  double trim=fabs(error_seconds)<=2./XZ_USB_RATE?0:-error_seconds*d->sync_rate/.25;
  double limit=.01*d->sync_rate;
  if(trim>limit)trim=limit;
  if(trim< -limit)trim=-limit;
  int current=authority_before(p,d);
  pthread_mutex_lock(&p->publish_lock);
  current=current&&authority_locked(p,d,selected,choice);
  if(current)sync_set_trim(d,trim);
  pthread_mutex_unlock(&p->publish_lock);
  if(!current){d->sync.reason=XZ_EXTRA_SYNC_WAIT_MASTER;return;}
  if(slave.wire_frame!=d->sync_last_wire){
   d->sync_lock_count=fabs(error_seconds)<=.001?(d->sync_lock_count<3?d->sync_lock_count+1:3):0;d->sync_last_wire=slave.wire_frame;
  }
  d->sync.locked=d->sync_lock_count>=3;
  d->sync.reason=d->sync.locked?XZ_EXTRA_SYNC_LOCKED:XZ_EXTRA_SYNC_ACQUIRING;
  return;
 }
 d->sync.master_deck=selected+XZ_EXTRA_FIRST_DECK;
 d->sync.master_scope=XZ_EXTRA_MASTER_SCOPE_SOFTWARE;
 if(d->sync_hold){
  /* A side nudge (here or on the master) keeps the alignment it left as an offset (S4, S8). */
  if(d->sync_offset_adopt&&d->sync_hold_reason==XZ_EXTRA_SYNC_MANUAL){d->sync_hold=0;d->sync_hold_reason=XZ_EXTRA_SYNC_OFF;}
  else sync_reacquire(d,now);
 }
 struct sync_clock master;
 pthread_mutex_lock(&p->publish_lock);master=p->clock[selected];pthread_mutex_unlock(&p->publish_lock);
 struct sync_clock slave=sync_clock(d,now);
 if(!master.loaded){d->sync.reason=XZ_EXTRA_SYNC_WAIT_MASTER;return;}
 d->sync.master_paused=!master.playing;
 d->sync.source=XZ_EXTRA_SYNC_SOURCE_LIVE;
 double bpm=master.tempo_bps*60.,rate=native_tempo_rate(d,bpm);
 if(bpm<=0||rate<=0){d->sync.reason=XZ_EXTRA_SYNC_NO_SOURCE_BPM;return;}
 int applied=sync_apply_base(p,d,selected,&choice,rate);
 if(applied<=0){d->sync.reason=applied<0?XZ_EXTRA_SYNC_ACQUIRING:XZ_EXTRA_SYNC_OUT_OF_RANGE;
  if(applied<0)d->sync.source=XZ_EXTRA_SYNC_SOURCE_CLOCK_LOST;
  return;}
 if(master.manual||slave.manual){
  /* A platter-top scratch (here or on the master) moves the beat: find it again after. A side nudge
     becomes a kept offset once it ends (S4, S8). */
  int scratch=d->state==XZ_XP_SCRATCHING||(master.manual&&!master.bending);
  if(d->state==XZ_XP_SCRATCHING&&d->quantize)d->sync_initial=SYNC_INITIAL_EXPLICIT;
  if(scratch)d->sync_offset_adopt=0;else if(d->sync_have_master)d->sync_offset_adopt=1;
  d->sync_hold=1;d->sync_hold_reason=XZ_EXTRA_SYNC_MANUAL;d->sync.reason=XZ_EXTRA_SYNC_MANUAL;return;
 }
 if(d->quantize&&!grid){d->sync.reason=XZ_EXTRA_SYNC_NO_GRID;return;}
 if(!master.playing||d->state==XZ_XP_CUED||d->state==XZ_XP_ENDED){
  int phase_known=master.period_frames>0&&(!master.playing||master.available)&&
   (uint32_t)(now-master.at_ms)<=150u;
  sync_stationary(p,d,selected,choice,bpm,master.phase,phase_known,now);return;
 }
 if(d->sync_stationary){d->sync_stationary=0;sync_reacquire(d,now);
   if(d->sync_initial!=SYNC_INITIAL_EXPLICIT)d->sync_initial=d->quantize;}
 if(!d->quantize||!position){d->sync.reason=XZ_EXTRA_SYNC_TEMPO_FOLLOW;return;}
 if(!master.period_frames){d->sync.reason=XZ_EXTRA_SYNC_NO_GRID;return;}
 if(!master.available||(uint32_t)(now-master.at_ms)>150u){d->sync.reason=XZ_EXTRA_SYNC_WAIT_ANCHOR;return;}
 if(!slave.period_frames){d->sync.reason=XZ_EXTRA_SYNC_NO_GRID;return;}
 if(!master.loop_aligned||!slave.loop_aligned){d->sync.reason=XZ_EXTRA_SYNC_LOOP_UNALIGNED;return;}
 if(!d->quantize){d->sync.reason=XZ_EXTRA_SYNC_TEMPO_FOLLOW;return;}
 if(master.manual||d->state!=XZ_XP_PLAYING||slave.manual){d->sync.reason=XZ_EXTRA_SYNC_MANUAL;return;}
 if(!slave.available){d->sync.reason=XZ_EXTRA_SYNC_WAIT_ANCHOR;return;}
 if(d->sync_have_master&&(master.route!=d->sync_master_clock.route||master.load!=d->sync_master_clock.load||
    master.generation!=d->sync_master_clock.generation||d->sync_master_id!=selected||d->sync_choice_seen!=choice||
    master.underrun!=d->sync_master_clock.underrun||d->underrun!=d->sync_slave_underrun)){
  d->sync_hold=1;d->sync_hold_reason=d->sync.reason=XZ_EXTRA_SYNC_DISCONTINUITY;return;
 }
 double wire_delta=(double)slave.wire_frame-(double)master.wire_frame;
 if(fabs(wire_delta)>XZ_USB_RATE*.15){d->sync.reason=XZ_EXTRA_SYNC_WAIT_ANCHOR;return;}
 double target_phase,master_step;
 if(!segment_at_wire(&master,slave.wire_frame,&target_phase,&master_step,NULL,NULL)){
  d->sync.reason=XZ_EXTRA_SYNC_WAIT_ANCHOR;return;
 }
 double error=quantized_delta(slave.phase-target_phase,grid);
 if(d->sync_offset_adopt)sync_offset_take(d,error,error/(master_step*XZ_USB_RATE));
 error=quantized_delta(error-d->sync_offset,grid);
 double error_seconds=error/(master_step*XZ_USB_RATE);
 d->sync.phase_error_us=(int32_t)llround(error_seconds*1000000.);
 d->sync_master_clock=master;d->sync_have_master=1;d->sync_master_id=selected;d->sync_slave_underrun=d->underrun;d->sync_choice_seen=choice;
 if(sync_snap_ready(d,now)){
  uint64_t wire=position->wire_submitted_frames?position->wire_submitted_frames:slave.wire_frame;
  double desired,forecast_step;
  if(!segment_at_wire(&master,wire,&desired,&forecast_step,NULL,NULL)){
   d->sync.reason=XZ_EXTRA_SYNC_WAIT_ANCHOR;return;
  }
  double delta=quantized_delta(slave.phase-desired,grid);
  double to=(double)d->heard-delta*slave.period_frames;
  if(d->loop.active){
   double span=(double)(d->loop.out-d->loop.in),relative=fmod(to-d->loop.in,span);
   if(relative<0)relative+=span;
   to=d->loop.in+relative; /* integer-beat loops preserve phase without seeking outside the loop */
  }else{
   if(to<0)to+=slave.period_frames;
   if(to>=d->r->frames)to-=slave.period_frames;
   if(to<0||to>=d->r->frames){d->sync.reason=XZ_EXTRA_SYNC_WAIT_ANCHOR;return;}
  }
  uint64_t target=(uint64_t)llround(to);
  if(d->loop.active)target=loop_frame(d,target);
  else if(target>=d->r->frames)target=d->r->frames-1;
  int current=authority_before(p,d);
  pthread_mutex_lock(&p->publish_lock);
  current=current&&authority_locked(p,d,selected,choice);
  if(current){d->sync_initial=0;d->sync_lock_count=d->sync_have_master=0;jump(d,target,now);d->sync_snap_at=now|1u;sync_offset_clear(d);}
  pthread_mutex_unlock(&p->publish_lock);
  if(!current){d->sync.reason=XZ_EXTRA_SYNC_WAIT_MASTER;return;}
  d->sync.reason=XZ_EXTRA_SYNC_ACQUIRING;return;
 }
 /* Bounded 250 ms phase trim; delayed/noisy completion acceptance measures its settling. */
 double trim=fabs(error_seconds)<=2./XZ_USB_RATE?0:-error_seconds*d->sync_rate/.25;
 double limit=.01*d->sync_rate;
 if(trim>limit)trim=limit;
 if(trim< -limit)trim=-limit;
 int current=authority_before(p,d);
 pthread_mutex_lock(&p->publish_lock);
 current=current&&authority_locked(p,d,selected,choice);
 if(current)sync_set_trim(d,trim);
 pthread_mutex_unlock(&p->publish_lock);
 if(!current){d->sync.reason=XZ_EXTRA_SYNC_WAIT_MASTER;return;}
 if(slave.wire_frame!=d->sync_last_wire){
  d->sync_lock_count=fabs(error_seconds)<=.001?(d->sync_lock_count<3?d->sync_lock_count+1:3):0;d->sync_last_wire=slave.wire_frame;
 }
 d->sync.locked=d->sync_lock_count>=3;
 d->sync.reason=d->sync.locked?XZ_EXTRA_SYNC_LOCKED:XZ_EXTRA_SYNC_ACQUIRING;
}
static void slip_begin(struct deck *d, uint64_t at, uint32_t now);
static void slip_return(struct deck *d, uint32_t now);
static double beat_frames(const struct deck *d, uint64_t at);
static void set_tempo(struct deck *d) {
 d->sync_rate_active=0;
 int32_t step = d->tempo_range <= 6 ? 2 : d->tempo_range <= 16 ? 5 : 50;
 int64_t raw = (int64_t)d->tempo_position * d->tempo_range * 100;
 int64_t steps = (raw >= 0 ? raw + (int64_t)step * XZ_EXTRA_TEMPO_SPAN / 2 : raw - (int64_t)step * XZ_EXTRA_TEMPO_SPAN / 2) /
                 ((int64_t)step * XZ_EXTRA_TEMPO_SPAN);
 d->tempo_x100 = d->tempo_reset ? 0 : (int32_t)(steps * step);
}
/* A loop called from a hot cue pad keeps that pad's loop in step with edits (p.53). */
static void loop_pad_store(struct xz_xp_players *p,struct deck *d) {
 if (d->loop_pad < 0 || d->loop_pad >= XZ_EXTRA_HOT_CUES) return;
 struct hot *h = &d->hot[d->loop_pad];
 if (!h->set || !h->loop || !d->loop.valid) { d->loop_pad = -1; return; }
 if(h->at==d->loop.in&&h->end==d->loop.out)return;
 h->at = d->loop.in; h->end = d->loop.out;d->saved_hot[d->loop_pad]=*h;
 struct xz_cue_edit edit={.kind=XZ_CUE_EDIT_HOT,.pad=(unsigned)d->loop_pad,.point=saved_hot(d,h)};cue_save(p,d,&edit);
}
/* CUE/LOOP CALL outside loop playback (p.51): the stored point before (back) or after (forward) the
   position; the track jumps there and pauses. A memory loop is set, so PLAY starts loop playback. */
static void memory_call(struct xz_xp_players *p, struct deck *d, int forward, uint32_t now) {
 if (!d->memory_count) { xz_xp_notice(p, "NO STORED POINTS"); return; }
 uint64_t at = heard_now(d, now), slack = d->r->rate / 100u;
 int pick = -1;
 for (int i = 0; i < d->memory_count; ++i) {
  uint64_t m = d->memory[i].at;
  if (forward ? m > at + slack && (pick < 0 || m < d->memory[pick].at)
              : m + slack < at && (pick < 0 || m > d->memory[pick].at)) pick = i;
 }
 if (pick < 0) { xz_xp_notice(p, forward ? "NO LATER POINT" : "NO EARLIER POINT"); return; }
 const struct memory_point *m = &d->memory[pick];
 if(d->slip_reverse)d->reverse=0;d->slip_reverse=0;d->slip_bg.active=0;d->slip_pad=-1;d->pad_hold=PAD_HOLD_NONE;d->search=0;
 d->loop.edit = 0; d->active_pad = d->loop_pad = -1;
 if (m->loop) { d->loop.in = m->at; d->loop.out = m->end; d->loop.valid = d->loop.active = d->loop.has_in = 1; }
 else d->loop.active = 0;
 d->cue = m->at;
 jump(d, m->at, now);
 d->state = XZ_XP_CUED;
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
  int stay = !d->loop.has_in;   /* from the press: already inside the loop, keep playing */
  d->loop.in = d->loop.has_in ? d->loop.in : quantize_floor(d, at);
  uint64_t length = (uint64_t)llround(4.0 * beat_frames(d, d->loop.in));
  d->loop.out = length > d->r->frames - d->loop.in ? d->r->frames : d->loop.in + length;
  d->loop.valid = d->loop.out > d->loop.in;
  d->loop.active = d->loop.valid; d->loop.edit = 0; d->loop.has_in = 1;
  if (d->loop.active) {
   if (d->slip) slip_begin(d, at, now);
   if (!stay) { jump(d, d->loop.in, now); start(d, now); }
   xz_xp_notice(p,"4 BEAT LOOP");
  }
  return;
 }
 if (e->kind == XZ_EXTRA_IN_LOOP_IN) {
  if (d->loop.active && e->shift) { jump(d, d->loop.in, now); start(d, now); return; }
  if (d->loop.active) {
   d->loop.edit = d->loop.edit == 1 ? 0 : 1;
   d->loop.anchor = d->loop.in; d->loop.edit_at = now;
   xz_xp_notice(p,d->loop.edit?"LOOP IN ADJUST":"LOOP ADJUST DONE");
  } else {
   d->loop.in = quantize_point(d, at); d->loop.has_in = 1; d->loop.valid = 0; d->cue = d->loop.in;
   xz_xp_notice(p,"LOOP IN SET");
  }
 } else if (e->kind == XZ_EXTRA_IN_LOOP_OUT) {
  if (d->loop.active) {
   d->loop.edit = d->loop.edit == 2 ? 0 : 2; d->loop.edit_at = now;
   if(!d->loop.edit)loop_pad_store(p,d);
   xz_xp_notice(p,d->loop.edit?"LOOP OUT ADJUST":"LOOP ADJUST DONE");return;
  }
  d->loop.in = d->cue;   /* p.47: LOOP IN is optional when the cue point is the IN point */
  uint64_t out = quantize_point(d, at);
  if (out <= d->loop.in) { xz_xp_notice(p, "LOOP OUT MUST FOLLOW LOOP IN"); return; }
  if (d->slip && out - d->loop.in > 8u * (uint64_t)d->r->rate) { xz_xp_notice(p, "LOOP OVER 8 SECONDS"); return; }
  d->loop.out = out; d->loop.valid = d->loop.active = d->loop.has_in = 1; d->loop.edit = 0;
  if (d->slip) slip_begin(d, at, now);
  jump(d, d->loop.in, now); start(d, now);
  xz_xp_notice(p,"LOOP ON");
 } else if (e->kind == XZ_EXTRA_IN_RELOOP) {
  d->loop.edit = 0;
  if (d->loop.active) {
   d->loop.active = 0; d->active_pad = -1;
   if (d->slip_bg.active) slip_return(d, now); else jump(d, at, now);
   xz_xp_notice(p,"LOOP EXIT");
  } else if (d->loop.valid) {
   d->loop.active = 1; jump(d, d->loop.in, now); start(d, now); xz_xp_notice(p,"RELOOP");
  } else xz_xp_notice(p, "SET A LOOP FIRST");
 } else if (!d->loop.active || !streaming(d->state)) {
  memory_call(p, d, e->kind == XZ_EXTRA_IN_LOOP_DOUBLE, now);
  return;
 } else if (d->loop.active) {
  uint64_t length = d->loop.out - d->loop.in;
  if (e->kind == XZ_EXTRA_IN_LOOP_HALF) length = length > 1 ? length / 2 : 1;
  else if (length <= (d->r->frames - d->loop.in) / 2) length *= 2;
  else length = d->r->frames - d->loop.in;
  d->loop.out = d->loop.in + length;
  d->loop.edit = 0; jump(d, loop_frame(d, at), now);
  loop_pad_store(p,d);
  xz_xp_notice(p,e->kind==XZ_EXTRA_IN_LOOP_HALF?"LOOP HALF":"LOOP DOUBLE");
 }
 if (d->loop.active) loop_pad_store(p,d); else d->loop_pad = -1;
}

/* ---- pad modes, DIRECTION REV, SLIP and VINYL SPEED ADJUST (manual pp.42-62) ---- */
/* One beat at a source frame: the beat grid's local period, else the track BPM, else 120 BPM (p.56). */
static double beat_frames(const struct deck *d, uint64_t at) {
 double phase, period;
 if (grid_phase(d, at, &phase, &period) && period > 0) return period;
 uint32_t bpm = d->track.track_bpm_x100 > 0 ? (uint32_t)d->track.track_bpm_x100 : 12000u;
 return (double)d->r->rate * 6000.0 / bpm;
}
/* p.45: counterclockwise is faster, clockwise slower. Exponential between the two ends, so the knob
   centre (level 0) is their geometric mean. */
static uint32_t vinyl_ramp_ms(int32_t level) {
 double t = ((double)level + XZ_EXTRA_TEMPO_SPAN) / (2.0 * XZ_EXTRA_TEMPO_SPAN);
 if (t < 0) t = 0;
 if (t > 1) t = 1;
 return (uint32_t)lround(XZ_XP_RAMP_FAST_MS * pow((double)XZ_XP_RAMP_SLOW_MS / XZ_XP_RAMP_FAST_MS, t));
}
/* Speed multiplier of a running vinyl ramp: 1 at full speed, 0 stopped. */
static double ramp_factor(const struct deck *d, uint32_t now) {
 if (!d->ramp.active) return 1.0;
 double moved = d->ramp.ms ? (double)(uint32_t)(now - d->ramp.at) / d->ramp.ms : 1.0;
 double f = d->ramp.stopping ? d->ramp.from - moved : d->ramp.from + moved;
 return f < 0 ? 0 : f > 1 ? 1 : f;
}
static void ramp_from(struct deck *d, int stopping, double from, uint32_t now) {
 d->ramp.active = 1; d->ramp.stopping = stopping; d->ramp.from = from; d->ramp.at = now;
 d->ramp.ms = vinyl_ramp_ms(d->vinyl_level);
}
/* Once per worker pass: a finished brake pauses where the platter stopped; a finished start returns to
   ordinary (Master Tempo) rendering. */
static void ramp_step(struct deck *d, uint32_t now) {
 if (!d->ramp.active) return;
 if (d->state != XZ_XP_PLAYING) { d->ramp.active = 0; xz_stretch_reset(&d->stretch); return; }
 double f = ramp_factor(d, now);
 if (d->ramp.stopping && f <= 0) pause_at_heard(d, now);
 else if (!d->ramp.stopping && f >= 1) { d->ramp.active = 0; xz_stretch_reset(&d->stretch); }
}
/* SLIP (pp.58-62): from at, playback continues silently at the playing speed and direction. */
static void slip_begin(struct deck *d, uint64_t at, uint32_t now) {
 if (d->slip_bg.active || d->state != XZ_XP_PLAYING) return;
 double rate = (d->sync_rate_active ? d->sync_rate : nominal(d)) * (d->reverse ? -1.0 : 1.0);
 d->slip_bg.active = 1; d->slip_bg.anchor = at; d->slip_bg.at = now; d->slip_bg.rate = rate;
 d->slip_bg.reverse_ms = 0;
}
static void slip_return(struct deck *d, uint32_t now) {
 if (!d->slip_bg.active) return;
 d->slip_bg.active = 0; d->slip_pad = -1; d->pad_hold=PAD_HOLD_NONE;
 double at = (double)d->slip_bg.anchor + (double)(uint32_t)(now - d->slip_bg.at) * d->slip_bg.rate * d->r->rate / 1000.0;
 if (at < 0) at = 0;
 if (at > (double)d->r->frames) at = (double)d->r->frames;
 jump(d, (uint64_t)at, now);
}
static void loop_off(struct deck *d, uint32_t now) {
 uint64_t at = heard_now(d, now);
 d->loop.active = 0; d->loop.edit = 0; d->active_pad = -1;
 d->pad_hold=PAD_HOLD_NONE;d->slip_pad=-1;
 if (d->slip_bg.active) slip_return(d, now); else jump(d, at, now);
}
static const double beat_loop_beats[XZ_EXTRA_HOT_CUES] = {0.25, 0.5, 1, 2, 4, 8, 16, 32};               /* p.56 */
static const double slip_loop_beats[XZ_EXTRA_HOT_CUES] = {1.0/16, 1.0/8, 0.25, 0.5, 1, 2, 1.0/3, 0.75}; /* p.62 */
/* BEAT LOOP: a press loops the pad's length from where it was pressed; the running loop's pad exits it;
   another pad resizes it from the same IN. SLIP LOOP: the loop lasts while the pad is held, with playback
   continuing in the background. With SLIP, loops over eight seconds are refused (pp.61-62). */
static void pad_loop(struct xz_xp_players *p, struct deck *d, int pad, int pressed, int slip_loop, uint32_t now) {
 if (!pressed) { if (slip_loop && d->active_pad == pad) loop_off(d, now); return; }
 if (!slip_loop && d->loop.active && d->active_pad == pad) { loop_off(d, now); xz_xp_notice(p, "LOOP EXIT"); return; }
 uint64_t at = heard_now(d, now);
 uint64_t in = d->loop.active && d->active_pad >= 0 ? d->loop.in : quantize_floor(d, at);
 double length = (slip_loop ? slip_loop_beats : beat_loop_beats)[pad] * beat_frames(d, in);
 int slip = slip_loop || d->slip;
 if (slip && length > 8.0 * d->r->rate) { xz_xp_notice(p, "LOOP OVER 8 SECONDS"); return; }
 uint64_t out = in + (uint64_t)llround(length < 1 ? 1 : length);
 if (out > d->r->frames) out = d->r->frames;
 if (out <= in) return;
 d->search=0;
 if(d->slip_reverse){d->slip_reverse=0;d->reverse=0;}
 d->slip_pad=-1;d->pad_hold=PAD_HOLD_NONE;
 if (slip) slip_begin(d, at, now);
 if (slip_loop) { d->slip_pad = pad; d->pad_hold=PAD_HOLD_LOOP; }
 d->loop.in = in; d->loop.out = out; d->loop.valid = d->loop.active = d->loop.has_in = 1; d->loop.edit = 0;
 d->active_pad = pad;
 if (!streaming(d->state)) jump(d, in, now);
}
/* Walk beat coordinates, preserving fractional phase and the edge interval outside the grid. */
static int jump_target(const struct deck *d,uint64_t at,int beats,double *target){
 const struct analysis *a=d->analysis;
 if(a&&a->grid_count>=2){
  double ms=(double)at*1000/d->r->rate;unsigned lo=0,hi=a->grid_count;
  while(lo<hi){unsigned mid=lo+(hi-lo)/2;if(a->grid[mid].ms<=ms)lo=mid+1;else hi=mid;}
  unsigned index=lo?lo-1:0;
  unsigned edge=index+1<a->grid_count?index:index-1;
  double length=(double)a->grid[edge+1].ms-a->grid[edge].ms;
  if(length<=0)return 0;
  double coordinate=index+(ms-a->grid[index].ms)/length+beats;
  double base=floor(coordinate),fraction=coordinate-base,result;
  if(base<0)result=a->grid[0].ms+coordinate*(a->grid[1].ms-a->grid[0].ms);
  else if(base>=a->grid_count-1)result=a->grid[a->grid_count-1].ms+
   (coordinate-(a->grid_count-1))*(a->grid[a->grid_count-1].ms-a->grid[a->grid_count-2].ms);
  else{unsigned i=(unsigned)base;result=a->grid[i].ms+fraction*(a->grid[i+1].ms-a->grid[i].ms);}
  *target=result*d->r->rate/1000;return isfinite(*target);
 }
 /* Manual p.56: without a detected BPM, BEAT JUMP uses 120 BPM. */
 int32_t bpm=d->track.track_bpm_x100>0?d->track.track_bpm_x100:12000;
 *target=(double)at+beats*(double)d->r->rate*6000/bpm;return isfinite(*target);
}
static void pad_jump_beats(struct xz_xp_players *p,struct deck *d,int beats,uint32_t now){
 uint64_t at=heard_now(d,now);double to;
 if(!jump_target(d,at,beats,&to)){xz_xp_notice(p,"JUMP UNAVAILABLE: NO BEAT GRID OR BPM");return;}
 double delta=to-(double)at;
 if(d->loop.active){
  double in=(double)d->loop.in+delta,out=(double)d->loop.out+delta;
  if(in<0||out>(double)d->r->frames){xz_xp_notice(p,"JUMP UNAVAILABLE: LOOP MOVE OUT OF TRACK");return;}
  if(d->slip_reverse){d->slip_reverse=0;d->reverse=0;}
  d->slip_pad=-1;d->pad_hold=PAD_HOLD_NONE;
  d->loop.in=(uint64_t)llround(in);d->loop.out=(uint64_t)llround(out);d->loop.edit=0;
  jump(d,loop_frame(d,(uint64_t)llround(to)),now);loop_pad_store(p,d);return;
 }
 if(to<0){xz_xp_notice(p,"JUMP UNAVAILABLE: BEFORE TRACK START");return;}
 if(to>=(double)d->r->frames){xz_xp_notice(p,"JUMP UNAVAILABLE: AFTER TRACK END");return;}
 if(d->slip_reverse){d->slip_reverse=0;d->reverse=0;}
 d->slip_pad=-1;d->pad_hold=PAD_HOLD_NONE;d->slip_bg.active=0;
 jump(d,(uint64_t)llround(to),now);
}
/* Pads A..H in the deck's pad mode. With SLIP on, a held hot cue plays from the cue and its release
   returns to the background position (p.58). */
static void pad(struct xz_xp_players *p, struct deck *d, const struct xz_extra_deck_event *e, uint32_t now) {
 int index = e->hot_cue;
 if (index < 0 || index >= XZ_EXTRA_HOT_CUES) return;
 switch (d->pad_mode) {
 case XZ_EXTRA_PAD_HOT_CUE:
  if(!e->shift&&d->hot[index].set){
   if(d->slip_reverse){d->slip_reverse=0;d->reverse=0;}
   d->slip_pad=-1;d->pad_hold=PAD_HOLD_NONE;
   if(!d->slip)d->slip_bg.active=0;
  }
  if (d->slip && !e->shift && d->hot[index].set && d->state == XZ_XP_PLAYING) {
   uint64_t at = heard_now(d, now);
   slip_begin(d, at, now); d->slip_pad = index; d->pad_hold=PAD_HOLD_CUE;
   double rate = fabs(d->slip_bg.rate) > 0.01 ? fabs(d->slip_bg.rate) : 1.0;
   d->slip_bg.reverse_ms = (uint32_t)llround(4.0 * beat_frames(d, d->hot[index].at) * 1000.0 / d->r->rate / rate);
  }
  {
   struct hot before=d->hot[index];hot_cue(d,e,now);
   /* Native follow-memory applies only to a successful recall of an existing cue. */
   if(before.set&&!e->shift&&d->hot[index].set&&d->hot[index].at==before.at){
    if(d->command_settings.smart)d->cue=before.at;
    if(d->command_settings.gate){
     if(!d->gate.held)d->gate.latched=0;
     d->gate.held|=1u<<index;d->gate.latest=index;d->gate.at=before.at;
     d->slip_bg.active=0;d->slip_pad=-1;d->pad_hold=PAD_HOLD_NONE;
    }else memset(&d->gate,0,sizeof d->gate);
   }
   const struct hot *after=&d->hot[index];
   if(before.set!=after->set||before.at!=after->at||before.end!=after->end||before.rgb!=after->rgb||before.loop!=after->loop){
    d->saved_hot[index]=*after;
    struct xz_cue_edit edit={.kind=XZ_CUE_EDIT_HOT,.pad=(unsigned)index,.point=saved_hot(d,after)};cue_save(p,d,&edit);
   }
  }
  break;
 case XZ_EXTRA_PAD_BEAT_LOOP: pad_loop(p, d, index, e->pressed, 0, now); break;
 case XZ_EXTRA_PAD_SLIP_LOOP: pad_loop(p, d, index, e->pressed, 1, now); break;
 case XZ_EXTRA_PAD_BEAT_JUMP: if(e->pressed&&d->command_jump_beats)pad_jump_beats(p,d,d->command_jump_beats,now); break;
 default: break;
 }
}
/* DIRECTION REV (p.42) toggles reverse playback. With SLIP on it is held (p.59): reverse while down,
   then the background position on release or after eight beats. */
static void reverse_button(struct deck *d, int pressed, uint32_t now) {
 if (!pressed) {
  if (d->slip_reverse) { d->slip_reverse = 0; d->reverse = 0; slip_return(d, now); }
  return;
 }
 uint64_t at = heard_now(d, now);
 if(d->pad_hold==PAD_HOLD_LOOP){d->loop.active=0;d->loop.edit=0;d->active_pad=-1;}
 d->pad_hold=PAD_HOLD_NONE;d->slip_pad=-1;
 if(!d->slip)d->slip_bg.active=0;
 if (d->slip && !d->reverse && d->state == XZ_XP_PLAYING) {
  slip_begin(d, at, now);
  double rate = fabs(d->slip_bg.rate) > 0.01 ? fabs(d->slip_bg.rate) : 1.0;
  d->slip_bg.reverse_ms = (uint32_t)llround(8.0 * beat_frames(d, at) * 1000.0 / d->r->rate / rate);
  d->slip_reverse = 1; d->reverse = 1; jump(d, at, now);
  return;
 }
 d->reverse = !d->reverse;
 if (d->state == XZ_XP_PLAYING || d->state == XZ_XP_SAMPLING) jump(d, at, now);
}
/* Worker pass: slip reverse ends after eight beats; reverse playback stops at the track start. */
static void direction_step(struct deck *d, uint32_t now) {
 /* Slip Hot Cue (p.59): four beats after it began, playback continues from the background position. */
 if (d->pad_hold == PAD_HOLD_CUE && d->slip_bg.active && d->slip_bg.reverse_ms &&
     (uint32_t)(now - d->slip_bg.at) >= d->slip_bg.reverse_ms) {
  d->pad_hold = PAD_HOLD_NONE; slip_return(d, now);
 }
 if (d->slip_reverse && d->slip_bg.active && (uint32_t)(now - d->slip_bg.at) >= d->slip_bg.reverse_ms) {
  d->slip_reverse = 0; d->reverse = 0; slip_return(d, now);
 }
 if (reversed(d) && d->state == XZ_XP_PLAYING && !d->loop.active && !d->pos && !d->pending &&
     d->completed >= d->submitted) { jump(d, 0, now); d->state = XZ_XP_CUED; }
}

/* ---- master handoff (stock behaviour) ----
   One deck is master. When it stops, the next playing deck in deck order (1, 2, 3, 4) becomes master,
   and every synced deck follows the new one without another SYNC press. Native DECK 1 and DECK 2 are one
   candidate: the firmware's own master, which moves between them itself. A software deck takes over from a
   stopped native master only after XZ_XP_MASTER_HANDOFF_MS, so the firmware's own handoff wins the race.
   Channel-fader-down handoff needs the fader positions, which this runtime does not read yet. */
static int state_playing(int s){return s==XZ_XP_PLAYING||s==XZ_XP_SAMPLING||s==XZ_XP_SCRATCHING;}
/* The native clock cache is only published while a software deck has SYNC on (xz_xp_needs_native_sync);
   without it the native master's state is unknown and reads as not playing and not absent. */
static int native_master_playing(struct xz_xp_players *p,uint32_t now,int *absent,struct xz_sync_native_snapshot *stopped){
 struct xz_sync_native_snapshot n={0};*absent=0;
 if(!p->native_sync_clock||(!atomic_load(&p->sync_requests)&&!p->master_observer))return 0;
 int copied=p->native_sync_clock(&n);
 int fresh=(uint32_t)(now-n.captured_ms)<=150u;
 if(!stopped&&fresh&&n.playing_native_mask)return 1;
 if(!copied)return 0;
 if(n.reason==XZ_SYNC_NATIVE_EXTERNAL_MASTER)return 0;
 if(n.reason==XZ_SYNC_NATIVE_MASTER_UNAVAILABLE){*absent=1;return 0;}
 if(!n.available||!fresh)return 0;
 if(stopped)*stopped=n;
 if(!n.playing){*absent=1;if(stopped)*stopped=n;return 0;}
 return 1;
}
/* p.64: with no master yet, the deck that first plays an analysed track becomes master. Stock cannot
   see DECK 3/4 until one claims, so a DECK 3/4 claims it here: only while the engine names no master
   and nobody has chosen one since start (operator epoch 1). A native deck playing an analysed track
   would already be master; of two DECK 3/4 starts the first claim wins (the second sees it pending). */
static int first_play_master(const struct deck *d){
 if(d->master_actual.deck!=XZ_MASTER_NONE||d->master_actual.operator_epoch!=1)return 0;
 if((d->state!=XZ_XP_PLAYING&&d->state!=XZ_XP_SAMPLING)||(d->ramp.active&&d->ramp.stopping))return 0;
 return d->analysis&&d->analysis->grid_count>=2;
}
static void master_handoff(struct xz_xp_players *p,struct deck *d,uint32_t now){
 uint32_t authority_choice;int selected=sync_selection(p,d,&authority_choice),own=0;
 if(p->master_observer){
  uint32_t intent_epoch=atomic_load(&p->sync_choice_epoch);
  if(d->handoff_intent_epoch!=intent_epoch){
   d->handoff_intent_epoch=intent_epoch;d->master_playing_seen=0;d->native_observed_known=0;d->native_idle_pending=0;
  }
  pthread_mutex_lock(&p->publish_lock);struct xz_master_intent intent=master_intent_locked(p);
  int pending_intent=p->acknowledged_intent!=intent.epoch;pthread_mutex_unlock(&p->publish_lock);
  /* The engine confirmed this deck for the current intent. The bridge withdraws
     a stopping source before this worker sees it stop, so the observation alone
     would lose the handoff. Operator events replace the intent instead. */
  own=!pending_intent&&intent.deck==d->index+2;
  if(own)authority_choice=intent.epoch;
  else if(intent.deck==XZ_MASTER_NONE&&!intent.explicit_choice&&first_play_master(d)){
   if(select_sync_master(p,d->index))xz_xp_notice(p,d->index?"MASTER: DECK 4":"MASTER: DECK 3");
   return;
  }else if(selected< -1||(pending_intent&&intent.explicit_choice&&intent.deck!=d->master_actual.deck))return;
 }
 int other=1-d->index,other_playing=state_playing(atomic_load(&p->deck[other].published_state));
 /* The stock-parity rules below (M2, M4, M5) apply with the observer; without it (legacy embedders, review
    fixtures) the handoffs stay as before. */
 int parity=p->master_observer!=NULL;
 int other_synced=parity&&other_playing&&(atomic_load(&p->sync_requests)&(1u<<other));
 int absent;
 if(selected==d->index||own){
  d->native_idle_pending=0;d->native_load_handoff=0;
  uint32_t choice=authority_choice;
  if(d->master_playing_choice!=choice){d->master_playing_choice=choice;d->master_playing_seen=0;d->master_hold_pending=0;}
  /* M4: held on its platter top (VINYL) for the hold time, the master passes on as at a stop; the
     platter's own playback resumes on release. M5: a new track loaded on the master passes master to
     the other playing deck; the loading deck keeps SYNC. */
  int held=parity&&d->state==XZ_XP_SCRATCHING&&d->scratch.contact&&streaming(d->scratch.resume);
  if(!held)d->master_hold_pending=0;
  else if(!d->master_hold_pending){d->master_hold_pending=1;d->master_hold_since=now;}
  int hold_due=held&&(uint32_t)(now-d->master_hold_since)>=XZ_XP_MASTER_HANDOFF_MS;
  int load_due=parity&&d->master_load_handoff;d->master_load_handoff=0;
  /* A pause in VINYL mode brakes first; the master leaves at the press, as in CDJ mode. */
  if(state_playing(d->state)&&!hold_due&&!load_due){d->master_playing_seen=1;if(!(d->ramp.active&&d->ramp.stopping))return;}
  if(loaded(d->state)&&!d->master_playing_seen&&!load_due)return;
  struct xz_sync_native_snapshot candidate={0};
  int native=native_master_playing(p,now,&absent,NULL),native_deck=-1;
  if(p->master_observer&&p->native_sync_clock){
   (void)p->native_sync_clock(&candidate);
   if((uint32_t)(now-candidate.captured_ms)<=150u){
    if(candidate.playing_native_mask&1u)native_deck=0;
    else if(candidate.playing_native_mask&2u)native_deck=1;
    else if(candidate.available&&candidate.playing&&(unsigned)candidate.native_deck<2)native_deck=candidate.native_deck;
   }
   native=native_deck>=0;
  }
  /* M4: a playing deck with SYNC on comes first. Then after DECK 3 comes DECK 4, then the native decks;
     after DECK 4 come the native decks, then DECK 3. */
  int next=other_synced?other:d->index==0?(other_playing?other:native?-1:d->index):(native?-1:other_playing?other:d->index);
  if(next!=d->index&&(next<0&&p->master_observer?request_master(p,native_deck,0,0,observe_operator(p)):select_sync_master(p,next)))xz_xp_notice(p,next<0?"MASTER: NATIVE DECK":next?"MASTER: DECK 4":"MASTER: DECK 3");
  return;
 }
 /* Without the observer, as before: only a playing deck reads the native clock here. */
 if(selected>=0||(!p->master_observer&&(!state_playing(d->state)||d->state==XZ_XP_SCRATCHING))){d->native_idle_pending=0;return;}
 struct xz_sync_native_snapshot stopped={0};
 int native_playing=native_master_playing(p,now,&absent,&stopped);
 /* An operator event is a new choice. The observation revision also moves when
    the stock consumer pauses, which must not forget that the master played. */
 uint32_t choice=p->master_observer?d->master_actual.operator_epoch:authority_choice;
 if(!stopped.available||(p->master_observer&&stopped.native_deck!=d->master_actual.deck)){
  d->native_idle_pending=0;return;
 }
 if(!d->native_observed_known||d->native_observed_choice!=choice||
    d->native_observed_deck!=stopped.native_deck||d->native_observed_identity!=stopped.track_identity||
    d->native_observed_revision!=stopped.source_revision){
  /* M5: the native master took a new track after it played: the other playing deck becomes master.
     With the observer only; without it (legacy embedders, review fixtures) behaviour is unchanged. */
  d->native_load_handoff=p->master_observer&&d->native_observed_known&&d->native_playing_seen&&
   d->native_observed_choice==choice&&d->native_observed_deck==stopped.native_deck&&
   d->native_observed_identity!=stopped.track_identity;
  d->native_observed_known=1;d->native_observed_choice=choice;d->native_observed_deck=stopped.native_deck;
  d->native_observed_identity=stopped.track_identity;d->native_observed_revision=stopped.source_revision;
  d->native_playing_seen=0;d->native_idle_pending=0;
 }
 /* Stock moves master between its own decks 1/2 (M2, M5): with the other native deck playing, it decides. */
 int other_native=!!(stopped.playing_native_mask&(1u<<(1-stopped.native_deck)));
 int me_synced=parity&&d->sync.requested,first=!(other_playing&&atomic_load(&p->deck[other].published_state)!=XZ_XP_SCRATCHING&&
  (d->index==1?other_synced||!me_synced:other_synced&&!me_synced)); /* DECK 3 first, a synced deck before either */
 /* A deck braking to a stop (its own VINYL pause) never takes master. */
 int braking=d->ramp.active&&d->ramp.stopping;
 if(d->native_load_handoff){
  d->native_load_handoff=0;
  if(state_playing(d->state)&&d->state!=XZ_XP_SCRATCHING&&!braking&&!other_native&&first){
   d->native_idle_pending=0;
   if(select_sync_master(p,d->index))xz_xp_notice(p,d->index?"MASTER: DECK 4":"MASTER: DECK 3");
   return;
  }
 }
 if(native_playing){d->native_playing_seen=1;d->native_idle_pending=0;return;}
 /* A paused deck still records that the native master played; only a playing deck takes over. */
 if(!state_playing(d->state)||d->state==XZ_XP_SCRATCHING||(parity&&braking)){d->native_idle_pending=0;return;}
 if(!absent||!d->native_playing_seen){d->native_idle_pending=0;return;}
 if(!d->native_idle_pending||d->native_idle_deck!=stopped.native_deck||
    d->native_idle_identity!=stopped.track_identity||d->native_idle_revision!=stopped.source_revision||
    (int32_t)(now-d->native_idle_since)<0){
  d->native_idle_pending=1;d->native_idle_since=now;
  d->native_idle_deck=stopped.native_deck;d->native_idle_identity=stopped.track_identity;
  d->native_idle_revision=stopped.source_revision;return;
 }
 /* M2: a paused native master hands over at once. The hold time applies to a hold on its platter top
    (M4, reads manual) and, so stock's own 1/2 handoff wins, while the other native deck plays. Without
    the observer (legacy embedders, review fixtures) the hold time applies to every stop, as before. */
 if((stopped.manual||other_native||!p->master_observer)&&(uint32_t)(now-d->native_idle_since)<XZ_XP_MASTER_HANDOFF_MS)return;
 if(!first)return;
 d->native_idle_pending=0;
 if(select_sync_master(p,d->index))xz_xp_notice(p,d->index?"MASTER: DECK 4":"MASTER: DECK 3");
}

static int key_target_current(struct xz_xp_players *p,const struct xz_key_command *c){
 struct xz_master_observation m;copy_master(p,&m);
 if(!c->target_valid||!authority_same(&m,&c->master))return 0;
 struct xz_key_owner owner={0};
 if(c->target_deck<2){
  if(!p->key_owner_reader||!p->key_owner_reader(c->target_deck,&owner))return 0;
 }else if(c->target_deck<4){
  pthread_mutex_lock(&p->publish_lock);
  const struct xz_extra_deck *v=&p->view[c->target_deck-2];
  if(v->loaded&&(v->known&XZ_EXTRA_KNOWN(XZ_EXTRA_F_KEY_STATE)))owner=v->key_state.owner;
  pthread_mutex_unlock(&p->publish_lock);
 }
 return xz_key_owner_equal(c->target_deck,c->target_owner,owner);
}
static void key_command(struct xz_xp_players *p,struct deck *d,const struct xz_key_command *c){
 struct xz_key_owner own={.route_epoch=d->epoch,.load_revision=d->load_revision,.media_generation=d->media_generation};
 if(!loaded(d->state)||!xz_key_owner_equal(d->index+2,own,c->source_owner)){
  xz_xp_notice(p,"TRACK CHANGED / RETRY KEY CONTROL");return;
 }
 if(d->emergency.active){xz_xp_notice(p,xz_key_refusal_text(XZ_KEY_EMERGENCY));return;}
 int cents=d->key.desired,mt=d->master_tempo,engaged=0,mt_before=d->key.mt_before;
 if(c->kind==XZ_KEY_RESET)cents=0;
 else if(c->kind==XZ_KEY_STEP&&(c->step==-1||c->step==1))cents+=100*c->step;
 else if(c->kind==XZ_KEY_SYNC_TOGGLE){
  if(d->key.engaged){cents=0;mt=d->key.mt_before;}
  else{
   if(!key_target_current(p,c)){xz_xp_notice(p,"MASTER CHANGED / RETRY KEY SYNC");return;}
   struct xz_key_snapshot s={.loaded=1,.tempo_ratio=1,.master_tempo=1};
   if(!xz_music_key_parse(d->track.key,&s.original)||!xz_key_sync_cents(&s,&c->target,&cents)){
    xz_xp_notice(p,xz_key_refusal_text(XZ_KEY_NO_SOURCE));return;
   }
   mt_before=mt;mt=1;engaged=1;
  }
 }else return;
 if(cents < -1200||cents>1200){xz_xp_notice(p,xz_key_refusal_text(XZ_KEY_RANGE));return;}
 if(cents&&!key_admit(p,d)){
  xz_xp_notice(p,"KEY SHIFT REFUSED: AUDIO LOAD TOO HIGH");return;
 }
 if(mt!=d->master_tempo)xz_stretch_reset(&d->stretch);
 d->master_tempo=mt;d->key.desired=cents;d->key.engaged=engaged;d->key.mt_before=mt_before;
 d->key.target=engaged?c->target_deck:-1;d->key.target_owner=c->target_owner;d->key.revision++;
 d->emergency.count=0;
 if(!cents&&!streaming(d->state))key_history_reset(d);
 xz_xp_notice(p,cents?"KEY REQUEST QUEUED / PLAY TO VERIFY":"KEY RESET QUEUED");
}
static void command(struct xz_xp_players *p, struct deck *d, const struct xz_extra_deck_event *e, uint32_t now, uint32_t arrived,uint32_t master_choice,uint32_t operator_epoch) {
 if(e->kind==XZ_EXTRA_IN_PLAY&&e->pressed&&!e->held&&d->gate.held&&!d->gate.latched){
  d->gate.latched=1;return; /* Native Gate Cue consumes PLAY to latch the recall. */
 }
 if(e->kind==XZ_EXTRA_IN_KEY){if(e->pressed&&!e->held)key_command(p,d,&e->key);return;}
 if(e->kind==XZ_EXTRA_IN_SYNC&&e->shift){
  if(e->pressed&&!e->held)xz_xp_notice(p,"SET UP KEYS / KEY TARGET NOT CAPTURED");
  return;
 }
 if(e->kind==XZ_EXTRA_IN_MEMORY||e->kind==XZ_EXTRA_IN_MEMORY_DELETE){
  if(!e->pressed||e->held||!loaded(d->state)||d->emergency.active)return;
  uint64_t at=d->loop.active?d->loop.in:d->cue,end=d->loop.active?d->loop.out:0;
  int index=0;while(index<d->memory_count&&d->memory[index].at<at)index++;
  int exists=index<d->memory_count&&d->memory[index].at==at;
  uint32_t rgb=exists?d->memory[index].rgb:0;
  int32_t saved_ms=exists?d->memory[index].saved_ms:to_ms(d,at);
  int32_t saved_end_ms=end>at?(exists&&d->memory[index].loop&&d->memory[index].end==end?d->memory[index].saved_end_ms:to_ms(d,end)):0;
  if(e->kind==XZ_EXTRA_IN_MEMORY&&end>at){
   if(saved_end_ms<to_ms(d,d->r->frames)&&to_frames(d,saved_end_ms)<end)saved_end_ms++;
   if(saved_end_ms<=saved_ms){xz_xp_notice(p,"LOOP KEPT FOR THIS SESSION");return;}
  }
  if(e->kind==XZ_EXTRA_IN_MEMORY_DELETE){
   if(!exists){xz_xp_notice(p,"NO MEMORY POINT AT CUE");return;}
   memmove(d->memory+index,d->memory+index+1,(size_t)(d->memory_count-index-1)*sizeof *d->memory);d->memory_count--;
   if(d->active_loop.in==at)d->active_loop.armed=0;
   xz_xp_notice(p,"MEMORY POINT DELETED");
  }else{
   if(!exists){
    if(d->memory_count==XZ_SHELL_MEMORY_CUES){xz_xp_notice(p,"MEMORY POINTS FULL");return;}
    memmove(d->memory+index+1,d->memory+index,(size_t)(d->memory_count-index)*sizeof *d->memory);d->memory_count++;
   }
   d->memory[index]=(struct memory_point){at,end,end>at,rgb,saved_ms,saved_end_ms};xz_xp_notice(p,"MEMORY POINT SET");
  }
  struct xz_cue_edit edit={.kind=e->kind==XZ_EXTRA_IN_MEMORY?XZ_CUE_EDIT_MEMORY:XZ_CUE_EDIT_DELETE,
   .point={1,saved_ms,rgb,saved_end_ms}};cue_save(p,d,&edit);return;
 }
 if(d->emergency.active&&e->kind!=XZ_EXTRA_IN_LOAD&&e->kind!=XZ_EXTRA_IN_INSTANT_DOUBLES){
  if(e->pressed&&(e->kind==XZ_EXTRA_IN_PLAY||e->kind==XZ_EXTRA_IN_CUE)){
   if(streaming(d->state))pause_at_heard(d,now);else if(e->kind==XZ_EXTRA_IN_PLAY)start(d,now);
  }
  return;
 }
 if(e->kind==XZ_EXTRA_IN_LOOP_4_8){
  if(!e->pressed){d->auto_button.down=0;return;}
  if(!loaded(d->state))return;
  if(e->held){
   if(!d->auto_button.down||!d->auto_button.extend||(uint32_t)(arrived-d->auto_button.at)<1000u)return;
   d->auto_button.extend=0;
  }else{
   if(d->auto_button.down)return;
   d->auto_button.down=1;d->auto_button.at=arrived;d->auto_button.extend=!d->loop.active;
   if(d->loop.active){
    struct xz_extra_deck_event resize={.kind=e->shift?XZ_EXTRA_IN_LOOP_DOUBLE:XZ_EXTRA_IN_LOOP_HALF,.pressed=1};
    loop_button(p,d,&resize,now);return;
   }
   d->auto_button.in=quantize_floor(d,heard_now(d,now));
  }
  uint64_t at=heard_now(d,now),in=d->auto_button.in;
  uint64_t length=(uint64_t)llround((e->held?8.:4.)*beat_frames(d,in));
  if(d->slip&&length>8u*(uint64_t)d->r->rate){xz_xp_notice(p,"LOOP OVER 8 SECONDS");return;}
  uint64_t end=length>d->r->frames-in?d->r->frames:in+length;
  if(end<=in)return;
  d->loop.in=in;d->loop.out=end;d->loop.has_in=d->loop.valid=d->loop.active=1;d->loop.edit=0;
  if(d->slip&&!d->slip_bg.active)slip_begin(d,at,now);
  if(d->state==XZ_XP_SCRATCHING)scratch_release(d,now);
  jump(d,loop_frame(d,at),now);start(d,now);return;
 }
 if(e->pressed&&(e->kind==XZ_EXTRA_IN_LOAD||e->kind==XZ_EXTRA_IN_CUE||e->kind==XZ_EXTRA_IN_SEEK||e->kind==XZ_EXTRA_IN_HOT_CUE))d->auto_button.down=0;
 if(e->kind==XZ_EXTRA_IN_INSTANT_DOUBLES){
  if(!e->pressed)return;
  int peer=1-d->index;struct xz_extra_deck_event source;struct xz_extra_deck source_view;
  pthread_mutex_lock(&p->publish_lock);source=p->duplicate_source[peer];source_view=p->view[peer];pthread_mutex_unlock(&p->publish_lock);
  if(!source.load.track_id||!source_view.loaded){xz_xp_notice(p,"LOAD OTHER SOFTWARE DECK FIRST");return;}
  source.deck=d->index;
  if(!load(p,d,&source,now,source.load.media_generation!=0))return;
  pthread_mutex_lock(&p->publish_lock);
  struct xz_extra_deck_event current=p->duplicate_source[peer];source_view=p->view[peer];
  int same=current.load.source==source.load.source&&current.load.track_id==source.load.track_id&&
   current.load.media_generation==source.load.media_generation&&current.load.route_epoch==source.load.route_epoch;
  pthread_mutex_unlock(&p->publish_lock);
  if(!same){xz_xp_notice(p,"DOUBLES SOURCE CHANGED");return;}
  int32_t at=source_view.elapsed_ms;uint32_t age=p->env.now_ms()-source_view.timeline.at_ms;
  if(source_view.playing&&(source_view.timeline.flags&XZ_EXTRA_TIMELINE_RATE)&&age<=150u)
   at+=(int32_t)((int64_t)age*source_view.timeline.rate_ppm/1000000);
  d->tempo_range=source_view.tempo_range;d->tempo_x100=source_view.tempo_x100;d->tempo_revision++;
  d->tempo_position=(int32_t)((int64_t)d->tempo_x100*XZ_EXTRA_TEMPO_SPAN/(d->tempo_range*100));d->tempo_reset=0;
  d->reverse=source_view.reverse;d->sync_rate_active=0;
  if(source_view.loop.active){
   uint64_t end;
   if(stored_loop_end(d,source_view.loop.in_ms,source_view.loop.out_ms,&end)){
    d->loop.in=to_frames(d,source_view.loop.in_ms);d->loop.out=end;d->loop.has_in=d->loop.valid=d->loop.active=1;
   }
  }
  jump(d,to_frames(d,at),now);if(source_view.playing||source_view.held_playing)start(d,now);
  return;
 }
 if(e->kind==XZ_EXTRA_IN_HOT_CUE&&d->pad_mode==XZ_EXTRA_PAD_CALL){
  if(e->pressed&&!e->held&&e->hot_cue>=0&&e->hot_cue<XZ_EXTRA_HOT_CUES&&d->saved_hot[e->hot_cue].set){
   d->hot[e->hot_cue]=d->saved_hot[e->hot_cue];d->called_hot|=1u<<e->hot_cue;
  }
  return;
 }
 /* A later transport/source gesture follows release in FIFO order and supersedes its tail.
    Settings and inert button-up traffic do not discard accepted motion or renew the deadline. */
 /* A release belongs to its accepted press, before current-mode or scratch cancellation gates.
    Repeat notifications and duplicate downs cannot acquire a new action or toggle REV. */
 if(e->kind==XZ_EXTRA_IN_HOT_CUE||e->kind==XZ_EXTRA_IN_REVERSE){
  unsigned bit=release_bit(e);if(!bit)return;
  if(e->pressed){
   if(e->held||(d->control_down&bit))return;
   d->control_down|=bit;
   /* Ordinary HOT CUE also has one-shot callers. Only a momentary action needs an up edge. */
   if(e->kind==XZ_EXTRA_IN_HOT_CUE&&d->pad_mode==XZ_EXTRA_PAD_HOT_CUE&&
      (!d->slip||e->shift||!d->hot[e->hot_cue].set||d->state!=XZ_XP_PLAYING)&&
       !(d->command_settings.gate&&!e->shift&&d->hot[e->hot_cue].set))d->control_down&=~bit;
  }else{
   d->control_down&=~bit;
   if(e->kind==XZ_EXTRA_IN_HOT_CUE&&(d->gate.held&(1u<<e->hot_cue))){
    d->gate.held&=~(1u<<e->hot_cue);
    const struct hot *latest=&d->hot[d->gate.latest];
    if(!d->gate.held&&!d->gate.latched&&latest->set&&latest->at==d->gate.at){
     pause_at_heard(d,now);d->loop.active=0;d->ramp.active=0;
     d->slip_bg.active=0;d->slip_pad=-1;d->pad_hold=PAD_HOLD_NONE;
     jump(d,d->gate.at,now);d->state=XZ_XP_CUED;
    }
   }
   if(e->kind==XZ_EXTRA_IN_REVERSE)reverse_button(d,0,now);
   else if(d->slip_pad==e->hot_cue){
    if(d->pad_hold==PAD_HOLD_LOOP)loop_off(d,now);
    else if(d->pad_hold==PAD_HOLD_CUE)slip_return(d,now);
    d->pad_hold=PAD_HOLD_NONE;d->slip_pad=-1;
   }
   return;
  }
 }
 if(e->kind==XZ_EXTRA_IN_SEARCH){
  unsigned bit=release_bit(e);if(!bit)return;
  if(e->pressed){if(e->held||(d->control_down&bit))return;d->control_down|=bit;}
  else{
   d->control_down&=~bit;
   if(d->search==(e->hot_cue?-1:1)){d->search=0;jump(d,heard_now(d,now),now);}
   return;
  }
 }
 if(e->kind==XZ_EXTRA_IN_HOT_CUE&&d->pad_mode==XZ_EXTRA_PAD_NONE)return;
 int superseded_release=d->scratch.releasing||
  (d->state==XZ_XP_SCRATCHING&&!d->scratch.contact);
 int later_transport=e->kind==XZ_EXTRA_IN_LOAD||e->kind==XZ_EXTRA_IN_SEEK||
    (e->kind==XZ_EXTRA_IN_CUE&&(e->pressed||d->scratch.resume==XZ_XP_SAMPLING))||
    (e->pressed&&(e->kind==XZ_EXTRA_IN_HOT_CUE||e->kind==XZ_EXTRA_IN_VINYL||
     e->kind==XZ_EXTRA_IN_MASTER_TEMPO||e->kind==XZ_EXTRA_IN_TRACK_NEXT||
     e->kind==XZ_EXTRA_IN_LOOP_IN||e->kind==XZ_EXTRA_IN_LOOP_OUT||
     e->kind==XZ_EXTRA_IN_RELOOP||e->kind==XZ_EXTRA_IN_LOOP_HALF||e->kind==XZ_EXTRA_IN_LOOP_DOUBLE));
 int unheld_play=e->kind==XZ_EXTRA_IN_PLAY&&e->pressed&&!d->scratch.contact;
 if((d->scratch.releasing&&(
    e->kind==XZ_EXTRA_IN_LOAD||e->kind==XZ_EXTRA_IN_SEEK||
    (e->kind==XZ_EXTRA_IN_JOG_TURN&&e->jog.delta)||
    (e->kind==XZ_EXTRA_IN_CUE&&(e->pressed||d->scratch.resume==XZ_XP_SAMPLING))||
    (e->pressed&&(e->kind==XZ_EXTRA_IN_PLAY||e->kind==XZ_EXTRA_IN_JOG_TOUCH||
     e->kind==XZ_EXTRA_IN_HOT_CUE||e->kind==XZ_EXTRA_IN_VINYL||
     e->kind==XZ_EXTRA_IN_MASTER_TEMPO||e->kind==XZ_EXTRA_IN_TRACK_NEXT||
     e->kind==XZ_EXTRA_IN_LOOP_IN||e->kind==XZ_EXTRA_IN_LOOP_OUT||
      e->kind==XZ_EXTRA_IN_RELOOP||e->kind==XZ_EXTRA_IN_LOOP_HALF||e->kind==XZ_EXTRA_IN_LOOP_DOUBLE))))||
    (d->state==XZ_XP_SCRATCHING&&(later_transport||unheld_play)))
  scratch_release(d,now);
 if (e->kind == XZ_EXTRA_IN_LOAD) {
  int playing = d->state == XZ_XP_PLAYING || d->state == XZ_XP_SAMPLING;
  if (load(p, d, e, now, 0) && playing && !d->auto_cue) start(d, now);
  return;
 }
 if(e->kind==XZ_EXTRA_IN_MASTER){
  if(e->pressed&&loaded(d->state)){
   if(p->master_observer){
    /* M1: as on stock, the MASTER lamps show the change when it commits; no pending notice. */
    (void)request_master(p,d->index+2,1,master_choice,operator_epoch);
    return;
   }
   d->master_playing_seen=state_playing(d->state);
   (void)select_sync_master(p,d->index);d->master_playing_choice=atomic_load(&p->sync_choice_epoch);
   d->sync_trim=0;
   d->sync_initial=d->sync_hold=d->sync_have_master=0;d->sync.locked=0;
   d->sync.master_deck=d->index+XZ_EXTRA_FIRST_DECK;
   d->sync.master_scope=XZ_EXTRA_MASTER_SCOPE_SOFTWARE;
   d->sync.reason=d->sync.requested?XZ_EXTRA_SYNC_MASTER_CLOCK:XZ_EXTRA_SYNC_OFF;
   xz_xp_notice(p,"SOFTWARE CLOCK MASTER");
  }
  return;
 }
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
  if(load(p,d,&next,now,1)&&playing&&!d->auto_cue)start(d,now);
  return;
 }
 if(e->kind==XZ_EXTRA_IN_TRACK_PREV){
  if(!e->pressed||!loaded(d->state))return;
  int twice=d->track_prev_known&&(uint32_t)(arrived-d->track_prev_at)<XZ_XP_TRACK_PREV_MS;
  d->track_prev_at=arrived;d->track_prev_known=1;
  int playing=d->state==XZ_XP_PLAYING||d->state==XZ_XP_SAMPLING||(d->state==XZ_XP_SCRATCHING&&streaming(d->scratch.resume));
  if(twice&&d->order&&d->order_epoch&&d->order_epoch==p->env.sink.link(p->env.sink.ctx)&&
     d->retired_count<XZ_XP_RETIRE_SLOTS&&d->order_index>0){
   /* p.40: pressed twice quickly, the beginning of the previous track. */
   uint32_t row=d->order_index;
   while(row>0&&!d->order->id[row-1])row--;
   if(row>0){
    struct xz_extra_deck_event prev={.kind=XZ_EXTRA_IN_LOAD,.deck=d->index};
    prev.load.source=d->order->source;prev.load.media_generation=d->order->media_generation;
    prev.load.route_epoch=d->order_epoch;prev.load.track_id=d->order->id[row-1];prev.load.order=d->order;prev.load.order_index=row-1;
    d->track_prev_known=0;
    if(load(p,d,&prev,now,1)&&playing&&!d->auto_cue)start(d,now);
    return;
   }
  }
  /* p.40: the beginning of the current track, the point it cued to at load. */
  if(d->slip_reverse)d->reverse=0;d->slip_reverse=0;d->slip_bg.active=0;d->slip_pad=-1;d->pad_hold=PAD_HOLD_NONE;d->search=0;
  if(d->state==XZ_XP_SCRATCHING)scratch_release(d,now);
  d->loop.active=0;d->loop.edit=0;d->active_pad=d->loop_pad=-1;
  d->cue=d->auto_cue_at;jump(d,d->cue,now);
  if(!streaming(d->state))d->state=XZ_XP_CUED;
  return;
 }
 if (e->kind == XZ_EXTRA_IN_TEMPO) {
  /* A synced deck takes its tempo from the master (stock): the fader is ignored until SYNC is off. */
  if(d->sync.requested&&!current_master(p,d))return;
  int32_t at = e->tempo_position;
  if (at > XZ_EXTRA_TEMPO_SPAN) at = XZ_EXTRA_TEMPO_SPAN;
  if (at < -XZ_EXTRA_TEMPO_SPAN) at = -XZ_EXTRA_TEMPO_SPAN;
  d->tempo_position = at; set_tempo(d);
  return;
 }
 if (e->kind == XZ_EXTRA_IN_TEMPO_RANGE) {
  if(d->sync.requested&&!current_master(p,d))return;
  if (e->pressed) {
   d->tempo_range = d->tempo_range == 6 ? 10 : d->tempo_range == 10 ? 16 : d->tempo_range == 16 ? 100 : 6;
   if(d->sync_rate_active){
    /* RANGE remaps pickup while the exact matched rate remains held. */
    d->tempo_position=(int32_t)((int64_t)d->tempo_x100*XZ_EXTRA_TEMPO_SPAN/(d->tempo_range*100));
    d->tempo_revision++;
   }else set_tempo(d);
  }
  return;
 }
 if (e->kind == XZ_EXTRA_IN_TEMPO_RESET) {
  if(d->sync.requested&&!current_master(p,d))return;
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
 if (e->kind == XZ_EXTRA_IN_PAD_MODE) {
  if(e->pressed&&!e->held&&!e->shift&&e->hot_cue==XZ_EXTRA_PAD_HOT_CUE){
   if(d->pad_mode==XZ_EXTRA_PAD_CALL){
    for(unsigned i=0;i<XZ_EXTRA_HOT_CUES;i++)if(!(d->called_hot&(1u<<i)))d->hot[i].set=0;
    d->pad_mode=XZ_EXTRA_PAD_HOT_CUE;
   }else if(d->pad_mode==XZ_EXTRA_PAD_HOT_CUE){d->pad_mode=XZ_EXTRA_PAD_CALL;d->called_hot=0;}
   else d->pad_mode=XZ_EXTRA_PAD_HOT_CUE;
   return;
  }
  /* SHIFT+BEAT JUMP selects page 2; SHIFT on a jump pad is inert (native forwards to stock). */
  if (e->pressed && e->hot_cue >= XZ_EXTRA_PAD_HOT_CUE && e->hot_cue <= XZ_EXTRA_PAD_BEAT_JUMP &&
      (!e->shift || e->hot_cue == XZ_EXTRA_PAD_HOT_CUE || e->hot_cue == XZ_EXTRA_PAD_BEAT_JUMP))
   d->pad_mode = e->shift && e->hot_cue==XZ_EXTRA_PAD_HOT_CUE ? XZ_EXTRA_PAD_NONE : e->hot_cue;
  return;
 }
 if (e->kind == XZ_EXTRA_IN_QUANTIZE || e->kind == XZ_EXTRA_IN_TIME_MODE || e->kind == XZ_EXTRA_IN_AUTO_CUE ||
     e->kind == XZ_EXTRA_IN_MODE_SET) {
  if (!e->pressed) return;
  int mode = e->kind == XZ_EXTRA_IN_MODE_SET ? e->hot_cue / 2 : e->kind == XZ_EXTRA_IN_QUANTIZE ? XZ_EXTRA_MODE_QUANTIZE :
             e->kind == XZ_EXTRA_IN_TIME_MODE ? XZ_EXTRA_MODE_REMAIN : XZ_EXTRA_MODE_AUTO_CUE;
  int *flag = mode == XZ_EXTRA_MODE_QUANTIZE ? &d->quantize : mode == XZ_EXTRA_MODE_REMAIN ? &d->remain :
              mode == XZ_EXTRA_MODE_AUTO_CUE ? &d->auto_cue : NULL;
  if (!flag) return;
  int on = e->kind == XZ_EXTRA_IN_MODE_SET ? e->hot_cue % 2 : !*flag;
  if (on != *flag) {
   *flag = on;
   /* Turning QUANTIZE on while synced snaps to the beat; off keeps the tempo and leaves the position. */
   if (mode == XZ_EXTRA_MODE_QUANTIZE && d->sync.requested) sync_reacquire(d, now);
  }
  if (e->kind == XZ_EXTRA_IN_QUANTIZE) xz_xp_notice(p, on ? "QUANTIZE ON" : "QUANTIZE OFF");
  if (e->kind == XZ_EXTRA_IN_AUTO_CUE) xz_xp_notice(p, on ? "AUTO CUE ON" : "AUTO CUE OFF");
  return;
 }
 if (e->kind == XZ_EXTRA_IN_SLIP) {
  if (e->pressed) {
   d->slip = !d->slip;
   if (!d->slip) {
    if(d->pad_hold==PAD_HOLD_LOOP){d->loop.active=0;d->loop.edit=0;d->active_pad=-1;}
    if(d->slip_reverse)d->reverse=0;
    slip_return(d,now);d->slip_bg.active=d->slip_reverse=0;d->slip_pad=-1;d->pad_hold=PAD_HOLD_NONE;
   }
  }
  return;
 }
 if (e->kind == XZ_EXTRA_IN_VINYL_SPEED) {
  int32_t at = e->tempo_position;
  d->vinyl_level = at > XZ_EXTRA_TEMPO_SPAN ? XZ_EXTRA_TEMPO_SPAN : at < -XZ_EXTRA_TEMPO_SPAN ? -XZ_EXTRA_TEMPO_SPAN : at;
  return;
 }
 if (e->kind == XZ_EXTRA_IN_REVERSE && !loaded(d->state)) { if (e->pressed) d->reverse = !d->reverse; return; }
 if (e->kind == XZ_EXTRA_IN_MASTER_TEMPO) {
  if(d->key.mt_owned){if(!e->pressed)d->key.mt_owned=0;return;}
  if(e->pressed&&!e->held&&!e->shift&&d->key.engaged){
   struct xz_key_command c={.kind=XZ_KEY_SYNC_TOGGLE,.source_owner={.route_epoch=d->epoch,
    .load_revision=d->load_revision,.media_generation=d->media_generation}};
   key_command(p,d,&c);if(!d->key.engaged)d->key.mt_owned=1;return;
  }
  if(d->emergency.active){if(e->pressed)xz_xp_notice(p,xz_key_refusal_text(XZ_KEY_EMERGENCY));return;}
  if(e->pressed){
   if(loaded(d->state))jump(d,heard_now(d,now),now);
   d->master_tempo=!d->master_tempo;
   /* Persistent deck/lamp state is the feedback; ordinary toggles do not replace notices. */
  }
  return;
 }
 if (!loaded(d->state)) return;
 switch (e->kind) {
 case XZ_EXTRA_IN_PLAY:
  if (!e->pressed) break;
  d->search=0;
  if(d->state==XZ_XP_SCRATCHING){
   d->scratch.resume=d->scratch.resume==XZ_XP_PLAYING?XZ_XP_CUED:XZ_XP_PLAYING;
   break;
  }
  /* VINYL mode (p.45): PAUSE brakes to a stop at the VINYL SPEED ADJUST rate. PLAY starts at once, as the
     native decks do (owner, 2026-10-22); PLAY during a brake returns to full speed at once. */
  if (d->vinyl && d->state == XZ_XP_PLAYING && !superseded_release) {
   if (d->ramp.active && d->ramp.stopping) {
    d->ramp.active = 0; xz_stretch_reset(&d->stretch);
    if (d->slip_pause) { d->slip_pause = 0; slip_return(d, now); }
    /* The brake left the master's beat behind: as after any pause, a synced deck finds it again. */
    else if (d->sync.requested) sync_reacquire(d, now);
   } else {
    if (d->slip && !d->slip_bg.active) { slip_begin(d, heard_now(d, now), now); d->slip_pause = 1; }
    if (d->vinyl_level <= -XZ_EXTRA_TEMPO_SPAN + 100) pause_at_heard(d, now);
    else ramp_from(d, 1, ramp_factor(d, now), now);
   }
   break;
  }
  if (d->state == XZ_XP_CUED && d->slip_pause && d->slip_bg.active) {
   /* p.59: PLAY cancels the slip pause from the position reached in the background. */
   d->slip_pause = 0; slip_return(d, now); start(d, now);
  } else if (d->state == XZ_XP_CUED) start(d, now);
  else if (d->state == XZ_XP_PLAYING) pause_at_heard(d, now);
  else if (d->state == XZ_XP_SAMPLING) d->state = XZ_XP_PLAYING;
  break;
 case XZ_EXTRA_IN_CUE: cue_button(d, e->pressed, now); break;
 case XZ_EXTRA_IN_HOT_CUE: pad(p, d, e, now); break;
 case XZ_EXTRA_IN_REVERSE: reverse_button(d, e->pressed, now); break;
 case XZ_EXTRA_IN_SEARCH: {
  /* SEARCH (pp.40, 45, 47-48): during loop IN/OUT adjust each press moves that edge half a frame; paused,
     each press moves the position half a frame (CUE then sets it); playing, held, it scans the track. */
  int dir = e->hot_cue ? -1 : 1;
  if (!e->pressed) break;
  uint64_t half = d->r->rate / 150u;
  if (d->loop.edit && d->loop.active) {
   if (d->loop.edit == 1) {
    uint64_t lo = d->loop.anchor > d->r->rate * 30u / 75u ? d->loop.anchor - d->r->rate * 30u / 75u : 0;
    uint64_t hi = d->loop.anchor + d->r->rate * 30u / 75u;
    uint64_t to = dir > 0 ? d->loop.in + half : d->loop.in > half ? d->loop.in - half : 0;
    if (to < lo) to = lo;
    if (to > hi) to = hi;
    if (to < d->loop.out) d->loop.in = to;
   } else {
    uint64_t to = dir > 0 ? d->loop.out + half : d->loop.out > half ? d->loop.out - half : 0;
    if (to > d->loop.in && to <= d->r->frames) d->loop.out = to;
   }
   d->loop.edit_at = now;
   break;
  }
  if (d->state == XZ_XP_CUED || d->state == XZ_XP_ENDED) {
   uint64_t at = d->pos >> 32;
   uint64_t to = dir > 0 ? (at + half > d->r->frames ? d->r->frames : at + half) : (at > half ? at - half : 0);
   jump(d, to, now); d->state = to >= d->r->frames ? XZ_XP_ENDED : XZ_XP_CUED;
   break;
  }
  if (d->state == XZ_XP_PLAYING || d->state == XZ_XP_SAMPLING) { d->search = dir; jump(d, heard_now(d, now), now); }
  break;
 }
 case XZ_EXTRA_IN_JOG_TURN: jog(p,d,e->jog.delta,e->jog.speed,e->jog.speed_known,now,arrived); break;
 case XZ_EXTRA_IN_JOG_TOUCH: if(e->pressed&&d->vinyl)d->search=0;scratch_touch(d,e->pressed,now); break;
 case XZ_EXTRA_IN_SYNC: if (e->pressed) sync_request(p,d,!d->sync.requested,now); break;
 case XZ_EXTRA_IN_LOOP_IN: case XZ_EXTRA_IN_LOOP_OUT: case XZ_EXTRA_IN_RELOOP:
 case XZ_EXTRA_IN_LOOP_HALF: case XZ_EXTRA_IN_LOOP_DOUBLE: loop_button(p, d, e, now); break;
 case XZ_EXTRA_IN_SEEK: {
  if(d->sync.requested){d->sync_hold=1;d->sync.locked=0;}
  uint64_t to = to_frames(d, e->seek_ms);
  if (d->loop.active && (to < d->loop.in || to >= d->loop.out)) d->loop.active = 0;
  jump(d, to, now);
  if (!streaming(d->state)) d->state = to >= d->r->frames ? XZ_XP_ENDED : XZ_XP_CUED;
  break;
 }
 default: break;
 }
}

static int take(struct xz_xp_players *p,struct deck *d, struct queued_control *q) {
 pthread_mutex_lock(&d->queue_lock);
 SP_ONLY(d->sp.queue_depth=d->count;if(d->count>d->sp.queue_max)d->sp.queue_max=d->count);
 int got = d->count > 0;
 if (got) {
  *q = d->queue[d->head];
  /* A producer can observe recovery after this worker sampled the old route.
     Leave that fresh command queued until the next step reconciles the route. */
  if(q->epoch!=d->epoch && q->epoch==p->env.sink.link(p->env.sink.ctx))got=0;
  /* Stale loads must release their references even while current loads wait for retire slots. */
  else if (q->epoch==d->epoch && q->event.kind == XZ_EXTRA_IN_LOAD &&
      d->retired_count >= XZ_XP_RETIRE_SLOTS) got = 0;
  else { d->head = (d->head + 1) % QUEUE_SLOTS; d->count--;
   if(q->event.kind==XZ_EXTRA_IN_MODE_SET)d->mode_pending[q->event.hot_cue/2]--;
   SP_ONLY(d->sp.controls++;d->sp.last_arrival=q->at;d->sp.arrival_seen=1); }
 }
 pthread_mutex_unlock(&d->queue_lock);
 return got;
}

static void completed_output(struct deck *d,const xz_usb_deck_position *q){
 struct output_history *h=&d->output;h->rate_valid=h->beat_valid=h->key_valid=0;
 uint64_t end=q->source_completed_output_frames;
 if(!end||end>q->completed_frames||end<h->completed||q->applied_generation!=d->generation){h->crossing_valid=0;return;}
 for(unsigned i=0;i<h->count;i++){
  const struct output_span *s=&h->span[(h->head+i)%OUTPUT_SPANS];
  if(end>s->begin&&end<=s->end){h->key=s->key;h->key_valid=1;break;}
 }
 const struct analysis *a=d->analysis;
 for(unsigned i=0;i<h->count;i++){
  const struct output_span *s=&h->span[(h->head+i)%OUTPUT_SPANS];uint64_t source,first;
  if(s->end<=h->completed||s->begin>=end)continue;
  uint64_t begin=h->completed>s->begin?h->completed:s->begin;
  uint64_t last=end<s->end?end:s->end;
  if(begin!=h->completed||!output_source(s,begin,&first)||
     (h->completed&&first!=h->completed_source))h->crossing_valid=0;
  if(!output_source(s,last,&source)){h->crossing_valid=0;h->completed=last;continue;}
  if(!s->loop_out&&source>(d->r->frames<<32))source=d->r->frames<<32;
  h->completed=last;h->completed_source=source;
  if(a&&a->grid_count){
  double ms=(double)source/(double)ONE*1000./d->r->rate;
  unsigned lo=0,hi=a->grid_count;
  while(lo<hi){unsigned mid=lo+(hi-lo)/2;if(a->grid[mid].ms<=ms)lo=mid+1;else hi=mid;}
  if(s->loop_out>s->loop_in&&(!lo||(double)a->grid[lo-1].ms*d->r->rate/1000.<(double)s->loop_in/ONE)){
   lo=0;hi=a->grid_count;
   while(lo<hi){unsigned mid=lo+(hi-lo)/2;if((double)a->grid[mid].ms*d->r->rate/1000.<(double)s->loop_out/ONE)lo=mid+1;else hi=mid;}
  }
  if(lo){
  h->beat_ms=a->grid[lo-1].ms;
  double beat=(double)h->beat_ms*d->r->rate/1000.,crossing;
  if(output_crossing(s,begin,last,beat,&crossing)){
   h->crossing=crossing;h->crossing_ms=h->beat_ms;h->crossing_valid=1;
  }
  if(s->loop_out>s->loop_in&&(beat<(double)s->loop_in/ONE||beat>=(double)s->loop_out/ONE))h->crossing_valid=0;
  }else h->crossing_valid=0;
  }else h->crossing_valid=0;
  if(last==end){
   if((source>>32)!=q->source_after_completed){h->key_valid=h->crossing_valid=0;return;}
   /* The published applied rate is the tempo part: a jog bend or the SYNC trim moves the beat position
      (an offset), never the tempo a native follower takes (stock S8). */
   h->rate=(double)(s->tempo_step?s->tempo_step:s->step)/(double)ONE*XZ_USB_RATE/d->r->rate;h->rate_valid=1;
   h->beat_valid=h->crossing_valid&&h->crossing_ms==h->beat_ms&&h->crossing<=(double)end;
   if(h->beat_valid)h->beat_age=(double)end-h->crossing;
   return;
  }
 }
 h->crossing_valid=0;
}

/* Source tags are a USB completion-boundary proxy, not an exact DAC-time observation. */
static void completed_timeline(struct deck *d,const xz_usb_deck_position *q,uint64_t wire_completed,uint32_t now){
 if(q->completed_frames<d->completed||q->source_completed_output_frames<d->timeline_output_frames)return;
 d->completed=q->completed_frames;
 if(!q->source_completed_output_frames||q->source_completed_output_frames==d->timeline_output_frames)return;
 completed_output(d,q);
 uint64_t frames=q->source_completed_output_frames-d->timeline_output_frames;
 int valid=q->source_completed_output_frames>d->timeline_output_frames&&
  q->source_completed_output_frames<=q->completed_frames&&q->source_after_completed<=d->r->frames&&d->r->rate;
 double distance=(double)q->source_after_completed-(double)d->heard;
 double max_rate=d->state==XZ_XP_SCRATCHING?8.1:3.1;
 /* Reverse playback inside a loop winds the opposite way; its tags hold the last position only. */
 if(valid&&d->state!=XZ_XP_SCRATCHING&&reversed(d)&&d->loop.active)valid=0;
 if(valid&&d->state!=XZ_XP_SCRATCHING&&!reversed(d)&&d->loop.active){
  double length=(double)(d->loop.out-d->loop.in);
  /* More than one possible winding cannot be recovered from an opaque end tag. */
  if(!length||(double)frames*d->r->rate/XZ_USB_RATE*max_rate>=length)valid=0;
  else if(distance<0){distance+=length;d->timeline_revision++;}
 }
 double rate=valid?distance*XZ_USB_RATE/((double)frames*d->r->rate):0;
 if(!isfinite(rate)||fabs(rate)>max_rate||(d->state!=XZ_XP_SCRATCHING&&(reversed(d)?rate>0:rate<0)))valid=0;
 d->heard_rate=valid?rate:0;d->timeline_valid=valid;
 if(q->source_after_completed<=d->r->frames){d->heard=q->source_after_completed;d->heard_at=now;d->timeline_anchor_valid=1;}
 d->timeline_output_frames=q->source_completed_output_frames;
 d->timeline_sink_frame=q->source_completed_wire_frame<=wire_completed?q->source_completed_wire_frame:0;
 d->timeline_wire_completed=wire_completed;
}
/* Lower envelope of (observation time - wire time), allowed to rise 0.5 ms a second for the
   USB and CPU clocks drifting apart. A new route restarts the wire count. publish_lock. */
static void wire_origin_observe(struct xz_xp_players *p,uint32_t route,uint64_t wire,uint32_t at){
 if(!wire)return;
 double sample=(double)at-(double)wire*1000./XZ_USB_RATE;
 if(!p->wire_origin_valid||route!=p->wire_origin_route||wire<p->wire_origin_seen){
  p->wire_origin_ms=sample;p->wire_origin_valid=1;p->wire_origin_route=route;
 }else{
  p->wire_origin_ms+=(double)(uint32_t)(at-p->wire_origin_at)*.0005;
  if(sample<p->wire_origin_ms)p->wire_origin_ms=sample;
 }
 p->wire_origin_seen=wire;p->wire_origin_at=at;
}
static int wire_time_ms(struct xz_xp_players *p,uint32_t route,uint64_t wire,double *ms){
 pthread_mutex_lock(&p->publish_lock);
 int ok=p->wire_origin_valid&&p->wire_origin_route==route&&wire&&wire<=p->wire_origin_seen;
 if(ok)*ms=p->wire_origin_ms+(double)wire*1000./XZ_USB_RATE;
 pthread_mutex_unlock(&p->publish_lock);
 return ok;
}
static int shared_position(struct xz_xp_players *p,uint32_t route,xz_usb_position *position,uint32_t *at,int observed){
 pthread_mutex_lock(&p->publish_lock);
 if(observed)wire_origin_observe(p,route,position->wire_completed_frames,*at);
 if(observed){
  int newer_route=!p->sync_position_route||(int32_t)(route-p->sync_position_route)>0;
  int current=route==p->sync_position_route&&(int32_t)(*at-p->sync_position_at)>=0&&
   position->wire_completed_frames>=p->sync_position.wire_completed_frames;
  if(current)for(unsigned i=0;i<XZ_EXTRA_DECKS;i++){
   if((int32_t)(position->requested_generation[i]-p->sync_position.requested_generation[i])<0||
      (int32_t)(position->deck[i].applied_generation-p->sync_position.deck[i].applied_generation)<0)current=0;
  }
  if(newer_route||current){p->sync_position=*position;p->sync_position_route=route;p->sync_position_at=*at;}
 }else if(p->sync_position_route==route&&(uint32_t)(*at-p->sync_position_at)<=150u){
  *position=p->sync_position;*at=p->sync_position_at;observed=1;
 }
 pthread_mutex_unlock(&p->publish_lock);return observed;
}
static int emergency_loop(struct xz_xp_players *p,struct deck *d,uint32_t now){
 if(d->state!=XZ_XP_PLAYING||d->emergency.active||reversed(d)||d->search)return 0;
 double rate=speed(d,now);uint64_t source_length=(uint64_t)llround(4.*beat_frames(d,d->emergency.after));
 if(rate<=0||!source_length||d->emergency.after<source_length)return 0;
 double frames=(double)source_length*XZ_USB_RATE/(d->r->rate*rate);
 if(frames<1||frames>EMERGENCY_FRAMES||frames>d->emergency.count)return 0;
 unsigned length=(unsigned)llround(frames);if(!length||length>d->emergency.count)return 0;
 d->emergency.active=1;d->emergency.length=length;d->emergency.read=0;
 d->emergency.start=(d->emergency.head+EMERGENCY_FRAMES-length)%EMERGENCY_FRAMES;
 d->loop.in=d->emergency.after-source_length;d->loop.out=d->emergency.after;
 d->loop.has_in=d->loop.valid=d->loop.active=1;d->loop.edit=0;
 d->sync_have_master=0;d->sync.locked=0;d->sync_trim=0;d->ramp.active=0;
 jump(d,d->loop.in,now);xz_xp_notice(p,"EMERGENCY LOOP / LOAD NEXT TRACK");return 1;
}
static void stream(struct xz_xp_players *p, struct deck *d, uint32_t now) {
 const struct xz_xp_sink *s = &p->env.sink;
 xz_usb_position position={0};
 uint64_t staged = 0;
 int applied = 0;
 uint32_t observation_at=now;
 int observed=SP_POSITION(p,d,&position)==XZ_USB_OK;
 if(atomic_load(&p->sync_requests))observed=shared_position(p,d->epoch,&position,&observation_at,observed);
 SP_ONLY(if(observed)sp_position_result(d,&position,observation_at));
 if (observed) {
  const xz_usb_deck_position *q = &position.deck[d->index];
  d->sync_wire_submitted=position.wire_submitted_frames;
  applied = q->applied_generation == d->generation;
  if(!applied)d->output.key_valid=0;
  if (applied) {
   if(d->underrun!=q->underrun_frames){output_reset(&d->output);key_history_reset(d);}
   d->underrun=q->underrun_frames;
   staged = q->staged_frames;
   if ((q->source_valid & 2) && q->completed_frames != d->completed) {
    completed_timeline(d,q,position.wire_completed_frames,observation_at);
   }
   if (d->state!=XZ_XP_SCRATCHING && !d->loop.active && !d->pending && d->pos >> 32 >= d->r->frames && q->completed_frames >= d->submitted) {
    d->state = XZ_XP_ENDED; d->pos = d->r->frames << 32; d->heard = d->r->frames;
    return;
   }
  }
 }
 if(applied)sync_update(p,d,&position,now);
 else if(d->sync.requested){
  uint64_t queued=d->submitted>d->completed?d->submitted-d->completed:0;
  uint32_t age=now-d->heard_at;
  if(d->timeline_valid&&d->timeline_anchor_valid&&!d->need_flush&&age<=150u&&
     (double)age*XZ_USB_RATE/1000.<queued){
   xz_usb_position cached={.wire_submitted_frames=d->sync_wire_submitted};sync_update(p,d,&cached,now);
  }else sync_update(p,d,NULL,now);
 }
 /* A SYNC jump targets this pass's wire horizon; submit it before that horizon moves. */
 if(d->need_flush){prepare_flush(p,d,now,d->epoch);applied=0;staged=0;}
 if(scratch_tail_done(p,d,now)){
  /* Resumed playback must be primed in this pass. An extra worker interval adds
     silence/phase lag after a tail whose last block has already completed. */
  prepare_flush(p,d,now,d->epoch);applied=0;staged=0;
 }
 if(d->need_flush||!streaming(d->state))return;
 for (int blocks = 0; blocks < 16; ++blocks) {
  if (!d->pending) {
   if ((d->state!=XZ_XP_SCRATCHING&&d->pos >> 32 >= d->r->frames) ||
       d->submitted - (applied ? staged : 0) >= XZ_XP_AHEAD_FRAMES) return;
   if(d->sync.requested&&d->sync.tempo_matched&&!d->emergency.active&&d->sync_have_master&&!sync_snap_ready(d,now)&&!d->sync_hold&&d->timeline_anchor_valid&&
      d->sync.master_scope==XZ_EXTRA_MASTER_SCOPE_SOFTWARE&&d->state==XZ_XP_PLAYING){
    struct sync_clock master;
    pthread_mutex_lock(&p->publish_lock);master=p->clock[d->sync_master_id];pthread_mutex_unlock(&p->publish_lock);
    if(master.route!=d->sync_master_clock.route||master.load!=d->sync_master_clock.load||
       master.generation!=d->sync_master_clock.generation||master.underrun!=d->sync_master_clock.underrun){
     d->sync_hold=1;d->sync_hold_reason=d->sync.reason=XZ_EXTRA_SYNC_DISCONTINUITY;d->sync.locked=0;return;
    }
    if(!master.available||(uint32_t)(now-master.at_ms)>150u){
     d->sync.locked=0;d->sync.reason=XZ_EXTRA_SYNC_WAIT_ANCHOR;
     d->sync_trim=0;d->sync.correction_ppm=0;goto render_ready;
    }
    double own_phase,period,phase,beat_step,nominal_bps;uint64_t remaining;
    uint64_t wire=d->timeline_sink_frame+d->submitted-d->timeline_output_frames;
    if(!grid_phase(d,d->pos>>32,&own_phase,&period)){
     d->sync.locked=0;d->sync.reason=XZ_EXTRA_SYNC_NO_GRID;
     d->sync_trim=0;d->sync.correction_ppm=0;goto render_ready;
    }
    /* Waiting at the produced horizon emits no speculative PCM. Current lock
       still refers only to the independently checked completed anchors. */
    if(!segment_at_wire(&master,wire,&phase,&beat_step,&remaining,&nominal_bps)){
     if(d->submitted-d->completed>2*XZ_USB_BLOCK_FRAMES)return;
     /* A prolonged absent forecast must not starve ordinary playback. Keep
        the last rate, suspend phase correction, and publish no lock. */
     d->sync.locked=0;d->sync.reason=XZ_EXTRA_SYNC_WAIT_ANCHOR;
     d->sync_trim=0;d->sync.correction_ppm=0;goto render_ready;
    }
    uint32_t choice;int selected=sync_selection(p,d,&choice);
    if(selected!=d->sync_master_id||choice!=d->sync_choice_seen){
     d->sync.locked=0;d->sync.reason=XZ_EXTRA_SYNC_WAIT_MASTER;
     d->sync_trim=0;d->sync.correction_ppm=0;goto render_ready;
    }
    int applied=sync_apply_base(p,d,selected,&choice,nominal_bps*6000./grid_bpm(d,d->pos>>32));
    if(applied<=0){
     d->sync.locked=0;d->sync.reason=applied<0?XZ_EXTRA_SYNC_ACQUIRING:XZ_EXTRA_SYNC_OUT_OF_RANGE;
     d->sync_trim=0;d->sync.correction_ppm=0;goto render_ready;
    }
    d->render_limit=remaining>XZ_USB_BLOCK_FRAMES?XZ_USB_BLOCK_FRAMES:(unsigned)remaining;
   }else if(d->sync.requested&&d->sync.tempo_matched&&!d->emergency.active&&d->sync_have_master&&!sync_snap_ready(d,now)&&!d->sync_hold&&d->timeline_anchor_valid&&
            d->sync.master_scope==XZ_EXTRA_MASTER_SCOPE_NATIVE&&d->state==XZ_XP_PLAYING&&d->sync_native_bpm>0){
    double own_phase,period;
    if(!grid_phase(d,d->pos>>32,&own_phase,&period)){
     d->sync.locked=0;d->sync.reason=XZ_EXTRA_SYNC_NO_GRID;
     d->sync_trim=0;d->sync.correction_ppm=0;goto render_ready;
    }
    uint32_t choice;int selected=sync_selection(p,d,&choice);
    if(selected>=0||choice!=d->sync_choice_seen){
     d->sync.locked=0;d->sync.reason=XZ_EXTRA_SYNC_WAIT_MASTER;
     d->sync_trim=0;d->sync.correction_ppm=0;goto render_ready;
    }
    int applied=sync_apply_base(p,d,selected,&choice,d->sync_native_bpm*100./grid_bpm(d,d->pos>>32));
    if(applied<=0){
     d->sync.locked=0;d->sync.reason=applied<0?XZ_EXTRA_SYNC_ACQUIRING:XZ_EXTRA_SYNC_OUT_OF_RANGE;
     d->sync_trim=0;d->sync.correction_ppm=0;goto render_ready;
    }
   }
render_ready:
   d->render_tempo_rate=nominal(d);
   if (!render(d, speed(d, now) * ramp_factor(d, now))) {
    if(emergency_loop(p,d,now))return;
    pause_at_heard(d, now); xz_xp_notice(p, "TRACK CANNOT BE READ");
    return;
   }
   if (!d->pending) return;
  }
#ifdef XZ_XP_SERVICE_PROOF
  uint32_t sp_begin=p->env.now_ms();
#endif
  SP_ONLY(sp_capture(d,sp_begin));
  xz_usb_result r = s->submit(s->ctx, d->epoch, d->index, d->generation, d->pcm, d->pending, d->pending_after);
  SP_ONLY(sp_service(&d->sp,XP_SP_SUBMIT,r,sp_begin,p->env.now_ms()));
  if (r == XZ_USB_OK) { accepted_segment(d);accepted_scratch(d);d->submitted += d->pending; d->pending = 0; continue; }
  if (r == XZ_USB_STALE) {
   if(d->scratch.releasing){scratch_release(d,now);xz_xp_notice(p,"SCRATCH RELEASE TRUNCATED");}
   else {d->pos=d->block_start;d->pending=0;d->need_flush=1;key_history_reset(d);}
  }
  else if (r != XZ_USB_AGAIN && r != XZ_USB_BUSY) { d->pos = d->block_start; d->pending = 0; key_history_reset(d); }
  return;
 }
}

void xz_xp_step(struct xz_xp_players *p, int index) {
 if (!p || index < 0 || index >= XZ_EXTRA_DECKS) return;
 struct deck *d = &p->deck[index];
 const struct xz_xp_sink *s = &p->env.sink;
 uint32_t now = p->env.now_ms();
 SP_ONLY(struct xp_sp_private *v=&d->sp;
  v->step_begin=now;v->controls=0;v->current[XP_SP_CURRENT_WORKER_START]=now;
  uint32_t gap=v->have_start?sp_elapsed(now,v->last_start,&v->clock_future):0;
  v->current[XP_SP_CURRENT_WORKER_GAP]=gap;
  if(gap!=XP_SP_UNKNOWN&&gap>v->current[XP_SP_CURRENT_MAX_WORKER_GAP])v->current[XP_SP_CURRENT_MAX_WORKER_GAP]=gap;
  v->last_start=now;v->have_start=1;
  if(gap==XP_SP_UNKNOWN||gap>=8)sp_history(d,now,2));
 if (d->loop.edit && now - d->loop.edit_at >= 10000u) { d->loop.edit = 0; loop_pad_store(p,d); }
 collect(d, now);
 uint32_t link = s->link(s->ctx);
 if(p->ram.step&&d->r->media&&p->ram.step(p->ram.context,(unsigned)index,d->r->media,d->state==XZ_XP_PLAYING)<0){
  pause_at_heard(d,now);d->state=XZ_XP_ERROR;xz_xp_notice(p,"RAM HANDOFF FAILED / LOAD ANOTHER TRACK");
 }
 if (link != d->epoch) {
  /* A new route instance starts its queues at the initial generation; a lost one took the audio. */
  if (streaming(d->state)) pause_at_heard(d, now);
  SP_ONLY(d->sp.contact=0;d->sp.history_dirty=1);
  d->epoch = link; d->generation = XZ_USB_INITIAL_GENERATION;d->key.mt_owned=0;
  d->timeline_revision++;d->timeline_valid=d->timeline_anchor_valid=0;d->heard_rate=0;
  output_reset(&d->output);key_history_reset(d);
  d->need_flush = 0; d->pending = 0; d->submitted = d->completed = 0;
  d->loop.in_down=d->loop.held=0;
  if(d->pad_hold==PAD_HOLD_LOOP){d->loop.active=0;d->loop.edit=0;d->active_pad=-1;}
  if(d->slip_reverse)d->reverse=0;
  memset(&d->gate,0,sizeof d->gate);
  d->auto_button.down=0;d->track_prev_known=0;d->search=0;d->control_down=0;d->pad_hold=PAD_HOLD_NONE;d->slip_pad=-1;d->slip_bg.active=d->slip_reverse=0;
 }
 (void)sync_grid_refresh(p,d);
 uint32_t operator_epoch=observe_operator(p);
 if(operator_epoch){pthread_mutex_lock(&p->publish_lock);consume_operator_locked(p,operator_epoch);pthread_mutex_unlock(&p->publish_lock);}
 struct queued_control q;
 /* Fixed queue-capacity budget: producers cannot prolong a worker drain indefinitely. */
 for(unsigned controls=0;controls<CONTROL_BUDGET&&take(p,d,&q);controls++) {
  /* Keep the captured route gate across LOAD's potentially blocking resolver. */
  if(q.event.kind==XZ_EXTRA_IN_LOAD&&!q.event.load.route_epoch)q.event.load.route_epoch=q.epoch;
  if(q.epoch && q.epoch==d->epoch && q.epoch==s->link(s->ctx)){
   SP_ONLY(if(q.event.kind==XZ_EXTRA_IN_JOG_TOUCH){if(q.event.pressed&&!d->sp.contact)d->sp.attempt++;d->sp.contact=!!q.event.pressed;d->sp.history_dirty=1;});
   d->command_settings=q.settings;d->command_jump_beats=q.jump_beats;
   command(p,d,&q.event,now,q.at,q.master_choice,q.operator_epoch);
   SP_ONLY(sp_capture(d,now));
  }
  if(q.master_choice){
   pthread_mutex_lock(&p->publish_lock);
   if(p->master_admitted==q.master_choice&&atomic_load(&p->sync_choice_epoch)!=q.master_choice)p->admitted_operator=0;
   pthread_mutex_unlock(&p->publish_lock);
  }
  if(q.event.kind==XZ_EXTRA_IN_LOAD)xz_extra_order_release(q.event.load.order);
 }
 if(d->state==XZ_XP_SCRATCHING&&d->scratch.group_open&&(int32_t)(now-d->scratch.at)>0)scratch_seal(d);
 d->sync_capture=atomic_load(&p->sync_requests)!=0;
 ramp_step(d,now);direction_step(d,now);master_handoff(p,d,now);
 if(!streaming(d->state)&&d->sync.requested){xz_usb_position none={0};sync_update(p,d,&none,now);}
 if(link&&d->loop.in_down&&!d->loop.held&&now-d->loop.in_at>=1000u){
  struct xz_extra_deck_event held={.kind=XZ_EXTRA_IN_LOOP_IN,.pressed=1,.held=1};
  if(loaded(d->state))loop_button(p,d,&held,now);
 }
 /* Expiry precedes an initial transition retry, where no tail PCM was accepted.
    Submitted tails first observe completion in stream before deciding expiry. */
 if(d->need_flush)(void)scratch_tail_done(p,d,now);
 prepare_flush(p,d,now,link);
 if (streaming(d->state) && link && !d->need_flush) stream(p, d, now);
 if(scratch_tail_done(p,d,now))flush(p,d);
 if(!d->key.desired&&fabsf(d->key.shift.ratio-1.f)<=.0001f)atomic_fetch_and(&p->key_active,~(1u<<d->index));
 publish(p, d, now);
 SP_ONLY(struct xp_sp_private *v=&d->sp;uint32_t end=p->env.now_ms();
  uint32_t cost=sp_elapsed(end,v->step_begin,&v->clock_future);
  v->current[XP_SP_CURRENT_WORKER_COST]=cost;
  if(cost!=XP_SP_UNKNOWN&&cost>v->current[XP_SP_CURRENT_MAX_WORKER_COST])v->current[XP_SP_CURRENT_MAX_WORKER_COST]=cost;
  sp_capture(d,end);
  if(v->history_dirty||(uint32_t)(end-v->history_at)>=16u)sp_history(d,end,1);
  struct xp_sp_current_block *b=&xz_mods_xp_service_proof_v1.deck[index].current;
  sp_publish(&b->seq,b->word,v->current,XP_SP_CURRENT_WORDS+XP_SP_SERVICE_WORDS));
}

/* One ordered slot per mode remains available beyond ordinary traffic. Admit
   a global change to both FIFOs together, never from one worker to the other. */
static void queue_epoch(struct xz_xp_players *p,struct deck *d,uint32_t epoch){
 if(d->queue_epoch==epoch)return;
 d->queue_epoch=epoch;d->release_owed=0;
 d->jump_input.down=d->jump_input.owned=d->jump_input.mode_down=0;
 pthread_mutex_lock(&p->publish_lock);
 d->mode_target[0]=p->view[d->index].quantize;d->mode_target[1]=p->view[d->index].remain;
 d->mode_target[2]=p->view[d->index].auto_cue;
 pthread_mutex_unlock(&p->publish_lock);
}
static int mode_input(struct xz_xp_players *p,const struct xz_extra_deck_event *e,uint32_t epoch){
 if(!e->pressed)return 1;
 int mode=e->kind==XZ_EXTRA_IN_MODE_SET?e->hot_cue/2:e->kind==XZ_EXTRA_IN_QUANTIZE?0:e->kind==XZ_EXTRA_IN_TIME_MODE?1:2;
 if(mode<0||mode>2||(e->kind==XZ_EXTRA_IN_MODE_SET&&(e->hot_cue<0||e->hot_cue>5)))return 0;
 int first=e->shift||e->kind==XZ_EXTRA_IN_MODE_SET?e->deck:0;
 int last=e->shift||e->kind==XZ_EXTRA_IN_MODE_SET?e->deck:1;
 pthread_mutex_lock(&p->deck[0].queue_lock);pthread_mutex_lock(&p->deck[1].queue_lock);
 int ok=epoch==p->env.sink.link(p->env.sink.ctx);
 if(ok){queue_epoch(p,&p->deck[0],epoch);queue_epoch(p,&p->deck[1],epoch);}
 for(int i=first;i<=last;i++){
  struct deck *d=&p->deck[i];
  ok=ok&&(d->count<XZ_XP_QUEUE||!d->mode_pending[mode])&&d->count+1+release_count(d->release_owed)<=QUEUE_SLOTS;
 }
 if(ok){
  int on=e->kind==XZ_EXTRA_IN_MODE_SET?e->hot_cue%2:!p->deck[e->deck].mode_target[mode];
  for(int i=first;i<=last;i++){
   struct deck *d=&p->deck[i];struct xz_extra_deck_event set={.deck=i,.kind=XZ_EXTRA_IN_MODE_SET,.pressed=1,.hot_cue=mode*2+on};
   d->queue[(d->head+d->count)%QUEUE_SLOTS]=(struct queued_control){.event=set,.epoch=epoch,.at=p->env.now_ms()};
   d->count++;d->mode_pending[mode]++;d->mode_target[mode]=on;
  }
 }
 pthread_mutex_unlock(&p->deck[1].queue_lock);pthread_mutex_unlock(&p->deck[0].queue_lock);return ok;
}
int xz_xp_input(struct xz_xp_players *p, const struct xz_extra_deck_event *e) {
 if (!p || !e || e->deck < 0 || e->deck >= XZ_EXTRA_DECKS || (unsigned)e->kind >= XZ_EXTRA_INPUT_COUNT) return 0;
 /* The knob is a physical side setting, not queued transport work. Both native
    selection and a disconnected route must retain its latest observed position. */
 if(e->kind==XZ_EXTRA_IN_VINYL_SPEED){
  int32_t at=e->tempo_position;
  atomic_store_explicit(&p->deck[e->deck].vinyl_level,
   at>XZ_EXTRA_TEMPO_SPAN?XZ_EXTRA_TEMPO_SPAN:at< -XZ_EXTRA_TEMPO_SPAN? -XZ_EXTRA_TEMPO_SPAN:at,memory_order_relaxed);
  return 1;
 }
 uint32_t epoch=p->env.sink.link(p->env.sink.ctx);
 if(!epoch){SP_ONLY(sp_input(&p->deck[e->deck],e,p->env.now_ms(),0,atomic_load(&p->deck[e->deck].published_state),0,XP_SP_NO_ROUTE,XP_SP_UNKNOWN,XP_SP_UNKNOWN,0));return 0;}
 if(e->kind==XZ_EXTRA_IN_QUANTIZE||e->kind==XZ_EXTRA_IN_TIME_MODE||e->kind==XZ_EXTRA_IN_AUTO_CUE||e->kind==XZ_EXTRA_IN_MODE_SET)
  return mode_input(p,e,epoch);
 if (e->kind == XZ_EXTRA_IN_LOAD && !p->env.resolve) { SP_ONLY(sp_input(&p->deck[e->deck],e,p->env.now_ms(),epoch,atomic_load(&p->deck[e->deck].published_state),0,XP_SP_NO_RESOLVER,XP_SP_UNKNOWN,XP_SP_UNKNOWN,0));xz_xp_notice(p, "TRACK NOT FOUND"); return 0; }
 struct deck *d = &p->deck[e->deck];
 int state = atomic_load(&d->published_state);
 int button = e->kind != XZ_EXTRA_IN_TEMPO && e->kind != XZ_EXTRA_IN_JOG_TURN &&
              e->kind != XZ_EXTRA_IN_SEEK && e->kind != XZ_EXTRA_IN_LOAD && e->kind != XZ_EXTRA_IN_VINYL_SPEED;
 int release = button && !e->pressed;
 int setting = e->kind == XZ_EXTRA_IN_TEMPO || e->kind == XZ_EXTRA_IN_TEMPO_RANGE ||
               e->kind == XZ_EXTRA_IN_TEMPO_RESET || e->kind == XZ_EXTRA_IN_VINYL ||
               e->kind == XZ_EXTRA_IN_MASTER_TEMPO || e->kind == XZ_EXTRA_IN_PAD_MODE ||
               e->kind == XZ_EXTRA_IN_REVERSE || e->kind == XZ_EXTRA_IN_SLIP || e->kind == XZ_EXTRA_IN_VINYL_SPEED ||
               e->kind == XZ_EXTRA_IN_QUANTIZE || e->kind == XZ_EXTRA_IN_MODE_SET || (e->kind==XZ_EXTRA_IN_SYNC&&e->shift) ||
               e->kind == XZ_EXTRA_IN_TIME_MODE || e->kind == XZ_EXTRA_IN_AUTO_CUE;
 if ((state == XZ_XP_EMPTY || state == XZ_XP_ERROR) && !release && e->kind != XZ_EXTRA_IN_LOAD && e->kind!=XZ_EXTRA_IN_INSTANT_DOUBLES && !setting)
  {SP_ONLY(sp_input(d,e,p->env.now_ms(),epoch,state,0,XP_SP_STATE_REFUSED,XP_SP_UNKNOWN,XP_SP_UNKNOWN,0));return 0;}
 int master_input=p->master_observer&&e->kind==XZ_EXTRA_IN_MASTER&&e->pressed;
 uint32_t operator_epoch=master_input?observe_operator(p):0,master_choice=0;
 if(master_input&&!operator_epoch){xz_xp_notice(p,"MASTER REQUEST NOT READY");return 0;}
 pthread_mutex_lock(&d->queue_lock);
 if(epoch!=p->env.sink.link(p->env.sink.ctx)){SP_ONLY(sp_input(d,e,p->env.now_ms(),epoch,state,0,XP_SP_ROUTE_CHANGED,d->count,d->release_owed,0));pthread_mutex_unlock(&d->queue_lock);return 0;}
 queue_epoch(p,d,epoch);
 unsigned bit=release_bit(e),owed=d->release_owed;
 int completing=release && (owed&bit);
 if(bit){
  if(!e->pressed)owed&=~bit;
  else if(!e->held||(e->kind!=XZ_EXTRA_IN_HOT_CUE&&e->kind!=XZ_EXTRA_IN_REVERSE))owed|=bit;
 }
 int taken=(d->count<XZ_XP_QUEUE || completing) && d->count+1+release_count(owed)<=QUEUE_SLOTS;
 if(taken&&master_input){
  pthread_mutex_lock(&p->publish_lock);consume_operator_locked(p,operator_epoch);
  if(operator_epoch>=p->operator_floor)master_choice=reserve_master_locked(p,operator_epoch);
  taken=master_choice!=0;pthread_mutex_unlock(&p->publish_lock);
 }
 if (taken) {
  if(e->kind==XZ_EXTRA_IN_LOAD)xz_extra_order_retain(e->load.order);
   int jump_beats=0;
   if(e->kind==XZ_EXTRA_IN_PAD_MODE||e->kind==XZ_EXTRA_IN_HOT_CUE){
    struct xz_jump_input ji={.mode_button=-1,.pad=-1,.pad_page=d->input_pad_mode,.shift=e->shift,.operation=e->pressed?(e->held?1:0):2};
    if(e->kind==XZ_EXTRA_IN_PAD_MODE){
     ji.mode_button=e->hot_cue;
     if(e->pressed&&!e->held&&(!e->shift||e->hot_cue==XZ_EXTRA_PAD_BEAT_JUMP||e->hot_cue==XZ_EXTRA_PAD_HOT_CUE))
      d->input_pad_mode=e->shift&&e->hot_cue==XZ_EXTRA_PAD_HOT_CUE?XZ_EXTRA_PAD_NONE:e->hot_cue;
    }else ji.pad=e->hot_cue;
    struct xz_jump_settings cfg=d->settings.jump;
    /* No firmware fallback exists here: page-1 OFF resolves through shared stock defaults. */
    if(!d->jump_input.page&&!cfg.enabled)cfg=xz_jump_defaults();
    jump_beats=xz_jump_resolve(&d->jump_input,&cfg,&ji).signed_beats;
   }
   d->queue[(d->head+d->count)%QUEUE_SLOTS]=(struct queued_control){.event=*e,.epoch=epoch,.at=p->env.now_ms(),.master_choice=master_choice,.operator_epoch=operator_epoch,.settings=d->settings,.jump_beats=jump_beats};
  d->count++;d->release_owed=owed;
 }
 SP_ONLY(uint32_t at=taken?d->queue[(d->head+d->count-1)%QUEUE_SLOTS].at:p->env.now_ms();
  struct xp_sp_deck_wire *w=&xz_mods_xp_service_proof_v1.deck[e->deck];
  if(d->count>atomic_load(&w->counter[XP_SP_COUNTER_MAX_DEPTH]))atomic_store(&w->counter[XP_SP_COUNTER_MAX_DEPTH],d->count);
  sp_input(d,e,at,epoch,state,taken,taken?XP_SP_ADMITTED:XP_SP_FIFO_FULL,d->count,d->release_owed,completing));
 pthread_mutex_unlock(&d->queue_lock);
 return taken;
}

static int read_view(struct xz_xp_players *p,struct xz_extra_decks *out,struct xz_extra_analysis_hold *hold) {
 if (!p || !out) return 0;
 if(hold)for(unsigned i=0;i<XZ_EXTRA_DECKS;i++)if(hold->analysis[i])return 0;
 struct xz_master_observation actual={.deck=XZ_MASTER_UNKNOWN};
 if(p->master_observer)copy_master(p,&actual);
 pthread_mutex_lock(&p->publish_lock);
 struct xz_master_observation tempo_view[XZ_EXTRA_DECKS];
 memcpy(tempo_view,p->tempo_view,sizeof tempo_view);
 memcpy(out->deck, p->view, sizeof out->deck);
 for(unsigned i=0;i<XZ_EXTRA_DECKS;i++){
  struct analysis *a=p->view_analysis[i];
  if(hold&&a&&(out->deck[i].wave.columns||out->deck[i].grid)&&
     atomic_load_explicit(&a->readers,memory_order_relaxed)<UINT32_MAX){
   atomic_fetch_add_explicit(&a->readers,1,memory_order_relaxed);hold->analysis[i]=a;
  }else{out->deck[i].wave.columns=NULL;out->deck[i].grid=NULL;}
 }
 memcpy(out->notice, p->notice, sizeof out->notice);
 out->notice_at_ms = p->notice_at; out->notice_seq = p->notice_seq;
 out->snapshot_revision=atomic_load_explicit(&p->revision,memory_order_acquire);
 out->master_intent=master_intent_locked(p);
 pthread_mutex_unlock(&p->publish_lock);
 if(!observation_source_matches(out->deck,&out->master_intent,&actual))actual=(struct xz_master_observation){.deck=XZ_MASTER_UNKNOWN};
 out->master_actual=actual;
 if(p->master_observer)for(int i=0;i<2;i++){
  struct xz_extra_deck *v=&out->deck[i];v->master=v->loaded&&actual.deck==i+2;
  v->master_scope=v->master?XZ_EXTRA_MASTER_SCOPE_SOFTWARE|XZ_EXTRA_MASTER_SCOPE_NATIVE:0;
  if(v->sync.requested){
   if(!authority_same(&actual,&tempo_view[i])){
    v->sync.tempo_matched=0;v->sync.locked=0;v->sync.correction_ppm=0;
   }
   v->sync.quantum_unknown=v->quantize&&!v->quantize_den;
   if(actual.deck<0||v->sync.master_deck!=actual.deck){
    v->sync.locked=0;v->sync.correction_ppm=0;v->sync.tempo_matched=0;v->sync.master_paused=0;
    if(v->sync.source!=XZ_EXTRA_SYNC_SOURCE_EMERGENCY)v->sync.source=actual.deck==XZ_MASTER_NONE?XZ_EXTRA_SYNC_SOURCE_NONE:XZ_EXTRA_SYNC_SOURCE_CLOCK_LOST;
   }
   v->sync.master_deck=actual.deck>=0?actual.deck:-1;
   v->sync.master_scope=actual.deck<0?0:actual.deck<2?XZ_EXTRA_MASTER_SCOPE_NATIVE:XZ_EXTRA_MASTER_SCOPE_NATIVE|XZ_EXTRA_MASTER_SCOPE_SOFTWARE;
   if(actual.deck<0)v->sync.reason=actual.deck==XZ_MASTER_EXTERNAL?XZ_EXTRA_SYNC_NATIVE_MASTER_UNSUPPORTED:XZ_EXTRA_SYNC_WAIT_MASTER;
  }
 }
 return 1;
}
int xz_xp_worker_load(struct xz_xp_players *p,struct xz_worker_load out[2]) {
 if(!p||!out||pthread_mutex_trylock(&p->publish_lock))return 0;
 unsigned active=atomic_load(&p->key_active);
 for(unsigned i=0;i<2;i++){out[i]=p->view[i].worker_load;out[i].key_active=!!(active&(1u<<i));}
 pthread_mutex_unlock(&p->publish_lock);return 1;
}
void xz_xp_read(struct xz_xp_players *p,struct xz_extra_decks *out){(void)read_view(p,out,NULL);}
int xz_xp_read_held(struct xz_xp_players *p,struct xz_extra_decks *out,struct xz_extra_analysis_hold *hold){
 return hold?read_view(p,out,hold):0;
}
void xz_xp_release_analysis(struct xz_extra_decks *out,struct xz_extra_analysis_hold *hold){
 if(!hold)return;
 for(unsigned i=0;i<XZ_EXTRA_DECKS;i++){
  struct analysis *a=hold->analysis[i];hold->analysis[i]=NULL;
  if(out){out->deck[i].wave.columns=NULL;out->deck[i].grid=NULL;}
  if(a)atomic_fetch_sub_explicit(&a->readers,1,memory_order_release);
 }
}

enum xz_extra_wave_copy_result xz_xp_copy_wave(struct xz_xp_players *p, int deck,
 const struct xz_extra_wave_key *expected, uint32_t first, struct xz_extra_wave_chunk *out) {
 if(!p||!out||(unsigned)deck>=XZ_EXTRA_DECKS)return XZ_EXTRA_WAVE_COPY_INVALID;
 if(pthread_mutex_trylock(&p->publish_lock))return XZ_EXTRA_WAVE_COPY_BUSY;
 const struct xz_extra_deck *v=&p->view[deck];
 enum xz_extra_wave_copy_result result=XZ_EXTRA_WAVE_COPY_OK;
 struct xz_extra_wave_key key={0};
 key.media_generation=v->timeline.media_generation;key.route_epoch=v->timeline.route_epoch;
 key.load_revision=v->timeline.load_revision;key.source=v->source;
 key.column_count=v->wave.count;key.span_ms=v->wave.span_ms;key.normalization=v->wave.normalization;
 if(!v->loaded||!(v->known&XZ_EXTRA_KNOWN(XZ_EXTRA_F_WAVE))||
    !v->wave.columns||!key.column_count||!key.span_ms)
  result=expected?XZ_EXTRA_WAVE_COPY_CHANGED:XZ_EXTRA_WAVE_COPY_ABSENT;
 else if(expected&&(expected->media_generation!=key.media_generation||expected->route_epoch!=key.route_epoch||
  expected->load_revision!=key.load_revision||expected->source!=key.source||
  expected->column_count!=key.column_count||expected->span_ms!=key.span_ms||expected->normalization!=key.normalization))
  result=XZ_EXTRA_WAVE_COPY_CHANGED;
 else if(first>key.column_count)result=XZ_EXTRA_WAVE_COPY_INVALID;
 if(result==XZ_EXTRA_WAVE_COPY_OK){
  uint32_t count=key.column_count-first;
  if(count>XZ_EXTRA_WAVE_CHUNK_COLUMNS)count=XZ_EXTRA_WAVE_CHUNK_COLUMNS;
  out->key=key;out->first=first;out->count=count;
  if(count)memcpy(out->columns,v->wave.columns+first,count*sizeof out->columns[0]);
 }
 pthread_mutex_unlock(&p->publish_lock);
 return result;
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
#ifdef XZ_XP_SERVICE_PROOF
 uint32_t sp_id=atomic_fetch_add(&sp_instance,1)+1;
 if(!sp_id)sp_id=atomic_fetch_add(&sp_instance,1)+1;
#endif
 int mutexes = 0;
 if (pthread_mutex_init(&p->publish_lock, NULL)) { free(p); return NULL; }
 for (; mutexes < XZ_EXTRA_DECKS; ++mutexes) {
  struct deck *d = &p->deck[mutexes];
  d->settings=(struct xz_performance_settings){.revision=1,.jump=xz_jump_defaults()};
  d->index = mutexes; d->generation = XZ_USB_INITIAL_GENERATION;
  d->tempo_range = 10;
  d->sync.master_deck=-1;
  d->active_pad = d->slip_pad = -1;
  d->vinyl = !!env->start_in_vinyl; d->quantize = !!env->start_with_quantize;
  d->auto_cue = 1; d->loop_pad = -1;
  d->mode_target[0]=d->quantize;d->mode_target[1]=d->remain;d->mode_target[2]=d->auto_cue;
  xz_stretch_reset(&d->stretch);key_history_reset(d);
  atomic_init(&d->published_state, XZ_XP_EMPTY);
  atomic_init(&d->vinyl_level,0);
  if (!(d->r = calloc(1, sizeof *d->r)) || pthread_mutex_init(&d->queue_lock, NULL)) { free(d->r); break; }
 }
 if (mutexes < XZ_EXTRA_DECKS) {
  while (mutexes--) { pthread_mutex_destroy(&p->deck[mutexes].queue_lock); free(p->deck[mutexes].r); }
  pthread_mutex_destroy(&p->publish_lock); free(p); return NULL;
 }
#ifdef XZ_XP_SERVICE_PROOF
 unsigned sp_free=0;p->sp_id=sp_id;
 p->sp_enabled=atomic_compare_exchange_strong(&sp_owner,&sp_free,sp_id);
#endif
 SP_ONLY(for(int i=0;i<XZ_EXTRA_DECKS;i++){
  struct deck *d=&p->deck[i];d->sp.current[XP_SP_CURRENT_INSTANCE]=sp_id;d->r->sp=&d->sp;d->r->sp_clock=env->now_ms;
  for(unsigned op=0;op<XP_SP_OPS;op++)d->sp.current[XP_SP_CURRENT_WORDS+op*8]=XP_SP_UNKNOWN;
  struct xp_sp_deck_wire *w=&xz_mods_xp_service_proof_v1.deck[i];
  for(unsigned j=0;j<XP_SP_COUNTER_WORDS;j++)atomic_store(&w->counter[j],0);
  uint32_t zero[XP_SP_LATCH_WORDS]={0};
  sp_publish(&w->current.seq,w->current.word,d->sp.current,XP_SP_CURRENT_WORDS+XP_SP_SERVICE_WORDS);
  sp_publish(&w->latest.seq,w->latest.word,zero,XP_SP_INPUT_WORDS);sp_publish(&w->last_refusal.seq,w->last_refusal.word,zero,XP_SP_INPUT_WORDS);
  for(unsigned h=0;h<XP_SP_HISTORY;h++)sp_publish(&w->history[h].seq,w->history[h].word,zero,XP_SP_HISTORY_WORDS);
  sp_publish(&w->first.seq,w->first.word,zero,XP_SP_LATCH_WORDS);
  sp_publish(&w->previous.seq,w->previous.word,zero,XP_SP_LATCH_WORDS);
 });
 atomic_init(&p->key_active,0);
 atomic_init(&p->quit, 0);
 atomic_init(&p->revision,0);
 atomic_init(&p->sync_master,-1);atomic_init(&p->sync_requests,0);
 atomic_init(&p->sync_choice_epoch,1);atomic_init(&p->sync_grid,0);
 atomic_init(&p->native_intent,XZ_MASTER_NONE);atomic_init(&p->explicit_intent,0);p->master_admitted=1;
 for(int i=0;i<XZ_EXTRA_DECKS;i++)p->view[i].jog_content_epoch=advance_jog_content_epoch(i);
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
 SP_ONLY(for(unsigned i=0;i<2;i++)atomic_fetch_add(&xz_mods_xp_service_proof_v1.deck[i].counter[XP_SP_COUNTER_START_REQUESTS],1));
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
 SP_ONLY(for(unsigned i=0;i<2;i++)atomic_fetch_add(&xz_mods_xp_service_proof_v1.deck[i].counter[XP_SP_COUNTER_STOP_REQUESTS],1));
 atomic_store(&p->quit, 1);
 while (p->threads) pthread_join(p->thread[--p->threads], NULL);
 if(p->ram.retire)for(unsigned i=0;i<XZ_EXTRA_DECKS;i++){
  struct deck *d=&p->deck[i];retire_reader(p,d);d->state=XZ_XP_EMPTY;
  publish(p,d,p->env.now_ms());
 }
}

void xz_xp_destroy(struct xz_xp_players *p) {
 if (!p) return;
 xz_xp_stop(p);
 for (int i = 0; i < XZ_EXTRA_DECKS; ++i) {
  struct deck *d = &p->deck[i];
  xz_extra_order_release(d->order);
  for(unsigned q=0;q<d->count;q++){struct xz_extra_deck_event *e=&d->queue[(d->head+q)%QUEUE_SLOTS].event;if(e->kind==XZ_EXTRA_IN_LOAD)xz_extra_order_release(e->load.order);}
  retire_reader(p,d);
  /* Teardown is off callback threads and cannot invalidate an outstanding hold.
     The owner has stopped new reads before destruction; release needs no p. */
  while(d->analysis&&atomic_load_explicit(&d->analysis->readers,memory_order_acquire))pause_ms(1);
  free_analysis(d->analysis);
  for (unsigned k = 0; k < d->retired_count; ++k){
   while(atomic_load_explicit(&d->retired[k].a->readers,memory_order_acquire))pause_ms(1);
   free_analysis(d->retired[k].a);
  }
  pthread_mutex_destroy(&d->queue_lock);
  free(d->r);
 }
#ifdef XZ_XP_SERVICE_PROOF
 if(p->sp_enabled)atomic_store(&sp_owner,0);
#endif
 pthread_mutex_destroy(&p->publish_lock);
 free(p);
}
