/* SPDX-License-Identifier: MIT */
#include "extra_detail.h"
#include "extra_lane.h"
#include "layered_wave.h"
#include "ui_skin.h"
#include "deck_colour.h"
#include <stdio.h>
#include <string.h>

#define K(f) XZ_EXTRA_KNOWN(XZ_EXTRA_F_##f)
#define W XZ_EXTRA_DETAIL_W
#define H XZ_EXTRA_DETAIL_H
#define RIGHT (W-6)
/* The plate's palette (extra_lane.c): white values, grey labels, red QUANTIZE/MT/WIDE, orange MASTER,
   blue SYNC, amber for waiting, the pale blue of a lit deck tag. */
#define BG 0x0d1115
#define EDGE 0x3a4652
#define RULE 0x222a32
#define INK 0xffffff
#define LABEL 0xb4bcc4
#define DIM 0x6a747e
#define BLANK 0x9aa2aa
#define SOFT 0x8a949e
#define RED 0xe8402c
#define MASTER_ORANGE 0xff6a00
#define SYNC_BLUE 0x2f7ff0
#define SYNC_INK 0x6fb2ff
#define WAIT_AMBER 0xffc070
#define LOOP_ORANGE 0xff9a30
#define SLIP_AMBER 0xffbd59
#define ADJUST_YELLOW 0xffe600
#define DARK_INK 0x1a1206
#define LIT 0x9cc8ff
#define LIT_INK 0x0a1626
#define KEY_FILL 0x1c2632
#define KEY_EDGE 0x8a96a2
/* Text rows, top to bottom (the touch boxes are extra_detail.h). Rows 0..5 right of column 150 sit under
   the MODS header strip, so nothing is drawn there. */
enum { ROW_HEAD=7, ROW_SIDE=32, ROW_RULE=63, ROW_TITLE=67, ROW_ARTIST=84, ROW_CLOCK=97, ROW_TEMPO=114,
       ROW_SYNC=129, ROW_SYNC2=146, ROW_PADS=158, ROW_GRID=169, ROW_MODES=190, ROW_LOOP=205, ROW_STATUS=246 };

const char *xz_extra_detail_notice_text(int notice,unsigned number,char *out,size_t size){
 if(!out||!size)return "";
 switch(notice){
 case XZ_EXTRA_DETAIL_NOTICE_NEEDLE_PLAYING:snprintf(out,size,"NEEDLE LOCK: PAUSE DECK %u TO SEARCH",number);break;
 case XZ_EXTRA_DETAIL_NOTICE_NEEDLE_HELD:snprintf(out,size,"NEEDLE LOCK: DECK %u IS COASTING",number);break;
 case XZ_EXTRA_DETAIL_NOTICE_NO_TRACK:snprintf(out,size,"LOAD A TRACK ON DECK %u FIRST",number);break;
 case XZ_EXTRA_DETAIL_NOTICE_NO_LENGTH:snprintf(out,size,"DECK %u IS STILL READING THE TRACK",number);break;
 case XZ_EXTRA_DETAIL_NOTICE_REFUSED:snprintf(out,size,"DECK %u DID NOT TAKE THE SEARCH",number);break;
 case XZ_EXTRA_DETAIL_NOTICE_SELECT_FAILED:snprintf(out,size,"DECK SELECT IS NOT AVAILABLE NOW");break;
 case XZ_EXTRA_DETAIL_NOTICE_UNAVAILABLE:snprintf(out,size,"DECK 3/4 PLAYERS ARE NOT RUNNING");break;
 default:out[0]=0;break;
 }
 return out;
}

static int chip(struct xz_canvas c,int x,int y,int h,const char *text,uint32_t fill,uint32_t edge,uint32_t ink){
 int w=xz_text_width(c,text,1,30)+8;
 if(fill)xz_rect(c,x,y,w,h,fill);
 if(edge)xz_border(c,x,y,w,h,edge);
 xz_text(c,x+4,y+(h-9)/2,text,1,30,ink);
 return w;
}
/* A mode lamp: filled in its colour while on, outlined dim while off. */
static int lamp(struct xz_canvas c,int x,int y,const char *text,int on,uint32_t color){
 return on?chip(c,x,y,12,text,color,0,color==INK||color==SLIP_AMBER?DARK_INK:INK):chip(c,x,y,12,text,0,DIM,DIM);
}
/* Text clipped to the pixels left before right; returns the width drawn. */
static int fit(struct xz_canvas c,int x,int y,int right,const char *text,int scale,uint32_t color){
 int limit=(right-x)/(6*scale);if(limit<=0)return 0;
 xz_text(c,x,y,text,scale,limit,color);
 return xz_text_width(c,text,scale,limit);
}
static void key(struct xz_canvas c,struct xz_extra_detail_box b,const char *text,int lit){
 xz_rect(c,b.x,b.y,b.w,b.h,lit?LIT:KEY_FILL);xz_border(c,b.x,b.y,b.w,b.h,lit?LIT:KEY_EDGE);
 int w=xz_text_width(c,text,1,16);xz_text(c,b.x+(b.w-w)/2,b.y+(b.h-9)/2,text,1,16,lit?LIT_INK:INK);
}
static void clock_text(char *out,size_t n,int known,int32_t ms,int remain){
 if(!known){snprintf(out,n,"-:--.-");return;}
 if(ms<0)ms=0;
 snprintf(out,n,"%s%d:%02d.%d",remain?"-":"",(int)(ms/60000),(int)(ms/1000%60),(int)(ms/100%10));
}
static void ms_text(char *out,size_t n,int32_t ms){
 if(ms<0)ms=0;
 snprintf(out,n,"%d:%02d.%d",(int)(ms/60000),(int)(ms/1000%60),(int)(ms/100%10));
}

/* The deck's number box (deck_colour.h) and DECK, whether it plays, CLOSE. */
enum { HEAD_BOX=22 };
static void header(struct xz_canvas c,unsigned number,const struct xz_extra_deck *d,int lit){
 int x=6+HEAD_BOX+1,w=xz_text_width(c,"DECK",2,8)+14;
 xz_deck_box(c,6,ROW_HEAD,HEAD_BOX,22,(int)number-1,1,0,2,0);
 if(lit)xz_rect(c,x,ROW_HEAD,w,22,LIT);
 else{xz_rect(c,x,ROW_HEAD,w,22,KEY_FILL);xz_border(c,x,ROW_HEAD,w,22,0x55606c);}
 xz_text(c,x+7,ROW_HEAD+4,"DECK",2,8,lit?LIT_INK:LABEL);
 int playing=(d->known&K(PLAYING))&&d->playing;
 const char *state=!d->loaded?"NO TRACK":playing?"PLAYING":d->held_playing?(d->platter_contact?"HELD":"COAST"):(d->known&K(PLAYING))?"PAUSED":"";
 xz_text(c,x+w+8,ROW_HEAD+7,state,1,10,playing?INK:d->held_playing?WAIT_AMBER:LABEL);
 key(c,xz_extra_detail_box(XZ_EXTRA_DETAIL_CLOSE),"CLOSE",0);
}
/* Which deck this side's controls drive: the DECK SELECT of SHIFT pressed twice, by touch. */
static void controls(struct xz_canvas c,int deck,int extra_on,int route_up){
 xz_text(c,6,ROW_SIDE+4,deck?"RIGHT SIDE":"LEFT SIDE",1,14,LABEL);
 xz_text(c,6,ROW_SIDE+16,"CONTROLS",1,14,LABEL);
 char text[8];
 snprintf(text,sizeof text,"DECK %d",deck?2:1);key(c,xz_extra_detail_box(XZ_EXTRA_DETAIL_NATIVE_SIDE),text,route_up&&!extra_on);
 snprintf(text,sizeof text,"DECK %d",3+deck);key(c,xz_extra_detail_box(XZ_EXTRA_DETAIL_EXTRA_SIDE),text,route_up&&extra_on);
 xz_rect(c,6,ROW_RULE,W-12,1,RULE);
}
/* Title; artist, then source and key at the right. */
static void track(struct xz_canvas c,const struct xz_extra_deck *d){
 int titled=(d->known&K(TITLE))&&d->title[0];
 fit(c,6,ROW_TITLE,RIGHT,titled?d->title:"--",2,titled?INK:BLANK);
 int right=RIGHT;
 char k[sizeof d->key];
 if((d->known&K(KEY))&&d->key[0]){memcpy(k,d->key,sizeof k);k[sizeof k-1]=0;}else snprintf(k,sizeof k,"--");
 int w=xz_text_width(c,k,1,8);xz_text(c,right-w,ROW_ARTIST,k,1,8,k[0]=='-'?BLANK:INK);right-=w+4;
 w=xz_text_width(c,"KEY",1,4);xz_text(c,right-w,ROW_ARTIST,"KEY",1,4,LABEL);right-=w+10;
 if((d->known&K(SOURCE))&&(d->source==1||d->source==2)){
  const char *usb=d->source==1?"USB1":"USB2";w=xz_text_width(c,usb,1,4);
  xz_text(c,right-w,ROW_ARTIST,usb,1,4,LABEL);right-=w+10;
 }
 if((d->known&K(ARTIST))&&d->artist[0])fit(c,6,ROW_ARTIST,right,d->artist,1,SOFT);
}
/* Clock in the deck's TIME MODE, playing BPM; tempo with its range or TEMPO RESET, MASTER TEMPO, track BPM. */
static void clock(struct xz_canvas c,const struct xz_extra_deck *d,uint32_t now_ms){
 char text[32];int32_t pos=0;
 int placed=xz_extra_lane_position_ms(d,now_ms,&pos);
 int mode=(d->known&K(TIME_MODE))!=0,remain=!mode||d->remain;
 int timed=placed&&(!remain||((d->known&K(TOTAL))&&d->total_ms>0));
 clock_text(text,sizeof text,timed,remain?d->total_ms-pos:pos,remain);
 xz_text(c,6,ROW_CLOCK,text,2,10,timed?INK:BLANK);
 int x=6+xz_text_width(c,text,2,10)+6;
 if(mode)xz_text(c,x,ROW_CLOCK+5,d->remain?"REMAIN":"TIME",1,8,LABEL);
 int bpm=(d->known&K(BPM))&&d->bpm_x100>0;
 if(bpm)snprintf(text,sizeof text,"%d.%d",(int)(d->bpm_x100/100),(int)(d->bpm_x100%100/10));
 else snprintf(text,sizeof text,"---.-");
 int w=xz_text_width(c,text,2,6);
 xz_text(c,RIGHT-w,ROW_CLOCK,text,2,6,bpm?INK:BLANK);
 xz_text(c,RIGHT-w-6-xz_text_width(c,"BPM",1,4),ROW_CLOCK+5,"BPM",1,4,LABEL);
 x=6;
 x+=fit(c,x,ROW_TEMPO+2,RIGHT,"TEMPO",1,LABEL)+5;
 if(d->known&K(TEMPO)){
  int32_t t=d->tempo_x100,a=t<0?-t:t;
  snprintf(text,sizeof text,"%c%d.%02d%%",t<0?'-':t>0?'+':' ',(int)(a/100),(int)(a%100));
  x+=fit(c,x,ROW_TEMPO+2,RIGHT,text,1,INK)+6;
 }else x+=fit(c,x,ROW_TEMPO+2,RIGHT,"--.--%",1,BLANK)+6;
 if(d->tempo_reset)x+=chip(c,x,ROW_TEMPO,12,"TEMPO RESET",INK,0,DARK_INK)+4;
 else if(d->known&K(TEMPO_RANGE)){
  if(d->tempo_range==100)x+=chip(c,x,ROW_TEMPO,12,"WIDE",RED,0,INK)+4;
  else if(d->tempo_range==6||d->tempo_range==10||d->tempo_range==16){
   snprintf(text,sizeof text,"\xc2\xb1%d%%",d->tempo_range);x+=chip(c,x,ROW_TEMPO,12,text,0,DIM,LABEL)+4;
  }
 }
 if(d->known&K(MASTER_TEMPO))x+=chip(c,x,ROW_TEMPO,12,"MT",0,d->master_tempo?RED:DIM,d->master_tempo?RED:DIM)+4;
 if((d->known&K(BPM))&&d->track_bpm_x100>0){
  snprintf(text,sizeof text,"TRACK %d.%02d",(int)(d->track_bpm_x100/100),(int)(d->track_bpm_x100%100));
  w=xz_text_width(c,text,1,16);
  if(RIGHT-w>=x)xz_text(c,RIGHT-w,ROW_TEMPO+2,text,1,16,LABEL);
 }
}
/* What SYNC or MASTER means for this deck now, in words: up to two lines after the chip. */
static void sync_lines(const struct xz_extra_deck *d,char *a,size_t an,char *b,size_t bn){
 a[0]=b[0]=0;
 int n=d->sync.master_deck>=0&&d->sync.master_deck<XZ_FOUR_DECKS?d->sync.master_deck+1:0;
 switch(xz_extra_sync_class(d)){
 case XZ_EXTRA_SYNC_CLASS_NONE:snprintf(a,an,(d->known&K(SYNC))?"OFF":"NOT REPORTED");break;
 case XZ_EXTRA_SYNC_CLASS_MASTER:{
  uint32_t scope=(d->known&K(MASTER))&&d->master?d->master_scope:d->sync.master_scope;
  if(scope==XZ_EXTRA_MASTER_SCOPE_SOFTWARE){snprintf(a,an,"TEMPO MASTER OF DECK 3 AND 4");snprintf(b,bn,"DECK 1 AND 2 SYNC CANNOT FOLLOW IT");}
  else snprintf(a,an,"TEMPO MASTER");
  break;
 }
 case XZ_EXTRA_SYNC_CLASS_LOCKED:
  if(n)snprintf(a,an,"LOCKED TO DECK %d",n);else snprintf(a,an,"LOCKED TO THE MASTER");
  snprintf(b,bn,"BEAT AND TEMPO FOLLOW THE MASTER");break;
 case XZ_EXTRA_SYNC_CLASS_TEMPO:
  if(n)snprintf(a,an,"FOLLOWS DECK %d TEMPO ONLY",n);else snprintf(a,an,"FOLLOWS THE MASTER TEMPO ONLY");
  snprintf(b,bn,"%s",d->sync.reason==XZ_EXTRA_SYNC_TEMPO_FOLLOW?"QUANTIZE OFF: BEAT POSITION LEFT AS IS":
   "THE MASTER BEAT POSITION IS NOT AVAILABLE");break;
 case XZ_EXTRA_SYNC_CLASS_WAIT:
  switch(d->sync.reason){
  case XZ_EXTRA_SYNC_WAIT_MASTER:snprintf(a,an,"WAITING FOR A PLAYING MASTER");snprintf(b,bn,"PLAY THE MASTER OR PRESS MASTER ON A DECK");break;
  case XZ_EXTRA_SYNC_WAIT_ANCHOR:snprintf(a,an,"LOCKS ONCE THIS DECK PLAYS");break;
  case XZ_EXTRA_SYNC_ACQUIRING:snprintf(a,an,"FINDING THE MASTER BEAT");break;
  case XZ_EXTRA_SYNC_DISCONTINUITY:snprintf(a,an,"FINDING THE BEAT AGAIN");snprintf(b,bn,"AFTER A JUMP, PAUSE OR MASTER CHANGE");break;
  case XZ_EXTRA_SYNC_MANUAL:snprintf(a,an,"WAITS WHILE A DECK IS HELD");snprintf(b,bn,"OR WHILE THE MASTER IS NOT PLAYING");break;
  default:snprintf(a,an,"ON");break;
  }
  break;
 case XZ_EXTRA_SYNC_CLASS_BLOCKED:
  switch(d->sync.reason){
  case XZ_EXTRA_SYNC_NO_GRID:snprintf(a,an,"CANNOT LOCK THIS TRACK");snprintf(b,bn,"IT HAS NO BEAT GRID: ANALYSE IT FIRST");break;
  case XZ_EXTRA_SYNC_NO_SOURCE_BPM:snprintf(a,an,"NOTHING TO FOLLOW WITH");snprintf(b,bn,"THE TRACK HAS NO BPM: ANALYSE IT FIRST");break;
  case XZ_EXTRA_SYNC_OUT_OF_RANGE:snprintf(a,an,"MASTER TEMPO OUT OF RANGE");snprintf(b,bn,"PRESS TEMPO RANGE FOR A WIDER RANGE");break;
  case XZ_EXTRA_SYNC_LOOP_UNALIGNED:snprintf(a,an,"LOOP IS OFF THE BEAT GRID");snprintf(b,bn,"EXIT THE LOOP TO LOCK AGAIN");break;
  default:snprintf(a,an,"NOT SUPPORTED");snprintf(b,bn,"PRESS MASTER ON DECK 1, 2, 3 OR 4");break;
  }
  break;
 }
}
static void sync(struct xz_canvas c,const struct xz_extra_deck *d){
 char text[24],a[48],b[48];
 enum xz_extra_sync_class k=xz_extra_sync_class(d);
 int x=6;
 if(k==XZ_EXTRA_SYNC_CLASS_NONE)x+=chip(c,x,ROW_SYNC,14,"SYNC",0,DIM,DIM)+6;
 else{
  uint32_t fill=0,edge=0,ink=INK;
  if(k==XZ_EXTRA_SYNC_CLASS_MASTER)fill=MASTER_ORANGE;
  else if(k==XZ_EXTRA_SYNC_CLASS_LOCKED)fill=SYNC_BLUE;
  else if(k==XZ_EXTRA_SYNC_CLASS_TEMPO)edge=ink=SYNC_INK;
  else if(k==XZ_EXTRA_SYNC_CLASS_WAIT)edge=ink=WAIT_AMBER;
  else{fill=WAIT_AMBER;ink=DARK_INK;}
  x+=chip(c,x,ROW_SYNC,14,xz_extra_sync_chip(d,text,sizeof text),fill,edge,ink)+6;
 }
 sync_lines(d,a,sizeof a,b,sizeof b);
 fit(c,x,ROW_SYNC+3,RIGHT,a,1,k==XZ_EXTRA_SYNC_CLASS_NONE?LABEL:INK);
 if(b[0])fit(c,6,ROW_SYNC2,RIGHT,b,1,k==XZ_EXTRA_SYNC_CLASS_BLOCKED||k==XZ_EXTRA_SYNC_CLASS_WAIT?WAIT_AMBER:SOFT);
}
/* What each pad A..H does in the pad mode: hot cues in their colours (a bar under loop cues), loop lengths
   with the running one lit, jump sizes; CALL recalls saved pads without moving playback. */
static void pads(struct xz_canvas c,const struct xz_extra_deck *d,unsigned number){
 char text[48];
 int x=6+fit(c,6,ROW_PADS,RIGHT,"PADS",1,LABEL)+6;
 if(d->pad_mode==XZ_EXTRA_PAD_NONE){
  snprintf(text,sizeof text,"DECK %u PADS OFF",number);fit(c,x,ROW_PADS,RIGHT,text,1,LABEL);
 }else fit(c,x,ROW_PADS,RIGHT,xz_extra_pad_text(d,text,sizeof text),1,INK);
 int looping=d->pad_mode==XZ_EXTRA_PAD_BEAT_LOOP||d->pad_mode==XZ_EXTRA_PAD_SLIP_LOOP;
 for(int i=0;i<XZ_EXTRA_HOT_CUES;i++){
  int px=6+i*32,py=ROW_GRID;
  xz_extra_pad_label(d,i,text,sizeof text);
  if(d->pad_mode==XZ_EXTRA_PAD_HOT_CUE||d->pad_mode==XZ_EXTRA_PAD_CALL){
   const struct xz_extra_cue *q=&d->hot_cue[i];
   if((d->known&K(HOT_CUES))&&q->set){
    xz_rect(c,px,py,30,17,xz_extra_cue_color(q));
    if(q->end_ms>q->ms)xz_rect(c,px+3,py+13,24,2,0x000000);
    xz_text(c,px+(30-xz_text_width(c,text,1,4))/2,py+3,text,1,4,0x000000);
   }else{xz_border(c,px,py,30,17,DIM);xz_text(c,px+(30-xz_text_width(c,text,1,4))/2,py+3,text,1,4,DIM);}
   continue;
  }
  int active=looping&&d->active_pad==i;
  if(active)xz_rect(c,px,py,30,17,LOOP_ORANGE);else xz_border(c,px,py,30,17,text[0]?KEY_EDGE:RULE);
  if(text[0])xz_text(c,px+(30-xz_text_width(c,text,1,5))/2,py+4,text,1,5,active?DARK_INK:INK);
 }
}
/* QUANTIZE, AUTO CUE, VINYL, REV, SLIP: lit while on; SLIP filled while playback runs on underneath. */
static void modes(struct xz_canvas c,const struct xz_extra_deck *d){
 int x=6;
 if(d->known&K(QUANTIZE))x+=lamp(c,x,ROW_MODES,"QUANTIZE",d->quantize,RED)+4;
 if(d->known&K(TIME_MODE))x+=lamp(c,x,ROW_MODES,"AUTO CUE",d->auto_cue,INK)+4;
 if(d->known&K(VINYL))x+=lamp(c,x,ROW_MODES,"VINYL",d->vinyl,INK)+4;
 x+=lamp(c,x,ROW_MODES,"REV",d->reverse,0xff5a3c)+4;
 if(d->slip_active)lamp(c,x,ROW_MODES,"SLIP",1,SLIP_AMBER);
 else chip(c,x,ROW_MODES,12,"SLIP",0,d->slip?SLIP_AMBER:DIM,d->slip?SLIP_AMBER:DIM);
}
/* The loop's length in beats at the track BPM: whole beats, or 1/2..1/32, as the FLAGSHIP shell rounds it. */
static void loop_beats(char *out,size_t n,int32_t ms,int32_t bpm_x100){
 out[0]=0;
 if(ms<=0||bpm_x100<=0)return;
 int64_t milli=(int64_t)ms*bpm_x100/6000;
 if(milli>=750){int beats=(int)((milli+500)/1000);snprintf(out,n,"%d BEAT%s",beats,beats==1?"":"S");return;}
 int den=2;while(den<32&&milli*den<750)den*=2;
 snprintf(out,n,"1/%d BEAT",den);
}
static void loop_line(struct xz_canvas c,const struct xz_extra_deck *d){
 char text[64],beats[16],in[12],out[12];
 int x=6+fit(c,6,ROW_LOOP,RIGHT,"LOOP",1,LABEL)+6;
 if(!(d->known&K(LOOP))){fit(c,x,ROW_LOOP,RIGHT,"--",1,BLANK);return;}
 if(d->loop.out_ms<=d->loop.in_ms||(!d->loop.active&&!d->loop.stored)){fit(c,x,ROW_LOOP,RIGHT,"NONE",1,SOFT);return;}
 loop_beats(beats,sizeof beats,d->loop.out_ms-d->loop.in_ms,(d->known&K(BPM))?d->track_bpm_x100:0);
 ms_text(in,sizeof in,d->loop.in_ms);ms_text(out,sizeof out,d->loop.out_ms);
 const char *state=d->loop.active?(d->loop.edit==1?"IN ADJUST":d->loop.edit==2?"OUT ADJUST":"ON"):"RELOOP READY";
 x+=fit(c,x,ROW_LOOP,RIGHT,state,1,d->loop.active?(d->loop.edit?ADJUST_YELLOW:LOOP_ORANGE):SOFT)+6;
 if(beats[0])x+=fit(c,x,ROW_LOOP,RIGHT,beats,1,INK)+6;
 snprintf(text,sizeof text,"%s-%s",in,out);fit(c,x,ROW_LOOP,RIGHT,text,1,LABEL);
}
/* Whole track: the 3-band shape, loop, cues and the position; at most eight samples per column. */
static void overview(struct xz_canvas c,const struct xz_extra_deck *d,uint32_t now_ms){
 struct xz_extra_detail_box b=xz_extra_detail_box(XZ_EXTRA_DETAIL_OVERVIEW);
 xz_rect(c,b.x,b.y,b.w,b.h,0x07090b);xz_border(c,b.x-1,b.y-1,b.w+2,b.h+2,EDGE);
 int total=d->loaded&&(d->known&K(TOTAL))&&d->total_ms>0;
 if(!total){
  const char *text=d->loaded?"LENGTH NOT KNOWN YET":"NO TRACK";
  xz_text(c,b.x+(b.w-xz_text_width(c,text,1,30))/2,b.y+(b.h-9)/2,text,1,30,DIM);return;
 }
 int wave=(d->known&K(WAVE))&&d->wave.columns&&d->wave.count&&d->wave.span_ms;
 if(wave){
  struct xz_layered_data look={(const uint8_t *)d->wave.columns,d->wave.count,d->wave.normalization?d->wave.normalization:765,
   XZ_LAYERED_THREE_BAND,{0,0,0}};
  xz_layered_band_colors(look.colors);
  unsigned half=(unsigned)(b.h/2-1);
  int cy=b.y+b.h/2;
  for(int x=0;x<b.w;x++){
   int64_t t0=(int64_t)x*d->total_ms/b.w,t1=(int64_t)(x+1)*d->total_ms/b.w;
   if(t0>=d->wave.span_ms)break;
   uint32_t first=(uint32_t)(t0*d->wave.count/d->wave.span_ms),end=(uint32_t)(t1*d->wave.count/d->wave.span_ms);
   if(first>=d->wave.count)break;
   if(end>d->wave.count)end=d->wave.count;
   if(end<=first)end=first+1;
   struct xz_layered_col col={{0,0,0},0},one;
   uint32_t span=end-first,step=span/8+(span%8!=0);
   for(uint32_t s=first;s<end;s+=step)if(xz_layered_heights(&look,s,s+1,half,&one)){
    for(int k=0;k<3;k++)if(one.h[k]>col.h[k])col.h[k]=one.h[k];
    col.valid=1;
   }
   if(!col.valid)continue;
   for(unsigned r=0;r<col.h[0]&&r<half;r++){
    int pixel=xz_layered_pixel(&look,&col,r);if(pixel<0)continue;
    c.p[(size_t)(cy-1-(int)r)*c.stride+(size_t)(b.x+x)]=(uint16_t)pixel;
    c.p[(size_t)(cy+(int)r)*c.stride+(size_t)(b.x+x)]=(uint16_t)pixel;
   }
  }
 }else xz_rect(c,b.x,b.y+b.h/2,b.w,1,DIM);
 if((d->known&K(LOOP))&&d->loop.out_ms>d->loop.in_ms&&(d->loop.active||d->loop.stored)){
  int x0=xz_extra_detail_column(d->loop.in_ms,d->total_ms),x1=xz_extra_detail_column(d->loop.out_ms,d->total_ms);
  xz_rect(c,x0,b.y,x1-x0+1,2,d->loop.active?LOOP_ORANGE:SOFT);
 }
 if(d->known&K(HOT_CUES))for(int i=0;i<XZ_EXTRA_HOT_CUES;i++)if(d->hot_cue[i].set)
  xz_rect(c,xz_extra_detail_column(d->hot_cue[i].ms,d->total_ms),b.y,1,5,xz_extra_cue_color(&d->hot_cue[i]));
 if((d->known&K(CUE))&&d->cue_ms>=0){
  int x=xz_extra_detail_column(d->cue_ms,d->total_ms);
  for(int h=0;h<4;h++)xz_rect(c,x-h,b.y+b.h-4+h,2*h+1,1,0xff8c00);
 }
 int32_t pos=0;
 if(xz_extra_lane_position_ms(d,now_ms,&pos)){
  int x=xz_extra_detail_column(pos,d->total_ms);
  xz_rect(c,x>b.x?x-1:x,b.y-1,2,b.h+2,(d->known&K(PLAYING))&&d->playing?0xff2020:INK);
 }
}
/* No track: the two ways the classic screen loads a software deck. */
static void empty(struct xz_canvas c,unsigned number,int deck){
 char text[64];
 xz_text(c,6,ROW_TITLE,"NO TRACK",2,10,BLANK);
 snprintf(text,sizeof text,"LOAD DECK %u FROM BROWSE:",number);fit(c,6,ROW_CLOCK,RIGHT,text,1,LABEL);
 snprintf(text,sizeof text,"SET %s CONTROLS TO DECK %u ABOVE,",deck?"RIGHT":"LEFT",number);fit(c,6,ROW_CLOCK+14,RIGHT,text,1,INK);
 fit(c,6,ROW_CLOCK+26,RIGHT,"THEN PRESS LOAD ON A TRACK",1,INK);
 fit(c,6,ROW_CLOCK+44,RIGHT,"OR PUSH THE BROWSE KNOB ON A TRACK",1,INK);
 snprintf(text,sizeof text,"AND CHOOSE LOAD TO DECK %u",number);fit(c,6,ROW_CLOCK+56,RIGHT,text,1,INK);
}
/* A refused touch, else the players' notice, else what the strip does now. */
static void status(struct xz_canvas c,const struct xz_extra_decks *s,const struct xz_extra_deck *d,unsigned number,
 const struct xz_extra_detail_view *v){
 char text[sizeof s->notice+8];
 if(v->notice>XZ_EXTRA_DETAIL_NOTICE_NONE&&v->notice<XZ_EXTRA_DETAIL_NOTICES){
  fit(c,6,ROW_STATUS,RIGHT,xz_extra_detail_notice_text(v->notice,number,text,sizeof text),1,WAIT_AMBER);return;
 }
 if(xz_extra_notice_active(s,v->now_ms)){
  memcpy(text,s->notice,sizeof s->notice);text[sizeof s->notice-1]=0;
  fit(c,6,ROW_STATUS,RIGHT,text,1,LOOP_ORANGE);return;
 }
 if(!d->loaded)return;
 if(d->pad_mode==XZ_EXTRA_PAD_CALL){
  fit(c,6,ROW_STATUS,RIGHT,"PADS RECALL; HOT CUE FINISHES CALL",1,SOFT);return;
 }
 fit(c,6,ROW_STATUS,RIGHT,xz_extra_detail_seek_refusal(d)==XZ_EXTRA_DETAIL_NOTICE_NONE?"TOUCH THE STRIP TO SEARCH THE TRACK":
  "THE STRIP SEARCHES WHILE THE DECK IS PAUSED",1,SOFT);
}

/* While the panel covers the lanes' left halves, each lane's number rides at its right end instead: its
   badge (deck_colour.h) in a one-pixel ring, lit like its tag while that side's controls drive it. */
enum { LANE_NUMBER_X=XZ_WAVE_WIDTH-15 };
static int lane_number_y(const struct xz_four_layout *g,int deck){return (int)g->lane_y[deck]+(g->lane_y[deck]?2:8);}
static void lane_number_box(struct xz_canvas c,const struct xz_four_layout *g,int deck){
 xz_deck_box(c,LANE_NUMBER_X+1,lane_number_y(g,deck)+1,10,11,deck,1,0,1,0);
}
static void lane_numbers(uint16_t *pixels,const struct xz_four_layout *g,const struct xz_extra_decks *s){
 struct xz_canvas c={pixels,XZ_WAVE_WIDTH,XZ_WAVE_WIDTH,XZ_WAVE_HEIGHT,0};
 for(int deck=0;deck<XZ_FOUR_DECKS;deck++){
  int lit=deck==0?!s->left_on_3:deck==1?!s->right_on_4:deck==2?s->left_on_3:s->right_on_4;
  xz_border(c,LANE_NUMBER_X,lane_number_y(g,deck),12,13,lit?LIT:0x55606c);
  lane_number_box(c,g,deck);
 }
}
int xz_four_frame_draw(uint16_t *pixels,size_t count,const struct xz_four_frame *f,struct xz_extra_lane_marks *marks){
 struct xz_four_layout g;
 if(!pixels||!f||(!f->decks&&!f->placeholders)||count<(size_t)XZ_WAVE_WIDTH*XZ_WAVE_HEIGHT||!xz_four_layout_get(f->order,&g))return 0;
 if(f->placeholders){
  /* The native sides drive DECK 1 and 2; DECK 3 and 4 keep no data to draw. */
  xz_four_badge_draw(pixels,XZ_WAVE_WIDTH,count,g.lane_y[0],1,1);
  xz_four_badge_draw(pixels,XZ_WAVE_WIDTH,count,g.lane_y[1],2,1);
  for(int d=2;d<4;d++)for(unsigned r=0;r<XZ_FOUR_LANE_HEIGHT;r++){
   uint16_t *row=pixels+(size_t)(g.lane_y[d]+r)*XZ_WAVE_WIDTH;
   for(unsigned x=0;x<XZ_WAVE_WIDTH;x++)row[x]=0x0841; /* #080808 */
  }
  return 1;
 }
 const struct xz_extra_decks *s=f->decks;
 struct xz_extra_lane_view view={f->now_ms,f->column_us,f->background,0};
 for(unsigned i=0;i<XZ_EXTRA_DECKS;i++){
  view.lit=i?s->right_on_4:s->left_on_3;
  xz_extra_lane_draw_wave(pixels,XZ_WAVE_WIDTH,count,g.lane_y[2+i],3+i,&s->deck[i],&view,marks?&marks[i]:NULL);
 }
 xz_four_badge_draw(pixels,XZ_WAVE_WIDTH,count,g.lane_y[0],1,!s->left_on_3);
 xz_four_badge_draw(pixels,XZ_WAVE_WIDTH,count,g.lane_y[1],2,!s->right_on_4);
 if(f->detail==0||f->detail==1){
  struct xz_extra_detail_view detail={f->now_ms,f->notice};
  xz_extra_detail_draw(pixels,XZ_WAVE_WIDTH,count,s,f->detail,&detail);
  lane_numbers(pixels,&g,s);
 }
 return 1;
}
/* The theme pass recolours the whole surface; the number boxes go back on in their own colours, exactly
   where xz_four_frame_draw put them. Writes only those boxes. */
void xz_four_identity_draw(uint16_t *pixels,size_t count,const struct xz_four_frame *f){
 struct xz_four_layout g;
 if(!pixels||!f||count<(size_t)XZ_WAVE_WIDTH*XZ_WAVE_HEIGHT||!xz_four_layout_get(f->order,&g))return;
 struct xz_canvas c={pixels,XZ_WAVE_WIDTH,XZ_WAVE_WIDTH,XZ_WAVE_HEIGHT,0};
 if(!f->placeholders&&f->decks&&(f->detail==0||f->detail==1)){
  xz_deck_box(c,6,ROW_HEAD,HEAD_BOX,22,2+f->detail,1,0,2,0);
  for(int deck=0;deck<XZ_FOUR_DECKS;deck++)lane_number_box(c,&g,deck);
  return;
 }
 /* DECK 3 and 4 not connected: their lanes carry no tag (the route card covers them). */
 for(int deck=0;deck<(f->placeholders?2:XZ_FOUR_DECKS);deck++)xz_four_number_box(c,3,(int)g.lane_y[deck]+2,(unsigned)deck+1);
}
int xz_extra_detail_draw(uint16_t *pixels,size_t stride,size_t count,const struct xz_extra_decks *s,int deck,
 const struct xz_extra_detail_view *v){
 if(!pixels||!s||!v||deck<0||deck>=XZ_EXTRA_DECKS||stride<XZ_WAVE_WIDTH||stride>SIZE_MAX/XZ_WAVE_HEIGHT||
    count<stride*XZ_WAVE_HEIGHT)return 0;
 struct xz_canvas c={pixels,stride,W,H,0};
 const struct xz_extra_deck *d=&s->deck[deck];
 unsigned number=3u+(unsigned)deck;
 int extra_on=deck?s->right_on_4:s->left_on_3;
 xz_rect(c,0,0,W,H,BG);xz_rect(c,W-1,0,1,H,EDGE);
 header(c,number,d,extra_on);
 controls(c,deck,extra_on,s->route_up);
 if(d->loaded){track(c,d);clock(c,d,v->now_ms);sync(c,d);pads(c,d,number);loop_line(c,d);}
 else empty(c,number,deck);
 modes(c,d);
 overview(c,d,v->now_ms);
 status(c,s,d,number,v);
 return 1;
}
