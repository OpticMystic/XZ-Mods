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
struct native { int loaded,playing,sync; uint32_t identity,revision; double bpm,beats; };
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
static int st_sync_mgr(void *mgr){
 (void)mgr;int32_t m=engine_master();
 if(m==2||m==3){
  /* The engine follows virtual player 3/4 through the hooked status reads. */
  int id=m+1;uint8_t lane=0;unsigned char st[56],bt[44];
  (void)hooks.get_player_info(sm_link,&id,st);(void)hooks.get_beat_info(sm_link,&id,&lane,bt);
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
 }
 int m=id<=0?-1:id-1;return hooks.set_master_on(sm,&m);
}
static int st_monotonic(uint64_t *out){*out=sim_us;return 0;}
/* Stock's own master change: every non-internal call is an operator event to the bridge. */
static uint32_t last_operator_ms;  /* last action that may legitimately change mastership */
static uint32_t now32(void);
static int master_was_playing;
static void stock_set_master(int32_t id){operator_events++;last_operator_ms=now32();master_was_playing=0;int v=id;(void)hooks.set_master_on(sm_master,&v);}

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
   s.bpm=(float)n->bpm;s.nominal_bpm=(float)n->bpm;s.speed=1;
   s.bar_phase=(float)fmod(n->beats,4.);s.playing=n->playing;s.manual=0;
   s.evidence=XZ_SYNC_NATIVE_MASTER_OBSERVED|XZ_SYNC_NATIVE_TRACK_OBSERVED;
   if(n->playing)s.playing_native_mask=1u<<m;
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
static char sim_wav[512];
static void write_fixture(const char *dir){
 snprintf(sim_wav,sizeof sim_wav,"%s/sim.wav",dir);
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
   for(unsigned i=0;i<4410;i++){int16_t x=(int16_t)(4000*sin(2*M_PI*220*(at+i)/44100.));buf[2*i]=buf[2*i+1]=x;}
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
}
static int sim_resolve(int source,uint32_t id,struct xz_extra_track *out){
 memset(out,0,sizeof *out);out->source=source;out->track_id=id;
 if(id==3){snprintf(out->path,sizeof out->path,"%s/missing.wav",fixtures);snprintf(out->title,sizeof out->title,"Missing");return 0;}
 if(id!=1&&id!=2)return -1;
 snprintf(out->path,sizeof out->path,"%s",sim_wav);
 snprintf(out->anlz_dir,sizeof out->anlz_dir,"%s",anlz_dir);
 snprintf(out->title,sizeof out->title,id==1?"Sim A":"Sim B");
 out->track_bpm_x100=12000;return 0;
}

/* ------------------------------------------------------- the actions */
enum act {
 ACT_SYNC,ACT_MASTER,ACT_PLAY,ACT_SELECT,ACT_LOAD,ACT_LOAD_MISSING,ACT_NATIVE_LOAD,ACT_NATIVE_UNLOAD,
 ACT_TEMPO,ACT_NATIVE_TEMPO,ACT_ROUTE_DOWN,ACT_ROUTE_UP,ACT_STALL_CONSUMER,ACT_STALL_PUBLISHERS,
 ACT_STALL_WORKERS,ACT_PACKET,ACT_LED_STOP,ACT_LED_START,ACT_STOCK_PLAY_OTHER,ACT_COUNT
};
static const char *act_name[]={"sync","master","play","select","load","load-missing","native-load","native-unload",
 "tempo","native-tempo","route-down","route-up","stall-consumer","stall-publishers","stall-workers","packet",
 "led-stop","led-start","native-play-other"};
struct action { uint32_t at_ms; uint8_t kind,side; int16_t arg; };
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
static uint64_t seed_value;
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
static struct pending_release releases[16];
static unsigned release_count;

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
 if(code==0x4101&&n->loaded){n->playing^=1;}
}
static uint32_t sync_presses_delivered[2],master_presses_delivered[2];
static void key(int side,uint16_t code,unsigned op){
 struct xz_ds_outcome o;
 xz_deck_select_route_instance(&ds,route_is_up,route_is_up?route_epoch:0);
 if(code==0x4312)o=xz_deck_select_global_key(&ds,(unsigned)side+1,code,op,now32());
 else{struct xz_ds_key k={.side=side,.code=code,.op=op};o=xz_deck_select_key(&ds,&k,now32());}
 if(o.verdict==XZ_DS_DELIVER){
  int ok=xz_extra_deck_input(&o.event);
  if(ok&&o.event.kind==XZ_EXTRA_IN_SYNC)sync_presses_delivered[side]++;
  if(ok&&o.event.kind==XZ_EXTRA_IN_MASTER)master_presses_delivered[side]++;
 }else if(o.verdict==XZ_DS_PASS&&op==0){
  if(code==0x4312)native_master_selected(side);else native_key(side,code);
 }
}
static void tap(int side,uint16_t code,unsigned hold_ms){
 /* A held button is released before it can be pressed again. */
 for(unsigned i=0;i<release_count;i++)if(releases[i].side==side&&releases[i].code==code){
  struct pending_release r=releases[i];releases[i]=releases[--release_count];key(r.side,r.code,2);break;
 }
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
 F_SYNC_PRESS_LOST,F_NONE_ELECTION,F_HANDOFF_UNLOADED,F_HANDOFF_ORDER,F_COUNT
};
static const char *fail_name[]={"none","sync-lamp-dark","sync-lamp-stuck-on","master-lamp-dark","master-lamp-false",
 "native-lamp-overwritten","foreign-forced-row-modified","jog-sync-indicator","duplicate-master","view-master-disagrees",
 "sync-not-locked","sync-wrong-master","handoff-missing","explicit-master-not-taken","sync-press-lost","none-election","handoff-from-unloaded-native","handoff-order"};
static int fail_class;
static uint32_t fail_at;
static char fail_detail[512];
static void fail(int c,const char *fmt,...) __attribute__((format(printf,2,3)));
static void fail(int c,const char *fmt,...){
 if(fail_class)return;
 fail_class=c;fail_at=now32();
 va_list ap;va_start(ap,fmt);vsnprintf(fail_detail,sizeof fail_detail,fmt,ap);va_end(ap);
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
static struct since lamp_sync_bad[2],lamp_sync_stuck[2],lamp_master_bad[2],lamp_master_false[2],jog_bad[2],dup_bad,view_bad[2],lock_bad[2],wrong_bad[2],none_bad,handoff_bad;
static struct since native_bad[2];
static uint32_t master_press_at[2]; static int master_press_pending[2],master_press_playing[2];
static uint32_t sync_press_at[2]; static int sync_press_pending[2],sync_press_before[2];
static uint32_t lock_signature[2];
static uint32_t lock_signature_at[2];
static int32_t master_tracked=-9; static uint32_t master_ident;

static int software_playing(const struct xz_extra_deck *d){return d->loaded&&d->playing;}
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
 int want_sync=d->loaded&&d->sync.requested;
 int engine_is=engine_master()==side+2;
 int want_master=d->loaded&&engine_is;
 if(want_sync)cov_sync_on++;else cov_sync_off++;
 if(want_master)cov_master_on++;else cov_master_off++;
 if(held(&lamp_sync_bad[side],want_sync&&!sync.state,120))
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
 int expected=-9;
 if(stale){
  if(m==2)expected=d4?3:native_playing>=0?-4:-9;
  else if(m==3)expected=native_playing>=0?-4:d3?2:-9;
  else expected=d3?2:d4?3:-9;
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
   int next=i==0?(d4?3:native_playing>=0?-4:-9):(native_playing>=0?-4:d3?2:-9);
   if(next!=-9){cov_handoff_order++;order_pending=1;order_expected=next;order_from=i+2;order_at=now;
    order_alt=pause_press_at[i]&&(uint32_t)(now-pause_press_at[i])<1500?press_next[i]:-9;}
  }
  prev_playing[i]=playing;
 }
 if(order_pending&&(int32_t)(last_operator_ms-order_at)>0)order_pending=0;
 /* A later transport press on a software deck changes what the next master should be. */
 for(int i=0;i<2;i++)if(order_pending&&play_press_at[i]&&(int32_t)(play_press_at[i]-order_at)>0)order_pending=0;
 if(order_pending&&native_transport_at&&(int32_t)(native_transport_at-order_at)>0)order_pending=0;
 if(order_pending&&(uint32_t)(now-order_at)>1500){
  order_pending=0;
  int still=order_expected==-4?native_playing>=0:order_expected==2?d3:d4;
  int ok=order_expected==-4?(m>=0&&m<2&&nat[m].playing):m==order_expected;
  if(order_alt!=-9)ok|=order_alt==-4?(m>=0&&m<2&&nat[m].playing):m==order_alt;
  if(still&&!ok)fail(F_HANDOFF_ORDER,"from=%d expected=%d engine=%d d3=%d/%d d4=%d/%d native=%d/%d",order_from,order_expected,m,
   v.deck[0].loaded,v.deck[0].playing,v.deck[1].loaded,v.deck[1].playing,nat[0].playing,nat[1].playing);
 }
 if(held(&unloaded_bad,unloaded&&software_candidate&&quiet,1500))
  fail(F_HANDOFF_UNLOADED,"engine=%d unloaded d3=%d/%d d4=%d/%d",m,v.deck[0].loaded,v.deck[0].playing,v.deck[1].loaded,v.deck[1].playing);
 /* No master while a software deck plays with SYNC requested by any deck. */
 int wants=v.deck[0].sync.requested||v.deck[1].sync.requested;
 if(held(&none_bad,m==-1&&software_candidate&&wants&&quiet&&!awaiting,1500))
  fail(F_NONE_ELECTION,"engine=-1 d3=%d/%d sync=%d d4=%d/%d sync=%d",v.deck[0].loaded,v.deck[0].playing,v.deck[0].sync.requested,v.deck[1].loaded,v.deck[1].playing,v.deck[1].sync.requested);
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

/* ------------------------------------------------------------ one run */
static unsigned trace_on,trace_every,trace_from;static uint32_t trace_start;
static void trace(const char *what,int side,int arg){
 if(!trace_on)return;
 char b[512];sim_bridge_debug(b,sizeof b);struct xz_extra_decks v;xz_xp_read(p,&v);
 printf("  t=%u %s side=%d arg=%d engine=%d | d3 %d/%d/m%d d4 %d/%d/m%d intent=%d/%u/x%u obs=%d | %s\n",now32()-trace_start,what,side,arg,engine_master(),
  v.deck[0].loaded,v.deck[0].playing,v.deck[0].master,v.deck[1].loaded,v.deck[1].playing,v.deck[1].master,
  v.master_intent.deck,v.master_intent.epoch,v.master_intent.explicit_choice,v.master_actual.deck,b);
}
static void reset_world(void){
 memset(nat,0,sizeof nat);memset(foreign_slot,0,sizeof foreign_slot);
 operator_events=0;release_count=0;fail_class=F_NONE;fail_detail[0]=0;
 memset(lamp_sync_bad,0,sizeof lamp_sync_bad);memset(lamp_sync_stuck,0,sizeof lamp_sync_stuck);memset(lamp_master_bad,0,sizeof lamp_master_bad);
 memset(lamp_master_false,0,sizeof lamp_master_false);memset(jog_bad,0,sizeof jog_bad);memset(&dup_bad,0,sizeof dup_bad);
 memset(view_bad,0,sizeof view_bad);memset(lock_bad,0,sizeof lock_bad);memset(wrong_bad,0,sizeof wrong_bad);
 memset(&none_bad,0,sizeof none_bad);memset(&unloaded_bad,0,sizeof unloaded_bad);order_pending=0;memset(prev_playing,0,sizeof prev_playing);press_next[0]=press_next[1]=-9;
 memset(pause_press_at,0,sizeof pause_press_at);memset(play_press_at,0,sizeof play_press_at);native_transport_at=0;memset(master_seen_at,0,sizeof master_seen_at);memset(&handoff_bad,0,sizeof handoff_bad);memset(native_bad,0,sizeof native_bad);
 memset(master_press_pending,0,sizeof master_press_pending);memset(sync_press_pending,0,sizeof sync_press_pending);
 memset(lock_signature,0,sizeof lock_signature);memset(lock_signature_at,0,sizeof lock_signature_at);
 memset(stall_until,0,sizeof stall_until);last_consumer_ms=consumer_gap_until=0;memset(sync_presses_delivered,0,sizeof sync_presses_delivered);
 memset(master_presses_delivered,0,sizeof master_presses_delivered);
 master_tracked=-9;master_was_playing=0;master_ident=0;last_operator_ms=0;memset(master_press_playing,0,sizeof master_press_playing);
 memset(sm_mgr,0,sizeof sm_mgr);memset(sm_master,0,sizeof sm_master);memset(sm_link,0,sizeof sm_link);
 void *smp=sm_master,*lmp=sm_link;memcpy(sm_mgr+4,&smp,sizeof smp);memcpy(sm_master+0x30,&lmp,sizeof lmp);
 int32_t none=-1;memcpy(sm_master+8,&none,4);
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
 switch(a->kind){
 case ACT_SYNC:
  if(extra&&v.deck[s].loaded){sync_press_pending[s]=1;sync_press_at[s]=now32();sync_press_before[s]=v.deck[s].sync.requested;}
  tap(s,0x4313,30+(unsigned)a->arg%80);break;
 case ACT_MASTER:
  last_operator_ms=now32();master_was_playing=0; /* a new choice must play before its stop hands over */
  if(extra&&v.deck[s].loaded){master_press_pending[s]=1;master_press_at[s]=now32();
   master_press_playing[s]=v.deck[s].playing&&!(play_press_at[s]&&(uint32_t)(now32()-play_press_at[s])<1000);}
  else master_press_pending[0]=master_press_pending[1]=0;
  tap(s,0x4312,30+(unsigned)a->arg%80);break;
 case ACT_PLAY:{
  int was=extra?v.deck[s].playing:nat[s].playing;
  if(extra)master_press_playing[s]=0; /* a transport press ends the explicit-press window */
  if(extra){play_press_at[s]=now32();
   if(was){int d3p=v.deck[0].playing&&s!=0,d4p=v.deck[1].playing&&s!=1;int np=(nat[0].loaded&&nat[0].playing)||(nat[1].loaded&&nat[1].playing);
    press_next[s]=s==0?(d4p?3:np?-4:-9):(np?-4:d3p?2:-9);pause_press_at[s]=now32();}}
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
  xz_deck_select_route_instance(&ds,route_is_up,route_is_up?route_epoch:0);
  xz_deck_select_toggle(&ds,s,NULL);publish_sides();break;
 case ACT_LOAD:case ACT_LOAD_MISSING:{
  struct xz_extra_deck_event e={.deck=s,.kind=XZ_EXTRA_IN_LOAD,.pressed=1};
  e.load.source=1;e.load.track_id=a->kind==ACT_LOAD_MISSING?3:1+(uint32_t)(a->arg&1);
  (void)xz_extra_deck_input(&e);master_press_playing[s]=0;
  if(master_tracked==s+2)last_operator_ms=now32();
  break;}
 case ACT_NATIVE_LOAD:{native_transport_at=now32();
  struct native *n=&nat[s];int was_master=engine_master()==s;
  n->loaded=1;n->playing=a->at_ms<800?s==0:0;n->identity=++native_identity_counter;n->revision=++native_revision_counter;
  n->bpm=118+(a->arg%10);n->beats=0;
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
  e.tempo_position=(int32_t)(a->arg%2001)-1000;(void)xz_extra_deck_input(&e);break;}
 case ACT_NATIVE_TEMPO:nat[s].bpm=115+(a->arg%15);break;
 case ACT_ROUTE_DOWN:
  if(!route_is_up)break;
  route_is_up=0;fake_route(0);sim_decks_route(0,0);last_operator_ms=now32();
  xz_deck_select_route_instance(&ds,0,0);publish_sides();break;
 case ACT_ROUTE_UP:
  if(route_is_up)break;
  route_is_up=1;route_epoch++;fake_route(route_epoch);sim_decks_route(1,route_epoch);last_operator_ms=now32();
  xz_deck_select_route_instance(&ds,1,route_epoch);publish_sides();break;
 /* A stall is a disturbance, not an operator choice: checks restart after it ends. */
 case ACT_STALL_CONSUMER:stall_until[0]=now32()+80+(uint32_t)a->arg%300;last_operator_ms=stall_until[0];break;
 case ACT_STALL_PUBLISHERS:stall_until[1]=now32()+80+(uint32_t)a->arg%300;last_operator_ms=stall_until[1];break;
 case ACT_STALL_WORKERS:stall_until[2]=now32()+20+(uint32_t)a->arg%200;last_operator_ms=stall_until[2];break;
 case ACT_PACKET:packet_variant[s]=(enum sim_packet)((unsigned)a->arg%SIM_PACKETS);
  lamp_sync_bad[s].on=lamp_sync_stuck[s].on=lamp_master_bad[s].on=lamp_master_false[s].on=native_bad[s].on=0;break;
 case ACT_LED_STOP:led_running=0;sim_led_stop();break;
 case ACT_LED_START:led_running=1;sim_led_start();break;
 }
}
/* Named regressions: fixed schedule, fixed stock policy, a short scripted story. */
#define OPENING {300,ACT_NATIVE_LOAD,0,0},{305,ACT_NATIVE_LOAD,1,0},{310,ACT_LOAD,0,0},{315,ACT_LOAD,1,1},{320,ACT_SELECT,0,0},{325,ACT_SELECT,1,0}
struct scenario { const char *name,*story; unsigned run_ms,count; struct action acts[24]; uint64_t seed; };
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
};
static const struct scenario *scenario_active;
static void use_scenario(const struct scenario *c){
 run_ms=c->run_ms;action_count=c->count;memcpy(actions,c->acts,sizeof c->acts);
 if(c->seed)return; /* keep that seed's drawn schedule and policy */
 sch=(struct schedule){.worker_ms=2,.consumer_ms=20,.clock_ms=33,.led_ms={16,20},.led_phase={0,5},.connector_ms=50,.usb_burst=1};
 policy=(struct stock_policy){.stop_policy=0,.unload_policy=1};
 run_ms=c->run_ms;action_count=c->count;memcpy(actions,c->acts,sizeof c->acts);
}
static int due(unsigned period,unsigned phase){return period&&(now32()+phase)%period==0;}
static int run_one(int verbose){
 reset_world();
 clock_ms=1000;sim_us=1000000;threaded_clock=0;wire_horizon=0;hold_flush=position_interval=position_blocked=position_noise=0;
 rng_seed(seed_value);draw_policy_and_schedule();draw_actions();
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
 struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,sim_resolve,NULL,NULL,NULL,1,1};
 p=xz_xp_create(&env);assert(p);
 xz_xp_set_master_observer(p,xz_sync_link_runtime_master_observe);
 xz_xp_set_native_sync_clock(p,nc_copy);
 route_epoch=7;route_is_up=1;fake_route(route_epoch);sim_decks_attach(p,route_epoch);
 xz_deck_select_init(&ds);xz_deck_select_route_instance(&ds,1,route_epoch);publish_sides();
 sim_led_init((int)sch.rebuild);led_running=1;packet_variant[0]=packet_variant[1]=SIM_PACKET_ONE;
 nc_publish();
 unsigned next_action=0;uint32_t start=now32(),end=start+run_ms;trace_start=start;
 uint32_t next_consumer=start+sch.consumer_ms,next_reassert=start+policy.reassert_ms;
 /* A fixed per-tick draw keeps scheduling independent of which actions are enabled. */
 while(now32()<end&&!fail_class){
  sim_us+=1000;clock_ms=now32();
  uint32_t t=now32()-start;
  uint32_t j0=rnd(),j1=rnd();(void)j1;
  for(int d=0;d<2;d++)if(nat[d].loaded&&nat[d].playing)nat[d].beats+=nat[d].bpm/60000.;
  if(t%sch.usb_burst==0){consume(0,44.1*sch.usb_burst);consume(1,44.1*sch.usb_burst);}
  while(next_action<action_count&&actions[next_action].at_ms<=t){
   if(action_enabled[next_action])apply(&actions[next_action]);
   next_action++;
  }
  for(unsigned i=0;i<release_count;){
   if(releases[i].at<=now32()){struct pending_release r=releases[i];releases[i]=releases[--release_count];key(r.side,r.code,2);}
   else i++;
  }
  if(policy.reassert_ms&&now32()>=next_reassert){
   int32_t m=engine_master();if(m>=0&&m<2)stock_set_master(m);
   next_reassert=now32()+policy.reassert_ms;
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
  if(t%10==0)check_world();
  if(trace_on&&trace_every&&t>=trace_from&&t%trace_every==0)trace("tick",-1,0);
 }
 int result=fail_class;
 if(result&&verbose){
  struct xz_extra_decks v;xz_xp_read(p,&v);struct xz_master_observation o;(void)xz_sync_link_runtime_master_observe(&o);
  printf("FAIL %s at=%u %s\n",fail_name[result],fail_at-start,fail_detail);
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
 for(int i=(int)action_count-1;i>=6;i--){
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
   int r=run_one(1);trace_on=0;failed|=r!=0;
   printf("%s %s: %s\n",r?"FAIL":"PASS",scenarios[i].name,scenarios[i].story);
  }
  scenario_active=NULL;return failed;
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
