/* SPDX-License-Identifier: MIT */
#define XZ_PLAY_KEY_RUNTIME_NO_MAIN
#include "test_play_key_runtime_r1.c"
#include "native_wave.h"
#include "extra_lane.h"
#include "native_palette.h"
#include "../shell/four_deck_fixtures.h"
#include <stdlib.h>
static uint16_t native_pixels[XZ_NATIVE_WAVE_PIXELS],scratch_pixels[XZ_NATIVE_WAVE_PIXELS];
static uint16_t rendered[800*480],before[800*480],popup[800*480];
static int render_order;
static void extra_draw(uint16_t *pixels,uint32_t count){
 struct xz_four_layout g;assert(xz_four_layout_get(render_order,&g));
 struct xz_extra_lane_view view={1000,XZ_EXTRA_LANE_COLUMN_US,1,0};
 for(int d=0;d<2;d++)assert(xz_extra_lane_draw(pixels,536,count,g.lane_y[d+2],(unsigned)d+3,&fixture_extra.deck[d],&view,NULL));
}
static void classic_scene(int theme,int order){
 stock_key_fixture(rendered);
 for(int i=0;i<800*480;i++)rendered[i]=xz_native_palette_pixel(theme,rendered[i]);
 for(unsigned y=0;y<268;y++)for(unsigned x=0;x<536;x++)native_pixels[y*536+x]=(uint16_t)((y<132?0x18c3:0x2104)+(x%16));
 if(order>=0){
  /* The same qualified scene and paired C renderer as test_native_wave_four.c.
   * The native window is a host fixture; no ABI reader or hooks execute. */
  struct xz_native_wave_scene scene={1,0,0x4d200c,0x4d1f30,0x1fc2fc,0x1fdbe4,
   2,1,100,200,536,268,9,0x41000000,131,18,667,286,1};
  struct xz_native_wave_pair pair={0};render_order=order;
  assert(xz_native_wave_capture(&pair,&scene,0x1fd058,7,native_pixels,1072));
  assert(xz_native_wave_finish_four(&pair,&scene,7,200,scratch_pixels,XZ_NATIVE_WAVE_PIXELS,order,extra_draw,NULL));
 }
 for(unsigned y=0;y<268;y++)memcpy(rendered+(18+y)*800+131,native_pixels+y*536,536*sizeof(uint16_t));
}
static int in_box(struct xz_key_strip_box b,int x,int y){return x>=b.x&&x<b.x+b.w&&y>=b.y&&y<b.y+b.h;}
static void save(const char *folder,const char *name,const uint16_t *pixels){
 if(!folder)return;char path[1024];snprintf(path,sizeof path,"%s/%s.ppm",folder,name);FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n800 480\n255\n");
 for(int i=0;i<800*480;i++){unsigned p=pixels[i];unsigned char c[3]={((p>>11)&31)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};assert(fwrite(c,1,3,f)==3);}assert(!fclose(f));
}
int main(int argc,char **argv){
 const char *images=argc>1?argv[1]:NULL;unsigned cases=0;
 /* Keep the included r1 local-deck action and basic fixture checks live too. */
 reset(0);render_classic_key_fixture();actions(0,0,30,135);
 for(int order=0;order<XZ_FOUR_ORDER_COUNT;order++)for(int pair=0;pair<4;pair++)for(int feedback=0;feedback<2;feedback++)for(int theme=0;theme<XZ_THEME_FLAGSHIP;theme++){
  reset(0);native_active=0;native_four=order;model.theme=theme;model.key_feedback=feedback;
  fixture_extra.left_on_3=pair&1;fixture_extra.right_on_4=(pair>>1)&1;
  classic_scene(theme,order);memcpy(before,rendered,sizeof before);
  struct xz_key_strip_deck native[2]={{.valid=1,.original="Cm",.effective="Cm"},{.valid=1,.original="Am",.effective="Am"}};
  for(int d=0;d<2;d++)assert(!model.key_feedback||xz_key_strip_render_deck(rendered,800,theme,d,&native[d],1));
  struct xz_classic_key_slots slots={0};xz_classic_key_render(&slots,&model,&fixture_extra,1,order,rendered,800);assert(slots.shown==3);
  unsigned ink[2]={0};
  for(int y=0;y<480;y++)for(int x=0;x<800;x++){
   int a=in_box(xz_classic_key_box(0),x,y),b=in_box(xz_classic_key_box(1),x,y);
   if(rendered[y*800+x]!=before[y*800+x]){assert(a||b);ink[b]++;}
   assert(xz_classic_key_slot_at(&slots,x,y)==(a?0:b?1:-1));
  }
  assert(ink[0]>10&&ink[1]>10);
  /* Run the actual runtime render entry over the same C-rendered four-lane
   * scene, then touch exactly the bounds it published. */
  memcpy(rendered,before,sizeof rendered);assert(xz_mods_render_v1(rendered,800,480,800,NULL)&&classic_keys.shown==3);
  for(int side=0;side<2;side++){
   int deck=side+2*((pair>>side)&1);assert(classic_keys.deck[side]==deck);
   struct xz_key_strip_box b=xz_classic_key_box(side);int requests=fixture_key_requests;
   tap_xy(b.x+b.w/2,b.y+b.h/2);assert(play_key.open&&play_key.deck==deck&&play_key.classic_side==side);
    paint_runtime_key_fixture();
   if(images&&theme==0&&feedback&&pair==0&&order==XZ_FOUR_STACKED){
    struct xz_play_key_view v;play_key_view_locked(&v);memcpy(popup,rendered,sizeof popup);xz_play_key_render(&play_key,&v,popup,800);
    save(images,side?"classic-four-native-deck2-key-controls":"classic-four-native-deck1-key-controls",popup);
   }
   press_button(2);assert(fixture_key_requests==requests+(deck<2));
   if(deck<2){assert(fixture_key_cents[deck]==100);press_button(1);assert(fixture_key_cents[deck]==0);}
   else{
    struct xz_play_key_view v;play_key_view_locked(&v);assert(!strcmp(v.key,fixture_extra.deck[deck-2].key)&&v.native_engine==1);
    memset(popup,0,sizeof popup);xz_play_key_render(&play_key,&v,popup,800);
    uint16_t bg=popup[(XZ_PLAY_KEY_Y+145)*800+XZ_PLAY_KEY_X+20];
    for(int i=0;i<4;i++){struct xz_ui_widget w=xz_play_key_button(i);assert(popup[(w.y+w.h-3)*800+w.x+w.w-3]==bg);}
    press_button(3);assert(fixture_key_requests==requests);
    press_button(4);assert(play_key.setup);play_key_view_locked(&v);assert(v.native_engine==1);
    paint_runtime_key_fixture();
    press_button(4);assert(play_key.deck==deck&&fixture_key_requests==requests); /* actual capability recheck, no install */
    press_button(side);assert(play_key.deck==side&&!play_key.setup&&play_key.classic_side==-1);
    paint_runtime_key_fixture();
    press_button(2);assert(fixture_key_cents[side]==100&&fixture_key_requests==requests+1);press_button(1);
   }
   back_key();assert(!play_key.open);
  }
  if(theme==0&&feedback&&pair==3&&(order==XZ_FOUR_STACKED||order==XZ_FOUR_MIXER)){
   save(images,order==XZ_FOUR_STACKED?"classic-four-numeric-selected-3-4-metadata":"classic-four-mixer-selected-3-4-metadata",rendered);
   struct xz_key_strip_box b=xz_classic_key_box(0);tap_xy(b.x+5,b.y+5);press_button(4);
   struct xz_play_key_view v;play_key_view_locked(&v);memcpy(popup,rendered,sizeof popup);xz_play_key_render(&play_key,&v,popup,800);
   save(images,order==XZ_FOUR_STACKED?"classic-four-software-capability-native-choice":"classic-mixer-software-capability-native-choice",popup);
   back_key();back_key();
  }
  cases++;
 }
 /* The access pass must not erase the existing native role/cents/error
  * renderer when feedback is on. These are actual C-rendered pixels. */
 reset(0);native_four=XZ_FOUR_STACKED;model.key_feedback=1;classic_scene(0,native_four);
 struct xz_key_strip_deck native[2]={{.valid=1,.original="Cm",.effective="Dm",.synced=1,.cents=17},{.valid=1,.error="LOAD A KEYED TRACK ON DECK 2"}};
 for(int d=0;d<2;d++)assert(xz_key_strip_render_deck(rendered,800,0,d,&native[d],1));
 memcpy(before,rendered,sizeof before);struct xz_classic_key_slots keep;
 xz_classic_key_render(&keep,&model,&fixture_extra,1,native_four,rendered,800);assert(keep.shown==3&&!memcmp(before,rendered,sizeof before));
 /* A selection changed after a frame, and while a native popup is open:
  * neither stale rendered label nor old popup may send a native request. */
 reset(0);native_four=XZ_FOUR_STACKED;render_classic_key_fixture();int requests=fixture_key_requests;
 fixture_extra.left_on_3=1;tap_xy(20,130);assert(!play_key.open&&fixture_key_requests==requests);
 fixture_extra.left_on_3=0;render_classic_key_fixture();tap_xy(20,130);assert(play_key.deck==0&&play_key.open);
 fixture_extra.left_on_3=1;press_button(2);assert(!play_key.open&&fixture_key_requests==requests);
 /* Unknown metadata stays unknown; missing native engine is reported as
  * actually unavailable and an explicit native choice stays gated. */
 reset(0);native_four=XZ_FOUR_MIXER;fixture_extra.left_on_3=1;fixture_extra.deck[0].known=0;key_available=0;
 render_classic_key_fixture();tap_xy(20,130);struct xz_play_key_view v;play_key_view_locked(&v);assert(!strcmp(v.key,"---")&&v.native_engine==0);
 press_button(4);paint_runtime_key_fixture();press_button(0);paint_runtime_key_fixture();requests=fixture_key_requests;press_button(2);assert(fixture_key_requests==requests);
 /* A blank or non-PLAY render does not create a hidden key touch target. */
 reset(0);native_four=XZ_FOUR_STACKED;memset(rendered,0,sizeof rendered);assert(xz_mods_render_v1(rendered,800,480,800,NULL)&&!classic_keys.shown);
 tap_xy(20,130);assert(!play_key.open);native_play=0;stock_key_fixture(rendered);assert(xz_mods_render_v1(rendered,800,480,800,NULL)&&!classic_keys.shown);
 /* The MODERN popup reads the same actual XP metadata snapshot, not a local
  * key snapshot or the currently selected native side. */
 reset(1);static struct fx_track track;fx_track_build(&track,7);fx_model_four(&key_model,&track);
 key_model.deck[0].key="Cm";key_model.deck[0].key_original="Cm";key_model.deck[1].key="Am";key_model.deck[1].key_original="Am";
 key_shell.play_decks=2;key_model.sides_extra=3;
 fixture_extra.left_on_3=fixture_extra.right_on_4=1;paint_runtime_key_fixture();
 struct xz_shell_model projected;xz_shell_play_projection(&key_shell,&key_model,&projected);
 struct xz_shell_rect key=xz_shell_play_rect(&projected,XZ_SHELL_R_KEY,2);tap_xy(key.x+8,key.y+8);
 assert(play_key.open&&play_key.deck==2);play_key_view_locked(&v);assert(!strcmp(v.key,"8A")&&v.native_engine==1);
 assert(xz_mods_render_v1(rendered,800,480,800,NULL));save(images,"modern-software-metadata-actual-runtime",rendered);
 printf("PASS CLASSIC rendered key bounds: %u C-rendered scenes; %d classic themes, all 3 orders, all 4 pairs, feedback on/off; exhaustive pixel hit identity and outside-region preservation\n",cases,XZ_THEME_FLAGSHIP);
 puts("PASS actual CLASSIC four-lane key routing, software metadata, detected installed capability/native choice, stale selection fences and unavailable engine");
 puts("PASS original native SYNC/cents and two-line error feedback preserved byte-for-byte");
 return 0;
}
