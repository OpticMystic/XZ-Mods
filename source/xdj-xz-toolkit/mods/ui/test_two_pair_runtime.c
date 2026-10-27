/* SPDX-License-Identifier: MIT */
/* 2 DECKS follows the sides (FOUR_DECK.md): the real wave transaction (native_wave_runtime.c lock and unlock
   hooks) over a fixture stock surface, offline. A side on DECK 3 or 4 replaces exactly its half; both native
   sides, the route down and a revoked scene leave the stock frame untouched; the painted pair is published for
   the overlay and touch and revoked with the scene; a switch raises one banner. */
#define _GNU_SOURCE
#include "../clock_compat.h"
static unsigned fake_ms=1000;
static int fake_clock(clockid_t clock,struct timespec *out){
 (void)clock;out->tv_sec=fake_ms/1000;out->tv_nsec=(long)(fake_ms%1000)*1000000L;return 0;
}
#define xz_clock_gettime_compat fake_clock
#include <stdint.h>
static uintptr_t test_caller=0x1fd058;
#define __builtin_return_address(depth) ((void*)test_caller)
#include "native_wave_runtime.c"
#undef __builtin_return_address
#undef xz_clock_gettime_compat
#include <assert.h>
#ifdef NDEBUG
#error 2 DECKS pair acceptance requires active assertions
#endif

#define GR 0x5000u
#define HW 0x6000u
static uint16_t surface[536*268],stock_frame[536*268];
static int scene_index=2,theme,layout=XZ_LAYOUT_TWO,stems=XZ_STEM_LAYOUT_HIDDEN;
static unsigned luts,protects,protect_masks[8],renders;
static struct xz_extra_decks fixture;
static unsigned holds;
static struct xz_extra_column columns[3000];

int xz_read_memory(uint32_t address,void *out,size_t size){
 uint32_t v;
 if(size==64&&address==GR){
  uint32_t values[16]={0};values[0]=536|(268u<<16);values[2]=9;values[6]=0x41000000u;values[7]=HW;
  values[12]=131;values[13]=18;values[14]=667;values[15]=286;memcpy(out,values,64);return 0;
 }
 if(size==8&&address==0x1569c0){static const unsigned char g[8]={0x5c,0x30,0x9f,0xe5,0x10,0x40,0x2d,0xe9};memcpy(out,g,8);return 0;}
 if(size!=4)return -1;
 switch(address){
 case 0x3eba54c:v=0x4d200c;break;case 0x3eba548:v=0x4d1f30;break;
 case 0x4d1f30+12:v=0x1fc2fc;break;case 0x4d1f30+16:v=0x1fdbe4;break;
 case 0x3c6fc7c:v=(uint32_t)scene_index;break;case 0x1b31f4c:v=1;break;case 0x1b31f88:v=GR;break;
 default:return -1;
 }
 memcpy(out,&v,4);return 0;
}
int xz_hook_arm(uint32_t a,const unsigned char g[8],void *r,void **o){(void)a;(void)g;(void)r;(void)o;return 0;}
void xz_log(const char *s){(void)s;}
int xz_native_raw_skin_start(int(*a)(void*,void**,int*),int(*b)(void*)){(void)a;(void)b;return 0;}
void xz_native_raw_skin_locked(void *w,void **p,int *s,uintptr_t c,int r){(void)w;(void)p;(void)s;(void)c;(void)r;}
void xz_native_raw_skin_before_unlock(void *w){(void)w;}
int xz_mods_native_style_v1(void){return theme;}
/* A stand-in theme map: flips the low bits of every pixel. */
void xz_native_skin_wave_pixels(uint16_t *p,unsigned w,unsigned h,int deck){
 assert(w==536&&h==268&&deck==-1);luts++;for(unsigned i=0;i<w*h;i++)p[i]^=0x0841;
}
int xz_layered_wave_start(void){return 0;}
int xz_layered_wave_enabled(void){return 0;}
int xz_layered_wave_active(void){return 0;}
void xz_layered_wave_begin(uint16_t *p){(void)p;}
void xz_layered_wave_end(void){}
void xz_layered_wave_view(uint32_t *column_us,unsigned *background){*column_us=0;*background=1;}
const struct xz_layered_col *xz_layered_wave_drawn(unsigned deck,struct xz_layered_data *look){(void)deck;(void)look;return NULL;}
void xz_layered_wave_protect(const uint16_t *p){xz_layered_wave_protect_decks(p,3u);}
void xz_layered_wave_protect_decks(const uint16_t *p,unsigned decks){(void)p;protect_masks[protects++&7]=decks;}
void xz_layered_wave_restore(uint16_t *p){(void)p;}
int xz_layered_overview(uint16_t *p,unsigned deck){(void)p;(void)deck;return 0;}
void xz_layered_overview_restore(uint16_t *p){(void)p;}
void xz_classic_mixer_native_overview(int d,const uint16_t *p,size_t n,int x,int y){(void)d;(void)p;(void)n;(void)x;(void)y;}
int xz_deck_layout_get(void){return layout;}
void xz_deck_layout_set(int l){layout=l;}
int xz_extra_decks_snapshot_held(struct xz_extra_decks *out,struct xz_extra_analysis_hold *hold){
 assert(!holds);*out=fixture;hold->analysis[0]=columns;holds=1;return out->route_up;
}
void xz_extra_decks_release_analysis(struct xz_extra_decks *out,struct xz_extra_analysis_hold *hold){
 if(hold->analysis[0]){assert(holds);holds=0;hold->analysis[0]=NULL;}
 for(int i=0;i<2;i++){out->deck[i].wave.columns=NULL;out->deck[i].grid=NULL;}
}
struct xz_extra_route_status xz_extra_decks_route_status(void){
 struct xz_extra_route_status s={0};s.state=fixture.route_up?XZ_EXTRA_ROUTE_READY:XZ_EXTRA_ROUTE_NOT_DETECTED;return s;
}
static int stock_lock_fixture(void *w,void **pixels,int *pitch){assert(w==(void*)(uintptr_t)GR);*pixels=surface;*pitch=1072;return 0;}
static int stock_unlock_fixture(void *w){assert(w==(void*)(uintptr_t)HW);return 0;}
static int stem_layout(void){return stems;}
static int render_rows(void *context,uint16_t *rows,size_t count,size_t stride,int width,int height){
 (void)context;(void)stride;(void)width;(void)height;renders++;for(size_t i=0;i<count;i++)rows[i]=0x07e0;return 1;
}

/* One stock frame: stock clears and redraws the whole surface, then the hooks run. */
static void frame(unsigned ms){
 fake_ms=ms;memcpy(surface,stock_frame,sizeof surface);
 void *pixels=NULL;int pitch=0;
 assert(lock_hook((void*)(uintptr_t)GR,&pixels,&pitch)==0&&pixels==surface);
 assert(unlock_hook((void*)(uintptr_t)HW)==0);
 assert(!holds);
}
static int stock_rows(unsigned first,unsigned last){
 for(unsigned y=first;y<=last;y++)if(memcmp(surface+y*536,stock_frame+y*536,536*2))return 0;
 return 1;
}
static void deck_fixture(void){
 for(unsigned i=0;i<3000;i++)columns[i]=(struct xz_extra_column){(uint8_t)(60+i%50),40,(uint8_t)(20+i%9)};
 memset(&fixture,0,sizeof fixture);
 for(int i=0;i<2;i++){
  struct xz_extra_deck *d=&fixture.deck[i];
  d->known=UINT32_MAX;d->loaded=1;d->elapsed_ms=10000;d->total_ms=20000;d->elapsed_at_ms=1000;d->playing=i==0;
  d->bpm_x100=12000;d->wave=(struct xz_extra_wave){columns,3000,20000,150};
 }
}
static void sides(int up,int left,int right){fixture.route_up=up;fixture.left_on_3=up&&left;fixture.right_on_4=up&&right;}
/* The software lane drew its playhead at column 268 through every row of the half. */
static int lane_in(unsigned first,unsigned rows,uint16_t playhead){
 for(unsigned r=0;r<rows;r++)if(surface[(first+r)*536+268]!=playhead)return 0;
 return 1;
}
static unsigned banner_pixels(void){
 unsigned n=0;for(unsigned y=244;y<266;y++)for(unsigned x=6;x<530;x++)n+=surface[y*536+x]==0xfcc6; /* the banner's orange edge */
 return n;
}

int main(void){
 for(unsigned i=0;i<536*268;i++)stock_frame[i]=(uint16_t)(0x1000+i*7919u%4096u);
 stock_lock=stock_lock_fixture;stock_unlock=stock_unlock_fixture;
 assert(!xz_native_inline_start(stem_layout,render_rows,NULL));
 deck_fixture();

 /* Frames far apart (more than 500 ms) adopt a new pair without a switch banner. */
 /* Both native, route down and up: the stock frame, nothing published. */
 sides(0,0,0);frame(1000);assert(xz_native_two_pair()==0&&stock_rows(0,267));
 /* No style, no rows, no notice: the transaction leaves stock alone (route up still admits the scene). */
 sides(1,0,0);frame(1600);assert(xz_native_two_pair()==0&&stock_rows(0,267));
 /* Left on DECK 3: rows 0..131 are DECK 3's whole lane (red playhead: playing), the right half stock. */
 sides(1,1,0);frame(2200);
 assert(xz_native_two_pair()==1u&&lane_in(0,132,0xf800)&&stock_rows(132,267));
 /* Right on DECK 4 as well: rows 132..135 black, 136..267 DECK 4 (paused: white playhead). */
 sides(1,1,1);frame(2800);
 assert(xz_native_two_pair()==3u&&lane_in(0,132,0xf800)&&lane_in(136,132,0xffff));
 for(unsigned i=132*536;i<136*536;i++)assert(surface[i]==0);
 /* Only the right side on DECK 4. */
 sides(1,0,1);frame(3400);
 assert(xz_native_two_pair()==2u&&stock_rows(0,131)&&lane_in(136,132,0xffff));
 /* A four-deck view never draws halves. */
 layout=XZ_LAYOUT_FOUR;frame(4000);assert(xz_native_two_pair()==0);layout=XZ_LAYOUT_TWO;
 puts("PASS 2 DECKS pair: each side on DECK 3/4 replaces exactly its half, the other half stays stock; published pair per frame");

 /* Route loss mid-view: the next frame is the stock frame again and nothing is published. */
 sides(1,1,1);frame(5000);assert(xz_native_two_pair()==3u);
 sides(0,0,0);frame(5016);
 assert(xz_native_two_pair()==0&&stock_rows(0,267));
 /* The play scene going revokes a painted pair at once, until a frame paints again. */
 sides(1,1,0);frame(5600);assert(xz_native_two_pair()==1u);
 scene_index=29;assert(xz_native_two_pair()==0);scene_index=2;
 assert(xz_native_two_pair()==0);
 frame(6200);assert(xz_native_two_pair()==1u);
 puts("PASS route loss and a hidden play scene return the stock frame and revoke the published pair");

 /* Stem rows: the native side keeps its layout and rows, the software half is neither resampled nor given rows. */
 stems=XZ_STEM_LAYOUT_BELOW;sides(1,1,0);renders=0;frame(7000);
 struct xz_wave_layout g;assert(xz_wave_layout_get(XZ_STEM_LAYOUT_BELOW,&g));
 assert(renders==1&&lane_in(0,132,0xf800));
 for(unsigned y=g.control_y[0];y<g.control_y[0]+g.control_height;y++)assert(surface[y*536+20]!=0x07e0);
 for(unsigned y=g.control_y[1];y<g.control_y[1]+g.control_height;y++)for(unsigned x=0;x<536;x++)assert(surface[y*536+x]==0x07e0);
 static uint16_t expect[536*268];memcpy(expect,stock_frame,sizeof expect);
 assert(xz_wave_layout_pixels(expect,536*268,536,XZ_STEM_LAYOUT_BELOW));
 for(unsigned y=g.lane_y[1];y<g.lane_y[1]+g.lane_height;y++)assert(!memcmp(surface+y*536,expect+y*536,536*2));
 stems=XZ_STEM_LAYOUT_OUTER;sides(1,0,1);frame(7600);assert(xz_wave_layout_get(XZ_STEM_LAYOUT_OUTER,&g));
 assert(lane_in(136,132,0xffff));
 for(unsigned y=g.control_y[0];y<g.control_y[0]+g.control_height;y++)for(unsigned x=0;x<536;x++)assert(surface[y*536+x]==0x07e0);
 stems=XZ_STEM_LAYOUT_HIDDEN;
 puts("PASS the native side keeps its stem layout and rows; the software half is a whole lane without rows");

 /* Theme: one pass over the whole surface, native layered protect limited to the native side. */
 theme=1;luts=protects=0;sides(1,1,0);frame(8200);
 assert(luts==1&&protects==1&&protect_masks[0]==2u);
 for(unsigned y=136;y<268;y++)for(unsigned x=0;x<536;x++)assert(surface[y*536+x]==(uint16_t)(stock_frame[y*536+x]^0x0841));
 /* The software lane's layer pixels survive the theme; its playhead is themed like the native marks. */
 unsigned exact=0;for(unsigned x=300;x<536;x++)exact+=surface[65*536+x]==0xffff;
 assert(exact>100&&surface[10*536+268]==(uint16_t)(0xf800^0x0841));
 luts=0;sides(1,0,0);frame(8800);assert(luts==1&&!stock_rows(0,0));
 theme=0;
 puts("PASS one theme pass per pair frame, native layers kept for the native side only, software layers exact");

 /* Switch notices: a side switching while frames run says where it went, for 2 s; returning to native too. */
 sides(1,0,0);frame(10000);frame(10016);
 unsigned plain=banner_pixels();
 sides(1,1,0);frame(10032);assert(pair_banner(10032)&&!strcmp(pair_banner(10032),"LEFT: DECK 3"));
 assert(banner_pixels()>plain+100);
 frame(11500);assert(pair_banner(11500));
 frame(12040);assert(!pair_banner(12040)&&xz_native_two_pair()==1u);
 sides(1,0,0);frame(12056);assert(!strcmp(pair_banner(12056),"LEFT: DECK 1"));
 assert(xz_native_two_pair()==0&&!stock_rows(244,265)&&stock_rows(0,243)&&stock_rows(266,267));
 frame(14100);assert(!pair_banner(14100)&&stock_rows(0,267));
 sides(1,1,1);frame(14116);assert(!strcmp(pair_banner(14116),"LEFT: DECK 3  RIGHT: DECK 4"));
 sides(1,1,0);frame(14132);assert(!strcmp(pair_banner(14132),"RIGHT: DECK 2"));
 /* A gap in the frames (the play screen was away) adopts the new pair without a notice. */
 frame(17000);assert(!pair_banner(17000));sides(1,0,0);frame(17900);assert(!pair_banner(17900));
 /* Route loss ends a banner at once; the players' ROUTE LOST notice is theirs to show. */
 frame(17916);sides(1,1,0);frame(17932);assert(pair_banner(17932));
 sides(0,0,0);frame(17948);assert(!pair_banner(17948));
 sides(1,0,0);frame(17964);frame(17980);assert(!pair_banner(17980));
 /* Four-deck views say nothing: the lanes do not move. */
 layout=XZ_LAYOUT_FOUR;sides(1,1,0);frame(17996);assert(!pair_banner(17996));layout=XZ_LAYOUT_TWO;
 frame(18012);assert(!pair_banner(18012));
 /* The newer of a switch and a player notice is the one drawn. */
 sides(1,0,0);frame(18028);assert(pair_banner(18028));
 frame(18044);static uint16_t switch_banner[536*22];memcpy(switch_banner,surface+244*536,sizeof switch_banner);
 snprintf(fixture.notice,sizeof fixture.notice,"DECK 3 REFUSED");fixture.notice_seq=1;fixture.notice_at_ms=18050;
 frame(18060);assert(pair_banner(18060)&&memcmp(switch_banner,surface+244*536,sizeof switch_banner));
 fixture.notice_at_ms=18000;frame(18076);assert(!memcmp(switch_banner,surface+244*536,sizeof switch_banner));
 puts("PASS switch banner: LEFT/RIGHT/both texts, 2 s, back to native too, frame gaps and four-deck views silent, route loss ends it, newer notice wins");
 return 0;
}
