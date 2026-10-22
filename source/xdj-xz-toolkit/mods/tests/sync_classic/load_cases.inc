extern void review_command_result(int),review_load_generation(int,unsigned,int),review_load_id(int,unsigned),review_reset_fence(void);
extern int review_command_calls(void);
static void review_start(unsigned side,int modern){
 init_case(side,modern);review_reset_fence();tap(side);tick(1,XZ_THEME_FLAGSHIP,2500,modern,1);
 CHECK(view.deck[side].sync.locked);
}
static void review_refusal(void){
 for(unsigned side=0;side<2;side++)for(int modern=0;modern<2;modern++)for(int stale=0;stale<2;stale++){
  review_start(side,modern);struct xz_sync_native_snapshot c;CHECK(xz_extra_native_sync_clock(&c));unsigned revision=c.source_revision;
  review_command_result(stale?0:-2);review_load_generation(0,stale?41:42,stale?3:2);xz_shell_runtime_owner_tick();
  CHECK(!xz_extra_native_sync_clock(&c));CHECK(review_command_calls()==!stale);
  tick(1,XZ_THEME_FLAGSHIP,80,modern,1);CHECK(xz_extra_native_sync_clock(&c));
  printf("review_refusal side=%u modern=%d stale=%d old_revision=%u new_revision=%u locked=%d reason=%d\n",side,modern,stale,revision,c.source_revision,view.deck[side].sync.locked,view.deck[side].sync.reason);
  CHECK(c.source_revision==revision);CHECK(view.deck[side].sync.locked);finish();
 }
}
static void review_fence(void){
 for(unsigned side=0;side<2;side++)for(int modern=0;modern<2;modern++)for(int batch=0;batch<2;batch++){
  review_start(side,modern);struct xz_sync_native_snapshot c;
  review_command_result(0);review_load_generation(0,42,2);
  if(batch)review_load_generation(0,41,3);
  xz_shell_runtime_owner_tick();CHECK(!xz_extra_native_sync_clock(&c));
  if(!batch){tick(1,XZ_THEME_FLAGSHIP,40,modern,1);CHECK(!xz_extra_native_sync_clock(&c));review_load_generation(0,41,3);xz_shell_runtime_owner_tick();}
  tick(1,XZ_THEME_FLAGSHIP,80,modern,1);int available=xz_extra_native_sync_clock(&c);
  printf("review_fence side=%u modern=%d batch=%d available=%d revision=%u reason=%d\n",side,modern,batch,available,c.source_revision,c.reason);
  CHECK(!available);finish();
 }
}
static void review_master(void){
 review_start(0,0);struct xz_sync_native_snapshot c;CHECK(xz_extra_native_sync_clock(&c));unsigned revision=c.source_revision;
 fixture_native_external(1);review_load_id(0,22);clock_ms+=40;fixture_native_update();xz_shell_runtime_owner_tick();
 CHECK(xz_extra_native_sync_clock(&c)&&c.native_deck==1);
 review_load_id(0,10);fixture_native_external(0);clock_ms+=40;fixture_native_update();xz_shell_runtime_owner_tick();
 CHECK(xz_extra_native_sync_clock(&c)&&c.native_deck==0);
 step();printf("review_master observed_nonmaster_ABA old_revision=%u new_revision=%u locked=%d reason=%d\n",revision,c.source_revision,view.deck[0].sync.locked,view.deck[0].sync.reason);
 CHECK(c.source_revision>revision);CHECK(view.deck[0].sync.reason==XZ_EXTRA_SYNC_DISCONTINUITY);finish();
}
static void review_prefence(void){
 for(unsigned side=0;side<2;side++)for(int modern=0;modern<2;modern++){
  review_start(side,modern);struct xz_sync_native_snapshot c;
  fixture_native_token(1);review_command_result(0);review_load_generation(0,42,2);
  clock_ms+=40;fixture_native_update();xz_shell_runtime_owner_tick();
  int available=xz_extra_native_sync_clock(&c);
  printf("review_prefence side=%u modern=%d command_calls=%d available=%d revision=%u\n",side,modern,review_command_calls(),available,c.source_revision);
  CHECK(review_command_calls()==1);CHECK(!available);finish();
 }
}
static void review_drawing(void){
 init_case(0,1);review_reset_fence();CHECK(fixture_frame(1,XZ_THEME_FLAGSHIP)==-1);CHECK(xz_shell_runtime_owns());int refreshes=fixture_refresh_count();
 CHECK(xz_shell_runtime_follow_interface(0)==0);CHECK(!xz_shell_runtime_owns());
 CHECK(fixture_frame(1,XZ_THEME_FLAGSHIP)==0);CHECK(!xz_shell_runtime_owns());CHECK(fixture_refresh_count()==refreshes);
 CHECK(xz_shell_runtime_follow_interface(1)==0);clock_ms+=120;CHECK(fixture_frame(0,XZ_THEME_FLAGSHIP)==0);CHECK(!xz_shell_runtime_owns());CHECK(fixture_refresh_count()==refreshes);
 clock_ms+=120;CHECK(fixture_frame(1,XZ_THEME_FLAGSHIP)==-1);CHECK(xz_shell_runtime_owns());finish();
}

extern void review_execute_hook(void (*)(void)),review_code_guard(int);
extern void review_load_known(int,int),review_observation_limit(void);
extern void review_guard_token_change(void);
extern int review_command_rc(void);
static void owner_observe(void){clock_ms+=40;fixture_native_update();xz_shell_runtime_owner_tick();}
static void observed_load_aba(void){review_load_id(0,22);owner_observe();review_load_id(0,10);owner_observe();}
static void observed_loading_complete(void){fixture_load_state(7);owner_observe();fixture_load_state(0);owner_observe();}
static void observed_token_aba(void){fixture_native_token(1);owner_observe();fixture_native_token(0);owner_observe();}
static void observed_media_aba(void){fixture_media_generation(43);owner_observe();fixture_media_generation(42);owner_observe();}
static void review_observation(void){
 void (*hooks[])(void)={observed_load_aba,observed_loading_complete,observed_token_aba,observed_media_aba};
 for(unsigned mode=0;mode<4;mode++)for(int accepted=0;accepted<2;accepted++){
  review_start(0,0);struct xz_sync_native_snapshot c;CHECK(xz_extra_native_sync_clock(&c));unsigned previous=c.source_revision;
  review_command_result(accepted?0:-2);review_execute_hook(hooks[mode]);review_load_generation(0,42,2);xz_shell_runtime_owner_tick();
  review_execute_hook(NULL);CHECK(review_command_calls()==1);CHECK(!xz_extra_native_sync_clock(&c));
  tick(0,0,80,0,1);CHECK(xz_extra_native_sync_clock(&c));CHECK(c.source_revision>previous);
  CHECK(view.deck[0].sync.reason==XZ_EXTRA_SYNC_DISCONTINUITY);
  printf("observation kind=%u accepted=%d old=%u new=%u reason=%d\n",mode,accepted,previous,c.source_revision,view.deck[0].sync.reason);finish();
 }
}
static void review_guards(void){
 for(int mode=0;mode<3;mode++){
  review_start(0,0);struct xz_sync_native_snapshot c;CHECK(xz_extra_native_sync_clock(&c));unsigned revision=c.source_revision;
  if(mode==0)review_code_guard(0);else if(mode==1)fixture_load_state(7);else review_load_known(0,0);
  review_command_result(0);review_load_generation(0,42,2);xz_shell_runtime_owner_tick();
  CHECK(review_command_calls()==0);CHECK(review_command_rc()==(mode==0?-1:mode==1?-4:-3));CHECK(!xz_extra_native_sync_clock(&c));
  review_code_guard(1);fixture_load_state(0);review_load_known(0,1);tick(0,0,80,0,1);
  CHECK(xz_extra_native_sync_clock(&c));
  if(mode==0){CHECK(c.source_revision==revision);CHECK(view.deck[0].sync.locked);}
  else{CHECK(c.source_revision>revision);CHECK(view.deck[0].sync.reason==XZ_EXTRA_SYNC_DISCONTINUITY);}
  finish();
 }
 review_start(0,0);struct xz_sync_native_snapshot c;review_observation_limit();owner_observe();CHECK(!xz_extra_native_sync_clock(&c));
 fixture_load_id(22);owner_observe();fixture_load_id(10);owner_observe();CHECK(!xz_extra_native_sync_clock(&c));finish();
}
static void review_completion(void){
 for(unsigned mode=0;mode<3;mode++)for(int modern=0;modern<2;modern++){
  review_start(0,modern);struct xz_sync_native_snapshot c;CHECK(xz_extra_native_sync_clock(&c));unsigned previous=c.source_revision;
  review_command_result(0);review_load_generation(0,42,2);xz_shell_runtime_owner_tick();tick(1,XZ_THEME_FLAGSHIP,80,modern,1);CHECK(!xz_extra_native_sync_clock(&c));
  review_command_result(-2);review_execute_hook(mode==0?observed_load_aba:mode==1?observed_loading_complete:observed_token_aba);
  review_load_generation(0,42,2);xz_shell_runtime_owner_tick();review_execute_hook(NULL);
  CHECK(review_command_calls()==1);tick(1,XZ_THEME_FLAGSHIP,80,modern,1);
  CHECK(xz_extra_native_sync_clock(&c)&&c.source_revision>previous);CHECK(view.deck[0].sync.reason==XZ_EXTRA_SYNC_DISCONTINUITY);finish();
 }
}
static void review_boundary(void){
 for(unsigned side=0;side<2;side++)for(int modern=0;modern<2;modern++)for(int during_guard=0;during_guard<2;during_guard++){
  review_start(side,modern);struct xz_sync_native_snapshot c;CHECK(xz_extra_native_sync_clock(&c));unsigned previous=c.source_revision;
  if(during_guard)review_guard_token_change();else fixture_native_token(1);
  review_command_result(0);review_load_generation(0,42,2);xz_shell_runtime_owner_tick();
  tick(1,XZ_THEME_FLAGSHIP,200,modern,1);CHECK(!xz_extra_native_sync_clock(&c));
  fixture_native_token(0);tick(1,XZ_THEME_FLAGSHIP,80,modern,1);CHECK(xz_extra_native_sync_clock(&c)&&c.source_revision>previous);
  CHECK(view.deck[side].sync.reason==XZ_EXTRA_SYNC_DISCONTINUITY);finish();
 }
 // An admitted command must not turn a still-settled load-state change into
 // a track incarnation. A real refusal path releases only its own admission.
 for(int guard=0;guard<2;guard++){
  review_start(0,0);struct xz_sync_native_snapshot c;CHECK(xz_extra_native_sync_clock(&c));unsigned revision=c.source_revision,at=c.captured_ms;
  fixture_load_state(1);owner_observe();CHECK(xz_extra_native_sync_clock(&c)&&c.source_revision==revision);
  review_command_result(-2);review_code_guard(!guard);review_load_generation(0,42,2);xz_shell_runtime_owner_tick();
  CHECK(!xz_extra_native_sync_clock(&c));CHECK(review_command_calls()==!guard);review_code_guard(1);
  clock_ms+=151;CHECK(!xz_extra_native_sync_clock(&c));fixture_native_update();xz_shell_runtime_owner_tick();
  CHECK(xz_extra_native_sync_clock(&c)&&c.captured_ms>at&&c.source_revision==revision);
  CHECK(c.captured_ms==clock_ms);finish();
 }
}
