/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error DECK VIEW acceptance requires active assertions
#endif
#include "deck_view.h"
#include "route_card.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { W=800, H=480, SENTINEL=0x5aa5 };
static uint16_t pixels[W*H];
static struct xz_canvas canvas(void) { return (struct xz_canvas){pixels,W,W,H,0}; }
static void clear(void) { for(int i=0;i<W*H;i++)pixels[i]=SENTINEL; }
/* Every pixel outside r (grown by grow) still holds the sentinel. */
static void only_inside(struct xz_dv_rect r, int grow) {
 for(int y=0;y<H;y++)for(int x=0;x<W;x++){
  int in=x>=r.x-grow&&y>=r.y-grow&&x<r.x+r.w+grow&&y<r.y+r.h+grow;
  if(!in)assert(pixels[y*W+x]==SENTINEL);
 }
}
static int changed_inside(struct xz_dv_rect r) {
 int n=0;for(int y=r.y;y<r.y+r.h;y++)for(int x=r.x;x<r.x+r.w;x++)n+=pixels[y*W+x]!=SENTINEL;return n;
}
static uint32_t fnv(void) { uint32_t h=2166136261u;for(int i=0;i<W*H;i++){h^=pixels[i];h*=16777619u;}return h; }

static void test_words(void) {
 assert(!strcmp(xz_deck_view_name(XZ_LAYOUT_TWO),"2 DECKS")&&!strcmp(xz_deck_view_name(XZ_LAYOUT_FOUR),"4 DECKS")&&!strcmp(xz_deck_view_name(XZ_LAYOUT_MIXER),"4 DECKS"));
 assert(!strcmp(xz_deck_view_sub(0),"DECKS 1 2")&&!strcmp(xz_deck_view_sub(1),"NUMBER ORDER 1 2 3 4")&&!strcmp(xz_deck_view_sub(2),"MIXER ORDER 3 1 2 4"));
 assert(!strcmp(xz_deck_view_name(7),"2 DECKS")&&!strcmp(xz_deck_view_sub(-1),"DECKS 1 2")&&xz_deck_view_valid(3)==XZ_LAYOUT_TWO);
 enum xz_deck_view_tone t;
 assert(!strcmp(xz_deck_view_status(0,0,&t),"LIKE THE PLAYER ITSELF")&&t==XZ_DECK_VIEW_TONE_DIM);
 assert(!strcmp(xz_deck_view_status(0,1,&t),"LIKE THE PLAYER ITSELF")&&t==XZ_DECK_VIEW_TONE_DIM);
 for(int v=1;v<3;v++){
  assert(!strcmp(xz_deck_view_status(v,0,&t),"3 AND 4 NOT CONNECTED")&&t==XZ_DECK_VIEW_TONE_AMBER);
  assert(!strcmp(xz_deck_view_status(v,1,&t),"DECKS 3 AND 4 READY")&&t==XZ_DECK_VIEW_TONE_GREEN);
 }
 unsigned char lanes[4];
 assert(xz_deck_view_lanes(0,lanes)==2&&lanes[0]==1&&lanes[1]==2&&!lanes[2]);
 assert(xz_deck_view_lanes(1,lanes)==4&&lanes[0]==1&&lanes[1]==2&&lanes[2]==3&&lanes[3]==4);
 assert(xz_deck_view_lanes(2,lanes)==4&&lanes[0]==3&&lanes[1]==1&&lanes[2]==2&&lanes[3]==4);
 assert(!strcmp(XZ_DECK_VIEW_READY_NOTICE,"DECKS 3 AND 4 ARE READY. TAP DECKS AT THE TOP FOR FOUR DECKS."));
 puts("PASS deck view words: names, sub-lines, status tones, lane order, invalid values read as 2 DECKS");
}
static void test_button(void) {
 const struct xz_deck_view_style *styles[2]={&xz_deck_view_modern,&xz_deck_view_classic};
 /* Design rects and touch bands. */
 assert(styles[0]->button.x==504&&styles[0]->button.w==120&&styles[0]->button.h==26&&styles[0]->button_touch_h==44);
 assert(styles[1]->button.x==162&&styles[1]->button.w==116&&styles[1]->button.h==18&&styles[1]->button_touch_h==40);
 for(int i=0;i<2;i++){
  const struct xz_deck_view_style *s=styles[i];
  assert(xz_deck_view_button_hit(s,s->button.x,0)&&xz_deck_view_button_hit(s,s->button.x+s->button.w-1,s->button_touch_h-1));
  assert(!xz_deck_view_button_hit(s,s->button.x-1,5)&&!xz_deck_view_button_hit(s,s->button.x+s->button.w,5)&&!xz_deck_view_button_hit(s,s->button.x+5,s->button_touch_h));
  assert(s->button_touch_h>=40);
  uint32_t seen[12];int n=0;
  for(int view=0;view<3;view++)for(int ready=0;ready<2;ready++){
   clear();xz_deck_view_button_draw(canvas(),s,view,ready,0,0,0);only_inside(s->button,0);
   assert(changed_inside(s->button)>s->button.w*s->button.h/2);
   seen[n++]=fnv();
  }
  /* Ready changes only four-deck pictures (grey 3 and 4, the amber dot). */
  assert(seen[0]==seen[1]&&seen[2]!=seen[3]&&seen[4]!=seen[5]&&seen[2]!=seen[4]&&seen[0]!=seen[2]);
  clear();xz_deck_view_button_draw(canvas(),s,1,0,0,1,0);only_inside(s->button,0);uint32_t open=fnv();
  clear();xz_deck_view_button_draw(canvas(),s,1,0,0,0,1);only_inside(s->button,0);uint32_t focus=fnv();
  assert(open!=seen[2]&&focus!=seen[2]&&open!=focus);
  /* A 2 DECKS pair names the decks the sides drive; not while 3 and 4 are down. */
  clear();xz_deck_view_button_draw(canvas(),s,0,1,1,0,0);assert(fnv()!=seen[1]);
  clear();xz_deck_view_button_draw(canvas(),s,0,0,3,0,0);assert(fnv()==seen[0]);
 }
 puts("PASS deck view button: design rects, touch bands >= 40 px, writes only its rect, 2/4/4 x ready/not, open and focus looks");
}
static void test_picker(void) {
 const struct xz_deck_view_style *styles[2]={&xz_deck_view_modern,&xz_deck_view_classic};
 assert(styles[0]->picker.x==296&&styles[0]->picker.y==30&&styles[0]->picker.w==500&&styles[0]->picker.h==206);
 assert(styles[1]->picker.x==140&&styles[1]->picker.y==22&&styles[1]->picker.w==500&&styles[1]->picker.h==206);
 uint32_t golden[2][3]={{0}};
 for(int i=0;i<2;i++){
  const struct xz_deck_view_style *s=styles[i];
  for(int t=0;t<3;t++){
   struct xz_dv_rect r=xz_deck_view_tile(s,t);
   assert(r.w==156&&r.h==166&&r.x==s->picker.x+12+164*t&&r.y==s->picker.y+28);
   assert(xz_deck_view_picker_hit(s,r.x,r.y)==t&&xz_deck_view_picker_hit(s,r.x+r.w-1,r.y+r.h-1)==t);
   if(t<2)assert(xz_deck_view_picker_hit(s,r.x+r.w+2,r.y+40)==XZ_DECK_VIEW_HIT_PANEL);
  }
  assert(!xz_deck_view_tile(s,3).w&&!xz_deck_view_tile(s,-1).w);
  assert(xz_deck_view_picker_hit(s,s->picker.x+5,s->picker.y+5)==XZ_DECK_VIEW_HIT_PANEL);
  assert(xz_deck_view_picker_hit(s,s->picker.x-1,s->picker.y+50)==XZ_DECK_VIEW_HIT_OUTSIDE);
  assert(xz_deck_view_picker_hit(s,s->picker.x+50,s->picker.y+s->picker.h)==XZ_DECK_VIEW_HIT_OUTSIDE);
  for(int view=0;view<3;view++)for(int ready=0;ready<2;ready++)for(int focus=-1;focus<3;focus++){
   clear();xz_deck_view_picker_draw(canvas(),s,view,ready,focus);only_inside(s->picker,1);
   assert(changed_inside(s->picker)==s->picker.w*s->picker.h);
   if(ready==0&&focus==1)golden[i][view]=fnv();
  }
 }
 /* Golden pictures of the picker: chosen view, decks 3/4 not connected, encoder on the middle tile. */
 static const uint32_t expected[2][3]={{0x4bc053b9u,0xd5e37c55u,0xf78a8060u},{0xc686895cu,0xb7053c71u,0x60620f48u}};
 for(int i=0;i<2;i++)for(int v=0;v<3;v++)if(golden[i][v]!=expected[i][v]){
  printf("golden picker %d view %d: 0x%08x, expected 0x%08x\n",i,v,golden[i][v],expected[i][v]);assert(!"picker golden frame changed");
 }
 assert(xz_deck_view_focus_step(0,-1)==0&&xz_deck_view_focus_step(0,1)==1&&xz_deck_view_focus_step(2,5)==2&&xz_deck_view_focus_step(-1,0)==0&&xz_deck_view_focus_step(9,-1)==1&&xz_deck_view_focus_step(0,2)==2&&xz_deck_view_focus_step(2,-99)==0);
 puts("PASS deck view picker: three 156x166 tiles, panel absorbs, outside closes, full panel paint, golden frames, encoder clamps");
}
static void test_notice_latch(void) {
 assert(!xz_deck_view_ready_notice_take(0));
 assert(xz_deck_view_ready_notice_take(41)&&!xz_deck_view_ready_notice_take(41));
 assert(xz_deck_view_ready_notice_take(42)&&!xz_deck_view_ready_notice_take(42)&&!xz_deck_view_ready_notice_take(0));
 assert(xz_deck_view_ready_notice_take(0xffffffffu)&&xz_deck_view_ready_notice_take(1));
 puts("PASS route-up notice latch: one notice per ready revision, shared by both interfaces, never for 0");
}
static void test_route_notice(void) {
 struct xz_deck_view_notice n={0};
 assert(!xz_deck_view_notice_text(&n,0)&&!xz_deck_view_notice_text(NULL,0));
 /* 2 DECKS: the ready notice from the edge for 3.5 s, once per ready revision. */
 assert(xz_deck_view_route_notice(XZ_LAYOUT_TWO,100,5000,&n)&&!strcmp(n.text,XZ_DECK_VIEW_READY_NOTICE));
 assert(xz_deck_view_notice_text(&n,5000)&&xz_deck_view_notice_text(&n,8499)&&!xz_deck_view_notice_text(&n,8500)&&!xz_deck_view_notice_text(&n,4999));
 assert(!xz_deck_view_route_notice(XZ_LAYOUT_TWO,100,6000,&n)&&!xz_deck_view_route_notice(XZ_LAYOUT_FOUR,0,6000,&n));
 /* A four-deck view: the success notice once the READY hold and the wipe end, for 2.5 s. */
 uint32_t after=XZ_ROUTE_CARD_READY_HOLD_MS+XZ_ROUTE_CARD_WIPE_MS;
 assert(xz_deck_view_route_notice(XZ_LAYOUT_MIXER,101,0xfffffe00u,&n)&&!strcmp(n.text,XZ_DECK_VIEW_SUCCESS_NOTICE));
 assert(!xz_deck_view_notice_text(&n,0xfffffe00u+after-1)&&xz_deck_view_notice_text(&n,0xfffffe00u+after)&&
        xz_deck_view_notice_text(&n,0xfffffe00u+after+2499)&&!xz_deck_view_notice_text(&n,0xfffffe00u+after+2500));
 /* Both fit the Classic banner (14 px in, 536 px surface). */
 uint16_t px[8];struct xz_canvas c={px,8,8,1,0};
 assert(xz_text_width(c,XZ_DECK_VIEW_READY_NOTICE,1,99)<=536-28&&strlen(XZ_DECK_VIEW_READY_NOTICE)<=63);
 puts("PASS route-up banner: 2 DECKS notice for 3.5 s from the edge, four-deck success notice after the hold and wipe for 2.5 s, once per revision, wraps, fits the banner");
}
int main(void) { test_words();test_button();test_picker();test_notice_latch();test_route_notice();return 0; }
