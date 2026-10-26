/* SPDX-License-Identifier: MIT */
#include "deck_view.h"
#include "route_card.h"

/* Design section 4. MODERN: the button at (504,0,120,26) answers y 0..43 like the other top-bar
   controls; the picker sits under the top bar's right half, like the shell's notice box. CLASSIC: the
   button fills the empty black between VJ.TOOLS and the METRICS readout; its touch reaches y 39, the
   corner of the top lane under it holding only cue flags and beat ticks. */
const struct xz_deck_view_style xz_deck_view_modern={
 .classic=0,.theme=XZ_THEME_FLAGSHIP,.button={504,0,120,26},.button_touch_h=44,.picker={296,30,500,206},
 .button_fill=0x191919,.button_edge=0x8c8c8c,.button_open_fill=0x3c3c3c,.button_open_edge=0xffffff,
 .ink=0xffffff,.dim=0xa4a4a4,.off=0x5a5a5a,.amber=0xffa82e,.green=0x28e028,.accent=0x007de1,
 .panel_fill=0x282828,.panel_edge=0x646465,.tile_fill=0x191919,.tile_sel_fill=0x0c2a48,.tile_edge=0x191919,
 .tile_sel_edge=0x007de1,.focus=0xffffff
};
const struct xz_deck_view_style xz_deck_view_classic={
 .classic=1,.theme=0,.button={162,0,116,18},.button_touch_h=40,.picker={140,22,500,206},
 .button_fill=0x000000,.button_edge=0xacaeb4,.button_open_fill=0x1c2430,.button_open_edge=0xacceff,
 .ink=0xf2f2f2,.dim=0x9aa09a,.off=0x5a5a5a,.amber=0xffa82e,.green=0x28e028,.accent=0xacceff,
 .panel_fill=0x0c1014,.panel_edge=0xacceff,.tile_fill=0x101410,.tile_sel_fill=0x182429,.tile_edge=0x313431,
 .tile_sel_edge=0xacceff,.focus=0xffffff
};

const char *xz_deck_view_name(int view) { return xz_deck_view_valid(view)==XZ_LAYOUT_TWO?"2 DECKS":"4 DECKS"; }
const char *xz_deck_view_sub(int view) {
 switch(xz_deck_view_valid(view)){
 case XZ_LAYOUT_FOUR:return "NUMBER ORDER 1 2 3 4";
 case XZ_LAYOUT_MIXER:return "MIXER ORDER 3 1 2 4";
 default:return "DECKS 1 2";
 }
}
const char *xz_deck_view_status(int view, int ready, enum xz_deck_view_tone *tone) {
 enum xz_deck_view_tone t=XZ_DECK_VIEW_TONE_DIM;const char *text="LIKE THE PLAYER ITSELF";
 if(xz_deck_view_valid(view)!=XZ_LAYOUT_TWO){
  if(ready){t=XZ_DECK_VIEW_TONE_GREEN;text="DECKS 3 AND 4 READY";}
  else{t=XZ_DECK_VIEW_TONE_AMBER;text="3 AND 4 NOT CONNECTED";}
 }
 if(tone)*tone=t;
 return text;
}
int xz_deck_view_lanes(int view, unsigned char out[4]) {
 view=xz_deck_view_valid(view);
 const unsigned char *order=xz_deck_layout_order(view);
 int n=view==XZ_LAYOUT_TWO?2:4;
 for(int i=0;i<4;i++)out[i]=i<n?(unsigned char)(order[i]+1):0;
 return n;
}
int xz_deck_view_focus_step(int focus, int delta) {
 int f=focus<0?0:focus>XZ_DECK_VIEW_TILES-1?XZ_DECK_VIEW_TILES-1:focus;
 f+=delta<-XZ_DECK_VIEW_TILES?-XZ_DECK_VIEW_TILES:delta>XZ_DECK_VIEW_TILES?XZ_DECK_VIEW_TILES:delta;
 return f<0?0:f>XZ_DECK_VIEW_TILES-1?XZ_DECK_VIEW_TILES-1:f;
}

/* ---- drawing helpers ---- */
static struct xz_canvas face(struct xz_canvas c, const struct xz_deck_view_style *s) { c.theme=s->theme;return c; }
static void text_right(struct xz_canvas c, int right, int y, const char *t, int scale, uint32_t color) {
 xz_text(c,right-xz_text_width(c,t,scale,99),y,t,scale,99,color);
}
static void text_center(struct xz_canvas c, int cx, int y, const char *t, int scale, uint32_t color) {
 xz_text(c,cx-xz_text_width(c,t,scale,99)/2,y,t,scale,99,color);
}
/* A down-pointing triangle, size rows tall, centred on cx. */
static void triangle_down(struct xz_canvas c, int cx, int top, int size, uint32_t color) {
 for(int row=0;row<size;row++)xz_rect(c,cx-(size-1-row),top+row,2*(size-1-row)+1,1,color);
}
/* A rounded box with a width-pixel outline: the CLASSIC shape. */
static void round_box(struct xz_canvas c, int x, int y, int w, int h, int radius, uint32_t fill, uint32_t edge, int width) {
 if(width>0){xz_round_rect(c,x,y,w,h,radius,15,edge);x+=width;y+=width;w-=2*width;h-=2*width;radius-=width;}
 xz_round_rect(c,x,y,w,h,radius<1?1:radius,15,fill);
}
static void ring_box(struct xz_canvas c, int x, int y, int w, int h, int width, uint32_t color) {
 for(int i=0;i<width;i++)xz_border(c,x+i,y+i,w-2*i,h-2*i,color);
}

void xz_deck_view_button_draw(struct xz_canvas c, const struct xz_deck_view_style *s, int view, int ready, int pair, int open, int focused) {
 view=xz_deck_view_valid(view);c=face(c,s);
 const struct xz_dv_rect b=s->button;
 int x=b.x,y=b.y,w=b.w,h=b.h;
 uint32_t fill=open?s->button_open_fill:s->button_fill,edge=open?s->button_open_edge:s->button_edge;
 if(s->classic){xz_rect(c,x,y,w,h,fill);xz_border(c,x,y,w,h,edge);}
 else{xz_rect(c,x,y,w,2,0x191919);xz_rect(c,x,y+h-2,w,2,0x191919);xz_rect(c,x,y+2,w,h-4,fill);xz_border(c,x,y+2,w,h-4,edge);}
 if(focused){if(s->classic)ring_box(c,x,y,w,h,2,s->accent);else ring_box(c,x,y+2,w,h-4,2,s->focus);}
 /* The lane glyph: two or four lanes, decks 3 and 4 grey while they are not ready. */
 unsigned char lanes[4];int n=xz_deck_view_lanes(view,lanes);
 if(view==XZ_LAYOUT_TWO&&ready){if(pair&1)lanes[0]=3;if(pair&2)lanes[1]=4;}
 int gy=y+(h-12)/2,pitch=12/n;
 for(int i=0;i<n;i++)xz_rect(c,x+6,gy+i*pitch,14,pitch-1,lanes[i]<=2||ready?s->ink:s->off);
 int ty=y+(h-9)/2,tx=x+26;
 xz_text(c,tx,ty,"DECKS",1,99,s->dim);tx+=xz_text_width(c,"DECKS",1,99)+5;
 for(int i=0;i<n;i++){
  char digit[2]={(char)('0'+lanes[i]),0};
  xz_text(c,tx,ty,digit,1,99,lanes[i]<=2||ready?s->ink:0x6a6a6a);
  tx+=xz_text_width(c,digit,1,99)+4;
 }
 triangle_down(c,x+w-10,y+h/2-1,4,s->ink);
 if(view!=XZ_LAYOUT_TWO&&!ready)xz_disc(c,x+w-4,y+5,3,s->amber);
}
int xz_deck_view_button_hit(const struct xz_deck_view_style *s, int x, int y) {
 return x>=s->button.x&&x<s->button.x+s->button.w&&y>=0&&y<s->button_touch_h;
}

struct xz_dv_rect xz_deck_view_tile(const struct xz_deck_view_style *s, int tile) {
 if(tile<0||tile>=XZ_DECK_VIEW_TILES)return (struct xz_dv_rect){0,0,0,0};
 return (struct xz_dv_rect){s->picker.x+12+164*tile,s->picker.y+28,156,166};
}
int xz_deck_view_picker_hit(const struct xz_deck_view_style *s, int x, int y) {
 const struct xz_dv_rect p=s->picker;
 if(x<p.x||y<p.y||x>=p.x+p.w||y>=p.y+p.h)return XZ_DECK_VIEW_HIT_OUTSIDE;
 for(int i=0;i<XZ_DECK_VIEW_TILES;i++){
  struct xz_dv_rect t=xz_deck_view_tile(s,i);
  if(x>=t.x&&y>=t.y&&x<t.x+t.w&&y<t.y+t.h)return i;
 }
 return XZ_DECK_VIEW_HIT_PANEL;
}

/* Waveform columns for the mini screens: a fixed deterministic shape per deck, no clock. */
static int amplitude(int k, int deck, int half) {
 int a=xz_sin1024((k+deck*13)*12),b=xz_cos1024((k+deck*7)*3);
 int mix=(a<0?-a:a)*(b<0?-b:b)/1024;
 return half*(358+563*mix/1024)/1024;
}
void xz_deck_view_mini_screen(struct xz_canvas c, const struct xz_deck_view_style *s, int x, int y, int w, int h, int view, int ready) {
 view=xz_deck_view_valid(view);c=face(c,s);
 int classic=s->classic;
 xz_rect(c,x,y,w,h,0x000000);xz_border(c,x,y,w,h,0x5a5a5a);
 int top=(h*26+240)/480;if(top<3)top=3;
 xz_rect(c,x+1,y+1,w-2,top,classic?0x101010:0x191919);
 unsigned char lanes[4];int n=xz_deck_view_lanes(view,lanes);
 int wave_h=classic?(h*268+240)/480:(h*(view==XZ_LAYOUT_TWO?236:312)+240)/480;
 int col_w=classic?(w*16+50)/100:(w*14+50)/100,right=classic?(w*15+50)/100:0;
 int bottom=y+1+top+wave_h+(classic?(h*20+240)/480:0);
 for(int i=0;i<n;i++){
  int ly=y+1+top+(i*wave_h*2+n)/(2*n),next=y+1+top+((i+1)*wave_h*2+n)/(2*n),lh=next-ly-1;
  int d=lanes[i],alive=d<=2||ready;
  xz_rect(c,x+1,ly,col_w,lh,alive?(classic?0x1a2638:0x1e1e1e):0x101010);
  if(lh>=11){char t[2]={(char)('0'+d),0};text_center(c,x+1+col_w/2,ly+(lh-9)/2,t,1,alive?s->ink:0x555555);}
  else xz_rect(c,x+3,ly+1,col_w-4,lh>2?lh-2:1,alive?0x5a5a5a:0x262626);
  int wx=x+col_w+2,ww=w-col_w-3-right,mid=ly+lh/2,half=lh/2-1;
  if(alive){
   for(int k=0;k<ww;k+=2){
    int amp=amplitude(k,d,half);
    xz_rect(c,wx+k,mid-amp,1,amp*2>0?amp*2:1,0x2a6fe0);
    int core=amp*45/100;xz_rect(c,wx+k,mid-core,1,core*2>0?core*2:1,0xffa028);
   }
   xz_rect(c,wx+ww/2,ly,1,lh,0xff2020);
  }else{xz_rect(c,wx,ly,ww,lh,0x141414);xz_rect(c,wx,mid,ww,1,0x2e2e2e);}
 }
 if(right){
  xz_rect(c,x+w-right-1,y+1+top,right,wave_h,0x22262c);
  xz_rect(c,x+w-right+2,y+4+top,right-6,3,0x5a6270);
 }
 static const unsigned char two[2]={1,2},four[4]={3,1,2,4};
 const unsigned char *panels=view==XZ_LAYOUT_TWO?two:four;int count=view==XZ_LAYOUT_TWO?2:4;
 int ph=y+h-1-bottom,span=w-2;
 for(int i=0;i<count;i++){
  int px=x+1+(i*span+count/2)/count,pw=((i+1)*span+count/2)/count-(i*span+count/2)/count-1;
  int alive=panels[i]<=2||ready;
  xz_rect(c,px,bottom+1,pw,ph-1,alive?0x1a1a1a:0x0e0e0e);
  if(classic)xz_rect(c,px,bottom+1,pw,4,alive?0x103462:0x161c26);
  if(alive){
   xz_rect(c,px+3,bottom+7,pw-6,2,0x8c8c8c);
   xz_rect(c,px+3,bottom+11,(pw-6)/2,3,0xffffff);
   xz_rect(c,px+3,bottom+ph-7,pw-6,4,0x2a6fe0);
  }else xz_rect(c,px+3,bottom+7,pw-6,2,0x2a2a2a);
 }
}

void xz_deck_view_picker_draw(struct xz_canvas c, const struct xz_deck_view_style *s, int view, int ready, int focus) {
 view=xz_deck_view_valid(view);c=face(c,s);
 const struct xz_dv_rect p=s->picker;
 if(s->classic){xz_rect(c,p.x-1,p.y-1,p.w+2,p.h+2,0x000000);round_box(c,p.x,p.y,p.w,p.h,8,s->panel_fill,s->panel_edge,1);}
 else{
  xz_rect(c,p.x-1,p.y-1,p.w+2,p.h+2,0x000000);
  xz_rect(c,p.x,p.y,p.w,p.h,s->panel_fill);xz_border(c,p.x,p.y,p.w,p.h,s->panel_edge);
  xz_rect(c,p.x,p.y,p.w,3,s->accent);
 }
 xz_text(c,p.x+12,p.y+11,"DECK VIEW",1,99,s->ink);
 text_right(c,p.x+p.w-12,p.y+11,"WHAT THE PLAY SCREEN SHOWS",1,s->dim);
 for(int i=0;i<XZ_DECK_VIEW_TILES;i++){
  struct xz_dv_rect t=xz_deck_view_tile(s,i);
  int selected=i==view,focused=i==focus;
  if(s->classic){
   if(focused)round_box(c,t.x-3,t.y-3,t.w+6,t.h+6,8,s->panel_fill,s->focus,2);
   round_box(c,t.x,t.y,t.w,t.h,6,selected?s->tile_sel_fill:s->tile_fill,selected?s->tile_sel_edge:s->tile_edge,selected?2:1);
  }else{
   if(focused)ring_box(c,t.x-3,t.y-3,t.w+6,t.h+6,2,s->focus);
   xz_rect(c,t.x,t.y,t.w,t.h,selected?s->tile_sel_fill:s->tile_fill);
   if(selected)ring_box(c,t.x,t.y,t.w,t.h,2,s->tile_sel_edge);
  }
  xz_deck_view_mini_screen(c,s,t.x+8,t.y+8,140,84,i,ready);
  xz_text(c,t.x+8,t.y+100,xz_deck_view_name(i),2,99,s->ink);
  xz_ring(c,t.x+t.w-16,t.y+107,7,2,s->ink);
  if(selected)xz_disc(c,t.x+t.w-16,t.y+107,4,s->accent);
  xz_text(c,t.x+8,t.y+124,xz_deck_view_sub(i),1,99,s->dim);
  enum xz_deck_view_tone tone;const char *status=xz_deck_view_status(i,ready,&tone);
  if(tone==XZ_DECK_VIEW_TONE_AMBER){xz_disc(c,t.x+12,t.y+146,3,s->amber);xz_text(c,t.x+20,t.y+142,status,1,99,s->amber);}
  else xz_text(c,t.x+8,t.y+142,status,1,99,tone==XZ_DECK_VIEW_TONE_GREEN?s->green:s->dim);
 }
}

static uint32_t notified_ready;
int xz_deck_view_ready_notice_take(uint32_t ready_revision) {
 if(!ready_revision)return 0;
 uint32_t seen=__atomic_load_n(&notified_ready,__ATOMIC_ACQUIRE);
 do{ if(seen==ready_revision)return 0; }
 while(!__atomic_compare_exchange_n(&notified_ready,&seen,ready_revision,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE));
 return 1;
}
int xz_deck_view_route_notice(int view, uint32_t ready_revision, uint32_t now_ms, struct xz_deck_view_notice *out) {
 if(!out||!xz_deck_view_ready_notice_take(ready_revision))return 0;
 if(xz_deck_view_valid(view)==XZ_LAYOUT_TWO)*out=(struct xz_deck_view_notice){XZ_DECK_VIEW_READY_NOTICE,now_ms,3500};
 else *out=(struct xz_deck_view_notice){XZ_DECK_VIEW_SUCCESS_NOTICE,
  now_ms+(uint32_t)(XZ_ROUTE_CARD_READY_HOLD_MS+XZ_ROUTE_CARD_WIPE_MS),2500};
 return 1;
}
const char *xz_deck_view_notice_text(const struct xz_deck_view_notice *n, uint32_t now_ms) {
 return n&&n->text&&n->ms&&now_ms-n->from_ms<n->ms?n->text:NULL;
}
