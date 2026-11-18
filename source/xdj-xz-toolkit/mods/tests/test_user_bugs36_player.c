#define main existing_player_tests
#include "test_extra_player.c"
#undef main
#include "deck_select.h"

static struct xz_xp_env ub_env(void) {
 return (struct xz_xp_env){{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,resolve,master_bpm,NULL,order_resolve,0,0};
}
static void ub_load(void) {
 for(int side=0;side<2;side++)for(int ac=0;ac<2;ac++)for(int playing=0;playing<2;playing++)for(int other=0;other<2;other++){
  struct xz_xp_env env=ub_env();p=xz_xp_create(&env);assert(p);fake_route(600);step();
  load(side,2);load(1-side,2);seek(side,1300);seek(1-side,900);
  if(!ac){struct xz_extra_deck_event e=event(side,XZ_EXTRA_IN_AUTO_CUE,1);e.shift=1;assert(input(e));step();}
  if(playing)press(side,XZ_EXTRA_IN_PLAY);
  if(other)press(1-side,XZ_EXTRA_IN_PLAY);
  load(side,1);
  assert(view.deck[side].playing==(playing&&!ac));
  assert(view.deck[side].elapsed_ms==(ac?500:0));
  assert(view.deck[1-side].elapsed_ms==900);
  xz_xp_destroy(p);p=NULL;
 }
 puts("PASS report3 software: both sides, AUTO CUE on/off, target/other playing matrix, new-track position");
}
static void ub_tracks(int with_order) {
 for(int side=0;side<2;side++){
  struct xz_xp_env env=ub_env();p=xz_xp_create(&env);assert(p);fake_route(601);step();
  uint32_t ids[]={2,1,2};uint8_t kinds[]={1,1,1};
  struct xz_extra_order *order=with_order?xz_extra_order_create(1,42,ids,kinds,3,1):NULL;
  struct xz_extra_deck_event e=event(side,XZ_EXTRA_IN_LOAD,1);e.load.source=1;e.load.track_id=1;
  e.load.media_generation=42;e.load.route_epoch=601;e.load.order=order;e.load.order_index=1;
  assert(input(e));xz_extra_order_release(order);step();seek(side,1400);
  struct xz_deck_select ds;xz_deck_select_init(&ds);xz_deck_select_route(&ds,1);ds.side[side].target=XZ_DS_EXTRA;
  for(int n=0;n<2;n++){
   struct xz_ds_outcome out=xz_deck_select_global_key(&ds,(unsigned)side+1,0x4213,0,clock_ms);
   assert(out.verdict==XZ_DS_DELIVER);assert(input(out.event));step();
   xz_deck_select_global_key(&ds,(unsigned)side+1,0x4213,2,clock_ms);
   if(!n)assert(view.deck[side].elapsed_ms==500);
   clock_ms+=100;
  }
  printf("report4 side=%d order=%d double-back=%s\n",side+3,with_order,view.deck[side].title);fflush(stdout);
  assert(!strcmp(view.deck[side].title,"Track B"));
  struct xz_ds_outcome out=xz_deck_select_global_key(&ds,(unsigned)side+1,0x4212,0,clock_ms);
  assert(out.verdict==XZ_DS_DELIVER);assert(input(out.event));step();
  assert(!strcmp(view.deck[side].title,"Track A")&&view.deck[side].elapsed_ms==500);
  xz_xp_destroy(p);p=NULL;
 }
 puts("PASS report4 physical TRACK SEARCH back/double-back/next");
}
static void ub_track_auto_cue(void) {
 for(int side=0;side<2;side++){
  struct xz_xp_env env=ub_env();p=xz_xp_create(&env);assert(p);fake_route(602);step();
  uint32_t ids[]={2,1};uint8_t kinds[]={1,1};struct xz_extra_order *order=xz_extra_order_create(1,42,ids,kinds,2,1);
  struct xz_extra_deck_event e=event(side,XZ_EXTRA_IN_LOAD,1);e.load.source=1;e.load.track_id=2;
  e.load.media_generation=42;e.load.route_epoch=602;e.load.order=order;
  assert(input(e));xz_extra_order_release(order);step();press(side,XZ_EXTRA_IN_PLAY);press(side,XZ_EXTRA_IN_TRACK_NEXT);
  printf("report4 AUTO CUE side=%d playing=%d at=%d\n",side+3,view.deck[side].playing,view.deck[side].elapsed_ms);fflush(stdout);
  assert(!view.deck[side].playing&&view.deck[side].elapsed_ms==500);
  /* A double-back load uses the same AUTO CUE rule. */
  press(side,XZ_EXTRA_IN_PLAY);press(side,XZ_EXTRA_IN_TRACK_PREV);clock_ms+=100;
  press(side,XZ_EXTRA_IN_TRACK_PREV);
  assert(!strcmp(view.deck[side].title,"Track B")&&!view.deck[side].playing&&view.deck[side].elapsed_ms==0);
  xz_xp_destroy(p);p=NULL;
 }
}
static void ub_jump(int no_bpm) {
 for(int side=0;side<2;side++)for(int quant=0;quant<2;quant++){
  struct xz_xp_env env=ub_env();p=xz_xp_create(&env);assert(p);fake_route(603);step();load(side,no_bpm?2:1);
  if(!quant)press(side,XZ_EXTRA_IN_QUANTIZE);
  mode(side,XZ_EXTRA_PAD_BEAT_JUMP,0);seek(side,200);press(side,XZ_EXTRA_IN_PLAY);
  for(int n=0;n<3;n++){
   pad_key(side,1,1);pad_key(side,1,0);
   printf("report6 side=%d quant=%d no_bpm=%d press=%d at=%d playing=%d\n",side+3,quant,no_bpm,n+1,view.deck[side].elapsed_ms,view.deck[side].playing);fflush(stdout);
   assert(view.deck[side].elapsed_ms==200+500*(n+1)&&view.deck[side].playing);
  }
  xz_xp_destroy(p);p=NULL;
 }
}
int main(int argc,char **argv) {
 assert(argc==3);assert(!pthread_mutex_init(&f.lock,NULL));
 snprintf(fixtures,sizeof fixtures,"%s",argv[1]);snprintf(anlz_dir,sizeof anlz_dir,"%s/anlz",argv[1]);build_analysis();
 if(!strcmp(argv[2],"load"))ub_load();
 else if(!strcmp(argv[2],"tracks"))ub_tracks(1);
 else if(!strcmp(argv[2],"classic-tracks"))ub_tracks(0);
 else if(!strcmp(argv[2],"track-auto-cue"))ub_track_auto_cue();
 else if(!strcmp(argv[2],"jump"))ub_jump(0);
 else if(!strcmp(argv[2],"jump-no-bpm"))ub_jump(1);
 else assert(0);
 return 0;
}
