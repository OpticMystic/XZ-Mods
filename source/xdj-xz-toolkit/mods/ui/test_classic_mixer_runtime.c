/* SPDX-License-Identifier: MIT */
/* Reuse the exact guarded native-memory fixture, not firmware or a live device. */
#define main existing_shell_model_checks
#include "../test_shell_model.c"
#undef main
#include "classic_mixer_runtime.c"
static struct xz_extra_decks fixture_extra;
static unsigned repaints,acquires,releases,active_holds;
static int fixture_layout=XZ_LAYOUT_MIXER;
/* This fixture flag represents a live global window, not a latched message ID. */
static int fixture_window_up;
int xz_stock_dialog_active(void){return fixture_window_up;}
int xz_deck_layout_get(void){return fixture_layout;}
static struct xz_extra_column wave_columns[188];
int xz_read_memory(uint32_t address,void *out,size_t count){return read_memory(address,out,count);}
int xz_extra_decks_snapshot_held(struct xz_extra_decks *out,struct xz_extra_analysis_hold *hold){
 assert(!active_holds&&!hold->analysis[0]&&!hold->analysis[1]);
 *out=fixture_extra;hold->analysis[0]=wave_columns;active_holds=1;acquires++;return out->route_up;
}
void xz_extra_decks_release_analysis(struct xz_extra_decks *out,struct xz_extra_analysis_hold *hold){
 if(hold->analysis[0]){assert(active_holds);active_holds=0;releases++;hold->analysis[0]=NULL;}
 for(int i=0;i<2;i++){out->deck[i].wave.columns=NULL;out->deck[i].grid=NULL;}
}
void xz_native_skin_request_repaint(void){repaints++;}
/* The native theme map: off unless a test turns it on; then it flips the low bits of each mapped pixel. */
static int fixture_theme;static unsigned mapped_pixels;
int xz_mods_native_style_v1(void){return fixture_theme;}
void xz_mods_native_pixels_v1(uint16_t *p,uint32_t n){mapped_pixels+=n;for(uint32_t i=0;i<n;i++)p[i]^=0x0841;}
/* 2 DECKS following the sides: a pixel of a side's half (column x 0..129, y 18..159 / 160..308; panel from y 309). */
static int in_half(int side,int x,int y){
 return (x<130&&y>=xz_classic_column_half_y(side)&&y<xz_classic_column_half_y(side)+xz_classic_column_half_h(side))||
        (y>=309&&x>=side*400&&x<side*400+400);
}
static uint16_t pair_base[800*480],pair_plain[800*480];
static void pair_fill(uint16_t *p){for(unsigned i=0;i<800*480;i++)p[i]=pair_base[i];}
/* Pixels the frame changed, each inside a half of sides; returns how many. */
static unsigned pair_changed(const uint16_t *p,unsigned sides){
 unsigned n=0;
 for(int y=0;y<480;y++)for(int x=0;x<800;x++)if(p[y*800+x]!=pair_base[y*800+x]){
  assert(((sides&1u)&&in_half(0,x,y))||((sides&2u)&&in_half(1,x,y)));n++;
 }
 return n;
}
static uint16_t pixels[800*480],footer_pixels[300*34];
int main(void){
 fixture();fixture_window_up=0;fixture_extra.route_up=1;
 for(unsigned i=0;i<188;i++)wave_columns[i]=(struct xz_extra_column){150,80,40};
 fixture_extra.deck[0].loaded=1;fixture_extra.deck[0].known=XZ_EXTRA_KNOWN(XZ_EXTRA_F_WAVE);
 fixture_extra.deck[0].wave=(struct xz_extra_wave){wave_columns,188,300000,270};
 assert(!xz_classic_mixer_runtime_start());
 const struct xz_classic_mixer_key keys[2]={{1,"4A","2A",-21},{1,"9B","9B",0}};
 assert(xz_classic_mixer_runtime_frame(pixels,800,800*480,1,0,1000,keys));
 assert(xz_classic_mixer_runtime_visible());
 assert(!active_holds&&acquires==releases&&xz_classic_mixer_runtime_layout()==XZ_LAYOUT_MIXER);
 unsigned wave_pixels=0;for(int y=441;y<473;y++)for(int x=6;x<194;x++)if(pixels[y*800+x]!=pixels[y*800])wave_pixels++;
 assert(wave_pixels>188);
 assert(native.deck[0].auto_cue&&(native.deck[0].known&K(AUTO_CUE)));
 assert(!strcmp(native.deck[0].key,"4A"));
 unsigned reads=read_calls;
 assert(xz_classic_mixer_runtime_frame(pixels,800,800*480,1,0,1050,keys)&&read_calls==reads);
 for(unsigned i=0;i<300*34;i++)footer_pixels[i]=(uint16_t)(i+1);
 xz_classic_mixer_native_overview(0,footer_pixels,300*34,7,423);
 xz_classic_mixer_native_overview(1,footer_pixels,300*34,407,423);
 int x,y;
 assert(xz_classic_mixer_runtime_native_point(0,299,33,&x,&y)&&x==306&&y==456);
 assert(xz_classic_mixer_runtime_native_point(1,0,0,&x,&y)&&x==407&&y==423);
 assert(!xz_classic_mixer_runtime_native_point(2,0,0,&x,&y));
 xz_classic_mixer_native_overview(0,footer_pixels,300*34,407,423);
 xz_classic_mixer_native_overview(0,footer_pixels,300*34-1,7,423);
 assert(xz_classic_mixer_runtime_native_point(0,299,33,&x,&y)&&x==306);
 assert(xz_classic_mixer_runtime_frame(pixels,800,800*480,1,0,1100,keys)&&read_calls>reads);
 assert(overview_sequence[0]&&overview_sequence[1]);
 assert(!memcmp(overview[0],footer_pixels,sizeof footer_pixels));
 /* A load invalidates old native overview pixels even if the capture lock is busy. */
 put16(title_record(0)+0x22,'X');put32(total(0)+0xc,999);
 pthread_mutex_lock(&overview_mutex);
 assert(xz_classic_mixer_runtime_frame(pixels,800,800*480,1,0,1200,keys));
 assert(!overview_sequence[0]&&(pending_invalidation&1));
 pthread_mutex_unlock(&overview_mutex);
 assert(xz_classic_mixer_runtime_frame(pixels,800,800*480,1,0,1210,keys));
 assert(!overview_sequence[0]&&!pending_invalidation);
 xz_classic_mixer_native_overview(0,footer_pixels,300*34,7,423);
 assert(xz_classic_mixer_runtime_frame(pixels,800,800*480,1,0,1220,keys)&&overview_sequence[0]);
 /* The side column: four tags beside the lanes (MIXER: 3 1 2 4), and the band's top rows cleared. */
 for(int y=309;y<311;y++)for(int x=0;x<800;x++)assert(!pixels[y*800+x]);
 int tags=0;for(int y=22;y<309;y++)for(int x=0;x<130;x++)tags+=pixels[y*800+x]!=0;
 assert(tags>3000);
 /* Decks 3 and 4 down with DECK VIEW four: the band stays, decks 3 and 4 NOT CONNECTED; never a hide. */
 unsigned before=repaints;
 fixture_extra.route_up=0;
 assert(xz_classic_mixer_runtime_frame(pixels,800,800*480,1,0,1230,keys));
 assert(xz_classic_mixer_runtime_visible()&&repaints==before&&!active_holds&&acquires==releases);
 uint16_t dim=(uint16_t)(((0x16>>3)<<11)|((0x1c>>2)<<5)|(0x26>>3));
 assert(pixels[312*800+60]==dim&&pixels[312*800+660]==dim&&pixels[312*800+260]!=dim);
 fixture_extra.route_up=1;
 assert(xz_classic_mixer_runtime_frame(pixels,800,800*480,1,0,1300,keys));
 fixture_window_up=1;
 assert(!xz_classic_mixer_runtime_frame(pixels,800,800*480,1,0,1400,keys));
 assert(!xz_classic_mixer_runtime_visible());
 assert(!active_holds&&acquires==releases);
 fixture_layout=XZ_LAYOUT_TWO;unsigned held_before=acquires;
 assert(!xz_classic_mixer_runtime_frame(pixels,800,800*480,1,0,1450,keys)&&held_before==acquires);
 fixture_layout=XZ_LAYOUT_FOUR;

 /* 2 DECKS following the sides. */
 fixture_window_up=0;fixture_layout=XZ_LAYOUT_TWO;
 fixture_extra.route_up=1;fixture_extra.left_on_3=1;fixture_extra.right_on_4=0;
 fixture_extra.deck[0].known|=XZ_EXTRA_KNOWN(XZ_EXTRA_F_TITLE)|XZ_EXTRA_KNOWN(XZ_EXTRA_F_KEY);
 snprintf(fixture_extra.deck[0].title,sizeof fixture_extra.deck[0].title,"Fixture");strcpy(fixture_extra.deck[0].key,"8A");
 fixture_extra.deck[1]=fixture_extra.deck[0];
 for(unsigned i=0;i<800*480;i++)pair_base[i]=(uint16_t)(i*2654435761u>>9);
 /* Left on DECK 3: exactly its column half and its panel. */
 pair_fill(pixels);
 assert(xz_classic_mixer_runtime_pair_frame(pixels,800,800*480,1,0,2000,keys,1u));
 assert(xz_classic_mixer_runtime_pair()==1u&&xz_classic_mixer_runtime_visible()&&!active_holds&&acquires==releases);
 assert(pair_changed(pixels,1u)>50000);
 memcpy(pair_plain,pixels,sizeof pair_plain);
 /* The wave painted both sides, the snapshot says only the left: only the left is drawn. */
 pair_fill(pixels);assert(xz_classic_mixer_runtime_pair_frame(pixels,800,800*480,1,0,2010,keys,3u));
 assert(xz_classic_mixer_runtime_pair()==1u&&pair_changed(pixels,1u)>50000);
 /* The snapshot has both, the wave painted the left only: only the left. */
 fixture_extra.right_on_4=1;
 pair_fill(pixels);assert(xz_classic_mixer_runtime_pair_frame(pixels,800,800*480,1,0,2020,keys,1u));
 assert(xz_classic_mixer_runtime_pair()==1u&&pair_changed(pixels,1u)>50000);
 /* Both. */
 unsigned before_pair=repaints;
 pair_fill(pixels);assert(xz_classic_mixer_runtime_pair_frame(pixels,800,800*480,1,0,2030,keys,3u));
 assert(xz_classic_mixer_runtime_pair()==3u&&pair_changed(pixels,3u)>100000&&repaints==before_pair);
 /* The right side back on DECK 2: one native repaint gives its stock half back. */
 fixture_extra.right_on_4=0;
 pair_fill(pixels);assert(xz_classic_mixer_runtime_pair_frame(pixels,800,800*480,1,0,2040,keys,1u));
 assert(xz_classic_mixer_runtime_pair()==1u&&repaints==before_pair+1&&pair_changed(pixels,1u));
 /* Route loss: nothing drawn, hidden, one repaint, the hold released. */
 fixture_extra.route_up=0;fixture_extra.left_on_3=0;
 pair_fill(pixels);assert(!xz_classic_mixer_runtime_pair_frame(pixels,800,800*480,1,0,2050,keys,1u));
 assert(!xz_classic_mixer_runtime_visible()&&!xz_classic_mixer_runtime_pair()&&repaints==before_pair+2&&!pair_changed(pixels,0));
 assert(!active_holds&&acquires==releases);
 /* MODS, a game or a takeover (not eligible), or the wave painting no pair: hidden without a snapshot. */
 fixture_extra.route_up=1;fixture_extra.left_on_3=1;
 assert(xz_classic_mixer_runtime_pair_frame(pixels,800,800*480,1,0,2060,keys,1u));
 unsigned taken=acquires;before_pair=repaints;
 pair_fill(pixels);assert(!xz_classic_mixer_runtime_pair_frame(pixels,800,800*480,0,0,2070,keys,1u));
 assert(!xz_classic_mixer_runtime_visible()&&repaints==before_pair+1&&acquires==taken&&!pair_changed(pixels,0));
 assert(!xz_classic_mixer_runtime_pair_frame(pixels,800,800*480,1,0,2080,keys,0u)&&acquires==taken);
 /* A native caution owns the screen. */
 fixture_window_up=1;refreshed=0;
 assert(!xz_classic_mixer_runtime_pair_frame(pixels,800,800*480,1,0,2090,keys,1u)&&!xz_classic_mixer_runtime_visible());
 fixture_window_up=0;refreshed=0;
 /* A four-deck view has no halves. */
 fixture_layout=XZ_LAYOUT_FOUR;assert(!xz_classic_mixer_runtime_pair_frame(pixels,800,800*480,1,0,2100,keys,1u));
 fixture_layout=XZ_LAYOUT_TWO;
 /* A theme maps the halves like the stock beside them, then the number boxes go back in their own colours. */
 fixture_theme=1;mapped_pixels=0;
 pair_fill(pixels);assert(xz_classic_mixer_runtime_pair_frame(pixels,800,800*480,1,0,2110,keys,1u));
 assert(mapped_pixels==130u*142u+400u*171u);
 uint16_t rose=(uint16_t)(((0xe8>>3)<<11)|((0x7c>>2)<<5)|(0x92>>3));
 assert(pixels[(18+10)*800+6]==rose&&pixels[(309+10)*800+10]==rose);
 unsigned themed=0;for(int y=0;y<480;y++)for(int x=0;x<800;x++){
  if(!in_half(0,x,y))assert(pixels[y*800+x]==pair_base[y*800+x]);
  else if(pixels[y*800+x]==(uint16_t)(pair_plain[y*800+x]^0x0841))themed++;
 }
 assert(themed>50000);
 fixture_theme=0;
 assert(!active_holds&&acquires==releases);
 puts("PASS 2 DECKS pair frame: only halves both the wave and the snapshot put on DECK 3/4, repaint when a half goes back, route loss / not eligible / caution / four-deck hide, theme map with boxes kept, holds balanced");
 fixture_layout=XZ_LAYOUT_FOUR;
 xz_classic_mixer_runtime_stop();
 assert(!xz_classic_mixer_runtime_frame(pixels,800,800*480,1,0,1500,keys));
 assert(!active_holds&&acquires==releases);
 puts("PASS held analysis through drawing, route-down/caution/TWO/shutdown releases; actual Classic mixer runtime: guarded native fields, 10Hz refresh, captured geometry, overview copy/load invalidation under contention, side column and band rows, decks 3/4 NOT CONNECTED while down, caution repaint, shutdown");
 return 0;
}
