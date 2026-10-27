#ifdef NDEBUG
#error Shell acceptance requires active assertions
#endif
/* DECK VIEW on PLAY (ui/deck_view.h, ui/route_card.h): the button and picker, four slots while decks 3 and 4
   are down, the card's buttons and their stale-paint guard, the encoder's capture, the motion cadence and
   the success hold. */
#include "four_deck_fixtures.h"
#include "shell_internal.h"
#include "../extra_decks.h"
#include "../ui/deck_view.h"
#include "../ui/route_card.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { STRIDE=804, GUARD=64, SIZE=GUARD+STRIDE*480+GUARD };
static struct fx_track track;
static uint16_t a[SIZE],b[SIZE];
static uint16_t *frame(uint16_t *p){return p+GUARD;}
/* Pure and complete: two prefills give one frame, nothing past the edges, shell and model untouched. */
static void render(const struct xz_shell *u,const struct xz_shell_model *m){
 struct xz_shell before=*u;struct xz_shell_model model=*m;
 for(int n=0;n<2;n++){
  uint16_t *p=n?b:a;
  for(size_t i=0;i<SIZE;i++)p[i]=0xa55a;
  for(int y=0;y<480;y++)for(int x=0;x<800;x++)frame(p)[y*STRIDE+x]=n?0xffff:0;
  assert(xz_shell_render(u,m,frame(p),STRIDE));
  for(int i=0;i<GUARD;i++)assert(p[i]==0xa55a&&p[SIZE-1-i]==0xa55a);
 }
 assert(!memcmp(a,b,sizeof a));
 assert(!memcmp(u,&before,sizeof before)&&!memcmp(m,&model,sizeof model));
}
static void paint(struct xz_shell *u,const struct xz_shell_model *m){render(u,m);assert(xz_shell_render_frame(u,m,frame(a),STRIDE));}
static size_t down_up(struct xz_shell *u,const struct xz_shell_model *m,int x,int y,struct xz_shell_action out[XZ_SHELL_ACTIONS]){
 struct xz_shell_action rest[XZ_SHELL_ACTIONS];
 size_t n=xz_shell_input(u,m,&(struct xz_shell_event){XZ_SHELL_TOUCH_DOWN,x,y,0,0,-1},out);
 size_t k=xz_shell_input(u,m,&(struct xz_shell_event){XZ_SHELL_TOUCH_UP,x,y,0,0,-1},rest);
 assert(!k);(void)rest;return n;
}
static size_t enc(struct xz_shell *u,const struct xz_shell_model *m,enum xz_shell_event_kind kind,int delta,struct xz_shell_action out[XZ_SHELL_ACTIONS]){
 return xz_shell_input(u,m,&(struct xz_shell_event){kind,0,0,delta,0,-1},out);
}
static size_t key(struct xz_shell *u,const struct xz_shell_model *m,enum xz_shell_key k,struct xz_shell_action out[XZ_SHELL_ACTIONS]){
 return xz_shell_input(u,m,&(struct xz_shell_event){XZ_SHELL_KEY,0,0,0,k,-1},out);
}
/* Decks 3 and 4 down: two audio decks, the route's state, DECK VIEW four. */
static void route_down(struct xz_shell_model *m,int view,int state){
 fx_model_full(m,&track);
 m->layout_preference=view+1;m->route_state=state;m->route_fail_part=XZ_EXTRA_ROUTE_PART_CABLE;
 snprintf(m->route_reason,sizeof m->route_reason,"%s","The loopback audio route has not been detected.");
 m->route_card_since_ms=m->clock_ms-1000;
}
static struct xz_route_card_rect button(const struct xz_shell_model *m,int hit){
 struct xz_shell_model view;struct xz_shell u;xz_shell_init(&u);xz_shell_play_projection(&u,m,&view);
 return xz_route_card_button(&xz_route_card_modern,xz_shell_play_rect(&view,XZ_SHELL_R_LANE,3).y,hit);
}

static void test_placeholders(void){
 struct xz_shell u;struct xz_shell_model m,view;struct xz_shell_action out[XZ_SHELL_ACTIONS];
 for(int view_id=XZ_LAYOUT_FOUR;view_id<=XZ_LAYOUT_MIXER;view_id++){
  xz_shell_init(&u);route_down(&m,view_id,XZ_EXTRA_ROUTE_NOT_DETECTED);
  xz_shell_play_projection(&u,&m,&view);
  assert(view.play_view_count==4&&view.play_placeholders==0xc&&sh_deck_count(&view)==2);
  /* Decks 3 and 4 keep only geometry: nothing on them reads touch, and no key readout. */
  for(int d=2;d<4;d++){
   assert(xz_shell_play_rect(&view,XZ_SHELL_R_LANE,d).w&&xz_shell_play_rect(&view,XZ_SHELL_R_STRIP,d).w);
   assert(!xz_shell_play_rect(&view,XZ_SHELL_R_KEY,d).w&&!xz_shell_play_rect(&view,XZ_SHELL_R_OVERVIEW,d).w&&!xz_shell_play_rect(&view,XZ_SHELL_R_TIME,d).w);
  }
  /* ZOOM / GRID sit at the foot of the lowest live lane, beside the card, not over it. */
  struct xz_shell_rect zoom=xz_shell_play_rect(&view,XZ_SHELL_R_ZOOM,0),lane2=xz_shell_play_rect(&view,XZ_SHELL_R_LANE,1);
  assert(zoom.y+zoom.h<=lane2.y+lane2.h&&zoom.y>=lane2.y);
  paint(&u,&m);
  /* The card's buttons, then a tap anywhere else on the card does nothing. */
  struct xz_route_card_rect rescan=button(&m,XZ_ROUTE_CARD_HIT_PRIMARY),two=button(&m,XZ_ROUTE_CARD_HIT_TWO);
  assert(rescan.w==150&&rescan.h==44&&two.w==150&&two.h==44);
  assert(down_up(&u,&m,rescan.x+20,rescan.y+20,out)==1&&out[0].kind==XZ_SHELL_ACT_ROUTE_RESCAN);
  struct xz_shell_rect lane3=xz_shell_play_rect(&view,XZ_SHELL_R_LANE,2);
  assert(!down_up(&u,&m,lane3.x+300,lane3.y+30,out)&&u.deck==0);
  /* Placeholder columns and strips take the touch and do nothing. */
  for(int d=2;d<4;d++){
   struct xz_shell_rect col=xz_shell_play_rect(&view,XZ_SHELL_R_INFO_COLUMN,d),strip=xz_shell_play_rect(&view,XZ_SHELL_R_STRIP,d);
   assert(!down_up(&u,&m,col.x+10,col.y+10,out)&&u.deck==0);
   assert(!down_up(&u,&m,strip.x+50,strip.y+120,out)&&u.deck==0);
  }
  /* Decks 1 and 2 stay fully usable: their lanes activate, their overviews search. */
  struct xz_shell_rect lane=xz_shell_play_rect(&view,XZ_SHELL_R_LANE,1);
  size_t n=down_up(&u,&m,lane.x+300,lane.y+40,out);assert(n==1&&out[0].kind==XZ_SHELL_ACT_ACTIVE_DECK&&u.deck==1);
  struct xz_shell_rect ov=xz_shell_play_rect(&view,XZ_SHELL_R_OVERVIEW,0);
  assert(down_up(&u,&m,ov.x+ov.w/2,ov.y+ov.h/2,out)==1&&out[0].kind==XZ_SHELL_ACT_SEEK&&out[0].deck==0);
  /* TWO DECKS chooses 2 DECKS through the one DECK VIEW action. */
  paint(&u,&m);
  assert(down_up(&u,&m,two.x+20,two.y+20,out)==1&&out[0].kind==XZ_SHELL_ACT_DECK_LAYOUT&&out[0].value==XZ_LAYOUT_TWO);
  /* A state the screen has not shown yet is never pressed: RESCAN vanished in WAITING. */
  m.route_state=XZ_EXTRA_ROUTE_WAITING_NATIVE_STOP;
  assert(!down_up(&u,&m,rescan.x+20,rescan.y+20,out));
  paint(&u,&m);assert(!down_up(&u,&m,rescan.x+20,rescan.y+20,out));
  assert(down_up(&u,&m,two.x+20,two.y+20,out)==1&&out[0].value==XZ_LAYOUT_TWO);
  /* FAILED offers TRY AGAIN in the same place; NO ENGINE only TWO DECKS. */
  m.route_state=XZ_EXTRA_ROUTE_FAILED;paint(&u,&m);
  assert(down_up(&u,&m,rescan.x+20,rescan.y+20,out)==1&&out[0].kind==XZ_SHELL_ACT_ROUTE_RESCAN);
  m.route_state=XZ_EXTRA_ROUTE_NO_ENGINE;paint(&u,&m);
  assert(!down_up(&u,&m,rescan.x+20,rescan.y+20,out));
  /* A READY report before the deck count follows reads as starting: the card stays. */
  m.route_state=XZ_EXTRA_ROUTE_READY;render(&u,&m);xz_shell_play_projection(&u,&m,&view);assert(view.play_placeholders==0xc);
 }
 puts("PASS DECK VIEW four slots while 3/4 are down: placeholders take no touch, card buttons by state, stale state never pressed, decks 1/2 usable, ZOOM beside the card");
}
static void test_button_and_picker(void){
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_action out[XZ_SHELL_ACTIONS];
 xz_shell_init(&u);route_down(&m,XZ_LAYOUT_TWO,XZ_EXTRA_ROUTE_NOT_DETECTED);
 struct xz_shell_rect b=xz_shell_play_rect(&m,XZ_SHELL_R_VIEW_TWO,0),fx=xz_shell_play_rect(&m,XZ_SHELL_R_FX,0);
 assert(b.x==504&&b.y==0&&b.w==120&&b.h==26&&fx.x+fx.w==498);
 paint(&u,&m);
 /* The touch band reaches 43 px down, like the other top-bar controls. */
 assert(!down_up(&u,&m,b.x+b.w-2,43,out)&&u.deck_view.open);
 paint(&u,&m);
 /* The panel takes touches in the header band too; the panel's empty parts do nothing. */
 assert(!down_up(&u,&m,700,35,out)&&u.deck_view.open);
 assert(!down_up(&u,&m,xz_deck_view_modern.picker.x+5,xz_deck_view_modern.picker.y+20,out)&&u.deck_view.open);
 /* Outside closes, and the touch goes no further. */
 assert(!down_up(&u,&m,150,300,out)&&!u.deck_view.open&&u.deck==0);
 /* BACK closes the picker and nothing else. */
 paint(&u,&m);assert(!down_up(&u,&m,b.x+10,10,out)&&u.deck_view.open);
 assert(!key(&u,&m,XZ_SHELL_KEY_BACK,out)&&!u.deck_view.open&&xz_shell_screen(&u)==XZ_SHELL_PLAY);
 /* Six seconds without input close it. */
 paint(&u,&m);assert(!down_up(&u,&m,b.x+10,10,out)&&u.deck_view.open);
 m.clock_ms+=XZ_DECK_VIEW_PICKER_TIMEOUT_MS;paint(&u,&m);assert(!u.deck_view.open);
 puts("PASS DECK VIEW button and picker: design rects, FX strip ends at 498, 44 px touch band, panel owns its header-band corner, outside/BACK/6 s close");
}
static void test_encoder(void){
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_action out[XZ_SHELL_ACTIONS];
 xz_shell_init(&u);route_down(&m,XZ_LAYOUT_FOUR,XZ_EXTRA_ROUTE_NOT_DETECTED);paint(&u,&m);
 int zoom=u.zoom;
 /* First push: RESCAN focused. Turn: TWO DECKS, then the DECK VIEW button; no zoom while it holds. */
 assert(!enc(&u,&m,XZ_SHELL_ENCODER_PUSH,0,out)&&u.deck_view.focus==XZ_SHELL_FOCUS_CARD_PRIMARY);
 assert(!enc(&u,&m,XZ_SHELL_ENCODER_TURN,1,out)&&u.deck_view.focus==XZ_SHELL_FOCUS_CARD_TWO&&u.zoom==zoom);
 assert(!enc(&u,&m,XZ_SHELL_ENCODER_TURN,1,out)&&u.deck_view.focus==XZ_SHELL_FOCUS_DECK_VIEW);
 assert(!enc(&u,&m,XZ_SHELL_ENCODER_TURN,1,out)&&u.deck_view.focus==XZ_SHELL_FOCUS_DECK_VIEW);
 assert(!enc(&u,&m,XZ_SHELL_ENCODER_TURN,-1,out)&&u.deck_view.focus==XZ_SHELL_FOCUS_CARD_TWO);
 assert(enc(&u,&m,XZ_SHELL_ENCODER_TURN,-1,out)==0&&enc(&u,&m,XZ_SHELL_ENCODER_PUSH,0,out)==1&&out[0].kind==XZ_SHELL_ACT_ROUTE_RESCAN);
 assert(u.deck_view.focus==XZ_SHELL_FOCUS_NONE);
 /* To the picker: push, turn twice, push opens it; turn picks a tile; push applies. */
 enc(&u,&m,XZ_SHELL_ENCODER_PUSH,0,out);enc(&u,&m,XZ_SHELL_ENCODER_TURN,2,out);
 assert(!enc(&u,&m,XZ_SHELL_ENCODER_PUSH,0,out)&&u.deck_view.open&&u.deck_view.tile==XZ_LAYOUT_FOUR);
 assert(!enc(&u,&m,XZ_SHELL_ENCODER_TURN,1,out)&&u.deck_view.tile==XZ_LAYOUT_MIXER&&u.zoom==zoom);
 assert(enc(&u,&m,XZ_SHELL_ENCODER_PUSH,0,out)==1&&out[0].kind==XZ_SHELL_ACT_DECK_LAYOUT&&out[0].value==XZ_LAYOUT_MIXER&&!u.deck_view.open);
 /* BACK leaves the focus; a hold gives the encoder back to GRID; 6 s idle too. */
 enc(&u,&m,XZ_SHELL_ENCODER_PUSH,0,out);assert(u.deck_view.focus);
 assert(!key(&u,&m,XZ_SHELL_KEY_BACK,out)&&!u.deck_view.focus&&xz_shell_screen(&u)==XZ_SHELL_PLAY);
 enc(&u,&m,XZ_SHELL_ENCODER_PUSH,0,out);assert(u.deck_view.focus);
 int grid=u.grid_mode;enc(&u,&m,XZ_SHELL_ENCODER_HOLD,0,out);assert(!u.deck_view.focus&&u.grid_mode!=grid);
 u.grid_mode=0;
 enc(&u,&m,XZ_SHELL_ENCODER_PUSH,0,out);assert(u.deck_view.focus);
 m.clock_ms+=XZ_DECK_VIEW_PICKER_TIMEOUT_MS;
 assert(enc(&u,&m,XZ_SHELL_ENCODER_TURN,1,out)==1&&out[0].kind==XZ_SHELL_ACT_ZOOM&&!u.deck_view.focus);
 /* Without the card the first push focuses the DECK VIEW button, the second opens the picker. */
 xz_shell_init(&u);route_down(&m,XZ_LAYOUT_TWO,XZ_EXTRA_ROUTE_NOT_DETECTED);paint(&u,&m);
 assert(!enc(&u,&m,XZ_SHELL_ENCODER_PUSH,0,out)&&u.deck_view.focus==XZ_SHELL_FOCUS_DECK_VIEW);
 render(&u,&m);
 assert(!enc(&u,&m,XZ_SHELL_ENCODER_PUSH,0,out)&&u.deck_view.open&&u.deck_view.tile==XZ_LAYOUT_TWO);
 /* A touch elsewhere drops the focus. */
 xz_shell_init(&u);paint(&u,&m);enc(&u,&m,XZ_SHELL_ENCODER_PUSH,0,out);assert(u.deck_view.focus);
 struct xz_shell_rect lane=xz_shell_play_rect(&m,XZ_SHELL_R_LANE,0);down_up(&u,&m,lane.x+200,lane.y+40,out);assert(!u.deck_view.focus);
 puts("PASS DECK VIEW encoder: push focuses RESCAN or the button, turn moves without zooming, push presses, picker by encoder, BACK/hold/6 s give the encoder back");
}
static void test_motion_and_success(void){
 struct xz_shell u;struct xz_shell_model m,view;
 xz_shell_init(&u);route_down(&m,XZ_LAYOUT_FOUR,XZ_EXTRA_ROUTE_NOT_DETECTED);
 m.deck[0].playing=m.deck[1].playing=0;m.frame_late=0;
 assert(xz_shell_play_motion_ms(&u,&m)==XZ_ROUTE_CARD_FRAME_MS&&xz_shell_motion_ms(&u,&m)==XZ_ROUTE_CARD_FRAME_MS);
 /* The picture steps down while a native deck plays (30 fps) and after a late frame (15 fps). */
 m.deck[1].playing=1;assert(xz_shell_play_motion_ms(&u,&m)==33u);
 m.frame_late=1;assert(xz_shell_play_motion_ms(&u,&m)==66u);m.frame_late=0;m.deck[1].playing=0;
 /* 60 s on, the picture's loop plays on to its main shot (at most one loop more); then nothing moves. */
 m.clock_ms=m.route_card_since_ms+XZ_ROUTE_CARD_REST_MS;assert(xz_shell_play_motion_ms(&u,&m)==XZ_ROUTE_CARD_FRAME_MS);
 m.clock_ms=m.route_card_since_ms+XZ_ROUTE_CARD_REST_MS+xz_cable_loop_ms();assert(!xz_shell_play_motion_ms(&u,&m));
 u.deck_view.input_at=m.clock_ms-5;assert(xz_shell_play_motion_ms(&u,&m)==XZ_ROUTE_CARD_FRAME_MS);
 /* Failed plays the explainer too: the DJ has a cable to fix. */
 m.route_state=XZ_EXTRA_ROUTE_FAILED;assert(xz_shell_play_motion_ms(&u,&m)==XZ_ROUTE_CARD_FRAME_MS);
 m.layout_preference=XZ_LAYOUT_TWO+1;m.route_state=XZ_EXTRA_ROUTE_NOT_DETECTED;assert(!xz_shell_play_motion_ms(&u,&m));
 /* Route up under the card: READY holds, the card wipes away, then decks 3 and 4 are live. */
 xz_shell_init(&u);fx_model_four(&m,&track);m.layout_preference=XZ_LAYOUT_FOUR+1;m.route_state=XZ_EXTRA_ROUTE_READY;
 m.route_card_ready_at_ms=m.clock_ms-100;
 xz_shell_play_projection(&u,&m,&view);assert(view.play_placeholders==0xc&&xz_shell_play_motion_ms(&u,&m));
 render(&u,&m);
 m.route_card_ready_at_ms=m.clock_ms-800;render(&u,&m);xz_shell_play_projection(&u,&m,&view);assert(view.play_placeholders==0xc);
 m.route_card_ready_at_ms=m.clock_ms-XZ_ROUTE_CARD_READY_HOLD_MS-XZ_ROUTE_CARD_WIPE_MS;
 xz_shell_play_projection(&u,&m,&view);assert(!view.play_placeholders&&!xz_shell_play_motion_ms(&u,&m));
 /* Four decks up without a success edge: no card, frames as before. */
 m.route_card_ready_at_ms=0;xz_shell_play_projection(&u,&m,&view);assert(!view.play_placeholders);
 puts("PASS DECK VIEW motion: 20 fps while the card moves, rest after 60 s from appearance or input and one loop more, still states and 2 DECKS idle; success hold and wipe then live decks");
}
/* The DECKS button is always on the play screen: its rect is there in every view and the BEAT FX strip ends
   before it. */
static void test_always_shown(void){
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_action out[XZ_SHELL_ACTIONS];
 for(int view=0;view<XZ_LAYOUT_COUNT;view++){
  xz_shell_init(&u);route_down(&m,view,XZ_EXTRA_ROUTE_NOT_DETECTED);
  struct xz_shell_rect b=xz_shell_play_rect(&m,XZ_SHELL_R_VIEW_TWO,0),fx=xz_shell_play_rect(&m,XZ_SHELL_R_FX,0);
  assert(b.w&&b.h&&fx.x+fx.w<=b.x);
 }
 xz_shell_init(&u);route_down(&m,XZ_LAYOUT_TWO,XZ_EXTRA_ROUTE_NOT_DETECTED);paint(&u,&m);
 down_up(&u,&m,560,10,out);assert(u.deck_view.open);
 puts("PASS DECKS button always on the play screen, BEAT FX strip ends before it, a tap opens the picker");
}
int main(void){fx_track_build(&track,7);test_placeholders();test_button_and_picker();test_encoder();test_motion_and_success();test_always_shown();return 0;}
