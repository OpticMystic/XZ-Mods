/* SPDX-License-Identifier: MIT */
#include "classic_mixer.h"
#include "ui_skin.h"
#include "extra_lane.h"
#include "deck_colour.h"
#include "../shell/shell_font.h"
#include <stdio.h>
#include <string.h>
#define N(f) XZ_SHELL_KNOWN(XZ_SHELL_F_##f)
#define E(f) XZ_EXTRA_KNOWN(XZ_EXTRA_F_##f)
#define WHITE 0xffffff
#define GRAY 0xaab3bd
#define RED 0xf34232
#define ORANGE 0xff7800
struct panel {
 int loaded,load_known,remain,time_known; int32_t elapsed,total,bpm,tempo;
 int total_known,bpm_known,tempo_known,range,range_known,mt,mt_known,quant,quant_known,acue,acue_known;
 uint32_t master_scope; int master,master_known,source,source_known,loop,slip,rev,call;
 int track_no,track_known;char sync[32];
 const char *title,*key; int title_known,key_known;
 const struct xz_extra_wave *wave;
};
static struct panel project(const struct xz_classic_mixer_view *v,int id){
 struct panel p={0};
 if(id<2){if(!v->native)return p;const struct xz_shell_deck *d=&v->native[id];
#define NK(f) ((d->known&N(f))!=0)
 p.load_known=NK(LOADED);p.loaded=p.load_known&&d->loaded;p.title_known=NK(TITLE);p.title=d->title;
 p.remain=NK(TIME_MODE)&&d->time_mode==XZ_SHELL_TIME_REMAIN;p.time_known=NK(TIME_MODE)&&NK(ELAPSED);p.elapsed=d->elapsed_ms;p.total=d->total_ms;p.total_known=NK(TOTAL);
 p.bpm_known=NK(BPM);p.bpm=d->bpm_x100;p.tempo_known=NK(TEMPO);p.tempo=d->tempo_x100;p.range_known=NK(TEMPO_RANGE);p.range=d->tempo_range;
 p.mt_known=NK(MASTER_TEMPO);p.mt=d->master_tempo;p.quant_known=NK(QUANTIZE);p.quant=d->quantize_on;p.acue_known=NK(AUTO_CUE);p.acue=d->auto_cue;
 snprintf(p.sync,sizeof p.sync,"%s",NK(SYNC_STATUS)?(d->sync_status==XZ_SHELL_SYNC_LOCKED?"SYNC":d->sync_status==XZ_SHELL_SYNC_TEMPO_ONLY?"BPM SYNC":d->sync_status==XZ_SHELL_SYNC_WAITING?"WAIT":""):NK(SYNC)&&d->sync?"SYNC":"");
 p.track_no=d->track_no;p.track_known=NK(TRACK_NO);p.master_known=NK(MASTER);p.master=p.master_known&&d->master;p.source_known=NK(SOURCE);p.source=d->source;p.key_known=NK(KEY);p.key=d->key;p.loop=NK(LOOP)&&d->loop.active;p.slip=NK(SLIP)&&d->slip;
#undef NK
 }else{const struct xz_extra_deck *d=&v->extra->deck[id-2];
#define EK(f) ((d->known&E(f))!=0)
 p.load_known=1;p.loaded=d->loaded;p.title_known=EK(TITLE);p.title=d->title;p.remain=d->remain;p.time_known=EK(TIME_MODE)&&xz_extra_lane_position_ms(d,v->now_ms,&p.elapsed);p.total=d->total_ms;p.total_known=EK(TOTAL);
 p.bpm_known=EK(BPM);p.bpm=d->bpm_x100;p.tempo_known=EK(TEMPO);p.tempo=d->tempo_x100;p.range_known=EK(TEMPO_RANGE);p.range=d->tempo_range;p.mt_known=EK(MASTER_TEMPO);p.mt=d->master_tempo;p.quant_known=EK(QUANTIZE);p.quant=d->quantize;
 /* These software modes are value-owned snapshot fields, not native guesses. */
 p.acue_known=EK(TIME_MODE);p.acue=d->auto_cue;p.master_known=EK(MASTER);p.master=p.master_known&&d->master;p.master_scope=d->master_scope;p.source_known=EK(SOURCE);p.source=d->source;p.key_known=EK(KEY);p.key=d->key;p.loop=EK(LOOP)&&d->loop.active;p.slip=d->slip;p.rev=d->reverse;p.call=d->pad_mode==XZ_EXTRA_PAD_CALL;
 if(!p.master)xz_extra_sync_chip(d,p.sync,sizeof p.sync);
 if(EK(WAVE))p.wave=&d->wave;
#undef EK
 }
 return p;
}
static void text(struct xz_canvas c,int x,int y,int w,const char *s,int scale,uint32_t color){if(!s)return;c.p+=x;c.width=w;xz_text(c,0,y,s,scale,w/(6*scale),color);}
static void lamp(struct xz_canvas c,int x,int y,int w,const char *s,int known,int on,uint32_t color){text(c,x,y,w,known?s:"--",1,known&&on?color:0x59616a);}
static void overview(struct xz_canvas c,const struct xz_classic_mixer_view *v,int id,struct panel p){
 /* The native counter draws END over its overview when the transport finishes.
    Own the entire replacement overview in that state, so no resampled stock
    glyph fragment survives in a channel. Keep a complete, centered END label. */
 if(id<2&&p.loaded&&p.time_known&&p.total_known&&p.total>0&&p.elapsed>=p.total){
  text(c,85,140,40,"END",1,RED);return;
 }
 const uint16_t *src=id<2&&p.loaded?v->native_overview[id]:NULL;
 if(src){/* Approximate native colours and markers by choosing
              the more saturated pixel in each 1..2-column bin, without recolouring. */
  for(int x=0;x<188;x++){int a=x*300/188,b=(x+1)*300/188;for(int y=0;y<34;y++){uint16_t chosen=src[y*300+a];unsigned best=0;for(int j=a;j<b;j++){uint16_t q=src[y*300+j];unsigned r=(q>>11)&31,g=((q>>5)&63)/2,bl=q&31;unsigned hi=r>g?r:g;if(bl>hi)hi=bl;unsigned lo=r<g?r:g;if(bl<lo)lo=bl;unsigned score=(hi-lo)*64+hi;if(score>=best){best=score;chosen=q;}}c.p[(size_t)(129+y)*c.stride+6+x]=chosen;}}
  return;
 }
 if(p.wave&&p.loaded&&p.wave->columns&&p.wave->count){
  const struct xz_extra_wave *w=p.wave;
  for(unsigned x=0;x<188;x++){uint32_t a=(uint32_t)((uint64_t)x*w->count/188),b=(uint32_t)((uint64_t)(x+1)*w->count/188);if(b<=a)b=a+1;unsigned samples=b-a;if(samples>8)samples=8;
   /* Rank at most eight evenly spaced samples, then use the shared 3-band palette. */
   struct xz_extra_column sample[8];
   for(unsigned i=0;i<samples;i++)sample[i]=w->columns[a+(uint32_t)((uint64_t)i*(b-a)/samples)];
   struct xz_layered_data data={(const uint8_t *)sample,samples,w->normalization?w->normalization:765,XZ_LAYERED_THREE_BAND,{0,0,0}};
   struct xz_layered_col col;xz_layered_heights(&data,0,samples,32,&col);
   for(unsigned y=0;y<32;y++){int color=xz_layered_pixel(&data,&col,31-y);if(color>=0)c.p[(size_t)(130+y)*c.stride+6+x]=(uint16_t)color;}

  }
  if(p.total_known&&p.total>0){
   const struct xz_extra_deck *d=&v->extra->deck[id-2];
   if(d->known&E(HOT_CUES))for(int i=0;i<XZ_EXTRA_HOT_CUES;i++)if(d->hot_cue[i].set){
    int64_t ms=d->hot_cue[i].ms;if(ms<0||ms>p.total)continue;
    int x=6+(int)(ms*187/p.total);xz_rect(c,x,129,1,7,xz_extra_cue_color(&d->hot_cue[i]));
   }
   if((d->known&E(LOOP))&&(d->loop.active||d->loop.stored)&&d->loop.out_ms>d->loop.in_ms){
    int64_t a=d->loop.in_ms,b=d->loop.out_ms;if(a<0)a=0;if(b>p.total)b=p.total;
    if(a<=b&&a<=p.total&&b>=0)xz_rect(c,6+(int)(a*187/p.total),129,(int)((b-a)*187/p.total)+1,2,d->loop.active?ORANGE:GRAY);
   }
   if((d->known&E(CUE))&&d->cue_ms>=0&&d->cue_ms<=p.total){
    int x=6+(int)((int64_t)d->cue_ms*187/p.total);xz_rect(c,x,158,1,5,ORANGE);
   }
  }
  if(p.total_known&&p.total>0&&p.time_known){int64_t e=p.elapsed;if(e<0)e=0;if(e>p.total)e=p.total;int x=6+(int)(e*187/p.total);xz_rect(c,x,129,1,34,WHITE);}
 }else text(c,6,140,188,"--",1,GRAY);
}
/* Stock shell's antialiased 28px digits, clipped to each 188px value area. */
static void digits(struct xz_canvas c,int x,int y,const char *s,int big,int end,uint32_t color){
 const char *charset=XZ_SHELL_FONT_CHARSET;
 for(;*s;s++){const char *at=strchr(charset,*s);if(!at)continue;
  const struct xz_shell_glyph *g=(big?xz_shell_font_big:xz_shell_font_bpm)+(at-charset);
  if(x+g->advance>end)break;
  for(unsigned row=0;row<g->height;row++)for(unsigned col=0;col<g->width;col++)xz_plot(c,x+g->x+(int)col,y+g->y+(int)row,color,xz_shell_font_coverage[g->offset+row*g->width+col]);
  x+=g->advance;
 }
}
/* DECK 3 or 4 while the loopback route is down (design 6.3): a dim title band with the deck's number box
   dimmed and NOT CONNECTED, one plain line, and an empty overview where the deck's would be. */
static void not_connected(struct xz_canvas c,int id){
 char b[32];
 xz_rect(c,0,0,200,169,0x030506);xz_rect(c,199,0,1,169,0x52606c);
 xz_rect(c,0,0,199,22,0x161c26);xz_rect(c,0,22,199,1,0x2a3038);
 xz_deck_box(c,3,2,20,18,id,0,0x161c26,2,0);
 text(c,30,6,166,"NOT CONNECTED",1,0xffa82e);
 snprintf(b,sizeof b,"DECK %d WAITS FOR THE",id+1);text(c,8,39,186,b,1,0x8a929a);
 text(c,8,55,186,"LOOPBACK CABLE.",1,0x8a929a);
 xz_rect(c,6,129,188,34,0x0a0c10);xz_rect(c,6,161,188,2,0x2a3038);xz_rect(c,6,165,188,1,0x58616a);
}
/* ---- 2 DECKS: a side's stock deck panel for DECK 3 or 4 (classic_mixer.h) ----
   Measured on the retained stock capture (the DECK 2 panel, offsets from its left edge): title band rows
   312..341 #103062 with the note and title, body #080c08 from row 342; DECK and REMAIN / QUANTIZE / TEMPO
   labels on row 349; TRACK and its number at x 17; the A.HOTCUE / A.CUE boxes at x 68..124; the clock from
   x 133, rows 368..402; tempo at x 312; the BPM box x 313..392, rows 401..445; END under the clock; the
   whole-track overview x 10..309, rows 423..456 and the position bar under it. Panel-local y below is the
   screen y minus 309. The deck's number box takes the note's place; source and key take TRACK's. */
#define HALF_TITLE 0x103062
#define HALF_BODY 0x080c08
#define HALF_DIM 0x59616a
enum { HALF_BOX_X=8, HALF_BOX_Y=6, HALF_BOX_S=24 };
static void lamp_box(struct xz_canvas c,int x,int y,int w,int h,const char *s,int known,int on,uint32_t color){
 uint32_t ink=known&&on?color:HALF_DIM;
 xz_border(c,x,y,w,h,ink);
 int tw=xz_text_width(c,s,1,99);xz_text(c,x+(w-tw+1)/2,y+(h-9+1)/2,s,1,99,ink);
}
/* The overview of the software deck over 300 columns, its cue marks, the position line and the bar under it. */
static void half_overview(struct xz_canvas c,const struct xz_extra_deck *d,struct panel p){
 enum { X=10, Y=114, W=300, H=34, BAR=151 };
 if(p.loaded&&p.wave&&p.wave->columns&&p.wave->count){
  const struct xz_extra_wave *w=p.wave;
  for(unsigned x=0;x<W;x++){uint32_t a=(uint32_t)((uint64_t)x*w->count/W),b=(uint32_t)((uint64_t)(x+1)*w->count/W);if(b<=a)b=a+1;unsigned samples=b-a;if(samples>8)samples=8;
   struct xz_extra_column sample[8];
   for(unsigned i=0;i<samples;i++)sample[i]=w->columns[a+(uint32_t)((uint64_t)i*(b-a)/samples)];
   struct xz_layered_data data={(const uint8_t *)sample,samples,w->normalization?w->normalization:765,XZ_LAYERED_THREE_BAND,{0,0,0}};
   struct xz_layered_col col;xz_layered_heights(&data,0,samples,H,&col);
   for(unsigned y=0;y<H;y++){int color=xz_layered_pixel(&data,&col,H-1-y);if(color>=0)c.p[(size_t)(Y+y)*c.stride+X+x]=(uint16_t)color;}
  }
 }else{text(c,X,Y+12,W,p.loaded?"NO WAVEFORM YET":"NO TRACK",1,GRAY);}
 xz_rect(c,X,BAR,W,3,0x5a5a5a);
 if(!p.loaded||!p.total_known||p.total<=0)return;
 if(d->known&E(HOT_CUES))for(int i=0;i<XZ_EXTRA_HOT_CUES;i++)if(d->hot_cue[i].set){
  int64_t ms=d->hot_cue[i].ms;if(ms<0||ms>p.total)continue;
  xz_rect(c,X+(int)(ms*(W-1)/p.total),Y-2,1,6,xz_extra_cue_color(&d->hot_cue[i]));
 }
 if((d->known&E(LOOP))&&(d->loop.active||d->loop.stored)&&d->loop.out_ms>d->loop.in_ms){
  int64_t a=d->loop.in_ms,b=d->loop.out_ms;if(a<0)a=0;if(b>p.total)b=p.total;
  if(a<=b)xz_rect(c,X+(int)(a*(W-1)/p.total),Y-2,(int)((b-a)*(W-1)/p.total)+1,2,d->loop.active?ORANGE:GRAY);
 }
 if((d->known&E(CUE))&&d->cue_ms>=0&&d->cue_ms<=p.total){
  int x=X+(int)((int64_t)d->cue_ms*(W-1)/p.total);for(int r=0;r<4;r++)xz_rect(c,x-r,BAR+3+r,2*r+1,1,ORANGE);
 }
 for(int64_t m=60000;m<p.total;m+=60000)xz_rect(c,X+(int)(m*(W-1)/p.total),BAR+4,1,4,0x8a8a8a);
 if(p.time_known){
  int64_t e=p.elapsed;if(e<0)e=0;if(e>p.total)e=p.total;int x=X+(int)(e*(W-1)/p.total);
  xz_rect(c,X,BAR,x-X+1,3,WHITE);xz_rect(c,x,Y,1,H,WHITE);
 }
}
int xz_classic_mixer_half_draw(uint16_t *pixels,size_t stride,size_t count,const struct xz_classic_mixer_view *v,int deck){
 if(!pixels||!v||!v->extra||(deck!=2&&deck!=3)||stride<800||stride>SIZE_MAX/480||count<stride*480)return 0;
 int side=deck-2,x0=side?3:0,x1=side?400:398;char b[64];
 struct xz_canvas c={pixels+(size_t)XZ_CLASSIC_HALF_PANEL_Y*stride+xz_classic_half_panel_x(side),stride,400,480-XZ_CLASSIC_HALF_PANEL_Y,v->theme};
 struct panel p=project(v,deck);const struct xz_extra_deck *d=&v->extra->deck[side];
 /* The frame: black above the band, a navy edge toward the other panel, title band and body. */
 xz_rect(c,0,0,400,171,0x000000);
 xz_rect(c,side?2:398,3,1,168,HALF_TITLE);
 xz_rect(c,x0,3,x1-x0,30,HALF_TITLE);xz_rect(c,x0,33,x1-x0,138,HALF_BODY);
 xz_deck_box(c,HALF_BOX_X+x0,HALF_BOX_Y,HALF_BOX_S,HALF_BOX_S,deck,1,HALF_TITLE,2,0);
 const char *title=p.load_known&&!p.loaded?"NO TRACK":p.title_known&&p.title&&p.title[0]?p.title:"--";
 text(c,x0+40,11,x1-x0-48,title,2,p.loaded?WHITE:GRAY);
 /* Row 349: DECK n, the time mode, QUANTIZE, TEMPO and its range. */
 snprintf(b,sizeof b,"DECK %d",deck+1);text(c,17,40,60,b,1,WHITE);
 text(c,137,40,56,p.time_known?(p.remain?"REMAIN":"TIME"):"--",1,p.time_known?WHITE:GRAY);
 lamp(c,197,40,60,"QUANTIZE",p.quant_known,p.quant,RED);
 text(c,315,40,40,"TEMPO",1,WHITE);
 if(p.range_known&&p.range==100)text(c,358,40,36,"WIDE",1,RED);
 else if(p.range_known){
  xz_rect(c,358,43,5,1,GRAY);xz_rect(c,360,41,1,5,GRAY);xz_rect(c,358,47,5,1,GRAY);
  snprintf(b,sizeof b,"%d",p.range);text(c,365,40,30,b,1,GRAY);
 }else text(c,358,40,36,"--",1,GRAY);
 /* Source and key where the stock panel has TRACK and its number; AUTO CUE and MASTER TEMPO as its boxes. */
 const char *source=p.source_known?(p.source==1?"USB1":p.source==2?"USB2":"--"):"--";
 text(c,17,62,46,source,1,GRAY);text(c,17,75,48,p.key_known&&p.key&&p.key[0]?p.key:"--",2,WHITE);
 lamp_box(c,68,60,57,13,"A.CUE",p.acue_known,p.acue,WHITE);
 lamp_box(c,68,79,57,13,"MT",p.mt_known,p.mt,RED);
 /* The clock in the deck's TIME MODE. */
 int ended=p.loaded&&p.time_known&&p.total_known&&p.total>0&&p.elapsed>=p.total;
 if(p.loaded&&p.time_known&&(!p.remain||p.total_known)){int64_t t=p.remain?(int64_t)p.total-p.elapsed:p.elapsed;if(t<0)t=0;if(t>5999999)t=5999999;snprintf(b,sizeof b,"%02d:%02d.%02d",(int)(t/60000),(int)(t/1000%60),(int)(t/10%100));}else strcpy(b,"--:--.--");
 digits(c,137,63,b,1,305,p.loaded&&p.time_known?WHITE:GRAY);
 /* Under the clock: END as stock, else what SYNC follows, what the pads do and the running modes. */
 if(ended)text(c,137,101,80,"END",2,RED);
 else if(p.loaded){
  int x=137;
  if(p.master_known&&p.master){const char *m=p.master_scope==XZ_EXTRA_MASTER_SCOPE_SOFTWARE?"3/4 MASTER":"MASTER";text(c,x,101,80,m,1,ORANGE);x+=xz_text_width(c,m,1,99)+10;}
  else if(p.sync[0]){text(c,x,101,80,p.sync,1,0x73b6ff);x+=xz_text_width(c,p.sync,1,99)+10;}
  char pad[48];xz_extra_pad_text(d,pad,sizeof pad);
  if(pad[0]&&x<280){text(c,x,101,300-x,pad,1,GRAY);x+=xz_text_width(c,pad,1,99)+10;}
  const char *mode=p.rev?"REV":p.slip?"SLIP":p.loop?"LOOP":p.call?"CALL":"";
  if(mode[0]&&x<280)text(c,x,101,300-x,mode,1,WHITE);
 }
 /* Tempo and the BPM box; MASTER fills its foot orange, as stock. */
 if(p.tempo_known&&p.loaded){int64_t t=p.tempo;snprintf(b,sizeof b,"%c%lld.%02lld%%",t<0?'-':'+',(long long)(t<0?-t:t)/100,(long long)(t<0?-t:t)%100);}else strcpy(b,"--.--%");
 text(c,312,65,86,b,2,p.tempo_known&&p.loaded?WHITE:GRAY);
 int master=p.master_known&&p.master;
 xz_border(c,313,92,80,45,master?ORANGE:WHITE);
 if(p.loaded&&p.bpm_known&&p.bpm>0)snprintf(b,sizeof b,"%d.%01d",p.bpm/100,p.bpm/10%10);else strcpy(b,"---.-");
 digits(c,318,96,b,0,390,master?ORANGE:WHITE);
 if(master){xz_rect(c,314,124,78,12,ORANGE);text(c,336,126,56,"MASTER",1,0x1a1206);}
 else text(c,368,126,26,"BPM",1,WHITE);
 half_overview(c,d,p);
 return 1;
}
void xz_classic_mixer_half_identity(uint16_t *pixels,size_t stride,size_t count,int deck){
 if(!pixels||(deck!=2&&deck!=3)||stride<800||stride>SIZE_MAX/480||count<stride*480)return;
 int side=deck-2;
 struct xz_canvas c={pixels+(size_t)XZ_CLASSIC_HALF_PANEL_Y*stride+xz_classic_half_panel_x(side),stride,400,480-XZ_CLASSIC_HALF_PANEL_Y,0};
 xz_deck_box(c,HALF_BOX_X+(side?3:0),HALF_BOX_Y,HALF_BOX_S,HALF_BOX_S,deck,1,HALF_TITLE,2,0);
}
int xz_classic_mixer_draw(uint16_t *pixels,size_t stride,size_t count,const struct xz_classic_mixer_view *v){
 if(!pixels||!v||v->layout==XZ_LAYOUT_TWO||stride<800||stride>SIZE_MAX/480||count<stride*480)return 0;
 int live=!v->placeholders&&v->extra&&v->extra->route_up;
 if(!live&&!v->placeholders)return 0;
 for(int s=0;s<4;s++){
  int id=xz_classic_mixer_order()[s];struct xz_canvas c={pixels+311*stride+s*200,stride,200,169,v->theme};char b[96];
  if(id>=2&&!live){not_connected(c,id);continue;}
  struct panel p=project(v,id);
  xz_rect(c,0,0,200,169,0x030506);xz_rect(c,0,0,199,22,0x123461);xz_rect(c,199,0,1,169,0x52606c);xz_rect(c,0,22,199,1,0x536376);
  /* The deck's number in its badge (deck_colour.h), matching its tag beside the lanes. */
  xz_deck_box(c,3,2,20,18,id,1,0x123461,2,0);
  const char *title=p.load_known&&!p.loaded?"NO TRACK":p.title_known&&p.title?p.title:"--";text(c,29,1,166,title,2,WHITE);
  snprintf(b,sizeof b,"DECK %d",id+1);text(c,6,25,54,b,1,WHITE);
  if(p.master_known&&p.master)text(c,72,25,120,id>=2&&p.master_scope==XZ_EXTRA_MASTER_SCOPE_SOFTWARE?"3/4 MASTER":"MASTER",1,ORANGE);
  const char *source=p.source_known?(p.source==1?"USB1":p.source==2?"USB2":p.source==3?"LINK":p.source==4?"PC":"--"):"--";
  text(c,6,39,45,source,1,GRAY);text(c,54,39,42,p.key_known&&p.key?p.key:"--",1,WHITE);text(c,103,39,91,p.sync,1,0x73b6ff);
  text(c,6,53,53,p.time_known?(p.remain?"REMAIN":"TIME"):"--",1,GRAY);
  lamp(c,60,53,46,"A.CUE",p.acue_known,p.acue,WHITE);lamp(c,108,53,22,"MT",p.mt_known,p.mt,RED);lamp(c,132,53,62,"QUANTIZE",p.quant_known,p.quant,RED);
  if(p.loaded&&p.time_known&&(!p.remain||p.total_known)){int64_t t=p.remain?(int64_t)p.total-p.elapsed:p.elapsed;if(t<0)t=0;if(t>5999999)t=5999999;snprintf(b,sizeof b,"%02d:%02d.%02d",(int)(t/60000),(int)(t/1000%60),(int)(t/10%100));}else strcpy(b,"--:--.--");
  digits(c,6,67,b,1,194,WHITE);
  if(p.loaded&&p.bpm_known&&p.bpm>0)snprintf(b,sizeof b,"%d.%01d",p.bpm/100,p.bpm/10%10);else strcpy(b,"---.-");digits(c,6,101,b,0,83,ORANGE);text(c,84,114,25,"BPM",1,ORANGE);
  if(p.tempo_known&&p.loaded){int64_t t=p.tempo;snprintf(b,sizeof b,"%c%lld.%02lld%%",t<0?'-':'+',(long long)(t<0?-t:t)/100,(long long)(t<0?-t:t)%100);}else strcpy(b,"--.--%");text(c,116,100,78,b,1,WHITE);
  if(p.range_known&&p.range!=100){
   /* Pixel themes lack U+00B1. Draw the same five-column plus/minus in every theme. */
   xz_rect(c,116,116,5,1,GRAY);xz_rect(c,118,114,1,5,GRAY);xz_rect(c,116,120,5,1,GRAY);
   snprintf(b,sizeof b,"%d",p.range);text(c,123,114,33,b,1,GRAY);
  }else text(c,116,114,40,p.range_known?"WIDE":"--",1,p.range_known?RED:GRAY);
  const char *mode=p.rev?"REV":p.slip?"SLIP":p.loop?"LOOP":p.call?"CALL":"";
  text(c,158,114,36,mode,1,WHITE);
  if(id>=2&&p.loaded&&p.title_known&&!p.master){char pad[64];xz_extra_pad_text(&v->extra->deck[id-2],pad,sizeof pad);text(c,72,25,122,pad,1,GRAY);}
  overview(c,v,id,p);xz_rect(c,6,165,188,1,0x58616a);
 }
 return 1;
}
