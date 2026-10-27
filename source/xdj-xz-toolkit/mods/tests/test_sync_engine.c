/* Real worker and completed PCM sink; native source and authority are fixtures. */
#define main retained_player_main
#include "test_extra_player.c"
#undef main
#include "sync_master.h"
static struct xz_master_observation engine_authority;
static unsigned authority_calls,revision_race;
static int authority_churn;
static int engine_observe(struct xz_master_observation *out){
 authority_calls++;
 if(authority_churn||authority_calls==revision_race)engine_authority.revision++;
 *out=engine_authority;out->observed_ms=clock_ms;return 1;
}
static int engine_resolve(int source,uint32_t id,struct xz_extra_track *out){
 int rc=sync_resolve(source,id==3?1:id,out);if(id==3){out->track_id=id;out->anlz_dir[0]=0;}return rc;
}
static void engine_begin(int q,int id){
 clock_ms=1000;wire_horizon=882;position_interval=position_noise=position_blocked=threaded_clock=hold_flush=0;
 authority_calls=revision_race=0;authority_churn=0;
 struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,engine_resolve,master_bpm,NULL,NULL,0,q};
 p=xz_xp_create(&env);assert(p);fake_route(91001);step();load(0,(uint32_t)id);
 engine_authority=(struct xz_master_observation){.deck=0,.revision=1,.operator_epoch=1};
 xz_xp_set_master_observer(p,engine_observe);xz_xp_set_native_sync_clock(p,copy_native);
 cached_native=(struct xz_sync_native_snapshot){.available=1,.native_deck=0,.master_id=0,.source_revision=7,
  .track_identity=123,.captured_ms=clock_ms,.bpm=126,.nominal_bpm=120,.speed=1.05f,.playing=1};
}
static void engine_end(void){xz_xp_destroy(p);p=NULL;}
static void immediate_tempo(void){
 for(int q=0;q<2;q++)for(int playing=0;playing<2;playing++)for(int follower=0;follower<2;follower++)
 for(int before=0;before<2;before++){
  engine_begin(q,1);cached_native.playing=playing;cached_native.captured_ms=clock_ms-(unsigned)before;
  if(follower)press(0,XZ_EXTRA_IN_PLAY);
  unsigned generation=debug(0).generation;enum xz_xp_state state=debug(0).state;
  press(0,XZ_EXTRA_IN_SYNC);
  assert(view.deck[0].tempo_x100==500&&view.deck[0].sync.tempo_matched);
  assert(view.deck[0].sync.source==XZ_EXTRA_SYNC_SOURCE_LIVE&&view.deck[0].sync.master_paused==!playing);
  assert(view.deck[0].sync.requested&&!view.deck[0].sync.locked&&!view.deck[0].sync.quantum_unknown);
  assert(debug(0).state==state&&debug(0).generation==generation);
  engine_end();
 }
 puts("PASS first worker pass: before/equal press, paused/playing master and follower, Q on/off, no phase transport mutation");
}
static void retained_tempo(void){
 engine_begin(1,1);press(0,XZ_EXTRA_IN_PLAY);press(0,XZ_EXTRA_IN_SYNC);advance(151);
 assert(view.deck[0].sync.tempo_matched&&!view.deck[0].sync.locked&&view.deck[0].tempo_x100==500);
 assert(view.deck[0].sync.reason==XZ_EXTRA_SYNC_NATIVE_PHASE_UNAVAILABLE);
 cached_native.track_identity++;cached_native.source_revision++;cached_native.bpm=132;step();
 assert(view.deck[0].tempo_x100==500&&!view.deck[0].sync.tempo_matched);
 assert(view.deck[0].sync.source==XZ_EXTRA_SYNC_SOURCE_CLOCK_LOST);
 cached_native.captured_ms=clock_ms;step();assert(view.deck[0].tempo_x100==1000&&view.deck[0].sync.tempo_matched);
 engine_authority.deck=XZ_MASTER_UNKNOWN;engine_authority.revision++;step();
 assert(view.deck[0].tempo_x100==1000&&!view.deck[0].sync.tempo_matched&&view.deck[0].sync.source==XZ_EXTRA_SYNC_SOURCE_CLOCK_LOST);
 engine_authority.deck=XZ_MASTER_NONE;engine_authority.revision++;step();
 assert(view.deck[0].sync.source==XZ_EXTRA_SYNC_SOURCE_NONE&&view.deck[0].tempo_x100==1000);
 engine_authority.deck=XZ_MASTER_EXTERNAL;engine_authority.revision++;step();
 assert(view.deck[0].sync.reason==XZ_EXTRA_SYNC_NATIVE_MASTER_UNSUPPORTED&&!view.deck[0].sync.tempo_matched&&!view.deck[0].master);
 engine_end();
 for(unsigned age=150;age<=151;age++){
  engine_begin(0,1);cached_native.captured_ms=clock_ms-age;press(0,XZ_EXTRA_IN_SYNC);
  assert(view.deck[0].sync.tempo_matched==(age==150));assert(view.deck[0].tempo_x100==(age==150?500:0));engine_end();
 }
 puts("PASS stale phase retains same-source tempo; stale first sample/new source and unknown/external authority cannot command it");
}
static void manual_and_anchor(void){
 for(int q=0;q<2;q++){
  engine_begin(q,1);cached_native.manual=1;press(0,XZ_EXTRA_IN_SYNC);
  assert(view.deck[0].tempo_x100==500&&view.deck[0].sync.tempo_matched&&view.deck[0].sync.reason==XZ_EXTRA_SYNC_MANUAL);
  assert(debug(0).state==XZ_XP_CUED&&!view.deck[0].sync.locked);
  cached_native.manual=0;cached_native.bpm=132;cached_native.captured_ms=clock_ms;step();
  assert(view.deck[0].tempo_x100==1000&&view.deck[0].sync.requested);
  press(0,XZ_EXTRA_IN_PLAY);position_blocked=1;cached_native.bpm=120;step();
  assert(view.deck[0].tempo_x100==0&&view.deck[0].sync.tempo_matched&&!view.deck[0].sync.locked);
  position_blocked=0;engine_end();
 }
 puts("PASS manual steady tempo and missing output anchors keep first-pass BPM; transport is preserved");
}
static void recopy_and_range(void){
 for(unsigned at=1;at<=12;at++){
  engine_begin(0,1);press(0,XZ_EXTRA_IN_SYNC);cached_native.bpm=132;
  authority_calls=0;revision_race=at;step();
  assert(view.deck[0].tempo_x100==1000&&view.deck[0].sync.reason!=XZ_EXTRA_SYNC_OUT_OF_RANGE);
  engine_end();
 }
 engine_begin(1,1);press(0,XZ_EXTRA_IN_SYNC);cached_native.bpm=132;authority_churn=1;step();
 assert(view.deck[0].tempo_x100==500&&!view.deck[0].sync.tempo_matched&&view.deck[0].sync.reason!=XZ_EXTRA_SYNC_OUT_OF_RANGE);
 authority_churn=0;step();assert(view.deck[0].tempo_x100==1000);
 cached_native.bpm=300;step();assert(view.deck[0].sync.reason==XZ_EXTRA_SYNC_OUT_OF_RANGE&&!view.deck[0].sync.tempo_matched&&view.deck[0].tempo_x100==1000);
 cached_native.bpm=126;xz_xp_set_quantize_grid(p,0,0);step();
 assert(view.deck[0].sync.quantum_unknown&&view.deck[0].sync.tempo_matched&&view.deck[0].sync.reason==XZ_EXTRA_SYNC_NO_GRID);
 engine_end();
 puts("PASS bounded authority recopy, retained rate on churn, range refusal and unknown quantum facts");
}
static void phase_limits_keep_pcm_tempo(void){
 for(int loop=0;loop<2;loop++){
  engine_begin(1,loop?1:3);
  if(loop){
   press(0,XZ_EXTRA_IN_QUANTIZE);seek(0,100);press(0,XZ_EXTRA_IN_LOOP_IN);
   seek(0,350);press(0,XZ_EXTRA_IN_LOOP_OUT);press(0,XZ_EXTRA_IN_QUANTIZE);
   assert(debug(0).loop_active);
  }
  sim_native=(__typeof__(sim_native)){.active=1,.native_deck=0,.playing=1,.source_revision=7,
   .track_identity=123,.bpm=126,.anchor_phase=.25,.anchor_us=(uint64_t)clock_ms*1000u};
  xz_xp_set_native_sync_clock(p,copy_sim_native);
  if(!view.deck[0].playing)press(0,XZ_EXTRA_IN_PLAY);
  press(0,XZ_EXTRA_IN_SYNC);advance(200);
  unsigned generation=debug(0).generation,flushes_before=f.flushes[0];
  sim_native_set_bpm(132);step();
  assert(view.deck[0].tempo_x100==1000&&view.deck[0].sync.tempo_matched&&!view.deck[0].sync.locked);
  assert(view.deck[0].sync.reason==(loop?XZ_EXTRA_SYNC_LOOP_UNALIGNED:XZ_EXTRA_SYNC_NO_GRID));
  advance(200);
  assert(fabs(view.deck[0].timeline.completed_rate-1.1)<.00001);
  assert(debug(0).generation==generation&&f.flushes[0]==flushes_before);
  if(loop)assert(debug(0).loop_active&&completed_tag(0)>=debug(0).loop_in&&completed_tag(0)<debug(0).loop_out);
  engine_end();
 }
 engine_begin(0,1);load(1,2);press(1,XZ_EXTRA_IN_PLAY);advance(50);press(1,XZ_EXTRA_IN_MASTER);
 const struct xz_extra_timeline *t=&view.deck[1].timeline;
 engine_authority=(struct xz_master_observation){.deck=3,.revision=2,.operator_epoch=1,
  .request_epoch=view.master_intent.epoch,.lease_generation=2,.route_epoch=t->route_epoch,
  .load_revision=t->load_revision,.transport_generation=t->transport_generation};
 step();tempo(1,5000);press(0,XZ_EXTRA_IN_PLAY);position_blocked=1;press(0,XZ_EXTRA_IN_SYNC);
 assert(view.deck[0].tempo_x100==500&&view.deck[0].sync.tempo_matched&&!view.deck[0].sync.locked);
 tempo(1,8000);step();assert(view.deck[0].tempo_x100==800&&view.deck[0].sync.tempo_matched);
 position_blocked=0;engine_end();
 puts("PASS missing grid and fractional loop update completed PCM without seeks; software tempo follows without output anchors");
}
int main(int argc,char **argv){
 assert(argc==2);assert(!pthread_mutex_init(&f.lock,NULL));
 snprintf(fixtures,sizeof fixtures,"%s",argv[1]);snprintf(anlz_dir,sizeof anlz_dir,"%s/anlz",argv[1]);build_analysis();
 immediate_tempo();retained_tempo();manual_and_anchor();recopy_and_range();phase_limits_keep_pcm_tempo();test_native_sync_cache();
 pthread_mutex_destroy(&f.lock);puts("PASS S1-S9 engine regressions");return 0;
}
