/* Two-deck PLAY screen at 800x480, dark mode, after the two-deck flagship
   layout (the 2026-10-18 flagship UI study in docs/research, sections 4.4, 5
   and 6.2): a slim top bar of global chips and the BEAT FX readout, two
   stacked waveform lanes with a narrow deck-info column each, and two bottom
   deck strips. The mod header buttons are drawn over the bar's right end by
   shell.c. Positions are extrapolated to this frame (xz_shell_elapsed_now)
   and lanes scroll in whole track pixels, so the picture moves every frame
   without shimmer. */
#include "shell_internal.h"
#include <stdio.h>
#include <string.h>

enum {
 TOP_H=26, LANE_Y=26, LANE_H=124, LANE_GAP=3, COL_W=96, WAVE_X=98, WAVE_W=XZ_SHELL_WIDTH-WAVE_X-2,
 STRIP_Y=LANE_Y+2*LANE_H+LANE_GAP+3, STRIP_H=XZ_SHELL_HEIGHT-STRIP_Y, HALF_W=XZ_SHELL_WIDTH/2, PAD=8,
 NUM_Y=STRIP_Y+34, CHIP_ROW_Y=STRIP_Y+97, OVERVIEW_Y=STRIP_Y+112, OVERVIEW_H=56, TICK_H=10, EDGE=12,
 CHIP_Y=5, CHIP_H=16, CHIP_GAP=8,
 INFO_W=44, INFO_X=XZ_SHELL_HEADER_X-INFO_W-2, LINK_RIGHT=INFO_X-2, FX_Y=2, FX_H=22, FX_GAP=10
};
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
static int link_width(void) { return sh_text_width(measure,"LINK",SH_SMALL)+12; }
static int fx_left(void) { struct chip k[3];top_chips(k);return k[2].r.x+k[2].r.w+FX_GAP; }
static int fx_right(void) { return LINK_RIGHT-link_width()-FX_GAP; }

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
 case XZ_SHELL_R_INFO_BUTTON:return (struct xz_shell_rect){INFO_X,0,INFO_W,TOP_H};
 case XZ_SHELL_R_FX:{int x=fx_left();return (struct xz_shell_rect){x,FX_Y,fx_right()-x,FX_H};}
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
static int playhead_x(const struct xz_shell *u) {
 return WAVE_X+(sh_wave_position(u)==XZ_SHELL_POSITION_LEFT?WAVE_W*27/100:WAVE_W/2);
}
static uint32_t cue_color(const struct xz_shell_cue *q) { return q->is_loop?SH_LOOP:q->rgb&0xffffff; }
static uint32_t ink_on(uint32_t fill) { return xz_theme_luma(fill)>150?SH_BLACK:SH_WHITE; }

/* ---- top bar ---- */
static void render_fx(const struct xz_shell_model *m, struct xz_canvas c);
static void render_top(const struct xz_shell_model *m, struct xz_canvas c) {
 struct chip chips[3];top_chips(chips);
 xz_rect(c,0,0,c.width,TOP_H,SH_CHROME);
 xz_rect(c,0,TOP_H-1,c.width,1,SH_BLACK);
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
  sh_chip(c,chips[i].r.x,chips[i].r.y,i==1?"A. HOT CUE":"A. CUE",on?SH_DIM:0x5a5a5a,ink,0);
 }
 render_fx(m,c);
 int link=m->link_known&&m->link_connected,lw=link_width();
 sh_chip(c,LINK_RIGHT-lw,CHIP_Y,"LINK",link?SH_BLUE:0x5a5a5a,link?SH_WHITE:SH_DIM,0);
 sh_info_icon(c,INFO_X+INFO_W/2,TOP_H/2,SH_WHITE);
}

/* ---- BEAT FX readout ----
   Effect, beat, time and the FX BPM in one strip of the top bar. Off it is a
   quiet outline; on it fills with the flagship's FX red, as the mixer's lit FX
   ON button does. Name, beat and time draw their blank on a clear bit; the
   optional readouts (channel, level, BPM) take no room until they are read. */
static int fx_has(const struct xz_shell_fx *f, enum xz_shell_fx_field b) { return (f->known>>b)&1; }
static int fx_on(const struct xz_shell_fx *f) { return fx_has(f,XZ_SHELL_FX_F_ON)&&f->on; }
static const char *fx_channel_text(enum xz_shell_fx_channel ch) {
 return ch==XZ_SHELL_FX_CH_MASTER?"MST":ch==XZ_SHELL_FX_CH_1?"CH1":ch==XZ_SHELL_FX_CH_2?"CH2":ch==XZ_SHELL_FX_CH_MIC?"MIC":NULL;
}
static void render_fx(const struct xz_shell_model *m, struct xz_canvas c) {
 const struct xz_shell_fx *f=&m->fx;
 struct xz_shell_rect r=xz_shell_play_rect(XZ_SHELL_R_FX,0);
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
  int cw=sh_text_width(c,"MST",SH_SMALL)+8;
  xz_border(c,right-cw,r.y+4,cw,14,on?SH_WHITE:SH_DIM);
  sh_text_center(c,right-cw/2,ty,ch,SH_SMALL,ink);
  right-=cw+8;
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
  xz_rect(c,COL_W-30+b*7,ly+10,5,4,d->bar_phase==b+1?SH_WHITE:xz_blend(active?SH_BLUE:SH_RAIL,SH_WHITE,80));
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
  char key[8];const char *shown=sh_key_text(d->key,sh_key_display(u),key,sizeof key);
  int kw=sh_min(40,sh_text_width(c,shown,SH_TEXT));
  sh_text_fit(c,32,ly+54,40,shown,SH_TEXT,ink);
  if(sh_has(d,XZ_SHELL_F_KEY_SHIFT)&&d->key_shift){
   int x=32+kw+5;
   sh_triangle(c,x+3,ly+57,4,d->key_shift<0,SH_WHITE);
   snprintf(text,sizeof text,"%d",d->key_shift<0?-d->key_shift:d->key_shift);
   sh_text(c,x+9,ly+57,text,SH_SMALL,SH_WHITE);
  }
 }else sh_text(c,32,ly+54,"--",SH_TEXT,SH_DIM);
 /* beat jump */
 sh_text(c,6,ly+78,"BEAT",SH_SMALL,SH_DIM);sh_text(c,6,ly+89,"JUMP",SH_SMALL,SH_DIM);
 /* A zero jump is no jump size at all: the blank, not a number. */
 if(sh_has(d,XZ_SHELL_F_BEAT_JUMP)&&d->beat_jump_num>0){fmt_fraction(text,sizeof text,d->beat_jump_num,d->beat_jump_den);sh_text(c,44,ly+80,text,SH_TEXT,SH_WHITE);}
 else sh_text(c,44,ly+80,"--",SH_TEXT,SH_DIM);
 /* quantize */
 int qk=sh_has(d,XZ_SHELL_F_QUANTIZE);
 uint32_t q_ink=qk&&d->quantize_on?SH_RED:SH_DIM;
 sh_text(c,6,ly+105,"Q",SH_TEXT,q_ink);
 if(qk){fmt_fraction(text,sizeof text,d->quantize_num,d->quantize_den);sh_text(c,44,ly+105,text,SH_TEXT,d->quantize_on?SH_RED:SH_WHITE);}
 else sh_text(c,44,ly+105,"-",SH_TEXT,SH_DIM);
}

/* ---- scrolling lane ----
   Everything in a lane is placed by absolute track pixel, the same mapping the
   waveform samples by, so markers never slide against the audio. */
struct lane_view { int64_t head_px; int pps, ph_x; };
static int64_t floor_div(int64_t a, int64_t b) { int64_t q=a/b;return (a%b!=0&&(a<0)!=(b<0))?q-1:q; }
/* Rounded, not floored: a time lands on its nearest track pixel, the playhead included, so the
   picture is never up to a whole pixel late. */
static int64_t track_px(int pps, int32_t ms) { return floor_div((int64_t)ms*pps+500,1000); }
static int lane_x(const struct lane_view *v, int32_t ms) {
 int64_t x=v->ph_x+track_px(v->pps,ms)-v->head_px;
 return x<-4096?-4096:x>4096?4096:(int)x;
}
static int32_t lane_ms(const struct lane_view *v, int x) { return (int32_t)floor_div((v->head_px+x-v->ph_x)*1000,v->pps); }
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
static void render_lane(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c, int deck) {
 const struct xz_shell_deck *d=&m->deck[deck];
 int ly=lane_y(deck);
 char text[32];
 xz_rect(c,COL_W,ly,c.width-COL_W,LANE_H,SH_BLACK);
 /* The deck the Beat FX is applied to carries the FX red on its lane edge. */
 if(fx_on_deck(m,deck))xz_rect(c,COL_W,ly,2,LANE_H,SH_RED);
 int loaded=sh_has(d,XZ_SHELL_F_LOADED)&&d->loaded;
 int32_t elapsed=loaded?xz_shell_elapsed_now(m,deck):-1;
 struct lane_view v={0,zoom_pps[u->zoom<0?0:u->zoom>XZ_SHELL_ZOOM_MAX?XZ_SHELL_ZOOM_MAX:u->zoom],playhead_x(u)};
 if(elapsed>=0){
  v.head_px=track_px(v.pps,elapsed);
  int32_t first=lane_ms(&v,WAVE_X)-50,last=lane_ms(&v,WAVE_X+WAVE_W)+50;
  struct xz_canvas lane=xz_sub(c,WAVE_X,ly,WAVE_W,LANE_H);
  if(sh_has(d,XZ_SHELL_F_LOOP)&&d->loop.active&&d->loop.out_ms>d->loop.in_ms){
   int x0=sh_max(WAVE_X,lane_x(&v,d->loop.in_ms)),x1=sh_min(WAVE_X+WAVE_W,lane_x(&v,d->loop.out_ms));
   if(x1>x0)xz_rect(c,x0,ly,x1-x0,LANE_H,xz_blend(SH_BLACK,SH_LOOP,48));
  }
  sh_deck_lane(c,(struct xz_shell_rect){WAVE_X,ly,WAVE_W,LANE_H},LANE_H/2-EDGE,u,m,d,v.head_px-(v.ph_x-WAVE_X),v.pps);
  /* STEMS with no stem layer yet draws 3BAND, and says so, quietly, in the lane corner. */
  if(sh_wave_color(u)==XZ_SHELL_WAVE_STEMS&&!sh_stem_wave_ready(d)){
   static const char hint[]="NO STEM WAVE YET \xc2\xb7 3BAND";
   lane_caption(c,WAVE_X+WAVE_W-6-sh_text_width(c,hint,SH_SMALL),ly+LANE_H-TICK_H-SH_CAP_SMALL-6,hint,SH_DIM);
  }
  if(sh_has(d,XZ_SHELL_F_GRID)&&d->grid){
   int bar=0;
   for(uint32_t i=0;i<d->grid_count;i++){
    const struct xz_shell_beat *b=&d->grid[i];
    if(b->beat_in_bar==1)bar++;
    if(b->ms<first)continue;
    if(b->ms>last)break;
    int x=lane_x(&v,b->ms),h=b->beat_in_bar==1?TICK_H:TICK_H/2;
    if(x<WAVE_X||x>=WAVE_X+WAVE_W)continue;
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
   xz_rect(lane,x-WAVE_X,TICK_H+1,12,12,fill);
   sh_text(lane,x-WAVE_X+3,TICK_H+2,cue_letters[i],SH_SMALL,ink_on(fill));
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
   if(sh_has(d,XZ_SHELL_F_HOT_CUES))for(int i=0;i<XZ_SHELL_HOT_CUES;i++)if(d->hot_cue[i].set&&d->hot_cue[i].ms>elapsed&&d->hot_cue[i].ms<next)next=d->hot_cue[i].ms;
   if(sh_has(d,XZ_SHELL_F_MEMORY_CUES))for(int i=0;i<d->memory_count&&i<XZ_SHELL_MEMORY_CUES;i++)if(d->memory_cue[i].set&&d->memory_cue[i].ms>elapsed&&d->memory_cue[i].ms<next)next=d->memory_cue[i].ms;
   if(sh_has(d,XZ_SHELL_F_CUE_POINT)&&d->cue_ms>elapsed&&d->cue_ms<next)next=d->cue_ms;
   if(next!=INT32_MAX){
    int beats=0;
    for(uint32_t i=0;i<d->grid_count&&d->grid[i].ms<=next;i++)if(d->grid[i].ms>elapsed)beats++;
    if(beats<=64){snprintf(text,sizeof text,"%d.%d",beats/4,beats%4);lane_caption(c,v.ph_x+5,ly+TICK_H+3,text,SH_WHITE);}
   }
  }
 }else if(sh_has(d,XZ_SHELL_F_LOADED)&&!d->loaded)
  sh_text_center(c,WAVE_X+WAVE_W/2,ly+LANE_H/2-7,"Not Loaded.",SH_TEXT,0x5a5a5a);
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
static void render_overview(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c, const struct xz_shell_deck *d, int deck, int32_t elapsed) {
 struct xz_shell_rect ov=xz_shell_play_rect(XZ_SHELL_R_OVERVIEW,deck);
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
  int flash=mode==XZ_SHELL_TIME_REMAIN&&d->total_ms-elapsed<30000&&((m->clock_ms/250)&1);
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
static void render_strip(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c, int deck) {
 const struct xz_shell_deck *d=&m->deck[deck];
 struct xz_shell_rect s=xz_shell_play_rect(XZ_SHELL_R_STRIP,deck);
 int sx=s.x+PAD,right=s.x+s.w-PAD;
 int loaded_known=sh_has(d,XZ_SHELL_F_LOADED),loaded=loaded_known&&d->loaded;
 int32_t elapsed=loaded?xz_shell_elapsed_now(m,deck):-1;
 char text[48],frac[16];
 xz_rect(c,s.x,s.y,s.w,s.h,SH_CHROME);
 /* title row */
 xz_rect(c,sx,s.y+5,44,20,u->deck==deck?SH_BLUE:SH_RAIL);
 snprintf(text,sizeof text,"DECK %d",deck+1);
 sh_text(c,sx+5,s.y+8,text,SH_SMALL,SH_WHITE);
 int tx=sx+52;
 if(loaded_known&&!loaded)sh_text(c,tx,s.y+6,"Not Loaded.",SH_TEXT,SH_RED);
 else if(loaded){
  if(sh_has(d,XZ_SHELL_F_ARTWORK)&&d->artwork&&d->artwork_side>0){render_artwork(c,d,tx,s.y+5,20);tx+=26;}
  else{sh_note(c,tx,s.y+7,SH_WHITE);tx+=16;}
  if(sh_has(d,XZ_SHELL_F_TITLE)&&d->title)sh_text_fit(c,tx,s.y+6,right-tx-22,d->title,SH_TEXT,SH_WHITE);
  else sh_text(c,tx,s.y+6,"---",SH_TEXT,SH_DIM);
 }else sh_text(c,tx,s.y+6,"---",SH_TEXT,SH_DIM);
 if(loaded&&sh_has(d,XZ_SHELL_F_PLAYING)){
  if(d->playing)sh_triangle(c,right-6,s.y+8,7,0,SH_WHITE);
  else{xz_rect(c,right-11,s.y+8,3,12,SH_DIM);xz_rect(c,right-5,s.y+8,3,12,SH_DIM);}
 }
 xz_rect(c,s.x,s.y+29,s.w,1,SH_BLACK);
 /* TRACK + QUANTIZE */
 sh_text(c,sx,NUM_Y,"TRACK",SH_SMALL,SH_DIM);
 if(loaded&&sh_has(d,XZ_SHELL_F_TRACK_NO)){snprintf(text,sizeof text,"%02d",d->track_no%100);sh_num(c,sx,NUM_Y+13,text,SH_NUM_MID,SH_WHITE);}
 else sh_num(c,sx,NUM_Y+13,"--",SH_NUM_MID,SH_DIM);
 if(sh_has(d,XZ_SHELL_F_QUANTIZE))sh_text(c,sx,NUM_Y+45,"QUANTIZE",SH_SMALL,d->quantize_on?SH_RED:0x5a5a5a);
 /* time */
 int tm_known;enum xz_shell_time_mode mode=time_mode_of(m,d,&tm_known);
 int cx=sx+60;
 uint32_t mode_ink=sh_writable(m,XZ_SHELL_SET_TIME_MODE)?SH_WHITE:0x8c8c8c;
 sh_text(c,cx,NUM_Y,"REMAIN",SH_SMALL,tm_known&&mode==XZ_SHELL_TIME_REMAIN?mode_ink:0x5a5a5a);
 int sep=cx+sh_text_width(c,"REMAIN",SH_SMALL)+4;
 sh_text(c,sep,NUM_Y,"/",SH_SMALL,0x5a5a5a);
 sh_text(c,sep+8,NUM_Y,"TIME",SH_SMALL,tm_known&&mode==XZ_SHELL_TIME_ELAPSED?mode_ink:0x5a5a5a);
 int have_total=loaded&&sh_has(d,XZ_SHELL_F_TOTAL);
 int32_t shown=-1;
 if(loaded_known&&!loaded)shown=0;
 else if(mode==XZ_SHELL_TIME_REMAIN&&elapsed>=0&&have_total)shown=d->total_ms-elapsed;
 else if(mode==XZ_SHELL_TIME_ELAPSED&&elapsed>=0)shown=elapsed;
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
 }else{xz_border(c,rx,NUM_Y-2,30,13,0x5a5a5a);sh_text(c,rx+5,NUM_Y,"\xb1--",SH_SMALL,SH_DIM);}
 if(sh_has(d,XZ_SHELL_F_TEMPO)){
  int32_t t=d->tempo_x100,a=t<0?-t:t;
  if(a>9999)a=9999;
  snprintf(text,sizeof text,"%s%d.%02d",t>0?"+":t<0?"-":"",(int)(a/100),(int)(a%100));
  sh_num(c,px,NUM_Y+18,text,SH_NUM_MID,SH_WHITE);
  sh_text(c,px+sh_num_width(SH_NUM_MID,text)+3,NUM_Y+29,"%",SH_SMALL,SH_WHITE);
 }else{sh_num(c,px,NUM_Y+18,"--.--",SH_NUM_MID,SH_DIM);sh_text(c,px+sh_num_width(SH_NUM_MID,"--.--")+3,NUM_Y+29,"%",SH_SMALL,SH_DIM);}
 int mx=px;
 if(sh_has(d,XZ_SHELL_F_MASTER_TEMPO)){
  uint32_t ink=d->master_tempo?SH_RED:0x5a5a5a;
  mx+=sh_chip(c,mx,NUM_Y+42,"MT",ink,ink,0)+4;
 }
 if(sh_has(d,XZ_SHELL_F_SYNC)&&d->sync)sh_chip(c,mx,NUM_Y+42,"SYNC",SH_BLUE,SH_WHITE,0);
 /* BPM box */
 int bx=sx+298,bw=right-bx,by=NUM_Y-2,bh=52;
 int master=sh_has(d,XZ_SHELL_F_MASTER)&&d->master;
 xz_border(c,bx,by,bw,bh,master?SH_RED:SH_WHITE);
 sh_text(c,bx+5,by+3,"BPM",SH_SMALL,SH_WHITE);
 if(master){
  int w=sh_text_width(c,"MASTER",SH_SMALL)+8;
  xz_rect(c,bx+bw-1-w,by+1,w,12,SH_RED);sh_text(c,bx+bw-1-w+4,by+3,"MASTER",SH_SMALL,SH_WHITE);
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
 render_overview(u,m,c,d,deck,elapsed);
}

static void play_render(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 /* Regions tile the frame; only the seams between them are cleared here. */
 xz_rect(c,0,lane_y(0)+LANE_H,c.width,LANE_GAP,SH_BLACK);
 xz_rect(c,0,lane_y(1)+LANE_H,c.width,STRIP_Y-lane_y(1)-LANE_H,SH_BLACK);
 xz_rect(c,0,STRIP_Y,2,STRIP_H,SH_BLACK);xz_rect(c,HALF_W-2,STRIP_Y,4,STRIP_H,SH_BLACK);xz_rect(c,c.width-2,STRIP_Y,2,STRIP_H,SH_BLACK);
 render_top(m,c);
 for(int deck=0;deck<XZ_SHELL_DECKS;deck++){
  render_column(u,m,c,deck);
  render_lane(u,m,c,deck);
  render_strip(u,m,c,deck);
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
/* Top-bar controls answer in their own column from y 0 down to XZ_SHELL_HEADER_HIT_H, so each is 44 px
   tall; below the bar that band is the top of deck 1's lane, which only activates the deck. */
static int header_band(int x, int y, struct xz_shell_rect r) { return x>=r.x&&x<r.x+r.w&&y>=0&&y<XZ_SHELL_HEADER_HIT_H; }
static size_t touch_down(struct xz_shell *u, const struct xz_shell_model *m, int x, int y, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 size_t n=0;
 u->down=1;u->down_region=XZ_SHELL_TOUCH_OTHER;u->down_deck=u->deck;
 for(int deck=0;deck<XZ_SHELL_DECKS;deck++)
  if(sh_inside(xz_shell_play_rect(XZ_SHELL_R_INFO_COLUMN,deck),x,y))return activate(u,deck,out,n);
 if(header_band(x,y,xz_shell_play_rect(XZ_SHELL_R_INFO_BUTTON,0)))return sh_key(u,m,XZ_SHELL_KEY_INFO,-1,out);
 struct chip chips[3];top_chips(chips);
 for(int i=0;i<3;i++)if(header_band(x,y,chips[i].r)){
  if(!sh_writable(m,chips[i].setting)||!sh_setting(m,chips[i].setting))return 0;
  int value=chips[i].setting==XZ_SHELL_SET_PLAY_MODE?(m->settings.play_mode==XZ_SHELL_PLAY_SINGLE?XZ_SHELL_PLAY_CONTINUE:XZ_SHELL_PLAY_SINGLE):
   chips[i].setting==XZ_SHELL_SET_AUTO_CUE?!m->settings.auto_cue:!m->settings.hot_cue_auto_load;
  return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_SET_SETTING,-1,value,(int)chips[i].setting});
 }
 if(y<TOP_H)return 0;
 if(sh_inside(xz_shell_play_rect(XZ_SHELL_R_ZOOM,0),x,y)){
  if(m->capabilities&XZ_SHELL_CAP_GRID)u->grid_mode=!u->grid_mode;
  return 0;
 }
 for(int deck=0;deck<XZ_SHELL_DECKS;deck++){
  if(sh_inside(xz_shell_play_rect(XZ_SHELL_R_LANE,deck),x,y))return activate(u,deck,out,n);
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
