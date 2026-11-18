/* All-four control admission and actual popup rendering with synthetic backends. */
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
 classic_keys.shown=0;classic_key_visible=0;play_key_raw_down=0;play_key_dirty=0;fake_idle=0;fake_frame_skip=fake_frame_hold_ui=fake_frame_switch_side=0;memset(&fixture_extra,0,sizeof fixture_extra);fixture_extra.route_up=1;
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

static int selected_master,load_picker;
#define queued fixture_event_count
#define queued_key fixture_events[fixture_event_count-1]
int xz_key_has_applied(int deck,uint32_t epoch){return deck<2&&epoch==fixture_keys[deck].epoch;}
int xz_sync_link_runtime_master_observe(struct xz_master_observation *m){
 *m=(struct xz_master_observation){.deck=selected_master,.revision=1,.operator_epoch=1,.observed_ms=xz_game_milliseconds()};return 1;
}
struct xz_extra_route xz_extra_decks_route(void){return (struct xz_extra_route){1,1};}
int xz_shell_runtime_key_setup_load(int deck){assert((unsigned)deck<4);load_picker++;return 1;}
static void four_keys(int interface,int master_deck){
 reset(interface);fixture_two_pair=0;selected_master=master_deck;queued=0;model.key_feedback=1;model.browse_traffic=1;
 for(int d=0;d<2;d++){
  fixture_keys[d].master=d==master_deck;
  struct xz_extra_deck *v=&fixture_extra.deck[d];v->known|=XZ_EXTRA_KNOWN(XZ_EXTRA_F_KEY_STATE);
  v->key_state=(struct xz_deck_key){.owner={.route_epoch=1,.load_revision=(uint32_t)d+1,.media_generation=11},
   .original={d?7:4,1},.playing={.key={d?7:4,1},.cents=23},.metadata_valid=1,.engine_ready=1,
   .playing_valid=1,.audio_state=XZ_KEY_AUDIO_APPLIED};
 }
 refresh();
}
int main(void){
 for(int interface=0;interface<2;interface++)for(int source=0;source<4;source++)for(int master_deck=0;master_deck<4;master_deck++){
  four_keys(interface,master_deck);
  fixture_two_pair=source>=2?1u<<(source-2):0;
  if(source>=2){fixture_extra.left_on_3=source==2;fixture_extra.right_on_4=source==3;}
  if(interface){
   key_shell.play_decks=4;key_model.sides_extra=(uint8_t)fixture_two_pair;paint_runtime_key_fixture();
   struct xz_shell_model projected;xz_shell_play_projection(&key_shell,&key_model,&projected);
   struct xz_shell_rect r=xz_shell_play_rect(&projected,XZ_SHELL_R_KEY,source);assert(r.w);tap_xy(8,r.y+12);
  }
  else {render_classic_key_fixture();tap_xy(30,135+143*(source%2));}
  if(!play_key.open||play_key.deck!=source)fprintf(stderr,"POPUP interface=%d source=%d master=%d open=%d deck=%d slots=%u two=%u stock=%d\n",interface,source,master_deck,play_key.open,play_key.deck,classic_keys.shown,fixture_two_pair,stock_calls);
  assert(play_key.open&&play_key.deck==source);paint_runtime_key_fixture();
  for(int button=0;button<3;button++){
   int before=queued,requests=fixture_key_requests;press_button(button);
   if(source>=2){assert(queued==before+1&&queued_key.deck==source-2);
    assert(queued_key.key.kind==(button==1?XZ_KEY_RESET:XZ_KEY_STEP));assert(queued_key.key.step==button-1);
    assert(xz_key_owner_equal(source,queued_key.key.source_owner,fixture_extra.deck[source-2].key_state.owner));
   }else assert(fixture_key_requests==requests+1);
  }
  int before=queued,requests=fixture_key_requests;press_button(3);
  if(source==master_deck){assert(queued==before&&fixture_key_requests==requests);assert(!sync_key(source));}
  else if(source>=2){assert(queued==before+1&&queued_key.key.kind==XZ_KEY_SYNC_TOGGLE&&queued_key.key.target_deck==master_deck);}
  else {assert(fixture_key_requests==requests+1&&key_sync[source].valid);
   struct xz_deck_key target;assert(deck_key_read(master_deck,&target,NULL));int cents;
   struct xz_key_snapshot src=fixture_keys[source];src.master_tempo=1;assert(xz_key_sync_cents(&src,&target.playing,&cents));
   assert(fixture_key_cents[source]==cents);
  }
  back_key();assert(!play_key.open);
 }
 for(int format=0;format<3;format++){
  four_keys(0,0);model.key_format=format;refresh();
  assert(strstr(model.deck[2].key_readout,">"));
  struct xz_play_key p0;xz_play_key_open(&p0,2);p0.down=0;
  struct xz_ui_action a;struct xz_ui_widget b=xz_play_key_button(2);
  xz_play_key_published(&p0,&p0);assert(!xz_play_key_touch(&p0,&model,b.x+3,b.y+3,1,&a));
  xz_play_key_changed(&p0);assert(!xz_play_key_touch(&p0,&model,0,0,0,&a));
 }
 four_keys(0,0);fixture_extra.deck[0].key_state.playing_valid=0;fixture_extra.deck[0].key_state.audio_state=XZ_KEY_AUDIO_UNKNOWN;
 refresh();assert(strstr(model.deck[2].key_readout,"TRACK"));
 struct xz_play_key_view v;play_key.deck=2;play_key_view_locked(&v);assert(!v.live_verified);
 fixture_extra.deck[0].key_state=(struct xz_deck_key){0};refresh();
 play_key_open_locked(2);play_key.down=0;paint_runtime_key_fixture();press_button(4);assert(play_key.setup&&play_key.deck==2);
 paint_runtime_key_fixture();press_button(4);paint_runtime_key_fixture();press_button(4);paint_runtime_key_fixture();press_button(4);
 assert(load_picker==1); /* Route is connected, so the existing track picker opens. */
 puts("PASS both actual popup interfaces: all 12 directions, source-master refusal, manual controls 1-4, notation, pending proof, setup and stale contacts");
 return 0;
}
