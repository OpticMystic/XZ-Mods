/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error Route card acceptance requires active assertions
#endif
#include "route_card.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { W=800, H=480, SENTINEL=0x5aa5 };
static uint16_t pixels[W*H], other[W*H];
static struct xz_route_card_cache cache;
static struct xz_canvas canvas(uint16_t *p) { return (struct xz_canvas){p,W,W,H,0}; }
static void clear(uint16_t *p) { for(int i=0;i<W*H;i++)p[i]=SENTINEL; }
static uint32_t fnv(const uint16_t *p) { uint32_t h=2166136261u;for(int i=0;i<W*H;i++){h^=p[i];h*=16777619u;}return h; }
static int inside(struct xz_route_card_rect r, int x, int y) { return x>=r.x&&y>=r.y&&x<r.x+r.w&&y<r.y+r.h; }
/* Pixels that differ between the two buffers all lie in r. */
static int diff_within(struct xz_route_card_rect r) {
 int n=0;
 for(int y=0;y<H;y++)for(int x=0;x<W;x++)if(pixels[y*W+x]!=other[y*W+x]){assert(inside(r,x,y));n++;}
 return n;
}
static const char *reason_of(int state) {
 switch(state){
 case XZ_EXTRA_ROUTE_FAILED:return "The cable is in USB 2. Move it to USB 1 and keep music in USB 2.";
 case XZ_EXTRA_ROUTE_STARTING:return "The cable was found and the audio input is starting.";
 case XZ_EXTRA_ROUTE_LOST:return "The audio route was disconnected; check the loopback cable.";
 case XZ_EXTRA_ROUTE_NO_ENGINE:return "The software players are unavailable or have not been started.";
 case XZ_EXTRA_ROUTE_SCANNING:return "Checking the loopback cable and audio route now.";
 default:return NULL;
 }
}
static struct xz_route_card_input input(int state, int part, uint32_t now) {
 return (struct xz_route_card_input){state,part,reason_of(state),now,100000u,0,-1,0};
}
/* The card's rows: joined in number order, two strips in mixer order. */
struct lanes { int ya, yb; };
static const struct lanes modern_lanes[2]={{182,260},{26,260}},classic_lanes[2]={{136,204},{0,204}};

static void only_card(const struct xz_route_card_style *s, struct lanes l) {
 struct xz_route_card_rect a=xz_route_card_row(s,l.ya,l.yb,0),b=xz_route_card_row(s,l.ya,l.yb,1);
 for(int y=0;y<H;y++)for(int x=0;x<W;x++)if(!inside(a,x,y)&&!inside(b,x,y))assert(pixels[y*W+x]==SENTINEL);
}
static void test_kinds(void) {
 assert(xz_route_card_kind(XZ_EXTRA_ROUTE_NO_ENGINE)==XZ_ROUTE_CARD_NO_ENGINE&&xz_route_card_kind(XZ_EXTRA_ROUTE_NOT_DETECTED)==XZ_ROUTE_CARD_NOT_DETECTED);
 assert(xz_route_card_kind(XZ_EXTRA_ROUTE_SCANNING)==XZ_ROUTE_CARD_SCANNING&&xz_route_card_kind(XZ_EXTRA_ROUTE_STARTING)==XZ_ROUTE_CARD_STARTING);
 assert(xz_route_card_kind(XZ_EXTRA_ROUTE_WAITING_NATIVE_STOP)==XZ_ROUTE_CARD_WAITING&&xz_route_card_kind(XZ_EXTRA_ROUTE_READY)==XZ_ROUTE_CARD_READY);
 assert(xz_route_card_kind(XZ_EXTRA_ROUTE_FAILED)==XZ_ROUTE_CARD_FAILED&&xz_route_card_kind(XZ_EXTRA_ROUTE_LOST)==XZ_ROUTE_CARD_LOST);
 assert(xz_route_card_kind(99)==XZ_ROUTE_CARD_NO_ENGINE&&xz_route_card_kind(-1)==XZ_ROUTE_CARD_NO_ENGINE);
 /* Buttons per state (design 5.2): RESCAN / TRY AGAIN only where it can help; TWO DECKS everywhere but READY. */
 static const struct { int state, primary, two; } offers[]={
  {XZ_EXTRA_ROUTE_NOT_DETECTED,1,1},{XZ_EXTRA_ROUTE_SCANNING,0,1},{XZ_EXTRA_ROUTE_STARTING,0,1},
  {XZ_EXTRA_ROUTE_WAITING_NATIVE_STOP,0,1},{XZ_EXTRA_ROUTE_READY,0,0},{XZ_EXTRA_ROUTE_FAILED,1,1},
  {XZ_EXTRA_ROUTE_LOST,1,1},{XZ_EXTRA_ROUTE_NO_ENGINE,0,1}};
 for(size_t i=0;i<sizeof offers/sizeof *offers;i++){
  struct xz_route_card_input in=input(offers[i].state,0,100000u);int order[2];
  assert(xz_route_card_offers(&in,XZ_ROUTE_CARD_HIT_PRIMARY)==offers[i].primary&&xz_route_card_offers(&in,XZ_ROUTE_CARD_HIT_TWO)==offers[i].two);
  int n=xz_route_card_focus_order(&in,order);assert(n==offers[i].primary+offers[i].two);
  if(n)assert(order[n-1]==(offers[i].two?XZ_ROUTE_CARD_HIT_TWO:XZ_ROUTE_CARD_HIT_PRIMARY));
 }
 puts("PASS route card states: every runtime state maps; RESCAN/TRY AGAIN only when it can help; TWO DECKS always but READY");
}
static void test_geometry(void) {
 const struct xz_route_card_style *m=&xz_route_card_modern,*c=&xz_route_card_classic;
 struct xz_route_card_rect r=xz_route_card_row(m,182,260,0);assert(r.x==98&&r.y==182&&r.w==702&&r.h==155);
 r=xz_route_card_row(m,26,260,0);assert(r.y==26&&r.h==77);r=xz_route_card_row(m,26,260,1);assert(r.y==260&&r.h==77);
 r=xz_route_card_button(m,260,XZ_ROUTE_CARD_HIT_PRIMARY);assert(r.x==484&&r.y==277&&r.w==150&&r.h==44);
 r=xz_route_card_button(m,260,XZ_ROUTE_CARD_HIT_TWO);assert(r.x==644&&r.y==277&&r.w==150&&r.h==44);
 r=xz_route_card_row(c,136,204,0);assert(r.x==0&&r.y==136&&r.w==536&&r.h==132);
 r=xz_route_card_button(c,204,XZ_ROUTE_CARD_HIT_PRIMARY);assert(r.x==268&&r.y==215&&r.w==128&&r.h==40);
 r=xz_route_card_button(c,204,XZ_ROUTE_CARD_HIT_TWO);assert(r.x==404&&r.y==215&&r.w==128&&r.h==40&&r.x+r.w<=536);
 /* Hits: buttons only where offered, the rest of the card absorbs, nothing outside. */
 struct xz_route_card_input in=input(XZ_EXTRA_ROUTE_NOT_DETECTED,0,100000u);
 assert(xz_route_card_hit(m,182,260,&in,500,290)==XZ_ROUTE_CARD_HIT_PRIMARY&&xz_route_card_hit(m,182,260,&in,700,300)==XZ_ROUTE_CARD_HIT_TWO);
 assert(xz_route_card_hit(m,182,260,&in,300,200)==XZ_ROUTE_CARD_HIT_CARD&&xz_route_card_hit(m,182,260,&in,97,200)==XZ_ROUTE_CARD_HIT_NONE);
 assert(xz_route_card_hit(m,182,260,&in,300,181)==XZ_ROUTE_CARD_HIT_NONE&&xz_route_card_hit(m,182,260,&in,300,337)==XZ_ROUTE_CARD_HIT_NONE);
 assert(xz_route_card_hit(m,26,260,&in,300,150)==XZ_ROUTE_CARD_HIT_NONE); /* mixer order: decks 1 and 2 between the strips */
 in.state=XZ_EXTRA_ROUTE_SCANNING;assert(xz_route_card_hit(m,182,260,&in,500,290)==XZ_ROUTE_CARD_HIT_CARD);
 in.state=XZ_EXTRA_ROUTE_WAITING_NATIVE_STOP;assert(xz_route_card_hit(c,136,204,&in,300,230)==XZ_ROUTE_CARD_HIT_CARD&&xz_route_card_hit(c,136,204,&in,450,230)==XZ_ROUTE_CARD_HIT_TWO);
 in.state=XZ_EXTRA_ROUTE_READY;assert(xz_route_card_hit(m,182,260,&in,500,290)==XZ_ROUTE_CARD_HIT_NONE); /* not visible without ready_at */
 puts("PASS route card geometry: design rects in both interfaces, joined and split rows, buttons >= 40 px, hits follow what is offered");
}
static void test_draw(void) {
 static const int states[]={XZ_EXTRA_ROUTE_NO_ENGINE,XZ_EXTRA_ROUTE_NOT_DETECTED,XZ_EXTRA_ROUTE_SCANNING,XZ_EXTRA_ROUTE_STARTING,
  XZ_EXTRA_ROUTE_WAITING_NATIVE_STOP,XZ_EXTRA_ROUTE_FAILED,XZ_EXTRA_ROUTE_LOST};
 const struct xz_route_card_style *styles[2]={&xz_route_card_modern,&xz_route_card_classic};
 uint32_t golden[2][7];
 for(int s=0;s<2;s++)for(int order=0;order<2;order++)for(size_t i=0;i<sizeof states/sizeof *states;i++)for(int part=0;part<5;part++){
  const struct lanes l=(s?classic_lanes:modern_lanes)[order];
  struct xz_route_card_input in=input(states[i],part,100450u);
  /* With and without the cache the frame is the same but for the picture: the explainer needs the cache's
     memory; without it the picture is the flat drawing. */
  struct xz_route_card_rect pic={styles[s]->x+styles[s]->picture.x,l.ya+styles[s]->picture.y,styles[s]->picture.w,styles[s]->picture.h};
  clear(pixels);xz_route_card_draw(canvas(pixels),styles[s],l.ya,l.yb,&in,&cache);only_card(styles[s],l);
  clear(other);xz_route_card_draw(canvas(other),styles[s],l.ya,l.yb,&in,NULL);
  int moved=diff_within(pic);assert(states[i]==XZ_EXTRA_ROUTE_NO_ENGINE?!moved:moved>0);
  /* Drawn again over the cached still and the cached picture, still the same. */
  clear(other);xz_route_card_draw(canvas(other),styles[s],l.ya,l.yb,&in,&cache);assert(!memcmp(pixels,other,sizeof pixels));
  if(order==0&&part==(states[i]==XZ_EXTRA_ROUTE_FAILED?XZ_EXTRA_ROUTE_PART_PORT:0))golden[s][i]=fnv(pixels);
 }
 /* Golden frames at a fixed clock, number order (the loop states on their main shot: a fresh clock). */
 static const uint32_t expected[2][7]={
  {0xbd95aa72u,0x2f2c2fedu,0xa78d547fu,0x16b4dcd3u,0x37a6de21u,0x96731db1u,0xff5d521eu},
  {0x6e1897ebu,0x7edb0d12u,0x7ebb1d16u,0x4c5966ffu,0xf806319fu,0xf8feef54u,0xd588f7a0u}};
 for(int s=0;s<2;s++)for(int i=0;i<7;i++)if(golden[s][i]!=expected[s][i]){
  printf("golden card %d state %d: 0x%08x, expected 0x%08x\n",s,states[i],golden[s][i],expected[s][i]);assert(!"card golden frame changed");
 }
 puts("PASS route card frames: every state x part x order in both interfaces writes only the card; the picture is the explainer with a cache, the flat drawing without; golden frames");
}
static void test_motion(void) {
 const struct xz_route_card_style *s=&xz_route_card_modern;
 struct xz_route_card_rect picture={s->x+s->picture.x,182+s->picture.y,s->picture.w,s->picture.h};
 /* The explainer moves inside the picture box only, in 16 ms (60 fps) steps; a fresh card starts on the main
    shot, the picture it rests on. */
 memset(&cache,0,sizeof cache);
 struct xz_route_card_input a=input(XZ_EXTRA_ROUTE_NOT_DETECTED,0,100000u),b=a;
 clear(pixels);xz_route_card_draw(canvas(pixels),s,182,260,&a,&cache);
 const uint32_t main_shot=fnv(pixels);
 b.now_ms=100015u;clear(other);xz_route_card_draw(canvas(other),s,182,260,&b,&cache);assert(!memcmp(pixels,other,sizeof pixels));
 b.now_ms=100016u;clear(other);xz_route_card_draw(canvas(other),s,182,260,&b,&cache);assert(diff_within(picture)>0);
 b.now_ms=100400u;clear(other);xz_route_card_draw(canvas(other),s,182,260,&b,&cache);assert(diff_within(picture)>0);
 assert(xz_route_card_motion_ms(&a)==XZ_ROUTE_CARD_FRAME_MS&&XZ_ROUTE_CARD_FRAME_MS==16);
 /* A caller that steps down (a native deck plays, frames run late) gets fewer, longer frames. */
 a.step_ms=b.step_ms=33u;a.now_ms=100429u;b.now_ms=100461u;assert(xz_route_card_motion_ms(&a)==33u);   /* 429 = 13 x 33 into the loop */
 clear(pixels);xz_route_card_draw(canvas(pixels),s,182,260,&a,&cache);
 clear(other);xz_route_card_draw(canvas(other),s,182,260,&b,&cache);assert(!memcmp(pixels,other,sizeof pixels));
 b.now_ms=100462u;clear(other);xz_route_card_draw(canvas(other),s,182,260,&b,&cache);assert(diff_within(picture)>0);
 a.step_ms=b.step_ms=0;
 /* Rest: 60 s after the card appeared or the last input the loop plays on to the main shot (at most one loop
    more, the card asking for frames that long) and holds it, never jumping; then nothing moves and the still
    card shows the same picture. Any input restarts it from the main shot. */
 uint32_t rest=100000u+XZ_ROUTE_CARD_REST_MS,loop=xz_cable_loop_ms(),held=0;
 for(uint32_t at=100478u;at<rest+loop+64u;at+=16u){
  a.now_ms=at;clear(pixels);xz_route_card_draw(canvas(pixels),s,182,260,&a,&cache);only_card(s,modern_lanes[0]);
  uint32_t h=fnv(pixels);
  if(at<rest)assert(xz_route_card_motion_ms(&a)==16u);
  else if(at<rest+loop)assert(xz_route_card_motion_ms(&a)==16u);
  else assert(!xz_route_card_motion_ms(&a)&&h==main_shot);
  if(at>=rest&&h==main_shot&&!held)held=at;
  if(held)assert(h==main_shot);
 }
 assert(held>=rest&&held<rest+loop);
 b=a;b.now_ms=rest+loop+400u;clear(other);xz_route_card_draw(canvas(other),s,182,260,&b,&cache);assert(!memcmp(pixels,other,sizeof pixels));
 b.rest_from_ms=b.now_ms-10;assert(xz_route_card_motion_ms(&b));
 clear(other);xz_route_card_draw(canvas(other),s,182,260,&b,&cache);assert(fnv(other)==main_shot);
 b.now_ms+=16;clear(other);xz_route_card_draw(canvas(other),s,182,260,&b,&cache);assert(diff_within(picture)>0);
 /* Scanning: the pulse, the ring and the sweep move; the rest of the card does not. */
 struct xz_route_card_input sc=input(XZ_EXTRA_ROUTE_SCANNING,0,100000u),sd=sc;sd.now_ms=100350u;
 clear(pixels);xz_route_card_draw(canvas(pixels),s,182,260,&sc,&cache);
 clear(other);xz_route_card_draw(canvas(other),s,182,260,&sd,&cache);
 int moved=0;
 for(int y=0;y<H;y++)for(int x=0;x<W;x++)if(pixels[y*W+x]!=other[y*W+x]){
  struct xz_route_card_rect ring={s->x+s->dot_dx-10,260+s->dot_dy-10,21,21},sweep={484+10,277+44-8,130,3};
  assert(inside(picture,x,y)||inside(ring,x,y)||inside(sweep,x,y));moved++;
 }
 assert(moved>0);
 /* Failed and lost play the explainer too (the DJ has a cable to fix), in the picture box only; no engine
    stands still. */
 static const int fix[]={XZ_EXTRA_ROUTE_FAILED,XZ_EXTRA_ROUTE_LOST};
 for(size_t i=0;i<sizeof fix/sizeof *fix;i++){
  struct xz_route_card_input f=input(fix[i],1,100000u),g=f;g.now_ms=100700u;
  clear(pixels);xz_route_card_draw(canvas(pixels),s,182,260,&f,&cache);
  clear(other);xz_route_card_draw(canvas(other),s,182,260,&g,&cache);
  assert(diff_within(picture)>0&&xz_route_card_motion_ms(&f)==XZ_ROUTE_CARD_FRAME_MS);
  f.now_ms=g.now_ms=100000u+XZ_ROUTE_CARD_REST_MS+xz_cable_loop_ms();g.now_ms+=700;assert(!xz_route_card_motion_ms(&f));
  clear(pixels);xz_route_card_draw(canvas(pixels),s,182,260,&f,&cache);
  clear(other);xz_route_card_draw(canvas(other),s,182,260,&g,&cache);assert(!memcmp(pixels,other,sizeof pixels));
 }
 struct xz_route_card_input f=input(XZ_EXTRA_ROUTE_NO_ENGINE,1,100000u),g=f;g.now_ms=100700u;
 clear(pixels);xz_route_card_draw(canvas(pixels),s,182,260,&f,&cache);
 clear(other);xz_route_card_draw(canvas(other),s,182,260,&g,&cache);
 assert(!memcmp(pixels,other,sizeof pixels)&&!xz_route_card_motion_ms(&f));
 puts("PASS route card motion: the explainer only in the picture box in 16 ms steps (33 when stepped down), starts on the main shot, plays on to it and rests after 60 s, input restarts it; scanning ring and sweep; failed and lost move, no engine stands still");
}
static void test_success(void) {
 const struct xz_route_card_style *s=&xz_route_card_modern;
 struct xz_route_card_input in=input(XZ_EXTRA_ROUTE_READY,0,100000u);in.ready_at_ms=100000u;
 assert(xz_route_card_visible(&in)&&xz_route_card_wipe_x(s,&in)==s->x);
 clear(pixels);xz_route_card_draw(canvas(pixels),s,182,260,&in,&cache);only_card(s,modern_lanes[0]);
 assert(pixels[300*W+300]!=SENTINEL);
 int previous=s->x;
 for(uint32_t t=XZ_ROUTE_CARD_READY_HOLD_MS;t<XZ_ROUTE_CARD_READY_HOLD_MS+XZ_ROUTE_CARD_WIPE_MS;t+=30){
  in.now_ms=100000u+t;int edge=xz_route_card_wipe_x(s,&in);
  assert(edge>previous&&edge<=s->x+s->w);previous=edge;
  clear(pixels);xz_route_card_draw(canvas(pixels),s,182,260,&in,&cache);
  for(int y=182;y<337;y++)for(int x=0;x<edge;x++)assert(pixels[y*W+x]==SENTINEL);
 }
 assert(previous==s->x+s->w);
 in.now_ms=100000u+XZ_ROUTE_CARD_READY_HOLD_MS+XZ_ROUTE_CARD_WIPE_MS;assert(!xz_route_card_visible(&in));
 clear(pixels);xz_route_card_draw(canvas(pixels),s,182,260,&in,&cache);for(int i=0;i<W*H;i++)assert(pixels[i]==SENTINEL);
 /* The clock wraps: the same intervals hold across 2^32. */
 in.ready_at_ms=0xffffff00u;in.now_ms=0xffffff00u+100u;assert(xz_route_card_visible(&in)&&xz_route_card_wipe_x(s,&in)==s->x);
 puts("PASS route card success: READY holds 700 ms, wipes left to right in 8 frames over 240 ms, then leaves; wrap-safe");
}
static void test_focus_and_long_reasons(void) {
 const struct xz_route_card_style *styles[2]={&xz_route_card_modern,&xz_route_card_classic};
 for(int i=0;i<2;i++){
  const struct lanes l=(i?classic_lanes:modern_lanes)[0];
  struct xz_route_card_input in=input(XZ_EXTRA_ROUTE_FAILED,XZ_EXTRA_ROUTE_PART_CABLE,100000u);
  in.reason="An eighty character reason sentence that keeps going until it reaches the limit.";
  assert(strlen(in.reason)<=80);
  clear(pixels);xz_route_card_draw(canvas(pixels),styles[i],l.ya,l.yb,&in,&cache);only_card(styles[i],l);
  uint32_t plain=fnv(pixels);
  in.focus=XZ_ROUTE_CARD_HIT_PRIMARY;clear(pixels);xz_route_card_draw(canvas(pixels),styles[i],l.ya,l.yb,&in,&cache);only_card(styles[i],l);
  uint32_t primary=fnv(pixels);
  in.focus=XZ_ROUTE_CARD_HIT_TWO;clear(pixels);xz_route_card_draw(canvas(pixels),styles[i],l.ya,l.yb,&in,&cache);only_card(styles[i],l);
  assert(plain!=primary&&primary!=fnv(pixels));
 }
 puts("PASS route card focus rings and 80-character reasons stay inside the card in both interfaces");
}
int main(void) { test_kinds();test_geometry();test_draw();test_motion();test_success();test_focus_and_long_reasons();return 0; }
