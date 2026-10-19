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
 TOP_H=26, LANE_Y=26, LANE_H=116, LANE_GAP=3, COL_W=96, WAVE_X=98, WAVE_W=XZ_SHELL_WIDTH-WAVE_X-2,
 PAD_ROW_Y=LANE_Y+2*LANE_H+LANE_GAP+3, PAD_ROW_H=28,
 STRIP_Y=PAD_ROW_Y+PAD_ROW_H, STRIP_H=XZ_SHELL_HEIGHT-STRIP_Y, HALF_W=XZ_SHELL_WIDTH/2, PAD=8,
 NUM_Y=STRIP_Y+34, CHIP_ROW_Y=STRIP_Y+97, OVERVIEW_Y=STRIP_Y+112, OVERVIEW_H=44, TICK_H=10, EDGE=12,
 STEM_X=46, STEM_W=62, STEM_GAP=6, STEM_H=22, LAMP_W=14, LAMP_GAP=3,
 CHIP_Y=5, CHIP_H=16, CHIP_GAP=8,
 INFO_W=44, INFO_X=XZ_SHELL_HEADER_X-INFO_W-2, FX_Y=2, FX_H=22, FX_GAP=10,
 ALERT_H=52, ALERT_BTN_W=150, GRID_BTN_W=78, GRID_BTN_GAP=4,
 QUANTIZE_RIGHT=206   /* strip x where the QUANTIZE lamp ends, 10 px short of TEMPO */
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
static int fx_left(void) { struct chip k[3];top_chips(k);return k[2].r.x+k[2].r.w+FX_GAP; }
static int fx_right(void) { return INFO_X-FX_GAP; }
static int mode_lamps(const struct xz_shell *u) { return sh_indicator_shown(u,XZ_SHELL_IND_MODE_LAMPS); }
/* The BEAT FX strip as drawn: XZ_SHELL_R_FX, reaching to the bar's left edge while the mode lamps are hidden. */
static struct xz_shell_rect fx_rect(const struct xz_shell *u) {
 struct xz_shell_rect r=xz_shell_play_rect(XZ_SHELL_R_FX,0);
 if(!mode_lamps(u)){r.w+=r.x-PAD;r.x=PAD;}
 return r;
}

static int lane_y(int deck) { return LANE_Y+deck*(LANE_H+LANE_GAP); }
/* The stem rows (shell.h enum xz_shell_stem_layout): -1 while the layout is not read, which keeps the
   small mute buttons in the pad row. */
enum { STEM_ROW_FULL=48, STEM_ROW_HALF=24, STEM_ROW_GAP=6, STEM_SLIDE=8 };
static int stem_layout(const struct xz_shell_model *m) {
 return m->stem_layout_known&&(unsigned)m->stem_layout<XZ_SHELL_STEM_LAYOUTS?(int)m->stem_layout:-1;
}
static int stem_row_full(int layout) { return layout==XZ_SHELL_STEMS_BELOW_FULL||layout==XZ_SHELL_STEMS_OUTSIDE_FULL; }
struct xz_shell_rect xz_shell_stem_row(const struct xz_shell_model *m, int deck) {
 int layout=m&&deck>=0&&deck<XZ_SHELL_DECKS?stem_layout(m):-1;
 if(layout<=XZ_SHELL_STEMS_HIDDEN)return (struct xz_shell_rect){0,0,0,0};
 int h=stem_row_full(layout)?STEM_ROW_FULL:STEM_ROW_HALF;
 int top=deck==0&&(layout==XZ_SHELL_STEMS_OUTSIDE_FULL||layout==XZ_SHELL_STEMS_OUTSIDE_HALF);
 return (struct xz_shell_rect){WAVE_X,top?lane_y(deck):lane_y(deck)+LANE_H-h,WAVE_W,h};
}
/* The waveform's part of a deck's band: all of it, less the stem row. */
static struct xz_shell_rect wave_band(const struct xz_shell_model *m, int deck) {
 struct xz_shell_rect row=xz_shell_stem_row(m,deck),w={WAVE_X,lane_y(deck),WAVE_W,LANE_H};
 if(row.h){w.h-=row.h;if(row.y==w.y)w.y+=row.h;}
 return w;
}
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
 case XZ_SHELL_R_PAD_ROW:return (struct xz_shell_rect){sx,PAD_ROW_Y,HALF_W-4,PAD_ROW_H};
 case XZ_SHELL_R_ALERT_BUTTON:return (struct xz_shell_rect){XZ_SHELL_WIDTH-2-ALERT_BTN_W-6,ly+LANE_H/2-22,ALERT_BTN_W,44};
 case XZ_SHELL_R_GRID_RESET:case XZ_SHELL_R_GRID_SNAP:case XZ_SHELL_R_GRID_SHIFT:{
  int i=(int)r-(int)XZ_SHELL_R_GRID_RESET,x0=XZ_SHELL_WIDTH-2-3*GRID_BTN_W-2*GRID_BTN_GAP-6;
  return (struct xz_shell_rect){x0+i*(GRID_BTN_W+GRID_BTN_GAP),ly+TICK_H+18,GRID_BTN_W,44};
 }
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
static void render_fx(const struct xz_shell_model *m, struct xz_canvas c, struct xz_shell_rect r);
static int alert_on(const struct xz_shell_deck *d);
static void render_alert(struct xz_canvas c, const struct xz_shell_deck *d, int deck);
static int grid_edit_shown(const struct xz_shell *u, const struct xz_shell_model *m, int deck);
static void render_grid_buttons(const struct xz_shell_model *m, struct xz_canvas c, int deck);
static void render_stem_row(const struct xz_shell_model *m, struct xz_canvas c, int deck);
static struct xz_shell_rect grid_button(const struct xz_shell_model *m, int deck, int i);
static void render_top(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 struct chip chips[3];top_chips(chips);
 xz_rect(c,0,0,c.width,TOP_H,SH_CHROME);
 xz_rect(c,0,TOP_H-1,c.width,1,SH_BLACK);
 render_fx(m,c,fx_rect(u));
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
   strip, so neither takes a row here. */
enum { KEY_Y=47, KEY_NAME_DY=22, KEY_FROM_DY=52, KEY_RIGHT=COL_W-6 };
static int same_key(const char *a, const char *b) {
 char x[8],y[8];
 a=sh_key_text(a,XZ_SHELL_KEY_CLASSIC,x,sizeof x);b=sh_key_text(b,XZ_SHELL_KEY_CLASSIC,y,sizeof y);
 return a&&b&&!strcmp(a,b);
}
static void render_key(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c, const struct xz_shell_deck *d, int ly) {
 int y=ly+KEY_Y,ny=y+KEY_NAME_DY;
 char text[16],key[8],from[8];
 if(sh_has(d,XZ_SHELL_F_KEY_SYNC)&&d->key_synced)sh_chip(c,6,y+2,"KEY SYNC",SH_BLUE,SH_WHITE,0);
 else sh_text(c,6,y+5,"KEY",SH_SMALL,SH_DIM);
 if(!sh_has(d,XZ_SHELL_F_KEY)||!d->key||!*d->key){sh_num(c,6,ny,"--",SH_NUM_MID,0x5a5a5a);return;}
 /* The master's own key is the reference, so plain. */
 int is_master=sh_has(d,XZ_SHELL_F_MASTER)&&d->master;
 uint32_t ink=is_master?SH_WHITE:sh_key_ink(m,d->key,d->key_matches_master,SH_WHITE);
 const char *shown=sh_key_text(d->key,sh_key_display(u),key,sizeof key);
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
  sh_text(c,6,y+KEY_FROM_DY,"ORIGINAL",SH_SMALL,SH_DIM);
  sh_text_fit(c,6+w,y+KEY_FROM_DY,KEY_RIGHT-6-w,sh_key_text(d->key_original,sh_key_display(u),from,sizeof from),SH_SMALL,SH_WHITE);
 }
}
static void render_column(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c, int deck) {
 const struct xz_shell_deck *d=&m->deck[deck];
 int ly=lane_y(deck),active=u->deck==deck;
 char text[32];
 xz_rect(c,0,ly,COL_W,LANE_H,SH_CHROME);
 /* The deck tile takes the colour of the stick its track came from (USB COLORS); inactive, a stripe. */
 uint32_t accent=sh_deck_accent(u,d,SH_BLUE),tile=active?accent:SH_RAIL,ink=sh_ink_on(tile);
 xz_rect(c,0,ly,COL_W,23,tile);
 if(!active&&accent!=SH_BLUE)xz_rect(c,0,ly,4,23,accent);
 snprintf(text,sizeof text,"DECK %d",deck+1);
 sh_text(c,6+(!active&&accent!=SH_BLUE?2:0),ly+4,text,SH_TEXT,ink);
 if(sh_has(d,XZ_SHELL_F_BAR_PHASE))for(int b=0;b<4;b++)
  xz_rect(c,COL_W-30+b*7,ly+10,5,4,d->bar_phase==b+1?ink:xz_blend(tile,ink,80));
 xz_rect(c,0,ly+23,COL_W,1,SH_BLACK);xz_rect(c,0,ly+KEY_Y-1,COL_W,1,SH_BLACK);
 /* source */
 const char *source=sh_has(d,XZ_SHELL_F_SOURCE)?
  (d->source==XZ_SHELL_SRC_USB1?"USB1":d->source==XZ_SHELL_SRC_USB2?"USB2":d->source==XZ_SHELL_SRC_LINK?"LINK":
   d->source==XZ_SHELL_SRC_COMPUTER?"PC":"NONE"):"---";
 uint32_t source_ink=sh_has(d,XZ_SHELL_F_SOURCE)&&d->source!=XZ_SHELL_SRC_NONE?SH_WHITE:SH_DIM;
 sh_usb_icon(c,7,ly+29,source_ink);
 sh_text(c,24,ly+28,source,SH_TEXT,source_ink);
 render_key(u,m,c,d,ly);
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
static void render_lane(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c, int deck) {
 const struct xz_shell_deck *d=&m->deck[deck];
 struct xz_shell_rect band=wave_band(m,deck);
 int ly=band.y,lh=band.h;
 char text[32];
 xz_rect(c,COL_W,lane_y(deck),c.width-COL_W,LANE_H,SH_BLACK);
 /* The deck the Beat FX is applied to carries the FX red on its lane edge. */
 if(fx_on_deck(m,deck))xz_rect(c,COL_W,lane_y(deck),2,LANE_H,SH_RED);
 int loaded=sh_has(d,XZ_SHELL_F_LOADED)&&d->loaded;
 int32_t elapsed=loaded?xz_shell_elapsed_now(m,deck):-1;
 struct lane_view v={0,zoom_pps[u->zoom<0?0:u->zoom>XZ_SHELL_ZOOM_MAX?XZ_SHELL_ZOOM_MAX:u->zoom],playhead_x(u)};
 if(elapsed>=0){
  v.head_px=track_px(v.pps,elapsed);
  int32_t first=lane_ms(&v,WAVE_X)-50,last=lane_ms(&v,WAVE_X+WAVE_W)+50;
  struct xz_canvas lane=xz_sub(c,WAVE_X,ly,WAVE_W,lh);
  if(sh_has(d,XZ_SHELL_F_LOOP)&&d->loop.active&&d->loop.out_ms>d->loop.in_ms){
   int x0=sh_max(WAVE_X,lane_x(&v,d->loop.in_ms)),x1=sh_min(WAVE_X+WAVE_W,lane_x(&v,d->loop.out_ms));
   if(x1>x0)xz_rect(c,x0,ly,x1-x0,lh,xz_blend(SH_BLACK,SH_LOOP,48));
  }
  sh_deck_lane(c,(struct xz_shell_rect){WAVE_X,ly,WAVE_W,lh},lh/2-EDGE,u,m,d,v.head_px-(v.ph_x-WAVE_X),v.pps);
  /* STEMS with no stem layer yet draws 3BAND, and says so, quietly, in the lane corner. */
  if(sh_deck_wave_color(u,d)==XZ_SHELL_WAVE_STEMS&&!sh_stem_wave_ready(d)){
   static const char hint[]="NO STEM WAVE YET \xc2\xb7 3BAND";
   lane_caption(c,WAVE_X+WAVE_W-6-sh_text_width(c,hint,SH_SMALL),ly+lh-TICK_H-SH_CAP_SMALL-6,hint,SH_DIM);
  }
  if(sh_has(d,XZ_SHELL_F_GRID)&&d->grid){
   int bar=0;
   for(uint32_t i=0;i<d->grid_count;i++){
    const struct xz_shell_beat *b=&d->grid[i];
    if(b->beat_in_bar==1)bar++;
    if(b->ms<first)continue;
    if(b->ms>last)break;
    /* The beat ruler on both edges: beats white, each bar's downbeat taller and red, as the stock lane marks them. */
    int down=b->beat_in_bar==1,x=lane_x(&v,b->ms),h=down?TICK_H:TICK_H/2;
    if(x<WAVE_X||x>=WAVE_X+WAVE_W)continue;
    uint32_t tick=down?SH_RED:SH_WHITE;
    xz_rect(c,x,ly,1+down,h,tick);xz_rect(c,x,ly+lh-h,1+down,h,tick);
    if(b->beat_in_bar==1&&bar%4==0){snprintf(text,sizeof text,"%d",bar);lane_caption(lane,x-WAVE_X+3,lh-TICK_H-2,text,SH_DIM);}
   }
  }
  if(sh_has(d,XZ_SHELL_F_MEMORY_CUES))for(int i=0;i<d->memory_count&&i<XZ_SHELL_MEMORY_CUES;i++){
   const struct xz_shell_cue *q=&d->memory_cue[i];
   if(!q->set||q->ms<first||q->ms>last)continue;
   int x=lane_x(&v,q->ms);
   xz_rect(c,x,ly,1,TICK_H+2,SH_MEMORY);xz_rect(c,x,ly+lh-TICK_H-2,1,TICK_H+2,SH_MEMORY);
  }
  if(sh_has(d,XZ_SHELL_F_CUE_POINT)&&d->cue_ms>=first&&d->cue_ms<=last)
   sh_triangle(c,lane_x(&v,d->cue_ms),ly+lh-7,5,0,SH_LOOP);
  if(sh_has(d,XZ_SHELL_F_HOT_CUES))for(int i=0;i<XZ_SHELL_HOT_CUES;i++){
   const struct xz_shell_cue *q=&d->hot_cue[i];
   if(!q->set||q->ms<first||q->ms>last)continue;
   int x=lane_x(&v,q->ms);uint32_t fill=cue_color(q);
   xz_rect(c,x,ly,1,lh,xz_blend(SH_BLACK,fill,140));
   xz_rect(lane,x-WAVE_X,TICK_H+1,12,12,fill);
   sh_text(lane,x-WAVE_X+3,TICK_H+2,cue_letters[i],SH_SMALL,ink_on(fill));
  }
  if(sh_has(d,XZ_SHELL_F_LOOP)&&d->loop.active){
   int x0=lane_x(&v,d->loop.in_ms),x1=lane_x(&v,d->loop.out_ms);
   if(x0>=WAVE_X&&x0<WAVE_X+WAVE_W)xz_rect(c,x0,ly,1,lh,SH_LOOP);
   if(x1>=WAVE_X&&x1<WAVE_X+WAVE_W)xz_rect(c,x1,ly,1,lh,SH_LOOP);
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
   int tw=sh_text_width(c,t,SH_SMALL)+12,bx=WAVE_X+6,by=ly+lh-TICK_H-24;
   xz_rect(c,bx,by,tw,18,SH_LOOP);
   sh_text(c,bx+6,by+4,t,SH_SMALL,SH_BLACK);
  }
 }else if(sh_has(d,XZ_SHELL_F_LOADED)&&!d->loaded)
  sh_text_center(c,WAVE_X+WAVE_W/2,ly+lh/2-7,"Not Loaded.",SH_TEXT,0x5a5a5a);
 xz_rect(c,v.ph_x,ly,1,lh,SH_PLAYHEAD);
 /* ON AIR: the channel is live on the master (XZ manual p.70), red on the lane's top right. */
 if(sh_has(d,XZ_SHELL_F_ON_AIR)&&d->on_air){
  int w=sh_text_width(c,"ON AIR",SH_SMALL)+12;
  xz_rect(c,WAVE_X+WAVE_W-w-4,ly+3,w,16,SH_RED);
  sh_text(c,WAVE_X+WAVE_W-w+2,ly+6,"ON AIR",SH_SMALL,SH_WHITE);
 }
 if(alert_on(d))render_alert(c,d,deck);
 if(grid_edit_shown(u,m,deck))render_grid_buttons(m,c,deck);
 render_stem_row(m,c,deck);
}
/* ---- persistent deck conditions and GRID mode buttons ---- */
static int alert_on(const struct xz_shell_deck *d) {
 return sh_has(d,XZ_SHELL_F_ALERT)&&d->alert.kind>XZ_SHELL_ALERT_NONE&&d->alert.kind<=XZ_SHELL_ALERT_UNSUPPORTED_FILE;
}
static void render_alert(struct xz_canvas c, const struct xz_shell_deck *d, int deck) {
 static const char *const title[]={"","EMERGENCY LOOP","CANNOT READ THE TRACK","UNSUPPORTED FILE"};
 static const char *const line[]={"","THE DECK LOOPS UNTIL YOU LOAD A TRACK","THE MEDIUM DID NOT ANSWER","THIS FILE CANNOT PLAY HERE"};
 int ly=lane_y(deck),y=ly+(LANE_H-ALERT_H)/2;
 struct xz_shell_rect b=xz_shell_play_rect(XZ_SHELL_R_ALERT_BUTTON,deck);
 xz_rect(c,WAVE_X,y,WAVE_W,ALERT_H,0x5a0c0c);
 xz_rect(c,WAVE_X,y,4,ALERT_H,SH_RED);
 int x=WAVE_X+14,room=b.x-x-10;
 char head[64];
 snprintf(head,sizeof head,"%s%s%s",title[d->alert.kind],d->alert.code?" · ":"",d->alert.code?d->alert.code:"");
 sh_text_fit(c,x,y+8,room,head,SH_TEXT,SH_WHITE);
 sh_text_fit(c,x,y+30,room,d->alert.text?d->alert.text:line[d->alert.kind],SH_SMALL,0xffc8b4);
 xz_rect(c,b.x,b.y,b.w,b.h,SH_WHITE);
 sh_text_center(c,b.x+b.w/2,b.y+(b.h-SH_CAP_TEXT)/2,"CHOOSE TRACK",SH_TEXT,0x5a0c0c);
}
static int grid_edit_shown(const struct xz_shell *u, const struct xz_shell_model *m, int deck) {
 const struct xz_shell_deck *d=&m->deck[deck];
 return u->grid_mode&&u->deck==deck&&(m->capabilities&XZ_SHELL_CAP_GRID_EDIT)&&(m->capabilities&XZ_SHELL_CAP_GRID)
  &&sh_has(d,XZ_SHELL_F_LOADED)&&d->loaded&&!alert_on(d);
}
/* A GRID button, below a stem row at the top of the band. */
static struct xz_shell_rect grid_button(const struct xz_shell_model *m, int deck, int i) {
 struct xz_shell_rect r=xz_shell_play_rect((enum xz_shell_play_region)(XZ_SHELL_R_GRID_RESET+i),deck),row=xz_shell_stem_row(m,deck);
 if(row.h&&row.y==lane_y(deck))r.y+=row.h;
 return r;
}
static void render_grid_buttons(const struct xz_shell_model *m, struct xz_canvas c, int deck) {
 static const char *const label[3]={"RESET","SNAP","SHIFT"};
 for(int i=0;i<3;i++){
  struct xz_shell_rect r=grid_button(m,deck,i);
  xz_rect(c,r.x,r.y,r.w,r.h,0x1c1c1c);
  xz_border(c,r.x,r.y,r.w,r.h,SH_BLUE);
  sh_text_center(c,r.x+r.w/2,r.y+(r.h-SH_CAP_SMALL)/2,label[i],SH_SMALL,SH_WHITE);
 }
}
/* The ZOOM/GRID box sits at the foot of deck 2's waveform, above a stem row there. */
static struct xz_shell_rect zoom_box(const struct xz_shell_model *m) {
 struct xz_shell_rect r=xz_shell_play_rect(XZ_SHELL_R_ZOOM,0),row=xz_shell_stem_row(m,1);
 if(row.h)r.y-=row.h;
 return r;
}
static void render_zoom(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 struct xz_shell_rect r=zoom_box(m);
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
 uint32_t accent=sh_deck_accent(u,d,SH_BLUE),tile=u->deck==deck?accent:SH_RAIL;
 xz_rect(c,sx,s.y+5,44,20,tile);
 if(tile!=accent&&accent!=SH_BLUE)xz_rect(c,sx,s.y+5,3,20,accent);
 snprintf(text,sizeof text,"DECK %d",deck+1);
 sh_text(c,sx+5,s.y+8,text,SH_SMALL,sh_ink_on(tile));
 int tx=sx+52;
 if(loaded_known&&!loaded)sh_text(c,tx,s.y+6,"Not Loaded.",SH_TEXT,SH_RED);
 else if(loaded){
  if(sh_has(d,XZ_SHELL_F_ARTWORK)&&d->artwork&&d->artwork_side>0){render_artwork(c,d,tx,s.y+5,20);tx+=26;}
  else{sh_note(c,tx,s.y+7,SH_WHITE);tx+=16;}
  int artist=sh_has(d,XZ_SHELL_F_ARTIST)&&d->artist&&*d->artist;
  if(sh_has(d,XZ_SHELL_F_TITLE)&&d->title)sh_text_fit(c,tx,artist?s.y+1:s.y+6,right-tx-22,d->title,SH_TEXT,SH_WHITE);
  else sh_text(c,tx,artist?s.y+1:s.y+6,"---",SH_TEXT,SH_DIM);
  if(artist)sh_text_fit(c,tx,s.y+19,right-tx-22,d->artist,SH_SMALL,SH_DIM);
 }else sh_text(c,tx,s.y+6,"---",SH_TEXT,SH_DIM);
 if(loaded&&sh_has(d,XZ_SHELL_F_PLAYING)){
  if(d->playing)sh_triangle(c,right-6,s.y+8,7,0,SH_WHITE);
  else{xz_rect(c,right-11,s.y+8,3,12,SH_DIM);xz_rect(c,right-5,s.y+8,3,12,SH_DIM);}
 }
 xz_rect(c,s.x,s.y+29,s.w,1,SH_BLACK);
 /* TRACK */
 sh_text(c,sx,NUM_Y,"TRACK",SH_SMALL,SH_DIM);
 if(loaded&&sh_has(d,XZ_SHELL_F_TRACK_NO)){snprintf(text,sizeof text,"%02d",d->track_no%100);sh_num(c,sx,NUM_Y+13,text,SH_NUM_MID,SH_WHITE);}
 else sh_num(c,sx,NUM_Y+13,"--",SH_NUM_MID,SH_DIM);
 /* time */
 int tm_known;enum xz_shell_time_mode mode=time_mode_of(m,d,&tm_known);
 int cx=sx+60;
 uint32_t mode_ink=sh_writable(m,XZ_SHELL_SET_TIME_MODE)?SH_WHITE:0x8c8c8c;
 sh_text(c,cx,NUM_Y,"REMAIN",SH_SMALL,tm_known&&mode==XZ_SHELL_TIME_REMAIN?mode_ink:0x5a5a5a);
 int sep=cx+sh_text_width(c,"REMAIN",SH_SMALL)+4;
 sh_text(c,sep,NUM_Y,"/",SH_SMALL,0x5a5a5a);
 sh_text(c,sep+8,NUM_Y,"TIME",SH_SMALL,tm_known&&mode==XZ_SHELL_TIME_ELAPSED?mode_ink:0x5a5a5a);
 /* QUANTIZE and its beat value between the time and tempo captions, where the stock screen has them
   (XZ manual pp.71-72, elements 24-25): red on, grey off. */
 if(sh_has(d,XZ_SHELL_F_QUANTIZE)&&sh_indicator_shown(u,XZ_SHELL_IND_QUANTIZE)){
  char beat[12];fmt_fraction(beat,sizeof beat,d->quantize_num>99?99:d->quantize_num,d->quantize_den>99?99:d->quantize_den);
  uint32_t q_ink=d->quantize_on?SH_RED:0x5a5a5a;
  int bw=sh_text_width(c,beat,SH_SMALL),qx=sx+QUANTIZE_RIGHT-bw-4-sh_text_width(c,"QUANTIZE",SH_SMALL);
  sh_text(c,qx,NUM_Y,"QUANTIZE",SH_SMALL,q_ink);
  sh_text(c,sx+QUANTIZE_RIGHT-bw,NUM_Y,beat,SH_SMALL,q_ink);
 }
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
 /* While KEY SYNC holds the key, MASTER TEMPO blinks on the model clock, half a second lit, half dark. */
 int key_sync=sh_has(d,XZ_SHELL_F_KEY_SYNC)&&d->key_synced;
 if(sh_has(d,XZ_SHELL_F_MASTER_TEMPO)||key_sync){
  int lit=key_sync?sh_key_sync_lit(m->clock_ms):d->master_tempo;
  uint32_t ink=lit?SH_RED:0x5a5a5a;
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

/* ---- pad row: stem mute buttons and hot-cue lamps ----
   The stems follow the reference DJ software's look: a dark button with the
   stem's colour as outline and label. A stem that plays is lit: tinted fill,
   full-colour outline and label. A muted stem goes dark, its outline and label
   dimmed, and the MUTE caption turns red while any stem is muted. */
static const int stem_slot[3]={0,2,1};   /* screen order DRUMS, VOCAL, INST -> stem index */
static const char *const stem_name[3]={"DRUMS","INST","VOCAL"};
static int stem_buttons(const struct xz_shell_model *m, int deck) {
 const struct xz_shell_deck *d=&m->deck[deck];
 return (m->capabilities&XZ_SHELL_CAP_STEM_MUTE)&&sh_has(d,XZ_SHELL_F_STEM_MUTE)&&(!sh_has(d,XZ_SHELL_F_LOADED)||d->loaded);
}
static uint32_t stem_rgb(const struct xz_shell_model *m, int stem) { return sh_stem_bright(m,stem); }
/* A row button as drawn: three across the row, the row's height less a 2 px margin (1 on half rows). */
static struct xz_shell_rect row_button(const struct xz_shell_model *m, int deck, int slot) {
 struct xz_shell_rect row=xz_shell_stem_row(m,deck);
 if(!row.h)return row;
 int w=(row.w-2*STEM_ROW_GAP)/3,inset=row.h>=STEM_ROW_FULL?2:1;
 return (struct xz_shell_rect){row.x+slot*(w+STEM_ROW_GAP),row.y+inset,slot==2?row.x+row.w-(row.x+2*(w+STEM_ROW_GAP)):w,row.h-2*inset};
}
struct xz_shell_rect xz_shell_stem_button(const struct xz_shell_model *m, int deck, int stem) {
 if(!m||deck<0||deck>=XZ_SHELL_DECKS||stem<0||stem>2||!stem_buttons(m,deck))return (struct xz_shell_rect){0,0,0,0};
 int slot=0;while(stem_slot[slot]!=stem)slot++;
 int layout=stem_layout(m);
 if(layout<0){
  int x=strip_x(deck)+STEM_X+slot*(STEM_W+STEM_GAP);
  return (struct xz_shell_rect){x,PAD_ROW_Y+PAD_ROW_H/2-22,STEM_W,44};
 }
 struct xz_shell_rect b=row_button(m,deck,slot),row=xz_shell_stem_row(m,deck);
 if(!b.h)return b;
 /* A half row's 22 px buttons answer 44 px tall, reaching into the waveform beside them. */
 if(b.h<44){int top=row.y==lane_y(deck);b.y=top?row.y:row.y+row.h-44;b.h=44;}
 return b;
}
/* Full rows are faders: a stem's level fills its button from the left. */
static int stem_fader(const struct xz_shell_model *m, int deck) {
 return stem_row_full(stem_layout(m))&&(m->capabilities&XZ_SHELL_CAP_STEM_GAIN)&&sh_has(&m->deck[deck],XZ_SHELL_F_STEM_GAIN);
}
static void render_stem_row(const struct xz_shell_model *m, struct xz_canvas c, int deck) {
 struct xz_shell_rect row=xz_shell_stem_row(m,deck);
 if(!row.h)return;
 const struct xz_shell_deck *d=&m->deck[deck];
 xz_rect(c,row.x,row.y,row.w,row.h,SH_BLACK);
 if(!stem_buttons(m,deck)){
  const char *why=m->stems_known&&!m->stems_on?"STEMS ARE OFF":sh_has(d,XZ_SHELL_F_LOADED)&&!d->loaded?"":"NO STEMS ON THIS TRACK";
  if(*why)sh_text_center(c,row.x+row.w/2,row.y+(row.h-SH_CAP_SMALL)/2,why,SH_SMALL,0x5a5a5a);
  return;
 }
 int full=row.h>=STEM_ROW_FULL,gain_known=sh_has(d,XZ_SHELL_F_STEM_GAIN);
 for(int slot=0;slot<3;slot++){
  int stem=stem_slot[slot],muted=(d->stem_muted>>stem)&1;
  struct xz_shell_rect b=row_button(m,deck,slot);
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
}
static void render_pad_row(const struct xz_shell_model *m, struct xz_canvas c, int deck) {
 const struct xz_shell_deck *d=&m->deck[deck];
 struct xz_shell_rect r=xz_shell_play_rect(XZ_SHELL_R_PAD_ROW,deck);
 xz_rect(c,r.x,r.y,r.w,r.h,SH_BLACK);
 int ty=r.y+(r.h-SH_CAP_SMALL)/2;
 if(stem_layout(m)<0&&stem_buttons(m,deck)){
  sh_text(c,r.x+8,ty,"MUTE",SH_SMALL,d->stem_muted&7?SH_RED:SH_DIM);
  for(int slot=0;slot<3;slot++){
   int stem=stem_slot[slot],muted=(d->stem_muted>>stem)&1;
   uint32_t rgb=stem_rgb(m,stem);
   int x=r.x+STEM_X+slot*(STEM_W+STEM_GAP),y=r.y+(r.h-STEM_H)/2;
   if(muted){
    xz_rect(c,x,y,STEM_W,STEM_H,0x101010);
    xz_border(c,x,y,STEM_W,STEM_H,xz_blend(0,rgb,90));
    sh_text_center(c,x+STEM_W/2,y+(STEM_H-SH_CAP_SMALL)/2,stem_name[stem],SH_SMALL,xz_blend(0,rgb,110));
   }else{
    xz_rect(c,x,y,STEM_W,STEM_H,xz_blend(0,rgb,70));
    xz_border(c,x,y,STEM_W,STEM_H,rgb);xz_border(c,x+1,y+1,STEM_W-2,STEM_H-2,rgb);
    sh_text_center(c,x+STEM_W/2,y+(STEM_H-SH_CAP_SMALL)/2,stem_name[stem],SH_SMALL,xz_blend(rgb,SH_WHITE,90));
   }
  }
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

static void play_render(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 /* Regions tile the frame; only the seams between them are cleared here. */
 xz_rect(c,0,lane_y(0)+LANE_H,c.width,LANE_GAP,SH_BLACK);
 xz_rect(c,0,lane_y(1)+LANE_H,c.width,PAD_ROW_Y-lane_y(1)-LANE_H,SH_BLACK);
 xz_rect(c,0,PAD_ROW_Y,2,c.height-PAD_ROW_Y,SH_BLACK);xz_rect(c,HALF_W-2,PAD_ROW_Y,4,c.height-PAD_ROW_Y,SH_BLACK);xz_rect(c,c.width-2,PAD_ROW_Y,2,c.height-PAD_ROW_Y,SH_BLACK);
 render_top(u,m,c);
 for(int deck=0;deck<XZ_SHELL_DECKS;deck++){
  render_column(u,m,c,deck);
  render_lane(u,m,c,deck);
  render_pad_row(m,c,deck);
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
/* NEEDLE LOCK (XZ manual p.123): while LOCK is set the overview searches only on a paused deck. */
int sh_needle_locked(const struct xz_shell_model *m, const struct xz_shell_deck *d) {
 return m->settings.needle_lock_known&&m->settings.needle_lock&&sh_has(d,XZ_SHELL_F_PLAYING)&&d->playing;
}
static size_t seek(struct xz_shell *u, const struct xz_shell_model *m, int deck, int x, struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t n) {
 const struct xz_shell_deck *d=&m->deck[deck];
 if(!(m->capabilities&XZ_SHELL_CAP_SEEK)||!sh_has(d,XZ_SHELL_F_TOTAL)||d->total_ms<=0)return n;
 if(!sh_has(d,XZ_SHELL_F_LOADED)||!d->loaded)return n;
 if(sh_needle_locked(m,d)){u->notice="NEEDLE LOCK: PAUSE THE DECK TO SEARCH";u->notice_at_ms=m->clock_ms;return n;}
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
 for(int i=0;i<3&&mode_lamps(u);i++)if(header_band(x,y,chips[i].r)){
  if(!sh_writable(m,chips[i].setting)||!sh_setting(m,chips[i].setting))return 0;
  int value=chips[i].setting==XZ_SHELL_SET_PLAY_MODE?(m->settings.play_mode==XZ_SHELL_PLAY_SINGLE?XZ_SHELL_PLAY_CONTINUE:XZ_SHELL_PLAY_SINGLE):
   chips[i].setting==XZ_SHELL_SET_AUTO_CUE?!m->settings.auto_cue:(m->settings.hot_cue_auto_load+1)%3;
  return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_SET_SETTING,-1,value,(int)chips[i].setting});
 }
 if(y<TOP_H)return 0;
 for(int deck=0;deck<XZ_SHELL_DECKS;deck++)for(int stem=0;stem<3;stem++){
  if(!sh_inside(xz_shell_stem_button(m,deck,stem),x,y))continue;
  if(stem_layout(m)>=0){
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
 for(int deck=0;deck<XZ_SHELL_DECKS;deck++){
  if(alert_on(&m->deck[deck])&&sh_inside(xz_shell_play_rect(XZ_SHELL_R_ALERT_BUTTON,deck),x,y)){
   n=activate(u,deck,out,n);
   return n+sh_key(u,m,XZ_SHELL_KEY_BROWSE,deck,out+n);
  }
  if(grid_edit_shown(u,m,deck))for(int i=0;i<3;i++)
   if(sh_inside(grid_button(m,deck,i),x,y))
    return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_GRID_EDIT,deck,0,i});
 }
 if(sh_inside(zoom_box(m),x,y)){
  if(m->capabilities&XZ_SHELL_CAP_GRID)u->grid_mode=!u->grid_mode;
  return 0;
 }
 for(int deck=0;deck<XZ_SHELL_DECKS;deck++){
  if(sh_inside(xz_shell_play_rect(XZ_SHELL_R_LANE,deck),x,y))return activate(u,deck,out,n);
  if(sh_inside(xz_shell_play_rect(XZ_SHELL_R_OVERVIEW,deck),x,y)){
   u->down_region=XZ_SHELL_TOUCH_OVERVIEW;u->down_deck=deck;
   return seek(u,m,deck,x,out,n);
  }
  if(sh_inside(xz_shell_play_rect(XZ_SHELL_R_TIME,deck),x,y)){
   if(!sh_writable(m,XZ_SHELL_SET_TIME_MODE))return 0;
   int known;enum xz_shell_time_mode mode=time_mode_of(m,&m->deck[deck],&known);
   return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_SET_SETTING,deck,mode==XZ_SHELL_TIME_REMAIN?XZ_SHELL_TIME_ELAPSED:XZ_SHELL_TIME_REMAIN,XZ_SHELL_SET_TIME_MODE});
  }
 }
 return n;
}
/* Sliding a full-row stem button: the level follows the finger, a button's width for the whole range. */
static size_t stem_slide(struct xz_shell *u, const struct xz_shell_model *m, int x, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 int deck=u->down_deck,stem=u->stem_touch.stem;
 if(deck<0||deck>=XZ_SHELL_DECKS||stem<0||stem>2||!stem_fader(m,deck))return 0;
 int dx=x-u->stem_touch.x0;
 if(!u->stem_touch.slid&&dx<STEM_SLIDE&&dx>-STEM_SLIDE)return 0;
 u->stem_touch.slid=1;
 int slot=0;while(stem_slot[slot]!=stem)slot++;
 int w=sh_max(1,row_button(m,deck,slot).w);
 int gain=sh_max(0,sh_min(XZ_SHELL_STEM_GAIN_MAX,u->stem_touch.gain0+dx*XZ_SHELL_STEM_GAIN_MAX/w));
 if(gain==m->deck[deck].stem_gain[stem])return 0;
 return emit(out,0,(struct xz_shell_action){XZ_SHELL_ACT_STEM_GAIN,deck,gain,stem});
}
static size_t play_input(struct xz_shell *u, const struct xz_shell_model *m, const struct xz_shell_event *e, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 switch(e->kind){
 case XZ_SHELL_TOUCH_DOWN:return touch_down(u,m,e->x,e->y,out);
 case XZ_SHELL_TOUCH_MOVE:
  if(u->down&&u->down_region==XZ_SHELL_TOUCH_STEM)return stem_slide(u,m,e->x,out);
  if(u->down&&u->down_region==XZ_SHELL_TOUCH_OVERVIEW&&!sh_needle_locked(m,&m->deck[u->down_deck]))return seek(u,m,u->down_deck,e->x,out,0);
  return 0;
 case XZ_SHELL_TOUCH_UP:{
  size_t n=0;
  if(u->down&&u->down_region==XZ_SHELL_TOUCH_STEM&&!u->stem_touch.slid){
   int deck=u->down_deck,stem=u->stem_touch.stem;
   if(deck>=0&&deck<XZ_SHELL_DECKS&&stem>=0&&stem<3&&stem_buttons(m,deck)){
    int muted=(m->deck[deck].stem_muted>>stem)&1;
    n=emit(out,0,(struct xz_shell_action){XZ_SHELL_ACT_STEM_MUTE,deck,!muted,stem});
   }
  }
  u->down=0;u->down_region=XZ_SHELL_TOUCH_NONE;
  return n;
 }
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
