/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "native_wave_runtime.h"
#include "layout_cycle.h"
#include "extra_lane.h"
#include "extra_detail.h"
#include "classic_mixer_runtime.h"
#include "route_card.h"
#include "deck_view.h"
#include "native_skin_runtime.h"
#include "native_raw_skin.h"
#include "layered_wave_runtime.h"
#include "../runtime.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../stock_dialog.h"
#pragma weak xz_stock_dialog_active
#pragma weak xz_stock_counter_ids
static int stock_dialog_up(void){return xz_stock_dialog_active&&xz_stock_dialog_active();}
#include <sys/syscall.h>
#include "../clock_compat.h"
#include <time.h>
#include <unistd.h>

static int (*stock_lock)(void *,void **,int *);
static int (*stock_unlock)(void *);
static int (*requested)(void);
static xz_native_wave_render renderer;
static void *renderer_context;
static _Thread_local struct xz_native_wave_pair pair;
static _Thread_local uint16_t scratch[XZ_NATIVE_WAVE_PIXELS];
static _Thread_local struct {uintptr_t gr,hw,thread;uint16_t *pixels;int pitch,deck,x,y;} footer;
static _Thread_local uint16_t footer_scratch[300*34];
__attribute__((visibility("default"))) uint32_t xz_native_wave_skin_v1[4]={1};
static int transformed, drawn_layout, observed, observe_only, focus_verified;
/* Four decks: route_up from the draw thread's snapshot at the wave lock; four_drawn is the drawn
   lane order + 1 after a successful four-deck frame, the geometry touches map against. */
static int route_up, four_drawn, notice_up;
static _Thread_local int four_frame_order,four_frame_layout;
/* The DECK 3/4 detail panel (extra_detail.h): the touch thread opens, switches and closes it, and raises
   a refusal as (sequence << 8) | notice; the draw thread reads both once per four-deck frame. */
static int detail_deck=-1;
static uint32_t detail_notice;
static _Thread_local int four_frame_detail=-1;
static _Thread_local uint32_t detail_notice_seen,detail_notice_until;
static _Thread_local int detail_notice_shown;
static uint32_t monotonic_ms(void);
static _Thread_local struct xz_extra_decks extra;
static _Thread_local struct xz_extra_analysis_hold extra_hold;
static _Thread_local uintptr_t extra_hold_window;
static _Thread_local struct xz_extra_lane_marks extra_marks[XZ_EXTRA_DECKS];
/* DECK VIEW 4 DECKS with decks 3 and 4 down (route_card.h). The draw thread owns the frame's card; the
   touch thread reads what was painted (card_painted) and reports input (card_input_at, card_focus). */
static _Thread_local int four_frame_live,card_shown_last,card_state_last;
static _Thread_local uint32_t card_since;
/* One still card (about 140 KB) for the whole process, not one per thread: the draw thread that takes it
   copies from it; another thread meanwhile draws the card directly. */
static struct xz_route_card_cache card_cache;
static int card_cache_busy;
static _Thread_local struct xz_route_card_input card_frame;
static _Thread_local struct xz_extra_route_status card_status;
static uint32_t card_painted,card_input_at,card_success_at;
static int card_focus=-1,card_route_seen;
/* The DECK VIEW route-up banner (deck_view.h). The draw thread that sees the route come up keeps asking for
   up to 5 s (the connector may publish the new ready revision a frame later), takes the shared latch and
   publishes the notice; every draw thread reads it. The route going down ends it. */
static int ready_seen_up,ready_pending;
static uint32_t ready_edge_at,ready_from,ready_ms;
static const char *ready_text;
/* 2 DECKS shows the decks the sides control (extra_lane.h xz_two_frame). two_frame_pair: the pair this frame
   draws, from the snapshot taken at the lock; two_painted: the pair the last completed frame painted, for the
   side column, the bottom panels and touch; revoked with the play scene. */
static _Thread_local unsigned two_frame_pair;
static unsigned two_painted;
/* A side switching deck while the route stays up and 2 DECKS shows raises one banner saying where it went
   (deck_view.h xz_deck_view_pair_notice). Seen only while wave frames run back to back, so returning to the
   play screen after a switch elsewhere says nothing. Draw thread only. */
static unsigned pair_seen;
static int pair_seen_valid;
static uint32_t pair_seen_at,pair_from;
static const char *pair_text;
static _Thread_local uint8_t four_keep[XZ_FOUR_KEEP_BYTES];
struct xz_inline_proof { uint32_t version,captures,frames,skipped; };
__attribute__((visibility("default"))) struct xz_inline_proof xz_inline_proof_v1={1,0,0,0};

static int word(uint32_t address,uint32_t *out){return xz_read_memory(address,out,4);}
static int play_scene(struct xz_native_wave_scene *s){
    if(stock_dialog_up())return 0;
    uint32_t table,controls,index,visible,gr,set,clear;
    memset(s,0,sizeof(*s));
    if(word(0x3eba54c,&table)||table!=0x4d200c||
       word(0x3eba548,&controls)||controls!=0x4d1f30||word(controls+12,&set)||word(controls+16,&clear)||
       word(0x3c6fc7c,&index)||word(0x1b31f4c,&visible)||word(0x1b31f88,&gr)||!gr)return 0;
    unsigned char data[64];if(xz_read_memory(gr,data,sizeof(data)))return 0;
    uint32_t values[16];memcpy(values,data,sizeof(data));
    s->enabled=1;s->window_table=table;s->control_table=controls;s->control_set=set;s->control_clear=clear;
    s->scene_index=index;s->visible=visible;s->gr=gr;s->hw=values[7];
    s->width=values[0]&65535;s->height=values[0]>>16;s->pixel_type=values[2];s->flags=values[6];
    s->x=(int32_t)values[12];s->y=(int32_t)values[13];s->right=(int32_t)values[14];s->bottom=(int32_t)values[15];
    s->lifecycle_epoch=gr;
    return xz_native_wave_scene_valid(s);
}
/* DECK VIEW 4 DECKS draws four lanes whether or not decks 3 and 4 are up. */
static int four_view(void){return xz_deck_layout_get()!=XZ_LAYOUT_TWO;}
static int scene(struct xz_native_wave_scene *s){
 return ((requested&&requested())||xz_mods_native_style_v1()||xz_layered_wave_active()||
         __atomic_load_n(&route_up,__ATOMIC_ACQUIRE)||__atomic_load_n(&notice_up,__ATOMIC_ACQUIRE)||four_view())&&play_scene(s);
}
static void ready_route(int up,uint32_t now){
 if(__atomic_exchange_n(&ready_seen_up,up,__ATOMIC_ACQ_REL)!=up){
  __atomic_store_n(&ready_edge_at,now,__ATOMIC_RELAXED);__atomic_store_n(&ready_pending,up,__ATOMIC_RELEASE);
  if(!up)__atomic_store_n(&ready_ms,0,__ATOMIC_RELEASE);
 }
 if(!up||!__atomic_load_n(&ready_pending,__ATOMIC_ACQUIRE))return;
 if(now-__atomic_load_n(&ready_edge_at,__ATOMIC_RELAXED)>=5000u){__atomic_store_n(&ready_pending,0,__ATOMIC_RELEASE);return;}
 struct xz_deck_view_notice n;
 if(!xz_deck_view_route_notice(xz_deck_layout_get(),xz_extra_decks_route_status().ready_revision,now,&n))return;
 __atomic_store_n(&ready_pending,0,__ATOMIC_RELEASE);
 __atomic_store_n(&ready_ms,0,__ATOMIC_RELEASE);
 __atomic_store_n(&ready_text,n.text,__ATOMIC_RELAXED);__atomic_store_n(&ready_from,n.from_ms,__ATOMIC_RELAXED);
 __atomic_store_n(&ready_ms,n.ms,__ATOMIC_RELEASE);
}
static const char *ready_banner(uint32_t now){
 struct xz_deck_view_notice n;n.ms=__atomic_load_n(&ready_ms,__ATOMIC_ACQUIRE);
 n.text=__atomic_load_n(&ready_text,__ATOMIC_RELAXED);n.from_ms=__atomic_load_n(&ready_from,__ATOMIC_RELAXED);
 return xz_deck_view_notice_text(&n,now);
}
static unsigned sides_of(const struct xz_extra_decks *s){
 return s->route_up?(s->left_on_3?1u:0u)|(s->right_on_4?2u:0u):0u;
}
/* Route loss ends the banner: the players' own ROUTE LOST notice says what happened. */
static void pair_route(int up,unsigned sides,uint32_t now){
 if(!up){pair_seen_valid=0;pair_text=NULL;return;}
 if(pair_seen_valid&&now-pair_seen_at<500u&&sides!=pair_seen&&xz_deck_layout_get()==XZ_LAYOUT_TWO){
  pair_text=xz_deck_view_pair_notice(pair_seen,sides);pair_from=now;
 }
 pair_seen=sides;pair_seen_valid=1;pair_seen_at=now;
}
static const char *pair_banner(uint32_t now){
 return pair_text&&now-pair_from<XZ_DECK_VIEW_PAIR_NOTICE_MS?pair_text:NULL;
}
uint32_t xz_native_four_card(void){return xz_native_four_order()<0?0:__atomic_load_n(&card_painted,__ATOMIC_ACQUIRE);}
void xz_native_four_card_input(uint32_t now_ms,int focus){
 __atomic_store_n(&card_input_at,now_ms?now_ms:1,__ATOMIC_RELEASE);
 __atomic_store_n(&card_focus,focus,__ATOMIC_RELEASE);
}
uint32_t xz_native_four_card_success(uint32_t now_ms){
 uint32_t at=__atomic_load_n(&card_success_at,__ATOMIC_ACQUIRE);
 return at&&now_ms-at<(uint32_t)(XZ_ROUTE_CARD_READY_HOLD_MS+XZ_ROUTE_CARD_WIPE_MS)?at:0;
}
/* Draw thread, once per four-deck frame: the route coming up while the previous frame drew the card. */
static uint32_t card_success_step(int up,uint32_t now){
 int was=card_route_seen;card_route_seen=up?2:1;
 if(up&&was==1)__atomic_store_n(&card_success_at,now?now:1,__ATOMIC_RELEASE);
 else if(!up)__atomic_store_n(&card_success_at,0,__ATOMIC_RELEASE);
 return xz_native_four_card_success(now);
}
int xz_native_play_visible(void){
 uint32_t table,controls,index;
 return !word(0x3eba54c,&table)&&table==0x4d200c&&!word(0x3eba548,&controls)&&controls==0x4d1f30&&
        !word(0x3c6fc7c,&index)&&index==2;
}
int xz_native_four_order(void){
    struct xz_native_wave_scene current;
    if(!scene(&current)){
        __atomic_store_n(&four_drawn,0,__ATOMIC_RELEASE);xz_native_four_set_detail(-1);return -1;
    }
    return __atomic_load_n(&four_drawn,__ATOMIC_ACQUIRE)-1;
}
/* No memory reads while both sides are native (the usual case): the scene is probed only to revoke a pair. */
unsigned xz_native_two_pair(void){
    unsigned pair=__atomic_load_n(&two_painted,__ATOMIC_ACQUIRE);
    struct xz_native_wave_scene current;
    if(pair&&!scene(&current)){__atomic_store_n(&two_painted,0,__ATOMIC_RELEASE);return 0;}
    return pair;
}
int xz_native_four_mixer_order(void){return xz_deck_layout_get()==XZ_LAYOUT_MIXER;}
void xz_native_four_set_mixer_order(int mixer){xz_deck_layout_set(mixer?XZ_LAYOUT_MIXER:XZ_LAYOUT_FOUR);}
int xz_native_four_detail(void){return __atomic_load_n(&detail_deck,__ATOMIC_ACQUIRE);}
void xz_native_four_set_detail(int deck){
    int next=deck==0||deck==1?deck:-1;
    if(__atomic_exchange_n(&detail_deck,next,__ATOMIC_ACQ_REL)!=next)xz_native_four_detail_notice(XZ_EXTRA_DETAIL_NOTICE_NONE);
}
void xz_native_four_detail_notice(int notice){
    if(notice<XZ_EXTRA_DETAIL_NOTICE_NONE||notice>=XZ_EXTRA_DETAIL_NOTICES)return;
    uint32_t old=__atomic_load_n(&detail_notice,__ATOMIC_ACQUIRE),next;
    do next=(((old>>8)+1u)<<8)|(uint32_t)notice;
    while(!__atomic_compare_exchange_n(&detail_notice,&old,next,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE));
}
/* Draw thread: a newly raised refusal shows for 2.5 s from the first frame that sees it. */
static int detail_notice_now(uint32_t now){
    uint32_t word=__atomic_load_n(&detail_notice,__ATOMIC_ACQUIRE);
    if(word!=detail_notice_seen){detail_notice_seen=word;detail_notice_shown=(int)(word&255u);detail_notice_until=now+2500u;}
    return detail_notice_shown&&(int32_t)(now-detail_notice_until)<0?detail_notice_shown:0;
}
int xz_native_focus_deck(void){
    uint32_t deck;
    if(!focus_verified||word(0x02205d10,&deck)||deck<1||deck>2)return -1;
    return (int)deck-1;
}
int xz_native_inline_active(void){
    struct xz_native_wave_scene current;
    if(!scene(&current)){__atomic_store_n(&transformed,0,__ATOMIC_RELEASE);return 0;}
    return __atomic_load_n(&transformed,__ATOMIC_ACQUIRE)&&requested&&requested()==__atomic_load_n(&drawn_layout,__ATOMIC_ACQUIRE);
}
static int lock_hook(void *window,void **pixels,int *pitch){
    uintptr_t caller=(uintptr_t)__builtin_return_address(0);
    int result=stock_lock(window,pixels,pitch);
    xz_native_raw_skin_locked(window,pixels,pitch,caller,result);
    if((caller==0x22ee4c||caller==0x22f7bc)&&(xz_mods_native_style_v1()||xz_layered_wave_active()||
        __atomic_load_n(&route_up,__ATOMIC_ACQUIRE)||four_view())){
        memset(&footer,0,sizeof(footer));uint32_t values[16];
        if(result==0&&pixels&&*pixels&&pitch&&*pitch>=600&&*pitch<=4096&&!(*pitch&1)&&!((uintptr_t)*pixels&1)&&
           !xz_read_memory((uint32_t)(uintptr_t)window,values,sizeof(values))&&
           (values[0]&65535)==300&&(values[0]>>16)==34&&values[2]==9&&values[7]){
            footer.gr=(uintptr_t)window;footer.hw=values[7];footer.thread=(uintptr_t)pthread_self();footer.pixels=*pixels;footer.pitch=*pitch;footer.deck=(int32_t)values[12]>=400;
            footer.x=(int32_t)values[12];footer.y=(int32_t)values[13];
        }else __atomic_fetch_add(&xz_native_wave_skin_v1[3],1,__ATOMIC_RELAXED);
    }
    int captured_extra=0;
    if(result==0&&caller==0x1fd058){
        xz_extra_decks_release_analysis(&extra,&extra_hold);extra_hold_window=0;
        int up=xz_extra_decks_snapshot_held(&extra,&extra_hold)!=0;uint32_t now=monotonic_ms();
        __atomic_store_n(&route_up,up,__ATOMIC_RELEASE);
        ready_route(up,now);
        pair_route(up,sides_of(&extra),now);
        __atomic_store_n(&notice_up,xz_extra_notice_active(&extra,now)||ready_banner(now)||pair_banner(now),__ATOMIC_RELEASE);
    }
    struct xz_native_wave_scene current;
    if(result==0&&caller==0x1fd058&&scene(&current)&&current.gr==(uintptr_t)window&&pixels&&pitch&&
       xz_native_wave_capture(&pair,&current,caller,(uintptr_t)pthread_self(),*pixels,*pitch)){
        captured_extra=1;extra_hold_window=current.hw;
        __atomic_fetch_add(&xz_inline_proof_v1.captures,1,__ATOMIC_RELAXED);
        four_frame_layout=xz_deck_layout_get();
        four_frame_order=four_frame_layout==XZ_LAYOUT_MIXER?XZ_FOUR_MIXER:XZ_FOUR_STACKED;
        xz_layered_wave_begin(*pixels);
        if(!__atomic_exchange_n(&observed,1,__ATOMIC_ACQ_REL))
            xz_log("INLINE_WAVE_PROOF: native play scene=2 window=536x268 at=131,18 pitch=1072 caller=1fd058");
    }
    if(result==0&&caller==0x1fd058&&!captured_extra)xz_extra_decks_release_analysis(&extra,&extra_hold);
    return result;
}
static void style_main_wave(uint16_t *pixels,uint32_t count){
 if(count!=XZ_NATIVE_WAVE_PIXELS)return;
 xz_layered_wave_protect(pixels);
 xz_native_skin_wave_pixels(pixels,536,268,-1);
 xz_layered_wave_restore(pixels);
}
/* The route-up banner, else the newer of a side's switch and the player's notice, in the same place. */
static void notice_banner(uint16_t *pixels,uint32_t count){
 uint32_t now=monotonic_ms();const char *ready=ready_banner(now),*side=pair_banner(now);
 if(ready)xz_extra_banner_draw(pixels,536,count,ready);
 else if(side&&!(xz_extra_notice_active(&extra,now)&&(int32_t)(extra.notice_at_ms-pair_from)>0))xz_extra_banner_draw(pixels,536,count,side);
 else xz_extra_notice_draw(pixels,536,count,&extra,now);
}
static void style_main_notice(uint16_t *pixels,uint32_t count){
 if(xz_mods_native_style_v1())style_main_wave(pixels,count);
 notice_banner(pixels,count);
}
/* The raw syscall, as shell_runtime.c: clock_gettime is GLIBC_2.17 and librt is not an allowed dependency. */
static uint32_t monotonic_ms(void){
 struct timespec t;if(xz_clock_gettime_compat(CLOCK_MONOTONIC,&t))return 0;
 return (uint32_t)((uint64_t)t.tv_sec*1000u+(uint64_t)t.tv_nsec/1000000u);
}
/* The card's input this frame: the runtime's route observation, its rest timer and the encoder focus. A
   READY report this frame's snapshot has not caught up with reads as starting. */
static void card_prepare(uint32_t now){
 uint32_t success=card_success_step(four_frame_live,now);
 card_frame=(struct xz_route_card_input){0};card_frame.focus=-1;
 if(four_frame_live&&!success){card_shown_last=0;return;}
 card_status=xz_extra_decks_route_status();
 int state=four_frame_live?XZ_EXTRA_ROUTE_READY:card_status.state==XZ_EXTRA_ROUTE_READY?XZ_EXTRA_ROUTE_STARTING:(int)card_status.state;
 if(!card_shown_last||state!=card_state_last)card_since=now?now:1;
 card_shown_last=1;card_state_last=state;
 uint32_t input=__atomic_load_n(&card_input_at,__ATOMIC_ACQUIRE);
 card_frame.state=state;card_frame.fail_part=card_status.fail_part;card_frame.reason=card_status.reason;
 card_frame.now_ms=now;card_frame.rest_from_ms=input&&(int32_t)(input-card_since)>0?input:card_since;
 card_frame.ready_at_ms=success;card_frame.focus=__atomic_load_n(&card_focus,__ATOMIC_ACQUIRE);
 /* The picture steps at 30 fps here: the wave surface's own rate is not known, so it is never asked for 60. */
 card_frame.step_ms=33u;
}
/* Both DECK 3/4 lanes, the native lanes' deck tags and the open detail panel (extra_detail.h), before the
   theme. Decks 3 and 4 down: the native tags and a dark tint in their lanes; the card comes after the theme. */
static void draw_extra(uint16_t *pixels,uint32_t count){
 uint32_t now=monotonic_ms();
 four_frame_detail=four_frame_live?__atomic_load_n(&detail_deck,__ATOMIC_ACQUIRE):-1;
 card_prepare(now);
 struct xz_four_frame frame={&extra,four_frame_order,now,0,1,four_frame_detail,detail_notice_now(now),!four_frame_live};
 xz_layered_wave_view(&frame.column_us,&frame.background);
 xz_four_frame_draw(pixels,count,&frame,extra_marks);
}
/* 2 DECKS with a side on DECK 3 or 4: its half before the theme, the one theme pass with the native decks'
   and the software halves' layer pixels kept, then the banner over the finished surface. */
static void draw_two(uint16_t *pixels,uint32_t count){
 struct xz_two_frame f={&extra,two_frame_pair,monotonic_ms(),0,1};
 xz_layered_wave_view(&f.column_us,&f.background);
 xz_two_frame_draw(pixels,count,&f,extra_marks);
}
static void style_two(uint16_t *pixels,uint32_t count){
 if(count!=XZ_NATIVE_WAVE_PIXELS||!xz_mods_native_style_v1())return;
 xz_layered_wave_protect_decks(pixels,~two_frame_pair&3u);
 xz_two_protect(two_frame_pair,extra_marks,pixels,four_keep);
 xz_native_skin_wave_pixels(pixels,536,268,-1);
 xz_layered_wave_restore(pixels);
 xz_two_restore(two_frame_pair,extra_marks,pixels,four_keep);
}
static uint32_t card_word(void){
 if(!xz_route_card_visible(&card_frame))return 0;
 return XZ_NATIVE_CARD_SHOWN|(uint32_t)(four_frame_order&7)<<1|(uint32_t)(card_frame.state&15)<<4|
  (card_frame.ready_at_ms?XZ_NATIVE_CARD_SUCCESS:0u);
}
/* The theme over the whole surface with layer pixels kept exact, then the player's notice banner. An open
   panel carries the notice in its own status row instead, so the banner never covers its controls. */
static void style_four(uint16_t *pixels,uint32_t count){
 if(count!=XZ_NATIVE_WAVE_PIXELS)return;
 if(xz_mods_native_style_v1()){
  struct xz_four_layout g;xz_four_layout_get(four_frame_order,&g);
  struct xz_four_marks m;memset(&m,0,sizeof(m));
  for(unsigned d=0;d<2;d++)m.col[d]=xz_layered_wave_drawn(d,&m.look[d]);
  for(unsigned i=0;i<XZ_EXTRA_DECKS;i++){m.look[2+i]=extra_marks[i].look;m.col[2+i]=four_frame_live?extra_marks[i].col:NULL;}
  xz_four_protect(&g,&m,pixels,four_keep);
  xz_native_skin_wave_pixels(pixels,536,268,-1);
  xz_four_restore(&g,&m,pixels,four_keep);
  /* The deck badges keep their own colours in every theme (deck_colour.h). */
  struct xz_four_frame frame={&extra,four_frame_order,0,0,0,four_frame_detail,0,!four_frame_live};
  xz_four_identity_draw(pixels,count,&frame);
 }
 /* The card after the theme, in its own colours; it says what a route notice would. */
 if(xz_route_card_visible(&card_frame)){
  struct xz_four_layout g;xz_four_layout_get(four_frame_order,&g);
  int cached=!__atomic_exchange_n(&card_cache_busy,1,__ATOMIC_ACQUIRE);
  xz_route_card_draw((struct xz_canvas){pixels,XZ_WAVE_WIDTH,XZ_WAVE_WIDTH,XZ_WAVE_HEIGHT,0},&xz_route_card_classic,
   (int)g.lane_y[2],(int)g.lane_y[3],&card_frame,cached?&card_cache:NULL);
  if(cached)__atomic_store_n(&card_cache_busy,0,__ATOMIC_RELEASE);
 }else if(four_frame_detail<0)notice_banner(pixels,count);
}
static int unlock_hook(void *window){
    xz_native_raw_skin_before_unlock(window);
    if(stock_dialog_up()||(xz_stock_counter_ids&&(xz_stock_counter_ids(NULL)&(1u<<(unsigned)footer.deck))))memset(&footer,0,sizeof footer);
    if(footer.hw==(uintptr_t)window&&footer.thread==(uintptr_t)pthread_self()&&footer.pixels){
        uint32_t values[16];
        if(!xz_read_memory((uint32_t)footer.gr,values,sizeof(values))&&values[7]==footer.hw&&
           (values[0]&65535)==300&&(values[0]>>16)==34&&values[2]==9){
            for(int y=0;y<34;y++)memcpy(footer_scratch+y*300,(unsigned char*)footer.pixels+y*footer.pitch,600);
            int layered=xz_layered_overview(footer_scratch,(unsigned)footer.deck);
            if(xz_mods_native_style_v1())xz_native_skin_wave_pixels(footer_scratch,300,34,footer.deck);
            if(layered)xz_layered_overview_restore(footer_scratch);
            xz_classic_mixer_native_overview(footer.deck,footer_scratch,300u*34u,footer.x,footer.y);
            for(int y=0;y<34;y++)memcpy((unsigned char*)footer.pixels+y*footer.pitch,footer_scratch+y*300,600);
            __atomic_fetch_add(&xz_native_wave_skin_v1[2],1,__ATOMIC_RELAXED);
        }
        memset(&footer,0,sizeof(footer));
    }
    if(pair.captured&&pair.scene.hw==(uintptr_t)window){
        xz_layered_wave_end();
        struct xz_native_wave_scene current;int drawn=0;
        /* Four decks hide the stem rows; the saved layout returns on the first frame after. DECK VIEW alone
           chooses four lanes; decks 3 and 4 are live only while their route is up. */
        int four=four_frame_layout!=XZ_LAYOUT_TWO;
        four_frame_live=__atomic_load_n(&route_up,__ATOMIC_ACQUIRE);
        /* 2 DECKS shows the decks the sides control: a side on DECK 3 or 4 replaces its stock half. */
        two_frame_pair=four?0u:sides_of(&extra);
        int inline_rows=four||!requested?XZ_STEM_LAYOUT_HIDDEN:requested();
        /* Two decks again, or decks 3 and 4 down: the DECK 3/4 panel has nothing to show and closes. */
        if(!four||!four_frame_live)xz_native_four_set_detail(-1);
        if(!four)card_route_seen=0;
        if(scene(&current)&&!observe_only){
            if(four)drawn=xz_native_wave_finish_four(&pair,&current,(uintptr_t)pthread_self(),(uintptr_t)window,
                scratch,XZ_NATIVE_WAVE_PIXELS,four_frame_order,draw_extra,style_four);
            else if(two_frame_pair)drawn=xz_native_wave_finish_pair(&pair,&current,(uintptr_t)pthread_self(),(uintptr_t)window,
                scratch,XZ_NATIVE_WAVE_PIXELS,renderer,renderer_context,inline_rows,two_frame_pair,draw_two,style_two,notice_banner);
            else drawn=xz_native_wave_finish_styled(&pair,&current,(uintptr_t)pthread_self(),(uintptr_t)window,
                scratch,XZ_NATIVE_WAVE_PIXELS,renderer,renderer_context,
                (xz_mods_native_style_v1()||__atomic_load_n(&notice_up,__ATOMIC_ACQUIRE))?style_main_notice:NULL,inline_rows);
            if(drawn&&xz_mods_native_style_v1())__atomic_fetch_add(&xz_native_wave_skin_v1[1],1,__ATOMIC_RELAXED);
        }
        else xz_native_wave_reset(&pair);
        __atomic_store_n(&drawn_layout,drawn?inline_rows:0,__ATOMIC_RELEASE);
        __atomic_store_n(&card_painted,drawn&&four?card_word():0u,__ATOMIC_RELEASE);
        __atomic_store_n(&four_drawn,drawn&&four?four_frame_order+1:0,__ATOMIC_RELEASE);
        __atomic_store_n(&two_painted,drawn?two_frame_pair:0u,__ATOMIC_RELEASE);
        __atomic_store_n(&transformed,drawn&&inline_rows,__ATOMIC_RELEASE);
        if(drawn)__atomic_fetch_add(&xz_inline_proof_v1.frames,1,__ATOMIC_RELAXED);
        else __atomic_fetch_add(&xz_inline_proof_v1.skipped,1,__ATOMIC_RELAXED);
    }
    if(extra_hold_window==(uintptr_t)window){
        xz_extra_decks_release_analysis(&extra,&extra_hold);extra_hold_window=0;
        for(unsigned i=0;i<XZ_EXTRA_DECKS;i++)extra_marks[i].look.bands=NULL;
    }
    return stock_unlock(window);
}
int xz_native_inline_start(int (*enabled)(void),xz_native_wave_render render,void *context){
    static const unsigned char lock_guard[8]={0x38,0x40,0x2d,0xe9,0x00,0x40,0xa0,0xe1};
    static const unsigned char unlock_guard[8]={0x10,0x40,0x2d,0xe9,0x58,0xd0,0x4d,0xe2};
    static const unsigned char focus_guard[16]={0x10,0x3d,0x05,0xe3,0x20,0x32,0x40,0xe3,0x00,0x00,0x93,0xe5,0x1e,0xff,0x2f,0xe1};
    unsigned char actual[16];
    focus_verified=!xz_read_memory(0xf3cc4,actual,sizeof(actual))&&!memcmp(actual,focus_guard,sizeof(actual));
    requested=enabled;renderer=render;renderer_context=context;
    observe_only=0; /* Persisted row layout is the sole visibility choice. */
    if(!stock_lock&&xz_hook_arm(0x156958,lock_guard,(void *)lock_hook,(void **)&stock_lock))return -1;
    if(!stock_unlock&&xz_hook_arm(0x1656c4,unlock_guard,(void *)unlock_hook,(void **)&stock_unlock))return -1;
    static const unsigned char gr_unlock_guard[8]={0x5c,0x30,0x9f,0xe5,0x10,0x40,0x2d,0xe9};
    if(xz_read_memory(0x1569c0,actual,8)||memcmp(actual,gr_unlock_guard,8))return -1;
    int result=xz_native_raw_skin_start(stock_lock,(int(*)(void*))(uintptr_t)0x1569c0);
    if(!result&&xz_layered_wave_start())xz_log("Layered waveform unavailable: native column ABI guard failed");
    return result;
}
