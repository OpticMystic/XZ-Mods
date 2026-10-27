#include "../ui/stem_pads.h"
/* PLAY screen at 800x480, dark mode, after the flagship's two- and four-deck
   views (the 2026-10-18 flagship UI study in docs/research, sections 4.4, 5
   and 6.2): a slim top bar of global chips and the BEAT FX readout, one
   waveform lane per deck stacked under one zoom with a narrow deck-info column
   each, and one bottom deck strip per deck (halves for two decks, quarters for
   four). The mod header buttons are drawn over the bar's right end by
   shell.c. Positions are extrapolated to this frame (xz_shell_elapsed_now)
   and lanes scroll in whole track pixels, so the picture moves every frame
   without shimmer. */
#include "shell_internal.h"
#include "shell_axis.h"
#include "../ui/layout_cycle.h"
#include "../ui/deck_view.h"
#include "../ui/route_card.h"
#include "../ui/deck_colour.h"
#include <stdio.h>
#include <string.h>

enum {
 TOP_H=26, COL_W=96, WAVE_X=98, WAVE_W=XZ_SHELL_WIDTH-WAVE_X-2, PAD=8,
 STEM_X=46, STEM_W=62, STEM_GAP=6, STEM_H=22, LAMP_W=14, LAMP_GAP=3,
 CHIP_Y=5, CHIP_H=16, CHIP_GAP=8,
 INFO_W=44, INFO_X=XZ_SHELL_HEADER_X-INFO_W-2, FX_Y=2, FX_H=22, FX_GAP=10,
 ALERT_BTN_W=150, GRID_BTN_W=78, GRID_BTN_GAP=4, HIT_H=44,
 /* The BEAT FX strip ends here in every view: the DECK VIEW button follows it (deck_view.h). */
 FX_END=498
};
/* Play-screen geometry for one deck count, chosen once per call (layout_of). The drawing and the hit
   tests read every position from here; an element a view leaves out has a negative position or no
   size. Strip offsets are from the strip's top left. */
struct strip_layout {
 int y, h, x0, pitch, w, pad;        /* strip i spans x0+i*pitch .. +w */
 int badge_y, badge_w, badge_h;      /* DECK n; the play state icon sits on the same row */
 int title_y, title_artist_y, artist_y; /* title alone, title over the artist (artist_y < 0: no artist line) */
 int artwork;                        /* artwork, or a note, before the title */
 int rule_y;
 int track;                          /* the TRACK number at the left of the numbers row */
 int num_y;                          /* REMAIN / TIME caption row */
 int time_dx, time_value_dy, time_face, time_frac; /* time column; time_frac: the .ms digits */
 struct xz_shell_rect time_hit;      /* REMAIN / TIME touch target */
 int quantize_right;                 /* where the QUANTIZE lamp and beat end; 0: not in the strip */
 int tempo_dx, tempo_dy, tempo_value_dy;
 int lamps_dx, lamps_dy;             /* MT and SYNC */
 int bpm_dx, bpm_dy, bpm_h;          /* the BPM box runs from bpm_dx to the strip's padded right edge */
 int chip_dy, overview_dy, overview_h;
 int scale;                          /* the minute scale under the overview */
};
struct play_layout {
 int lane_y, lane_h, lane_gap;
 int tag_h, tag_text_dy, phase_dy, source_y;
 int key_rule_y, key_caption_y, key_name_y, key_original_y; /* key_caption_y < 0: no KEY caption or KEY SYNC chip */
 int column_q;                       /* the QUANTIZE lamp ends the source row instead of the strip's caption row */
 int tick_h, edge, alert_h;
 int grid_dy, grid_h, grid_right;    /* GRID buttons: drawn grid_h tall, answering HIT_H tall */
 int grid_in_stem_row;               /* GRID edit takes the active deck's stem row while it shows */
 int pad_row_y, pad_row_h;           /* the pad row between lanes and strips; 0 high: none */
 int stem_rows_half;                 /* stem rows draw half height below the lane, only for a deck with stem buttons */
 struct strip_layout strip;
};
static const struct play_layout two_deck={
 .lane_y=26, .lane_h=116, .lane_gap=3,
 .tag_h=23, .tag_text_dy=4, .phase_dy=10, .source_y=28,
 .key_rule_y=46, .key_caption_y=47, .key_name_y=69, .key_original_y=99, .column_q=0,
 .tick_h=10, .edge=12, .alert_h=52,
 .grid_dy=28, .grid_h=44, .grid_right=XZ_SHELL_WIDTH-8, .grid_in_stem_row=0,
 .pad_row_y=264, .pad_row_h=28, .stem_rows_half=0,
 .strip={.y=292, .h=188, .x0=2, .pitch=XZ_SHELL_WIDTH/2, .w=XZ_SHELL_WIDTH/2-4, .pad=8,
  .badge_y=5, .badge_w=44, .badge_h=20, .title_y=6, .title_artist_y=1, .artist_y=19, .artwork=1, .rule_y=29,
  .track=1, .num_y=34, .time_dx=60, .time_value_dy=12, .time_face=SH_NUM_BIG, .time_frac=1,
  .time_hit={68,32,150,56}, .quantize_right=206,
  .tempo_dx=216, .tempo_dy=34, .tempo_value_dy=18, .lamps_dx=216, .lamps_dy=76,
  .bpm_dx=298, .bpm_dy=32, .bpm_h=52, .chip_dy=97, .overview_dy=112, .overview_h=44, .scale=1}
};
/* Four lanes of 76 px share the zoom; four quarter strips keep the numbers a DJ reads mid-mix. Left
   out: the pad row, the strip's artist, artwork, TRACK, .ms digits and minute scale, and the column's
   KEY caption and KEY SYNC chip (MT blinks while KEY SYNC holds). */
static const struct play_layout four_deck={
 .lane_y=26, .lane_h=76, .lane_gap=2,
 .tag_h=22, .tag_text_dy=2, .phase_dy=8, .source_y=23,
 .key_rule_y=41, .key_caption_y=-1, .key_name_y=44, .key_original_y=65, .column_q=1,
 .tick_h=8, .edge=9, .alert_h=48,
 .grid_dy=52, .grid_h=24, .grid_right=710, .grid_in_stem_row=1,
 .pad_row_y=0, .pad_row_h=0, .stem_rows_half=1,
 .strip={.y=339, .h=141, .x0=2, .pitch=XZ_SHELL_WIDTH/4, .w=XZ_SHELL_WIDTH/4-4, .pad=6,
  .badge_y=2, .badge_w=56, .badge_h=22, .title_y=1, .title_artist_y=1, .artist_y=-1, .artwork=0, .rule_y=24,
  .track=0, .num_y=28, .time_dx=0, .time_value_dy=12, .time_face=SH_NUM_MID, .time_frac=0,
  .time_hit={6,26,84,44}, .quantize_right=0,
  .tempo_dx=0, .tempo_dy=64, .tempo_value_dy=12, .lamps_dx=90, .lamps_dy=80,
  .bpm_dx=90, .bpm_dy=27, .bpm_h=50, .chip_dy=100, .overview_dy=113, .overview_h=24, .scale=0}
};
static const struct play_layout *layout_of(const struct xz_shell_model *m) { return sh_play_deck_count(m)==4?&four_deck:&two_deck; }
/* The route came up under the decks 3 and 4 card: READY holds, then the card wipes away (route_card.h). */
static int card_success(const struct xz_shell_model *m) {
 return sh_deck_count(m)==4&&m->route_card_ready_at_ms&&
  m->clock_ms-m->route_card_ready_at_ms<(uint32_t)(XZ_ROUTE_CARD_READY_HOLD_MS+XZ_ROUTE_CARD_WIPE_MS);
}
/* DECK VIEW chooses the geometry alone: four slots even while only two decks play, decks 3 and 4 then
   drawn as placeholders. TWO keeps MODERN's assigned pair. Audio deck_count never changes here. */
void xz_shell_play_projection(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_shell_model *view) {
 *view=*m;view->play_placeholders=0;
 if(m->layout_preference>0){
  int layout=xz_deck_view_valid(m->layout_preference-1);
  view->play_view_count=layout!=XZ_LAYOUT_TWO?4:2;
  view->wave_order=layout==XZ_LAYOUT_MIXER;
  if(view->play_view_count==4&&(sh_deck_count(m)!=4||card_success(m)))view->play_placeholders=0xc;
 }else view->play_view_count=sh_deck_count(m)==4&&u->play_decks!=2?4:2;
}
static const int zoom_pps[XZ_SHELL_ZOOM_MAX+1]={32,48,64,96,128,192,256};
static const char *const cue_letters[XZ_SHELL_HOT_CUES]={"A","B","C","D","E","F","G","H"};
static const struct xz_canvas measure={NULL,XZ_SHELL_WIDTH,XZ_SHELL_WIDTH,XZ_SHELL_HEIGHT,XZ_THEME_FLAGSHIP};

/* ---- top bar geometry ---- */
struct chip { struct xz_shell_rect r; enum xz_shell_setting setting; };
static void top_chips(struct chip out[3]) {
 int x=PAD;
 int w0=sh_text_width(measure,"SINGLE",SH_SMALL)+sh_text_width(measure,"CONTINUE",SH_SMALL)+28;
 out[0]=(struct chip){{x,CHIP_Y,w0,CHIP_H},XZ_SHELL_SET_PLAY_MODE};x+=w0+CHIP_GAP;
 int w1=sh_text_width(measure,"A. HOT CUE",SH_SMALL)+12;
 out[1]=(struct chip){{x,CHIP_Y,w1,CHIP_H},XZ_SHELL_SET_HOT_CUE_AUTO_LOAD};x+=w1+CHIP_GAP;
 int w2=sh_text_width(measure,"A. CUE",SH_SMALL)+12;
 out[2]=(struct chip){{x,CHIP_Y,w2,CHIP_H},XZ_SHELL_SET_AUTO_CUE};
}
static int fx_left(void) { struct chip k[3];top_chips(k);return k[2].r.x+k[2].r.w+FX_GAP; }
/* The BEAT FX strip ends before the DECKS button, which is always there. */
static int fx_right(const struct xz_shell_model *m) { (void)m;return FX_END; }
static int mode_lamps(const struct xz_shell *u) { return sh_indicator_shown(u,XZ_SHELL_IND_MODE_LAMPS); }
/* The BEAT FX strip as drawn: XZ_SHELL_R_FX, reaching to the bar's left edge while the mode lamps are hidden. */
static struct xz_shell_rect fx_rect(const struct xz_shell *u, const struct xz_shell_model *m) {
 struct xz_shell_rect r=xz_shell_play_rect(m,XZ_SHELL_R_FX,0);
 if(!mode_lamps(u)){r.w+=r.x-PAD;r.x=PAD;}
 return r;
}

static int lane_y(const struct play_layout *L, const struct xz_shell_model *m, int deck) {
 int row=L==&two_deck?(deck&1):m&&m->wave_order==1?sh_strip_column(4,deck):deck;
 return L->lane_y+row*(L->lane_h+L->lane_gap);
}
/* The stem rows (shell.h enum xz_shell_stem_layout): -1 while the layout is not read, which keeps the
   small mute buttons in the pad row. */
enum { STEM_ROW_FULL=48, STEM_ROW_HALF=24, STEM_ROW_GAP=6, STEM_SLIDE=8 };
static int stem_layout(const struct xz_shell_model *m) {
 return m->stem_layout_known&&(unsigned)m->stem_layout<XZ_SHELL_STEM_LAYOUTS?(int)m->stem_layout:-1;
}
/* The small buttons in the pad row: the PADS layout, and an unread layout (an older runtime). */
static int pad_row(const struct xz_shell_model *m) { int layout=stem_layout(m);return layout_of(m)->pad_row_h&&(layout<0||layout==XZ_SHELL_STEMS_PADS); }
static int stem_row_full(int layout) { return layout==XZ_SHELL_STEMS_BELOW_FULL||layout==XZ_SHELL_STEMS_OUTSIDE_FULL; }
static int stem_buttons(const struct xz_shell_model *m, int deck);
struct xz_shell_rect xz_shell_stem_row(const struct xz_shell_model *m, int deck) {
 if(!m||!sh_play_visible(m,deck))return (struct xz_shell_rect){0,0,0,0};
 const struct play_layout *L=layout_of(m);
 int layout=stem_layout(m);
 if(L->stem_rows_half){
  if(layout==XZ_SHELL_STEMS_HIDDEN||!stem_buttons(m,deck))return (struct xz_shell_rect){0,0,0,0};
  return (struct xz_shell_rect){WAVE_X,lane_y(L,m,deck)+L->lane_h-STEM_ROW_HALF,WAVE_W,STEM_ROW_HALF};
 }
 if(layout<=XZ_SHELL_STEMS_HIDDEN||layout==XZ_SHELL_STEMS_PADS)return (struct xz_shell_rect){0,0,0,0};
 int h=stem_row_full(layout)?STEM_ROW_FULL:STEM_ROW_HALF;
 int top=(deck&1)==0&&(layout==XZ_SHELL_STEMS_OUTSIDE_FULL||layout==XZ_SHELL_STEMS_OUTSIDE_HALF);
 return (struct xz_shell_rect){WAVE_X,top?lane_y(L,m,deck):lane_y(L,m,deck)+L->lane_h-h,WAVE_W,h};
}
/* The waveform's part of a deck's band: all of it, less the stem row. */
static int wave_title_shown(const struct xz_shell *u,const struct xz_shell_model *m) {
 return sh_four_view(m)&&sh_indicator_shown(u,XZ_SHELL_IND_WAVE_TITLES);
}
static struct xz_shell_rect wave_band(const struct play_layout *L,const struct xz_shell *u, const struct xz_shell_model *m, int deck) {
 struct xz_shell_rect row=xz_shell_stem_row(m,deck),w={WAVE_X,lane_y(L,m,deck),WAVE_W,L->lane_h};
 if(row.h){w.h-=row.h;if(row.y==w.y)w.y+=row.h;}
 if(wave_title_shown(u,m)){w.y+=20;w.h-=20;}
 return w;
}
static int strip_x(const struct play_layout *L, int deck) { return L->strip.x0+(L==&four_deck?sh_strip_column(4,deck):(deck&1))*L->strip.pitch; }
/* A touch target padded upwards to HIT_H. */
static struct xz_shell_rect hit_up(struct xz_shell_rect r) {
 if(r.h<HIT_H){r.y-=HIT_H-r.h;r.h=HIT_H;}
 return r;
}
/* The lowest lane that shows a playing deck: ZOOM / GRID sit at its foot (beside the card while decks 3
   and 4 are placeholders). */
static int zoom_deck(const struct xz_shell_model *m) {
 for(int slot=sh_play_deck_count(m)-1;slot>0;slot--)if(!sh_play_placeholder(m,sh_play_deck(m,slot)))return sh_play_deck(m,slot);
 return sh_play_deck(m,0);
}
struct xz_shell_rect xz_shell_play_rect(const struct xz_shell_model *m, enum xz_shell_play_region r, int deck) {
 const struct play_layout *L=layout_of(m);
 const struct strip_layout *S=&L->strip;
 int ly=lane_y(L,m,deck),sx=strip_x(L,deck),last=m?zoom_deck(m):1;
 if(m&&!sh_play_visible(m,deck)&&r!=XZ_SHELL_R_TOP_BAR&&r!=XZ_SHELL_R_ZOOM&&r!=XZ_SHELL_R_INFO_BUTTON&&r!=XZ_SHELL_R_FX&&r!=XZ_SHELL_R_VIEW_TWO&&r!=XZ_SHELL_R_VIEW_FOUR)return (struct xz_shell_rect){0,0,0,0};
 /* A placeholder keeps its column, lane and strip for drawing; nothing on it answers touch. */
 if(sh_play_placeholder(m,deck)&&r!=XZ_SHELL_R_INFO_COLUMN&&r!=XZ_SHELL_R_LANE&&r!=XZ_SHELL_R_STRIP&&r!=XZ_SHELL_R_TOP_BAR&&
    r!=XZ_SHELL_R_ZOOM&&r!=XZ_SHELL_R_INFO_BUTTON&&r!=XZ_SHELL_R_FX&&r!=XZ_SHELL_R_VIEW_TWO&&r!=XZ_SHELL_R_VIEW_FOUR)return (struct xz_shell_rect){0,0,0,0};
 switch(r){
 case XZ_SHELL_R_VIEW_TWO:{
  const struct xz_dv_rect b=xz_deck_view_modern.button;
  return (struct xz_shell_rect){b.x,b.y,b.w,b.h};
 }
 case XZ_SHELL_R_VIEW_FOUR:return (struct xz_shell_rect){0,0,0,0};
 case XZ_SHELL_R_TOP_BAR:return (struct xz_shell_rect){0,0,XZ_SHELL_WIDTH,TOP_H};
 case XZ_SHELL_R_INFO_COLUMN:return (struct xz_shell_rect){0,ly,COL_W,L->lane_h};
 case XZ_SHELL_R_KEY:return (struct xz_shell_rect){0,ly+L->lane_h-44,COL_W,44};
 case XZ_SHELL_R_DECK_TAG:return (struct xz_shell_rect){0,ly,COL_W,L->tag_h};
 case XZ_SHELL_R_LANE:return (struct xz_shell_rect){COL_W,ly,XZ_SHELL_WIDTH-COL_W,L->lane_h};
 case XZ_SHELL_R_STRIP:return (struct xz_shell_rect){sx,S->y,S->w,S->h};
 case XZ_SHELL_R_TIME:return (struct xz_shell_rect){sx+S->time_hit.x,S->y+S->time_hit.y,S->time_hit.w,S->time_hit.h};
 case XZ_SHELL_R_OVERVIEW:return (struct xz_shell_rect){sx+S->pad,S->y+S->overview_dy,S->w-2*S->pad,S->overview_h};
 case XZ_SHELL_R_ZOOM:return (struct xz_shell_rect){XZ_SHELL_WIDTH-84,lane_y(L,m,last)+L->lane_h-17,82,15};
 case XZ_SHELL_R_INFO_BUTTON:return (struct xz_shell_rect){INFO_X,0,INFO_W,TOP_H};
 case XZ_SHELL_R_FX:{int x=fx_left();return (struct xz_shell_rect){x,FX_Y,fx_right(m)-x,FX_H};}
 case XZ_SHELL_R_PAD_ROW:return L->pad_row_h?(struct xz_shell_rect){sx,L->pad_row_y,S->w,L->pad_row_h}:(struct xz_shell_rect){0,0,0,0};
 case XZ_SHELL_R_ALERT_BUTTON:return (struct xz_shell_rect){XZ_SHELL_WIDTH-2-ALERT_BTN_W-6,ly+L->lane_h/2-22,ALERT_BTN_W,HIT_H};
 case XZ_SHELL_R_GRID_RESET:case XZ_SHELL_R_GRID_SNAP:case XZ_SHELL_R_GRID_SHIFT:{
  int i=(int)r-(int)XZ_SHELL_R_GRID_RESET,x0=L->grid_right-3*GRID_BTN_W-2*GRID_BTN_GAP;
  return hit_up((struct xz_shell_rect){x0+i*(GRID_BTN_W+GRID_BTN_GAP),ly+L->grid_dy,GRID_BTN_W,L->grid_h});
 }
 }
 return (struct xz_shell_rect){0,0,0,0};
}
static uint32_t key_projection(const struct xz_shell *u,const struct xz_shell_model *m){
 if(xz_shell_screen(u)!=XZ_SHELL_PLAY)return 0;
 struct xz_shell_model view;xz_shell_play_projection(u,m,&view);
 /* Bit 0 valid, bit 1 four available, bit 2 four visible, bit 3 mixer order,
  * bits 4/5 selected sides. Unused geometry values are normalized. */
 int four=sh_deck_count(&view)==4,lanes=sh_play_deck_count(&view)==4;
 return 1u|(four?2u:0u)|(lanes?4u:0u)|((four&&lanes&&view.wave_order==1)?8u:0u)|
  (four?((uint32_t)view.sides_extra&3u)<<4:0u);
}
void sh_play_key_publish(struct xz_shell *u,const struct xz_shell_model *m){
 struct xz_shell_model view;xz_shell_play_projection(u,m,&view);
 memset(&u->key_frame,0,sizeof u->key_frame);u->key_frame.projection=key_projection(u,m);
 if(!u->key_frame.projection)return;
 for(int d=0;d<XZ_SHELL_DECKS;d++){
  struct xz_shell_rect r=xz_shell_play_rect(&view,XZ_SHELL_R_KEY,d);
  u->key_frame.key[d].x=r.x;u->key_frame.key[d].y=r.y;u->key_frame.key[d].w=r.w;u->key_frame.key[d].h=r.h;
 }
}
uint32_t xz_shell_play_key_frame(const struct xz_shell *u,const struct xz_shell_model *m){
 uint32_t current=key_projection(u,m);
 return current&&u->key_frame.projection==current?current:0;
}
int xz_shell_play_key_deck(const struct xz_shell *u,const struct xz_shell_model *m,int x,int y){
 if(xz_shell_screen(u)!=XZ_SHELL_PLAY||!u->key_frame.projection)return -1;
 for(int d=0;d<XZ_SHELL_DECKS;d++){
  const struct xz_shell_rect r={u->key_frame.key[d].x,u->key_frame.key[d].y,u->key_frame.key[d].w,u->key_frame.key[d].h};
  if(r.w&&sh_inside(r,x,y))return xz_shell_play_key_frame(u,m)?d:-2;
 }
 return -1;
}
int32_t xz_shell_overview_ms(const struct xz_shell_model *m, int32_t total_ms, int x, int deck) {
 struct xz_shell_rect ov=xz_shell_play_rect(m,XZ_SHELL_R_OVERVIEW,deck);
 int rel=x-ov.x;
 if(total_ms<=0)return 0;
 if(rel<0)rel=0;if(rel>ov.w-1)rel=ov.w-1;
 return (int32_t)((int64_t)rel*total_ms/(ov.w-1));
}
static int overview_x(struct xz_shell_rect ov, int32_t ms, int32_t total) {
 if(total<=0)return ov.x;
 if(ms<0)ms=0;if(ms>total)ms=total;
 return ov.x+(int)((int64_t)ms*(ov.w-1)/total);
}

/* ---- DECK VIEW: the top-bar button, its picker and the decks 3 and 4 card (ui/deck_view.h, ui/route_card.h) ----
   All three read the PLAY projection. The picker and the encoder focus are shell state; the card is the
   model's route observation. A touch acts only on the overlay last painted (deck_view.painted). */
static int view_of(const struct xz_shell_model *m) {
 return sh_play_deck_count(m)==2?XZ_LAYOUT_TWO:m->wave_order==1?XZ_LAYOUT_MIXER:XZ_LAYOUT_FOUR;
}
static int recent(const struct xz_shell *u, const struct xz_shell_model *m) {
 return m->clock_ms-u->deck_view.input_at<(uint32_t)XZ_DECK_VIEW_PICKER_TIMEOUT_MS;
}
static int picker_live(const struct xz_shell *u, const struct xz_shell_model *m) {
 return xz_shell_screen(u)==XZ_SHELL_PLAY&&u->deck_view.open&&recent(u,m);
}
static int focus_live(const struct xz_shell *u, const struct xz_shell_model *m) {
 return xz_shell_screen(u)==XZ_SHELL_PLAY&&!u->deck_view.open&&u->deck_view.focus!=XZ_SHELL_FOCUS_NONE&&recent(u,m);
}
static int card_shown(const struct xz_shell_model *m) { return sh_play_deck_count(m)==4&&(m->play_placeholders&0xc)==0xc; }
/* The card's input: the route as the runtime reports it. A READY report the deck count has not caught
   up with yet reads as starting; the success hold only ever follows a real route-up. */
static struct xz_route_card_input card_input(const struct xz_shell *u, const struct xz_shell_model *m) {
 struct xz_route_card_input in={0};
 int up=sh_deck_count(m)==4;
 in.state=up?XZ_EXTRA_ROUTE_READY:m->route_state==XZ_EXTRA_ROUTE_READY?XZ_EXTRA_ROUTE_STARTING:m->route_state;
 in.fail_part=m->route_fail_part;in.reason=m->route_reason;in.now_ms=m->clock_ms;
 uint32_t input=u->deck_view.input_at,since=m->route_card_since_ms;
 in.rest_from_ms=!input?since:!since?input:(int32_t)(input-since)>0?input:since;
 in.ready_at_ms=up?m->route_card_ready_at_ms:0;
 in.focus=!focus_live(u,m)?-1:u->deck_view.focus==XZ_SHELL_FOCUS_CARD_PRIMARY?XZ_ROUTE_CARD_HIT_PRIMARY:
  u->deck_view.focus==XZ_SHELL_FOCUS_CARD_TWO?XZ_ROUTE_CARD_HIT_TWO:-1;
 in.step_ms=sh_picture_step_ms(m);
 return in;
}
uint32_t sh_picture_step_ms(const struct xz_shell_model *m) {
 if(m->frame_late)return 66u;
 for(int d=0;d<2;d++)if(m->deck[d].playing)return 33u;   /* the native decks 1 and 2 */
 return XZ_ROUTE_CARD_FRAME_MS;
}
static int card_lane(const struct xz_shell_model *m, int deck);
static int card_hit(const struct xz_shell *u, const struct xz_shell_model *m, int x, int y) {
 if(!card_shown(m))return XZ_ROUTE_CARD_HIT_NONE;
 struct xz_route_card_input in=card_input(u,m);
 return xz_route_card_hit(&xz_route_card_modern,card_lane(m,2),card_lane(m,3),&in,x,y);
}
/* What a touch must match: view, readiness, the picker, and the card's state and buttons. */
static uint32_t overlay_token(const struct xz_shell *u, const struct xz_shell_model *m) {
 if(xz_shell_screen(u)!=XZ_SHELL_PLAY)return 0;
 uint32_t t=1u|(uint32_t)view_of(m)<<1|(uint32_t)(sh_deck_count(m)==4)<<3|(uint32_t)picker_live(u,m)<<4|1u<<12;
 if(card_shown(m)){
  struct xz_route_card_input in=card_input(u,m);
  if(xz_route_card_visible(&in))t|=1u<<5|(uint32_t)xz_route_card_kind(in.state)<<6|
   (uint32_t)xz_route_card_offers(&in,XZ_ROUTE_CARD_HIT_PRIMARY)<<10|(uint32_t)xz_route_card_offers(&in,XZ_ROUTE_CARD_HIT_TWO)<<11;
 }
 return t;
}
uint32_t xz_shell_motion_ms(const struct xz_shell *u, const struct xz_shell_model *m) {
 return u&&m?xz_shell_play_motion_ms(u,m):0;
}
uint32_t xz_shell_play_motion_ms(const struct xz_shell *u, const struct xz_shell_model *m) {
 if(!u||!m||xz_shell_screen(u)!=XZ_SHELL_PLAY)return 0;
 struct xz_shell_model view;xz_shell_play_projection(u,m,&view);
 if(!card_shown(&view))return 0;
 struct xz_route_card_input in=card_input(u,&view);
 return xz_route_card_motion_ms(&in);
}

/* Readout text. Unknown values take the flagship's dash conventions. */
static void fmt_time(char *out, size_t cap, int32_t ms, char *frac, size_t frac_cap) {
 if(ms<0)ms=0;
 int32_t s=ms/1000,m=s/60;if(m>99){m=99;s=99*60+59;}
 snprintf(out,cap,"%02d:%02d",(int)m,(int)(s%60));
 snprintf(frac,frac_cap,".%03d",(int)(ms%1000));
}
static void fmt_fraction(char *out, size_t cap, int num, int den) {
 if(den<=1||num<=0)snprintf(out,cap,"%d",num<=0?0:num);
 else snprintf(out,cap,"%d/%d",num,den);
}
static enum xz_shell_time_mode time_mode_of(const struct xz_shell_model *m, const struct xz_shell_deck *d, int *known) {
 if(sh_has(d,XZ_SHELL_F_TIME_MODE)){*known=1;return d->time_mode;}
 if(sh_setting(m,XZ_SHELL_SET_TIME_MODE)){*known=1;return m->settings.time_mode;}
 *known=0;return XZ_SHELL_TIME_REMAIN;
}
static int playhead_x(const struct xz_shell *u) {
 return WAVE_X+(sh_wave_position(u)==XZ_SHELL_POSITION_LEFT?WAVE_W*27/100:WAVE_W/2);
}
/* A four-deck lane's or strip's deck number in its badge (deck_colour.h), dimmed while the deck is not live
   (bg is what it sits on); a black column parts it from the rest of the tag. */
enum { NUMBER_BOX_W=24 };
/* 2 DECKS shows the decks the sides control: DECK 3 or 4 there carries its badge like on a four-deck screen,
   so "3 2" reads as decks 3 and 2 at a glance; DECK 1 and 2 keep the plain number (FOUR_DECK.md). */
static int two_deck_box(const struct play_layout *L, int deck) { return L==&two_deck&&deck>=2; }
static void number_box(struct xz_canvas c, int x, int y, int h, int deck, int live, uint32_t bg) {
 char text[4];snprintf(text,sizeof text,"%d",deck+1);
 xz_rect(c,x,y,NUMBER_BOX_W,h,xz_deck_box_fill(deck,live,bg));xz_rect(c,x+NUMBER_BOX_W,y,1,h,SH_BLACK);
 sh_num(c,x+(NUMBER_BOX_W-sh_num_width(SH_NUM_MID,text))/2,y+(h-sh_num_cap(SH_NUM_MID))/2,text,SH_NUM_MID,xz_deck_box_ink(deck,live,bg));
}
static uint32_t cue_color(const struct xz_shell_cue *q) { return q->is_loop?SH_LOOP:q->rgb&0xffffff; }
static uint32_t ink_on(uint32_t fill) { return xz_theme_luma(fill)>150?SH_BLACK:SH_WHITE; }

/* ---- top bar ---- */
static void render_fx(const struct xz_shell_model *m, struct xz_canvas c, struct xz_shell_rect r);
static int alert_on(const struct xz_shell_deck *d);
static void render_alert(const struct play_layout *L, const struct xz_shell_model *m, struct xz_canvas c, const struct xz_shell_deck *d, int deck);
static int grid_edit_shown(const struct xz_shell *u, const struct xz_shell_model *m, int deck);
static void render_grid_buttons(const struct play_layout *L, const struct xz_shell_model *m, struct xz_canvas c, int deck);
static void render_stem_row(const struct xz_shell *, const struct xz_shell_model *m, struct xz_canvas c, int deck);
static struct xz_shell_rect grid_button(const struct play_layout *L, const struct xz_shell_model *m, int deck, int i);
static void render_top(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 struct chip chips[3];top_chips(chips);
 xz_rect(c,0,0,c.width,TOP_H,SH_CHROME);
 xz_rect(c,0,TOP_H-1,c.width,1,SH_BLACK);
 render_fx(m,c,fx_rect(u,m));
 /* DECK VIEW, always on PLAY: 2 DECKS names the pair the sides drive. */
 xz_deck_view_button_draw(c,&xz_deck_view_modern,view_of(m),sh_deck_count(m)==4,sh_deck_count(m)==4?m->sides_extra&3:0,
  picker_live(u,m),focus_live(u,m)&&u->deck_view.focus==XZ_SHELL_FOCUS_DECK_VIEW);
 sh_info_icon(c,INFO_X+INFO_W/2,TOP_H/2,SH_WHITE);
 if(!mode_lamps(u))return;
 int mode_known=sh_setting(m,XZ_SHELL_SET_PLAY_MODE);
 struct xz_shell_rect r=chips[0].r;
 xz_border(c,r.x,r.y,r.w,r.h,SH_DIM);
 uint32_t mode_ink=sh_writable(m,XZ_SHELL_SET_PLAY_MODE)?SH_WHITE:0x8c8c8c;
 sh_text(c,r.x+6,r.y+3,"SINGLE",SH_SMALL,mode_known&&m->settings.play_mode==XZ_SHELL_PLAY_SINGLE?mode_ink:0x5a5a5a);
 int sep=r.x+6+sh_text_width(c,"SINGLE",SH_SMALL)+8;
 xz_rect(c,sep,r.y+3,1,10,SH_DIM);
 sh_text(c,sep+8,r.y+3,"CONTINUE",SH_SMALL,mode_known&&m->settings.play_mode==XZ_SHELL_PLAY_CONTINUE?mode_ink:0x5a5a5a);
 for(int i=1;i<3;i++){
  enum xz_shell_setting s=chips[i].setting;
  int on=sh_setting(m,s)&&(s==XZ_SHELL_SET_AUTO_CUE?m->settings.auto_cue:m->settings.hot_cue_auto_load);
  uint32_t ink=on?(sh_writable(m,s)?SH_WHITE:0x8c8c8c):0x5a5a5a;
  /* HOT CUE AUTO LOAD ON lights red and the library's own setting white, as the XZ lamp does (manual p.55). */
  if(on&&ink==SH_WHITE&&s==XZ_SHELL_SET_HOT_CUE_AUTO_LOAD&&m->settings.hot_cue_auto_load==XZ_SHELL_HCAL_ON)ink=SH_RED;
  sh_chip(c,chips[i].r.x,chips[i].r.y,i==1?"A. HOT CUE":"A. CUE",on?SH_DIM:0x5a5a5a,ink,0);
 }
}

/* ---- BEAT FX readout ----
   Effect, beat, time and the FX BPM in one strip of the top bar. Off it is a
   quiet outline; on it fills with the flagship's FX red, as the mixer's lit FX
   ON button does. Name, beat and time draw their blank on a clear bit; the
   optional readouts (channel, level, BPM) take no room until they are read. */
static int fx_has(const struct xz_shell_fx *f, enum xz_shell_fx_field b) { return (f->known>>b)&1; }
static int fx_on(const struct xz_shell_fx *f) { return fx_has(f,XZ_SHELL_FX_F_ON)&&f->on; }
static const char *fx_channel_text(enum xz_shell_fx_channel ch) {
 static const char *const names[]={[XZ_SHELL_FX_CH_MASTER]="MST",[XZ_SHELL_FX_CH_1]="CH1",[XZ_SHELL_FX_CH_2]="CH2",
  [XZ_SHELL_FX_CH_MIC]="MIC",[XZ_SHELL_FX_CH_3]="CH3",[XZ_SHELL_FX_CH_4]="CH4",[XZ_SHELL_FX_CH_XF_A]="XF-A",[XZ_SHELL_FX_CH_XF_B]="XF-B"};
 return (unsigned)ch<sizeof names/sizeof *names?names[ch]:NULL;
}
static void render_fx(const struct xz_shell_model *m, struct xz_canvas c, struct xz_shell_rect r) {
 const struct xz_shell_fx *f=&m->fx;
 int on=fx_on(f),off=fx_has(f,XZ_SHELL_FX_F_ON)&&!f->on;
 uint32_t ink=off?0xb4b4b4:SH_WHITE,dim=on?0xffc8b4:0x5a5a5a;
 char text[24];
 xz_rect(c,r.x,r.y,r.w,r.h,on?SH_RED:SH_BLACK);
 if(!on)xz_border(c,r.x,r.y,r.w,r.h,SH_RAIL);
 int x=r.x+6,right=r.x+r.w-6,ty=r.y+6,vy=r.y+3;
 /* right end, packed leftwards: BPM, level, channel, each only once read */
 if(fx_has(f,XZ_SHELL_FX_F_BPM)&&f->bpm_x10>0){
  int32_t b=f->bpm_x10>9999?9999:f->bpm_x10;
  if(fx_has(f,XZ_SHELL_FX_F_TAP)){
   const char *src=f->tap?"TAP":"AUTO";
   sh_text_right(c,right,ty,src,SH_SMALL,f->tap?(on?SH_WHITE:SH_RED):dim);
   right-=sh_text_width(c,src,SH_SMALL)+4;
  }
  snprintf(text,sizeof text,"%d.%d",(int)(b/10),(int)(b%10));
  sh_text_right(c,right,ty,text,SH_SMALL,ink);
  right-=sh_text_width(c,"000.0",SH_SMALL)+8;
 }
 if(fx_has(f,XZ_SHELL_FX_F_LEVEL)){
  int pct=f->level_pct<0?0:f->level_pct>100?100:f->level_pct,lw=30;
  xz_rect(c,right-lw,r.y+9,lw,5,on?0xa02a0e:0x2a2a2a);
  xz_rect(c,right-lw,r.y+9,lw*pct/100,5,ink);
  right-=lw+8;
 }
 const char *ch=fx_has(f,XZ_SHELL_FX_F_CHANNEL)?fx_channel_text(f->channel):NULL;
 if(ch){
  int cw=sh_text_width(c,"XF-A",SH_SMALL)+8;
  xz_border(c,right-cw,r.y+4,cw,14,on?SH_WHITE:SH_DIM);
  sh_text_center(c,right-cw/2,ty,ch,SH_SMALL,ink);
  right-=cw+8;
 }
 /* BEAT FX QUANTIZE, its own lamp beside the target (XDJ-AZ manual p.41) */
 if(fx_has(f,XZ_SHELL_FX_F_QUANTIZE)){
  int qw=sh_text_width(c,"Q",SH_SMALL)+8;
  if(f->quantize)xz_rect(c,right-qw,r.y+4,qw,14,on?SH_WHITE:SH_RED);
  else xz_border(c,right-qw,r.y+4,qw,14,on?0xffc8b4:SH_RAIL);
  sh_text_center(c,right-qw/2,ty,"Q",SH_SMALL,f->quantize?(on?SH_RED:SH_WHITE):dim);
  right-=qw+8;
 }
 /* left end: the FX tag, then name, beat and time */
 sh_text(c,x,ty,"FX",SH_SMALL,on?SH_WHITE:SH_DIM);
 x+=sh_text_width(c,"FX",SH_SMALL)+8;
 char beat[12],time[12];
 int has_beat=fx_has(f,XZ_SHELL_FX_F_BEAT)&&f->beat_num>0,has_time=fx_has(f,XZ_SHELL_FX_F_TIME)&&f->time_ms>=0;
 if(has_beat)fmt_fraction(beat,sizeof beat,f->beat_num,f->beat_den);else snprintf(beat,sizeof beat,"-");
 if(has_time)snprintf(time,sizeof time,"%dms",(int)(f->time_ms>99999?99999:f->time_ms));else snprintf(time,sizeof time,"---ms");
 /* An effect without beats (REVERB, PITCH) reads its name alone. */
 int name_known=fx_has(f,XZ_SHELL_FX_F_NAME)&&f->name&&*f->name;
 int show_beat=has_beat||!name_known,show_time=has_time||!name_known;
 int tw=show_time?sh_text_width(c,time,SH_SMALL):0,bw=show_beat?sh_text_width(c,beat,SH_TEXT):0;
 int tail=(show_beat?bw+10:0)+(show_time?tw+8:0);
 int name_w=right-x-tail;
 int nx=x;
 if(name_known){sh_text_fit(c,x,vy,name_w,f->name,SH_TEXT,ink);nx+=sh_min(name_w,sh_text_width(c,f->name,SH_TEXT));}
 else{sh_text(c,x,vy,"---",SH_TEXT,dim);nx+=sh_text_width(c,"---",SH_TEXT);}
 nx+=10;
 if(show_beat){sh_text(c,nx,vy,beat,SH_TEXT,has_beat?ink:dim);nx+=bw+8;}
 if(show_time)sh_text(c,nx,ty,time,SH_SMALL,has_time?ink:dim);
}

/* ---- deck info column ----
   DECK tag (touch: active deck; the bar phase), the track's source, and the
   deck's live key. The key it plays now is the large one, in the traffic-light
   colour against the master deck, with its tuning in cents while it sits off
   the semitone: tempo with MASTER TEMPO off, key shift and KEY SYNC all move
   it, and matching keys by hand means reading exactly this. Under it the
   analysed key, once the playing key has left it. KEY SYNC takes the place of
   the KEY caption. Without the analysed key (an older runtime, no
   XZ_SHELL_F_KEY_ORIGINAL) the key and its key-shift step draw as before.
   The XZ has a pad bank for beat jumps and its QUANTIZE lamp sits in the deck
   strip (in the four-deck view, at the end of the source row), so neither
   takes a row here. */
enum { KEY_RIGHT=COL_W-6, Q_LAMP=15 };
static int same_key(const char *a, const char *b) {
 char x[8],y[8];
 a=sh_key_text(a,XZ_SHELL_KEY_CLASSIC,x,sizeof x);b=sh_key_text(b,XZ_SHELL_KEY_CLASSIC,y,sizeof y);
 return a&&b&&!strcmp(a,b);
}
static void render_key(const struct play_layout *L, const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c, const struct xz_shell_deck *d, int ly) {
 struct xz_shell_deck original;
 if(m->mods_settings_known&&!m->key_feedback){
  original=*d;
  if(sh_has(d,XZ_SHELL_F_KEY_ORIGINAL)&&d->key_original&&*d->key_original)original.key=d->key_original;
  original.key_cents=original.key_shift=original.key_synced=0;d=&original;
 }
 int y=ly+L->key_caption_y,ny=ly+L->key_name_y;
 char text[16],key[8],from[8];
 if(L->key_caption_y>=0){
  if(sh_has(d,XZ_SHELL_F_KEY_SYNC)&&d->key_synced)sh_chip(c,6,y+2,"KEY SYNC",SH_BLUE,SH_WHITE,0);
  else sh_text(c,6,y+5,"KEY",SH_SMALL,SH_DIM);
 }
 if(!sh_has(d,XZ_SHELL_F_KEY)||!d->key||!*d->key){sh_num(c,6,ny,"--",SH_NUM_MID,0x5a5a5a);return;}
 /* The master's own key is the reference, so plain. */
 int is_master=sh_has(d,XZ_SHELL_F_MASTER)&&d->master;
 uint32_t ink=is_master?SH_WHITE:sh_key_ink(m,d->key,d->key_matches_master,SH_WHITE);
 const char *shown=sh_key_text(d->key,sh_key_display(u,m),key,sizeof key);
 int analysed=sh_has(d,XZ_SHELL_F_KEY_ORIGINAL);
 /* Right of the name: the tuning in cents, or (older runtime) the key-shift step. */
 int tail=0;
 if(analysed&&d->key_cents){
  int cents=d->key_cents<-50?-50:d->key_cents>50?50:d->key_cents;
  snprintf(text,sizeof text,"%+d\xc2\xa2",cents);
  tail=sh_text_width(c,text,SH_SMALL);
  sh_text(c,KEY_RIGHT-tail,ny,text,SH_SMALL,SH_WHITE);
 }else if(!analysed&&sh_has(d,XZ_SHELL_F_KEY_SHIFT)&&d->key_shift){
  int steps=d->key_shift<-99?99:d->key_shift>99?99:d->key_shift<0?-d->key_shift:d->key_shift;
  snprintf(text,sizeof text,"%d",steps);
  tail=sh_text_width(c,text,SH_SMALL)+9;
  sh_triangle(c,KEY_RIGHT-tail+3,ny+2,4,d->key_shift<0,SH_WHITE);
  sh_text(c,KEY_RIGHT-tail+9,ny,text,SH_SMALL,SH_WHITE);
 }
 int room=KEY_RIGHT-6-(tail?tail+4:0);
 if(sh_num_drawable(SH_NUM_KEY,shown)&&sh_num_width(SH_NUM_KEY,shown)<=room)sh_num(c,6,ny,shown,SH_NUM_KEY,ink);
 else sh_text_fit(c,6,ny+(sh_num_cap(SH_NUM_KEY)-SH_CAP_TEXT)/2,room,shown,SH_TEXT,ink);
 if(analysed&&d->key_original&&*d->key_original&&!same_key(d->key,d->key_original)){
  int w=sh_text_width(c,"ORIGINAL",SH_SMALL)+5;
  sh_text(c,6,ly+L->key_original_y,"ORIGINAL",SH_SMALL,SH_DIM);
  sh_text_fit(c,6+w,ly+L->key_original_y,KEY_RIGHT-6-w,sh_key_text(d->key_original,sh_key_display(u,m),from,sizeof from),SH_SMALL,SH_WHITE);
 }
}
static void render_column(const struct play_layout *L, const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c, int deck) {
 const struct xz_shell_deck *d=&m->deck[deck];
 int ly=lane_y(L,m,deck),active=u->deck==deck;
 char text[32];
 xz_rect(c,0,ly,COL_W,L->lane_h,SH_CHROME);
 /* The deck tile takes the colour of the stick its track came from (USB COLORS); inactive, a stripe. */
 int control=sh_control_active(m,deck);
 int filled=sh_four_view(m)?control:active;
 uint32_t accent=sh_deck_accent(u,d,SH_BLUE),tile=filled?accent:SH_RAIL,ink=sh_ink_on(tile);
 xz_rect(c,0,ly,COL_W,L->tag_h,tile);
 /* Four lanes: the number in its badge; the USB colour stripe moves to the badge's right. */
 if(L==&four_deck||two_deck_box(L,deck)){number_box(c,0,ly,L->tag_h,deck,1,tile);if(!filled&&accent!=SH_BLUE)xz_rect(c,NUMBER_BOX_W+1,ly,4,L->tag_h,accent);}
 else if(!filled&&accent!=SH_BLUE)xz_rect(c,0,ly,4,L->tag_h,accent);
 snprintf(text,sizeof text,"DECK %d",deck+1);
 if(sh_four_view(m)){
  if(L!=&four_deck&&!two_deck_box(L,deck)){snprintf(text,sizeof text,"%d",deck+1);sh_num(c,6,ly+1,text,SH_NUM_MID,ink);}
  if(control)sh_control_arrow(c,30,ly+1,deck&1);
  else sh_text(c,30,ly+5,"DECK",SH_SMALL,ink);
  if(control)sh_text(c,30,ly+10,"CONTROL",SH_SMALL,ink);
 }else sh_text(c,6+(!filled&&accent!=SH_BLUE?2:0),ly+L->tag_text_dy,text,SH_TEXT,ink);
 if(sh_has(d,XZ_SHELL_F_BAR_PHASE))for(int b=0;b<4;b++)
  xz_rect(c,COL_W-30+b*7,ly+(control?3:L->phase_dy),5,4,d->bar_phase==b+1?ink:xz_blend(tile,ink,80));
 if(sh_four_view(m)&&active)xz_border(c,0,ly,COL_W,L->tag_h,SH_WHITE);
 xz_rect(c,0,ly+L->tag_h,COL_W,1,SH_BLACK);xz_rect(c,0,ly+L->key_rule_y,COL_W,1,SH_BLACK);
 /* source */
 const char *source=sh_has(d,XZ_SHELL_F_SOURCE)?
  (d->source==XZ_SHELL_SRC_USB1?"USB1":d->source==XZ_SHELL_SRC_USB2?"USB2":d->source==XZ_SHELL_SRC_LINK?"LINK":
   d->source==XZ_SHELL_SRC_COMPUTER?"PC":"NONE"):"---";
 uint32_t source_ink=sh_has(d,XZ_SHELL_F_SOURCE)&&d->source!=XZ_SHELL_SRC_NONE?SH_WHITE:SH_DIM;
 /* Where the strip has no room for QUANTIZE, its lamp ends the source row: red on, grey off. The beat
    value is one device setting, read on SHORTCUT and UTILITY. */
 int q_w=0;
 if(L->column_q&&sh_has(d,XZ_SHELL_F_QUANTIZE)&&sh_indicator_shown(u,XZ_SHELL_IND_QUANTIZE)){
  int y=ly+L->source_y+1,x=KEY_RIGHT-Q_LAMP;
  if(d->quantize_on)xz_rect(c,x,y,Q_LAMP,Q_LAMP,SH_RED);else xz_border(c,x,y,Q_LAMP,Q_LAMP,0x5a5a5a);
  sh_text_center(c,x+Q_LAMP/2+1,y+3,"Q",SH_SMALL,d->quantize_on?SH_WHITE:0x5a5a5a);
  q_w=Q_LAMP+4;
 }
 sh_usb_icon(c,7,ly+L->source_y+1,source_ink);
 sh_text_fit(c,24,ly+L->source_y,KEY_RIGHT-24-q_w,source,SH_TEXT,source_ink);
 render_key(L,u,m,c,d,ly);
 if(sh_deck_count(m)==4&&deck>=2&&sh_has(d,XZ_SHELL_F_VINYL))
  sh_text(c,6,ly+L->key_original_y,d->vinyl?"VINYL ON":"VINYL OFF",SH_SMALL,d->vinyl?SH_WHITE:SH_DIM);
}

/* ---- scrolling lane ----
   Everything in a lane is placed by absolute track pixel, the same mapping the
   waveform samples by, so markers never slide against the audio. */
struct lane_view { int64_t head_px; struct sh_wave_axis axis; int ph_x; };
/* Rounded, not floored: a time lands on its nearest track pixel, the playhead included, so the
   picture is never up to a whole pixel late. */
static int64_t track_px(struct sh_wave_axis axis, int32_t ms) { return sh_axis_px(axis,ms); }
static int lane_x(const struct lane_view *v, int32_t ms) {
 int64_t x=v->ph_x+track_px(v->axis,ms)-v->head_px;
 return x<-4096?-4096:x>4096?4096:(int)x;
}
static int32_t lane_ms(const struct lane_view *v, int x) { return sh_axis_ms(v->axis,v->head_px+x-v->ph_x); }
/* Small lane captions sit over the waveform, so they carry a black backing. */
static void lane_caption(struct xz_canvas c, int x, int y, const char *text, uint32_t ink) {
 xz_rect(c,x-2,y-1,sh_text_width(c,text,SH_SMALL)+4,SH_CAP_SMALL+3,SH_BLACK);
 sh_text(c,x,y,text,SH_SMALL,ink);
}
static int fx_on_deck(const struct xz_shell_model *m, int deck) {
 const struct xz_shell_fx *f=&m->fx;
 if(!fx_on(f)||!fx_has(f,XZ_SHELL_FX_F_CHANNEL))return 0;
 return f->channel==XZ_SHELL_FX_CH_MASTER||(f->channel==XZ_SHELL_FX_CH_1&&deck==0)||(f->channel==XZ_SHELL_FX_CH_2&&deck==1);
}
/* Where the track would be without the slip move, advanced like elapsed; -1 when not shown. */
static int32_t slip_now(const struct xz_shell_model *m, const struct xz_shell_deck *d) {
 if(!sh_has(d,XZ_SHELL_F_SLIP)||!d->slip||!sh_has(d,XZ_SHELL_F_SLIP_POSITION))return -1;
 int64_t e=d->slip_ms;
 if(d->slip_at_ms&&sh_has(d,XZ_SHELL_F_PLAYING)&&d->playing){
  uint32_t dt=m->clock_ms-d->slip_at_ms;
  if(dt<=0x7fffffffu){
   if(dt>XZ_SHELL_EXTRAPOLATE_MAX_MS)dt=XZ_SHELL_EXTRAPOLATE_MAX_MS;
   int64_t rate=10000+(sh_has(d,XZ_SHELL_F_TEMPO)?d->tempo_x100:0);
   e+=(int64_t)dt*(rate<0?0:rate)/10000;
  }
 }
 if(e<0)e=0;
 if(sh_has(d,XZ_SHELL_F_TOTAL)&&d->total_ms>0&&e>d->total_ms)e=d->total_ms;
 return (int32_t)e;
}
static void render_lane(const struct play_layout *L, const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c, int deck) {
 const struct xz_shell_deck *d=&m->deck[deck];
 struct xz_shell_rect band=wave_band(L,u,m,deck);
 int ly=band.y,lh=band.h,tick_h=L->tick_h;
 int title_h=wave_title_shown(u,m)?20:0,ruler_top=ly-title_h;
 char text[32];
 xz_rect(c,COL_W,lane_y(L,m,deck),c.width-COL_W,L->lane_h,SH_BLACK);
 /* The deck the Beat FX is applied to carries the FX red on its lane edge. */
 if(fx_on_deck(m,deck))xz_rect(c,COL_W,lane_y(L,m,deck),2,L->lane_h,SH_RED);
 int loaded=sh_has(d,XZ_SHELL_F_LOADED)&&d->loaded;
 int32_t elapsed=loaded?xz_shell_elapsed_now(m,deck):-1;
 struct lane_view v={0,sh_axis(zoom_pps[u->zoom<0?0:u->zoom>XZ_SHELL_ZOOM_MAX?XZ_SHELL_ZOOM_MAX:u->zoom],d),playhead_x(u)};
 if(elapsed>=0){
  v.axis=sh_axis_window(v.axis,elapsed,WAVE_W);
  v.head_px=track_px(v.axis,elapsed);
  int32_t first=lane_ms(&v,WAVE_X),last=lane_ms(&v,WAVE_X+WAVE_W);
  first=first<INT32_MIN+50?INT32_MIN:first-50;last=last>INT32_MAX-50?INT32_MAX:last+50;
  struct xz_canvas lane=xz_sub(c,WAVE_X,ly,WAVE_W,lh);
  if(sh_has(d,XZ_SHELL_F_LOOP)&&d->loop.active&&d->loop.out_ms>d->loop.in_ms){
   int x0=sh_max(WAVE_X,lane_x(&v,d->loop.in_ms)),x1=sh_min(WAVE_X+WAVE_W,lane_x(&v,d->loop.out_ms));
   if(x1>x0)xz_rect(c,x0,ly,x1-x0,lh,xz_blend(SH_BLACK,SH_LOOP,48));
  }
  sh_deck_lane_axis(c,(struct xz_shell_rect){WAVE_X,ly,WAVE_W,lh},lh/2-L->edge,u,m,d,v.head_px-(v.ph_x-WAVE_X),v.axis);
  /* STEMS with no stem layer yet draws 3BAND, and says so, quietly, in the lane corner. */
  if(sh_deck_wave_color(u,d)==XZ_SHELL_WAVE_STEMS&&!sh_stem_wave_ready(d)){
   static const char hint[]="NO STEM WAVE YET \xc2\xb7 3BAND";
   lane_caption(c,WAVE_X+WAVE_W-6-sh_text_width(c,hint,SH_SMALL),ly+lh-tick_h-SH_CAP_SMALL-6,hint,SH_DIM);
  }
  if(sh_has(d,XZ_SHELL_F_GRID)&&d->grid){
   uint32_t begin=v.axis.count?v.axis.lo:sh_axis_lower(d->grid,d->grid_count,first);
   uint32_t end=v.axis.count?v.axis.hi+1:d->grid_count;
   if((uint64_t)begin+114<end)end=begin+114;
   for(uint32_t i=begin;i<end;i++){
    const struct xz_shell_beat *b=&d->grid[i];
    uint32_t first_down=(5u-d->grid[0].beat_in_bar)%4u;
    uint32_t bar=i>=first_down?1u+(i-first_down)/4u:0;
    if(b->ms<first)continue;
    if(b->ms>last)break;
    /* The beat ruler on both edges: beats white, each bar's downbeat taller and red, as the stock lane marks them. */
    int down=b->beat_in_bar==1,x=lane_x(&v,b->ms),h=down?tick_h:tick_h/2;
    if(x<WAVE_X||x>=WAVE_X+WAVE_W)continue;
    uint32_t ink=down?SH_RED:SH_WHITE;
    /* Continue the same beat coordinate behind the title; text is painted last. */
    if(title_h)xz_rect(c,x,ruler_top,1+down,title_h,xz_blend(SH_BLACK,ink,112));
    xz_rect(c,x,ly,1+down,h,ink);xz_rect(c,x,ly+lh-h,1+down,h,ink);
    if(b->beat_in_bar==1&&bar%4==0){snprintf(text,sizeof text,"%u",bar);lane_caption(lane,x-WAVE_X+3,lh-tick_h-2,text,SH_DIM);}
   }
  }
  if(sh_has(d,XZ_SHELL_F_MEMORY_CUES))for(int i=0;i<d->memory_count&&i<XZ_SHELL_MEMORY_CUES;i++){
   const struct xz_shell_cue *q=&d->memory_cue[i];
   if(!q->set||q->ms<first||q->ms>last)continue;
   int x=lane_x(&v,q->ms);
   xz_rect(c,x,ly,1,tick_h+2,SH_MEMORY);xz_rect(c,x,ly+lh-tick_h-2,1,tick_h+2,SH_MEMORY);
  }
  if(sh_has(d,XZ_SHELL_F_CUE_POINT)&&d->cue_ms>=first&&d->cue_ms<=last)
   sh_triangle(c,lane_x(&v,d->cue_ms),ly+lh-7,5,0,SH_LOOP);
  if(sh_has(d,XZ_SHELL_F_HOT_CUES))for(int i=0;i<XZ_SHELL_HOT_CUES;i++){
   const struct xz_shell_cue *q=&d->hot_cue[i];
   if(!q->set||q->ms<first||q->ms>last)continue;
   int x=lane_x(&v,q->ms);uint32_t fill=cue_color(q);
   xz_rect(c,x,ly,1,lh,xz_blend(SH_BLACK,fill,140));
   xz_rect(lane,x-WAVE_X,tick_h+1,12,12,fill);
   sh_text(lane,x-WAVE_X+3,tick_h+2,cue_letters[i],SH_SMALL,ink_on(fill));
  }
  if(sh_has(d,XZ_SHELL_F_LOOP)&&d->loop.active){
   int x0=lane_x(&v,d->loop.in_ms),x1=lane_x(&v,d->loop.out_ms);
   if(x0>=WAVE_X&&x0<WAVE_X+WAVE_W)xz_rect(c,x0,ly,1,lh,SH_LOOP);
   if(x1>=WAVE_X&&x1<WAVE_X+WAVE_W)xz_rect(c,x1,ly,1,lh,SH_LOOP);
   char beats[16];fmt_fraction(beats,sizeof beats,d->loop.beats_num,d->loop.beats_den);
   if(d->loop.beats_num>0)snprintf(text,sizeof text,"%s BEAT LOOP",beats);else snprintf(text,sizeof text,"LOOP");
   lane_caption(c,WAVE_X+6,ly+tick_h+3,text,SH_LOOP);
  }
  /* beat countdown to the next cue ahead of the playhead, in bars.beats */
  if(v.axis.grid){
   int32_t next=INT32_MAX;
   if(sh_has(d,XZ_SHELL_F_HOT_CUES))for(int i=0;i<XZ_SHELL_HOT_CUES;i++)if(d->hot_cue[i].set&&d->hot_cue[i].ms>elapsed&&d->hot_cue[i].ms<next)next=d->hot_cue[i].ms;
   if(sh_has(d,XZ_SHELL_F_MEMORY_CUES))for(int i=0;i<d->memory_count&&i<XZ_SHELL_MEMORY_CUES;i++)if(d->memory_cue[i].set&&d->memory_cue[i].ms>elapsed&&d->memory_cue[i].ms<next)next=d->memory_cue[i].ms;
   if(sh_has(d,XZ_SHELL_F_CUE_POINT)&&d->cue_ms>elapsed&&d->cue_ms<next)next=d->cue_ms;
   if(next!=INT32_MAX){
    uint32_t a=sh_axis_lower(d->grid,d->grid_count,elapsed),b=sh_axis_lower(d->grid,d->grid_count,next);
    if(a<d->grid_count&&d->grid[a].ms==elapsed)a++;
    if(b<d->grid_count&&d->grid[b].ms==next)b++;
    unsigned beats=b>=a?b-a:65;
    if(beats<=64){snprintf(text,sizeof text,"%u.%u",beats/4,beats%4);lane_caption(c,v.ph_x+5,ly+tick_h+3,text,SH_WHITE);}
   }
  }
  /* The slip ghost: a dashed amber line where the track will land when SLIP lets go. */
  int32_t ghost=slip_now(m,d);
  if(ghost>=0){
   int gx=lane_x(&v,ghost);
   if(gx>=WAVE_X&&gx<WAVE_X+WAVE_W){
    for(int y=ly;y<ly+lh;y+=6)xz_rect(c,gx,y,1,3,SH_KEY_AMBER);
    lane_caption(c,sh_min(gx+4,WAVE_X+WAVE_W-34),ly+lh/2-6,"SLIP",SH_KEY_AMBER);
   }
  }
  /* A loop point under the jog: its line doubled in white and its time in a box (XZ manual pp.47-48). */
  if(sh_has(d,XZ_SHELL_F_LOOP_EDIT)){
   int ex=lane_x(&v,d->loop_edit.ms);
   if(ex>=WAVE_X&&ex<WAVE_X+WAVE_W-1)xz_rect(c,ex,ly,2,lh,SH_WHITE);
   char t[40],clock[16],frac[8];
   fmt_time(clock,sizeof clock,d->loop_edit.ms,frac,sizeof frac);
   snprintf(t,sizeof t,"%s %s%s",d->loop_edit.endpoint?"LOOP OUT":"LOOP IN",clock,frac);
   int tw=sh_text_width(c,t,SH_SMALL)+12,bx=WAVE_X+6,by=ly+lh-tick_h-24;
   xz_rect(c,bx,by,tw,18,SH_LOOP);
   sh_text(c,bx+6,by+4,t,SH_SMALL,SH_BLACK);
  }
 }else if(sh_has(d,XZ_SHELL_F_LOADED)&&!d->loaded)
  sh_text_center(c,WAVE_X+WAVE_W/2,ly+lh/2-7,"Not Loaded.",SH_TEXT,0x5a5a5a);
 xz_rect(c,v.ph_x,ruler_top,1,lh+title_h,SH_PLAYHEAD);
 /* ON AIR: the channel is live on the master (XZ manual p.70), red on the lane's top right. */
 if(sh_has(d,XZ_SHELL_F_ON_AIR)&&d->on_air){
  int w=sh_text_width(c,"ON AIR",SH_SMALL)+12;
  xz_rect(c,WAVE_X+WAVE_W-w-4,ly+3,w,16,SH_RED);
  sh_text(c,WAVE_X+WAVE_W-w+2,ly+6,"ON AIR",SH_SMALL,SH_WHITE);
 }
 if(title_h){
  const char *title=sh_has(d,XZ_SHELL_F_TITLE)&&d->title?d->title:"---";
  int title_x=WAVE_X+6,title_right=v.ph_x-10;
  sh_text_fit(c,title_x,ruler_top+2,sh_max(0,title_right-title_x),title,SH_TEXT,SH_WHITE);
 }
 if(alert_on(d))render_alert(L,m,c,d,deck);
 int grid=grid_edit_shown(u,m,deck);
 if(grid)render_grid_buttons(L,m,c,deck);
 if(!grid||!L->grid_in_stem_row)render_stem_row(u,m,c,deck);
}
/* ---- persistent deck conditions and GRID mode buttons ---- */
static int alert_on(const struct xz_shell_deck *d) {
 return sh_has(d,XZ_SHELL_F_ALERT)&&d->alert.kind>XZ_SHELL_ALERT_NONE&&d->alert.kind<=XZ_SHELL_ALERT_UNSUPPORTED_FILE;
}
static void render_alert(const struct play_layout *L, const struct xz_shell_model *m, struct xz_canvas c, const struct xz_shell_deck *d, int deck) {
 static const char *const title[]={"","EMERGENCY LOOP","CANNOT READ THE TRACK","UNSUPPORTED FILE"};
 static const char *const line[]={"","THE DECK LOOPS UNTIL YOU LOAD A TRACK","THE MEDIUM DID NOT ANSWER","THIS FILE CANNOT PLAY HERE"};
 int ly=lane_y(L,m,deck),y=ly+(L->lane_h-L->alert_h)/2;
 struct xz_shell_rect b=xz_shell_play_rect(m,XZ_SHELL_R_ALERT_BUTTON,deck);
 xz_rect(c,WAVE_X,y,WAVE_W,L->alert_h,0x5a0c0c);
 xz_rect(c,WAVE_X,y,4,L->alert_h,SH_RED);
 int x=WAVE_X+14,room=b.x-x-10;
 char head[64];
 snprintf(head,sizeof head,"%s%s%s",title[d->alert.kind],d->alert.code?" Ãƒâ€šÃ‚Â· ":"",d->alert.code?d->alert.code:"");
 sh_text_fit(c,x,y+8,room,head,SH_TEXT,SH_WHITE);
 sh_text_fit(c,x,y+30,room,d->alert.text?d->alert.text:line[d->alert.kind],SH_SMALL,0xffc8b4);
 xz_rect(c,b.x,b.y,b.w,b.h,SH_WHITE);
 sh_text_center(c,b.x+b.w/2,b.y+(b.h-SH_CAP_TEXT)/2,"CHOOSE TRACK",SH_TEXT,0x5a0c0c);
}
static int grid_edit_shown(const struct xz_shell *u, const struct xz_shell_model *m, int deck) {
 const struct xz_shell_deck *d=&m->deck[deck];
 uint32_t caps=sh_deck_caps(m,deck);
 return u->grid_mode&&sh_play_active_deck(u,m)==deck&&(caps&XZ_SHELL_CAP_GRID_EDIT)&&(caps&XZ_SHELL_CAP_GRID)
  &&sh_has(d,XZ_SHELL_F_LOADED)&&d->loaded&&!alert_on(d);
}
/* A GRID button's touch target, below a stem row at the top of the band. */
static struct xz_shell_rect grid_button(const struct play_layout *L, const struct xz_shell_model *m, int deck, int i) {
 struct xz_shell_rect r=xz_shell_play_rect(m,(enum xz_shell_play_region)(XZ_SHELL_R_GRID_RESET+i),deck),row=xz_shell_stem_row(m,deck);
 if(row.h&&row.y==lane_y(L,m,deck))r.y+=row.h;
 return r;
}
static void render_grid_buttons(const struct play_layout *L, const struct xz_shell_model *m, struct xz_canvas c, int deck) {
 static const char *const label[3]={"RESET","SNAP","SHIFT"};
 for(int i=0;i<3;i++){
  struct xz_shell_rect r=grid_button(L,m,deck,i);
  r.y+=r.h-L->grid_h;r.h=L->grid_h;
  xz_rect(c,r.x,r.y,r.w,r.h,0x1c1c1c);
  xz_border(c,r.x,r.y,r.w,r.h,SH_BLUE);
  sh_text_center(c,r.x+r.w/2,r.y+(r.h-SH_CAP_SMALL)/2,label[i],SH_SMALL,SH_WHITE);
 }
}
/* The ZOOM/GRID box sits at the foot of the lowest deck's waveform, above a stem row there. */
static struct xz_shell_rect zoom_box(const struct xz_shell_model *m) {
 struct xz_shell_rect r=xz_shell_play_rect(m,XZ_SHELL_R_ZOOM,0),row=xz_shell_stem_row(m,zoom_deck(m));
 if(row.h)r.y-=stem_buttons(m,zoom_deck(m))?sh_max(row.h,HIT_H):row.h;
 return r;
}
static void render_zoom(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 struct xz_shell_rect r=zoom_box(m);
 int grid=(sh_deck_caps(m,sh_play_active_deck(u,m))&XZ_SHELL_CAP_GRID)!=0;
 int zw=sh_text_width(c,"ZOOM",SH_SMALL)+8,gw=sh_text_width(c,"GRID",SH_SMALL)+8;
 int x=grid?r.x:r.x+r.w-zw;
 xz_rect(c,r.x,r.y,r.w,r.h,SH_BLACK);
 if(!u->grid_mode||!grid)xz_border(c,x,r.y,zw,r.h,SH_BLUE);
 sh_text(c,x+4,r.y+3,"ZOOM",SH_SMALL,!u->grid_mode||!grid?SH_WHITE:SH_DIM);
 if(grid){
  int gx=r.x+r.w-gw;
  if(u->grid_mode)xz_border(c,gx,r.y,gw,r.h,SH_BLUE);
  sh_text(c,gx+4,r.y+3,"GRID",SH_SMALL,u->grid_mode?SH_WHITE:SH_DIM);
 }
}

/* ---- bottom deck strip ---- */
static void render_overview(const struct play_layout *L, const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c, const struct xz_shell_deck *d, int deck, int32_t elapsed) {
 struct xz_shell_rect ov=xz_shell_play_rect(m,XZ_SHELL_R_OVERVIEW,deck);
 int chip_y=L->strip.y+L->strip.chip_dy;
 int loaded=sh_has(d,XZ_SHELL_F_LOADED)&&d->loaded;
 int timed=loaded&&elapsed>=0&&sh_has(d,XZ_SHELL_F_TOTAL)&&d->total_ms>0;
 int tm_known;enum xz_shell_time_mode mode=time_mode_of(m,d,&tm_known);
 int ph=timed?overview_x(ov,elapsed,d->total_ms):-1;
 char text[16];
 xz_rect(c,ov.x,ov.y,ov.w,ov.h,SH_BLACK);
 xz_rect(c,ov.x,ov.y+ov.h-1,ov.w,1,SH_RAIL);
 if(loaded)sh_deck_overview(c,(struct xz_shell_rect){ov.x,ov.y+1,ov.w,ov.h-1},u,m,d,ph);
 /* marker row above: hot cue letter chips and memory cue marks */
 if(timed&&sh_has(d,XZ_SHELL_F_MEMORY_CUES))for(int i=0;i<d->memory_count&&i<XZ_SHELL_MEMORY_CUES;i++){
  const struct xz_shell_cue *q=&d->memory_cue[i];
  if(q->set)sh_triangle(c,overview_x(ov,q->ms,d->total_ms),chip_y+7,4,1,SH_MEMORY);
 }
 if(timed&&sh_has(d,XZ_SHELL_F_HOT_CUES))for(int i=0;i<XZ_SHELL_HOT_CUES;i++){
  const struct xz_shell_cue *q=&d->hot_cue[i];
  if(!q->set)continue;
  int x=overview_x(ov,q->ms,d->total_ms)-6;uint32_t fill=cue_color(q);
  if(x<ov.x)x=ov.x;if(x>ov.x+ov.w-12)x=ov.x+ov.w-12;
  xz_rect(c,x,chip_y,12,12,fill);
  sh_text(c,x+3,chip_y+1,cue_letters[i],SH_SMALL,ink_on(fill));
 }
 /* playing address bar and position */
 int bar_y=ov.y+ov.h+1;
 if(timed){
  /* The end warning (XZ manual p.71): slow flash in the last 30 seconds, fast in the last 15, in either time mode. */
  int32_t left_ms=d->total_ms-elapsed;
  int flash=left_ms<30000&&((m->clock_ms/(left_ms<15000?125u:250u))&1);
  uint32_t left=mode==XZ_SHELL_TIME_REMAIN?SH_RED:SH_WHITE,right=mode==XZ_SHELL_TIME_REMAIN?SH_WHITE:SH_RAIL;
  if(flash)left=right=SH_RED;
  xz_rect(c,ov.x,bar_y,ph-ov.x,3,left);xz_rect(c,ph,bar_y,ov.x+ov.w-ph,3,right);
  xz_rect(c,ph,ov.y,1,ov.h,SH_PLAYHEAD);
  int32_t ghost=slip_now(m,d);
  if(ghost>=0)xz_rect(c,overview_x(ov,ghost,d->total_ms),ov.y,1,ov.h,SH_KEY_AMBER);
 }else xz_rect(c,ov.x,bar_y,ov.w,3,SH_RAIL);
 /* NEEDLE LOCK holds while the deck plays: the overview says so where a finger would search. */
 if(timed&&sh_needle_locked(m,d)){
  int w=sh_text_width(c,"NEEDLE LOCK",SH_SMALL)+10;
  xz_rect(c,ov.x+ov.w-w-2,ov.y+2,w,14,0x2a2a2a);
  sh_text(c,ov.x+ov.w-w+3,ov.y+4,"NEEDLE LOCK",SH_SMALL,SH_DIM);
 }
 /* minute scale */
 if(!L->strip.scale)return;
 int scale_y=bar_y+5;
 if(timed){
  int step=1;
  while((int64_t)60000*step*(ov.w-1)/d->total_ms<44&&step<64)step*=2;
  for(int32_t t=0;t<=d->total_ms;t+=30000){
   int32_t at=mode==XZ_SHELL_TIME_REMAIN?d->total_ms-t:t;
   int x=overview_x(ov,at,d->total_ms),minute=(t/60000);
   int label=t%60000==0&&minute>0&&minute%step==0;
   xz_rect(c,x,scale_y,1,label?5:3,SH_DIM);
   if(label){
    snprintf(text,sizeof text,mode==XZ_SHELL_TIME_REMAIN?"-%d:00":"%d:00",minute);
    int w=sh_text_width(c,text,SH_SMALL);
    if(x-w/2>=ov.x&&x+w/2<=ov.x+ov.w)sh_text(c,x-w/2,scale_y+6,text,SH_SMALL,SH_DIM);
   }
  }
  if(sh_has(d,XZ_SHELL_F_CUE_POINT))sh_triangle(c,overview_x(ov,d->cue_ms,d->total_ms),scale_y+7,5,0,SH_LOOP);
 }else xz_rect(c,ov.x,scale_y,ov.w,1,SH_RAIL);
}
static void render_artwork(struct xz_canvas c, const struct xz_shell_deck *d, int x, int y, int side) {
 for(int yy=0;yy<side;yy++)for(int xx=0;xx<side;xx++){
  uint16_t p=d->artwork[(size_t)(yy*d->artwork_side/side)*d->artwork_side+(size_t)(xx*d->artwork_side/side)];
  xz_rect(c,x+xx,y+yy,1,1,xz_theme_rgb888(p));
 }
}
static void render_strip(const struct play_layout *L, const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c, int deck) {
 const struct strip_layout *S=&L->strip;
 const struct xz_shell_deck *d=&m->deck[deck];
 struct xz_shell_rect s=xz_shell_play_rect(m,XZ_SHELL_R_STRIP,deck);
 int sx=s.x+S->pad,right=s.x+s.w-S->pad,num_y=s.y+S->num_y;
 int loaded_known=sh_has(d,XZ_SHELL_F_LOADED),loaded=loaded_known&&d->loaded;
 int32_t elapsed=loaded?xz_shell_elapsed_now(m,deck):-1;
 char text[48],frac[16];
 xz_rect(c,s.x,s.y,s.w,s.h,SH_CHROME);
 /* title row */
 int control=sh_control_active(m,deck);
 uint32_t accent=sh_deck_accent(u,d,SH_BLUE),tile=(sh_four_view(m)?control:u->deck==deck)?accent:SH_RAIL;
 int badge_w=sh_four_view(m)&&L==&two_deck?64:S->badge_w;
 int badge_h=sh_four_view(m)?22:S->badge_h;
 int by0=s.y+S->badge_y,ty=s.y+S->title_y;
 xz_rect(c,sx,by0,badge_w,badge_h,tile);
 if(L==&four_deck){
  /* The same badge and DECK as the deck's lane tag; a side that drives it: the arrow on its fill. */
  int seg=sx+NUMBER_BOX_W+1,seg_w=badge_w-NUMBER_BOX_W-1;
  number_box(c,sx,by0,badge_h,deck,1,tile);
  if(tile!=accent&&accent!=SH_BLUE)xz_rect(c,seg,by0,3,badge_h,accent);
  if(control)sh_control_arrow(c,seg+(seg_w-27)/2,by0+6,deck&1);
  else sh_text_center(c,seg+seg_w/2+1,by0+(badge_h-SH_CAP_SMALL)/2,"DECK",SH_SMALL,sh_ink_on(tile));
  if(u->deck==deck)xz_border(c,sx,by0,badge_w,badge_h,SH_WHITE);
 }else{
  if(tile!=accent&&accent!=SH_BLUE)xz_rect(c,two_deck_box(L,deck)?sx+NUMBER_BOX_W+1:sx,by0,3,badge_h,accent);
  if(sh_four_view(m)){
   snprintf(text,sizeof text,"%d",deck+1);
   int nx=control?sx+5:sx+(badge_w-sh_num_width(SH_NUM_MID,text))/2;
   if(two_deck_box(L,deck))number_box(c,sx,by0,badge_h,deck,1,tile);
   else sh_num(c,nx,by0+1,text,SH_NUM_MID,sh_ink_on(tile));
   if(control)sh_control_arrow(c,sx+badge_w-27,by0+6,deck&1);
   if(u->deck==deck)xz_border(c,sx,by0,badge_w,badge_h,SH_WHITE);
  }else{snprintf(text,sizeof text,"DECK %d",deck+1);sh_text(c,sx+5,by0+3,text,SH_SMALL,sh_ink_on(tile));}
 }
 int tx=sx+badge_w+8;
 enum sh_face title_face=L==&four_deck?SH_SMALL:SH_TEXT;
 if(control&&L==&four_deck)sh_text(c,tx,by0+12,"CONTROL",SH_SMALL,SH_WHITE);
 if(loaded_known&&!loaded)sh_text(c,tx,ty,"Not Loaded.",title_face,SH_RED);
 else if(loaded){
  if(S->artwork&&sh_has(d,XZ_SHELL_F_ARTWORK)&&d->artwork&&d->artwork_side>0){render_artwork(c,d,tx,by0,20);tx+=26;}
  else if(S->artwork){sh_note(c,tx,by0+2,SH_WHITE);tx+=16;}
  int artist=S->artist_y>=0&&sh_has(d,XZ_SHELL_F_ARTIST)&&d->artist&&*d->artist;
  if(artist)ty=s.y+S->title_artist_y;
  if(sh_has(d,XZ_SHELL_F_TITLE)&&d->title)sh_text_fit(c,tx,ty,right-tx-22,d->title,title_face,SH_WHITE);
  else sh_text(c,tx,ty,"---",title_face,SH_DIM);
  if(artist)sh_text_fit(c,tx,s.y+S->artist_y,right-tx-22,d->artist,SH_SMALL,SH_DIM);
 }else sh_text(c,tx,ty,"---",title_face,SH_DIM);
 if(loaded&&sh_has(d,XZ_SHELL_F_PLAYING)){
  if(d->playing)sh_triangle(c,right-6,by0+3,7,0,SH_WHITE);
  else{xz_rect(c,right-11,by0+3,3,12,SH_DIM);xz_rect(c,right-5,by0+3,3,12,SH_DIM);}
 }
 xz_rect(c,s.x,s.y+S->rule_y,s.w,1,SH_BLACK);
 /* TRACK */
 if(S->track){
  sh_text(c,sx,num_y,"TRACK",SH_SMALL,SH_DIM);
  if(loaded&&sh_has(d,XZ_SHELL_F_TRACK_NO)){snprintf(text,sizeof text,"%02d",d->track_no%1000);sh_num(c,sx,num_y+13,text,SH_NUM_MID,SH_WHITE);}
  else sh_num(c,sx,num_y+13,"--",SH_NUM_MID,SH_DIM);
 }
 /* time */
 int tm_known;enum xz_shell_time_mode mode=time_mode_of(m,d,&tm_known);
 int cx=sx+S->time_dx;
 uint32_t mode_ink=sh_writable(m,XZ_SHELL_SET_TIME_MODE)?SH_WHITE:0x8c8c8c;
 sh_text(c,cx,num_y,"REMAIN",SH_SMALL,tm_known&&mode==XZ_SHELL_TIME_REMAIN?mode_ink:0x5a5a5a);
 int sep=cx+sh_text_width(c,"REMAIN",SH_SMALL)+4;
 sh_text(c,sep,num_y,"/",SH_SMALL,0x5a5a5a);
 sh_text(c,sep+8,num_y,"TIME",SH_SMALL,tm_known&&mode==XZ_SHELL_TIME_ELAPSED?mode_ink:0x5a5a5a);
 /* QUANTIZE and its beat value between the time and tempo captions, where the stock screen has them
   (XZ manual pp.71-72, elements 24-25): red on, grey off. */
 if(S->quantize_right&&sh_has(d,XZ_SHELL_F_QUANTIZE)&&sh_indicator_shown(u,XZ_SHELL_IND_QUANTIZE)){
  char beat[12];
  if(d->quantize_num<=0||d->quantize_den<=0)snprintf(beat,sizeof beat,"?");
  else fmt_fraction(beat,sizeof beat,d->quantize_num>99?99:d->quantize_num,d->quantize_den>99?99:d->quantize_den);
  uint32_t q_ink=d->quantize_on?SH_RED:0x5a5a5a;
  int bw=sh_text_width(c,beat,SH_SMALL),qx=sx+S->quantize_right-bw-4-sh_text_width(c,"QUANTIZE",SH_SMALL);
  sh_text(c,qx,num_y,"QUANTIZE",SH_SMALL,q_ink);
  sh_text(c,sx+S->quantize_right-bw,num_y,beat,SH_SMALL,q_ink);
 }
 int have_total=loaded&&sh_has(d,XZ_SHELL_F_TOTAL);
 int32_t shown=-1;
 if(loaded_known&&!loaded)shown=0;
 else if(mode==XZ_SHELL_TIME_REMAIN&&elapsed>=0&&have_total)shown=d->total_ms-elapsed;
 else if(mode==XZ_SHELL_TIME_ELAPSED&&elapsed>=0)shown=elapsed;
 if(shown>=0)fmt_time(text,sizeof text,shown,frac,sizeof frac);
 else{snprintf(text,sizeof text,"--:--");snprintf(frac,sizeof frac,".---");}
 uint32_t time_ink=shown>=0?SH_WHITE:SH_DIM;
 enum sh_num_face time_face=(enum sh_num_face)S->time_face;
 sh_num(c,cx,num_y+S->time_value_dy,text,time_face,time_ink);
 if(S->time_frac)sh_num(c,cx+sh_num_width(time_face,text)+2,num_y+20,frac,SH_NUM_MID,time_ink);
 /* tempo */
 int px=sx+S->tempo_dx,tempo_y=s.y+S->tempo_dy,value_y=tempo_y+S->tempo_value_dy;
 sh_text(c,px,tempo_y,"TEMPO",SH_SMALL,SH_WHITE);
 int rx=px+sh_text_width(c,"TEMPO",SH_SMALL)+6;
 if(sh_has(d,XZ_SHELL_F_TEMPO_RANGE)){
  if(d->tempo_range>=100)snprintf(text,sizeof text,"WIDE");else snprintf(text,sizeof text,"\xb1%d",d->tempo_range);
  int w=sh_text_width(c,text,SH_SMALL)+10;
  xz_rect(c,rx,tempo_y-2,w,13,SH_RED);sh_text(c,rx+5,tempo_y,text,SH_SMALL,SH_WHITE);
 }else{xz_border(c,rx,tempo_y-2,30,13,0x5a5a5a);sh_text(c,rx+5,tempo_y,"\xb1--",SH_SMALL,SH_DIM);}
 if(sh_has(d,XZ_SHELL_F_TEMPO)){
  int32_t t=d->tempo_x100,a=t<0?-t:t;
  if(a>9999)a=9999;
  snprintf(text,sizeof text,"%s%d.%02d",t>0?"+":t<0?"-":"",(int)(a/100),(int)(a%100));
  sh_num(c,px,value_y,text,SH_NUM_MID,SH_WHITE);
  sh_text(c,px+sh_num_width(SH_NUM_MID,text)+3,value_y+11,"%",SH_SMALL,SH_WHITE);
 }else{sh_num(c,px,value_y,"--.--",SH_NUM_MID,SH_DIM);sh_text(c,px+sh_num_width(SH_NUM_MID,"--.--")+3,value_y+11,"%",SH_SMALL,SH_DIM);}
 int mx=sx+S->lamps_dx,my=s.y+S->lamps_dy;
 /* While KEY SYNC holds the key, MASTER TEMPO blinks on the model clock, half a second lit, half dark. */
 int key_sync=sh_has(d,XZ_SHELL_F_KEY_SYNC)&&d->key_synced;
 if(sh_has(d,XZ_SHELL_F_MASTER_TEMPO)||key_sync){
  int lit=key_sync?sh_key_sync_lit(m->clock_ms):d->master_tempo;
  uint32_t ink=lit?SH_RED:0x5a5a5a;
  mx+=sh_chip(c,mx,my,"MT",ink,ink,0)+4;
 }
 if(sh_has(d,XZ_SHELL_F_SYNC)&&d->sync){
  if(sh_has(d,XZ_SHELL_F_SYNC_STATUS)){
   if(d->sync_status!=XZ_SHELL_SYNC_MASTER){
    const char *label=d->sync_status==XZ_SHELL_SYNC_LOCKED?"SYNC":d->sync_status==XZ_SHELL_SYNC_TEMPO_ONLY?"BPM":d->sync_status==XZ_SHELL_SYNC_RATE_LIMIT?"LIMIT":"WAIT";
    char status[12];
    if(d->sync_master_deck>=0&&d->sync_master_deck<4)snprintf(status,sizeof status,"%s %d",label,d->sync_master_deck+1);
    else snprintf(status,sizeof status,"%s",label);
    sh_chip(c,mx,my,status,d->sync_status==XZ_SHELL_SYNC_LOCKED?SH_BLUE:SH_LOOP,SH_WHITE,0);
   }
  }else sh_chip(c,mx,my,"SYNC",SH_BLUE,SH_WHITE,0);
 }
 /* BPM box */
 int bx=sx+S->bpm_dx,bw=right-bx,by=s.y+S->bpm_dy,bh=S->bpm_h;
 int master=sh_has(d,XZ_SHELL_F_MASTER)&&d->master;
 xz_border(c,bx,by,bw,bh,master?SH_RED:SH_WHITE);
 sh_text(c,bx+5,by+3,"BPM",SH_SMALL,SH_WHITE);
 if(master){
  const char *label=d->master_scope==12u?"3/4 MASTER":"MASTER";
  int w=sh_text_width(c,label,SH_SMALL)+8;
  xz_rect(c,bx+bw-1-w,by+1,w,12,SH_RED);sh_text(c,bx+bw-1-w+4,by+3,label,SH_SMALL,SH_WHITE);
 }
 if(loaded&&sh_has(d,XZ_SHELL_F_BPM)){
  int32_t b=d->bpm_x100<0?0:d->bpm_x100>99999?99999:d->bpm_x100;
  snprintf(text,sizeof text,"%d",(int)(b/100));snprintf(frac,sizeof frac,".%d",(int)((b/10)%10));
  uint32_t ink=master?SH_RED:SH_WHITE;
  int fw=sh_num_width(SH_NUM_MID,frac),vw=sh_num_width(SH_NUM_BPM,text);
  int vx=bx+bw-5-fw-vw;
  sh_num(c,vx,by+22,text,SH_NUM_BPM,ink);sh_num(c,vx+vw,by+26,frac,SH_NUM_MID,ink);
 }else{
  int fw=sh_num_width(SH_NUM_MID,".-"),vw=sh_num_width(SH_NUM_BPM,"---");
  int vx=bx+bw-5-fw-vw;
  sh_num(c,vx,by+22,"---",SH_NUM_BPM,SH_DIM);sh_num(c,vx+vw,by+26,".-",SH_NUM_MID,SH_DIM);
 }
 render_overview(L,u,m,c,d,deck,elapsed);
}

/* ---- pad row: stem mute buttons, BYPASS and hot-cue lamps ----
   The stems follow the reference DJ software's look: a dark button with the
   stem's colour as outline and label. A stem that plays is lit: tinted fill,
   full-colour outline and label. A muted stem goes dark, its outline and label
   dimmed, and the MUTE caption turns red while any stem is muted. BYPASS sits
   after the stems, white like its pad: lit, the original track plays and every
   stem button goes dark, as the pads do; the mutes come back on its release. */
static int stem_at(const struct xz_shell_model *m,int slot){
 int order=m->mods_settings_known&&((unsigned)m->stem_order<XZ_STEM_ORDER_COUNT)?m->stem_order:4;
 return xz_stem_pad_role(order,3,slot);
}
static int stem_slot_of(const struct xz_shell_model *m,int stem){
 for(int slot=0;slot<3;slot++)if(stem_at(m,slot)==stem)return slot;
 return -1;
}
static const char *const stem_name[3]={"DRUMS","INST","VOCAL"};
enum { BYPASS_ROW_W=96, PAD_STEM_W=54, PAD_STEM_GAP=5, PAD_BYPASS_GAP=8 };
static int stem_buttons(const struct xz_shell_model *m, int deck) {
 const struct xz_shell_deck *d=&m->deck[deck];
 return (sh_deck_caps(m,deck)&XZ_SHELL_CAP_STEM_MUTE)&&sh_has(d,XZ_SHELL_F_STEM_MUTE)&&(!sh_has(d,XZ_SHELL_F_LOADED)||d->loaded);
}
/* No shell (the geometry queries' NULL) counts as the default: shown. */
static int bypass_shown(const struct xz_shell *u, const struct xz_shell_model *m, int deck) {
 return stem_buttons(m,deck)&&(sh_deck_caps(m,deck)&XZ_SHELL_CAP_STEM_BYPASS)&&sh_has(&m->deck[deck],XZ_SHELL_F_STEM_BYPASS)&&
  (!u||sh_indicator_shown(u,XZ_SHELL_IND_BYPASS));
}
static int bypassed(const struct xz_shell *u, const struct xz_shell_model *m, int deck) { return bypass_shown(u,m,deck)&&m->deck[deck].stem_bypass; }
static uint32_t stem_rgb(const struct xz_shell_model *m, int stem) { return sh_stem_bright(m,stem); }
/* A row button as drawn: the stems share the row, less BYPASS at its right end when shown; the row's
   height less a 2 px margin (1 on half rows). slot 3 is BYPASS. */
static struct xz_shell_rect row_button(const struct xz_shell *u, const struct xz_shell_model *m, int deck, int slot) {
 struct xz_shell_rect row=xz_shell_stem_row(m,deck);
 if(!row.h)return row;
 int inset=row.h>=STEM_ROW_FULL?2:1,by=bypass_shown(u,m,deck);
 if(slot==3)return by?(struct xz_shell_rect){row.x+row.w-BYPASS_ROW_W,row.y+inset,BYPASS_ROW_W,row.h-2*inset}:(struct xz_shell_rect){0,0,0,0};
 int span=by?row.w-BYPASS_ROW_W-STEM_ROW_GAP:row.w,w=(span-2*STEM_ROW_GAP)/3;
 return (struct xz_shell_rect){row.x+slot*(w+STEM_ROW_GAP),row.y+inset,slot==2?span-2*(w+STEM_ROW_GAP):w,row.h-2*inset};
}
/* The pad row's small buttons (stem layout not read): with BYPASS shown the MUTE caption gives way so the
   four buttons clear the hot-cue lamps. slot 3 is BYPASS. */
static struct xz_shell_rect pad_button(const struct xz_shell *u, const struct xz_shell_model *m, int deck, int slot) {
 struct xz_shell_rect r=xz_shell_play_rect(m,XZ_SHELL_R_PAD_ROW,deck);
 int y=r.y+(r.h-STEM_H)/2;
 if(!bypass_shown(u,m,deck))return slot==3?(struct xz_shell_rect){0,0,0,0}:(struct xz_shell_rect){r.x+STEM_X+slot*(STEM_W+STEM_GAP),y,STEM_W,STEM_H};
 int x=r.x+PAD+(slot==3?3*(PAD_STEM_W+PAD_STEM_GAP)-PAD_STEM_GAP+PAD_BYPASS_GAP:slot*(PAD_STEM_W+PAD_STEM_GAP));
 return (struct xz_shell_rect){x,y,PAD_STEM_W,STEM_H};
}
/* A drawn button's touch target: 44 px tall, a half row's 22 px reaching into the waveform beside it. */
static struct xz_shell_rect button_target(const struct xz_shell_model *m, int deck, struct xz_shell_rect b) {
 if(!b.h||b.h>=44)return b;
 const struct play_layout *L=layout_of(m);
 if(pad_row(m))return (struct xz_shell_rect){b.x,L->pad_row_y+L->pad_row_h/2-22,b.w,44};
 struct xz_shell_rect row=xz_shell_stem_row(m,deck);
 int top=row.y==lane_y(L,m,deck);b.y=top?row.y:row.y+row.h-44;b.h=44;
 return b;
}
static struct xz_shell_rect drawn_button(const struct xz_shell *u, const struct xz_shell_model *m, int deck, int slot) {
 return pad_row(m)?pad_button(u,m,deck,slot):row_button(u,m,deck,slot);
}
struct xz_shell_rect xz_shell_stem_button(const struct xz_shell *u, const struct xz_shell_model *m, int deck, int stem) {
 if(!m||!sh_play_visible(m,deck)||stem<0||stem>2||!stem_buttons(m,deck))return (struct xz_shell_rect){0,0,0,0};
 int slot=stem_slot_of(m,stem);
 return button_target(m,deck,drawn_button(u,m,deck,slot));
}
struct xz_shell_rect xz_shell_bypass_button(const struct xz_shell *u, const struct xz_shell_model *m, int deck) {
 if(!m||!sh_play_visible(m,deck)||!bypass_shown(u,m,deck))return (struct xz_shell_rect){0,0,0,0};
 return button_target(m,deck,drawn_button(u,m,deck,3));
}
/* Full rows are faders: a stem's level fills its button from the left. */
static int stem_fader(const struct xz_shell_model *m, int deck) {
 return xz_shell_stem_row(m,deck).h>=STEM_ROW_FULL&&(sh_deck_caps(m,deck)&XZ_SHELL_CAP_STEM_GAIN)&&sh_has(&m->deck[deck],XZ_SHELL_F_STEM_GAIN);
}
/* BYPASS: grey outline and label while the stems play; lit white with a black label while it holds. */
static void render_bypass(struct xz_canvas c, struct xz_shell_rect b, int on, int full) {
 if(!b.h)return;
 uint32_t edge=on?SH_WHITE:0x6e6e6e;
 xz_rect(c,b.x,b.y,b.w,b.h,on?0xe6e6e6:0x161616);
 xz_border(c,b.x,b.y,b.w,b.h,edge);
 if(full)xz_border(c,b.x+1,b.y+1,b.w-2,b.h-2,edge);
 sh_text_center(c,b.x+b.w/2,b.y+(b.h-(full?SH_CAP_TEXT:SH_CAP_SMALL))/2,"BYPASS",full?SH_TEXT:SH_SMALL,on?SH_BLACK:0xa4a4a4);
}
static void render_stem_row(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c, int deck) {
 struct xz_shell_rect row=xz_shell_stem_row(m,deck);
 if(!row.h)return;
 const struct xz_shell_deck *d=&m->deck[deck];
 xz_rect(c,row.x,row.y,row.w,row.h,SH_BLACK);
 if(!stem_buttons(m,deck)){
  const char *why=m->stems_known&&!m->stems_on?"STEMS ARE OFF":sh_has(d,XZ_SHELL_F_LOADED)&&!d->loaded?"":"NO STEMS ON THIS TRACK";
  if(*why)sh_text_center(c,row.x+row.w/2,row.y+(row.h-SH_CAP_SMALL)/2,why,SH_SMALL,0x5a5a5a);
  return;
 }
 int full=row.h>=STEM_ROW_FULL,gain_known=sh_has(d,XZ_SHELL_F_STEM_GAIN),by=bypassed(u,m,deck);
 for(int slot=0;slot<3;slot++){
  int stem=stem_at(m,slot),muted=by||((d->stem_muted>>stem)&1);
  struct xz_shell_rect b=row_button(u,m,deck,slot);
  uint32_t bright=stem_rgb(m,stem),dark=sh_stem_dark(bright);
  int gain=gain_known?sh_min(d->stem_gain[stem],XZ_SHELL_STEM_GAIN_MAX):XZ_SHELL_STEM_GAIN_MAX;
  /* Two tones as the reference stem buttons: dark inside, bright outline and label. The level is a
     lighter fill from the left; muted, the button goes grey and keeps its level only in the label. */
  uint32_t edge=muted?0x4a4a4a:bright,ink=muted?0x6e6e6e:bright;
  xz_rect(c,b.x,b.y,b.w,b.h,muted?0x161616:dark);
  if(!muted&&gain_known&&full)xz_rect(c,b.x+2,b.y+2,(b.w-4)*gain/XZ_SHELL_STEM_GAIN_MAX,b.h-4,xz_blend(dark,bright,90));
  xz_border(c,b.x,b.y,b.w,b.h,edge);
  if(full)xz_border(c,b.x+1,b.y+1,b.w-2,b.h-2,edge);
  if(full){
   sh_text(c,b.x+12,b.y+(b.h-SH_CAP_TEXT)/2,stem_name[stem],SH_TEXT,ink);
   if(gain_known){char pct[8];snprintf(pct,sizeof pct,"%d%%",gain);sh_text_right(c,b.x+b.w-12,b.y+(b.h-SH_CAP_SMALL)/2,pct,SH_SMALL,ink);}
  }else sh_text_center(c,b.x+b.w/2,b.y+(b.h-SH_CAP_SMALL)/2,stem_name[stem],SH_SMALL,ink);
 }
 render_bypass(c,row_button(u,m,deck,3),by,full);
}
static void render_pad_row(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c, int deck) {
 const struct xz_shell_deck *d=&m->deck[deck];
 struct xz_shell_rect r=xz_shell_play_rect(m,XZ_SHELL_R_PAD_ROW,deck);
 if(!r.h)return;
 xz_rect(c,r.x,r.y,r.w,r.h,SH_BLACK);
 int ty=r.y+(r.h-SH_CAP_SMALL)/2;
 if(pad_row(m)&&stem_buttons(m,deck)){
  int by=bypassed(u,m,deck);
  if(!bypass_shown(u,m,deck))sh_text(c,r.x+8,ty,"MUTE",SH_SMALL,d->stem_muted&7?SH_RED:SH_DIM);
  for(int slot=0;slot<3;slot++){
   int stem=stem_at(m,slot),muted=by||((d->stem_muted>>stem)&1);
   uint32_t rgb=stem_rgb(m,stem);
   struct xz_shell_rect b=pad_button(u,m,deck,slot);
   if(muted){
    xz_rect(c,b.x,b.y,b.w,b.h,0x101010);
    xz_border(c,b.x,b.y,b.w,b.h,xz_blend(0,rgb,90));
    sh_text_center(c,b.x+b.w/2,b.y+(b.h-SH_CAP_SMALL)/2,stem_name[stem],SH_SMALL,xz_blend(0,rgb,110));
   }else{
    xz_rect(c,b.x,b.y,b.w,b.h,xz_blend(0,rgb,70));
    xz_border(c,b.x,b.y,b.w,b.h,rgb);xz_border(c,b.x+1,b.y+1,b.w-2,b.h-2,rgb);
    sh_text_center(c,b.x+b.w/2,b.y+(b.h-SH_CAP_SMALL)/2,stem_name[stem],SH_SMALL,xz_blend(rgb,SH_WHITE,90));
   }
  }
  render_bypass(c,pad_button(u,m,deck,3),by,0);
 }
 /* Hot-cue lamps A-H, as the pads show them. */
 int lx=r.x+r.w-8-8*LAMP_W-7*LAMP_GAP,ly=r.y+(r.h-LAMP_W)/2;
 int known=sh_has(d,XZ_SHELL_F_HOT_CUES)&&(!sh_has(d,XZ_SHELL_F_LOADED)||d->loaded);
 for(int i=0;i<XZ_SHELL_HOT_CUES;i++){
  int x=lx+i*(LAMP_W+LAMP_GAP),set=known&&d->hot_cue[i].set;
  uint32_t fill=set?cue_color(&d->hot_cue[i]):0x1c1c1c;
  /* A stored cue not yet called onto its pad (manual call, XZ manual pp.54-55): an outline in its colour. */
  int waiting=set&&sh_has(d,XZ_SHELL_F_HOT_CUE_CALL)&&!((d->hot_cue_called>>i)&1);
  if(waiting){
   xz_rect(c,x,ly,LAMP_W,LAMP_W,0x1c1c1c);
   xz_border(c,x,ly,LAMP_W,LAMP_W,fill);
   sh_text_center(c,x+LAMP_W/2+1,ly+2,cue_letters[i],SH_SMALL,fill);
   continue;
  }
  xz_rect(c,x,ly,LAMP_W,LAMP_W,fill);
  if(!set)xz_border(c,x,ly,LAMP_W,LAMP_W,0x3c3c3c);
  sh_text_center(c,x+LAMP_W/2+1,ly+2,cue_letters[i],SH_SMALL,set?ink_on(fill):0x5a5a5a);
 }
}

/* ---- decks 3 and 4 not connected: placeholders and the card (design 5.1) ----
   A placeholder is drawn from fixed words, never from deck data: the column and the strip say NOT
   CONNECTED, the lane is covered by the card. */
enum { HOLD_INK=0x6a6a6a, HOLD_FILL=0x101010, HOLD_AMBER=0xffa82e };
static int card_lane(const struct xz_shell_model *m, int deck) { return lane_y(&four_deck,m,deck); }
static void render_placeholder_column(const struct play_layout *L, const struct xz_shell_model *m, struct xz_canvas c, int deck) {
 int ly=lane_y(L,m,deck);
 xz_rect(c,0,ly,COL_W,L->lane_h,HOLD_FILL);
 number_box(c,0,ly,L->tag_h,deck,0,HOLD_FILL);
 xz_rect(c,30,ly+3,40,14,0x262626);sh_text(c,36,ly+5,"DECK",SH_SMALL,HOLD_INK);
 sh_text(c,6,ly+L->lane_h-34,"NOT",SH_SMALL,HOLD_AMBER);sh_text(c,6,ly+L->lane_h-20,"CONNECTED",SH_SMALL,HOLD_AMBER);
}
static void render_placeholder_lane(const struct play_layout *L, const struct xz_shell_model *m, struct xz_canvas c, int deck) {
 int ly=lane_y(L,m,deck);
 xz_rect(c,COL_W,ly,c.width-COL_W,L->lane_h,0x0a0a0a);
 xz_rect(c,WAVE_X,ly+L->lane_h/2,WAVE_W,1,0x2e2e2e);
}
static void render_placeholder_strip(const struct xz_shell_model *m, struct xz_canvas c, int deck) {
 struct xz_shell_rect s=xz_shell_play_rect(m,XZ_SHELL_R_STRIP,deck);
 const struct strip_layout *S=&four_deck.strip;
 xz_rect(c,s.x,s.y,s.w,s.h,0x0e0e0e);
 /* Where the live strip's number box sits, dimmed: it lights in place when the deck comes up. */
 number_box(c,s.x+S->pad,s.y+S->badge_y,S->badge_h,deck,0,0x0e0e0e);
 sh_text(c,s.x+52,s.y+10,"NOT CONNECTED",SH_SMALL,HOLD_AMBER);
 sh_text(c,s.x+10,s.y+53,"Waiting for the",SH_SMALL,0x8c8c8c);
 sh_text(c,s.x+10,s.y+69,"loopback cable.",SH_SMALL,0x8c8c8c);
 xz_rect(c,s.x+10,s.y+113,s.w-20,1,0x2a2a2a);
}
/* The route picture's still, drawn once per state. PLAY renders on one thread at a time. */
static struct xz_route_card_cache card_cache;
static void render_card(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 if(!card_shown(m))return;
 struct xz_route_card_input in=card_input(u,m);
 xz_route_card_draw(c,&xz_route_card_modern,card_lane(m,2),card_lane(m,3),&in,&card_cache);
}
static void render_picker(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 if(picker_live(u,m))xz_deck_view_picker_draw(c,&xz_deck_view_modern,view_of(m),sh_deck_count(m)==4,u->deck_view.tile);
}
static void play_render_model(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 const struct play_layout *L=layout_of(m);
 int decks=sh_play_deck_count(m),last=lane_y(L,m,sh_play_deck(m,decks-1))+L->lane_h,below=L->pad_row_h?L->pad_row_y:L->strip.y;
 /* Regions tile the frame; only the seams between them are cleared here. */
 for(int deck=0;deck+1<decks;deck++)xz_rect(c,0,lane_y(L,NULL,deck)+L->lane_h,c.width,L->lane_gap,SH_BLACK);
 xz_rect(c,0,last,c.width,below-last,SH_BLACK);
 xz_rect(c,0,below,2,c.height-below,SH_BLACK);
 for(int deck=1;deck<decks;deck++)xz_rect(c,deck*L->strip.pitch-2,below,4,c.height-below,SH_BLACK);
 xz_rect(c,c.width-2,below,2,c.height-below,SH_BLACK);
 render_top(u,m,c);
 for(int slot=0;slot<decks;slot++){
  int deck=sh_play_deck(m,slot);
  if(sh_play_placeholder(m,deck)){
   /* While the card wipes away the live lane shows beside it; the column and strip go live after. */
   render_placeholder_column(L,m,c,deck);
   if(sh_deck_count(m)==4)render_lane(L,u,m,c,deck);else render_placeholder_lane(L,m,c,deck);
   render_placeholder_strip(m,c,deck);
   continue;
  }
  render_column(L,u,m,c,deck);
  render_lane(L,u,m,c,deck);
  render_pad_row(u,m,c,deck);
  render_strip(L,u,m,c,deck);
 }
 render_card(u,m,c);
 render_zoom(u,m,c);
 render_picker(u,m,c);
}

/* ---- input ---- */
static size_t emit(struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t n, struct xz_shell_action a) {
 if(n<XZ_SHELL_ACTIONS)out[n++]=a;
 return n;
}
static size_t activate(struct xz_shell *u, int deck, struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t n) {
 if(u->deck==deck)return n;
 u->deck=deck;
 return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_ACTIVE_DECK,deck,0,0});
}
/* NEEDLE LOCK (XZ manual p.123): while LOCK is set the overview searches only on a paused deck. */
int sh_needle_locked(const struct xz_shell_model *m, const struct xz_shell_deck *d) {
 return m->settings.needle_lock_known&&m->settings.needle_lock&&sh_has(d,XZ_SHELL_F_PLAYING)&&d->playing;
}
static size_t seek(struct xz_shell *u, const struct xz_shell_model *m, int deck, int x, struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t n) {
 const struct xz_shell_deck *d=&m->deck[deck];
 if(!(sh_deck_caps(m,deck)&XZ_SHELL_CAP_SEEK)||!sh_has(d,XZ_SHELL_F_TOTAL)||d->total_ms<=0)return n;
 if(!sh_has(d,XZ_SHELL_F_LOADED)||!d->loaded)return n;
 if(sh_needle_locked(m,d)){u->notice="NEEDLE LOCK: PAUSE THE DECK TO SEARCH";u->notice_at_ms=m->clock_ms;return n;}
 return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_SEEK,deck,xz_shell_overview_ms(m,d->total_ms,x,deck),0});
}
/* Top-bar controls answer in their own column from y 0 down to XZ_SHELL_HEADER_HIT_H, so each is 44 px
   tall; below the bar that band is the top of deck 1's lane, which only activates the deck. */
static int header_band(int x, int y, struct xz_shell_rect r) { return x>=r.x&&x<r.x+r.w&&y>=0&&y<XZ_SHELL_HEADER_HIT_H; }
/* ---- DECK VIEW input: the picker, the button and the card answer before anything under them ---- */
static void note_input(struct xz_shell *u, const struct xz_shell_model *m) { u->deck_view.input_at=m->clock_ms?m->clock_ms:1; }
/* Choosing a view: one action; the runtime applies it through its one DECK VIEW entry point. */
static size_t choose_view(struct xz_shell *u, int view, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 u->deck_view.open=0;u->deck_view.focus=XZ_SHELL_FOCUS_NONE;
 return emit(out,0,(struct xz_shell_action){XZ_SHELL_ACT_DECK_LAYOUT,-1,xz_deck_view_valid(view),0});
}
static void open_picker(struct xz_shell *u, const struct xz_shell_model *m) {
 u->deck_view.open=1;u->deck_view.tile=view_of(m);u->deck_view.focus=XZ_SHELL_FOCUS_NONE;
}
static size_t card_press(struct xz_shell *u, int hit, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 u->deck_view.focus=XZ_SHELL_FOCUS_NONE;
 if(hit==XZ_ROUTE_CARD_HIT_PRIMARY)return emit(out,0,(struct xz_shell_action){XZ_SHELL_ACT_ROUTE_RESCAN,-1,0,0});
 if(hit==XZ_ROUTE_CARD_HIT_TWO)return choose_view(u,XZ_LAYOUT_TWO,out);
 return 0;
}
/* 1 when the overlay owns this contact (it keeps it until release); *n actions in out. */
static int overlay_touch_down(struct xz_shell *u, const struct xz_shell_model *m, int x, int y, struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t *n) {
 *n=0;
 int open=picker_live(u,m),button=xz_deck_view_button_hit(&xz_deck_view_modern,x,y),card=card_hit(u,m,x,y);
 if(!open&&!button&&card==XZ_ROUTE_CARD_HIT_NONE)return 0;
 u->deck_view.contact=1;u->deck_view.focus=XZ_SHELL_FOCUS_NONE;
 /* The overlay under the finger must be the one on screen. */
 if(overlay_token(u,m)!=u->deck_view.painted)return 1;
 note_input(u,m);
 if(open){
  int hit=xz_deck_view_picker_hit(&xz_deck_view_modern,x,y);
  if(hit>=0)*n=choose_view(u,hit,out);
  else if(hit==XZ_DECK_VIEW_HIT_OUTSIDE)u->deck_view.open=0;
  return 1;
 }
 if(button){open_picker(u,m);return 1;}
 *n=card_press(u,card,out);
 return 1;
}
/* The encoder on PLAY. Open picker: turn moves, push picks. Otherwise the first push puts a focus ring on
   the card's buttons (RESCAN first) and the DECK VIEW button; turn moves it, push presses it. A hold, or
   six seconds without input, gives the encoder back to zoom and GRID. */
static int focus_list(const struct xz_shell *u, const struct xz_shell_model *m, int out[3]) {
 int n=0;
 if(card_shown(m)){
  struct xz_route_card_input in=card_input(u,m);int order[2],k=xz_route_card_visible(&in)?xz_route_card_focus_order(&in,order):0;
  for(int i=0;i<k;i++)out[n++]=order[i]==XZ_ROUTE_CARD_HIT_PRIMARY?XZ_SHELL_FOCUS_CARD_PRIMARY:XZ_SHELL_FOCUS_CARD_TWO;
 }
 out[n++]=XZ_SHELL_FOCUS_DECK_VIEW;
 return n;
}
static int overlay_encoder(struct xz_shell *u, const struct xz_shell_model *m, const struct xz_shell_event *e, struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t *n) {
 *n=0;
 if(picker_live(u,m)){
  if(e->kind==XZ_SHELL_ENCODER_TURN)u->deck_view.tile=xz_deck_view_focus_step(u->deck_view.tile,e->delta);
  else if(e->kind==XZ_SHELL_ENCODER_PUSH)*n=choose_view(u,u->deck_view.tile,out);
  note_input(u,m);return 1;
 }
 if(e->kind==XZ_SHELL_ENCODER_HOLD){u->deck_view.focus=XZ_SHELL_FOCUS_NONE;return 0;}
 int list[3],count=focus_list(u,m,list),at=-1;
 if(!count){u->deck_view.focus=XZ_SHELL_FOCUS_NONE;return 0;} /* nothing to focus: the push stays the player's */
 if(focus_live(u,m))for(int i=0;i<count;i++)if(list[i]==u->deck_view.focus)at=i;
 if(e->kind==XZ_SHELL_ENCODER_TURN){
  if(at<0){u->deck_view.focus=XZ_SHELL_FOCUS_NONE;return 0;}
  at+=e->delta<-3?-3:e->delta>3?3:e->delta;at=at<0?0:at>=count?count-1:at;
  u->deck_view.focus=list[at];note_input(u,m);return 1;
 }
 if(e->kind!=XZ_SHELL_ENCODER_PUSH)return 0;
 note_input(u,m);
 if(at<0){u->deck_view.focus=list[0];return 1;}
 int focus=list[at];
 if(focus==XZ_SHELL_FOCUS_DECK_VIEW){open_picker(u,m);return 1;}
 *n=card_press(u,focus==XZ_SHELL_FOCUS_CARD_PRIMARY?XZ_ROUTE_CARD_HIT_PRIMARY:XZ_ROUTE_CARD_HIT_TWO,out);
 return 1;
}
int sh_play_overlay_owns(const struct xz_shell *u, const struct xz_shell_model *m, int x, int y) {
 if(!u||!m||xz_shell_screen(u)!=XZ_SHELL_PLAY)return 0;
 struct xz_shell_model view;xz_shell_play_projection(u,m,&view);
 return picker_live(u,&view)&&xz_deck_view_picker_hit(&xz_deck_view_modern,x,y)!=XZ_DECK_VIEW_HIT_OUTSIDE;
}
int sh_play_dismiss(struct xz_shell *u, int back) {
 int had=u->deck_view.open||u->deck_view.focus!=XZ_SHELL_FOCUS_NONE;
 u->deck_view.open=0;u->deck_view.focus=XZ_SHELL_FOCUS_NONE;
 return back&&had;
}
void sh_play_overlay_publish(struct xz_shell *u, const struct xz_shell_model *m) {
 struct xz_shell_model view;xz_shell_play_projection(u,m,&view);
 if(!picker_live(u,&view))u->deck_view.open=0;
 if(!focus_live(u,&view))u->deck_view.focus=XZ_SHELL_FOCUS_NONE;
 u->deck_view.painted=overlay_token(u,&view);
}

static size_t touch_down(struct xz_shell *u, const struct xz_shell_model *m, int x, int y, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 const struct play_layout *L=layout_of(m);
 size_t n=0;
 u->down=1;u->down_region=XZ_SHELL_TOUCH_OTHER;u->down_deck=u->deck;
 if(overlay_touch_down(u,m,x,y,out,&n))return n;
 note_input(u,m);u->deck_view.focus=XZ_SHELL_FOCUS_NONE;
 /* A placeholder's column, lane and strip take the touch and do nothing. */
 for(int slot=0;slot<sh_play_deck_count(m);slot++){
  int deck=sh_play_deck(m,slot);
  if(sh_play_placeholder(m,deck)&&(sh_inside(xz_shell_play_rect(m,XZ_SHELL_R_INFO_COLUMN,deck),x,y)||
     sh_inside(xz_shell_play_rect(m,XZ_SHELL_R_LANE,deck),x,y)||sh_inside(xz_shell_play_rect(m,XZ_SHELL_R_STRIP,deck),x,y)))return 0;
 }
 for(int slot=0;slot<sh_play_deck_count(m);slot++)
  if(sh_inside(xz_shell_play_rect(m,XZ_SHELL_R_INFO_COLUMN,sh_play_deck(m,slot)),x,y))return activate(u,sh_play_deck(m,slot),out,n);
 if(header_band(x,y,xz_shell_play_rect(m,XZ_SHELL_R_INFO_BUTTON,0)))return sh_key(u,m,XZ_SHELL_KEY_INFO,-1,out);
 struct chip chips[3];top_chips(chips);
 for(int i=0;i<3&&mode_lamps(u);i++)if(header_band(x,y,chips[i].r)){
  if(!sh_writable(m,chips[i].setting)||!sh_setting(m,chips[i].setting))return 0;
  int value=chips[i].setting==XZ_SHELL_SET_PLAY_MODE?(m->settings.play_mode==XZ_SHELL_PLAY_SINGLE?XZ_SHELL_PLAY_CONTINUE:XZ_SHELL_PLAY_SINGLE):
   chips[i].setting==XZ_SHELL_SET_AUTO_CUE?!m->settings.auto_cue:(m->settings.hot_cue_auto_load+1)%3;
  return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_SET_SETTING,-1,value,(int)chips[i].setting});
 }
 if(y<TOP_H)return 0;
 for(int slot=0;slot<sh_play_deck_count(m);slot++){
  int deck=sh_play_deck(m,slot);
  if(L->grid_in_stem_row&&grid_edit_shown(u,m,deck))continue;
  if(sh_inside(xz_shell_bypass_button(u,m,deck),x,y))
   return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_STEM_BYPASS,deck,!m->deck[deck].stem_bypass,0});
 }
 for(int slot=0;slot<sh_play_deck_count(m);slot++)for(int stem=0;stem<3;stem++){
  int deck=sh_play_deck(m,slot);
  if(L->grid_in_stem_row&&grid_edit_shown(u,m,deck))break;
  if(!sh_inside(xz_shell_stem_button(u,m,deck,stem),x,y))continue;
  if(!pad_row(m)){
   /* A row button decides on release: a slide set the level, a tap toggles the mute. */
   const struct xz_shell_deck *d=&m->deck[deck];
   u->down_region=XZ_SHELL_TOUCH_STEM;u->down_deck=deck;
   u->stem_touch.stem=stem;u->stem_touch.x0=x;u->stem_touch.slid=0;
   u->stem_touch.gain0=sh_has(d,XZ_SHELL_F_STEM_GAIN)?sh_min(d->stem_gain[stem],XZ_SHELL_STEM_GAIN_MAX):XZ_SHELL_STEM_GAIN_MAX;
   return n;
  }
  int muted=(m->deck[deck].stem_muted>>stem)&1;
  return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_STEM_MUTE,deck,!muted,stem});
 }
 for(int slot=0;slot<sh_play_deck_count(m);slot++){
  int deck=sh_play_deck(m,slot);
  if(alert_on(&m->deck[deck])&&sh_inside(xz_shell_play_rect(m,XZ_SHELL_R_ALERT_BUTTON,deck),x,y)){
   n=activate(u,deck,out,n);
   return n+sh_key(u,m,XZ_SHELL_KEY_BROWSE,deck,out+n);
  }
  if(grid_edit_shown(u,m,deck))for(int i=0;i<3;i++)
   if(sh_inside(grid_button(L,m,deck,i),x,y))
    return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_GRID_EDIT,deck,0,i});
 }
 if(sh_inside(zoom_box(m),x,y)){
  if(sh_deck_caps(m,sh_play_active_deck(u,m))&XZ_SHELL_CAP_GRID)u->grid_mode=!u->grid_mode;
  return 0;
 }
 for(int slot=0;slot<sh_play_deck_count(m);slot++){
  int deck=sh_play_deck(m,slot);
  if(sh_inside(xz_shell_play_rect(m,XZ_SHELL_R_LANE,deck),x,y))return activate(u,deck,out,n);
  if(sh_inside(hit_up(xz_shell_play_rect(m,XZ_SHELL_R_OVERVIEW,deck)),x,y)){
   u->down_region=XZ_SHELL_TOUCH_OVERVIEW;u->down_deck=deck;
   return seek(u,m,deck,x,out,n);
  }
  if(sh_inside(xz_shell_play_rect(m,XZ_SHELL_R_TIME,deck),x,y)){
   if(!sh_writable(m,XZ_SHELL_SET_TIME_MODE)||!(sh_deck_caps(m,deck)&XZ_SHELL_CAP_SET_SETTING))return 0;
   int known;enum xz_shell_time_mode mode=time_mode_of(m,&m->deck[deck],&known);
   return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_SET_SETTING,deck,mode==XZ_SHELL_TIME_REMAIN?XZ_SHELL_TIME_ELAPSED:XZ_SHELL_TIME_REMAIN,XZ_SHELL_SET_TIME_MODE});
  }
 }
 return n;
}
/* Sliding a full-row stem button: the level follows the finger, a button's width for the whole range. */
static size_t stem_slide(struct xz_shell *u, const struct xz_shell_model *m, int x, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 int deck=u->down_deck,stem=u->stem_touch.stem;
 if(deck<0||deck>=sh_deck_count(m)||stem<0||stem>2||!stem_fader(m,deck))return 0;
 int dx=x-u->stem_touch.x0;
 if(!u->stem_touch.slid&&dx<STEM_SLIDE&&dx>-STEM_SLIDE)return 0;
 u->stem_touch.slid=1;
 int slot=stem_slot_of(m,stem);
 int w=sh_max(1,row_button(u,m,deck,slot).w);
 int gain=sh_max(0,sh_min(XZ_SHELL_STEM_GAIN_MAX,u->stem_touch.gain0+dx*XZ_SHELL_STEM_GAIN_MAX/w));
 if(gain==m->deck[deck].stem_gain[stem])return 0;
 return emit(out,0,(struct xz_shell_action){XZ_SHELL_ACT_STEM_GAIN,deck,gain,stem});
}
static size_t play_input_model(struct xz_shell *u, const struct xz_shell_model *m, const struct xz_shell_event *e, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 if(e->kind==XZ_SHELL_ENCODER_TURN||e->kind==XZ_SHELL_ENCODER_PUSH||e->kind==XZ_SHELL_ENCODER_HOLD){
  size_t n;if(overlay_encoder(u,m,e,out,&n))return n;
  note_input(u,m);
 }
 /* A contact the overlay took stays with it until release. */
 if((e->kind==XZ_SHELL_TOUCH_MOVE||e->kind==XZ_SHELL_TOUCH_UP)&&u->deck_view.contact){
  if(e->kind==XZ_SHELL_TOUCH_UP){u->deck_view.contact=0;u->down=0;u->down_region=XZ_SHELL_TOUCH_NONE;}
  return 0;
 }
 switch(e->kind){
 case XZ_SHELL_TOUCH_DOWN:u->deck_view.contact=0;return touch_down(u,m,e->x,e->y,out);
 case XZ_SHELL_TOUCH_MOVE:
  if(u->down&&u->down_region==XZ_SHELL_TOUCH_STEM)return stem_slide(u,m,e->x,out);
  if(u->down&&u->down_region==XZ_SHELL_TOUCH_OVERVIEW&&!sh_needle_locked(m,&m->deck[u->down_deck]))return seek(u,m,u->down_deck,e->x,out,0);
  return 0;
 case XZ_SHELL_TOUCH_UP:{
  size_t n=0;
  if(u->down&&u->down_region==XZ_SHELL_TOUCH_STEM&&!u->stem_touch.slid){
   int deck=u->down_deck,stem=u->stem_touch.stem;
   if(deck>=0&&deck<sh_deck_count(m)&&stem>=0&&stem<3&&stem_buttons(m,deck)){
    int muted=(m->deck[deck].stem_muted>>stem)&1;
    n=emit(out,0,(struct xz_shell_action){XZ_SHELL_ACT_STEM_MUTE,deck,!muted,stem});
   }
  }
  u->down=0;u->down_region=XZ_SHELL_TOUCH_NONE;
  return n;
 }
 case XZ_SHELL_ENCODER_TURN:{
  if(u->grid_mode&&(sh_deck_caps(m,sh_play_active_deck(u,m))&XZ_SHELL_CAP_GRID))
   return emit(out,0,(struct xz_shell_action){XZ_SHELL_ACT_GRID_ADJUST,sh_play_active_deck(u,m),e->delta,0});
  int zoom=u->zoom+e->delta;
  if(zoom<0)zoom=0;if(zoom>XZ_SHELL_ZOOM_MAX)zoom=XZ_SHELL_ZOOM_MAX;
  if(zoom==u->zoom)return 0;
  u->zoom=zoom;
  return emit(out,0,(struct xz_shell_action){XZ_SHELL_ACT_ZOOM,-1,zoom,0});
 }
 case XZ_SHELL_ENCODER_HOLD:
  if(sh_deck_caps(m,sh_play_active_deck(u,m))&XZ_SHELL_CAP_GRID)u->grid_mode=!u->grid_mode;
  return 0;
 default:return 0;
 }
}
static void play_render(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 struct xz_shell_model view;xz_shell_play_projection(u,m,&view);play_render_model(u,&view,c);
}
static size_t play_input(struct xz_shell *u, const struct xz_shell_model *m, const struct xz_shell_event *e, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 struct xz_shell_model view;xz_shell_play_projection(u,m,&view);return play_input_model(u,&view,e,out);
}
const struct sh_screen xz_shell_play_screen={play_render,play_input};
