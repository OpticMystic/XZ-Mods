#define XZ_PLAY_KEY_RUNTIME_NO_MAIN
#include "test_play_key_runtime_r1.c"
static uint16_t layout_pixels[800*480];
static void draw_layout(const char *dir,int mode){
 stock_key_fixture(layout_pixels);assert(xz_mods_render_v1(layout_pixels,800,480,800,NULL));
 if(!dir)return;
 char path[1024];snprintf(path,sizeof path,"%s/classic-layout-%d.ppm",dir,mode);FILE *f=fopen(path,"wb");assert(f);fputs("P6\n800 480\n255\n",f);
 for(int y=0;y<480;y++)for(int x=0;x<800;x++){unsigned v=layout_pixels[y*800+x];unsigned char b[3]={((v>>11)&31)*255/31,((v>>5)&63)*255/63,(v&31)*255/31};assert(fwrite(b,1,3,f)==3);}assert(!fclose(f));
}
int main(int argc,char **argv){
 assert(argc==2);(void)actions;(void)render_classic_key_fixture;
 for(int sides=0;sides<4;sides++)for(int mode=0;mode<XZ_LAYOUT_COUNT;mode++){
  reset(XZ_UI_INTERFACE_CLASSIC);settings_readonly=1;fixture_extra.left_on_3=sides&1;fixture_extra.right_on_4=(sides>>1)&1;
  xz_deck_layout_set(mode);native_four=mode==XZ_LAYOUT_TWO?-1:mode==XZ_LAYOUT_MIXER?XZ_FOUR_MIXER:XZ_FOUR_STACKED;
  draw_layout(sides==3?argv[1]:NULL,mode);assert(layout_button_shown);
  for(int side=0;side<2;side++)assert(classic_keys.deck[side]==(mode==XZ_LAYOUT_TWO?side:side+2*((sides>>side)&1)));
  if(mode!=XZ_LAYOUT_TWO){
   struct xz_four_layout g;assert(xz_four_layout_get(native_four,&g));native_active=0;
   for(int d=0;d<4;d++){
    int before=stock_calls;unsigned y=18+g.lane_y[d]+20;
    contact(solver,320,y,1);
    if(d<2)assert(stock_calls==before+1&&last_stock.y==18+xz_four_layout_source_row(&g,g.lane_y[d]+20));
    else assert(stock_calls==before);
    contact(solver,320,y,0);
    if(d>=2)assert(stock_calls==before);
   }
   native_active=1;
  }
  unsigned writes=settings_revision;int assigned_left=fixture_extra.left_on_3,assigned_right=fixture_extra.right_on_4;
  tap_xy(XZ_CLASSIC_LAYOUT_X+12,10);assert(xz_deck_layout_get()==xz_deck_layout_next(mode));assert(settings_revision==writes);
  assert(fixture_extra.left_on_3==assigned_left&&fixture_extra.right_on_4==assigned_right&&!layout_button_contact);
  /* No fresh matching frame: the old rectangle cannot cycle a second time. */
  tap_xy(XZ_CLASSIC_LAYOUT_X+12,10);assert(xz_deck_layout_get()==xz_deck_layout_next(mode));
  draw_layout(NULL,mode);assert(layout_button_painted&&!layout_button_shown);
  int pending=xz_deck_layout_get(),stock_before=stock_calls;
  tap_xy(XZ_CLASSIC_LAYOUT_X+12,10);assert(xz_deck_layout_get()==pending&&stock_calls==stock_before);
  int saved=xz_deck_layout_get();native_play=0;draw_layout(NULL,mode);assert(!layout_button_shown&&xz_deck_layout_get()==saved);
  native_play=1;fixture_extra.route_up=0;draw_layout(NULL,mode);assert(!layout_button_shown&&xz_deck_layout_get()==saved);
  fixture_extra.route_up=1;native_four=saved==XZ_LAYOUT_TWO?-1:saved==XZ_LAYOUT_MIXER?XZ_FOUR_MIXER:XZ_FOUR_STACKED;draw_layout(NULL,saved);assert(layout_button_shown&&xz_deck_layout_get()==saved);
  model.ui_interface=XZ_UI_INTERFACE_MODERN;assert(xz_deck_layout_get()==saved);model.ui_interface=XZ_UI_INTERFACE_CLASSIC;assert(xz_deck_layout_get()==saved);
 }
 puts("PASS actual CLASSIC UI render/touch: three layouts, all side assignments; TWO labels/keys stay native 1/2; one cycle per contact; no settings writes; browse/route/interface retain preference");
 return 0;
}
