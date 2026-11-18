#define XZ_PLAY_KEY_RUNTIME_NO_MAIN
#include "test_play_key_runtime_r1.c"
static uint16_t screen[800*480+8];
int main(void){
 (void)actions;(void)render_classic_key_fixture;
 reset(XZ_UI_INTERFACE_CLASSIC);settings_readonly=1;
 stock_uikey_rekordbox=fake_stock_rekordbox;stock_global_key=fake_global;rekordbox_shift_held=fake_shift_held;
 unsigned checks=0;
 for(unsigned cycle=0;cycle<32;cycle++){
  int wanted=(int)(cycle&1u);struct xz_ui_action mode={.kind=XZ_UI_SET_INTERFACE,.phase=XZ_UI_PRESS,.index=wanted};apply(NULL,&mode,1);
  if(!wanted){struct xz_ui_action theme={.kind=XZ_UI_SET_THEME,.phase=XZ_UI_PRESS,.index=(int)(cycle%XZ_CLASSIC_THEME_COUNT)};apply(NULL,&theme,1);}
  for(unsigned context=0;context<8;context++){
   native_play=(context&1u)!=0;native_active=1;fake_owns=wanted&&native_play;
   fixture_extra_ok=context!=3;fixture_extra.route_up=context!=4;
   fixture_extra.left_on_3=fixture_extra.route_up&&!!(context&2);fixture_extra.right_on_4=fixture_extra.route_up&&!!(context&4);
   fixture_extra.deck[0].loaded=context!=2;fixture_extra.deck[1].loaded=context!=5;
   fixture_extra.deck[0].playing=!!(context&1);fixture_extra.deck[1].playing=!!(context&2);
   key_model.deck_count=fixture_extra.route_up?4:2;key_shell.play_decks=key_model.deck_count;
   key_model.sides_extra=(uint8_t)(fixture_extra.left_on_3|(fixture_extra.right_on_4<<1));native_four=fixture_extra.route_up?XZ_FOUR_MIXER:-1;
   unsigned prior_frames=frame_calls;refresh();stock_key_fixture(screen);for(unsigned i=800*480;i<800*480+8;i++)screen[i]=0xa55a;
   (void)xz_mods_render_v1(screen,800,480,800,NULL);
   assert(model.ui_interface==wanted&&model.theme==(wanted?XZ_THEME_FLAGSHIP:model.classic_theme));checks++;
   assert(fake_shell_running==wanted);if(!wanted)assert((unsigned)frame_calls==prior_frames);checks++;
   for(unsigned i=800*480;i<800*480+8;i++)assert(screen[i]==0xa55a);
  }
 }
 native_play=native_active=1;touch.visible=0;model.rekordbox_assign[0]=XZ_REKORDBOX_INTERFACE;
 for(unsigned cycle=0;cycle<64;cycle++){
  int before=model.ui_interface;fake_owns=fake_shell_running;
  rekordbox_contact(0,0);assert(model.ui_interface==!before);checks++;
  refresh();assert(model.ui_interface==!before);checks++;
 }
 printf("PASS interface preference: 32 explicit choices, 256 browse/play/load/route contexts, 64 complete shortcut gestures, %u checks; Classic never draws Flagship\n",checks);return 0;
}
