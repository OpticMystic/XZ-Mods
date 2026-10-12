/* Actual native-screen touch routing for all four visible layouts. */
static void stem_layout_runtime_check(void) {
 unsigned char self[32]={0};struct xz_ui_action out[XZ_UI_ACTIONS];
 started=audio_available=requested_stems=native_active=1;arcade_present=0;
 model.enabled=XZ_UI_STEM;model.fb_takeover=0;model.connection.connected=0;
 settings_running=0;settings_readonly=1;fixture_focus=-1;fixture_prepared=1;
 for(int d=0;d<2;d++){fixture_pc_source[d]=(struct xz_pc_stem_source){.slot=-1,.epoch=1};fixture_pc_valid[d]=1;}
 xz_ui_init(&ui);xz_ui_init(&inline_ui);xz_native_touch_init(&touch,&ui,&model,apply,NULL);stock_touch=stock;
 inline_contact=inline_wave_contact=inline_cancelled=0;inline_redraw_pending=0;
 refresh();
 for(int mode=1;mode<XZ_STEM_LAYOUT_COUNT;mode++){
  model.stems_overlay=mode;refresh();struct xz_wave_layout g;assert(xz_wave_layout_get(mode,&g));
  for(int deck=0;deck<2;deck++){
   struct xz_ui_action level={.kind=XZ_UI_LEVEL,.deck=deck,.index=2,.value=.5f};apply(NULL,&level,1);
   int x=160,y=18+(int)g.control_y[deck]+8;
   int before=stock_calls;contact(self,(unsigned)x,(unsigned)y,1);
   assert(inline_contact&&inline_contact_deck==deck&&stock_calls==before);
   unsigned redraws=fixture_pc_redraws;
   contact(self,(unsigned)(x+30),(unsigned)y,1);
   assert(model.deck[deck].levels[2]>.5f&&stock_calls==before);
   contact(self,(unsigned)(x-20),(unsigned)y,1);
   assert(model.deck[deck].levels[2]<.5f&&stock_calls==before);
   assert(fixture_pc_redraws==redraws+2);
   contact(self,(unsigned)(x-20),(unsigned)y,0);assert(!inline_contact&&stock_calls==before);
   /* Wave gestures keep their original deck when crossing a control/other lane. */
   int wave_y=18+(int)g.lane_y[deck]+10;
   contact(self,200,(unsigned)wave_y,1);
   assert(inline_wave_contact&&inline_wave_deck==deck);
   assert(last_stock.y==18+xz_wave_layout_source_row(&g,g.lane_y[deck]+10));
   contact(self,200,400,1);
   assert(last_stock.y==18+xz_wave_layout_source_row(&g,g.lane_y[deck]+g.lane_height-1));
   contact(self,200,400,0);assert(!inline_wave_contact);
  }
  /* Changing layout during a drag consumes its release without muting a stem. */
  int y=18+(int)g.control_y[0]+8;unsigned mute=model.deck[0].muted;
  contact(self,160,(unsigned)y,1);struct xz_ui_action cycle={.kind=XZ_UI_STEMS_OVERLAY};apply(NULL,&cycle,1);
  assert(inline_cancelled&&!inline_ui.down);
  contact(self,300,260,1);contact(self,300,260,0);
  assert(!inline_contact&&model.deck[0].muted==mute);
  xz_ui_cancel(&inline_ui,out);
 }
 model.stems_overlay=XZ_STEM_LAYOUT_HIDDEN;refresh();
 int before=stock_calls;contact(self,160,130,1);contact(self,160,130,0);
 assert(stock_calls==before+2&&!inline_contact&&!inline_wave_contact);
 /* PC-only refresh and profile restoration must keep the mode, not a boolean. */
 for(int d=0;d<2;d++)fixture_pc_source[d]=(struct xz_pc_stem_source){.pc=1,.rekordbox=1,.linked=1,.loaded=1,.slot=d,.epoch=2};
 for(int mode=0;mode<XZ_STEM_LAYOUT_COUNT;mode++){
  model.stems_overlay=mode;pc_refresh_locked();assert(inline_enabled()==mode);
  struct xz_settings profile;xz_settings_default(&profile);profile.stems_overlay=mode;
  profile_apply_locked(&profile);assert(inline_enabled()==mode);
 }
 for(int d=0;d<2;d++)fixture_pc_source[d]=(struct xz_pc_stem_source){.slot=-1,.epoch=3};
 /* SET UP replaces BYPASS only while the stem engine is missing (stems requested at boot, engine absent). */
 struct xz_wave_layout below;assert(xz_wave_layout_get(XZ_STEM_LAYOUT_BELOW,&below));
 int setup_y=18+(int)below.control_y[0]+(int)below.control_height/2;
 model.stems_overlay=XZ_STEM_LAYOUT_BELOW;audio_available=0;model.enabled&=~(uint32_t)XZ_UI_STEM;fixture_prepared=0;refresh();
 contact(self,640,(unsigned)setup_y,1);contact(self,640,(unsigned)setup_y,0);
 assert(touch.visible&&ui.page==XZ_UI_LOCAL_STEM_SETUP&&!ui.stem_setup_step&&ui.deck==0);
 assert(!inline_enabled());
 ram_tap(XZ_UI_LOCAL_STEM_SETUP_STEP,0);assert(ui.stem_setup_step==0&&strstr(ui.notice,"ENGINE MISSING"));
 audio_available=1;
 ram_tap(XZ_UI_LOCAL_STEM_SETUP_STEP,0);assert(ui.stem_setup_step==1);
 ram_tap(XZ_UI_LOCAL_STEM_SETUP_STEP,1);assert(ui.stem_setup_step==2&&(model.enabled&XZ_UI_STEM));
 /* An engine alone is not a verified feature: the live test needs a prepared track. */
 ram_tap(XZ_UI_LOCAL_STEM_SETUP_STEP,2);assert(touch.visible&&strstr(ui.notice,"NOT FOUND"));
 fixture_prepared=1;refresh();ram_tap(XZ_UI_LOCAL_STEM_SETUP_STEP,2);assert(!touch.visible);
 assert(inline_enabled()==XZ_STEM_LAYOUT_BELOW);
 puts("PASS native physical coordinates: four row layouts, both decks, right/left drags, captured wave crossing, mode-change cancellation and hidden passthrough");
}
