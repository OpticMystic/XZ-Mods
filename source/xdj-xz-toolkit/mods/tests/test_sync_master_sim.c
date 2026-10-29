/* SPDX-License-Identifier: GPL-2.0-only */
/* Seeded, deterministic SYNC/MASTER simulation of the four-deck chain.
 *
 * Real own-C modules, linked together: extra_player.c (DECK 3/4 players),
 * extra_decks.c (snapshot, master projection, input), sync_link_runtime.c and
 * sync_link_plan.c (master observer and native-sync bridge), deck_select.c
 * (SYNC/MASTER routing), ui/native_led.c (the LED hook) and the jog display
 * snapshot. Stock behaviour is a model: native decks 1/2, the stock sync
 * manager behind the bridge's test seam, the native clock cache and the stock
 * LED packet. No threads run; one scheduler steps every actor on simulated
 * time, so every run replays exactly from its seed.
 *
 * Usage: test-sync-master-sim DATA_DIR FIRST_SEED SEEDS [SECONDS] [--trace]
 *        test-sync-master-sim DATA_DIR replay SEED [action-mask-file]
 */
#define main sim_unused_player_main
#include "../test_extra_player.c"
#undef main
#include "sync_master_sim.h"
#include "../sync_link_runtime.h"
#include "../deck_select.h"
#include "../jog_display_runtime.h"
#include "../jog_display.h"
#include "../ui/native_led.h"
#include <stdarg.h>
#include <sys/stat.h>
/* The same harness runs on the pre-parity tree (no offset field) to prove each story fails there. */
#ifdef XZ_XP_SYNC_OFFSET_MS
#define SYNC_OFFSET(d) ((d)->sync.offset)
#else
#define XZ_XP_SYNC_OFFSET_MS 2.0
#define SYNC_OFFSET(d) 0
#endif
/* Scripted actions and stories leave their optional trailing fields zero. */
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

uint64_t sim_us;
int xz_hook_arm(uint32_t address,const unsigned char expected[8],void *replacement,void **original){
 (void)address;(void)expected;(void)replacement;(void)original;return -1;
}
int xz_runtime_deck_selection_snapshot(struct xz_ds_selection *out);

/* ------------------------------------------------------------------ RNG */
static uint64_t rng_state;
static uint32_t rnd(void){rng_state^=rng_state<<13;rng_state^=rng_state>>7;rng_state^=rng_state<<17;return (uint32_t)(rng_state>>11);}
static uint32_t rnd_below(uint32_t n){return n?rnd()%n:0;}
static void rng_seed(uint64_t s){rng_state=s*0x9e3779b97f4a7c15ull+0x632be59bd9b4e019ull;if(!rng_state)rng_state=1;for(int i=0;i<4;i++)rnd();}

/* ------------------------------------------------------- stock: native decks */
/* A native deck moves its beats by bpm x motion every millisecond. VINYL (stock decks start in it):
   a jog-top touch holds the platter (the clock reads not playing, manual), a nudge bends the speed
   for a moment (manual), PAUSE brakes over brake_ms, PLAY starts at once. */
struct native { int loaded,playing,sync; uint32_t identity,revision; double bpm,beats;
 int vinyl,touch,brake; uint32_t brake_at,brake_ms,bend_until; double bend,motion; };
static struct native nat[2];
static uint32_t native_identity_counter=0x1000,native_revision_counter;
/* Stock policies the code cannot know. Each seed draws one per field. */
struct stock_policy {
 int master_press_empty_sets;   /* MASTER on an empty native deck still becomes master */
 int master_press_toggles_off;  /* MASTER on the current native master clears it (-1) */
 int stop_policy;               /* native master stops: 0 keep, 1 move to playing native deck, 2 clear */
 int unload_policy;             /* native master unloads: 0 keep, 1 move, 2 clear */
 int sync_press_claims;         /* native SYNC with no master makes that deck master */
 unsigned reassert_ms;          /* stock re-asserts its master through set_master_on (0 never) */
 unsigned reassert_playing_ms;  /* stock calls set_master_on(its master's own id) while that native master plays */
 int refuse_playing;            /* stock's link-master call refuses player 3/4 while a native master plays */
 int brake_reports_playing;     /* a native VINYL brake still reads playing until the platter stops */
 int native_vinyl;              /* native decks load in VINYL mode (stock starts in it) */
};
static struct stock_policy policy;

/* ------------------------------------------------ stock: sync manager memory */
static unsigned char sm_mgr[64] __attribute__((aligned(8)));
static unsigned char sm_master[64] __attribute__((aligned(8)));
static unsigned char sm_link[16] __attribute__((aligned(8)));
static struct xz_sync_link_test_ops hooks;
static int foreign_slot[2];
static uint32_t operator_events;
static int32_t engine_master(void){int32_t m;memcpy(&m,sm_master+8,4);return m;}
static void le32(unsigned char *p,uint32_t v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);p[2]=(unsigned char)(v>>16);p[3]=(unsigned char)(v>>24);}
static void check_publication(int id,const unsigned char *status,int status_rc,const unsigned char *beat,int beat_rc);
static int st_sync_mgr(void *mgr){
 (void)mgr;int32_t m=engine_master();
 if(m==2||m==3){
  /* The engine follows virtual player 3/4 through the hooked status reads. */
  int id=m+1;uint8_t lane=0;unsigned char st[56],bt[44];
  int src=hooks.get_player_info(sm_link,&id,st),brc=hooks.get_beat_info(sm_link,&id,&lane,bt);
  check_publication(id,st,src,bt,brc);
 }
 return 0;
}
static int st_get_player(void *lm,const int *id,void *out){
 (void)lm;unsigned char *b=out;memset(b,0,56);
 if(id&&(*id==3||*id==4)&&foreign_slot[*id-3]){le32(b,(uint32_t)*id);b[4]=0xa4;b[5]=1;le32(b+0x1c,12800);return 0;}
 le32(b+0x1c,0x7fffffffu);return 0;
}
static int st_get_beat(void *lm,const int *id,const uint8_t *lane,void *out){(void)lm;(void)id;(void)lane;memset(out,0xee,44);return 0;}
static int st_set_player(void *lm,const int *id,const void *a2,const void *a3,const void *a4,const void *a5,const void *a6,
 const void *a7,const void *a8,const void *a9,const void *a10,const void *a11,const void *a12,const void *a13){
 (void)lm;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6;(void)a7;(void)a8;(void)a9;(void)a10;(void)a11;(void)a12;(void)a13;
 if(id&&(*id==3||*id==4))foreign_slot[*id-3]=1;return 0;
}
static int st_set_beat(void *lm,const int *id,const void *lane,const void *pkt){(void)lm;(void)lane;(void)pkt;if(id&&(*id==3||*id==4))foreign_slot[*id-3]=1;return 0;}
static int st_set_master_on(void *sm,const int *id){if(!sm||!id)return -1;int32_t m=*id;memcpy((unsigned char *)sm+8,&m,4);return 0;}
static int st_set_link_master(void *sm,const int *input){
 if(!sm||!input)return -1;int id=*input;
 if(id>=3&&id<=4){
  unsigned char st[56];
  if(hooks.get_player_info(sm_link,&id,st))return -1;
  if((st[4]&0x80)==0||(st[4]&0x24)!=0x24)return -1;
  int32_t cur=engine_master();
  if(policy.refuse_playing&&cur>=0&&cur<2&&nat[cur].loaded&&nat[cur].playing)return -1;
 }
 int m=id<=0?-1:id-1;return hooks.set_master_on(sm,&m);
}
static int st_monotonic(uint64_t *out){*out=sim_us;return 0;}
/* Stock's own master change: every non-internal call is an operator event to the bridge. */
static uint32_t last_operator_ms;  /* last action that may legitimately change mastership */
/* The same, without a load on the master: a load is not a choice of master (M5, M6). */
static uint32_t last_choice_ms;
static uint32_t now32(void);
static int master_was_playing;
static void stock_set_master(int32_t id){operator_events++;last_operator_ms=now32();last_choice_ms=last_operator_ms;master_was_playing=0;int v=id;(void)hooks.set_master_on(sm_master,&v);}

/* ---------------------------------------------- stock: native clock cache */
static struct xz_sync_native_snapshot nc_cache;
static void nc_publish(void){
 struct xz_sync_native_snapshot s={0};
 s.native_deck=-1;s.master_id=engine_master();
 s.captured_us=sim_us;s.captured_ms=(uint32_t)(sim_us/1000u);s.earliest_us=s.latest_us=sim_us;s.uncertainty_us=200;
 int32_t m=s.master_id;
 if(m>=2&&m<=5){
  s.reason=XZ_SYNC_NATIVE_EXTERNAL_MASTER;
  for(unsigned d=0;d<2;d++)if(nat[d].loaded&&nat[d].playing)s.playing_native_mask|=1u<<d;
 }else if(m<0||m>1){
  s.reason=XZ_SYNC_NATIVE_MASTER_UNAVAILABLE;
 }else{
  const struct native *n=&nat[m];s.native_deck=m;
  if(!n->loaded){s.reason=XZ_SYNC_NATIVE_SOURCE_CHANGED;}
  else{
   s.reason=XZ_SYNC_NATIVE_OK;s.available=1;s.phase_available=1;
   s.track_identity=n->identity;s.source_revision=n->revision;
   /* The steady tempo excludes a bend; the phase is where the platter is (native_clock.c). A held
      platter reads not playing and manual, a bend reads manual. */
   int bending=n->bend!=0&&(int32_t)(n->bend_until-(uint32_t)(sim_us/1000u))>0;
   s.bpm=(float)n->bpm;s.nominal_bpm=(float)n->bpm;s.speed=1;
   s.bar_phase=(float)fmod(n->beats,4.);
   s.playing=n->playing&&!n->touch&&!(n->brake&&!policy.brake_reports_playing);s.manual=n->touch||bending;
   s.evidence=XZ_SYNC_NATIVE_MASTER_OBSERVED|XZ_SYNC_NATIVE_TRACK_OBSERVED;
   if(s.playing&&!s.manual)s.playing_native_mask=1u<<m;
   /* sync_native.c also reads the other native deck (M2). */
   const struct native *o=&nat[1-m];
   int o_bending=o->bend!=0&&(int32_t)(o->bend_until-(uint32_t)(sim_us/1000u))>0;
   if(o->loaded&&o->playing&&!o->touch&&!o_bending&&!(o->brake&&!policy.brake_reports_playing))s.playing_native_mask|=1u<<(1-m);
  }
 }
 nc_cache=s;
}
/* The runtime's copier (shell_runtime.c xz_extra_native_sync_clock) rules. */
static int nc_copy(struct xz_sync_native_snapshot *out){
 *out=nc_cache;uint32_t age=(uint32_t)(sim_us/1000u)-out->captured_ms;
 if(!out->available)return 0;
 if(!out->source_revision){out->available=0;out->phase_available=0;out->reason=XZ_SYNC_NATIVE_SOURCE_CHANGED;return 0;}
 if((int32_t)age<0){out->available=0;out->phase_available=0;out->playing_native_mask=0;out->reason=XZ_SYNC_NATIVE_TIME_INVALID;return 0;}
 if(age>150u){out->phase_available=0;out->playing_native_mask=0;}
 return 1;
}

/* --------------------------------------------------- tracks and fixtures */
/* Tracks: one position-coded WAV, several beat grids (beats at 100 ms + n x 60000/BPM, integer ms). */
struct sim_track { uint32_t id; const char *title; uint32_t bpm_x100; char anlz[420]; };
static struct sim_track tracks[]={{1,"Sim A",12000,""},{2,"Sim B",12000,""},{4,"Sim 122",12200,""},
 {5,"Sim 128",12800,""},{6,"Sim 87",8700,""},{7,"Sim 126.5",12650,""}};
#define TRACKS (sizeof tracks/sizeof tracks[0])
static double grid_ms(uint32_t bpm_x100,int64_t i){return 100.+(double)llround((double)i*6000000./bpm_x100);}
static double track_beats(uint32_t bpm_x100,double frame){
 double ms=frame*1000./44100.;int64_t i=(int64_t)floor((ms-100.)*bpm_x100/6000000.);
 for(int k=0;k<3;k++){if(grid_ms(bpm_x100,i)>ms)i--;else if(grid_ms(bpm_x100,i+1)<=ms)i++;}
 double a=grid_ms(bpm_x100,i),b=grid_ms(bpm_x100,i+1);
 return (double)i+(ms-a)/(b-a);
}
static char sim_wav[512];
/* Every source frame n carries its own index: left (n mod 4096)-2048, right (n/4096 mod 4096)-2048.
   The players' linear interpolation keeps a fractional index exact, so the PCM a deck actually
   sends tells where it played, independently of the player's own bookkeeping. */
#define POS_LO(n) ((int)((n)&4095u)-2048)
#define POS_HI(n) ((int)(((n)>>12)&4095u)-2048)
static void write_fixture(const char *dir){
 snprintf(sim_wav,sizeof sim_wav,"%s/sim-position.wav",dir);
 FILE *w=fopen(sim_wav,"rb");
 if(!w){
  w=fopen(sim_wav,"wb");assert(w);
  const uint32_t rate=44100,frames=rate*40,bytes=frames*4;
  unsigned char h[44]={'R','I','F','F'};
  uint32_t v=36+bytes;memcpy(h+4,&v,4);memcpy(h+8,"WAVEfmt ",8);v=16;memcpy(h+16,&v,4);
  uint16_t s=1;memcpy(h+20,&s,2);s=2;memcpy(h+22,&s,2);memcpy(h+24,&rate,4);v=rate*4;memcpy(h+28,&v,4);
  s=4;memcpy(h+32,&s,2);s=16;memcpy(h+34,&s,2);memcpy(h+36,"data",4);memcpy(h+40,&bytes,4);
  assert(fwrite(h,1,44,w)==44);
  int16_t buf[2*4410];
  for(uint32_t at=0;at<frames;at+=4410){
   for(unsigned i=0;i<4410;i++){buf[2*i]=(int16_t)POS_LO(at+i);buf[2*i+1]=(int16_t)POS_HI(at+i);}
   assert(fwrite(buf,4,4410,w)==4410);
  }
 }
 fclose(w);
 build_analysis();
 /* Replace the 4-beat grid with a 40 s 120 BPM grid: beats at 100 + 500 n ms. */
 file_start(&dat);
 uint32_t sec=open_section(&dat,"PQTZ",24);
 u32(&dat,0);u32(&dat,0x80000);u32(&dat,80);
 for(uint32_t i=0;i<80;i++){u16(&dat,i%4+1);u16(&dat,12000);u32(&dat,100+i*500);}
 close_section(&dat,sec);file_end(&dat);
 write_file("ANLZ0000.DAT",&dat);
 /* Other grids for the same audio, each in its own ANLZ folder. */
 char base[sizeof anlz_dir];snprintf(base,sizeof base,"%s",anlz_dir);
 for(unsigned k=0;k<TRACKS;k++){
  if(tracks[k].bpm_x100==12000){snprintf(tracks[k].anlz,sizeof tracks[k].anlz,"%s",base);continue;}
  snprintf(tracks[k].anlz,sizeof tracks[k].anlz,"%s-%u",base,tracks[k].bpm_x100);
  mkdir(tracks[k].anlz,0755);snprintf(anlz_dir,sizeof anlz_dir,"%s",tracks[k].anlz);
  build_analysis();
  uint32_t n=0;while(grid_ms(tracks[k].bpm_x100,n)<39900.)n++;
  file_start(&dat);sec=open_section(&dat,"PQTZ",24);
  u32(&dat,0);u32(&dat,0x80000);u32(&dat,n);
  for(uint32_t i=0;i<n;i++){u16(&dat,i%4+1);u16(&dat,tracks[k].bpm_x100);u32(&dat,(uint32_t)grid_ms(tracks[k].bpm_x100,i));}
  close_section(&dat,sec);file_end(&dat);write_file("ANLZ0000.DAT",&dat);
 }
 snprintf(anlz_dir,sizeof anlz_dir,"%s",base);
}
static int sim_resolve(int source,uint32_t id,struct xz_extra_track *out){
 memset(out,0,sizeof *out);out->source=source;out->track_id=id;
 if(id==3){snprintf(out->path,sizeof out->path,"%s/missing.wav",fixtures);snprintf(out->title,sizeof out->title,"Missing");return 0;}
 for(unsigned k=0;k<TRACKS;k++)if(tracks[k].id==id){
  snprintf(out->path,sizeof out->path,"%s",sim_wav);
  snprintf(out->anlz_dir,sizeof out->anlz_dir,"%s",tracks[k].anlz);
  snprintf(out->title,sizeof out->title,"%s",tracks[k].title);
  out->track_bpm_x100=(int32_t)tracks[k].bpm_x100;return 0;
 }
 return -1;
}

/* ------------------------------------------------------- the actions */
enum act {
 ACT_SYNC,ACT_MASTER,ACT_PLAY,ACT_SELECT,ACT_LOAD,ACT_LOAD_MISSING,ACT_NATIVE_LOAD,ACT_NATIVE_UNLOAD,
 ACT_TEMPO,ACT_NATIVE_TEMPO,ACT_ROUTE_DOWN,ACT_ROUTE_UP,ACT_STALL_CONSUMER,ACT_STALL_PUBLISHERS,
 ACT_STALL_WORKERS,ACT_PACKET,ACT_LED_STOP,ACT_LED_START,ACT_STOCK_PLAY_OTHER,
 /* Jog and mode actions on the deck a side controls (software or native): */
 ACT_TOUCH,ACT_NUDGE,ACT_RANGE,ACT_VINYL,ACT_SEEK,
 /* The native deck of a side, whichever deck that side controls (the DJ switches over to it): */
 ACT_NATIVE_PLAY,ACT_NATIVE_TOUCH,ACT_NATIVE_NUDGE,ACT_NATIVE_MASTER,
 /* Scripted only (never drawn): */
 ACT_AUTO_CUE,ACT_COUNT
};
static const char *act_name[]={"sync","master","play","select","load","load-missing","native-load","native-unload",
 "tempo","native-tempo","route-down","route-up","stall-consumer","stall-publishers","stall-workers","packet",
 "led-stop","led-start","native-play-other","touch","nudge","range","vinyl","seek",
 "native-play","native-touch","native-nudge","native-master","auto-cue"};
_Static_assert(sizeof act_name/sizeof act_name[0]==ACT_COUNT,"one name per action");
/* value: an exact scripted quantity (V(x)); zero keeps the drawn arg's meaning. */
struct action { uint32_t at_ms; uint8_t kind,side; int16_t arg; int32_t value; };
#define V(x) ((int32_t)(x)+(1<<20))
#define HAS_VALUE(a) ((a)->value!=0)
#define VALUE(a) ((a)->value-(1<<20))
#define MAX_ACTIONS 1024
static struct action actions[MAX_ACTIONS];
static unsigned action_count;
static unsigned char action_enabled[MAX_ACTIONS];

/* ---------------------------------------------------- scheduler params */
struct schedule {
 unsigned worker_ms,consumer_ms,consumer_jitter,clock_ms,led_ms[2],led_phase[2],connector_ms,usb_burst;
 unsigned rebuild,late_writer,variant_side,variant_kind;
};
static struct schedule sch;
static unsigned run_ms;

/* ------------------------------------------------------------ the world */
static struct xz_deck_select ds;
static uint32_t route_epoch;
static int route_is_up;
static enum sim_packet packet_variant[2];
static int led_running;
static uint32_t stall_until[3]; /* consumer, publishers (clock/connector/LED), workers */
static uint32_t last_consumer_ms,consumer_gap_until;
static uint32_t now32(void){return (uint32_t)(sim_us/1000u);}
int xz_runtime_deck_selection_snapshot(struct xz_ds_selection *out){xz_deck_select_snapshot(&ds,out);return 1;}

struct pending_release { uint32_t at; int side; uint16_t code; int global; };
static struct pending_release releases[16],holds[4];
static unsigned release_count,hold_count;
/* SYNC long press (DRI1625B p.65: held 1 s or more is Instant Doubles): every press toggles SYNC once; only a
   press that lasted 1 s, with stock's hold notification, runs Instant Doubles, and then exactly once. */
static uint32_t sync_down_at[2],doubles_delivered[2],doubles_at_down[2],doubles_short[2],doubles_count_bad[2];static int sync_down[2],sync_hold_sent[2];
static uint32_t global_disturb_ms; /* defined with the lock checks */
/* Jog-top releases, due later: on a software deck (extra) or on the native deck of that side. */
struct deferred { uint32_t at; int side,extra; };
static struct deferred deferred[16];
static unsigned deferred_count;

/* ------------------------------------------- what the software decks actually play */
/* Each accepted block's PCM is decoded back to source frame indices (write_fixture). When the
   fake transport completes a block, its samples land on a ring indexed by a monotonic wire
   frame. Wire frame W is heard at act_time_us(W): the transport consumes 44.1 frames per
   simulated millisecond. NAN marks silence, a stretched or shifted sample, or no data. */
#define ACT_RING (1u<<18)
struct shadow_block { uint32_t frames; double pos[XZ_USB_BLOCK_FRAMES]; };
static struct shadow_block shadow[2][FAKE_BLOCKS];
static double act_pos[2][ACT_RING];
static uint64_t act_end[2],mono_wire,wire_base;
static uint64_t consume_us;           /* when mono_wire was last advanced */
static uint32_t act_jumps[2],act_jump_ms[2]; /* playback discontinuities (snaps, jumps) */
static double decode_sample(const float *s){
 double lo=(double)s[0]*32768.+2048.,hi=(double)s[1]*32768.+2048.;
 double h=floor(hi+.5);
 if(fabs(hi-h)>1e-3||lo<-1e-3||lo>=4096.)return NAN;
 return h*4096.+lo;
}
static void shadow_store(int d,unsigned index,const float *pcm,uint32_t frames){
 struct shadow_block *b=&shadow[d][index%FAKE_BLOCKS];b->frames=frames;
 for(uint32_t i=0;i<frames;i++)b->pos[i]=decode_sample(pcm+2*i);
}
static xz_usb_result sim_submit(void *ctx,uint32_t epoch,int d,uint32_t generation,const float *pcm,uint32_t frames,uint64_t after){
 unsigned index=f.write[d];
 xz_usb_result r=fake_submit(ctx,epoch,d,generation,pcm,frames,after);
 if(r==XZ_USB_OK)shadow_store(d,index,pcm,frames);
 return r;
}
static xz_usb_result sim_transition(void *ctx,uint32_t epoch,int d,uint32_t expected,const float *pcm,uint32_t frames,uint64_t after,uint32_t *generation){
 unsigned index=f.write[d];
 xz_usb_result r=fake_transition(ctx,epoch,d,expected,pcm,frames,after,generation);
 if(r==XZ_USB_OK)shadow_store(d,index,pcm,frames);
 return r;
}
static double act_at(int d,double w){
 if(w<0)return NAN;
 uint64_t i=(uint64_t)floor(w);
 if(i+1>=act_end[d]||act_end[d]-i>ACT_RING-2)return NAN;
 double a=act_pos[d][i%ACT_RING],b=act_pos[d][(i+1)%ACT_RING];
 if(isnan(a)||isnan(b)||fabs(b-a)>8.5)return NAN; /* no interpolation across a jump */
 return a+(w-(double)i)*(b-a);
}
/* Source frames per wire frame over the span before w; NAN across silence or a jump. */
static double act_rate(int d,double w,double span){
 double a=act_at(d,w-span),m=act_at(d,w-span/2),b=act_at(d,w);
 if(isnan(a)||isnan(b)||isnan(m))return NAN;
 double r=(b-a)/span;
 if(fabs((m-a)/(span/2)-r)>.02+.05*fabs(r))return NAN;
 return r;
}
static double act_time_us(double w){return (double)consume_us-((double)mono_wire-w)*1000000./XZ_USB_RATE;}
static double act_wire_at(double us){return (double)mono_wire-((double)consume_us-us)*XZ_USB_RATE/1000000.;}
static void act_complete(int d,const struct shadow_block *b,double end){
 int64_t begin=(int64_t)llround(end-(double)b->frames);
 if(begin<0)return;
 double prev=act_end[d]?act_pos[d][(act_end[d]-1)%ACT_RING]:NAN,prev2=act_end[d]>1?act_pos[d][(act_end[d]-2)%ACT_RING]:NAN;
 int contiguous=act_end[d]==(uint64_t)begin;
 for(uint64_t w=act_end[d];w<(uint64_t)begin&&w<act_end[d]+ACT_RING;w++)act_pos[d][w%ACT_RING]=NAN;
 for(uint32_t i=0;i<b->frames;i++)act_pos[d][((uint64_t)begin+i)%ACT_RING]=b->pos[i];
 if((uint64_t)begin+b->frames>act_end[d])act_end[d]=(uint64_t)begin+b->frames;
 /* A discontinuity: the first sample does not continue the previous step. */
 if(contiguous&&!isnan(prev)&&!isnan(prev2)&&!isnan(b->pos[0])&&fabs(b->pos[0]-prev-(prev-prev2))>2.)
  {act_jumps[d]++;act_jump_ms[d]=now32();}
}
/* The fake transport, cut at block boundaries so each completion gets its exact wire frame.
   The transport state after the whole call is the same as one consume(). */
static void sim_consume(int d,double frames){
 /* End on the exact wire count one consume() would give, so both decks' counters stay equal and
    no completion lands past the published wire count through rounding of the pieces. */
 double target=f.wire[d]+frames;
 while(frames>1e-9){
  double part=frames;
  const xz_usb_deck_position *q=&f.position.deck[d];
  uint32_t applied=f.queue_generation[d];(void)q;
  for(unsigned i=f.read[d];i!=f.write[d];i++){
   const struct block *b=&f.queue[d][i%FAKE_BLOCKS];
   if(b->generation<applied)continue;
   if(b->generation==applied){double need=(double)b->frames-f.credit[d];if(need>1e-9&&need<part)part=need;}
   break;
  }
  unsigned r=f.read[d];
  consume(d,part);frames-=part;
  for(unsigned i=r;i!=f.read[d];i++){
   const struct block *b=&f.queue[d][i%FAKE_BLOCKS];
   if(b->generation!=f.position.deck[d].applied_generation)continue;
   act_complete(d,&shadow[d][i%FAKE_BLOCKS],(double)wire_base+f.wire[d]-f.credit[d]);
  }
 }
 pthread_mutex_lock(&f.lock);
 f.wire[d]=target;f.position.wire_completed_frames=(uint64_t)target;
 xz_usb_deck_position *q=&f.position.deck[d];
 if(q->source_completed_wire_frame>(uint64_t)target)q->source_completed_wire_frame=(uint64_t)target;
 if(q->source_staged_wire_frame>(uint64_t)target)q->source_staged_wire_frame=(uint64_t)target;
 pthread_mutex_unlock(&f.lock);
}
static void sim_route(uint32_t link){fake_route(link);wire_base=mono_wire;}
/* Native decks: beats at every simulated millisecond, for the same time base. */
#define NAT_RING 8192u
static double nat_beats[2][NAT_RING],nat_motion[2][NAT_RING];
static uint32_t nat_hist_end; /* ms written: (nat_hist_end-NAT_RING, nat_hist_end] */
static double nat_beats_at(int d,double us){
 double ms=us/1000.;uint32_t a=(uint32_t)floor(ms);
 if((int32_t)(a+1-nat_hist_end)>0||(int32_t)(nat_hist_end-a)>=(int32_t)NAT_RING-2)return NAN;
 double x=nat_beats[d][a%NAT_RING],y=nat_beats[d][(a+1)%NAT_RING];
 return x+(ms-(double)a)*(y-x);
}

static void native_master_selected(int side){
 /* runtime.c: a native MASTER press reaches the players before stock acts. */
 xz_extra_decks_native_master_selected((unsigned)side);
 const struct native *n=&nat[side];
 int32_t m=engine_master();
 if(policy.master_press_toggles_off&&m==side){stock_set_master(-1);return;}
 if(!n->loaded&&!policy.master_press_empty_sets)return;
 stock_set_master(side);
}
static void native_key(int side,uint16_t code){
 struct native *n=&nat[side];
 if(code==0x4313){
  n->sync^=1;
  if(n->sync&&policy.sync_press_claims&&engine_master()==-1&&n->loaded)stock_set_master(side);
 }
 if(code==0x4101&&n->loaded){
  /* VINYL: PAUSE brakes (the tick loop stops the deck), PLAY during a brake or from a stop is instant. */
  if(n->brake)n->brake=0;
  else if(n->playing&&n->vinyl&&n->brake_ms){n->brake=1;n->brake_at=now32();}
  else n->playing^=1;
 }
}
static uint32_t sync_presses_delivered[2],master_presses_delivered[2];
static void key(int side,uint16_t code,unsigned op){
 struct xz_ds_outcome o;
 xz_deck_select_route_instance(&ds,route_is_up,route_is_up?route_epoch:0);
 if(code==0x4312)o=xz_deck_select_global_key(&ds,(unsigned)side+1,code,op,now32());
 else{
  /* runtime.c: a player key reaches the early global hook first (xz_runtime_deck_global_input);
     stock dispatches it to the player callback (physical_key -> deck_route) only when that passes. */
  struct xz_ds_key k={.side=side,.code=code,.op=op};
  o=xz_deck_select_global_player(&ds,&k,now32());
  if(o.verdict==XZ_DS_PASS)o=xz_deck_select_key(&ds,&k,now32());
 }
 if(o.verdict==XZ_DS_DELIVER){
  int ok=xz_extra_deck_input(&o.event);
  if(ok&&o.event.kind==XZ_EXTRA_IN_SYNC)sync_presses_delivered[side]++;
  if(ok&&o.event.kind==XZ_EXTRA_IN_INSTANT_DOUBLES&&code==0x4313){
   doubles_delivered[side]++;
   if(!sync_down[side]||(uint32_t)(now32()-sync_down_at[side])<1000)doubles_short[side]=now32()-sync_down_at[side]+1;
  }
  if(ok&&o.event.kind==XZ_EXTRA_IN_MASTER)master_presses_delivered[side]++;
 }else if(o.verdict==XZ_DS_PASS&&op==0){
  if(code==0x4312)native_master_selected(side);else native_key(side,code);
 }
}
static void sync_release_check(int side){
 if(!sync_down[side])return;
 sync_down[side]=0;
 unsigned ran=doubles_delivered[side]-doubles_at_down[side];
 if(sync_hold_sent[side]&&ran!=1)doubles_count_bad[side]=ran+1;
}
static void tap(int side,uint16_t code,unsigned hold_ms){
 /* A held button is released before it can be pressed again. */
 for(unsigned i=0;i<release_count;i++)if(releases[i].side==side&&releases[i].code==code){
  struct pending_release r=releases[i];releases[i]=releases[--release_count];key(r.side,r.code,2);
  if(code==0x4313)sync_release_check(side);break;
 }
 if(code==0x4313){sync_down[side]=1;sync_down_at[side]=now32();doubles_at_down[side]=doubles_delivered[side];sync_hold_sent[side]=0;
  /* Stock's hold notification for a press held 1 s or more. */
  if(hold_ms>=1000&&hold_count<4)holds[hold_count++]=(struct pending_release){now32()+1000,side,code,0};}
 key(side,code,0);
 if(release_count<16)releases[release_count++]=(struct pending_release){now32()+hold_ms,side,code,code==0x4312};
}
static void publish_sides(void){
 xz_extra_decks_publish_sides(ds.side[0].target==XZ_DS_EXTRA,ds.side[1].target==XZ_DS_EXTRA);
}

/* ------------------------------------------------------ failure classes */
enum fail {
 F_NONE,F_SYNC_LAMP_DARK,F_SYNC_LAMP_STUCK,F_MASTER_LAMP_DARK,F_MASTER_LAMP_FALSE,F_NATIVE_LAMP,F_FOREIGN_ROW,
 F_JOG_SYNC,F_DUPLICATE_MASTER,F_VIEW_MASTER,F_SYNC_NOT_LOCKED,F_SYNC_WRONG_MASTER,F_HANDOFF,F_EXPLICIT_MASTER,
 F_SYNC_PRESS_LOST,F_NONE_ELECTION,F_HANDOFF_UNLOADED,F_HANDOFF_ORDER,
 /* Lock quality, measured on what the decks actually play: */
 F_TEMPO_MISMATCH,F_PHASE_NOT_ACQUIRED,F_PHASE_DRIFT,F_UNPROMPTED_SNAP,F_SYNC_SILENT,F_LOCK_FLAG,
 /* What DECK 3/4 publishes as master to the stock engine: */
 F_PUB_PHASE,F_PUB_TEMPO,F_PUB_STOPPED,F_PUB_GAP,
 /* Owner-observed stock (stock-sync-spec.md M4, M6): holding the master's platter top hands master to
    the other synced, playing deck in under 2 s, and a handoff never changes the tempo. */
 F_HOLD_NOT_HANDED,F_HANDOFF_TEMPO,
 /* The engine passes through a deck nobody chose on the way between two masters. */
 F_MASTER_DETOUR,
 /* p.64: before anybody chose a master, the deck that first plays an analysed track becomes master. */
 F_FIRST_PLAY,
 /* Owner-observed stock (stock-sync-spec.md), one class per rule: M1 no pending notice; M2 a paused master
    hands over at once; M5 a load on the master hands over; M6 the engine never names a native deck while
    a loaded DECK 3/4 stays the chosen master; S2 SYNC off does not step the speed; S4 a kept offset
    flashes the SYNC lamp and the view says so. */
 F_MASTER_NOTICE,F_PAUSE_NOT_HANDED,F_LOAD_NOT_HANDED,F_MASTER_UNCHOSEN,F_SYNC_OFF_STEP,F_SYNC_LAMP_FLASH,F_OFFSET_FLAG,F_COUNT
};
static const char *fail_name[]={"none","sync-lamp-dark","sync-lamp-stuck-on","master-lamp-dark","master-lamp-false",
 "native-lamp-overwritten","foreign-forced-row-modified","jog-sync-indicator","duplicate-master","view-master-disagrees",
 "sync-not-locked","sync-wrong-master","handoff-missing","explicit-master-not-taken","sync-press-lost","none-election","handoff-from-unloaded-native","handoff-order",
 "sync-tempo-mismatch","sync-phase-not-acquired","sync-phase-drift","sync-unprompted-snap","sync-silent","sync-lock-flag-wrong",
 "publish-phase-error","publish-tempo-error","publish-while-stopped","publish-gap-while-playing","hold-not-handed-over","handoff-tempo-changed","master-detour",
 "first-play-not-master","master-requested-notice","pause-not-handed-over","load-not-handed-over","master-named-unchosen",
 "sync-off-speed-step","sync-lamp-not-flashing","sync-offset-flag-wrong"};
_Static_assert(sizeof fail_name/sizeof fail_name[0]==F_COUNT,"one name per class");
static int fail_class,keep_running;
static uint64_t fail_mask;static uint32_t fail_at,class_at[F_COUNT];
_Static_assert(F_COUNT<=64,"fail_mask holds one bit per class");
static char fail_detail[512],class_detail[F_COUNT][320];
static void fail(int c,const char *fmt,...) __attribute__((format(printf,2,3)));
static void fail(int c,const char *fmt,...){
 if(fail_class&&!keep_running)return;
 if(fail_mask&(1ull<<c))return;
 fail_mask|=1ull<<c;class_at[c]=now32();
 va_list ap;va_start(ap,fmt);vsnprintf(class_detail[c],sizeof class_detail[c],fmt,ap);va_end(ap);
 if(fail_class)return;
 fail_class=c;fail_at=now32();
 snprintf(fail_detail,sizeof fail_detail,"%s",class_detail[c]);
}

/* Persistence filters: a mismatch only fails once it outlives its bound. */
struct since { uint32_t at; int on; };
static int held(struct since *s,int cond,uint32_t bound){
 if(!cond){s->on=0;return 0;}
 if(!s->on){s->on=1;s->at=now32();}
 return (uint32_t)(now32()-s->at)>bound;
}
static struct since unloaded_bad;
/* Documented order after a software master stops: from 3, DECK 4 then native; from 4, native then DECK 3. */
static int order_alt,order_pending,order_expected,order_from,prev_playing[2];static uint32_t order_at,master_seen_at[2];
/* What was playing when a playing software deck's PLAY (pause) was pressed: a VINYL pause
   hands over at the press, so either moment's order is a valid next master. */
static int press_next[2]={-9,-9};static uint32_t pause_press_at[2],play_press_at[2],native_transport_at;
/* S4: a kept offset flashes the SYNC lamp: the row must change state at least every 300 ms. */
static struct since lamp_flash_bad[2],jog_flash_bad[2];static uint32_t lamp_flip_at[2],lamp_prev[2];
static struct since lamp_sync_bad[2],lamp_sync_stuck[2],lamp_master_bad[2],lamp_master_false[2],jog_bad[2],dup_bad,view_bad[2],lock_bad[2],wrong_bad[2],none_bad,handoff_bad,first_bad;
static struct since native_bad[2],pause_bad,unchosen_bad;
static int load_pending,load_from,chosen_live;static uint32_t load_at,chosen_epoch,native_stale_from;
static uint32_t det_cur_at; /* defined with the detour tracker */
static uint32_t master_press_at[2]; static int master_press_pending[2],master_press_playing[2];
static uint32_t sync_press_at[2]; static int sync_press_pending[2],sync_press_before[2];
/* SYNC press parity: from a quiet base, every press sent to a loaded, visible software deck must be
   delivered once and toggle the request once, however fast the presses come. */
static unsigned sync_sent[2],sync_base_sent[2],sync_base_delivered[2];static int sync_base_req[2],sync_base_valid[2];
static uint32_t lock_signature[2];
static uint32_t lock_signature_at[2];
static int32_t master_tracked=-9; static uint32_t master_ident;

/* A jog-top hold on a playing deck (VINYL) is not a stop: it resumes when released. */
static int software_playing(const struct xz_extra_deck *d){return d->loaded&&(d->playing||d->held_playing);}
static int deck_playing(int logical,const struct xz_extra_decks *v){
 if(logical==0||logical==1)return nat[logical].loaded&&nat[logical].playing;
 if(logical==2||logical==3)return software_playing(&v->deck[logical-2]);
 return 0;
}
static int deck_loaded(int logical,const struct xz_extra_decks *v){
 if(logical==0||logical==1)return nat[logical].loaded;
 if(logical==2||logical==3)return v->deck[logical-2].loaded;
 return 0;
}

static struct sim_led_stock stock_lamps(void){
 struct sim_led_stock s={0};int32_t m=engine_master();
 for(int i=0;i<2;i++){s.sync[i]=!!nat[i].sync;s.master[i]=m==i;}
 return s;
}
/* Coverage: how often each check had something to decide, summed over a batch. */
static unsigned long cov_forced,cov_native,cov_sync_on,cov_sync_off,cov_master_on,cov_master_off,cov_jog,cov_lock,cov_locked,cov_handoff_order;
static void check_led(unsigned channel){
 int side=(int)channel-1;
 struct sim_led_row sync,master;sim_led_rows(channel,&sync,&master);
 struct xz_extra_decks v;xz_xp_read(p,&v);
 int extra=route_is_up&&ds.side[side].target==XZ_DS_EXTRA&&led_running;
 const struct xz_extra_deck *d=&v.deck[side];
 enum sim_packet variant=packet_variant[side];
 if(variant==SIM_PACKET_FORCED){cov_forced++;
  if(sync.state!=0||master.state!=0||!sync.forced_by_stock||!master.forced_by_stock)
   fail(F_FOREIGN_ROW,"channel=%u sync-state=%u master-state=%u",channel,sync.state,master.state);
  return;
 }
 if(variant!=SIM_PACKET_ONE)return;
 if(!extra){
  struct sim_led_stock s=stock_lamps();
  int bad=sync.state!=s.sync[side]||master.state!=s.master[side];cov_native++;
  if(held(&native_bad[side],bad,0))fail(F_NATIVE_LAMP,"channel=%u sync=%u/%u master=%u/%u",channel,sync.state,s.sync[side],master.state,s.master[side]);
  lamp_sync_bad[side].on=lamp_sync_stuck[side].on=lamp_master_bad[side].on=lamp_master_false[side].on=0;
  return;
 }
 native_bad[side].on=0;
 int want_sync=d->loaded&&d->sync.requested,flash=want_sync&&SYNC_OFFSET(d);
 if(sync.state!=lamp_prev[side]){lamp_prev[side]=sync.state;lamp_flip_at[side]=now32();}
 int engine_is=engine_master()==side+2;
 int want_master=d->loaded&&engine_is;
 if(want_sync)cov_sync_on++;else cov_sync_off++;
 if(want_master)cov_master_on++;else cov_master_off++;
 if(held(&lamp_flash_bad[side],flash&&(uint32_t)(now32()-lamp_flip_at[side])>300,300))
  fail(F_SYNC_LAMP_FLASH,"deck=%d keeps an offset but the SYNC lamp has not flashed for %u ms",side+3,now32()-lamp_flip_at[side]);
 if(held(&lamp_sync_bad[side],want_sync&&!flash&&!sync.state,120))
  fail(F_SYNC_LAMP_DARK,"deck=%d requested=%d loaded=%d lamp=%u",side+3,d->sync.requested,d->loaded,sync.state);
 if(held(&lamp_sync_stuck[side],!want_sync&&sync.state,120))
  fail(F_SYNC_LAMP_STUCK,"deck=%d requested=%d loaded=%d lamp=%u",side+3,d->sync.requested,d->loaded,sync.state);
 int gap=(uint32_t)(now32()-last_consumer_ms)>XZ_MASTER_MAX_AGE_MS||(int32_t)(consumer_gap_until-now32())>0;
 if(held(&lamp_master_bad[side],want_master&&!master.state&&!gap,300))
  fail(F_MASTER_LAMP_DARK,"deck=%d engine=%d view-master=%d observed=%d lamp=%u",side+3,engine_master(),d->master,v.master_actual.deck,master.state);
 if(held(&lamp_master_false[side],!want_master&&master.state,300))
  fail(F_MASTER_LAMP_FALSE,"deck=%d engine=%d view-master=%d lamp=%u",side+3,engine_master(),d->master,master.state);
 struct xz_jd_snapshot jd;
 if(xz_jog_display_snapshot_from_deck(side,d,now32(),&jd)){
  int jog_on=jd.sync_mode!=XZ_JD_SYNC_OFF;cov_jog++;
  if(held(&jog_bad[side],jog_on!=want_sync,120))fail(F_JOG_SYNC,"deck=%d jog=%d want=%d",side+3,jog_on,want_sync);
  if(held(&jog_flash_bad[side],flash&&jd.sync_mode!=XZ_JD_SYNC_BLINK,120))fail(F_JOG_SYNC,"deck=%d keeps an offset, jog SYNC indicator not flashing",side+3);
 }else jog_bad[side].on=0;
}

static void check_world(void){
 struct xz_extra_decks v;xz_xp_read(p,&v);
 int32_t m=engine_master();
 uint32_t now=now32();
 /* One effective master: the engine has one id; a software view may not claim another. */
 int claims=v.deck[0].master+v.deck[1].master;
 if(held(&dup_bad,claims>1,150))fail(F_DUPLICATE_MASTER,"deck3=%d deck4=%d engine=%d",v.deck[0].master,v.deck[1].master,m);
 for(int i=0;i<2;i++){
  int bad=route_is_up&&v.deck[i].master&&m!=i+2;
  if(held(&view_bad[i],bad,300))fail(F_VIEW_MASTER,"deck=%d view-master engine=%d",i+3,m);
 }
 if(!route_is_up)return;
 /* SYNC presses on a visible software deck toggle its request. */
 for(int i=0;i<2;i++)if(sync_base_valid[i]&&sync_sent[i]!=sync_base_sent[i]&&(uint32_t)(now-sync_press_at[i])>250){
  unsigned sent=sync_sent[i]-sync_base_sent[i],got=sync_presses_delivered[i]-sync_base_delivered[i];
  int want=sync_base_req[i]^(int)(got&1u);
  if(got!=sent)fail(F_SYNC_PRESS_LOST,"deck=%d %u presses sent, %u delivered",i+3,sent,got);
  else if(v.deck[i].loaded&&v.deck[i].sync.requested!=want)
   fail(F_SYNC_PRESS_LOST,"deck=%d %u presses from requested=%d, now requested=%d",i+3,sent,sync_base_req[i],v.deck[i].sync.requested);
  sync_base_valid[i]=0;
 }
 for(int i=0;i<2;i++)if(sync_press_pending[i]&&(uint32_t)(now-sync_press_at[i])>250){
  sync_press_pending[i]=0;
  if(v.deck[i].loaded&&v.deck[i].sync.requested==sync_press_before[i])
   fail(F_SYNC_PRESS_LOST,"deck=%d requested=%d",i+3,v.deck[i].sync.requested);
 }
 /* An explicit MASTER press on a playing software deck takes the engine master,
    unless a later press or stock event moved it. */
 for(int i=0;i<2;i++)if(master_press_pending[i]&&(uint32_t)(now-master_press_at[i])>1000){
  master_press_pending[i]=0;
  if((int32_t)(last_operator_ms-master_press_at[i])<=0&&master_press_playing[i]&&software_playing(&v.deck[i])&&m!=i+2&&!foreign_slot[i])
   fail(F_EXPLICIT_MASTER,"deck=%d engine=%d observed=%d intent=%d/%u",i+3,m,v.master_actual.deck,v.master_intent.deck,v.master_intent.epoch);
 }
 /* Track whether the current engine master has played with its current track. */
 uint32_t ident=m<0||m>3?0:m<2?nat[m].identity:v.deck[m-2].timeline.load_revision^((uint32_t)m<<28);
 if(m!=master_tracked||ident!=master_ident){master_tracked=m;master_ident=ident;master_was_playing=0;}
 if(deck_playing(m,&v))master_was_playing=1;
 /* Handoff (EXTRA_DECKS.md MASTER): when the master stops, master passes to the next
    playing deck in order: from 3: 4, then the native master; from 4: native, then 3;
    a stopped native master hands over to a playing DECK 3, then DECK 4. */
 int software_candidate=(software_playing(&v.deck[0])&&!foreign_slot[0])||(software_playing(&v.deck[1])&&!foreign_slot[1]);
 int stale=m>=0&&m<=3&&master_was_playing&&deck_loaded(m,&v)&&!deck_playing(m,&v);
 int unloaded=m>=0&&m<=1&&!deck_loaded(m,&v);
 int quiet=(int32_t)(now-last_operator_ms)>200;
 /* An explicit request for a loaded, paused software deck keeps its place until it plays. */
 int target=v.master_intent.deck-2;
 int awaiting=v.master_intent.explicit_choice&&(unsigned)target<2&&v.deck[target].loaded&&!v.deck[target].playing;
 int native_playing=nat[0].loaded&&nat[0].playing?0:nat[1].loaded&&nat[1].playing?1:-1;
 int d3=software_playing(&v.deck[0])&&!foreign_slot[0],d4=software_playing(&v.deck[1])&&!foreign_slot[1];
 int s3=d3&&v.deck[0].sync.requested,s4=d4&&v.deck[1].sync.requested;
 int expected=-9;
 /* M4: a playing deck with SYNC on is preferred; then the documented order. */
 if(stale){
  if(m==2)expected=d4?3:native_playing>=0?-4:-9;
  else if(m==3)expected=s3?2:native_playing>=0?-4:d3?2:-9;
  else expected=s4&&!s3?3:d3?2:d4?3:-9;
 }
 int met=expected==-4?(m>=0&&m<2&&nat[m].playing):m==expected;
 if(held(&handoff_bad,expected!=-9&&!met&&quiet&&!awaiting,1500))
  fail(F_HANDOFF,"engine=%d expected=%d d3=%d/%d d4=%d/%d native=%d/%d intent=%d/x%u observed=%d",m,expected,v.deck[0].loaded,v.deck[0].playing,v.deck[1].loaded,v.deck[1].playing,nat[0].playing,nat[1].playing,v.master_intent.deck,v.master_intent.explicit_choice,v.master_actual.deck);
 for(int i=0;i<2;i++){
  if(m==i+2)master_seen_at[i]=now;
  int playing=software_playing(&v.deck[i]);
  /* A master choice in the last second, not the stop, decides who is master. */
  if(prev_playing[i]&&!playing&&master_seen_at[i]&&(uint32_t)(now-master_seen_at[i])<600&&
     (int32_t)(now-last_operator_ms)>1000&&!order_pending){
   int next=i==0?(d4?3:native_playing>=0?-4:-9):(s3?2:native_playing>=0?-4:d3?2:-9);
   /* VINYL: master passes at the pause press (EXTRA_DECKS.md); with nothing playing then, it stays. */
   if(pause_press_at[i]&&(uint32_t)(now-pause_press_at[i])<1500&&press_next[i]==-9)next=-9;
   if(next!=-9){cov_handoff_order++;order_pending=1;order_expected=next;order_from=i+2;order_at=now;
    order_alt=pause_press_at[i]&&(uint32_t)(now-pause_press_at[i])<1500?press_next[i]:-9;}
  }
  prev_playing[i]=playing;
 }
 if(order_pending&&(int32_t)(last_operator_ms-order_at)>0)order_pending=0;
 /* A later transport press on a software deck changes what the next master should be. */
 for(int i=0;i<2;i++)if(order_pending&&play_press_at[i]&&(int32_t)(play_press_at[i]-order_at)>0)order_pending=0;
 if(order_pending&&native_transport_at&&(int32_t)(native_transport_at-order_at)>0)order_pending=0;
 if(order_pending&&order_alt!=-9&&native_transport_at&&(int32_t)(native_transport_at-pause_press_at[order_from-2])>0)order_pending=0;
 if(order_pending&&(uint32_t)(now-order_at)>1500){
  order_pending=0;
  int still=order_expected==-4?native_playing>=0:order_expected==2?d3:d4;
  int ok=order_expected==-4?(m>=0&&m<2&&nat[m].playing):m==order_expected;
  if(order_alt!=-9)ok|=order_alt==-4?(m>=0&&m<2&&nat[m].playing):m==order_alt;
  if(still&&!ok)fail(F_HANDOFF_ORDER,"from=%d expected=%d engine=%d d3=%d/%d d4=%d/%d native=%d/%d",order_from,order_expected,m,
   v.deck[0].loaded,v.deck[0].playing,v.deck[1].loaded,v.deck[1].playing,nat[0].playing,nat[1].playing);
 }
 for(int i=0;i<2;i++){
  if(doubles_short[i]){fail(F_SYNC_PRESS_LOST,"deck=%d SYNC press of %u ms ran Instant Doubles (needs 1 s or more, p.65)",i+3,doubles_short[i]-1);doubles_short[i]=0;}
  if(doubles_count_bad[i]){fail(F_SYNC_PRESS_LOST,"deck=%d SYNC held 1 s with the hold notification: Instant Doubles ran %u times",i+3,doubles_count_bad[i]-1);doubles_count_bad[i]=0;}
 }
 /* M1: MASTER on a DECK 3/4 shows no pending notice; the lamps show the change when it commits. */
 if(strstr(v.notice,"MASTER REQUESTED"))fail(F_MASTER_NOTICE,"notice \"%s\" shown (stock shows the master change on the lamps only)",v.notice);
 /* M2: a paused native master, with the other native deck stopped, hands master to a playing DECK 3/4 at once. */
 int other_native_playing=m>=0&&m<2&&nat[1-m].loaded&&nat[1-m].playing;
 int gap_now=(uint32_t)(now-last_consumer_ms)>XZ_MASTER_MAX_AGE_MS||(int32_t)(consumer_gap_until-now)>0;
 /* An explicit MASTER choice the engine has not taken yet keeps its place (stop handoffs defer to it). */
 int explicit_pending=v.master_intent.explicit_choice&&v.master_intent.deck!=m;
 /* A deck braking after its own pause press (VINYL) reads playing but never takes master. */
 int live_sw=0;
 for(int i=0;i<2;i++){int pausing=pause_press_at[i]&&pause_press_at[i]==play_press_at[i]&&(uint32_t)(now-pause_press_at[i])<3500;
  if(software_playing(&v.deck[i])&&!v.deck[i].held_playing&&!foreign_slot[i]&&!pausing)live_sw=1;}
 /* Known limit: a native master is handed over only once the native clock showed it playing (one clock
    period); one paused within that period of becoming master stays. */
 if(!(stale&&m>=0&&m<2))native_stale_from=now;
 int observable=(int32_t)(native_stale_from-det_cur_at)>(int32_t)(2*sch.clock_ms+2*sch.worker_ms);
 int paused_native=stale&&m>=0&&m<2&&observable&&!nat[m].touch&&!other_native_playing&&live_sw&&!awaiting&&!explicit_pending&&!gap_now&&
  (int32_t)(now-global_disturb_ms)>0&&(int32_t)(now-last_operator_ms)>0;
 if(held(&pause_bad,paused_native,250))
  fail(F_PAUSE_NOT_HANDED,"native DECK %d master paused %u ms ago, other native stopped, DECK 3/4 playing: master stayed (stock M2: at once)",m+1,now-pause_bad.at);
 /* M5: a new track on the master makes the other playing deck master. */
 if(load_pending&&(int32_t)(last_operator_ms-load_at)>0)load_pending=0;
 if(load_pending&&m!=load_from)load_pending=0; /* master moved on from the loading deck */
 if(load_pending&&(uint32_t)(now-load_at)>600){
  load_pending=0;
  int others=load_from>=2?(deck_playing(5-load_from,&v)||native_playing>=0):d3||d4; /* another deck played at the load and still does */
  if(m==load_from&&others&&route_is_up&&!explicit_pending)fail(F_LOAD_NOT_HANDED,"DECK %d loaded a track while master with another deck playing: still master 600 ms later",load_from+1);
 }
 /* M6: while a loaded DECK 3/4 stays the chosen master, the engine never names a native deck nobody chose
    (a seek, beat jump, reload, VINYL pause or hold on it renews its lease, nothing else). */
 int chosen=v.master_intent.deck-2;
 if((int32_t)(now-global_disturb_ms)<=300)chosen_live=0;
 else if(m>=2&&m==v.master_intent.deck){chosen_live=1;chosen_epoch=v.master_intent.epoch;}
 else if(chosen<0||chosen>1||v.master_intent.epoch!=chosen_epoch)chosen_live=0;
 int unchosen=chosen_live&&m>=0&&m<2&&route_is_up&&v.deck[chosen].loaded&&!foreign_slot[chosen]&&
  (int32_t)(now-last_choice_ms)>200&&(int32_t)(now-global_disturb_ms)>300&&!gap_now;
 if(held(&unchosen_bad,unchosen,150))
  fail(F_MASTER_UNCHOSEN,"engine names native DECK %d while loaded DECK %d is still the chosen master",m+1,chosen+3);
 if(held(&unloaded_bad,unloaded&&software_candidate&&quiet,1500))
  fail(F_HANDOFF_UNLOADED,"engine=%d unloaded d3=%d/%d d4=%d/%d",m,v.deck[0].loaded,v.deck[0].playing,v.deck[1].loaded,v.deck[1].playing);
 /* No master while a software deck plays with SYNC requested by any deck. */
 int wants=v.deck[0].sync.requested||v.deck[1].sync.requested;
 if(held(&none_bad,m==-1&&software_candidate&&wants&&quiet&&!awaiting,1500))
  fail(F_NONE_ELECTION,"engine=-1 d3=%d/%d sync=%d d4=%d/%d sync=%d",v.deck[0].loaded,v.deck[0].playing,v.deck[0].sync.requested,v.deck[1].loaded,v.deck[1].playing,v.deck[1].sync.requested);
 /* p.64: nobody has chosen a master yet (stock never set one) and a DECK 3/4 plays an analysed track
    first: it becomes master, as the first deck to play does on stock decks 1/2. */
 if(held(&first_bad,!operator_events&&m==-1&&software_candidate,1000))
  fail(F_FIRST_PLAY,"engine=-1 no master chosen yet d3=%d/%d d4=%d/%d",v.deck[0].loaded,v.deck[0].playing,v.deck[1].loaded,v.deck[1].playing);
 /* A synced, playing software deck follows a playing master and locks. */
 for(int i=0;i<2;i++){
  const struct xz_extra_deck *d=&v.deck[i];
  int follow=d->loaded&&d->playing&&d->sync.requested&&d->quantize&&m>=0&&m<=3&&m!=i+2&&deck_playing(m,&v)&&!foreign_slot[i];
  if(m>=2&&foreign_slot[m-2])follow=0;
  uint32_t sig=follow?(uint32_t)(m+1)*7919u^d->timeline.load_revision*31u^d->timeline.transport_generation*131u^(uint32_t)d->tempo_range:0;
  if(m>=0&&m<2)sig^=nat[m].revision*17u;
  if(m>=2&&m<=3)sig^=v.deck[m-2].timeline.load_revision*1031u;
  if(sig!=lock_signature[i]){lock_signature[i]=sig;lock_signature_at[i]=now;}
  int settled=follow&&(uint32_t)(now-lock_signature_at[i])>2500&&quiet;
  if(settled){cov_lock++;if(d->sync.locked)cov_locked++;}
  if(held(&lock_bad[i],settled&&!d->sync.locked,1500))
   fail(F_SYNC_NOT_LOCKED,"deck=%d engine=%d reason=%d master_deck=%d source=%d tempo_matched=%d observed=%d",i+3,m,d->sync.reason,d->sync.master_deck,d->sync.source,d->sync.tempo_matched,v.master_actual.deck);
  if(held(&wrong_bad[i],settled&&d->sync.locked&&d->sync.master_deck!=m,300))
   fail(F_SYNC_WRONG_MASTER,"deck=%d engine=%d locked-to=%d",i+3,m,d->sync.master_deck);
 }
}

/* ------------------------------------------- lock quality, on what is actually heard */
static uint32_t deck_bpm_x100(const struct xz_extra_deck *d){
 for(unsigned k=0;k<TRACKS;k++)if(d->loaded&&!strcmp(d->title,tracks[k].title))return tracks[k].bpm_x100;
 return 0;
}
static double wrap_unit(double x){return x-floor(x+.5);} /* [-0.5, 0.5) */
/* Disturbances end a settled window; checks restart from the last one. Logical decks 0..3. */
/* disturb_ms restarts a lock window; reset_ms also marks an event that finds the beat again (the kept
   offset ends); nudge_ms is the end of a side nudge on that deck (the offset model's input). */
static uint32_t disturb_ms[4],reset_ms[4],nudge_ms[4],global_disturb_ms,lock_master_change_ms,sync_on_ms[2];
static int32_t lock_master_seen=-9;static int sync_seen[2];
static int32_t hold_master=-9,hold_tempo_bpm,last_view_bpm[2];static uint32_t hold_since,hold_tempo_at;static int hold_tempo_deck=-1;
/* S8 (owner-observed stock): a side nudge on the master is an alignment offset; followers do not re-snap. */
static int nudge_live,nudge_deck;static uint32_t nudge_at,nudge_jumps[2];
static void global_disturb(uint32_t until){if((int32_t)(until-global_disturb_ms)>0)global_disturb_ms=until;}
static void disturb_keep(int logical,uint32_t until){if(logical>=0&&logical<4&&(int32_t)(until-disturb_ms[logical])>0)disturb_ms[logical]=until;}
static void disturb(int logical,uint32_t until){
 disturb_keep(logical,until);if(logical>=0&&logical<4&&(int32_t)(until-reset_ms[logical])>0)reset_ms[logical]=until;
}
static void nudge_mark(int logical,uint32_t until){if(logical>=0&&logical<4)nudge_ms[logical]=until|1u;}
/* Every engine master change, millisecond by millisecond: a master held for under 300 ms that no
   press chose, between two others, is a detour (followers take its tempo meanwhile). */
static int32_t det_prev=-9,det_cur=-9;static uint32_t det_cur_at,det_gen,det_load,det_change_ms;static int det_intended;
static void track_engine(void){
 int32_t m=engine_master();uint32_t now=now32();
 struct xz_extra_decks v;
 if(m==det_cur){
  /* While a software deck leads, note when its load or transport generation last changed. */
  if(m>=2&&m<=3){
   xz_xp_read(p,&v);uint32_t g=v.deck[m-2].timeline.transport_generation,l=v.deck[m-2].timeline.load_revision;
   if(g!=det_gen||l!=det_load){det_gen=g;det_load=l;det_change_ms=now;}
  }
  return;
 }
 xz_xp_read(p,&v);
 /* The reviewed bridge contract returns the old master while the same DECK 3/4 is briefly without a
    publishable beat for a reason other than a VINYL hold: a new load or transport generation (seek,
    snap), its own VINYL brake, or a stalled publisher. A detour on the way to the other deck is not. */
 int sw=det_prev>=2&&det_prev<=3,same=sw&&m==det_prev;
 /* A VINYL pause with no deck to hand to at the press keeps master on the braking deck. */
 int lone_brake=sw&&pause_press_at[det_prev-2]&&(int32_t)(pause_press_at[det_prev-2]+1500-det_cur_at)>0&&press_next[det_prev-2]==-9;
 /* M6: a seek, beat jump, reload or VINYL brake on it no longer excuses the pass (no contract exemption). */
 (void)same;(void)lone_brake;
 int contract=(int32_t)(global_disturb_ms+300-det_cur_at)>0;
 if(det_cur!=-9&&det_prev!=-9&&det_cur!=m&&det_cur>=0&&det_cur<2&&(det_prev>=2||m>=2)&&!det_intended&&
    (uint32_t)(now-det_cur_at)<300&&(int32_t)(det_cur_at-last_choice_ms)>200&&!contract)
  fail(F_MASTER_DETOUR,"engine DECK %d -> DECK %d for %u ms -> DECK %d",det_prev+1,det_cur+1,now-det_cur_at,m+1);
 if(m>=2&&m<=3){det_gen=v.deck[m-2].timeline.transport_generation;det_load=v.deck[m-2].timeline.load_revision;}
 det_prev=det_cur;det_cur=m;det_cur_at=now;det_intended=m>=0&&m<2&&v.master_intent.deck==m;
}
static int lock_trace;static uint32_t trace_start;static double pub_log;static uint64_t seed_value;
/* Offset model (stock S4, S7, S8): a side nudge on a synced follower or on its master leaves an alignment
   offset that SYNC keeps (tempo matched, lamp flashing) until an event that finds the beat again (SYNC
   pressed, pause, jump, load, hold, master change). expect_ms is the offset the oracle measured once the
   nudge settled; the lock checks hold the follower to it, and any snap while it is kept is a forced
   realignment. Tempo changes on the master keep it (S7). */
struct lock_state { int active,acquired,reset_known,expect_set,nudge_pending,last_valid; double expect_ms,last_err;
 uint32_t ok_since,jumps,from,reset_from,nudge_seen_f,nudge_seen_m,nudge_until,nudge_last;
 struct since tempo_bad,avg_bad,phase_bad,drift_bad,silent_bad,flag_bad,offset_bad,offset_false; };
static struct lock_state lk[2];
/* Tolerances. Phase: the fixture observes completions once per USB burst and worker pass, so
   its measurement noise is bounded by those periods; a lock sits well inside it. */
#define LOCK_TEMPO_SETTLE_MS 1500u
#define LOCK_PHASE_SETTLE_MS 4000u
#define LOCK_SILENT_MS 5000u
#define LOCK_NUDGE_SETTLE_MS 350u  /* after the bend ends, before the kept offset is measured */
#define LOCK_TEMPO_INSTANT 0.0125  /* bounded phase trim (1%) plus bend decay */
#define LOCK_TEMPO_AVERAGE 0.0025  /* 1 s average while QUANTIZE locks the phase */
#define LOCK_TEMPO_ONLY 0.0005     /* QUANTIZE off: tempo only, no trim */
static double lock_phase_tol_ms(void){return 3.+sch.usb_burst+sch.worker_ms;}
static unsigned long cov_lock_samples,cov_lock_phase,cov_lock_ok,cov_pub_served,cov_pub_refused,cov_pub_checked,cov_offset_kept;
static double lock_worst_ms,pub_worst_ms,flutter_sum,flutter_max;static unsigned long flutter_n,locked_n;
/* M4: whether the master was held on its platter top when master moved; S2: SYNC off presses. */
static int last_master_held,last_view_sync[2];static uint32_t soff_at[2];static double soff_rate[2];static int soff_pending[2];
static uint32_t latest(const uint32_t *marks,unsigned n,uint32_t from){
 for(unsigned k=0;k<n;k++)if((int32_t)(marks[k]-from)>0)from=marks[k];
 return from;
}
/* One lock sample per software follower: its actual phase and tempo against the engine master's. */
static void check_lock(void){
 struct xz_extra_decks v;xz_xp_read(p,&v);
 uint32_t now=now32();int32_t m=engine_master();
 /* M4: holding the master's platter top (VINYL) with a synced, playing DECK 3/4 hands master to it within
    2 s, and to a synced deck rather than an unsynced one. A native or a DECK 3/4 master. */
 int synced_playing=-1;
 for(int i=0;i<2;i++)if(v.deck[i].loaded&&v.deck[i].playing&&v.deck[i].sync.requested&&!foreign_slot[i]&&m!=i+2){synced_playing=i;break;}
 int master_held=(m>=0&&m<2&&nat[m].loaded&&nat[m].playing&&nat[m].touch)||
  (m>=2&&m<=3&&v.deck[m-2].held_playing&&v.deck[m-2].platter_contact);
 if(master_held&&synced_playing>=0){
  if(hold_master!=m){hold_master=m;hold_since=now;}
  else if((uint32_t)(now-hold_since)>2000)
   fail(F_HOLD_NOT_HANDED,"DECK %d held %u ms on its platter top (VINYL) with synced DECK %d playing: master stayed",m+1,now-hold_since,synced_playing+3);
 }else hold_master=-9;
 if(m!=lock_master_seen){
  /* M4: from a held master, master goes to a synced playing deck when there is one. */
  if(last_master_held&&m>=2&&m<=3&&!last_view_sync[m-2]){
   int other=3-m;
   if(v.deck[other].loaded&&v.deck[other].playing&&v.deck[other].sync.requested&&lock_master_seen!=other+2)
    fail(F_HOLD_NOT_HANDED,"held DECK %d handed master to unsynced DECK %d while synced DECK %d played",lock_master_seen+1,m+1,other+3);
  }
  /* M6: a DECK 3/4 that takes master after following keeps the shared synced BPM. */
  if(lock_master_seen>=0&&m>=2&&last_view_bpm[m-2]>0&&last_view_sync[m-2]){
   hold_tempo_deck=m-2;hold_tempo_bpm=last_view_bpm[m-2];hold_tempo_at=now;
  }
  lock_master_seen=m;lock_master_change_ms=now;
 }
 last_master_held=master_held;
 if(hold_tempo_deck>=0&&(uint32_t)(now-hold_tempo_at)>=1000){
  int32_t b=v.deck[hold_tempo_deck].bpm_x100;
  if(m==hold_tempo_deck+2&&v.deck[hold_tempo_deck].playing&&abs(b-hold_tempo_bpm)>hold_tempo_bpm/500+2&&
     (int32_t)(disturb_ms[hold_tempo_deck+2]-hold_tempo_at)<=0&&(int32_t)(global_disturb_ms-hold_tempo_at)<=0)
   fail(F_HANDOFF_TEMPO,"DECK %d took master at the synced %.2f BPM, 1 s later %.2f",hold_tempo_deck+3,hold_tempo_bpm/100.,b/100.);
  hold_tempo_deck=-1;
 }
 for(int i=0;i<2;i++){last_view_bpm[i]=v.deck[i].loaded?v.deck[i].bpm_x100:0;last_view_sync[i]=v.deck[i].loaded&&v.deck[i].sync.requested;}
 if(nudge_live&&m==nudge_deck&&(uint32_t)(now-nudge_at)<=4000){
  for(int i=0;i<2;i++)if(act_jumps[i]!=nudge_jumps[i]&&v.deck[i].sync.requested&&(int32_t)(reset_ms[i+2]-nudge_at)<=0){
   fail(F_UNPROMPTED_SNAP,"deck=%d re-snapped %u ms after a side nudge on native DECK %d (stock S8 keeps the offset)",
    i+3,act_jump_ms[i]-nudge_at,nudge_deck+1);
   nudge_live=0;break;
  }
 }else nudge_live=0;
 for(int i=0;i<2;i++){
  const struct xz_extra_deck *d=&v.deck[i];struct lock_state *s=&lk[i];
  int on=d->loaded&&d->sync.requested;
  if(on&&!sync_seen[i])sync_on_ms[i]=now;
  sync_seen[i]=on;
  int follow=route_is_up&&on&&m>=0&&m<=3&&m!=i+2&&!foreign_slot[i]&&!(m>=2&&foreign_slot[m-2]);
  if(!follow){uint32_t j=act_jumps[i];memset(s,0,sizeof *s);s->jumps=j;continue;}
  /* A re-acquiring event clears the kept offset. A new side nudge on this deck or its master makes one. */
  uint32_t rmarks[]={reset_ms[i+2],reset_ms[m],lock_master_change_ms,global_disturb_ms};
  uint32_t rf=latest(rmarks,4,sync_on_ms[i]);
  if(!s->reset_known||rf!=s->reset_from){
   s->reset_known=1;s->reset_from=rf;s->expect_ms=0;s->expect_set=s->nudge_pending=0;s->jumps=act_jumps[i];
   s->nudge_seen_f=nudge_ms[i+2];s->nudge_seen_m=nudge_ms[m];
  }
  if(nudge_ms[i+2]!=s->nudge_seen_f||nudge_ms[m]!=s->nudge_seen_m){
   s->nudge_seen_f=nudge_ms[i+2];s->nudge_seen_m=nudge_ms[m];
   uint32_t until=(int32_t)(nudge_ms[i+2]-nudge_ms[m])>0?nudge_ms[i+2]:nudge_ms[m];
   s->nudge_pending=1;s->nudge_until=until+LOCK_NUDGE_SETTLE_MS;s->nudge_last=until;
  }
  double W=(double)act_end[i]-2.;
  if(m>=2){double w2=(double)act_end[m-2]-2.;if(w2<W)W=w2;}
  double T=act_time_us(W),sF=act_at(i,W),rF=act_rate(i,W,441.);
  uint32_t bF=deck_bpm_x100(d);
  int fresh=(double)sim_us-T<30000.;
  int fplaying=fresh&&d->playing&&!isnan(rF)&&rF>.05&&bF;
  double beatsM=NAN,bpmM=NAN;
  if(m<2){
   const struct native *n=&nat[m];
   if(n->loaded&&n->playing&&!n->touch&&!n->brake&&!((int32_t)(n->bend_until-now)>0)){beatsM=nat_beats_at(m,T);bpmM=n->bpm;}
  }else{
   const struct xz_extra_deck *md=&v.deck[m-2];uint32_t bM=deck_bpm_x100(md);
   double sM=act_at(m-2,W),rM=act_rate(m-2,W,441.);
   if(md->playing&&bM&&!isnan(rM)&&rM>.05){beatsM=track_beats(bM,sM);bpmM=bM/100.*rM;}
  }
  if(!fplaying||isnan(beatsM)||isnan(bpmM))continue; /* keep the window and the kept offset meanwhile */
  uint32_t marks[]={disturb_ms[i+2],disturb_ms[m]};
  uint32_t from=latest(marks,2,rf);
  uint32_t since=(int32_t)(now-from)>0?now-from:0;
  if(!s->active||s->from!=from){s->active=1;s->acquired=0;s->ok_since=0;s->from=from;
   memset(&s->tempo_bad,0,sizeof s->tempo_bad);memset(&s->avg_bad,0,sizeof s->avg_bad);memset(&s->phase_bad,0,sizeof s->phase_bad);
   memset(&s->drift_bad,0,sizeof s->drift_bad);memset(&s->silent_bad,0,sizeof s->silent_bad);}
  int quantize=d->quantize&&d->quantize_den;
  double err=wrap_unit((track_beats(bF,sF)-beatsM))*60000./bpmM,tol=lock_phase_tol_ms(),beat_ms=60000./bpmM;
  /* A realignment moves the alignment: a transport discontinuity that leaves it in place is not one. */
  double moved=s->last_valid?fabs(wrap_unit((err-s->last_err)/beat_ms))*beat_ms:0;
  s->last_err=err;s->last_valid=1;
  if(act_jumps[i]!=s->jumps){
   if(quantize&&(s->expect_set||s->nudge_pending)&&!d->loop.active&&moved>1.)
    fail(F_UNPROMPTED_SNAP,"deck=%d re-snapped after a side nudge (%s); stock S4/S8 keep the offset",i+3,s->expect_set?"offset kept":"while settling");
   else if(since>LOCK_PHASE_SETTLE_MS&&s->acquired&&!d->loop.active)
    fail(F_UNPROMPTED_SNAP,"deck=%d master=%d jumped while locked (phase was within %.1fms) since=%u reason=%d",i+3,m+1,tol,since,d->sync.reason);
   s->jumps=act_jumps[i];
  }
  if(s->nudge_pending&&(int32_t)(now-s->nudge_until)<0)continue; /* the nudge itself */
  if(s->nudge_pending){s->nudge_pending=0;if(quantize){s->expect_ms=err;s->expect_set=1;}s->ok_since=0;
   if(lock_trace)printf("  offset t=%u deck=%d kept=%.2fms\n",now-trace_start,i+3,err);}
  cov_lock_samples++;
  double bpmF=bF/100.*rF,tempo=bpmF/bpmM-1.;
  if(held(&s->tempo_bad,since>LOCK_TEMPO_SETTLE_MS&&fabs(tempo)>(quantize?LOCK_TEMPO_INSTANT:LOCK_TEMPO_ONLY*2),300))
   fail(F_TEMPO_MISMATCH,"deck=%d master=%d follower=%.3f master=%.3f BPM err=%.2f%% since=%u reason=%d matched=%d",i+3,m+1,bpmF,bpmM,tempo*100,since,d->sync.reason,d->sync.tempo_matched);
  /* One second average: the follower's own tempo, without the phase trim or a nudge in the window. */
  double r1=act_rate(i,W,44100.);
  if(!isnan(r1)&&since>LOCK_TEMPO_SETTLE_MS+1200&&(uint32_t)(now-s->nudge_last)>1300){
   double bpmM1=bpmM;
   if(m>=2){uint32_t bM=deck_bpm_x100(&v.deck[m-2]);double rM1=act_rate(m-2,W,44100.);bpmM1=isnan(rM1)?NAN:bM/100.*rM1;}
   double avg=isnan(bpmM1)?0:(bF/100.*r1)/bpmM1-1.;
   if(held(&s->avg_bad,fabs(avg)>(quantize?LOCK_TEMPO_AVERAGE:LOCK_TEMPO_ONLY),200))
    fail(F_TEMPO_MISMATCH,"deck=%d master=%d 1s-average err=%.3f%% follower-track=%u master-bpm=%.3f since=%u reason=%d",i+3,m+1,avg*100,bF,bpmM1,since,d->sync.reason);
  }
  if(!quantize){
   if(held(&s->silent_bad,since>LOCK_SILENT_MS&&!d->sync.tempo_matched,500))
    fail(F_SYNC_SILENT,"deck=%d master=%d tempo-only, never matched: reason=%d source=%d",i+3,m+1,d->sync.reason,d->sync.source);
   continue;
  }
  /* Phase against the kept offset (zero unless a nudge made one). */
  double rel=err-s->expect_ms;rel-=floor(rel/beat_ms+.5)*beat_ms;
  cov_lock_phase++;
  if(lock_trace&&lock_trace-3==i)printf("  lock t=%u deck=%d m=%d since=%u reason=%d locked=%d offset=%d engine-err=%dus trim=%dppm actual=%.2fms kept=%.2fms tempo=%.3f%% jumps=%u\n",
   now-trace_start,i+3,m+1,since,d->sync.reason,d->sync.locked,SYNC_OFFSET(d),d->sync.phase_error_us,d->sync.correction_ppm,err,s->expect_ms,tempo*100,act_jumps[i]);
  int ok=fabs(rel)<=tol;
  if(ok){if(!s->ok_since)s->ok_since=now|1u;if((int32_t)(now-s->ok_since)>1000)s->acquired=1;cov_lock_ok++;}
  else s->ok_since=0;
  if(s->expect_set)cov_offset_kept++;
  if(since>LOCK_PHASE_SETTLE_MS&&fabs(rel)>lock_worst_ms)lock_worst_ms=fabs(rel);
  if(since>LOCK_PHASE_SETTLE_MS){flutter_n++;flutter_sum+=fabs(tempo);if(fabs(tempo)>flutter_max)flutter_max=fabs(tempo);if(d->sync.locked)locked_n++;}
  if(held(&s->phase_bad,since>LOCK_PHASE_SETTLE_MS&&!s->acquired&&!ok,1000))
   fail(F_PHASE_NOT_ACQUIRED,"deck=%d master=%d phase=%.1fms kept=%.1fms tol=%.1f since=%u reason=%d locked=%d trim=%dppm tempo=%.2f%%",i+3,m+1,err,s->expect_ms,tol,since,d->sync.reason,d->sync.locked,d->sync.correction_ppm,tempo*100);
  /* Leaving a kept offset is a chase (the trim closing it), which stock does not do (S4, S7, S8). */
  if(held(&s->drift_bad,s->acquired&&!ok,1300))
   fail(F_PHASE_DRIFT,"deck=%d master=%d phase=%.1fms kept=%.1fms tol=%.1f since=%u reason=%d locked=%d trim=%dppm",i+3,m+1,err,s->expect_ms,tol,since,d->sync.reason,d->sync.locked,d->sync.correction_ppm);
  if(held(&s->silent_bad,since>LOCK_SILENT_MS&&!d->sync.locked,500))
   fail(F_SYNC_SILENT,"deck=%d master=%d SYNC on, never locked: reason=%d source=%d matched=%d phase=%.1fms",i+3,m+1,d->sync.reason,d->sync.source,d->sync.tempo_matched,err);
  if(held(&s->flag_bad,since>LOCK_PHASE_SETTLE_MS&&d->sync.locked&&fabs(rel)>3*tol,200))
   fail(F_LOCK_FLAG,"deck=%d master=%d reads LOCKED at phase %.1fms (kept %.1fms)",i+3,m+1,err,s->expect_ms);
  /* S4: a kept offset is shown (the SYNC lamp flashes from it); none is shown without a nudge. */
  double kept=fabs(s->expect_ms-floor(s->expect_ms/beat_ms+.5)*beat_ms);
  if(held(&s->offset_bad,s->expect_set&&ok&&kept>XZ_XP_SYNC_OFFSET_MS+tol&&!SYNC_OFFSET(d),300))
   fail(F_OFFSET_FLAG,"deck=%d keeps a %.1f ms offset after a nudge but does not show it",i+3,s->expect_ms);
  if(held(&s->offset_false,!s->expect_set&&s->acquired&&since>LOCK_PHASE_SETTLE_MS&&SYNC_OFFSET(d),300))
   fail(F_OFFSET_FLAG,"deck=%d shows a kept offset with no nudge since its last alignment",i+3);
 }
}
/* S2: SYNC off does not step the speed, and the deck keeps the synced BPM. Checked per 10 ms sample over
   the 400 ms after the press (the trim may fade, never step) and 1.5 s later against the master. */
static void check_sync_off(void){
 struct xz_extra_decks v;
 int any=soff_pending[0]||soff_pending[1];if(!any)return;
 xz_xp_read(p,&v);uint32_t now=now32();int32_t m=engine_master();
 for(int i=0;i<2;i++){
  if(!soff_pending[i])continue;
  /* Anything else done to this deck or its master since the press ends the check. */
  if((int32_t)(disturb_ms[i+2]-soff_at[i])>0||(m>=0&&m<=3&&(int32_t)(disturb_ms[m]-soff_at[i])>0)||(m>=0&&m<=3&&(int32_t)(nudge_ms[m]-soff_at[i])>0)||
     (int32_t)(nudge_ms[i+2]-soff_at[i])>0||(int32_t)(lock_master_change_ms-soff_at[i])>0||(int32_t)(global_disturb_ms-soff_at[i])>0){soff_pending[i]=0;continue;}
  double W=(double)act_end[i]-2.,rF=act_rate(i,W,441.);uint32_t after=now-soff_at[i];
  if(!isnan(rF)&&after<=400){
   if(soff_rate[i]>0&&fabs(rF/soff_rate[i]-1.)>.0015)
    fail(F_SYNC_OFF_STEP,"deck=%d speed stepped %.3f%% at SYNC off (%u ms after the press)",i+3,(rF/soff_rate[i]-1.)*100.,after);
   soff_rate[i]=rF;
  }
  if(after<1500)continue;
  soff_pending[i]=0;
  if(m<0||m>3||m==i+2||(int32_t)(disturb_ms[i+2]-soff_at[i])>0||(m>=0&&(int32_t)(disturb_ms[m]-soff_at[i])>0)||
     (int32_t)(lock_master_change_ms-soff_at[i])>0||(int32_t)(global_disturb_ms-soff_at[i])>0)continue;
  uint32_t bF=deck_bpm_x100(&v.deck[i]);double r1=act_rate(i,W,44100.),bpmM1=NAN;
  if(m<2){if(nat[m].loaded&&nat[m].playing&&!nat[m].touch&&!nat[m].brake)bpmM1=nat[m].bpm;}
  else{uint32_t bM=deck_bpm_x100(&v.deck[m-2]);double rM1=act_rate(m-2,W,44100.);bpmM1=isnan(rM1)?NAN:bM/100.*rM1;}
  if(bF&&!isnan(r1)&&!isnan(bpmM1)&&fabs(bF/100.*r1/bpmM1-1.)>.0008)
   fail(F_SYNC_OFF_STEP,"deck=%d 1.5 s after SYNC off plays %.3f BPM, master %.3f (not the synced BPM)",i+3,bF/100.*r1,bpmM1);
 }
}
/* What a stock follower is handed when the engine follows virtual player 3/4: the beat packet's
   timeline and tempo, against what that deck actually plays at the same moment. */
static struct since pub_phase_bad[2],pub_tempo_bad[2],pub_stopped_bad[2],pub_gap_bad[2],pub_nudge_bad[2];
static double pub_tol_ms(void){return 2.+sch.usb_burst+sch.worker_ms;}
static uint32_t rd32(const unsigned char *b){return (uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;}
static void check_publication(int id,const unsigned char *status,int status_rc,const unsigned char *beat,int beat_rc){
 int i=id-3;if(i<0||i>1||foreign_slot[i]||!p)return;
 struct xz_extra_decks v;xz_xp_read(p,&v);
 const struct xz_extra_deck *d=&v.deck[i];uint32_t b=deck_bpm_x100(d);
 double W=(double)act_end[i]-2.,T=act_time_us(W),s=act_at(i,W),r=act_rate(i,W,220.),r4=act_rate(i,W,882.);
 int moving=!isnan(r)&&r>.05,steady=moving&&!isnan(r4)&&fabs(r4-r)<.002*r;
 int nudging=nudge_ms[i+2]&&(int32_t)(now32()-nudge_ms[i+2])<=150;
 int quiet=(int32_t)(now32()-disturb_ms[i+2])>150&&(int32_t)(now32()-global_disturb_ms)>150&&!nudging;
 /* S8 rule 2: a side nudge bends only this deck's own playback. What a follower takes is the tempo
    setting (the deck's BPM), never the bent speed. */
 if(nudging&&!status_rc&&!beat_rc&&b&&d->bpm_x100>0){
  uint32_t q=rd32(beat+0x18),bp=rd32(beat+0x1c);double pub=bp/100.*(q/1048576.),set=d->bpm_x100/100.;
  if(held(&pub_nudge_bad[i],fabs(pub/set-1.)>.001,20))
   fail(F_PUB_TEMPO,"deck=%d published %.3f BPM during a side nudge; its tempo is %.3f BPM (stock S8: followers do not chase a nudge)",i+3,pub,set);
 }else pub_nudge_bad[i].on=0;
 if(status_rc||beat_rc){
  cov_pub_refused++;
  if(held(&pub_gap_bad[i],steady&&quiet&&d->playing&&b,150))
   fail(F_PUB_GAP,"deck=%d engine follows it, playing steadily, status/beat refused %d/%d",i+3,status_rc,beat_rc);
  return;
 }
 pub_gap_bad[i].on=0;cov_pub_served++;
 if(held(&pub_stopped_bad[i],!moving,120))fail(F_PUB_STOPPED,"deck=%d published as playing while stopped",i+3);
 if(!steady||!b||isnan(s))return;
 cov_pub_checked++;
 uint32_t next=rd32(beat),q20=rd32(beat+0x18),bpm100=rd32(beat+0x1c);unsigned bib=beat[0x20];
 int32_t sec,usec;memcpy(&sec,beat+0x24,4);memcpy(&usec,beat+0x28,4);
 double speed=q20/1048576.,origin=(double)sec*1e6+usec-1.7e15;
 double bar=(double)(bib-1)+(T-origin)/1000.*speed/next,actual=fmod(track_beats(b,s),4.);
 if(actual<0)actual+=4;
 double e=fmod(bar-actual+6.,4.)-2.,err=e*next/speed;
 if(quiet&&fabs(err)>pub_worst_ms)pub_worst_ms=fabs(err);
 if(pub_log&&quiet&&fabs(err)>pub_log)printf("  publish seed=%llu t=%u deck=%d err=%.2fms published-beat=%.3f plays=%.3f speed=%.5f/%.5f bib=%u next=%u jumps=%u\n",
  (unsigned long long)seed_value,now32()-trace_start,i+3,err,bar,actual,speed,r,bib,next,act_jumps[i]);
 if(held(&pub_phase_bad[i],quiet&&fabs(err)>pub_tol_ms(),100))
  fail(F_PUB_PHASE,"deck=%d published beat %.2f, plays beat %.2f: %.1fms (tol %.1f) speed pub=%.5f actual=%.5f",i+3,bar,actual,err,pub_tol_ms(),speed,r);
 /* S8 rule 2: the published tempo is the deck's tempo setting (its BPM); a trim still in the queued audio
    (a follower that just became master) or a nudge moves the position, which the phase check covers. */
 double pub_bpm=bpm100/100.*speed,set_bpm=d->bpm_x100/100.;
 if(held(&pub_tempo_bad[i],quiet&&set_bpm>0&&fabs(pub_bpm/set_bpm-1.)>.001,100))
  fail(F_PUB_TEMPO,"deck=%d published %.3f BPM (track %u x %.5f), its tempo is %.3f BPM (plays %.3f)",i+3,pub_bpm,bpm100,speed,set_bpm,b/100.*r);
 (void)status;
}

/* ------------------------------------------------------------ one run */
static unsigned trace_on,trace_every,trace_from;
static void trace(const char *what,int side,int arg){
 if(!trace_on)return;
 char b[512];sim_bridge_debug(b,sizeof b);struct xz_extra_decks v;xz_xp_read(p,&v);
 printf("  t=%u %s side=%d arg=%d engine=%d | d3 %d/%d/m%d d4 %d/%d/m%d intent=%d/%u/x%u obs=%d | %s\n",now32()-trace_start,what,side,arg,engine_master(),
  v.deck[0].loaded,v.deck[0].playing,v.deck[0].master,v.deck[1].loaded,v.deck[1].playing,v.deck[1].master,
  v.master_intent.deck,v.master_intent.epoch,v.master_intent.explicit_choice,v.master_actual.deck,b);
}
static void reset_world(void){
 memset(nat,0,sizeof nat);memset(foreign_slot,0,sizeof foreign_slot);
 operator_events=0;release_count=0;hold_count=0;fail_class=F_NONE;fail_detail[0]=0;
 memset(sync_down,0,sizeof sync_down);memset(doubles_delivered,0,sizeof doubles_delivered);memset(sync_hold_sent,0,sizeof sync_hold_sent);
 memset(doubles_short,0,sizeof doubles_short);memset(doubles_count_bad,0,sizeof doubles_count_bad);
 memset(lamp_sync_bad,0,sizeof lamp_sync_bad);memset(lamp_sync_stuck,0,sizeof lamp_sync_stuck);memset(lamp_master_bad,0,sizeof lamp_master_bad);
 memset(lamp_master_false,0,sizeof lamp_master_false);memset(jog_bad,0,sizeof jog_bad);
 memset(lamp_flash_bad,0,sizeof lamp_flash_bad);memset(jog_flash_bad,0,sizeof jog_flash_bad);memset(lamp_flip_at,0,sizeof lamp_flip_at);memset(lamp_prev,0,sizeof lamp_prev);memset(&dup_bad,0,sizeof dup_bad);
 memset(view_bad,0,sizeof view_bad);memset(lock_bad,0,sizeof lock_bad);memset(wrong_bad,0,sizeof wrong_bad);
 memset(&none_bad,0,sizeof none_bad);memset(&first_bad,0,sizeof first_bad);memset(&pause_bad,0,sizeof pause_bad);memset(&unchosen_bad,0,sizeof unchosen_bad);
 load_pending=0;chosen_live=0;chosen_epoch=0;memset(&unloaded_bad,0,sizeof unloaded_bad);order_pending=0;memset(prev_playing,0,sizeof prev_playing);press_next[0]=press_next[1]=-9;
 memset(pause_press_at,0,sizeof pause_press_at);memset(play_press_at,0,sizeof play_press_at);native_transport_at=0;memset(master_seen_at,0,sizeof master_seen_at);memset(&handoff_bad,0,sizeof handoff_bad);memset(native_bad,0,sizeof native_bad);
 memset(master_press_pending,0,sizeof master_press_pending);memset(sync_press_pending,0,sizeof sync_press_pending);
 memset(sync_sent,0,sizeof sync_sent);memset(sync_base_valid,0,sizeof sync_base_valid);
 memset(lock_signature,0,sizeof lock_signature);memset(lock_signature_at,0,sizeof lock_signature_at);
 memset(stall_until,0,sizeof stall_until);last_consumer_ms=consumer_gap_until=0;memset(sync_presses_delivered,0,sizeof sync_presses_delivered);
 memset(master_presses_delivered,0,sizeof master_presses_delivered);
 master_tracked=-9;master_was_playing=0;master_ident=0;last_operator_ms=0;last_choice_ms=0;memset(master_press_playing,0,sizeof master_press_playing);
 memset(sm_mgr,0,sizeof sm_mgr);memset(sm_master,0,sizeof sm_master);memset(sm_link,0,sizeof sm_link);
 void *smp=sm_master,*lmp=sm_link;memcpy(sm_mgr+4,&smp,sizeof smp);memcpy(sm_master+0x30,&lmp,sizeof lmp);
 int32_t none=-1;memcpy(sm_master+8,&none,4);
 /* Lock-quality state. */
 memset(act_pos,0,sizeof act_pos);memset(act_end,0,sizeof act_end);mono_wire=wire_base=0;consume_us=sim_us;
 memset(act_jumps,0,sizeof act_jumps);memset(act_jump_ms,0,sizeof act_jump_ms);memset(shadow,0,sizeof shadow);
 memset(nat_beats,0,sizeof nat_beats);memset(nat_motion,0,sizeof nat_motion);nat_hist_end=now32();
 memset(disturb_ms,0,sizeof disturb_ms);global_disturb_ms=lock_master_change_ms=0;memset(sync_on_ms,0,sizeof sync_on_ms);
 lock_master_seen=-9;hold_master=-9;hold_tempo_deck=-1;nudge_live=0;memset(last_view_bpm,0,sizeof last_view_bpm);
 memset(reset_ms,0,sizeof reset_ms);memset(nudge_ms,0,sizeof nudge_ms);last_master_held=0;memset(last_view_sync,0,sizeof last_view_sync);
 memset(soff_pending,0,sizeof soff_pending);memset(sync_seen,0,sizeof sync_seen);memset(lk,0,sizeof lk);det_prev=det_cur=-9;det_change_ms=0;
 memset(pub_phase_bad,0,sizeof pub_phase_bad);memset(pub_tempo_bad,0,sizeof pub_tempo_bad);
 memset(pub_stopped_bad,0,sizeof pub_stopped_bad);memset(pub_gap_bad,0,sizeof pub_gap_bad);memset(pub_nudge_bad,0,sizeof pub_nudge_bad);
 fail_mask=0;memset(class_at,0,sizeof class_at);deferred_count=0;
}
static void draw_policy_and_schedule(void){
 static const unsigned consumer[]={5,10,16,20,33,50,66,90};
 static const unsigned leds[]={10,16,20,33,50};
 static const unsigned workers[]={2,3,5};
 sch.worker_ms=workers[rnd_below(3)];
 sch.consumer_ms=consumer[rnd_below(8)];sch.consumer_jitter=rnd_below(3)?0:rnd_below(sch.consumer_ms/2+1);
 sch.clock_ms=25+rnd_below(20);
 for(int i=0;i<2;i++){sch.led_ms[i]=leds[rnd_below(5)];sch.led_phase[i]=rnd_below(sch.led_ms[i]);}
 sch.connector_ms=50;sch.usb_burst=1u<<rnd_below(4);
 sch.rebuild=rnd_below(2);sch.late_writer=rnd_below(4)==0;
 policy.master_press_empty_sets=rnd_below(4)==0;policy.master_press_toggles_off=rnd_below(3)==0;
 policy.stop_policy=(int)rnd_below(3);policy.unload_policy=(int)rnd_below(3);policy.sync_press_claims=rnd_below(2);
 policy.reassert_ms=rnd_below(4)==0?500+rnd_below(2000):0;
 /* Optional fixed stock policies (environment), for separating stock-dependent classes. */
 const char *e;
 if((e=getenv("SIM_REASSERT")))policy.reassert_ms=(unsigned)atoi(e);
 if((e=getenv("SIM_TOGGLE")))policy.master_press_toggles_off=atoi(e);
 if((e=getenv("SIM_EMPTY")))policy.master_press_empty_sets=atoi(e);
 if((e=getenv("SIM_STOP")))policy.stop_policy=atoi(e);
 if((e=getenv("SIM_UNLOAD")))policy.unload_policy=atoi(e);
 if((e=getenv("SIM_CLAIMS")))policy.sync_press_claims=atoi(e);
 if((e=getenv("SIM_CONSUMER_MAX"))&&sch.consumer_ms>(unsigned)atoi(e)){sch.consumer_ms=(unsigned)atoi(e);sch.consumer_jitter=0;}
 /* Stock behaviour for the lock and MASTER-takeover stories; never drawn, so old seeds replay unchanged. */
 policy.reassert_playing_ms=0;policy.refuse_playing=0;policy.brake_reports_playing=1;policy.native_vinyl=0;
 if((e=getenv("SIM_REASSERT_PLAYING")))policy.reassert_playing_ms=(unsigned)atoi(e);
 if((e=getenv("SIM_REFUSE_PLAYING")))policy.refuse_playing=atoi(e);
 if((e=getenv("SIM_BRAKE_PLAYING")))policy.brake_reports_playing=atoi(e);
 if((e=getenv("SIM_NATIVE_VINYL")))policy.native_vinyl=atoi(e);
 /* Hardware-like timing: the transport publishes its position about every 10 ms; workers pass every 2 ms. */
 if((e=getenv("SIM_USB_BURST")))sch.usb_burst=(unsigned)atoi(e);
 if((e=getenv("SIM_WORKER")))sch.worker_ms=(unsigned)atoi(e);
 if((e=getenv("SIM_CONSUMER")))sch.consumer_ms=(unsigned)atoi(e);
}
static void draw_actions(void){
 action_count=0;
 uint32_t t=300;
 /* Opening: both native decks loaded and one playing as master, both software decks loaded. */
 actions[action_count++]=(struct action){t,ACT_NATIVE_LOAD,0,0};
 actions[action_count++]=(struct action){t+5,ACT_NATIVE_LOAD,1,0};
 actions[action_count++]=(struct action){t+10,ACT_LOAD,0,1};
 actions[action_count++]=(struct action){t+15,ACT_LOAD,1,2};
 actions[action_count++]=(struct action){t+20,ACT_SELECT,0,0};
 actions[action_count++]=(struct action){t+25,ACT_SELECT,1,0};
 t+=400;
 while(t<run_ms-500&&action_count<MAX_ACTIONS){
  t+=20+rnd_below(rnd_below(4)?400:1500);
  struct action a={t,0,(uint8_t)rnd_below(2),0};
  unsigned r=rnd_below(100);
  if(r<18)a.kind=ACT_SYNC;else if(r<30)a.kind=ACT_MASTER;else if(r<46)a.kind=ACT_PLAY;
  else if(r<54)a.kind=ACT_SELECT;else if(r<59)a.kind=ACT_LOAD;else if(r<60)a.kind=ACT_LOAD_MISSING;
  else if(r<63)a.kind=ACT_NATIVE_LOAD;else if(r<65)a.kind=ACT_NATIVE_UNLOAD;else if(r<70)a.kind=ACT_TEMPO;
  else if(r<73)a.kind=ACT_NATIVE_TEMPO;else if(r<74)a.kind=ACT_ROUTE_DOWN;else if(r<76)a.kind=ACT_STALL_CONSUMER;
  else if(r<78)a.kind=ACT_STALL_PUBLISHERS;else if(r<80)a.kind=ACT_STALL_WORKERS;else if(r<83)a.kind=ACT_PACKET;
  else if(r<84)a.kind=ACT_LED_STOP;else if(r<90)a.kind=ACT_STOCK_PLAY_OTHER;else a.kind=ACT_PLAY;
  a.arg=(int16_t)rnd_below(1000);
  actions[action_count++]=a;
  if(a.kind==ACT_ROUTE_DOWN&&action_count<MAX_ACTIONS)
   actions[action_count++]=(struct action){t+100+rnd_below(900),ACT_ROUTE_UP,0,0};
  if(a.kind==ACT_LED_STOP&&action_count<MAX_ACTIONS)
   actions[action_count++]=(struct action){t+50+rnd_below(500),ACT_LED_START,0,0};
 }
 /* Stable sort by time. */
 for(unsigned i=1;i<action_count;i++){struct action x=actions[i];unsigned j=i;while(j&&actions[j-1].at_ms>x.at_ms){actions[j]=actions[j-1];j--;}actions[j]=x;}
}
/* ---- Seeded lock stories: one master/follower pair, VINYL or CDJ, then a disturbance every few
   seconds. Pairs: 3<-1, 3<-2, 4<-2, 4<-1 (native master), 4<-3, 3<-4 (DECK 3/4 master; a native
   deck can follow it, so what it publishes is checked too). */
static int lock_mode,lock_pair,lock_vinyl;static unsigned opening_count=6;
static const char *const pair_name[]={"3<-1","3<-2","4<-2","4<-1","4<-3","3<-4"};
static void push(uint32_t at,int kind,int side,int arg,int32_t value){
 if(action_count<MAX_ACTIONS)actions[action_count++]=(struct action){at,(uint8_t)kind,(uint8_t)side,(int16_t)arg,value};
}
static void draw_lock_story(void){
 static const uint32_t pool[]={1,4,5,7};
 action_count=0;
 lock_pair=(int)rnd_below(6);lock_vinyl=rnd_below(4)!=0;
 int M=lock_pair<2?lock_pair==1:lock_pair<4?lock_pair==2:lock_pair==4?2:3; /* master logical deck */
 int F=lock_pair==0||lock_pair==1||lock_pair==5?0:1;                        /* follower software index */
 if(lock_pair==3)M=0;
 policy.native_vinyl=lock_vinyl;
 int32_t b[2]={11800+(int32_t)rnd_below(1600),11800+(int32_t)rnd_below(1600)};
 push(300,ACT_NATIVE_LOAD,0,0,V(b[0]));push(305,ACT_NATIVE_LOAD,1,0,V(b[1]));
 if(M==1){push(340,ACT_NATIVE_MASTER,1,0,0);push(345,ACT_STOCK_PLAY_OTHER,1,0,0);push(350,ACT_STOCK_PLAY_OTHER,0,0,0);}
 else if(M>=2&&rnd_below(5))push(350,ACT_STOCK_PLAY_OTHER,0,0,0);
 push(600,ACT_SELECT,0,0,0);push(605,ACT_SELECT,1,0,0);
 push(610,ACT_LOAD,0,0,V(pool[rnd_below(4)]));push(615,ACT_LOAD,1,0,V(pool[rnd_below(4)]));
 push(700,ACT_TEMPO,0,0,V((int32_t)rnd_below(6001)-3000));push(710,ACT_TEMPO,1,0,V((int32_t)rnd_below(6001)-3000));
 if(!lock_vinyl){push(720,ACT_VINYL,0,0,0);push(725,ACT_VINYL,1,0,0);}
 if(M>=2){push(1000,ACT_PLAY,M-2,0,0);push(1100,ACT_MASTER,M-2,0,0);}
 push(1300,ACT_PLAY,F,0,0);push(1800,ACT_SYNC,F,0,0);
 opening_count=action_count;
 for(uint32_t t=4000+rnd_below(1500);t+3000<run_ms;t+=2500+rnd_below(2000)){
  unsigned r=rnd_below(100);int arg=(int)rnd_below(1000);int nat_m=M<2,ms=M-2;
  if(r<15){if(nat_m)push(t,ACT_NATIVE_TEMPO,M,0,V(11800+(int32_t)rnd_below(1600)));else push(t,ACT_TEMPO,ms,0,V((int32_t)rnd_below(6001)-3000));}
  else if(r<27){if(nat_m)push(t,ACT_NATIVE_TOUCH,M,arg,0);else push(t,ACT_TOUCH,ms,arg,0);}
  else if(r<37){if(nat_m)push(t,ACT_NATIVE_NUDGE,M,arg,0);else push(t,ACT_NUDGE,ms,arg,0);}
  else if(r<52){uint32_t back=400+rnd_below(1600);
   if(nat_m){push(t,ACT_NATIVE_PLAY,M,0,0);push(t+back,ACT_NATIVE_PLAY,M,0,0);}
   else{push(t,ACT_PLAY,ms,arg,0);push(t+back,ACT_PLAY,ms,arg,0);push(t+back+150,ACT_MASTER,ms,0,0);}}
  else if(r<62)push(t,ACT_TOUCH,F,arg,0);
  else if(r<70)push(t,ACT_NUDGE,F,arg,0);
  else if(r<78){
   if(nat_m){push(t,ACT_NATIVE_LOAD,M,0,V(11800+(int32_t)rnd_below(1600)));push(t+300,ACT_NATIVE_PLAY,M,0,0);}
   else{push(t,ACT_LOAD,ms,0,V(pool[rnd_below(4)]));push(t+300,ACT_PLAY,ms,0,0);push(t+450,ACT_MASTER,ms,0,0);}}
  else if(r<86){
   if(nat_m){int o=1-M;push(t,ACT_STOCK_PLAY_OTHER,o,0,0);push(t+100,ACT_NATIVE_MASTER,o,0,0);push(t+1500,ACT_NATIVE_MASTER,M,0,0);push(t+1600,ACT_STOCK_PLAY_OTHER,o,0,0);}
   else{push(t,ACT_MASTER,F,0,0);push(t+1500,ACT_MASTER,ms,0,0);}}
  else if(r<93){push(t,ACT_SYNC,F,0,0);push(t+300+rnd_below(500),ACT_SYNC,F,0,0);}
  else{if(nat_m)push(t,ACT_SEEK,M,arg,0);else push(t,ACT_RANGE,ms,0,0);}
 }
 for(unsigned i=1;i<action_count;i++){struct action x=actions[i];unsigned j=i;while(j&&actions[j-1].at_ms>x.at_ms){actions[j]=actions[j-1];j--;}actions[j]=x;}
}
static int whole_beats(int arg){return arg%3==0;}
static void native_stop_policy(int side){
 if(engine_master()!=side)return;
 int other=1-side;
 if(policy.stop_policy==1&&nat[other].loaded&&nat[other].playing)stock_set_master(other);
 else if(policy.stop_policy==2)stock_set_master(-1);
}
static void apply(const struct action *a){
 int s=a->side;struct xz_extra_decks v;xz_xp_read(p,&v);
 int extra=ds.side[s].target==XZ_DS_EXTRA&&route_is_up;
 trace(act_name[a->kind],s,a->arg);
 uint32_t now=now32();
 if(nudge_live&&a->kind!=ACT_NATIVE_NUDGE)nudge_live=0;
 /* Which deck this action disturbs, for the lock checks. */
 switch(a->kind){
 /* Tempo, range and mode changes keep a kept offset (S7); the others find the beat again. Nudges are marked
    where they act (nudge_mark): they make the offset. */
 case ACT_TEMPO:disturb_keep(s+2,now);break;
 case ACT_RANGE:case ACT_VINYL:disturb_keep(extra?s+2:s,now);break;
 /* A platter-top touch acts only in VINYL mode (CDJ mode: nothing happens, the offset stays). */
 case ACT_TOUCH:if(extra?v.deck[s].vinyl:nat[s].vinyl)disturb(extra?s+2:s,now);break;
 case ACT_PLAY:disturb(extra?s+2:s,now);break;
 /* The lock stories' native beat jump is marked where it acts, below. */
 case ACT_SEEK:if(!(lock_mode&&HAS_VALUE(a)==0&&a->side<2&&lock_pair<4))disturb(extra?s+2:s,now);break;
 case ACT_NUDGE:break;
 case ACT_LOAD:case ACT_LOAD_MISSING:disturb(s+2,now);break;
 case ACT_NATIVE_TEMPO:disturb_keep(s,now);break;
 case ACT_NATIVE_LOAD:case ACT_NATIVE_UNLOAD:case ACT_STOCK_PLAY_OTHER:
 case ACT_NATIVE_PLAY:disturb(s,now);break;
 case ACT_NATIVE_TOUCH:if(nat[s].vinyl)disturb(s,now);break;
 case ACT_NATIVE_NUDGE:break;
 case ACT_NATIVE_MASTER:global_disturb(now);break;
 case ACT_ROUTE_DOWN:case ACT_ROUTE_UP:case ACT_MASTER:global_disturb(now);break;
 default:break;
 }
 if(a->kind==ACT_NATIVE_PLAY||a->kind==ACT_NATIVE_TOUCH||a->kind==ACT_NATIVE_NUDGE){
  if(a->kind==ACT_NATIVE_NUDGE){nudge_live=1;nudge_deck=s;nudge_at=now;nudge_jumps[0]=act_jumps[0];nudge_jumps[1]=act_jumps[1];}
  else if(s==nudge_deck)nudge_live=0; /* another hand on that deck (hold, PLAY) ends the nudge window */
  struct action b=*a;b.kind=a->kind==ACT_NATIVE_PLAY?ACT_PLAY:a->kind==ACT_NATIVE_TOUCH?ACT_TOUCH:ACT_NUDGE;
  if(b.kind==ACT_PLAY){int was=nat[s].playing&&!nat[s].brake;native_key(s,0x4101);native_transport_at=now;
   if(was&&!nat[s].playing)native_stop_policy(s);return;}
  extra=0;a=&b;
  switch(b.kind){
  case ACT_TOUCH:{uint32_t hold=HAS_VALUE(a)?(uint32_t)VALUE(a):100u+(uint32_t)a->arg%700u;
   if(nat[s].loaded&&nat[s].vinyl){nat[s].touch=1;if(deferred_count<16)deferred[deferred_count++]=(struct deferred){now+hold,s,0};disturb(s,now+hold);}
   return;}
  default:{int dir=a->arg&1?1:-1,ticks=HAS_VALUE(a)?VALUE(a):5+a->arg%30;
   if(nat[s].loaded){nat[s].bend=dir*.004*ticks;nat[s].bend_until=now+150;nudge_mark(s,now+150);}
   return;}
  }
 }
 /* A jump by whole beats keeps the beat alignment (and a kept offset); a fractional one finds the beat again. */
 if(a->kind==ACT_SEEK&&lock_mode&&HAS_VALUE(a)==0&&a->side<2&&lock_pair<4){if(whole_beats(a->arg))disturb_keep(s,now);else disturb(s,now);
  if(nat[s].loaded)nat[s].beats+=1+a->arg%7+.25*(a->arg%3);return;}
 if(a->kind==ACT_NATIVE_MASTER){last_operator_ms=now;last_choice_ms=last_operator_ms;master_was_playing=0;native_master_selected(s);return;}
 switch(a->kind){
 case ACT_TOUCH:{
  uint32_t hold=HAS_VALUE(a)?(uint32_t)VALUE(a):100u+(uint32_t)a->arg%700u;
  if(extra){struct xz_extra_deck_event e={.deck=s,.kind=XZ_EXTRA_IN_JOG_TOUCH,.pressed=1};(void)xz_extra_deck_input(&e);}
  else if(nat[s].loaded&&nat[s].vinyl)nat[s].touch=1;
  else break;
  if(deferred_count<16)deferred[deferred_count++]=(struct deferred){now+hold,s,extra};
  if(!extra||v.deck[s].vinyl)disturb(extra?s+2:s,now+hold); /* CDJ mode: a touch does nothing */
  break;}
 case ACT_NUDGE:{
  int dir=a->arg&1?1:-1,ticks=5+a->arg%30;
  if(HAS_VALUE(a))ticks=VALUE(a);
  if(extra){struct xz_extra_deck_event e={.deck=s,.kind=XZ_EXTRA_IN_JOG_TURN,.pressed=1};e.jog.delta=dir*ticks;(void)xz_extra_deck_input(&e);
   nudge_mark(s+2,now+XZ_XP_BEND_DECAY_MS);}
  else if(nat[s].loaded){nat[s].bend=dir*.004*ticks;if(nat[s].bend>.9)nat[s].bend=.9;if(nat[s].bend<-.9)nat[s].bend=-.9;
   nat[s].bend_until=now+150;nudge_mark(s,now+150);}
  break;}
 case ACT_RANGE:
  if(extra){struct xz_extra_deck_event e={.deck=s,.kind=XZ_EXTRA_IN_TEMPO_RANGE,.pressed=1};(void)xz_extra_deck_input(&e);}
  break;
 case ACT_VINYL:
  if(extra){struct xz_extra_deck_event e={.deck=s,.kind=XZ_EXTRA_IN_VINYL,.pressed=1};(void)xz_extra_deck_input(&e);}
  else nat[s].vinyl=!nat[s].vinyl;
  break;
 case ACT_SEEK:
  if(extra){struct xz_extra_deck_event e={.deck=s,.kind=XZ_EXTRA_IN_SEEK,.pressed=1};
   e.seek_ms=HAS_VALUE(a)?VALUE(a):1000+a->arg*20;(void)xz_extra_deck_input(&e);}
  else if(nat[s].loaded)nat[s].beats+=1+a->arg%7+.25*(a->arg%3);
  break;
 case ACT_SYNC:
  /* S2: a SYNC off press on a playing synced deck: its speed must not step. */
  soff_pending[s]=0; /* a later press ends the previous check */
  /* S2 is checked on a deck that was locked (in phase for 1 s) when SYNC was pressed off. */
  if(extra&&v.deck[s].loaded&&v.deck[s].sync.requested&&v.deck[s].playing&&lk[s].acquired){soff_pending[s]=1;soff_at[s]=now;soff_rate[s]=0;}
  if(extra&&v.deck[s].loaded){
   if(!sync_base_valid[s]){sync_base_valid[s]=1;sync_base_sent[s]=sync_sent[s];sync_base_delivered[s]=sync_presses_delivered[s];sync_base_req[s]=v.deck[s].sync.requested;}
   sync_sent[s]++;sync_press_at[s]=now32();
  }else sync_base_valid[s]=0;
  tap(s,0x4313,HAS_VALUE(a)?(unsigned)VALUE(a):30+(unsigned)a->arg%80);break;
 case ACT_MASTER:
  last_operator_ms=now32();last_choice_ms=last_operator_ms;master_was_playing=0; /* a new choice must play before its stop hands over */
  if(extra&&v.deck[s].loaded){master_press_pending[s]=1;master_press_at[s]=now32();
   master_press_playing[s]=v.deck[s].playing&&!(play_press_at[s]&&(uint32_t)(now32()-play_press_at[s])<1000);}
  else master_press_pending[0]=master_press_pending[1]=0;
  tap(s,0x4312,30+(unsigned)a->arg%80);break;
 case ACT_PLAY:{
  int was=extra?v.deck[s].playing:nat[s].playing;
  if(extra)master_press_playing[s]=0; /* a transport press ends the explicit-press window */
  if(extra){play_press_at[s]=now32();
   if(was){int d3p=v.deck[0].playing&&s!=0,d4p=v.deck[1].playing&&s!=1;int np=(nat[0].loaded&&nat[0].playing)||(nat[1].loaded&&nat[1].playing);
    press_next[s]=s==0?(d4p?3:np?-4:-9):(d3p&&v.deck[0].sync.requested?2:np?-4:d3p?2:-9);pause_press_at[s]=now32();}}
  tap(s,0x4101,30+(unsigned)a->arg%60);
  if(!extra&&was&&!nat[s].playing)native_stop_policy(s);
  if(!extra)native_transport_at=now32();
  break;}
 case ACT_STOCK_PLAY_OTHER:{
  native_transport_at=now32();
  /* The hidden native deck under a software side keeps playing or stopping on its own (e.g. a DJ on the other side). */
  int was=nat[s].playing;if(nat[s].loaded)nat[s].playing^=1;
  if(was&&!nat[s].playing)native_stop_policy(s);
  break;}
 case ACT_SELECT:
  sync_base_valid[s]=0;
  xz_deck_select_route_instance(&ds,route_is_up,route_is_up?route_epoch:0);
  xz_deck_select_toggle(&ds,s,NULL);publish_sides();break;
 case ACT_LOAD:case ACT_LOAD_MISSING:{
  if(a->kind==ACT_LOAD&&engine_master()==s+2&&deck_playing(s+2,&v)&&
     (deck_playing(3-s,&v)||(nat[0].loaded&&nat[0].playing)||(nat[1].loaded&&nat[1].playing))){load_pending=1;load_from=s+2;load_at=now32();}
  struct xz_extra_deck_event e={.deck=s,.kind=XZ_EXTRA_IN_LOAD,.pressed=1};
  e.load.source=1;e.load.track_id=a->kind==ACT_LOAD_MISSING?3:HAS_VALUE(a)?(uint32_t)VALUE(a):1+(uint32_t)(a->arg&1);
  (void)xz_extra_deck_input(&e);master_press_playing[s]=0;sync_base_valid[s]=0;
  if(master_tracked==s+2)last_operator_ms=now32();
  break;}
 case ACT_NATIVE_LOAD:{native_transport_at=now32();
  struct native *n=&nat[s];int was_master=engine_master()==s;
  if(was_master&&n->loaded&&n->playing&&!(nat[1-s].loaded&&nat[1-s].playing)&&
     ((software_playing(&v.deck[0])&&!v.deck[0].held_playing&&!(pause_press_at[0]&&pause_press_at[0]==play_press_at[0]&&(uint32_t)(now-pause_press_at[0])<3500))||
      (software_playing(&v.deck[1])&&!v.deck[1].held_playing&&!(pause_press_at[1]&&pause_press_at[1]==play_press_at[1]&&(uint32_t)(now-pause_press_at[1])<3500)))){
   load_pending=1;load_from=s;load_at=now32();}
  n->loaded=1;n->playing=a->at_ms<800?s==0:0;n->identity=++native_identity_counter;n->revision=++native_revision_counter;
  n->bpm=HAS_VALUE(a)?VALUE(a)/100.:118+(a->arg%10);n->beats=0;n->brake=n->touch=0;n->bend=0;
  n->vinyl=policy.native_vinyl;n->brake_ms=300;
  if(a->at_ms<800&&s==0)stock_set_master(0);
  (void)was_master;break;}
 case ACT_NATIVE_UNLOAD:{native_transport_at=now32();
  struct native *n=&nat[s];if(!n->loaded)break;
  int was_master=engine_master()==s;n->loaded=n->playing=0;n->identity=0;
  if(was_master){
   int other=1-s;
   if(policy.unload_policy==1&&nat[other].loaded)stock_set_master(other);
   else if(policy.unload_policy==2)stock_set_master(-1);
  }
  break;}
 case ACT_TEMPO:{
  struct xz_extra_deck_event e={.deck=s,.kind=XZ_EXTRA_IN_TEMPO,.pressed=1};
  e.tempo_position=HAS_VALUE(a)?VALUE(a):(int32_t)(a->arg%2001)-1000;(void)xz_extra_deck_input(&e);break;}
 case ACT_NATIVE_TEMPO:nat[s].bpm=HAS_VALUE(a)?VALUE(a)/100.:115+(a->arg%15);break;
 case ACT_ROUTE_DOWN:
  if(!route_is_up)break;
  route_is_up=0;sim_route(0);sync_base_valid[0]=sync_base_valid[1]=0;sim_decks_route(0,0);last_operator_ms=now32();last_choice_ms=last_operator_ms;
  xz_deck_select_route_instance(&ds,0,0);publish_sides();break;
 case ACT_ROUTE_UP:
  if(route_is_up)break;
  route_is_up=1;route_epoch++;sim_route(route_epoch);sim_decks_route(1,route_epoch);last_operator_ms=now32();last_choice_ms=last_operator_ms;
  xz_deck_select_route_instance(&ds,1,route_epoch);publish_sides();break;
 /* A stall is a disturbance, not an operator choice: checks restart after it ends. */
 case ACT_STALL_CONSUMER:stall_until[0]=now32()+80+(uint32_t)a->arg%300;last_operator_ms=stall_until[0];last_choice_ms=last_operator_ms;global_disturb(stall_until[0]);break;
 case ACT_STALL_PUBLISHERS:stall_until[1]=now32()+80+(uint32_t)a->arg%300;last_operator_ms=stall_until[1];last_choice_ms=last_operator_ms;global_disturb(stall_until[1]);break;
 case ACT_STALL_WORKERS:stall_until[2]=now32()+20+(uint32_t)a->arg%200;last_operator_ms=stall_until[2];last_choice_ms=last_operator_ms;global_disturb(stall_until[2]);break;
 case ACT_PACKET:packet_variant[s]=(enum sim_packet)((unsigned)a->arg%SIM_PACKETS);
  lamp_sync_bad[s].on=lamp_sync_stuck[s].on=lamp_master_bad[s].on=lamp_master_false[s].on=native_bad[s].on=0;break;
 case ACT_AUTO_CUE:
  if(extra){struct xz_extra_deck_event e={.deck=s,.kind=XZ_EXTRA_IN_AUTO_CUE,.pressed=1};(void)xz_extra_deck_input(&e);}
  break;
 case ACT_LED_STOP:led_running=0;sim_led_stop();break;
 case ACT_LED_START:led_running=1;sim_led_start();break;
 }
}
/* Named regressions: fixed schedule, fixed stock policy, a short scripted story. */
/* Native DECK 1 loads and plays as master (b1, b2: effective BPM x100); N2 makes playing DECK 2 master
   instead (MASTER on that side while it still controls the native deck), DECK 1 stopped. SW: both
   sides to DECK 3/4 and their tracks (sim_track ids). */
#define N1(b1,b2) {300,ACT_NATIVE_LOAD,0,0,V(b1)},{305,ACT_NATIVE_LOAD,1,0,V(b2)}
#define N2(b1,b2) N1(b1,b2),{340,ACT_MASTER,1,0},{345,ACT_STOCK_PLAY_OTHER,1,0},{350,ACT_STOCK_PLAY_OTHER,0,0}
#define SW(t3,t4) {600,ACT_SELECT,0,0},{605,ACT_SELECT,1,0},{610,ACT_LOAD,0,0,V(t3)},{615,ACT_LOAD,1,0,V(t4)}
#define OPENING {300,ACT_NATIVE_LOAD,0,0},{305,ACT_NATIVE_LOAD,1,0},{310,ACT_LOAD,0,0},{315,ACT_LOAD,1,1},{320,ACT_SELECT,0,0},{325,ACT_SELECT,1,0}
/* Stock and deck setup for a story: native VINYL, stock re-asserting or refusing while its master plays. */
/* stock_dependent: the story models one stock behaviour our code cannot see; it reports NOTE, not FAIL. */
struct story_setup { int native_vinyl; unsigned reassert_playing_ms; int refuse_playing,brake_drops_playing,consumer_ms,worker_ms,usb_burst,stock_dependent,stop_policy; };
/* end_master: the engine master when the story ends, logical deck + 1 (0: not checked). */
struct scenario { const char *name,*story; unsigned run_ms,count; struct action acts[40]; uint64_t seed; struct story_setup setup; int end_master; };
static const struct scenario scenarios[]={
 {"software-master-pause","DECK 4 master, then MASTER on DECK 3, then DECK 3 paused (VINYL brake) while DECK 4 plays: master passes to DECK 4",
  7000,11,{OPENING,{1000,ACT_PLAY,1,0},{1500,ACT_MASTER,1,0},{2500,ACT_PLAY,0,0},{3000,ACT_MASTER,0,0},{4500,ACT_PLAY,0,0}},0},
 {"native-stop-before-software-play","native DECK 1 master plays, stops, then DECK 4 starts: a stopped native master hands over after 300 ms",
  5000,8,{OPENING,{1000,ACT_STOCK_PLAY_OTHER,0,0},{2000,ACT_PLAY,1,0}},0},
 {"software-master-stop-to-playing-native","DECK 4 master stops (VINYL brake) while native DECK 2 plays and the old native master DECK 1 is stopped: master passes to DECK 2",
  6000,11,{OPENING,{1000,ACT_STOCK_PLAY_OTHER,0,0},{1500,ACT_PLAY,1,0},{2000,ACT_MASTER,1,0},{3000,ACT_STOCK_PLAY_OTHER,1,0},{4000,ACT_PLAY,1,0}},0},
 {"native-stop-then-consumer-gap","DECK 3 plays; native DECK 1 master stops; the stock sync manager pauses 200 ms: DECK 3 still takes over",
  5000,9,{OPENING,{1000,ACT_PLAY,0,0},{2000,ACT_STOCK_PLAY_OTHER,0,0},{2050,ACT_STALL_CONSUMER,0,120}},0},
 /* Minimized seed 153, on that seed's drawn schedule and stock policy: the DECK 3 lease records
    DECK 4 as its previous master, so a lost lease once tried to return the engine to player 4. */
 {"lost-lease-ghost","DECK 4 master, MASTER on DECK 3, DECK 3 paused: the engine never keeps following a player nobody publishes",
  15000,21,{{300,ACT_NATIVE_LOAD,0,0},{305,ACT_NATIVE_LOAD,1,0},{310,ACT_LOAD,0,1},{315,ACT_LOAD,1,2},{320,ACT_SELECT,0,0},{325,ACT_SELECT,1,0},
   {1106,ACT_SELECT,0,610},{1714,ACT_PLAY,1,689},{2432,ACT_SELECT,0,917},{2785,ACT_SELECT,1,284},{5200,ACT_STALL_CONSUMER,1,661},
   {8428,ACT_PLAY,0,322},{8940,ACT_SELECT,1,658},{9063,ACT_PLAY,1,169},{9598,ACT_SELECT,0,927},{9816,ACT_PLAY,1,13},
   {9947,ACT_MASTER,1,39},{11815,ACT_SELECT,0,854},{11916,ACT_STALL_PUBLISHERS,1,661},{12034,ACT_MASTER,0,880},{12993,ACT_PLAY,0,652}},153},
 /* The same lease (previous master: player 4), but DECK 3 pauses with nothing else playing and
    then plays again. A lost lease must not leave the engine on player 3: DECK 3 can claim again. */
 {"lost-lease-relaunch","as lost-lease-ghost, with DECK 4 and native DECK 1 stopped first; DECK 3 plays again and is master with its MASTER lamp lit",
  16000,24,{{300,ACT_NATIVE_LOAD,0,0},{305,ACT_NATIVE_LOAD,1,0},{310,ACT_LOAD,0,1},{315,ACT_LOAD,1,2},{320,ACT_SELECT,0,0},{325,ACT_SELECT,1,0},
   {1106,ACT_SELECT,0,610},{1714,ACT_PLAY,1,689},{2432,ACT_SELECT,0,917},{2785,ACT_SELECT,1,284},{5200,ACT_STALL_CONSUMER,1,661},
   {8428,ACT_PLAY,0,322},{8940,ACT_SELECT,1,658},{9063,ACT_PLAY,1,169},{9598,ACT_SELECT,0,927},{9816,ACT_PLAY,1,13},
   {9947,ACT_MASTER,1,39},{11815,ACT_SELECT,0,854},{11916,ACT_STALL_PUBLISHERS,1,661},{12034,ACT_MASTER,0,880},
   {12400,ACT_STOCK_PLAY_OTHER,0,0},{12500,ACT_PLAY,1,0},{12993,ACT_PLAY,0,652},{13800,ACT_PLAY,0,0}},153},

 /* ---- Owner report, 22.13, VINYL: MASTER on DECK 3/4 reads MASTER REQUESTED and only takes over
    once the playing native master stops. The DECK 3/4 deck plays (and follows with SYNC); the
    native master keeps playing. MASTER must move within 1 s, MASTER lamps with it. */
 {"master-takeover-vinyl-3-over-1","native DECK 1 master plays; DECK 3 plays synced; MASTER on DECK 3 takes over while DECK 1 keeps playing (VINYL)",
  5000,0,{N1(12000,12000),SW(1,2),{1000,ACT_PLAY,0,0},{1500,ACT_SYNC,0,0},{2500,ACT_MASTER,0,0}},0,{1}},
 {"master-takeover-vinyl-4-over-2","native DECK 2 master plays; DECK 4 plays synced; MASTER on DECK 4 takes over while DECK 2 keeps playing (VINYL)",
  5000,0,{N2(12000,12000),SW(1,2),{1000,ACT_PLAY,1,0},{1500,ACT_SYNC,1,0},{2500,ACT_MASTER,1,0}},0,{1}},
 {"master-takeover-vinyl-3-over-2","native DECK 2 master plays; MASTER on playing DECK 3 takes over (VINYL)",
  5000,0,{N2(12000,12000),SW(1,2),{1000,ACT_PLAY,0,0},{2500,ACT_MASTER,0,0}},0,{1}},
 {"master-takeover-vinyl-4-over-1","native DECK 1 master plays; MASTER on playing DECK 4 takes over (VINYL)",
  5000,0,{N1(12000,12000),SW(1,2),{1000,ACT_PLAY,1,0},{2500,ACT_MASTER,1,0}},0,{1}},
 {"master-takeover-cdj-3-over-1","as master-takeover-vinyl-3-over-1 in CDJ mode",
  5000,0,{N1(12000,12000),SW(1,2),{700,ACT_VINYL,0,0},{1000,ACT_PLAY,0,0},{1500,ACT_SYNC,0,0},{2500,ACT_MASTER,0,0}},0,{0}},
 {"master-takeover-cdj-4-over-2","as master-takeover-vinyl-4-over-2 in CDJ mode",
  5000,0,{N2(12000,12000),SW(1,2),{700,ACT_VINYL,1,0},{1000,ACT_PLAY,1,0},{1500,ACT_SYNC,1,0},{2500,ACT_MASTER,1,0}},0,{0}},
 /* M1 (stock-sync-spec.md): MASTER on the other playing deck switches at once, also when stock confirms its own
    master's id meanwhile (that is not a DJ choice). Before, the confirmation cancelled the request. */
 {"master-takeover-stock-confirms-playing-master","M1: as master-takeover-vinyl-3-over-1; stock calls set_master_on with its own playing master's id every 20 ms: DECK 3 still takes master at once",
  5000,0,{N1(12000,12000),SW(1,2),{1000,ACT_PLAY,0,0},{1500,ACT_SYNC,0,0},{2500,ACT_MASTER,0,0}},0,{1,20},3},
 /* A stock behaviour our code cannot change (NOTE, not gating). */
 {"master-takeover-stock-refuses-while-playing","as master-takeover-vinyl-3-over-1; stock's link-master call refuses player 3/4 while its native master plays",
  5000,0,{N1(12000,12000),SW(1,2),{1000,ACT_PLAY,0,0},{1500,ACT_SYNC,0,0},{2500,ACT_MASTER,0,0}},0,{1,0,1,0,0,0,0,1}},
 {"master-takeover-slow-stock-sync-manager","as master-takeover-vinyl-3-over-1; stock calls its sync manager every 120 ms (beyond the 100 ms authority age)",
  5000,0,{N1(12000,12000),SW(1,2),{1000,ACT_PLAY,0,0},{1500,ACT_SYNC,0,0},{2500,ACT_MASTER,0,0}},0,{1,0,0,0,120,0,0,1}},
 /* ---- Owner report: DECK 4 reads SYNC 2 but does not play at DECK 2's BPM. Different track BPMs,
    non-zero pitch on both decks, every tempo range. The master's later tempo move must follow too. */
 {"absolute-tempo-4-follows-2","native DECK 2: 128 BPM track at +3.2% (132.10); DECK 4: 122 BPM track at -1.5%; SYNC on DECK 4 plays 132.10, then follows DECK 2 to 130.00",
  12000,0,{N2(12000,13210),SW(1,4),{700,ACT_TEMPO,1,0,V(-1500)},{1000,ACT_PLAY,1,0},{1500,ACT_SYNC,1,0},{7000,ACT_NATIVE_TEMPO,1,0,V(13000)}},0,{1}},
 {"absolute-tempo-4-follows-2-range16","as absolute-tempo-4-follows-2, DECK 4 range 16%",
  12000,0,{N2(12000,13210),SW(1,4),{650,ACT_RANGE,1,0},{700,ACT_TEMPO,1,0,V(-1500)},{1000,ACT_PLAY,1,0},{1500,ACT_SYNC,1,0},{7000,ACT_NATIVE_TEMPO,1,0,V(13000)}},0,{1}},
 {"absolute-tempo-4-follows-2-wide","as absolute-tempo-4-follows-2, DECK 4 range WIDE",
  12000,0,{N2(12000,13210),SW(1,4),{650,ACT_RANGE,1,0},{660,ACT_RANGE,1,0},{700,ACT_TEMPO,1,0,V(-1500)},{1000,ACT_PLAY,1,0},{1500,ACT_SYNC,1,0},{7000,ACT_NATIVE_TEMPO,1,0,V(13000)}},0,{1}},
 {"absolute-tempo-4-follows-2-range6","as absolute-tempo-4-follows-2, DECK 4 range 6%",
  12000,0,{N2(12000,13210),SW(1,4),{650,ACT_RANGE,1,0},{660,ACT_RANGE,1,0},{670,ACT_RANGE,1,0},{700,ACT_TEMPO,1,0,V(-1500)},{1000,ACT_PLAY,1,0},{1500,ACT_SYNC,1,0},{7000,ACT_NATIVE_TEMPO,1,0,V(13000)}},0,{1}},
 {"absolute-tempo-3-follows-1","native DECK 1: 132.10 BPM; DECK 3: 122 BPM track at -1.5%; SYNC on DECK 3 plays 132.10, then follows DECK 1 to 130.00",
  12000,0,{N1(13210,12000),SW(4,1),{700,ACT_TEMPO,0,0,V(-1500)},{1000,ACT_PLAY,0,0},{1500,ACT_SYNC,0,0},{7000,ACT_NATIVE_TEMPO,0,0,V(13000)}},0,{1}},
 {"absolute-tempo-4-follows-3","natives stopped; DECK 3 master: 128 BPM track at +3.2%; DECK 4: 122 BPM track at -1.5%; SYNC on DECK 4, then DECK 3 to +1.0%",
  12000,0,{N1(12000,12000),{400,ACT_STOCK_PLAY_OTHER,0,0},SW(5,4),{700,ACT_TEMPO,0,0,V(3200)},{710,ACT_TEMPO,1,0,V(-1500)},{1000,ACT_PLAY,0,0},
   {1100,ACT_MASTER,0,0},{1300,ACT_PLAY,1,0},{1800,ACT_SYNC,1,0},{7000,ACT_TEMPO,0,0,V(1000)}},0,{1}},
 {"absolute-tempo-3-follows-4","natives stopped; DECK 4 master: 128 BPM track at +3.2%; DECK 3: 122 BPM track at -1.5%; SYNC on DECK 3, then DECK 4 to +1.0%",
  12000,0,{N1(12000,12000),{400,ACT_STOCK_PLAY_OTHER,0,0},SW(4,5),{700,ACT_TEMPO,1,0,V(3200)},{710,ACT_TEMPO,0,0,V(-1500)},{1000,ACT_PLAY,1,0},
   {1100,ACT_MASTER,1,0},{1300,ACT_PLAY,0,0},{1800,ACT_SYNC,0,0},{7000,ACT_TEMPO,1,0,V(1000)}},0,{1}},
 /* ---- Owner report: SYNC does not change reliably when pressed. Quick, slow and repeated presses,
    and a press right after a load, on a playing DECK 4 following native DECK 2. */
 {"sync-press-sequence","DECK 4 follows native DECK 2; SYNC pressed 400, 100, 50 and 60 ms apart, held 100 ms, and 5 ms after a load: one toggle per press",
  9000,0,{N2(12000,12800),SW(1,4),{1000,ACT_PLAY,1,0},{1500,ACT_SYNC,1,0},{1900,ACT_SYNC,1,0},{2000,ACT_SYNC,1,0},{2050,ACT_SYNC,1,0},
   {3000,ACT_SYNC,1,70},{3400,ACT_SYNC,1,0},{3460,ACT_SYNC,1,0},{4500,ACT_LOAD,1,0,V(5)},{4505,ACT_SYNC,1,0}},0,{1}},
 /* ---- Lock stories (minimized from lock seeds 37 and 1): a hand on the master, a master change. */
 {"vinyl-native-master-touch","DECK 3 follows native DECK 1 (VINYL, QUANTIZE); the DJ holds DECK 1's platter 250 ms: DECK 3 finds the beat again within 4 s, not 1%-slow for 20 s",
  14000,0,{N1(12800,12000),SW(4,1),{700,ACT_TEMPO,0,0,V(-1500)},{1000,ACT_PLAY,0,0},{1500,ACT_SYNC,0,0},{6000,ACT_NATIVE_TOUCH,0,0,V(250)}},0,{1}},
 {"vinyl-native-master-hold","as vinyl-native-master-touch with a 2.5 s hold: master passes to synced DECK 3 within 2 s at the shared synced BPM (stock M4, M6)",
  14000,0,{N1(12800,12000),SW(4,1),{700,ACT_TEMPO,0,0,V(-1500)},{1000,ACT_PLAY,0,0},{1500,ACT_SYNC,0,0},{6000,ACT_NATIVE_TOUCH,0,0,V(2500)}},0,{1}},
 {"native-follow-worker-3ms","DECK 4 follows native DECK 2 with workers passing every 3 ms and the USB position published every 8 ms (hardware-like): the lock holds, no trim flutter",
  14000,0,{N2(12000,13210),SW(1,4),{700,ACT_TEMPO,1,0,V(-1500)},{1000,ACT_PLAY,1,0},{1500,ACT_SYNC,1,0}},0,{1,0,0,0,0,3,8}},
 {"software-handoff-keeps-tempo","natives loaded, DECK 1 stopped at 128 BPM; DECK 3 follows DECK 4 (122 BPM track, +2%); DECK 4 pauses (VINYL): DECK 3 takes MASTER at its tempo, never DECK 1's",
  9000,0,{N1(12800,12800),{400,ACT_STOCK_PLAY_OTHER,0,0},SW(4,4),{700,ACT_TEMPO,1,0,V(2000)},{1000,ACT_PLAY,1,0},{1100,ACT_MASTER,1,0},
   {1300,ACT_PLAY,0,0},{1800,ACT_SYNC,0,0},{6000,ACT_PLAY,1,0}},0,{1}},
 {"software-master-press-keeps-tempo","as software-handoff-keeps-tempo; MASTER pressed on DECK 3 while DECK 4 plays: no detour through stopped DECK 1",
  9000,0,{N1(12800,12800),{400,ACT_STOCK_PLAY_OTHER,0,0},SW(4,4),{700,ACT_TEMPO,1,0,V(2000)},{1000,ACT_PLAY,1,0},{1100,ACT_MASTER,1,0},
   {1300,ACT_PLAY,0,0},{1800,ACT_SYNC,0,0},{6000,ACT_MASTER,0,0}},0,{1}},
 /* Minimized lock seed 14: a VINYL hold on a DECK 3/4 master has no publishable beat for a moment. */
 {"software-master-held","native DECK 1 master pauses, playing DECK 4 takes MASTER, DECK 1 plays again (130 BPM), DECK 3 follows DECK 4; the DJ holds DECK 4's platter 400 ms: the engine never names DECK 1 meanwhile",
  9000,0,{N1(13000,12000),SW(4,4),{700,ACT_TEMPO,1,0,V(2000)},{1000,ACT_PLAY,1,0},{1500,ACT_NATIVE_PLAY,0,0},
   {1600,ACT_PLAY,0,0},{2000,ACT_SYNC,0,0},{3000,ACT_NATIVE_PLAY,0,0},{6000,ACT_TOUCH,1,0,V(400)}},0,{1}},
 /* Minimized random seed 148: PLAY during this deck's own VINYL brake. */
 {"vinyl-brake-cancel-resnaps","DECK 4 follows native DECK 1 (VINYL); DECK 4 paused then played again 150 ms into its brake: it finds the beat again, not 1%-off for seconds",
  12000,0,{N1(12800,12000),SW(1,4),{700,ACT_TEMPO,1,0,V(-1500)},{1000,ACT_PLAY,1,0},{1500,ACT_SYNC,1,0},{6000,ACT_PLAY,1,0},{6150,ACT_PLAY,1,0}},0,{1}},
 /* M6: a seek on the committed DECK 3/4 master renews its lease without naming the old native master
    (before, by the old contract in tests/test_sync_link_runtime.c, case 6, it did for a moment). */
 {"software-master-beat-jump","M6: as software-master-held, with a seek on DECK 4 instead of a hold: the engine never names DECK 1",
  9000,0,{N1(13000,12000),SW(4,4),{700,ACT_TEMPO,1,0,V(2000)},{1000,ACT_PLAY,1,0},{1500,ACT_MASTER,1,0},
   {1600,ACT_PLAY,0,0},{2000,ACT_SYNC,0,0},{6000,ACT_SEEK,1,0,V(15000)}},0,{1},4},
 {"vinyl-native-master-nudge","S8 rule 1: as vinyl-native-master-touch, with a 150 ms side nudge on DECK 1: master stays, DECK 3 keeps the offset (no chase, no re-snap) and its SYNC lamp flashes",
  14000,0,{N1(12800,12000),SW(4,1),{700,ACT_TEMPO,0,0,V(-1500)},{1000,ACT_PLAY,0,0},{1500,ACT_SYNC,0,0},{6000,ACT_NATIVE_NUDGE,0,999}},0,{1}},
 {"software-master-back-from-follower","natives stopped; DECK 4 follows DECK 3; MASTER on DECK 4, then on DECK 3 again: DECK 4 finds DECK 3's beat again",
  14000,0,{N1(12000,12000),{400,ACT_STOCK_PLAY_OTHER,0,0},SW(5,4),{700,ACT_TEMPO,0,0,V(3200)},{710,ACT_TEMPO,1,0,V(-1500)},{1000,ACT_PLAY,0,0},
   {1100,ACT_MASTER,0,0},{1300,ACT_PLAY,1,0},{1800,ACT_SYNC,1,0},{6000,ACT_MASTER,1,0},{7500,ACT_MASTER,0,0}},0,{1}},
 {"software-master-reload","natives stopped; DECK 4 follows DECK 3; DECK 3 loads a new track, plays and takes MASTER again: DECK 4 finds its beat again",
  14000,0,{N1(12000,12000),{400,ACT_STOCK_PLAY_OTHER,0,0},SW(5,4),{700,ACT_TEMPO,0,0,V(3200)},{710,ACT_TEMPO,1,0,V(-1500)},{1000,ACT_PLAY,0,0},
   {1100,ACT_MASTER,0,0},{1300,ACT_PLAY,1,0},{1800,ACT_SYNC,1,0},{6000,ACT_LOAD,0,0,V(7)},{6300,ACT_PLAY,0,0},{6450,ACT_MASTER,0,0}},0,{1}},
 /* p.64: the deck that first plays an analysed track becomes master. Stock cannot see DECK 3/4 until one
    claims, so before this rule nobody became master and the native deck that played later took it. */
 {"first-play-software-master","no master yet, natives empty: DECK 3 plays first, then native DECK 1 loads and plays and DECK 4 syncs: DECK 3 is master throughout",
  8000,0,{SW(1,2),{1000,ACT_PLAY,0,0},{3000,ACT_NATIVE_LOAD,0,0,V(12000)},{3500,ACT_NATIVE_PLAY,0,0},{4000,ACT_PLAY,1,0},{4500,ACT_SYNC,1,0}},0,{1}},

 /* ---- Stock two-deck parity (stock-sync-spec.md, owner-observed in VINYL mode). One story per rule. */
 {"m2-native-pause-at-once","M2: native DECK 1 master (CDJ stop) pauses while synced DECK 3 plays and DECK 2 is stopped: DECK 3 is master at once, at the synced BPM",
  9000,0,{N1(12800,12000),SW(4,1),{700,ACT_TEMPO,0,0,V(-1500)},{1000,ACT_PLAY,0,0},{1500,ACT_SYNC,0,0},{6000,ACT_NATIVE_PLAY,0,0}},0,{0},3},
 {"m2-native-pause-defers-to-native","M2: as m2-native-pause-at-once with native DECK 2 also playing: stock hands master to DECK 2 itself and DECK 3 does not take it",
  9000,0,{N1(12800,12800),{400,ACT_STOCK_PLAY_OTHER,1,0},SW(4,1),{700,ACT_TEMPO,0,0,V(-1500)},{1000,ACT_PLAY,0,0},{1500,ACT_SYNC,0,0},{6000,ACT_NATIVE_PLAY,0,0}},0,
  {0,0,0,0,0,0,0,0,1},2},
 {"m4-software-master-hold","M4: natives stopped; DECK 3 master, DECK 4 synced; the DJ holds DECK 3's platter top 2.5 s (VINYL): DECK 4 takes master within 2 s at the shared BPM",
  10000,0,{N1(12000,12000),{400,ACT_STOCK_PLAY_OTHER,0,0},SW(5,4),{700,ACT_TEMPO,0,0,V(3200)},{710,ACT_TEMPO,1,0,V(-1500)},{1000,ACT_PLAY,0,0},
   {1100,ACT_MASTER,0,0},{1300,ACT_PLAY,1,0},{1800,ACT_SYNC,1,0},{6000,ACT_TOUCH,0,0,V(2500)}},0,{1},4},
 {"m4-native-hold-prefers-synced","M4: native DECK 1 master held 2.5 s (VINYL); DECK 3 plays unsynced, DECK 4 plays synced: master goes to DECK 4",
  12000,0,{N1(12800,12000),SW(1,4),{700,ACT_TEMPO,1,0,V(-1500)},{1000,ACT_PLAY,0,0},{1100,ACT_PLAY,1,0},{1500,ACT_SYNC,1,0},{6000,ACT_NATIVE_TOUCH,0,0,V(2500)}},0,{1},4},
 {"m5-software-master-load","M5: natives stopped; DECK 3 master (SYNC on, AUTO CUE off), DECK 4 synced; DECK 3 loads a new track while playing: DECK 4 is master and DECK 3 follows it at the shared BPM",
  12000,0,{N1(12000,12000),{400,ACT_STOCK_PLAY_OTHER,0,0},SW(5,4),{650,ACT_AUTO_CUE,0,0},{700,ACT_TEMPO,0,0,V(3200)},{710,ACT_TEMPO,1,0,V(-1500)},{1000,ACT_PLAY,0,0},
   {1100,ACT_MASTER,0,0},{1150,ACT_SYNC,0,0},{1300,ACT_PLAY,1,0},{1800,ACT_SYNC,1,0},{5000,ACT_LOAD,0,0,V(7)}},0,{1},4},
 {"m5-native-master-load","M5: native DECK 1 master plays, DECK 3 synced; DECK 1 loads a new track: DECK 3 is master at once, at the synced BPM",
  9000,0,{N1(12800,12000),SW(4,1),{700,ACT_TEMPO,0,0,V(-1500)},{1000,ACT_PLAY,0,0},{1500,ACT_SYNC,0,0},{5000,ACT_NATIVE_LOAD,0,0,V(12500)}},0,{1},3},
 {"m6-software-master-lone-pause","M6: natives stopped; DECK 4 master pauses (VINYL brake) with nothing else playing, then plays again: the engine never names stopped DECK 1, DECK 4 is master again",
  9000,0,{N1(12000,12000),{400,ACT_STOCK_PLAY_OTHER,0,0},SW(4,4),{700,ACT_TEMPO,1,0,V(2000)},{1000,ACT_PLAY,1,0},{1100,ACT_MASTER,1,0},
   {4000,ACT_PLAY,1,0},{6000,ACT_PLAY,1,0}},0,{1},4},
 {"m6-software-master-reload","M6: natives stopped; DECK 4 master (AUTO CUE off) loads a new track with nothing else playing: the engine never names stopped DECK 1, DECK 4 stays master",
  9000,0,{N1(12000,12000),{400,ACT_STOCK_PLAY_OTHER,0,0},SW(4,4),{650,ACT_AUTO_CUE,1,0},{1000,ACT_PLAY,1,0},{1100,ACT_MASTER,1,0},{4000,ACT_LOAD,1,0,V(7)}},0,{1},4},
 {"s2-sync-off-keeps-speed","S2: DECK 3 follows native DECK 1; a side nudge on DECK 1, then SYNC off on DECK 3: its speed does not step and it keeps the synced BPM",
  10000,0,{N1(12800,12000),SW(4,1),{700,ACT_TEMPO,0,0,V(-1500)},{1000,ACT_PLAY,0,0},{1500,ACT_SYNC,0,0},{6000,ACT_NATIVE_NUDGE,0,1,V(30)},{6400,ACT_SYNC,0,0}},0,{1}},
 {"s4-follower-nudge-keeps-offset","S4, S5, S3: DECK 4 follows native DECK 2; a side nudge on DECK 4 is kept as an offset (tempo matched, SYNC lamp flashing, no re-snap); SYNC off, then on again: it snaps to the beat, lamp steady",
  15000,0,{N2(12000,12800),SW(1,4),{1000,ACT_PLAY,1,0},{1500,ACT_SYNC,1,0},{6000,ACT_NUDGE,1,1,V(30)},{10000,ACT_SYNC,1,0},{10500,ACT_SYNC,1,0}},0,{1}},
 {"s4-follower-nudge-cdj","S4 in CDJ mode: as s4-follower-nudge-keeps-offset with both decks in CDJ mode (the nudge does not depend on the platter top)",
  15000,0,{N2(12000,12800),SW(1,4),{720,ACT_VINYL,1,0},{1000,ACT_PLAY,1,0},{1500,ACT_SYNC,1,0},{6000,ACT_NUDGE,1,0,V(30)},{10000,ACT_SYNC,1,0},{10500,ACT_SYNC,1,0}},0,{0}},
 {"s7-offset-survives-master-tempo","S7: as s4-follower-nudge-keeps-offset, then native DECK 2 moves from 128 to 131 BPM: DECK 4 follows the tempo and keeps the offset",
  14000,0,{N2(12000,12800),SW(1,4),{1000,ACT_PLAY,1,0},{1500,ACT_SYNC,1,0},{6000,ACT_NUDGE,1,1,V(30)},{8000,ACT_NATIVE_TEMPO,1,0,V(13100)}},0,{1}},
 {"s8-software-master-nudge","S8 rule 2: natives stopped; DECK 3 master, DECK 4 synced; a side nudge on DECK 3: master stays, DECK 4 keeps the offset, and what DECK 3 publishes keeps its tempo, not the bend",
  12000,0,{N1(12000,12000),{400,ACT_STOCK_PLAY_OTHER,0,0},SW(5,4),{700,ACT_TEMPO,0,0,V(3200)},{710,ACT_TEMPO,1,0,V(-1500)},{1000,ACT_PLAY,0,0},
   {1100,ACT_MASTER,0,0},{1300,ACT_PLAY,1,0},{1800,ACT_SYNC,1,0},{6000,ACT_NUDGE,0,1,V(30)}},0,{1},3},
 {"s8-native-master-nudge-cdj","S8 rule 1 in CDJ mode: DECK 4 follows native DECK 2 (CDJ); a side nudge on DECK 2: DECK 4 keeps the offset, no chase, no re-snap",
  12000,0,{N2(12000,12800),SW(1,4),{720,ACT_VINYL,1,0},{1000,ACT_PLAY,1,0},{1500,ACT_SYNC,1,0},{6000,ACT_NATIVE_NUDGE,1,0,V(30)}},0,{0}},
 {"sync-long-press","SYNC press and long press: DECK 4 follows native DECK 2; SYNC held 600 ms toggles once with no Instant Doubles; held 1.3 s it toggles once and Instant Doubles runs once",
  9000,0,{N2(12000,12800),SW(1,4),{1000,ACT_PLAY,1,0},{1500,ACT_SYNC,1,0},{3000,ACT_SYNC,1,0,V(600)},{5000,ACT_SYNC,1,0,V(1300)}},0,{1}},
};
static const struct scenario *scenario_active;
static void use_scenario(const struct scenario *c){
 /* The story's actions run until its first empty slot (every action is at t >= 300 ms). */
 run_ms=c->run_ms;memcpy(actions,c->acts,sizeof c->acts);
 for(action_count=0;action_count<sizeof c->acts/sizeof c->acts[0]&&c->acts[action_count].at_ms;action_count++){}
 assert(!c->count||c->count==action_count);
 if(c->seed)return; /* keep that seed's drawn schedule and policy */
 sch=(struct schedule){.worker_ms=2,.consumer_ms=20,.clock_ms=33,.led_ms={16,20},.led_phase={0,5},.connector_ms=50,.usb_burst=1};
 policy=(struct stock_policy){.stop_policy=0,.unload_policy=1,.brake_reports_playing=1};
 policy.native_vinyl=c->setup.native_vinyl;policy.reassert_playing_ms=c->setup.reassert_playing_ms;
 policy.refuse_playing=c->setup.refuse_playing;policy.brake_reports_playing=!c->setup.brake_drops_playing;
 policy.stop_policy=c->setup.stop_policy;
 if(c->setup.consumer_ms)sch.consumer_ms=(unsigned)c->setup.consumer_ms;
 if(c->setup.worker_ms)sch.worker_ms=(unsigned)c->setup.worker_ms;
 if(c->setup.usb_burst)sch.usb_burst=(unsigned)c->setup.usb_burst;
}
static int due(unsigned period,unsigned phase){return period&&(now32()+phase)%period==0;}
static int run_one(int verbose){
 reset_world();
 clock_ms=1000;sim_us=1000000;threaded_clock=0;wire_horizon=0;hold_flush=position_interval=position_blocked=position_noise=0;
 pub_log=getenv("SIM_PUBLOG")?atof(getenv("SIM_PUBLOG")):0;
 keep_running=keep_running||getenv("SIM_ALL")!=NULL;lock_trace=getenv("SIM_LOCKTRACE")?atoi(getenv("SIM_LOCKTRACE")):0;
 rng_seed(seed_value);draw_policy_and_schedule();draw_actions();
 if(lock_mode)draw_lock_story();
 if(scenario_active)use_scenario(scenario_active);
 if(verbose)printf("SCHEDULE worker=%u consumer=%u+-%u clock=%u led=%u/%u connector=%u usb-burst=%u rebuild=%u late=%u "
  "policy empty-sets=%d toggles-off=%d stop=%d unload=%d sync-claims=%d reassert=%u\n",
  sch.worker_ms,sch.consumer_ms,sch.consumer_jitter,sch.clock_ms,sch.led_ms[0],sch.led_ms[1],sch.connector_ms,sch.usb_burst,
  sch.rebuild,sch.late_writer,policy.master_press_empty_sets,policy.master_press_toggles_off,policy.stop_policy,
  policy.unload_policy,policy.sync_press_claims,policy.reassert_ms);
 /* The first runtime start fails at its first hook, as on a host, but selects global mode. */
 sim_bridge_reset();
 (void)xz_sync_link_runtime_start();
 struct xz_sync_link_test_ops ops={.sync_mgr_sync=st_sync_mgr,.get_player_info=st_get_player,.get_beat_info=st_get_beat,
  .set_player_info=st_set_player,.set_beat_info=st_set_beat,.set_master_on=st_set_master_on,
  .set_link_master=st_set_link_master,.monotonic_us=st_monotonic};
 xz_sync_link_runtime_test_install(&ops,&hooks);
 struct xz_xp_env env={{NULL,fake_link,sim_submit,fake_flush,fake_position,sim_transition},now_ms,sim_resolve,NULL,NULL,NULL,1,1};
 p=xz_xp_create(&env);assert(p);
 xz_xp_set_master_observer(p,xz_sync_link_runtime_master_observe);
 xz_xp_set_native_sync_clock(p,nc_copy);
 route_epoch=7;route_is_up=1;sim_route(route_epoch);sim_decks_attach(p,route_epoch);
 xz_deck_select_init(&ds);xz_deck_select_route_instance(&ds,1,route_epoch);publish_sides();
 sim_led_init((int)sch.rebuild);led_running=1;packet_variant[0]=packet_variant[1]=SIM_PACKET_ONE;
 nc_publish();
 unsigned next_action=0;uint32_t start=now32(),end=start+run_ms;trace_start=start;
 uint32_t next_consumer=start+sch.consumer_ms,next_reassert=start+policy.reassert_ms,next_same=start+policy.reassert_playing_ms;
 /* A fixed per-tick draw keeps scheduling independent of which actions are enabled. */
 while(now32()<end&&(!fail_class||keep_running)){
  sim_us+=1000;clock_ms=now32();
  uint32_t t=now32()-start;
  uint32_t j0=rnd(),j1=rnd();(void)j1;
  for(int d=0;d<2;d++){
   struct native *n=&nat[d];double motion=0;
   if(n->loaded&&n->playing){
    if(n->touch)motion=0;
    else if(n->brake){
     double left=1.-(double)(now32()-n->brake_at)/(double)n->brake_ms;
     if(left<=0){n->brake=0;n->playing=0;native_stop_policy(d);}else motion=left;
    }else motion=1.+((int32_t)(n->bend_until-now32())>0?n->bend:0.);
   }
   n->motion=motion;n->beats+=n->bpm/60000.*motion;
   nat_beats[d][now32()%NAT_RING]=n->beats;nat_motion[d][now32()%NAT_RING]=motion;
  }
  nat_hist_end=now32();
  if(t%sch.usb_burst==0){sim_consume(0,44.1*sch.usb_burst);sim_consume(1,44.1*sch.usb_burst);mono_wire=wire_base+(uint64_t)llround(f.wire[0]);consume_us=sim_us;}
  while(next_action<action_count&&actions[next_action].at_ms<=t){
   if(action_enabled[next_action])apply(&actions[next_action]);
   next_action++;
  }
  for(unsigned i=0;i<hold_count;){
   if(holds[i].at<=now32()){struct pending_release r=holds[i];holds[i]=holds[--hold_count];
    if(sync_down[r.side]){sync_hold_sent[r.side]=1;key(r.side,r.code,1);}}
   else i++;
  }
  for(unsigned i=0;i<release_count;){
   if(releases[i].at<=now32()){struct pending_release r=releases[i];releases[i]=releases[--release_count];key(r.side,r.code,2);
    if(r.code==0x4313)sync_release_check(r.side);}
   else i++;
  }
  for(unsigned i=0;i<deferred_count;){
   if((int32_t)(now32()-deferred[i].at)<0){i++;continue;}
   struct deferred r=deferred[i];deferred[i]=deferred[--deferred_count];
   if(r.extra){struct xz_extra_deck_event e={.deck=r.side,.kind=XZ_EXTRA_IN_JOG_TOUCH,.pressed=0};(void)xz_extra_deck_input(&e);}
   else nat[r.side].touch=0;
  }
  if(policy.reassert_ms&&now32()>=next_reassert){
   int32_t m=engine_master();if(m>=0&&m<2)stock_set_master(m);
   next_reassert=now32()+policy.reassert_ms;
  }
  /* Stock confirming its own playing master: the same id again, not a DJ's choice. */
  if(policy.reassert_playing_ms&&now32()>=next_same){
   int32_t m=engine_master();if(m>=0&&m<2&&nat[m].loaded&&nat[m].playing){int id=m;(void)hooks.set_master_on(sm_master,&id);}
   next_same=now32()+policy.reassert_playing_ms;
  }
  /* Actors due this millisecond run in a seeded order. */
  int order[6]={0,1,2,3,4,5};
  for(int i=5;i>0;i--){int k=(int)((j0>>(i*4))%(unsigned)(i+1));int x=order[i];order[i]=order[k];order[k]=x;}
  for(int k=0;k<6;k++)switch(order[k]){
   case 0:if(now32()>=stall_until[2]&&due(sch.worker_ms,0)){stepping_side=0;xz_xp_step(p,0);}break;
   case 1:if(now32()>=stall_until[2]&&due(sch.worker_ms,1)){stepping_side=1;xz_xp_step(p,1);}break;
   case 2:if(now32()>=next_consumer){
     if(now32()>=stall_until[0]){
      /* A stock consumer gap beyond the observation age bound expires authority by design. */
      if((uint32_t)(now32()-last_consumer_ms)>XZ_MASTER_MAX_AGE_MS)consumer_gap_until=now32()+150;
      hooks.sync_mgr_sync(sm_mgr);last_consumer_ms=now32();
     }
     next_consumer=now32()+sch.consumer_ms+(sch.consumer_jitter?rnd_below(sch.consumer_jitter+1):0);
    }break;
   case 3:if(now32()>=stall_until[1]&&due(sch.clock_ms,3))nc_publish();break;
   case 4:if(now32()>=stall_until[1]&&due(sch.connector_ms,7)){struct xz_extra_decks snap;(void)xz_extra_decks_snapshot(&snap);}break;
   case 5:if(now32()>=stall_until[1])for(unsigned c=1;c<=2;c++)if(due(sch.led_ms[c-1],sch.led_phase[c-1])){
     struct sim_led_stock st=stock_lamps();
     sim_led_callback(c,packet_variant[c-1],(int)sch.late_writer,&st);
     check_led(c);
    }break;
  }
  track_engine();
  if(t%10==0){check_world();check_lock();check_sync_off();}
  if(trace_on&&trace_every&&t>=trace_from&&t%trace_every==0)trace("tick",-1,0);
 }
 if(scenario_active&&scenario_active->end_master&&engine_master()!=scenario_active->end_master-1)
  fail(F_HANDOFF,"story end: engine names DECK %d, the story expects DECK %d",engine_master()+1,scenario_active->end_master);
 int result=fail_class;
 if(result&&verbose){
  struct xz_extra_decks v;xz_xp_read(p,&v);struct xz_master_observation o;(void)xz_sync_link_runtime_master_observe(&o);
  printf("FAIL %s at=%u %s\n",fail_name[result],fail_at-start,fail_detail);
  for(int c=1;c<F_COUNT;c++)if(c!=result&&(fail_mask&(1ull<<c)))printf("  ALSO %s at=%u %s\n",fail_name[c],class_at[c]-start,class_detail[c]);
  {char b[512];sim_bridge_debug(b,sizeof b);printf("  bridge %s\n",b);}
  printf("  engine=%d observed=%d rev=%u op=%u req=%u lease=%u intent=%d/%u/x%u/op%u route=%d/%u\n",engine_master(),o.deck,o.revision,o.operator_epoch,o.request_epoch,
   o.lease_generation,v.master_intent.deck,v.master_intent.epoch,v.master_intent.explicit_choice,v.master_intent.operator_epoch,route_is_up,route_epoch);
  for(int i=0;i<2;i++)printf("  deck%d loaded=%d playing=%d master=%d sync=%d locked=%d reason=%d mdeck=%d src=%d tm=%d err=%dus | native%d loaded=%d playing=%d sync=%d bpm=%.0f | side%d %s\n",
   i+3,v.deck[i].loaded,v.deck[i].playing,v.deck[i].master,v.deck[i].sync.requested,v.deck[i].sync.locked,v.deck[i].sync.reason,
   v.deck[i].sync.master_deck,v.deck[i].sync.source,v.deck[i].sync.tempo_matched,v.deck[i].sync.phase_error_us,
   i+1,nat[i].loaded,nat[i].playing,nat[i].sync,nat[i].bpm,i+1,ds.side[i].target==XZ_DS_EXTRA?"EXTRA":"NATIVE");
 }
 sim_decks_attach(NULL,0);xz_sync_link_runtime_stop();xz_xp_destroy(p);p=NULL;
 return result;
}

/* ------------------------------------------------------ minimization */
static int replay_masked(void){return run_one(0);}
static void minimize(int cls){
 /* Drop actions one at a time from the end while the same class reproduces. */
 for(int i=(int)action_count-1;i>=(int)opening_count;i--){
  if(!action_enabled[i])continue;
  action_enabled[i]=0;
  if(replay_masked()!=cls)action_enabled[i]=1;
 }
}
static void print_actions(void){
 for(unsigned i=0;i<action_count;i++)if(action_enabled[i])
  printf("  action %u t=%u %s side=%u arg=%d\n",i,actions[i].at_ms,act_name[actions[i].kind],actions[i].side,actions[i].arg);
}

int main(int argc,char **argv){
 assert(argc>=3);
 assert(!pthread_mutex_init(&f.lock,NULL));
 snprintf(fixtures,sizeof fixtures,"%s",argv[1]);
 snprintf(anlz_dir,sizeof anlz_dir,"%s/anlz",argv[1]);
 write_fixture(argv[1]);
 run_ms=15000;
 if(!strcmp(argv[2],"regress")){
  int failed=0;
  for(unsigned i=0;i<sizeof scenarios/sizeof scenarios[0];i++){
   if(argc>3&&strcmp(argv[3],scenarios[i].name))continue;
   scenario_active=&scenarios[i];seed_value=scenarios[i].seed?scenarios[i].seed:1;memset(action_enabled,1,sizeof action_enabled);
   trace_on=getenv("SIM_TRACE_EVERY")!=NULL;if(trace_on){trace_every=(unsigned)atoi(getenv("SIM_TRACE_EVERY"));trace_from=getenv("SIM_TRACE_FROM")?(unsigned)atoi(getenv("SIM_TRACE_FROM")):0;}
   unsigned long l0=cov_lock_samples,p0=cov_lock_phase,k0=cov_lock_ok,s0=cov_pub_served,c0=cov_pub_checked,r0=cov_pub_refused;lock_worst_ms=pub_worst_ms=0;
   int r=run_one(1);trace_on=0;int note=scenarios[i].setup.stock_dependent;failed|=r!=0&&!note;
   printf("%s %s: %s\n",r?note?"NOTE":"FAIL":"PASS",scenarios[i].name,scenarios[i].story);
   if(getenv("SIM_COVERAGE"))printf("  coverage lock=%lu phase=%lu within=%lu worst=%.2fms publish served=%lu checked=%lu refused=%lu worst=%.2fms\n",
    cov_lock_samples-l0,cov_lock_phase-p0,cov_lock_ok-k0,lock_worst_ms,cov_pub_served-s0,cov_pub_checked-c0,cov_pub_refused-r0,pub_worst_ms);
  }
  scenario_active=NULL;return failed;
 }
 if(!strcmp(argv[2],"lock")||!strcmp(argv[2],"lockreplay")){
  /* Lock stories: every class each seed hits is counted (a seed keeps running after a failure). */
  lock_mode=1;keep_running=1;
  if(!strcmp(argv[2],"lockreplay")){
   seed_value=strtoull(argv[3],NULL,10);run_ms=argc>4?(unsigned)strtoul(argv[4],NULL,10):30000;
   memset(action_enabled,1,sizeof action_enabled);
   if(argc>5){FILE *m=fopen(argv[5],"r");assert(m);memset(action_enabled,0,sizeof action_enabled);unsigned k;while(fscanf(m,"%u",&k)==1)if(k<MAX_ACTIONS)action_enabled[k]=1;fclose(m);}
   trace_on=getenv("SIM_TRACE")!=NULL;if(getenv("SIM_TRACE_EVERY"))trace_every=(unsigned)atoi(getenv("SIM_TRACE_EVERY"));if(getenv("SIM_TRACE_FROM"))trace_from=(unsigned)atoi(getenv("SIM_TRACE_FROM"));
   int r=run_one(1);
   printf("RESULT lockreplay seed=%llu pair=%s %s mask=0x%llx first=%s\n",(unsigned long long)seed_value,pair_name[lock_pair],lock_vinyl?"VINYL":"CDJ",(unsigned long long)fail_mask,fail_name[r]);
   print_actions();return 0;
  }
  uint64_t first=strtoull(argv[3],NULL,10);unsigned seeds=(unsigned)strtoul(argv[4],NULL,10);
  run_ms=argc>5?(unsigned)strtoul(argv[5],NULL,10):30000;
  unsigned counts[F_COUNT]={0},by[F_COUNT][12];uint64_t first_seed[F_COUNT]={0};unsigned runs[12]={0},bad=0;
  memset(by,0,sizeof by);
  for(unsigned n=0;n<seeds;n++){
   seed_value=first+n;memset(action_enabled,1,sizeof action_enabled);
   (void)run_one(0);runs[lock_pair*2+lock_vinyl]++;
   /* Classes that exist only under some stock behaviour report without gating, as in the seed batch. */
   uint64_t stock_only=getenv("SIM_STOCK_CLASSES_REPORT_ONLY")?(1ull<<F_NONE_ELECTION)|(1ull<<F_HANDOFF_UNLOADED):0;
   if(fail_mask&~stock_only)bad++;
   for(int c=1;c<F_COUNT;c++)if(fail_mask&(1ull<<c)){if(!counts[c])first_seed[c]=seed_value;counts[c]++;by[c][lock_pair*2+lock_vinyl]++;}
  }
  printf("LOCKCOVERAGE samples=%lu phase=%lu within=%lu publish-served=%lu checked=%lu refused=%lu\n",cov_lock_samples,cov_lock_phase,cov_lock_ok,cov_pub_served,cov_pub_checked,cov_pub_refused);
  printf("LOCKQUALITY settled=%lu locked-flag=%.1f%% mean|tempo-err|=%.3f%% max=%.3f%% worst-phase=%.2fms worst-publish=%.2fms\n",
   flutter_n,flutter_n?100.*locked_n/flutter_n:0.,flutter_n?100.*flutter_sum/flutter_n:0.,100.*flutter_max,lock_worst_ms,pub_worst_ms);
  printf("LOCKRUNS");for(int k=0;k<12;k++)printf(" %s/%s=%u",pair_name[k/2],k&1?"VINYL":"CDJ",runs[k]);printf("\n");
  for(int c=1;c<F_COUNT;c++)if(counts[c]){
   printf("LOCKCLASS %s seeds=%u/%u first-seed=%llu by-pair:",fail_name[c],counts[c],seeds,(unsigned long long)first_seed[c]);
   for(int k=0;k<12;k++)if(by[c][k])printf(" %s/%s=%u",pair_name[k/2],k&1?"VINYL":"CDJ",by[c][k]);printf("\n");
  }
  printf("RESULT lock first-seed=%llu seeds=%u run-ms=%u seeds-with-failures=%u\n",(unsigned long long)first,seeds,run_ms,bad);
  return bad?1:0;
 }
 if(!strcmp(argv[2],"replay")){
  seed_value=strtoull(argv[3],NULL,10);if(argc>4)run_ms=(unsigned)strtoul(argv[4],NULL,10);
  memset(action_enabled,1,sizeof action_enabled);
  if(argc>5){FILE *m=fopen(argv[5],"r");assert(m);memset(action_enabled,0,sizeof action_enabled);unsigned k;while(fscanf(m,"%u",&k)==1)if(k<MAX_ACTIONS)action_enabled[k]=1;fclose(m);for(unsigned k2=0;k2<6;k2++)action_enabled[k2]=1;}
  trace_on=1;if(getenv("SIM_TRACE_EVERY"))trace_every=(unsigned)atoi(getenv("SIM_TRACE_EVERY"));if(getenv("SIM_TRACE_FROM"))trace_from=(unsigned)atoi(getenv("SIM_TRACE_FROM"));
  int r=run_one(1);trace_on=0;
  printf("RESULT replay seed=%llu class=%s\n",(unsigned long long)seed_value,fail_name[r]);return r?1:0;
 }
 uint64_t first=strtoull(argv[2],NULL,10);unsigned seeds=(unsigned)strtoul(argv[3],NULL,10);
 if(argc>4)run_ms=(unsigned)strtoul(argv[4],NULL,10);
 int minimize_on=argc>5&&!strcmp(argv[5],"--minimize");
 unsigned counts[F_COUNT]={0};uint64_t first_seed[F_COUNT]={0};unsigned failures=0,notes=0;
 /* Classes whose expectation depends on stock behaviour the model only guesses (see REPORT). */
 int report_only=getenv("SIM_STOCK_CLASSES_REPORT_ONLY")!=NULL;
 for(unsigned n=0;n<seeds;n++){
  seed_value=first+n;memset(action_enabled,1,sizeof action_enabled);
  int r=run_one(0);
  if(r){if(report_only&&(r==F_NONE_ELECTION||r==F_HANDOFF_UNLOADED))notes++;else failures++;if(!counts[r])first_seed[r]=seed_value;counts[r]++;}
 }
 /* Before the replays below, which would add their own counts. */
 printf("COVERAGE led-sync-on=%lu led-sync-off=%lu led-master-on=%lu led-master-off=%lu native-rows=%lu forced-rows=%lu jog=%lu settled-follow=%lu locked=%lu master-stops=%lu\n",
  cov_sync_on,cov_sync_off,cov_master_on,cov_master_off,cov_native,cov_forced,cov_jog,cov_lock,cov_locked,cov_handoff_order);
 for(int c=1;c<F_COUNT;c++)if(counts[c]){
  printf("%s %s count=%u first-seed=%llu\n",report_only&&(c==F_NONE_ELECTION||c==F_HANDOFF_UNLOADED)?"NOTE":"CLASS",fail_name[c],counts[c],(unsigned long long)first_seed[c]);
  seed_value=first_seed[c];memset(action_enabled,1,sizeof action_enabled);
  if(minimize_on){minimize(c);}
  int r=run_one(1);
  printf("MINIMIZED seed=%llu class=%s reproduced=%s actions:\n",(unsigned long long)seed_value,fail_name[c],r==c?"yes":"no");
  print_actions();
 }
 printf("RESULT first-seed=%llu seeds=%u run-ms=%u failures=%u stock-dependent-notes=%u\n",(unsigned long long)first,seeds,run_ms,failures,notes);
 return failures?1:0;
}
