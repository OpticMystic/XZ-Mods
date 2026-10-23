/* SPDX-License-Identifier: MIT */
#ifndef XZ_TEST_KEY_PUBLICATION_R4_H
#define XZ_TEST_KEY_PUBLICATION_R4_H
/* Included by the retained runtime test only. Independent probe.c is unchanged. */
static uint16_t r4_frame[800*480];
static void r4_open_software(int interface){
 reset(interface);fixture_extra.left_on_3=1;
 if(interface){
  key_shell.play_decks=2;key_model.sides_extra=1;paint_runtime_key_fixture();
  struct xz_shell_model view;xz_shell_play_projection(&key_shell,&key_model,&view);
  struct xz_shell_rect r=xz_shell_play_rect(&view,XZ_SHELL_R_KEY,2);tap_xy(r.x+8,r.y+8);
 }else{native_four=XZ_FOUR_STACKED;render_classic_key_fixture();tap_xy(20,130);}
 assert(play_key.open&&play_key.deck==2);press_button(4);assert(play_key.setup);paint_runtime_key_fixture();
}
static void r4_publication_regressions(void){
 /* Pure preview rendering and failed interactive rendering cannot publish
  * targets. A successful interactive render records value-owned geometry. */
 reset(1);key_shell.play_decks=4;
 assert(!xz_shell_play_key_frame(&key_shell,&key_model));
 assert(xz_shell_render(&key_shell,&key_model,r4_frame,800));
 assert(!xz_shell_play_key_frame(&key_shell,&key_model));
 assert(!xz_shell_render_frame(&key_shell,&key_model,NULL,800));
 assert(!xz_shell_render_frame(&key_shell,&key_model,r4_frame,799));
 assert(!xz_shell_play_key_frame(&key_shell,&key_model));
 assert(xz_shell_render_frame(&key_shell,&key_model,r4_frame,800));
 struct xz_shell_key_frame recorded=key_shell.key_frame;
 struct xz_shell_model view;xz_shell_play_projection(&key_shell,&key_model,&view);
 struct xz_shell_rect third=xz_shell_play_rect(&view,XZ_SHELL_R_KEY,2);
 assert(xz_shell_play_key_deck(&key_shell,&key_model,third.x+8,third.y+8)==2);
 key_shell.play_decks=2;
 assert(!xz_shell_play_key_frame(&key_shell,&key_model));
 assert(xz_shell_play_key_deck(&key_shell,&key_model,third.x+8,third.y+8)==-2);
 assert(!xz_shell_render_frame(&key_shell,&key_model,NULL,800));
 assert(!memcmp(&recorded,&key_shell.key_frame,sizeof recorded));
 key_shell.play_decks=4;struct xz_shell copy=key_shell;copy.play_decks=2;
 assert(xz_shell_render_frame(&copy,&key_model,r4_frame,800));
 assert(!memcmp(&recorded,&key_shell.key_frame,sizeof recorded));
 assert(xz_shell_play_key_frame(&key_shell,&key_model));

 /* Every logical pair/order uses only its rendered key rectangles. Changing
  * projection without painting rejects each old key target, never remaps it. */
 for(int order=0;order<2;order++)for(int sides=0;sides<4;sides++)for(int count=2;count<=4;count+=2){
  reset(1);key_shell.play_decks=count;key_model.wave_order=order;key_model.sides_extra=(uint8_t)sides;
  fixture_extra.left_on_3=sides&1;fixture_extra.right_on_4=(sides>>1)&1;paint_runtime_key_fixture();
  xz_shell_play_projection(&key_shell,&key_model,&view);
  for(int d=0;d<4;d++){
   struct xz_shell_rect r=xz_shell_play_rect(&view,XZ_SHELL_R_KEY,d);if(!r.w)continue;
   assert(xz_shell_play_key_deck(&key_shell,&key_model,r.x+8,r.y+8)==d);
   key_shell.play_decks=count==4?2:4;
   assert(xz_shell_play_key_deck(&key_shell,&key_model,r.x+8,r.y+8)==-2);
   key_shell.play_decks=count;
  }
 }
 /* A fresh side snapshot can invalidate the implicit native target before
  * the shell model refresh catches up. */
 reset(1);key_shell.play_decks=2;paint_runtime_key_fixture();
 xz_shell_play_projection(&key_shell,&key_model,&view);struct xz_shell_rect first=xz_shell_play_rect(&view,XZ_SHELL_R_KEY,0);
 tap_xy(first.x+8,first.y+8);paint_runtime_key_fixture();int requests=fixture_key_requests;
 fixture_extra.left_on_3=1;assert(key_model.sides_extra==0);press_button(2);
 assert(!play_key.open&&fixture_key_requests==requests);
 reset(1);key_shell.play_decks=2;paint_runtime_key_fixture();tap_xy(first.x+8,first.y+8);paint_runtime_key_fixture();
 fake_frame_switch_side=1;paint_runtime_key_fixture();assert(!play_key.open);fake_frame_switch_side=0;
 requests=fixture_key_requests;tap_xy(first.x+8,first.y+8);assert(!play_key.open&&fixture_key_requests==requests);

 for(int interface=0;interface<2;interface++)for(int deck=0;deck<2;deck++){
  r4_open_software(interface);struct xz_play_key old_choice=play_key;
  press_button(deck);assert(play_key.deck==deck&&!play_key.setup&&play_key.classic_side==-1&&!play_key.source_frame);
  requests=fixture_key_requests;
  /* A late completion of the capability page cannot authorize the native bar. */
  struct xz_play_key_view old_view;xz_play_key_view(&old_choice,&model,NULL,&old_view);
  xz_play_key_render(&old_choice,&old_view,r4_frame,800);xz_play_key_published(&play_key,&old_choice);
  assert(play_key.painted_revision!=play_key.revision);
  struct xz_ui_widget plus=xz_play_key_button(2);
  contact(solver,plus.x+8,plus.y+8,1);paint_runtime_key_fixture();contact(solver,0,0,0);
  assert(fixture_key_requests==requests); /* paint between down/up cannot revive an unseen press */
  press_button(2);assert(fixture_key_requests==requests+1&&fixture_key_cents[deck]==100);
  /* The explicit choice stays independent from subsequent side switching. */
  fixture_extra.left_on_3=0;key_model.sides_extra=0;paint_runtime_key_fixture();
  requests=fixture_key_requests;press_button(1);assert(fixture_key_requests==requests+1&&fixture_key_cents[deck]==0);
 }
 /* A skipped frame or unavailable publication lock cannot acknowledge the
  * native bar. The next completed/acknowledged frame makes it usable. */
 r4_open_software(1);press_button(0);requests=fixture_key_requests;fake_frame_skip=1;
 assert(!xz_mods_render_v1(r4_frame,800,480,800,NULL));press_button(2);assert(fixture_key_requests==requests);
 fake_frame_skip=0;fake_frame_hold_ui=1;assert(xz_mods_render_v1(r4_frame,800,480,800,NULL));
 pthread_mutex_unlock(&ui_mutex);fake_frame_hold_ui=0;press_button(2);assert(fixture_key_requests==requests);
 paint_runtime_key_fixture();press_button(2);assert(fixture_key_requests==requests+1);
 /* A completed old popup cannot acknowledge a new instance with the same deck/revision. */
 reset(0);render_classic_key_fixture();tap_xy(20,130);paint_runtime_key_fixture();struct xz_play_key old=play_key;
 back_key();tap_xy(20,130);assert(play_key.open&&play_key.instance!=old.instance);
 struct xz_play_key_view old_view;play_key_view_locked(&old_view);xz_play_key_render(&old,&old_view,r4_frame,800);
 xz_play_key_published(&play_key,&old);assert(play_key.painted_revision!=play_key.revision);
 requests=fixture_key_requests;press_button(2);assert(fixture_key_requests==requests);paint_runtime_key_fixture();press_button(2);assert(fixture_key_requests==requests+1);
 puts("PASS r4: successful-frame publication, failed/pure render refusal, copied-shell isolation, all pairs/orders, lagging side model, chosen-native down-before-paint and late-popup acknowledgements");
}
#endif
