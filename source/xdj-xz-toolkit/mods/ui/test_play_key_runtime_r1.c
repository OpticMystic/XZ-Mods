/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#define main preserved_runtime_controls_main
#include "test_runtime_controls.c"
#undef main
#include "../shell/shell_internal.h"
#include "native_palette.h"

/* Only the memory reads already used by key_player() succeed. No real memory,
 * device, USB, network or native functions are accessed by this host test. */
int xz_read_memory(uint32_t address,void *out,size_t n){
 uint32_t value=0;
 if(n==4){
  if(address==0x1c8a308)value=0x200000;
  else if(address==0x200040)value=0x210000;
  else if(address==0x200048)value=2;
  else if(address==0x210000)value=0x220000;
  else if(address==0x210004)value=0x221000;
  else if(address==0x220138)value=0x230000;
  else if(address==0x221138)value=0x231000;
  else return -1;
  memcpy(out,&value,4);return 0;
 }
 if(n==1&&(address==0x230026||address==0x231026)){*(unsigned char *)out=address==0x230026?1:2;return 0;}
 return -1;
}
void xz_layered_wave_configure(int mode,const uint32_t rgb[3]){(void)mode;(void)rgb;}
static struct xz_shell key_shell;
static struct xz_shell_model key_model;
static struct xz_extra_decks fixture_extra;
static int fixture_extra_ok=1;
int xz_extra_decks_snapshot(struct xz_extra_decks *out){if(!fixture_extra_ok)return 0;*out=fixture_extra;return 1;}
int xz_shell_runtime_key_hit(int x,int y,uint32_t *frame){
 int deck=xz_shell_play_key_deck(&key_shell,&key_model,x,y);
 *frame=deck>=0?xz_shell_play_key_frame(&key_shell,&key_model):0;return deck;
}
uint32_t xz_shell_runtime_key_frame(void){return xz_shell_play_key_frame(&key_shell,&key_model);}
static int fake_idle,frame_calls,fake_frame_skip,fake_frame_hold_ui,fake_frame_switch_side;
int xz_shell_runtime_idle(void){return fake_idle;}
int xz_shell_runtime_frame(uint16_t *pixels,size_t stride,const struct xz_shell_runtime_frame_input *in){
 if(!fake_shell_running||!fake_owns)return 0;
 assert(in->key_controls_open==play_key.open);frame_calls++;
 if(fake_frame_skip)return -1;
 int rendered=xz_shell_render_frame(&key_shell,&key_model,pixels,stride);
 if(fake_frame_switch_side)fixture_extra.left_on_3=1;
 if(fake_frame_hold_ui)pthread_mutex_lock(&ui_mutex);
 return rendered;
}
static int mt_calls,mt_deck,mt_value;
static int mt_set(void *player,int on){mt_calls++;mt_deck=(uintptr_t)player==0x230000?0:1;mt_value=on;return 0;}
static unsigned char solver[32];
static void tap_xy(int x,int y){contact(solver,(unsigned)x,(unsigned)y,1);contact(solver,0,0,0);}
static void press_button(int which){struct xz_ui_widget w=xz_play_key_button(which);tap_xy(w.x+w.w/2,w.y+20);}
static void back_key(void){unsigned char input[16]={0};input[8]=8;input[9]=0x42;menu_back_key_hook(NULL,input);input[11]=2;menu_back_key_hook(NULL,input);}
static void reset(int interface){
 memset(solver,0,sizeof solver);memset(&play_key,0,sizeof play_key);memset(key_sync,0,sizeof key_sync);play_key_visible=0;
 classic_keys.shown=0;classic_key_visible=0;play_key_raw_down=0;play_key_dirty=0;fake_idle=0;fake_frame_skip=fake_frame_hold_ui=fake_frame_switch_side=0;fixture_extra_ok=1;memset(&fixture_extra,0,sizeof fixture_extra);fixture_extra.route_up=1;
 for(int d=0;d<2;d++){fixture_extra.deck[d].loaded=1;fixture_extra.deck[d].known=XZ_EXTRA_KNOWN(XZ_EXTRA_F_KEY);snprintf(fixture_extra.deck[d].key,8,"%s",d?"Gm":"8A");}
 rekordbox_reset(interface,XZ_REKORDBOX_MODS,XZ_REKORDBOX_STOCK);xz_ui_init(&ui);xz_ui_init(&inline_ui);
 started=key_available=1;native_master_tempo_on=mt_set;native_play=native_active=1;native_four=-1;stock_touch=stock;
 xz_native_touch_init(&touch,&ui,&model,apply,NULL);stock_calls=0;model.ui_interface=interface;
 for(int d=0;d<2;d++){
  model.deck[d].ready=XZ_UI_KEY|XZ_UI_KEYSYNC;model.deck[d].key_synced=0;
  fixture_key_valid[d]=1;fixture_key_cents[d]=fixture_key_applied[d]=0;
  fixture_keys[d]=(struct xz_key_snapshot){.original={d?9:0,1},.epoch=(uint32_t)(10+d),.loaded=1,.tempo_ratio=1,.master_tempo=1};
 }
 xz_shell_init(&key_shell);memset(&key_model,0,sizeof key_model);key_model.deck_count=4;
}
static void stock_key_fixture(uint16_t *pixels){
 memset(pixels,0,800*480*sizeof *pixels);
 for(int x=0;x<800;x++){pixels[314*800+x]=0x118c;pixels[345*800+x]=0x0861;}
 pixels[31*800+21]=pixels[43*800+50]=0xffff;
}
static void render_classic_key_fixture(void){
 static uint16_t pixels[800*480];stock_key_fixture(pixels);assert(xz_mods_render_v1(pixels,800,480,800,NULL));
 assert(classic_keys.shown==3);
}
static void paint_runtime_key_fixture(void){
 static uint16_t pixels[800*480];if(!model.ui_interface){
  stock_key_fixture(pixels);
  for(size_t i=0;i<800*480;i++)pixels[i]=xz_native_palette_pixel(model.theme,pixels[i]);
 }
 assert(xz_mods_render_v1(pixels,800,480,800,NULL));
}
static void actions(int interface,int deck,int x,int y){
 if(interface)paint_runtime_key_fixture();
 int before=fixture_key_requests;
 tap_xy(x,y);assert(play_key.open&&play_key.deck==deck&&!stock_calls);
 /* The fixture must draw the opened controls before sending key gestures. */
 paint_runtime_key_fixture();
 press_button(2);assert(fixture_key_requests==before+1&&fixture_key_cents[deck]==100);
 press_button(0);assert(fixture_key_cents[deck]==0);
 press_button(0);assert(fixture_key_cents[deck]==-100);
 press_button(1);assert(fixture_key_cents[deck]==0);
 mt_calls=0;press_button(3);assert(mt_calls==1&&mt_deck==deck&&mt_value==1&&key_sync[deck].valid);
 assert(fixture_key_requests==before+5);
 press_button(3);assert(!key_sync[deck].valid&&fixture_key_cents[deck]==0);
 back_key();assert(!play_key.open&&!stock_calls);
 printf("PASS actual %s touch_hook -> popover -> apply -> key engine requests: local deck %d KEY-/RESET/KEY+/SYNC toggle and BACK\n",interface?"MODERN":"CLASSIC",deck+1);
}
#ifndef XZ_PLAY_KEY_RUNTIME_NO_MAIN
#include "test_key_publication_r4.h"
int main(void){
 for(int interface=0;interface<2;interface++)for(int d=0;d<2;d++){
  reset(interface);
  if(interface){key_shell.play_decks=2;struct xz_shell_model view;xz_shell_play_projection(&key_shell,&key_model,&view);struct xz_shell_rect r=xz_shell_play_rect(&view,XZ_SHELL_R_KEY,d);actions(interface,d,8,r.y+12);}
  else {render_classic_key_fixture();actions(interface,d,30,135+143*d);}
 }
 reset(1);
 for(int view_mode=2;view_mode<=4;view_mode+=2)for(int sides=0;sides<4;sides++)for(int order=0;order<2;order++){
  key_shell.play_decks=view_mode;key_model.sides_extra=(uint8_t)sides;key_model.wave_order=order;
  fixture_extra.left_on_3=sides&1;fixture_extra.right_on_4=(sides>>1)&1;paint_runtime_key_fixture();
  struct xz_shell_model view;xz_shell_play_projection(&key_shell,&key_model,&view);
  for(int d=0;d<4;d++){
   struct xz_shell_rect r=xz_shell_play_rect(&view,XZ_SHELL_R_KEY,d);if(!r.w)continue;
   int before=fixture_key_requests;tap_xy(8,r.y+12);assert(play_key.open&&play_key.deck==d);
   paint_runtime_key_fixture();
   press_button(2);assert(fixture_key_requests==before+(d<2));
   if(d<2){assert(fixture_key_cents[d]==100);press_button(1);assert(fixture_key_cents[d]==0);}
   else{
    press_button(3);press_button(4);assert(play_key.setup&&fixture_key_requests==before);
    /* Even a stale/direct producer cannot shift or sync a software deck. */
    struct xz_ui_action a={.kind=XZ_UI_KEY_SHIFT,.deck=d,.index=1};apply(NULL,&a,1);a.kind=XZ_UI_KEY_SYNC;apply(NULL,&a,1);assert(fixture_key_requests==before);
   }
   if(play_key.setup)back_key();back_key();assert(!play_key.open);
  }
 }
 puts("PASS actual MODERN selected-pair/4-lane touch routing: all side assignments, both wave orders, no local-deck alias from software decks 3/4");
 reset(1);key_shell.play_decks=4;fake_idle=1;
 static uint16_t pixels[800*480],clean[800*480];
 assert(xz_shell_render_frame(&key_shell,&key_model,clean,800));
 struct xz_shell_model projected;xz_shell_play_projection(&key_shell,&key_model,&projected);
 struct xz_shell_rect first=xz_shell_play_rect(&projected,XZ_SHELL_R_KEY,0);tap_xy(8,first.y+8);
 frame_calls=0;assert(xz_mods_render_v1(pixels,800,480,800,NULL)&&frame_calls==1&&memcmp(pixels,clean,sizeof clean));
 memcpy(clean,pixels,sizeof clean);pthread_mutex_lock(&ui_mutex);
 assert(!xz_mods_render_v1(pixels,800,480,800,NULL));pthread_mutex_unlock(&ui_mutex);assert(!memcmp(pixels,clean,sizeof clean));
 assert(encoder_rotate_hook(1)==1&&encoder_push_hook(NULL,NULL,NULL,NULL)==1);
 back_key();assert(!play_key.open);assert(xz_mods_render_v1(pixels,800,480,800,NULL)&&frame_calls==2&&!play_key_dirty);
 puts("PASS real render entry: modal overlay survives idle/busy frames, close redraws the base, encoder cannot operate hidden settings");
 reset(0);fixture_key_valid[0]=0;model.deck[0].ready=0;snprintf(model.deck[0].key_readout,sizeof model.deck[0].key_readout,"---");
 render_classic_key_fixture();tap_xy(30,135);int before=fixture_key_requests;for(int b=0;b<4;b++)press_button(b);assert(fixture_key_requests==before);
 press_button(4);assert(play_key.setup);for(int i=0;i<4;i++)press_button(4);
 assert(play_key.setup&&play_key.step==3&&strstr(ui.notice,"LOAD ANALYZED"));
 back_key();assert(play_key.open&&!play_key.setup);back_key();assert(!play_key.open);
 reset(0);native_four=0;render_classic_key_fixture();actions(0,0,30,135);
 puts("PASS unknown/unanalysed keys: no engine request, working setup flow, failed live verify stays open; CLASSIC four-lane uses the rendered logical key slots");
 r4_publication_regressions();
 return 0;
}
#endif
