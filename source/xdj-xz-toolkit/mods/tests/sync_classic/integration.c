#define main retained_player_main
#define usb retained_player_usb
#include "test_extra_player.c"
#undef usb
#undef main
#include "deck_select.h"
#include "shell_runtime.h"
#include "runtime.h"
#include "ui/themes.h"
extern void fixture_runtime_reset(void),fixture_route(int);
extern int fixture_global_input(const void *),fixture_toggle(int),fixture_delivered(void);
extern void fixture_shell_reset(void),fixture_native_init(void),fixture_native_update(void);
extern unsigned fixture_native_polls(void),fixture_wave_revision(void);
extern int fixture_waves(void),fixture_refresh_count(void),fixture_frame(int,int),fixture_stops(void);
extern void fixture_load_state(unsigned),fixture_load_id(unsigned),fixture_media_generation(unsigned),fixture_media_reuse(void),fixture_torn_load(void),fixture_handoff(void);
extern void fixture_native_bad_guard(int),fixture_native_change_master(int),fixture_native_change_source(int),fixture_native_manual(int);
extern void fixture_native_token(int),fixture_native_external(int),fixture_media_publication(unsigned),fixture_known_load(int),fixture_revision_limit(void);
extern void fixture_native_read_window(unsigned);
void test_player_init(void){}
int test_player_input(const struct xz_extra_deck_event *e){return xz_xp_input(p,e);}
uint32_t fixture_now_ms(void){return clock_ms;}
int fixture_snapshot(struct xz_extra_decks *o){xz_xp_read(p,o);o->route_up=!!fake_link(NULL);return o->route_up;}
void fixture_sides(int l,int r){(void)l;(void)r;}
int fixture_native_master(void){return xz_xp_use_native_master(p);}
int xz_extra_decks_needs_native_sync(void){return xz_xp_needs_native_sync(p);}
uint32_t xz_extra_decks_revision(void){return 0;}
uint32_t xz_extra_decks_control_revision(void){return 0;}
static int fixture_stock_global,started;
static struct {int theme;} model;
static pthread_mutex_t ui_mutex=PTHREAD_MUTEX_INITIALIZER;
static void *key_player(int d){(void)d;assert(0);return NULL;}
int xz_runtime_deck_games_pending(void);
int xz_runtime_deck_early_key(uint16_t);
int xz_runtime_deck_global_input(void *,const void *);
int xz_runtime_deck_global_key(uint16_t,unsigned,unsigned);
static int shell_eligible_locked(void){assert(0);return 0;}
int xz_shell_runtime_extra_load_key(int a,int b,int c){(void)a;(void)b;(void)c;assert(0);return 0;}
static void shell_ui_requests_locked(void){assert(0);}
#include "dispatcher.inc"
static unsigned checks,failed;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#x);failed++;}}while(0)
static void dispatch(unsigned side,unsigned op){
 unsigned char packet[24]={0};uint16_t code=0x4313;memcpy(packet+8,&code,2);packet[10]=(unsigned char)(side+1);packet[11]=(unsigned char)op;menu_back_key_hook(NULL,packet);
}
static void tap(unsigned side){dispatch(side,0);dispatch(side,2);step();}
static void init_case(unsigned side,int modern){
 clock_ms=1000;wire_horizon=882;struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,sync_resolve,master_bpm,NULL,NULL,0,0};
 p=xz_xp_create(&env);assert(p);xz_xp_set_native_sync_clock(p,xz_extra_native_sync_clock);fake_route(997);step();fixture_runtime_reset();
 CHECK(fixture_toggle((int)side)==1);sync_loop_track((int)side,1,350);
 fixture_shell_reset();fixture_native_init();CHECK(xz_shell_runtime_follow_interface(modern)==0);
 memset(&sim_native,0,sizeof sim_native);sim_native.active=sim_native.playing=1;
 sim_native.bpm=126.;sim_native.anchor_us=(uint64_t)clock_ms*1000u;sim_native.anchor_phase=2.25;
}
static void tick(int eligible,int theme,unsigned n,int frames,int owner){
 for(unsigned t=0;t<n;t+=10){clock_ms+=10;fixture_native_update();if(owner)xz_shell_runtime_owner_tick();if(frames)fixture_frame(eligible,theme);consume(0,441);consume(1,441);step();}
}
static void finish(void){xz_xp_destroy(p);p=NULL;}
static void positive(void){
 for(unsigned side=0;side<2;side++)for(int scenario=0;scenario<14;scenario++){
  int master=scenario/7,mode=scenario%7;
  int modern=mode!=0&&mode!=4;init_case(side,modern);fixture_native_external(master);
  int eligible=mode==1||mode==3,theme=mode==0||mode==4?0:XZ_THEME_FLAGSHIP;
  if(mode==3){fixture_frame(1,XZ_THEME_FLAGSHIP);tick(1,XZ_THEME_FLAGSHIP,100,1,1);CHECK(xz_shell_runtime_owns());CHECK(xz_shell_runtime_follow_interface(0)==0);CHECK(!xz_shell_runtime_owns());fixture_frame(1,XZ_THEME_FLAGSHIP);CHECK(!xz_shell_runtime_owns());eligible=0;theme=0;}
  if(mode==5){fixture_frame(1,XZ_THEME_FLAGSHIP);fixture_handoff();eligible=1;}
  unsigned polls_before=fixture_native_polls();
  dispatch(side,0);step();CHECK(!view.deck[side].sync.requested);CHECK(fixture_delivered()==0);
  dispatch(side,2);step();CHECK(view.deck[side].sync.requested&&fixture_delivered()==1);CHECK(fake_link(NULL)==997);
  if(mode==6)CHECK(fixture_toggle((int)side)==1);
  tick(eligible,theme,2500,mode!=4,1);
  struct xz_sync_native_snapshot c;int copied=xz_extra_native_sync_clock(&c);
  double error=fabs(actual_native_sync_error((int)side));
  printf("case native=%d side=%u mode=%d route_ready=%d request=%d tempo=%d locked=%d reason=%d clock=%d revision=%u polls=%u waves=%d refreshes=%d stops=%d pcm_error_us=%.1f\n",master,side,mode,fake_link(NULL)==997,view.deck[side].sync.requested,view.deck[side].tempo_x100,view.deck[side].sync.locked,view.deck[side].sync.reason,copied,c.source_revision,fixture_native_polls()-polls_before,fixture_waves(),fixture_refresh_count(),fixture_stops(),error*1e6);
  CHECK(copied&&c.native_deck==master&&c.source_revision&&c.phase_available&&view.deck[side].tempo_x100==500);
  CHECK(view.deck[side].sync.locked&&error<=.001);
  CHECK(fixture_native_polls()>polls_before);CHECK(fixture_native_polls()-polls_before<=63u);
  if(mode!=1&&mode!=3&&mode!=5)CHECK(fixture_waves()==0);
  if(mode==6)CHECK(fixture_toggle((int)side)==1);
  tap(side);CHECK(!view.deck[side].sync.requested);tick(eligible,theme,40,mode!=4,1);CHECK(!xz_extra_native_sync_clock(&c));
  finish();
 }
}
static void negative(void){
 for(int mode=0;mode<11;mode++){
  init_case(0,1);fixture_frame(1,XZ_THEME_FLAGSHIP);tap(0);tick(1,XZ_THEME_FLAGSHIP,200,1,1);
  struct xz_sync_native_snapshot c;CHECK(xz_extra_native_sync_clock(&c));
  switch(mode){case 0:fixture_load_state(7);break;case 1:fixture_media_generation(0);break;case 2:fixture_native_bad_guard(1);break;case 3:fixture_native_change_master(1);break;case 4:fixture_native_change_source(1);break;case 5:fixture_torn_load();break;case 6:clock_ms=c.captured_ms+151;break;case 7:clock_ms=c.captured_ms-1;break;case 8:fixture_media_reuse();break;case 9:fixture_media_publication(43);break;case 10:fixture_native_external(2);break;}
  if(mode<6||mode>7)tick(0,0,40,1,1);
  CHECK(!xz_extra_native_sync_clock(&c));CHECK(!c.available&&!c.phase_available);
  printf("negative mode=%d available=%d reason=%d\n",mode,c.available,c.reason);finish();
 }
}
static void coherence(void){
 struct xz_sync_native_snapshot c;unsigned previous;
 init_case(0,0);tick(0,0,100,1,1);CHECK(!fixture_native_polls()&&!fixture_waves());
 tap(0);tick(0,0,80,1,1);CHECK(xz_extra_native_sync_clock(&c));previous=c.source_revision;
 CHECK(previous>0&&!fixture_waves()&&!fixture_wave_revision());
 tick(0,0,80,0,1);CHECK(xz_extra_native_sync_clock(&c)&&c.source_revision==previous);
 clock_ms=c.captured_ms+150;CHECK(xz_extra_native_sync_clock(&c));clock_ms++;CHECK(!xz_extra_native_sync_clock(&c));
 tick(0,0,40,0,1);CHECK(xz_extra_native_sync_clock(&c)&&c.source_revision==previous);
 fixture_load_state(7);tick(0,0,40,0,1);CHECK(!xz_extra_native_sync_clock(&c));
 fixture_load_state(0);tick(0,0,40,0,1);CHECK(xz_extra_native_sync_clock(&c)&&c.source_revision>previous);previous=c.source_revision;
 fixture_native_token(1);tick(0,0,40,0,1);CHECK(xz_extra_native_sync_clock(&c)&&c.source_revision>previous);previous=c.source_revision;
 fixture_native_token(0);tick(0,0,40,0,1);CHECK(xz_extra_native_sync_clock(&c)&&c.source_revision>previous);previous=c.source_revision;
 fixture_load_id(22);tick(0,0,40,0,1);CHECK(xz_extra_native_sync_clock(&c)&&c.source_revision>previous);previous=c.source_revision;
 fixture_load_id(10);tick(0,0,40,0,1);CHECK(xz_extra_native_sync_clock(&c)&&c.source_revision>previous);previous=c.source_revision;
 fixture_media_generation(43);fixture_media_reuse();tick(0,0,40,0,1);CHECK(xz_extra_native_sync_clock(&c)&&c.source_revision>previous);previous=c.source_revision;
 fixture_media_generation(44);tick(0,0,40,0,1);CHECK(xz_extra_native_sync_clock(&c)&&c.source_revision>previous);previous=c.source_revision;
 unsigned before_load_polls=fixture_native_polls();
 for(unsigned i=0;i<10;i++){fixture_known_load(1);clock_ms++;fixture_native_update();xz_shell_runtime_owner_tick();}
 CHECK(fixture_native_polls()-before_load_polls<=1u);
 fixture_known_load(1);tick(0,0,40,0,1);CHECK(xz_extra_native_sync_clock(&c)&&c.source_revision==previous);
 fixture_known_load(0);xz_shell_runtime_owner_tick();CHECK(!xz_extra_native_sync_clock(&c));
 tick(0,0,200,0,1);CHECK(!xz_extra_native_sync_clock(&c));
 fixture_load_state(9);tick(0,0,40,0,1);CHECK(!xz_extra_native_sync_clock(&c));
 fixture_load_state(0);tick(0,0,40,0,1);CHECK(xz_extra_native_sync_clock(&c)&&c.source_revision>previous);previous=c.source_revision;
 tap(0);tick(0,0,10,0,1);CHECK(!xz_extra_native_sync_clock(&c));
 unsigned polls=fixture_native_polls();tick(0,0,100,0,1);CHECK(fixture_native_polls()==polls);
 tap(0);tick(0,0,40,0,1);CHECK(xz_extra_native_sync_clock(&c)&&c.source_revision>previous);previous=c.source_revision;
 fixture_revision_limit();fixture_load_state(7);tick(0,0,40,0,1);fixture_load_state(0);tick(0,0,40,0,1);CHECK(!xz_extra_native_sync_clock(&c));
 printf("coherence revisions_advanced_from=%u no_waves=%d polls=%u\n",previous,fixture_waves(),fixture_native_polls());
 finish();
}
static void uncertainty(void){
 struct xz_sync_native_snapshot c;
 init_case(0,0);fixture_native_read_window(3000);tap(0);tick(0,0,200,0,1);
 CHECK(xz_extra_native_sync_clock(&c)&&c.available&&!c.phase_available&&c.uncertainty_us==1500);
 CHECK(view.deck[0].tempo_x100==500&&!view.deck[0].sync.locked&&view.deck[0].sync.reason==XZ_EXTRA_SYNC_NATIVE_PHASE_UNAVAILABLE);
 fixture_native_read_window(20001);tick(0,0,40,0,1);CHECK(!xz_extra_native_sync_clock(&c)&&c.reason==XZ_SYNC_NATIVE_TIME_INVALID);
 printf("uncertainty tempo_only_then_invalid=%d\n",!c.available);finish();
}
#include "load_cases.inc"
int main(int argc,char **argv){
 assert(argc==3);assert(!pthread_mutex_init(&f.lock,NULL));snprintf(fixtures,sizeof fixtures,"%s",argv[1]);snprintf(anlz_dir,sizeof anlz_dir,"%s/anlz",argv[1]);build_analysis();
 if(!strcmp(argv[2],"positive"))positive();else if(!strcmp(argv[2],"negative"))negative();else if(!strcmp(argv[2],"coherence"))coherence();else if(!strcmp(argv[2],"uncertainty"))uncertainty();else if(!strcmp(argv[2],"refusal"))review_refusal();else if(!strcmp(argv[2],"fence"))review_fence();else if(!strcmp(argv[2],"master"))review_master();else if(!strcmp(argv[2],"prefence"))review_prefence();else if(!strcmp(argv[2],"drawing"))review_drawing();else if(!strcmp(argv[2],"observation"))review_observation();else if(!strcmp(argv[2],"completion"))review_completion();else if(!strcmp(argv[2],"boundary"))review_boundary();else if(!strcmp(argv[2],"guards"))review_guards();else return 2;
 printf("RESULT checks=%u failures=%u\n",checks,failed);return failed?1:0;
}
