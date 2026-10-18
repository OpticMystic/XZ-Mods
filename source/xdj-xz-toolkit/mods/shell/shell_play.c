/* Two-deck PLAY screen at 800x480, dark mode, after the two-deck flagship
   layout (the 2026-10-18 flagship UI study in docs/research, sections 4.4, 5
   and 6.2): a slim top bar of global chips, two stacked waveform lanes with a
   narrow deck-info column each, and two bottom deck strips. */
#include "shell_internal.h"
#include <stdio.h>
#include <string.h>

enum {
 TOP_H=26, LANE_Y=26, LANE_H=124, LANE_GAP=3, COL_W=96, WAVE_X=98, WAVE_W=XZ_SHELL_WIDTH-WAVE_X-2,
 STRIP_Y=LANE_Y+2*LANE_H+LANE_GAP+3, STRIP_H=XZ_SHELL_HEIGHT-STRIP_Y, HALF_W=XZ_SHELL_WIDTH/2, PAD=8,
 NUM_Y=STRIP_Y+34, CHIP_ROW_Y=STRIP_Y+97, OVERVIEW_Y=STRIP_Y+112, OVERVIEW_H=56, TICK_H=10, EDGE=12
};
static const int zoom_pps[XZ_SHELL_ZOOM_MAX+1]={32,48,64,96,128,192,256};
static const char *const cue_letters[XZ_SHELL_HOT_CUES]={"A","B","C","D","E","F","G","H"};

/* Small lane captions sit over the waveform, so they carry a black backing. */
static void lane_caption(struct xz_canvas c, int x, int y, const char *text, uint32_t ink) {
 xz_rect(c,x-2,y-1,sh_text_width(c,text,SH_SMALL)+4,SH_CAP_SMALL+3,SH_BLACK);
 sh_text(c,x,y,text,SH_SMALL,ink);
}
static int lane_y(int deck) { return LANE_Y+deck*(LANE_H+LANE_GAP); }
static int strip_x(int deck) { return deck*HALF_W+2; }
struct xz_shell_rect xz_shell_play_rect(enum xz_shell_play_region r, int deck) {
 int ly=lane_y(deck),sx=strip_x(deck);
 switch(r){
 case XZ_SHELL_R_TOP_BAR:return (struct xz_shell_rect){0,0,XZ_SHELL_WIDTH,TOP_H};
 case XZ_SHELL_R_INFO_COLUMN:return (struct xz_shell_rect){0,ly,COL_W,LANE_H};
 case XZ_SHELL_R_DECK_TAG:return (struct xz_shell_rect){0,ly,COL_W,23};
 case XZ_SHELL_R_LANE:return (struct xz_shell_rect){COL_W,ly,XZ_SHELL_WIDTH-COL_W,LANE_H};
 case XZ_SHELL_R_STRIP:return (struct xz_shell_rect){sx,STRIP_Y,HALF_W-4,STRIP_H};
 case XZ_SHELL_R_TIME:return (struct xz_shell_rect){sx+PAD+60,NUM_Y-2,150,56};
 case XZ_SHELL_R_OVERVIEW:return (struct xz_shell_rect){sx+PAD,OVERVIEW_Y,HALF_W-4-2*PAD,OVERVIEW_H};
 case XZ_SHELL_R_ZOOM:return (struct xz_shell_rect){XZ_SHELL_WIDTH-84,lane_y(1)+LANE_H-17,82,15};
 case XZ_SHELL_R_INFO_BUTTON:return (struct xz_shell_rect){XZ_SHELL_WIDTH-32,0,32,TOP_H};
 }
 return (struct xz_shell_rect){0,0,0,0};
}
int32_t xz_shell_overview_ms(int32_t total_ms, int x, int deck) {
 struct xz_shell_rect ov=xz_shell_play_rect(XZ_SHELL_R_OVERVIEW,deck);
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
static enum xz_shell_wave_color wave_color_of(const struct xz_shell_model *m) {
 return sh_setting(m,XZ_SHELL_SET_WAVE_COLOR)?m->settings.wave_color:XZ_SHELL_WAVE_3BAND;
}
static int playhead_x(const struct xz_shell_model *m) {
 int left=sh_setting(m,XZ_SHELL_SET_WAVE_POSITION)&&m->settings.wave_position==XZ_SHELL_POSITION_LEFT;
 return WAVE_X+(left?WAVE_W*27/100:WAVE_W/2);
}
static uint32_t cue_color(const struct xz_shell_cue *q) { return q->is_loop?SH_LOOP:q->rgb&0xffffff; }
static uint32_t ink_on(uint32_t fill) { return xz_theme_luma(fill)>150?SH_BLACK:SH_WHITE; }

/* ---- top bar ---- */
struct chip { struct xz_shell_rect r; enum xz_shell_setting setting; };
static int top_chips(struct xz_canvas c, struct chip out[3]) {
 int x=PAD;
 int w0=sh_text_width(c,"SINGLE",SH_SMALL)+sh_text_width(c,"CONTINUE",SH_SMALL)+28;
 out[0]=(struct chip){{x,5,w0,16},XZ_SHELL_SET_PLAY_MODE};x+=w0+8;
 int w1=sh_text_width(c,"A. HOT CUE",SH_SMALL)+12;
 out[1]=(struct chip){{x,5,w1,16},XZ_SHELL_SET_HOT_CUE_AUTO_LOAD};x+=w1+8;
 int w2=sh_text_width(c,"A. CUE",SH_SMALL)+12;
 out[2]=(struct chip){{x,5,w2,16},XZ_SHELL_SET_AUTO_CUE};
 return 3;
}
static void render_top(const struct xz_shell_model *m, struct xz_canvas c) {
 struct chip chips[3];top_chips(c,chips);
 xz_rect(c,0,0,c.width,TOP_H,SH_CHROME);
 xz_rect(c,0,TOP_H-1,c.width,1,SH_BLACK);
 int mode_known=sh_setting(m,XZ_SHELL_SET_PLAY_MODE);
 struct xz_shell_rect r=chips[0].r;
 xz_border(c,r.x,r.y,r.w,r.h,SH_DIM);
 uint32_t mode_ink=sh_writable(m,XZ_SHELL_SET_PLAY_MODE)?SH_WHITE:0x8c8c8c;
 sh_text(c,r.x+6,r.y+3,"SINGLE",SH_SMALL,mode_known&&m->settings.play_mode==XZ_SHELL_PLAY_SINGLE?mode_ink:SH_DIM);
 int sep=r.x+6+sh_text_width(c,"SINGLE",SH_SMALL)+8;
 xz_rect(c,sep,r.y+3,1,10,SH_DIM);
 sh_text(c,sep+8,r.y+3,"CONTINUE",SH_SMALL,mode_known&&m->settings.play_mode==XZ_SHELL_PLAY_CONTINUE?mode_ink:SH_DIM);
 int hot=sh_setting(m,XZ_SHELL_SET_HOT_CUE_AUTO_LOAD)&&m->settings.hot_cue_auto_load;
 sh_chip(c,chips[1].r.x,chips[1].r.y,"A. HOT CUE",SH_DIM,hot?(sh_writable(m,XZ_SHELL_SET_HOT_CUE_AUTO_LOAD)?SH_WHITE:0x8c8c8c):SH_DIM,0);
 int acue=sh_setting(m,XZ_SHELL_SET_AUTO_CUE)&&m->settings.auto_cue;
 sh_chip(c,chips[2].r.x,chips[2].r.y,"A. CUE",SH_DIM,acue?(sh_writable(m,XZ_SHELL_SET_AUTO_CUE)?SH_WHITE:0x8c8c8c):SH_DIM,0);
 int link=m->link_known&&m->link_connected;
 int lw=sh_text_width(c,"LINK",SH_SMALL)+12;
 sh_chip(c,c.width-44-lw,5,"LINK",link?SH_BLUE:SH_DIM,link?SH_WHITE:SH_DIM,0);
 sh_info_icon(c,c.width-16,TOP_H/2,SH_WHITE);
}

/* ---- deck info column ---- */
static void render_column(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c, int deck) {
 const struct xz_shell_deck *d=&m->deck[deck];
 int ly=lane_y(deck),active=u->deck==deck;
 char text[32];
 xz_rect(c,0,ly,COL_W,LANE_H,SH_CHROME);
 xz_rect(c,0,ly,COL_W,23,active?SH_BLUE:SH_RAIL);
 snprintf(text,sizeof text,"DECK %d",deck+1);
 sh_text(c,6,ly+4,text,SH_TEXT,SH_WHITE);
 if(sh_has(d,XZ_SHELL_F_BAR_PHASE))for(int b=0;b<4;b++)
  xz_disc(c,COL_W-26+b*7,ly+11,2,d->bar_phase==b+1?SH_WHITE:xz_blend(active?SH_BLUE:SH_RAIL,SH_WHITE,80));
 static const int rows[4]={24,50,76,102};
 for(int i=0;i<4;i++)xz_rect(c,0,ly+rows[i]-1,COL_W,1,SH_BLACK);
 /* source */
 const char *source=sh_has(d,XZ_SHELL_F_SOURCE)?
  (d->source==XZ_SHELL_SRC_USB1?"USB1":d->source==XZ_SHELL_SRC_USB2?"USB2":d->source==XZ_SHELL_SRC_LINK?"LINK":
   d->source==XZ_SHELL_SRC_COMPUTER?"PC":"NONE"):"---";
 uint32_t source_ink=sh_has(d,XZ_SHELL_F_SOURCE)&&d->source!=XZ_SHELL_SRC_NONE?SH_WHITE:SH_DIM;
 sh_usb_icon(c,7,ly+29,source_ink);
 sh_text(c,24,ly+28,source,SH_TEXT,source_ink);
 /* key */
 sh_text(c,6,ly+57,"KEY",SH_SMALL,SH_DIM);
 if(sh_has(d,XZ_SHELL_F_KEY)&&d->key){
  uint32_t ink=d->key_matches_master?SH_GREEN:SH_WHITE;
  sh_text_fit(c,32,ly+54,40,d->key,SH_TEXT,ink);
  if(sh_has(d,XZ_SHELL_F_KEY_SHIFT)&&d->key_shift){
   int x=32+sh_min(40,sh_text_width(c,d->key,SH_TEXT))+5;
   sh_triangle(c,x+3,ly+57,4,d->key_shift<0,SH_WHITE);
   snprintf(text,sizeof text,"%d",d->key_shift<0?-d->key_shift:d->key_shift);
   sh_text(c,x+9,ly+57,text,SH_SMALL,SH_WHITE);
  }
 }else sh_text(c,32,ly+54,"--",SH_TEXT,SH_DIM);
 /* beat jump */
 sh_text(c,6,ly+78,"BEAT",SH_SMALL,SH_DIM);sh_text(c,6,ly+89,"JUMP",SH_SMALL,SH_DIM);
 if(sh_has(d,XZ_SHELL_F_BEAT_JUMP)){fmt_fraction(text,sizeof text,d->beat_jump_num,d->beat_jump_den);sh_text(c,44,ly+80,text,SH_TEXT,SH_WHITE);}
 else sh_text(c,44,ly+80,"--",SH_TEXT,SH_DIM);
 /* quantize */
 int qk=sh_has(d,XZ_SHELL_F_QUANTIZE);
 uint32_t q_ink=qk&&d->quantize_on?SH_RED:SH_DIM;
 sh_text(c,6,ly+105,"Q",SH_TEXT,q_ink);
 if(qk){fmt_fraction(text,sizeof text,d->quantize_num,d->quantize_den);sh_text(c,44,ly+105,text,SH_TEXT,d->quantize_on?SH_RED:SH_WHITE);}
 else sh_text(c,44,ly+105,"-",SH_TEXT,SH_DIM);
}

/* ---- scrolling lane ---- */
struct lane_view { int32_t elapsed; int pps, ph_x; };
static int lane_x(const struct lane_view *v, int32_t ms) { return v->ph_x+(int)(((int64_t)ms-v->elapsed)*v->pps/1000); }
static int32_t lane_ms(const struct lane_view *v, int x) { return v->elapsed+(int32_t)(((int64_t)x-v->ph_x)*1000/v->pps); }
static void column_max(const struct xz_shell_wave *w, uint32_t from, uint32_t to, int band[3]) {
 band[0]=band[1]=band[2]=0;
 if(to<=from)to=from+1;
 for(uint32_t i=from;i<to&&i<w->count;i++){
  const struct xz_shell_column *q=&w->columns[i];
  band[0]=sh_max(band[0],q->low);band[1]=sh_max(band[1],q->mid);band[2]=sh_max(band[2],q->high);
 }
}
static void band_colors(enum xz_shell_wave_color mode, int overview, uint32_t out[3], int scale[3]) {
 scale[0]=scale[1]=scale[2]=256;
 switch(mode){
 case XZ_SHELL_WAVE_RGB:out[0]=0xe03030;out[1]=0x30c040;out[2]=0x4080ff;break;
 case XZ_SHELL_WAVE_BLUE:out[0]=overview?SH_WAVE_LOW_DEEP:SH_WAVE_LOW;out[1]=out[0];out[2]=SH_WHITE;scale[2]=128;break;
 default:out[0]=overview?SH_WAVE_LOW_DEEP:SH_WAVE_LOW;out[1]=SH_WAVE_MID;out[2]=overview?SH_WHITE:SH_WAVE_HIGH;break;
 }
}
static void render_lane_wave(const struct xz_shell_model *m, struct xz_canvas c, const struct xz_shell_deck *d, const struct lane_view *v, int ly) {
 const struct xz_shell_wave *w=&d->wave;
 uint32_t color[3];int scale[3];
 int cy=ly+LANE_H/2,amp=LANE_H/2-EDGE;
 if(!w->columns||!w->count||!w->span_ms)return;
 band_colors(wave_color_of(m),0,color,scale);
 for(int x=WAVE_X;x<WAVE_X+WAVE_W;x++){
  int32_t ms0=lane_ms(v,x),ms1=lane_ms(v,x+1);
  if(ms1<=0||ms0>=(int32_t)w->span_ms)continue;
  if(ms0<0)ms0=0;
  uint32_t from=(uint32_t)((int64_t)ms0*w->count/w->span_ms),to=(uint32_t)((int64_t)ms1*w->count/w->span_ms);
  int band[3];column_max(w,from,to,band);
  for(int b=0;b<3;b++){
   int h=band[b]*scale[b]/256*amp/255;
   if(h>0)xz_rect(c,x,cy-h,1,h*2+1,color[b]);
  }
 }
}
static void render_lane(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c, int deck) {
 const struct xz_shell_deck *d=&m->deck[deck];
 int ly=lane_y(deck);
 char text[32];
 xz_rect(c,COL_W,ly,c.width-COL_W,LANE_H,SH_BLACK);
 int loaded=sh_has(d,XZ_SHELL_F_LOADED)&&d->loaded;
 int positioned=loaded&&sh_has(d,XZ_SHELL_F_ELAPSED);
 struct lane_view v={positioned?d->elapsed_ms:0,zoom_pps[u->zoom],playhead_x(m)};
 struct xz_canvas lane=xz_sub(c,WAVE_X,ly,WAVE_W,LANE_H);
 if(positioned){
  int32_t first=lane_ms(&v,WAVE_X),last=lane_ms(&v,WAVE_X+WAVE_W);
  if(sh_has(d,XZ_SHELL_F_LOOP)&&d->loop.active&&d->loop.out_ms>d->loop.in_ms){
   int x0=sh_max(WAVE_X,lane_x(&v,d->loop.in_ms)),x1=sh_min(WAVE_X+WAVE_W,lane_x(&v,d->loop.out_ms));
   if(x1>x0)xz_rect(c,x0,ly,x1-x0,LANE_H,xz_blend(SH_BLACK,SH_LOOP,48));
  }
  if(sh_has(d,XZ_SHELL_F_WAVE))render_lane_wave(m,c,d,&v,ly);
  if(sh_has(d,XZ_SHELL_F_GRID)&&d->grid){
   int bar=0;
   for(uint32_t i=0;i<d->grid_count;i++){
    const struct xz_shell_beat *b=&d->grid[i];
    if(b->beat_in_bar==1)bar++;
    if(b->ms<first)continue;
    if(b->ms>last)break;
    int x=lane_x(&v,b->ms),h=b->beat_in_bar==1?TICK_H:TICK_H/2;
    xz_rect(c,x,ly,1,h,SH_WHITE);xz_rect(c,x,ly+LANE_H-h,1,h,SH_WHITE);
    if(b->beat_in_bar==1&&bar%4==0){snprintf(text,sizeof text,"%d",bar);lane_caption(lane,x-WAVE_X+3,LANE_H-TICK_H-2,text,SH_DIM);}
   }
  }
  if(sh_has(d,XZ_SHELL_F_MEMORY_CUES))for(int i=0;i<d->memory_count&&i<XZ_SHELL_MEMORY_CUES;i++){
   const struct xz_shell_cue *q=&d->memory_cue[i];
   if(!q->set||q->ms<first||q->ms>last)continue;
   int x=lane_x(&v,q->ms);
   xz_rect(c,x,ly,1,TICK_H+2,SH_MEMORY);xz_rect(c,x,ly+LANE_H-TICK_H-2,1,TICK_H+2,SH_MEMORY);
  }
  if(sh_has(d,XZ_SHELL_F_CUE_POINT)&&d->cue_ms>=first&&d->cue_ms<=last)
   sh_triangle(c,lane_x(&v,d->cue_ms),ly+LANE_H-7,5,0,SH_LOOP);
  if(sh_has(d,XZ_SHELL_F_HOT_CUES))for(int i=0;i<XZ_SHELL_HOT_CUES;i++){
   const struct xz_shell_cue *q=&d->hot_cue[i];
   if(!q->set||q->ms<first||q->ms>last)continue;
   int x=lane_x(&v,q->ms);uint32_t fill=cue_color(q);
   xz_rect(c,x,ly,1,LANE_H,xz_blend(SH_BLACK,fill,140));
   xz_rect(c,x,ly+TICK_H+1,12,12,fill);
   sh_text(c,x+3,ly+TICK_H+2,cue_letters[i],SH_SMALL,ink_on(fill));
  }
  if(sh_has(d,XZ_SHELL_F_LOOP)&&d->loop.active){
   int x0=lane_x(&v,d->loop.in_ms),x1=lane_x(&v,d->loop.out_ms);
   if(x0>=WAVE_X&&x0<WAVE_X+WAVE_W)xz_rect(c,x0,ly,1,LANE_H,SH_LOOP);
   if(x1>=WAVE_X&&x1<WAVE_X+WAVE_W)xz_rect(c,x1,ly,1,LANE_H,SH_LOOP);
   char beats[16];fmt_fraction(beats,sizeof beats,d->loop.beats_num,d->loop.beats_den);
   snprintf(text,sizeof text,"%s BEAT LOOP",beats);
   lane_caption(c,WAVE_X+6,ly+TICK_H+3,text,SH_LOOP);
  }
  /* beat countdown to the next cue ahead of the playhead, in bars.beats */
  if(sh_has(d,XZ_SHELL_F_GRID)&&d->grid){
   int32_t next=INT32_MAX;
   if(sh_has(d,XZ_SHELL_F_HOT_CUES))for(int i=0;i<XZ_SHELL_HOT_CUES;i++)if(d->hot_cue[i].set&&d->hot_cue[i].ms>d->elapsed_ms&&d->hot_cue[i].ms<next)next=d->hot_cue[i].ms;
   if(sh_has(d,XZ_SHELL_F_MEMORY_CUES))for(int i=0;i<d->memory_count&&i<XZ_SHELL_MEMORY_CUES;i++)if(d->memory_cue[i].set&&d->memory_cue[i].ms>d->elapsed_ms&&d->memory_cue[i].ms<next)next=d->memory_cue[i].ms;
   if(sh_has(d,XZ_SHELL_F_CUE_POINT)&&d->cue_ms>d->elapsed_ms&&d->cue_ms<next)next=d->cue_ms;
   if(next!=INT32_MAX){
    int beats=0;
    for(uint32_t i=0;i<d->grid_count;i++)if(d->grid[i].ms>d->elapsed_ms&&d->grid[i].ms<=next)beats++;
    if(beats<=64){snprintf(text,sizeof text,"%d.%d",beats/4,beats%4);lane_caption(c,v.ph_x+5,ly+TICK_H+3,text,SH_WHITE);}
   }
  }
 }
 xz_rect(c,v.ph_x,ly,1,LANE_H,SH_PLAYHEAD);
}
static void render_zoom(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 struct xz_shell_rect r=xz_shell_play_rect(XZ_SHELL_R_ZOOM,0);
 int grid=(m->capabilities&XZ_SHELL_CAP_GRID)!=0;
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
static void render_overview(const struct xz_shell_model *m, struct xz_canvas c, const struct xz_shell_deck *d, int deck) {
 struct xz_shell_rect ov=xz_shell_play_rect(XZ_SHELL_R_OVERVIEW,deck);
 int loaded=sh_has(d,XZ_SHELL_F_LOADED)&&d->loaded;
 int timed=loaded&&sh_has(d,XZ_SHELL_F_ELAPSED)&&sh_has(d,XZ_SHELL_F_TOTAL)&&d->total_ms>0;
 int tm_known;enum xz_shell_time_mode mode=time_mode_of(m,d,&tm_known);
 int ph=timed?overview_x(ov,d->elapsed_ms,d->total_ms):-1;
 char text[16];
 xz_rect(c,ov.x,ov.y,ov.w,ov.h,SH_BLACK);
 if(loaded&&sh_has(d,XZ_SHELL_F_OVERVIEW)&&d->overview.columns&&d->overview.count){
  const struct xz_shell_wave *w=&d->overview;
  uint32_t color[3];int scale[3];band_colors(wave_color_of(m),1,color,scale);
  static const uint32_t played[3]={0x3c3c3c,0x6e6e6e,0xa0a0a0};
  for(int x=0;x<ov.w;x++){
   uint32_t from=(uint32_t)((int64_t)x*w->count/ov.w),to=(uint32_t)((int64_t)(x+1)*w->count/ov.w);
   int band[3];column_max(w,from,to,band);
   for(int b=0;b<3;b++){
    int h=band[b]*scale[b]/256*(ov.h-2)/255;
    if(h>0)xz_rect(c,ov.x+x,ov.y+ov.h-h,1,h,ph>=0&&ov.x+x<ph?played[b]:color[b]);
   }
  }
 }else xz_rect(c,ov.x,ov.y+ov.h-1,ov.w,1,SH_RAIL);
 /* marker row above: hot cue letter chips and memory cue marks */
 if(timed&&sh_has(d,XZ_SHELL_F_MEMORY_CUES))for(int i=0;i<d->memory_count&&i<XZ_SHELL_MEMORY_CUES;i++){
  const struct xz_shell_cue *q=&d->memory_cue[i];
  if(q->set)sh_triangle(c,overview_x(ov,q->ms,d->total_ms),CHIP_ROW_Y+7,4,1,SH_MEMORY);
 }
 if(timed&&sh_has(d,XZ_SHELL_F_HOT_CUES))for(int i=0;i<XZ_SHELL_HOT_CUES;i++){
  const struct xz_shell_cue *q=&d->hot_cue[i];
  if(!q->set)continue;
  int x=overview_x(ov,q->ms,d->total_ms)-6;uint32_t fill=cue_color(q);
  if(x<ov.x)x=ov.x;if(x>ov.x+ov.w-12)x=ov.x+ov.w-12;
  xz_rect(c,x,CHIP_ROW_Y,12,12,fill);
  sh_text(c,x+3,CHIP_ROW_Y+1,cue_letters[i],SH_SMALL,ink_on(fill));
 }
 /* playing address bar and position */
 int bar_y=ov.y+ov.h+1;
 if(timed){
  int flash=mode==XZ_SHELL_TIME_REMAIN&&d->total_ms-d->elapsed_ms<30000&&((m->clock_ms/250)&1);
  uint32_t left=mode==XZ_SHELL_TIME_REMAIN?SH_RED:SH_WHITE,right=mode==XZ_SHELL_TIME_REMAIN?SH_WHITE:SH_RAIL;
  if(flash)left=right=SH_RED;
  xz_rect(c,ov.x,bar_y,ph-ov.x,3,left);xz_rect(c,ph,bar_y,ov.x+ov.w-ph,3,right);
  xz_rect(c,ph,ov.y,1,ov.h,SH_PLAYHEAD);
 }else xz_rect(c,ov.x,bar_y,ov.w,3,SH_RAIL);
 /* minute scale */
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
static void render_strip(const struct xz_shell_model *m, struct xz_canvas c, int deck) {
 const struct xz_shell_deck *d=&m->deck[deck];
 struct xz_shell_rect s=xz_shell_play_rect(XZ_SHELL_R_STRIP,deck);
 int sx=s.x+PAD,right=s.x+s.w-PAD;
 int loaded_known=sh_has(d,XZ_SHELL_F_LOADED),loaded=loaded_known&&d->loaded;
 char text[48],frac[16];
 xz_rect(c,s.x,s.y,s.w,s.h,SH_CHROME);
 /* title row */
 xz_rect(c,sx,s.y+5,44,20,SH_BLUE);
 snprintf(text,sizeof text,"DECK %d",deck+1);
 sh_text(c,sx+5,s.y+8,text,SH_SMALL,SH_WHITE);
 int tx=sx+52;
 if(loaded_known&&!loaded)sh_text_center(c,(sx+right)/2,s.y+7,"Not Loaded.",SH_TEXT,SH_RED);
 else if(loaded){
  if(sh_has(d,XZ_SHELL_F_ARTWORK)&&d->artwork&&d->artwork_side>0){render_artwork(c,d,tx,s.y+5,20);tx+=26;}
  else{sh_note(c,tx,s.y+7,SH_WHITE);tx+=16;}
  if(sh_has(d,XZ_SHELL_F_TITLE)&&d->title)sh_text_fit(c,tx,s.y+6,right-tx-22,d->title,SH_TEXT,SH_WHITE);
 }
 if(loaded&&sh_has(d,XZ_SHELL_F_PLAYING)){
  if(d->playing)sh_triangle(c,right-6,s.y+8,7,0,SH_WHITE);
  else{xz_rect(c,right-11,s.y+8,3,12,SH_DIM);xz_rect(c,right-5,s.y+8,3,12,SH_DIM);}
 }
 xz_rect(c,s.x,s.y+29,s.w,1,SH_BLACK);
 /* TRACK + QUANTIZE */
 sh_text(c,sx,NUM_Y,"TRACK",SH_SMALL,SH_DIM);
 if(loaded&&sh_has(d,XZ_SHELL_F_TRACK_NO)){snprintf(text,sizeof text,"%02d",d->track_no%100);sh_num(c,sx,NUM_Y+13,text,SH_NUM_MID,SH_WHITE);}
 else sh_num(c,sx,NUM_Y+13,"--",SH_NUM_MID,SH_DIM);
 if(sh_has(d,XZ_SHELL_F_QUANTIZE))sh_text(c,sx,NUM_Y+40,"QUANTIZE",SH_SMALL,d->quantize_on?SH_RED:SH_DIM);
 /* time */
 int tm_known;enum xz_shell_time_mode mode=time_mode_of(m,d,&tm_known);
 int cx=sx+60;
 uint32_t mode_ink=sh_writable(m,XZ_SHELL_SET_TIME_MODE)?SH_WHITE:0x8c8c8c;
 sh_text(c,cx,NUM_Y,"REMAIN",SH_SMALL,tm_known&&mode==XZ_SHELL_TIME_REMAIN?mode_ink:SH_DIM);
 int sep=cx+sh_text_width(c,"REMAIN",SH_SMALL)+4;
 sh_text(c,sep,NUM_Y,"/",SH_SMALL,SH_DIM);
 sh_text(c,sep+8,NUM_Y,"TIME",SH_SMALL,tm_known&&mode==XZ_SHELL_TIME_ELAPSED?mode_ink:SH_DIM);
 int have_elapsed=loaded&&sh_has(d,XZ_SHELL_F_ELAPSED),have_total=loaded&&sh_has(d,XZ_SHELL_F_TOTAL);
 int32_t shown=-1;
 if(loaded_known&&!loaded)shown=0;
 else if(mode==XZ_SHELL_TIME_REMAIN&&have_elapsed&&have_total)shown=d->total_ms-d->elapsed_ms;
 else if(mode==XZ_SHELL_TIME_ELAPSED&&have_elapsed)shown=d->elapsed_ms;
 if(shown>=0)fmt_time(text,sizeof text,shown,frac,sizeof frac);
 else{snprintf(text,sizeof text,"--:--");snprintf(frac,sizeof frac,".---");}
 uint32_t time_ink=shown>=0?SH_WHITE:SH_DIM;
 sh_num(c,cx,NUM_Y+12,text,SH_NUM_BIG,time_ink);
 sh_num(c,cx+sh_num_width(SH_NUM_BIG,text)+2,NUM_Y+20,frac,SH_NUM_MID,time_ink);
 /* tempo */
 int px=sx+216;
 sh_text(c,px,NUM_Y,"TEMPO",SH_SMALL,SH_WHITE);
 int rx=px+sh_text_width(c,"TEMPO",SH_SMALL)+6;
 if(sh_has(d,XZ_SHELL_F_TEMPO_RANGE)){
  if(d->tempo_range>=100)snprintf(text,sizeof text,"WIDE");else snprintf(text,sizeof text,"\xb1%d",d->tempo_range);
  int w=sh_text_width(c,text,SH_SMALL)+10;
  xz_rect(c,rx,NUM_Y-2,w,13,SH_RED);sh_text(c,rx+5,NUM_Y,text,SH_SMALL,SH_WHITE);
 }else{xz_border(c,rx,NUM_Y-2,30,13,SH_DIM);sh_text(c,rx+5,NUM_Y,"\xb1--",SH_SMALL,SH_DIM);}
 if(sh_has(d,XZ_SHELL_F_TEMPO)){
  int32_t t=d->tempo_x100,a=t<0?-t:t;
  snprintf(text,sizeof text,"%s%d.%02d",t>0?"+":t<0?"-":"",(int)(a/100),(int)(a%100));
  sh_num(c,px,NUM_Y+18,text,SH_NUM_MID,SH_WHITE);
  sh_text(c,px+sh_num_width(SH_NUM_MID,text)+3,NUM_Y+29,"%",SH_SMALL,SH_WHITE);
 }else{sh_num(c,px,NUM_Y+18,"--.--",SH_NUM_MID,SH_DIM);sh_text(c,px+sh_num_width(SH_NUM_MID,"--.--")+3,NUM_Y+29,"%",SH_SMALL,SH_DIM);}
 int mx=px;
 if(sh_has(d,XZ_SHELL_F_MASTER_TEMPO)){
  uint32_t ink=d->master_tempo?SH_RED:SH_DIM;
  mx+=sh_chip(c,mx,NUM_Y+42,"MT",ink,ink,0)+4;
 }
 if(sh_has(d,XZ_SHELL_F_SYNC)&&d->sync)sh_chip(c,mx,NUM_Y+42,"SYNC",SH_BLUE,SH_WHITE,0);
 /* BPM box */
 int bx=sx+298,bw=right-bx,by=NUM_Y-2,bh=52;
 xz_border(c,bx,by,bw,bh,SH_WHITE);
 sh_text(c,bx+5,by+3,"BPM",SH_SMALL,SH_WHITE);
 int master=sh_has(d,XZ_SHELL_F_MASTER)&&d->master;
 if(master){
  int w=sh_text_width(c,"MASTER",SH_SMALL)+8;
  xz_rect(c,bx+bw-1-w,by+1,w,12,SH_RED);sh_text(c,bx+bw-1-w+4,by+3,"MASTER",SH_SMALL,SH_WHITE);
 }
 if(loaded&&sh_has(d,XZ_SHELL_F_BPM)){
  int32_t b=d->bpm_x100<0?0:d->bpm_x100;
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
 render_overview(m,c,d,deck);
}

static void play_render(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 xz_rect(c,0,0,c.width,c.height,SH_BLACK);
 render_top(m,c);
 for(int deck=0;deck<XZ_SHELL_DECKS;deck++){
  render_column(u,m,c,deck);
  render_lane(u,m,c,deck);
  render_strip(m,c,deck);
 }
 render_zoom(u,m,c);
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
static size_t seek(const struct xz_shell_model *m, int deck, int x, struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t n) {
 const struct xz_shell_deck *d=&m->deck[deck];
 if(!(m->capabilities&XZ_SHELL_CAP_SEEK)||!sh_has(d,XZ_SHELL_F_TOTAL)||d->total_ms<=0)return n;
 if(!sh_has(d,XZ_SHELL_F_LOADED)||!d->loaded)return n;
 return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_SEEK,deck,xz_shell_overview_ms(d->total_ms,x,deck),0});
}
static size_t touch_down(struct xz_shell *u, const struct xz_shell_model *m, int x, int y, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 size_t n=0;
 struct xz_canvas measure={NULL,XZ_SHELL_WIDTH,XZ_SHELL_WIDTH,XZ_SHELL_HEIGHT,XZ_THEME_FLAGSHIP};
 u->down=1;u->down_region=XZ_SHELL_TOUCH_OTHER;u->down_deck=u->deck;
 if(sh_inside(xz_shell_play_rect(XZ_SHELL_R_INFO_BUTTON,0),x,y))return sh_key(u,m,XZ_SHELL_KEY_INFO,-1,out);
 if(sh_inside(xz_shell_play_rect(XZ_SHELL_R_ZOOM,0),x,y)){
  if(m->capabilities&XZ_SHELL_CAP_GRID)u->grid_mode=!u->grid_mode;
  return 0;
 }
 struct chip chips[3];
 for(int i=0;i<top_chips(measure,chips);i++)if(sh_inside(chips[i].r,x,y)){
  if(!sh_writable(m,chips[i].setting)||!sh_setting(m,chips[i].setting))return 0;
  int value=chips[i].setting==XZ_SHELL_SET_PLAY_MODE?(m->settings.play_mode==XZ_SHELL_PLAY_SINGLE?XZ_SHELL_PLAY_CONTINUE:XZ_SHELL_PLAY_SINGLE):
   chips[i].setting==XZ_SHELL_SET_AUTO_CUE?!m->settings.auto_cue:!m->settings.hot_cue_auto_load;
  return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_SET_SETTING,-1,value,(int)chips[i].setting});
 }
 for(int deck=0;deck<XZ_SHELL_DECKS;deck++){
  if(sh_inside(xz_shell_play_rect(XZ_SHELL_R_INFO_COLUMN,deck),x,y)||sh_inside(xz_shell_play_rect(XZ_SHELL_R_LANE,deck),x,y))
   return activate(u,deck,out,n);
  if(sh_inside(xz_shell_play_rect(XZ_SHELL_R_OVERVIEW,deck),x,y)){
   u->down_region=XZ_SHELL_TOUCH_OVERVIEW;u->down_deck=deck;
   return seek(m,deck,x,out,n);
  }
  if(sh_inside(xz_shell_play_rect(XZ_SHELL_R_TIME,deck),x,y)){
   if(!sh_writable(m,XZ_SHELL_SET_TIME_MODE))return 0;
   int known;enum xz_shell_time_mode mode=time_mode_of(m,&m->deck[deck],&known);
   return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_SET_SETTING,deck,mode==XZ_SHELL_TIME_REMAIN?XZ_SHELL_TIME_ELAPSED:XZ_SHELL_TIME_REMAIN,XZ_SHELL_SET_TIME_MODE});
  }
 }
 return n;
}
static size_t play_input(struct xz_shell *u, const struct xz_shell_model *m, const struct xz_shell_event *e, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 switch(e->kind){
 case XZ_SHELL_TOUCH_DOWN:return touch_down(u,m,e->x,e->y,out);
 case XZ_SHELL_TOUCH_MOVE:
  if(u->down&&u->down_region==XZ_SHELL_TOUCH_OVERVIEW)return seek(m,u->down_deck,e->x,out,0);
  return 0;
 case XZ_SHELL_TOUCH_UP:u->down=0;u->down_region=XZ_SHELL_TOUCH_NONE;return 0;
 case XZ_SHELL_ENCODER_TURN:{
  if(u->grid_mode&&(m->capabilities&XZ_SHELL_CAP_GRID))
   return emit(out,0,(struct xz_shell_action){XZ_SHELL_ACT_GRID_ADJUST,u->deck,e->delta,0});
  int zoom=u->zoom+e->delta;
  if(zoom<0)zoom=0;if(zoom>XZ_SHELL_ZOOM_MAX)zoom=XZ_SHELL_ZOOM_MAX;
  if(zoom==u->zoom)return 0;
  u->zoom=zoom;
  return emit(out,0,(struct xz_shell_action){XZ_SHELL_ACT_ZOOM,-1,zoom,0});
 }
 case XZ_SHELL_ENCODER_HOLD:
  if(m->capabilities&XZ_SHELL_CAP_GRID)u->grid_mode=!u->grid_mode;
  return 0;
 default:return 0;
 }
}
const struct sh_screen xz_shell_play_screen={play_render,play_input};
