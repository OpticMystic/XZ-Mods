/* SPDX-License-Identifier: MIT */
/* FLAGSHIP MODS skin: the UTILITY / SHORTCUT / browse chrome of the current
   flagship club players, adapted to 800x480. Measurements come from
   docs/research/2026-10-18-cdj3000-xdj-az-ui.md sections 3.3-3.5 and 4:
   #191919 header and list rows with 1 px black separators, a #3c3c3c category
   rail whose open cell is #646465, #007de1 for the selected row, #ff4619 for
   alarms, white caps text with #a4a4a4 captions, outlined caption boxes that
   carry a big value, and hatched #313131 quick-menu tiles. */
#include "ui_skin.h"
#include <stdio.h>
#include <string.h>

#ifndef XZ_FLAGSHIP_CHROME
#define XZ_FLAGSHIP_CHROME 0x191919
#endif
#ifndef XZ_FLAGSHIP_RAIL
#define XZ_FLAGSHIP_RAIL 0x3c3c3c
#endif
#ifndef XZ_FLAGSHIP_RAIL_SELECTED
#define XZ_FLAGSHIP_RAIL_SELECTED 0x646465
#endif
#ifndef XZ_FLAGSHIP_TILE
#define XZ_FLAGSHIP_TILE 0x313131
#endif
#ifndef XZ_FLAGSHIP_DIM
#define XZ_FLAGSHIP_DIM 0xa4a4a4
#endif
#ifndef XZ_FLAGSHIP_BLUE
#define XZ_FLAGSHIP_BLUE 0x007de1
#endif
#ifndef XZ_FLAGSHIP_RED
#define XZ_FLAGSHIP_RED 0xff4619
#endif
#ifndef XZ_FLAGSHIP_GREEN
#define XZ_FLAGSHIP_GREEN 0x28e028
#endif
#ifndef XZ_FLAGSHIP_WAVE_LOW
#define XZ_FLAGSHIP_WAVE_LOW 0x0055e1
#endif

enum { BLACK=0, WHITE=0xffffff, CHROME=XZ_FLAGSHIP_CHROME, RAIL=XZ_FLAGSHIP_RAIL, RAIL_ON=XZ_FLAGSHIP_RAIL_SELECTED,
 TILE=XZ_FLAGSHIP_TILE, TILE_HATCH=0x3b3b3b, DIM=XZ_FLAGSHIP_DIM, BLUE=XZ_FLAGSHIP_BLUE, RED=XZ_FLAGSHIP_RED,
 GREEN=XZ_FLAGSHIP_GREEN, WAVE=XZ_FLAGSHIP_WAVE_LOW };
enum { X0=XZ_UI_CX, CW=XZ_UI_CW, TOP=XZ_UI_HEAD };
enum { ALIGN_L, ALIGN_C, ALIGN_R };

/* Every widget kind maps to one row style before anything is drawn. The
   table is the design: a kind not listed here is a plain UTILITY list row. */
enum style { ROW, TOGGLE, CHOICE, PRIMARY, PAD, STRIP, SLIDER, THEME, DECK, KEY, RAIL_CELL, CARD, LINK, READOUT, SHARED };
static const unsigned char kind_style[XZ_UI_REKORDBOX_ASSIGN+1]={
 [XZ_UI_PANEL]=LINK,[XZ_UI_FEATURE_LINK]=LINK,[XZ_UI_BACK]=KEY,[XZ_UI_CLOSE]=KEY,[XZ_UI_DECK]=DECK,
 [XZ_UI_SET_THEME]=THEME,[XZ_UI_GROOVE_PAD]=PAD,[XZ_UI_HOTCUE_PAD]=PAD,[XZ_UI_SAMPLE_PAD]=PAD,
 [XZ_UI_STRIP]=STRIP,[XZ_UI_VOLUME]=SLIDER,[XZ_UI_LEVEL]=SLIDER,
 [XZ_UI_ENABLE]=TOGGLE,[XZ_UI_PAD_FEEDBACK]=TOGGLE,[XZ_UI_JUMP_ENABLE]=TOGGLE,[XZ_UI_JUMP_SHIFT]=TOGGLE,
 [XZ_UI_SHIFT_PAGES]=TOGGLE,[XZ_UI_SHIFT_KEYSYNC]=TOGGLE,[XZ_UI_TAKEOVER_TOGGLE]=TOGGLE,[XZ_UI_KEY_FEEDBACK]=TOGGLE,
 [XZ_UI_RAM_DEBUG]=TOGGLE,[XZ_UI_REKORDBOX_KEY]=TOGGLE,[XZ_UI_CONNECTION_ENABLE]=TOGGLE,[XZ_UI_DISCOVERY]=TOGGLE,
 [XZ_UI_HOLD]=TOGGLE,[XZ_UI_OVERDUB]=TOGGLE,[XZ_UI_METRICS_TOGGLE]=TOGGLE,[XZ_UI_MUTE]=TOGGLE,[XZ_UI_BYPASS]=TOGGLE,
 [XZ_UI_STEM_PAGE]=CHOICE,[XZ_UI_WAVE_MODE]=CHOICE,[XZ_UI_KEY_FORMAT]=CHOICE,[XZ_UI_TAKEOVER_ASSIGN]=CHOICE,
 [XZ_UI_CONTROL_SECTION]=CHOICE,[XZ_UI_JUMP_EDIT_PAGE]=CHOICE,[XZ_UI_RAM_DECK]=CHOICE,[XZ_UI_SDK_STEP]=CHOICE,
 [XZ_UI_SDK_CHOICE]=CHOICE,[XZ_UI_WAD_SELECT]=CHOICE,[XZ_UI_STEM_BANK]=CHOICE,
 [XZ_UI_SET_INTERFACE]=CHOICE,[XZ_UI_SET_MODERN_THEME]=CHOICE,[XZ_UI_REKORDBOX_ASSIGN]=CHOICE,
 [XZ_UI_DOOM_START]=PRIMARY,[XZ_UI_UPDATE_CHECK]=PRIMARY,[XZ_UI_RAM_REQUEST]=PRIMARY,[XZ_UI_PC_CHECK]=PRIMARY,
 [XZ_UI_PC_TEST]=PRIMARY,[XZ_UI_KEY_CHECK]=PRIMARY,[XZ_UI_LOCAL_STEM_SETUP_STEP]=PRIMARY,[XZ_UI_METRICS_CHECK]=PRIMARY,
 [XZ_UI_MYHOUSE_CHECK]=PRIMARY,[XZ_UI_DOOM_CHECK]=PRIMARY,[XZ_UI_FEATURE_SETUP_OPEN]=PRIMARY,
 [XZ_UI_FEATURE_SETUP_STEP]=PRIMARY,[XZ_UI_LOCAL_STEM_SETUP_OPEN]=PRIMARY,[XZ_UI_PC_CONFIRM]=PRIMARY,
 [XZ_UI_GAME_SELECT]=CARD,[XZ_UI_ARCADE_OPEN]=CARD,[XZ_UI_WAVE_RIDER_OPEN]=CARD,[XZ_UI_RHYTHM_LAB_OPEN]=CARD,
 [XZ_UI_GLYPH_RUN_OPEN]=CARD,
 /* Drawn by shared code after the skin (ui_layout.c, ui.c). */
 [XZ_UI_STEM_PAD_CONFIG]=SHARED,[XZ_UI_JUMP_PAIR]=SHARED,[XZ_UI_STEMS_OVERLAY]=SHARED,[XZ_UI_STEM_ORDER]=SHARED,
 [XZ_UI_STEM_COLOR]=SHARED,[XZ_UI_STEM_COLORS_RESET]=SHARED,
};
/* Launch cards on EXTRAS and GAMES carry a tagline; the same words as skin_classic.c. */
static const char *tagline(const struct xz_ui_widget *a){
 switch(a->kind){
 case XZ_UI_PANEL:switch(a->index){
  case XZ_UI_CONNECTION:return "COMPUTER SCREEN AND FILMSTRIP LINK";
  case XZ_UI_GAMES:return "CLASSIC GAMES AND BEAT ARCADE";
  case XZ_UI_APPS:return "XZ SDK APPS FROM USB";
  case XZ_UI_UPDATES:return "SIGNED UPDATES, ACTIVE NEXT BOOT";
  case XZ_UI_DOOM_FILES:return "BASE GAME + ADD-ON";
  case XZ_UI_MYHOUSE_SETUP:return "NEEDS THE NATIVE GZDOOM PORT";
  case XZ_UI_PROFILE_SETUP:return "YOUR NAME AND LOGO FOR VJ SCREENS";
  case XZ_UI_NETWORK:return "ADAPTER, ADDRESS, APPLY FOR THIS BOOT";
  case XZ_UI_SUPPORT:return "PREPARE, CHECK AND SEND A REPORT";
  case XZ_UI_ABOUT:return "CREDITS, LINKS AND SUPPORT";
  default:return NULL;}
 case XZ_UI_GAME_SELECT:return a->index?"ORIGINAL GAME + PATCH":"DOOM AND DOOM II DATA";
 case XZ_UI_ARCADE_OPEN:return "BEAT-SYNCED, JOG WHEEL";
 case XZ_UI_WAVE_RIDER_OPEN:return "RIDE THE TRACK WAVEFORM";
 case XZ_UI_RHYTHM_LAB_OPEN:return "PLAYED ON THE PADS";
 case XZ_UI_GLYPH_RUN_OPEN:return "RUNNER SET TO THE TRACK";
 default:return NULL;}
}
/* The header names the open page, as the MODERN path line does. */
static const char *section_title(enum xz_ui_page page){return xz_ui_page_title((int)page);}
static enum style style_of(const struct xz_ui_widget *a){
 enum style s=(unsigned)a->kind<=(unsigned)XZ_UI_SET_MODERN_THEME?(enum style)kind_style[a->kind]:ROW;
 if(a->kind==XZ_UI_PANEL&&a->x<XZ_UI_RAIL_W)return RAIL_CELL;
 if(a->kind==XZ_UI_PC_CONFIRM&&a->index!=1)return ROW;
 if(a->kind==XZ_UI_KEY_SHIFT&&a->index==0)return READOUT;
 if((s==LINK||s==CARD)&&tagline(a)&&a->h>=72)return CARD;
 if(s==CARD)return LINK;
 return s;
}

/* Text fits its box: scale 2 when it fits, else scale 1, always clipped. */
static int glyph_h(int scale){return scale>1?14:9;}
static void text(struct xz_canvas c,int x,int y,int w,int h,const char *s,int scale,int align,uint32_t col){
 if(!s||!*s||w<=0||h<=0)return;
 if(scale>1&&xz_text_width(c,s,2,200)>w)scale=1;
 int tw=xz_text_width(c,s,scale,200);if(tw>w)tw=w;
 int tx=align==ALIGN_L?0:align==ALIGN_C?(w-tw)/2:w-tw;
 struct xz_canvas box=xz_sub(c,x,y,w,h);
 xz_text(box,tx,(h-glyph_h(scale))/2,s,scale,200,col);
}
static void hline(struct xz_canvas c,int x,int y,int w,uint32_t col){xz_rect(c,x,y,w,1,col);}
/* Outlined caption box: NOT READY, READ ONLY and the flagship status tags. Returns its width. */
static int tag(struct xz_canvas c,int right,int cy,const char *word,uint32_t col){
 int w=xz_text_width(c,word,1,20)+10,x=right-w;
 xz_border(c,x,cy-7,w,14,col);text(c,x,cy-7,w,14,word,1,ALIGN_C,col);
 return w;
}
/* The two-deck player's outlined ON/OFF chip: solid white when on. */
static void chip(struct xz_canvas c,int x,int y,int w,int h,const char *word,int on,uint32_t off_col){
 if(on){xz_round_rect(c,x,y,w,h,3,15,WHITE);text(c,x,y,w,h,word,1,ALIGN_C,BLACK);}
 else{xz_round_rect(c,x,y,w,h,3,15,off_col);xz_round_rect(c,x+1,y+1,w-2,h-2,2,15,CHROME);text(c,x,y,w,h,word,1,ALIGN_C,off_col);}
}
static void chevron(struct xz_canvas c,int x,int y,int dir,uint32_t color){
 xz_line16(c,x*16,(y-5)*16,(x+5*dir)*16,y*16,28,color);xz_line16(c,(x+5*dir)*16,y*16,x*16,(y+5)*16,28,color);
}
/* UTILITY value-pane radio: a white ring, filled with dot when chosen (blue on a
   dark row, white on the blue selected row). */
static void radio(struct xz_canvas c,int cx,int cy,int on,uint32_t dot){
 xz_ring(c,cx,cy,7,2,WHITE);if(on)xz_disc(c,cx,cy,4,dot);
}
/* Quick-menu tile: #313131 with a low-contrast diagonal hatch on a 4 px pitch. */
static void hatch(struct xz_canvas c,int x,int y,int w,int h){
 xz_rect(c,x,y,w,h,TILE);
 for(int j=0;j<h;j++)for(int i=(j&3);i<w;i+=4)xz_rect(c,x+i,y+j,1,1,TILE_HATCH);
}
static int percent(float v){int n=(int)(v*100+.5f);return n<0?0:n>100?100:n;}
static uint32_t on_fill(uint32_t fill){return xz_theme_luma(fill)>142?BLACK:WHITE;}

/* The flagship status band in miniature: PLAYER box, title, BPM box, overview waveform. */
static void deck_header(struct xz_canvas c,const struct xz_ui *u,const struct xz_ui_deck *d,char *buf,size_t len){
 int wave_top=92,wave_h=u->page==XZ_UI_STEMS?44:28;
 snprintf(buf,len,"%d",u->deck+1);
 xz_border(c,X0,48,44,36,WHITE);text(c,X0+4,51,36,9,"DECK",1,ALIGN_L,DIM);text(c,X0+4,62,36,18,buf,2,ALIGN_R,WHITE);
 text(c,X0+56,48,512,36,d->track?d->track:(u->deck<2?"NO TRACK LOADED":"COMPUTER DECK / EXTERNAL AUDIO"),2,ALIGN_L,WHITE);
 xz_border(c,680,48,112,36,WHITE);text(c,684,51,40,9,"BPM",1,ALIGN_L,DIM);
 if(d->bpm>0)snprintf(buf,len,"%.1f",(double)d->bpm);else snprintf(buf,len,"---.-");
 text(c,684,62,104,18,buf,2,ALIGN_R,WHITE);
 if(d->wave_peaks&&d->wave_count){
  size_t n=d->wave_count<328?d->wave_count:328;int base=wave_top+wave_h;
  for(size_t j=0;j<n;j++){
   int h=d->wave_peaks[j*d->wave_count/n]*wave_h/255;if(h<2)h=2;
   int x=X0+(int)(j*CW/n);xz_rect(c,x,base-h,2,h,WAVE);xz_rect(c,x,base-h,2,1,WHITE);
  }
  hline(c,X0,base,CW,RAIL);
 }else{
  hline(c,X0,wave_top+wave_h/2,CW,RAIL);
  int tw=xz_text_width(c,"WAVEFORM NOT AVAILABLE",1,30)+16;
  xz_rect(c,X0+(CW-tw)/2,wave_top+wave_h/2-7,tw,14,BLACK);
  text(c,X0,wave_top+wave_h/2-7,CW,14,"WAVEFORM NOT AVAILABLE",1,ALIGN_C,DIM);
 }
}
/* HOT CUE pad chip: outlined when ready, filled with a white edge when playing,
   #3c3c3c with NOT READY when the pad has nothing to play. */
static void pad(struct xz_canvas c,const struct xz_ui_widget *a,uint32_t col,int available,int active,const char *state){
 int small=a->h<90;uint32_t fill=active?col:BLACK,edge=!available?RAIL:active?WHITE:col;
 xz_round_rect(c,a->x,a->y,a->w,a->h,4,15,edge);
 xz_round_rect(c,a->x+2,a->y+2,a->w-4,a->h-4,3,15,active?WHITE:fill);
 if(active)xz_round_rect(c,a->x+4,a->y+4,a->w-8,a->h-8,2,15,fill);
 uint32_t ink=!available?DIM:active?on_fill(fill):col;
 text(c,a->x+8,a->y+6,a->w-16,small?16:22,a->label,2,ALIGN_L,ink);
 text(c,a->x+8,a->y+a->h-(small?16:20),a->w-16,12,state,1,ALIGN_L,!available?RED:active?on_fill(fill):DIM);
}
static void strip(struct xz_canvas c,const struct xz_ui_widget *a,const struct xz_ui_deck *d,int available){
 int cw=a->w/6,lane_top=a->y+40,lane_h=a->h-58;
 for(int k=0;k<6;k++){
  int x=a->x+k*cw,active=available&&d->loop_index==k;
  xz_round_rect(c,x+4,a->y+4,cw-8,28,3,15,active?WHITE:available?WHITE:DIM);
  if(!active)xz_round_rect(c,x+5,a->y+5,cw-10,26,2,15,BLACK);
  text(c,x+4,a->y+4,cw-8,28,xz_ui_lengths[k],2,ALIGN_C,active?BLACK:available?WHITE:DIM);
  xz_rect(c,x+cw/2-2,lane_top,4,lane_h,RAIL);
 }
 hline(c,a->x+8,lane_top+lane_h/2,a->w-16,RAIL);
 text(c,a->x+6,lane_top,30,9,"+12",1,ALIGN_L,DIM);text(c,a->x+6,lane_top+lane_h-9,30,9,"-12",1,ALIGN_L,DIM);
 if(available&&d->loop_index>=0&&d->loop_index<6){
  float pitch=d->pitch<-12?-12:d->pitch>12?12:d->pitch;
  int py=lane_top+(int)((12-pitch)*(float)(lane_h-6)/24);
  xz_round_rect(c,a->x+d->loop_index*cw+cw/2-14,py,28,6,2,15,BLUE);
 }
 if(!available)tag(c,a->x+a->w-8,a->y+a->h-14,"NOT READY",RED);
}
static void slider(struct xz_canvas c,const struct xz_ui_widget *a,const char *caption,float value,uint32_t bar,int available,char *buf,size_t len){
 int v=percent(value);
 xz_border(c,a->x,a->y,a->w,a->h,available?WHITE:RAIL);
 text(c,a->x+6,a->y+3,a->w-12,9,caption,1,ALIGN_L,DIM);
 if(available){
  snprintf(buf,len,"%d%%",v);text(c,a->x+6,a->y+12,a->w-12,16,buf,2,ALIGN_R,WHITE);
  xz_rect(c,a->x+6,a->y+a->h-8,a->w-12,4,RAIL);xz_rect(c,a->x+6,a->y+a->h-8,(a->w-12)*v/100,4,bar);
 }else tag(c,a->x+a->w-6,a->y+a->h/2+2,"NOT READY",RED);
}
static void theme_row(struct xz_canvas c,const struct xz_ui_widget *a,int selected){
 const struct xz_theme_palette *sample=xz_theme_palette(a->index);int cy=a->y+a->h/2;
 xz_rect(c,a->x,a->y,a->w,a->h,CHROME);
 radio(c,a->x+12,cy,selected,BLUE);
 text(c,a->x+26,a->y+6,a->w-32,12,a->label,1,ALIGN_L,WHITE);
 text(c,a->x+26,a->y+a->h-16,a->w-80,10,a->index>=XZ_THEME_LCARS?"FULL SKIN":"",1,ALIGN_L,DIM);
 for(int k=0;k<4;k++)xz_rect(c,a->x+a->w-44+k*10,a->y+a->h-15,8,8,k<3?sample->stem[k]:sample->accent);
}
static void deck_tag(struct xz_canvas c,const struct xz_ui_widget *a,int selected,char *buf,size_t len){
 int x=a->x+4,y=a->y+6,w=a->w-8,h=a->h-12;
 snprintf(buf,len,"DECK %d",a->index+1);
 if(selected)xz_rect(c,x,y,w,h,BLUE);else{xz_rect(c,x,y,w,h,CHROME);xz_border(c,x,y,w,h,WHITE);}
 int tw=xz_text_width(c,buf,2,8);
 text(c,x+8,y,w-16,h,buf,2,ALIGN_L,WHITE);
 text(c,x+8+tw+8,y,w-tw-24,h,a->label,1,ALIGN_R,selected?WHITE:DIM);
}
static void card(struct xz_canvas c,const struct xz_ui_widget *a,int available,int lit){
 enum xz_ui_page icon=a->kind==XZ_UI_PANEL?(enum xz_ui_page)a->index:XZ_UI_GAMES;
 const char *line=tagline(a);int big=a->h>=120;uint32_t ink=available?WHITE:DIM;
 hatch(c,a->x,a->y,a->w,a->h);
 if(big){
  xz_ui_chrome_icon(c,icon,a->x+40,a->y+40,ink);
  text(c,a->x+16,a->y+a->h-70,a->w-48,16,a->label,2,ALIGN_L,ink);
  text(c,a->x+16,a->y+a->h-46,a->w-48,12,line,1,ALIGN_L,DIM);
  xz_rect(c,a->x+16,a->y+a->h-24,a->w-48,4,lit?BLUE:RAIL);
 }else{
  xz_ui_chrome_icon(c,icon,a->x+32,a->y+a->h/2,ink);
  text(c,a->x+62,a->y+12,a->w-96,16,a->label,2,ALIGN_L,ink);
  text(c,a->x+62,a->y+34,a->w-96,12,line,1,ALIGN_L,DIM);
  xz_rect(c,a->x+62,a->y+a->h-16,a->w-96,4,lit?BLUE:RAIL);
 }
 chevron(c,a->x+a->w-22,a->y+a->h/2,1,ink);
}
/* UTILITY list row: label left, the current value right, blue when selected. */
static void row(struct xz_canvas c,const struct xz_ui_widget *a,enum style s,int available,int selected,const char *value,uint32_t value_col,const char *blocked){
 int tall=a->h>=56,cy=a->y+a->h/2,right=a->x+a->w-10;
 uint32_t fill=s==PRIMARY&&available?BLUE:s==CHOICE&&selected&&available?BLUE:CHROME;
 uint32_t ink=!available?DIM:WHITE;
 xz_rect(c,a->x,a->y,a->w,a->h,fill);hline(c,a->x,a->y+a->h-1,a->w,BLACK);
 if(s==ROW&&selected&&available)xz_rect(c,a->x,a->y,4,a->h-1,BLUE);
 if(!available){
  if(a->w>=150)right-=tag(c,right,tall?a->y+a->h-14:cy,blocked,RED)+8;
  else{xz_disc(c,right-4,cy,3,RED);right-=12;}
 }else if(s==TOGGLE){
  int cw=a->w<200?40:48;chip(c,right-cw,cy-10,cw,20,selected?"ON":"OFF",selected,DIM);right-=cw+8;
 }else if(s==CHOICE&&a->w>=120){radio(c,right-8,cy,selected,WHITE);right-=24;}
 else if(s==LINK){chevron(c,right-8,cy,1,WHITE);right-=20;}
 if(value&&*value){
  int vw=xz_text_width(c,value,1,30);
  text(c,right-vw,tall?a->y+a->h-22:a->y,vw,tall?12:a->h,value,1,ALIGN_R,value_col);
  right-=vw+8;
 }
 int lw=right-(a->x+10);
 /* Link labels end in " >" for skins without a chevron; this one draws the arrow itself. */
 char plain[64];size_t n=strlen(a->label);
 if(s==LINK&&n>2&&n<sizeof(plain)&&!strcmp(a->label+n-2," >")){memcpy(plain,a->label,n-2);plain[n-2]=0;}
 else{strncpy(plain,a->label,sizeof(plain)-1);plain[sizeof(plain)-1]=0;}
 if(s==PRIMARY&&available)text(c,a->x+10,a->y,a->w-20,a->h,plain,2,ALIGN_C,WHITE);
 else text(c,a->x+10,tall?a->y+8:a->y,lw,tall?16:a->h,plain,2,ALIGN_L,ink);
}

static void render(const struct xz_ui_scene *s){
 struct xz_canvas c=s->c;const struct xz_ui *u=s->u;const struct xz_ui_model *m=s->m;
 const struct xz_ui_deck *d=s->d;char buf[96];size_t i;
 int decks=0;for(i=0;i<s->n;i++)decks|=s->w[i].kind==XZ_UI_DECK&&s->w[i].y<TOP;
 xz_rect(c,0,0,800,480,BLACK);
 /* Header band and the category rail. */
 xz_rect(c,0,0,800,TOP,CHROME);hline(c,0,TOP,800,BLACK);
 xz_rect(c,0,TOP+1,XZ_UI_RAIL_W,XZ_UI_DOCK_Y-TOP-1,RAIL);
 text(c,X0,5,200,9,"XZ MODS",1,ALIGN_L,DIM);
 text(c,X0,19,decks?156:420,16,section_title(u->page),2,ALIGN_L,WHITE);
 if(!decks&&!m->dj)text(c,600,0,112,TOP,"xzmods.com",1,ALIGN_R,BLUE);
 for(i=0;i<s->n;i++){
  struct xz_ui_widget a=s->w[i];int available=s->look[i].available,selected=s->look[i].selected;uint32_t color=s->look[i].color;
  const char *blocked=a.kind==XZ_UI_CONNECTION_ENABLE||a.kind==XZ_UI_DISCOVERY?"READ ONLY":"NOT READY";
  switch(style_of(&a)){
  case SHARED:break;
  case RAIL_CELL:
   xz_rect(c,a.x,a.y,a.w,a.h,selected?RAIL_ON:RAIL);xz_rect(c,a.x,a.y+a.h,a.w,1,BLACK);xz_rect(c,a.x,a.y-1,a.w,1,BLACK);
   xz_ui_chrome_icon(c,(enum xz_ui_page)a.index,a.x+a.w/2,a.y+15,WHITE);
   text(c,a.x+2,a.y+a.h-19,a.w-4,12,a.label,1,ALIGN_C,WHITE);
   break;
  case KEY:{
   int bx=a.kind==XZ_UI_BACK?a.x+8:a.x+a.w-42,cy=a.y+a.h/2;
   xz_border(c,bx,a.y+6,34,28,WHITE);
   if(a.kind==XZ_UI_BACK){
    chevron(c,bx+12,cy,-1,WHITE);xz_rect(c,bx+12,cy-1,12,2,WHITE);
    text(c,bx+42,a.y,60,a.h,"BACK",1,ALIGN_L,DIM);
   }else{
    xz_line16(c,(bx+12)*16,(cy-5)*16,(bx+22)*16,(cy+5)*16,28,WHITE);xz_line16(c,(bx+12)*16,(cy+5)*16,(bx+22)*16,(cy-5)*16,28,WHITE);
    text(c,a.x+4,a.y,bx-a.x-8,a.h,"CLOSE",1,ALIGN_R,DIM);
   }
   break;}
  case DECK:deck_tag(c,&a,selected,buf,sizeof(buf));break;
  case THEME:theme_row(c,&a,selected);break;
  case CARD:card(c,&a,available,selected||(a.kind==XZ_UI_PANEL&&a.index==XZ_UI_CONNECTION&&m->connection.connected));break;
  case PAD:{
   int active=a.kind==XZ_UI_GROOVE_PAD?d->groove_active==a.index:a.kind==XZ_UI_SAMPLE_PAD?selected:0;
   uint32_t col=a.kind==XZ_UI_HOTCUE_PAD?GREEN:color;
   const char *state=!available?"NOT READY":a.kind==XZ_UI_HOTCUE_PAD?"HOT CUE":active?"PLAYING":"READY";
   pad(c,&a,col,available,available&&active,state);
   break;}
  case STRIP:strip(c,&a,d,available);break;
  case SLIDER:{
   int level=a.kind==XZ_UI_LEVEL;
   slider(c,&a,level?a.label:"VOLUME",level?d->levels[a.index&3]:d->sample_volume,level?s->p->stem[a.index%3]:BLUE,available&&!(level&&(d->muted&(1u<<a.index))),buf,sizeof(buf));
   break;}
  case READOUT:
   xz_border(c,a.x,a.y,a.w,a.h,available?WHITE:RAIL);
   if(available)snprintf(buf,sizeof(buf),"%+d",d->key_semitones);else snprintf(buf,sizeof(buf),"--");
   text(c,a.x+6,a.y+3,40,9,"KEY",1,ALIGN_L,DIM);
   text(c,a.x+6,a.y+12,a.w-12,16,buf,2,ALIGN_L,available?WHITE:DIM);
   if(available)text(c,a.x+6,a.y+a.h-14,a.w-12,10,"RESET",1,ALIGN_R,DIM);
   else tag(c,a.x+a.w-6,a.y+a.h/2,"NOT READY",RED);
   break;
  default:{
   enum style st=style_of(&a);const char *value=NULL;uint32_t value_col=WHITE;
   if(a.kind==XZ_UI_SERVER_AUTO){value=m->server_auto?"AUTO":"MANUAL";value_col=BLUE;}
   else if(a.kind==XZ_UI_SERVER_ADDRESS){value=m->server_address&&*m->server_address?m->server_address:"NOT SET";value_col=DIM;}
   else if((a.kind==XZ_UI_CONNECTION_ENABLE||a.kind==XZ_UI_DISCOVERY)&&!m->connection.ready){value="UNAVAILABLE";value_col=DIM;}
   row(c,&a,st,available,selected,value,value_col,blocked);
   break;}
  }
 }
 if(u->page==XZ_UI_STEMS||u->page==XZ_UI_XPAD)deck_header(c,u,d,buf,sizeof(buf));
 if(u->page==XZ_UI_XPAD){
  snprintf(buf,sizeof(buf),"BEAT LOOP %s  /  %+.1f KEY",xz_ui_lengths[d->loop_index>=0&&d->loop_index<6?d->loop_index:0],(double)d->pitch);
  text(c,X0,122,480,12,buf,1,ALIGN_L,DIM);
 }else if(u->page==XZ_UI_CONNECTION)xz_ui_page_details(s);
}
static void render_inline(const struct xz_ui_scene *s){
 xz_rect(s->c,0,0,s->c.width,s->c.height,BLACK);
 xz_ui_stem_rows(s);
}
/* Black with a white rule, solid blue while on. */
static struct xz_top_face top_button(struct xz_canvas c,int x,int w,const char *label,int lit){
 int h=XZ_TOP_BAR_H;xz_rect(c,x,0,w,h,BLACK);
 if(lit)xz_rect(c,x+1,1,w-2,h-2,BLUE);else xz_border(c,x+1,1,w-2,h-2,WHITE);
 return xz_top_bar_label(c,x,w,label,lit?BLUE:BLACK,WHITE);
}
const struct xz_ui_skin xz_skin_flagship={xz_ui_place,render,render_inline,top_button};
