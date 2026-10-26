/* SPDX-License-Identifier: MIT */
#include "extra_lane.h"
#include "ui_skin.h"
#include "native_led.h"
#include "deck_colour.h"
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(struct xz_extra_column)==3,"PWV7 columns are three packed bytes");
#define K(f) XZ_EXTRA_KNOWN(XZ_EXTRA_F_##f)
#define LANE XZ_FOUR_LANE_HEIGHT
#define WIDTH XZ_WAVE_WIDTH
#define PLAYHEAD XZ_EXTRA_LANE_PLAYHEAD_X
#define PLATE XZ_EXTRA_LANE_READOUT_W
/* Native lane geometry (LAYERED_WAVE.md): body rows 14..117 around the centre line under row 65,
   beat ticks in rows 4..13 and 118..127, cue bands in the outer ten rows. */
#define CENTER 65u
#define EXTENT 52u
#define LOOP_TINT 0xb300
#define BEAT_TICK 0xce59
#define BAR_TICK 0xf800
/* Plate colours (0xRRGGBB), the native panels' conventions: white values, grey labels, red QUANTIZE,
   MT and WIDE, orange MASTER, blue SYNC, amber for anything still waiting. */
#define INK 0xffffff
#define LABEL 0xb4bcc4
#define DIM 0x6a747e
#define BLANK 0x9aa2aa
#define RED 0xe8402c
#define MASTER_ORANGE 0xff6a00
#define SYNC_BLUE 0x2f7ff0
#define SYNC_INK 0x6fb2ff
#define WAIT_AMBER 0xffc070
#define LOOP_ORANGE 0xff9a30
#define SLIP_AMBER 0xffbd59
#define DARK_INK 0x1a1206

int xz_extra_notice_active(const struct xz_extra_decks *s,uint32_t now_ms) {
 return s&&s->notice_seq&&s->notice[0]&&(uint32_t)(now_ms-s->notice_at_ms)<3500u;
}
int xz_extra_banner_draw(uint16_t *pixels,size_t stride,size_t count,const char *text) {
 if(!pixels||!text||!text[0]||stride<WIDTH||stride>SIZE_MAX/XZ_WAVE_HEIGHT||count<stride*XZ_WAVE_HEIGHT)return 0;
 struct xz_canvas c={pixels,stride,WIDTH,XZ_WAVE_HEIGHT,0};
 xz_rect(c,6,244,WIDTH-12,22,0x151515);
 xz_border(c,6,244,WIDTH-12,22,0xff9a30);
 xz_text(c,14,251,text,1,63,0xffffff);
 return 1;
}
int xz_extra_notice_draw(uint16_t *pixels,size_t stride,size_t count,const struct xz_extra_decks *s,uint32_t now_ms) {
 if(!xz_extra_notice_active(s,now_ms))return 0;
 char text[sizeof s->notice];memcpy(text,s->notice,sizeof text);text[sizeof text-1]=0;
 return xz_extra_banner_draw(pixels,stride,count,text);
}

enum xz_extra_sync_class xz_extra_sync_class(const struct xz_extra_deck *d){
 if(!d)return XZ_EXTRA_SYNC_CLASS_NONE;
 if((d->known&K(MASTER))&&d->master)return XZ_EXTRA_SYNC_CLASS_MASTER;
 if(!(d->known&K(SYNC))||!d->sync.requested)return XZ_EXTRA_SYNC_CLASS_NONE;
 if(d->sync.locked)return XZ_EXTRA_SYNC_CLASS_LOCKED;
 switch(d->sync.reason){
 case XZ_EXTRA_SYNC_TEMPO_FOLLOW:case XZ_EXTRA_SYNC_NATIVE_PHASE_UNAVAILABLE:return XZ_EXTRA_SYNC_CLASS_TEMPO;
 case XZ_EXTRA_SYNC_MASTER_CLOCK:return XZ_EXTRA_SYNC_CLASS_MASTER;
 case XZ_EXTRA_SYNC_NO_GRID:case XZ_EXTRA_SYNC_NO_SOURCE_BPM:case XZ_EXTRA_SYNC_OUT_OF_RANGE:
 case XZ_EXTRA_SYNC_LOOP_UNALIGNED:case XZ_EXTRA_SYNC_NATIVE_MASTER_UNSUPPORTED:return XZ_EXTRA_SYNC_CLASS_BLOCKED;
 default:return XZ_EXTRA_SYNC_CLASS_WAIT;
 }
}
const char *xz_extra_sync_chip(const struct xz_extra_deck *d,char *out,size_t size){
 if(!out||!size)return "";
 out[0]=0;
 enum xz_extra_sync_class k=xz_extra_sync_class(d);
 if(k==XZ_EXTRA_SYNC_CLASS_NONE)return out;
 int n=d->sync.master_deck>=0&&d->sync.master_deck<XZ_FOUR_DECKS?d->sync.master_deck+1:0;
 const char *label=NULL;
 switch(k){
 case XZ_EXTRA_SYNC_CLASS_MASTER:{
  uint32_t scope=(d->known&K(MASTER))&&d->master?d->master_scope:d->sync.master_scope;
  label=scope==XZ_EXTRA_MASTER_SCOPE_SOFTWARE?"3/4 MASTER":"MASTER";n=0;break;
 }
 case XZ_EXTRA_SYNC_CLASS_LOCKED:label="SYNC";break;
 case XZ_EXTRA_SYNC_CLASS_TEMPO:label="BPM";break;
 case XZ_EXTRA_SYNC_CLASS_WAIT:label="WAIT";break;
 default:
  n=0;
  label=d->sync.reason==XZ_EXTRA_SYNC_NO_GRID?"NO BEAT GRID":d->sync.reason==XZ_EXTRA_SYNC_NO_SOURCE_BPM?"NO TRACK BPM":
   d->sync.reason==XZ_EXTRA_SYNC_OUT_OF_RANGE?"OUT OF RANGE":d->sync.reason==XZ_EXTRA_SYNC_LOOP_UNALIGNED?"LOOP OFF GRID":"EXT MASTER";
 }
 if(n)snprintf(out,size,"%s %d",label,n);else snprintf(out,size,"%s",label);
 return out;
}
/* The pads' meaning per mode, as the players apply it (extra_player.c beat_loop_beats, slip_loop_beats and
   pad_jump; manual pp.56, 59, 62). */
static const char *const beat_loop_sizes[XZ_EXTRA_HOT_CUES]={"1/4","1/2","1","2","4","8","16","32"};
static const char *const slip_loop_sizes[XZ_EXTRA_HOT_CUES]={"1/16","1/8","1/4","1/2","1","2","1/3","3/4"};
static const char *const jump_sizes[XZ_EXTRA_HOT_CUES]={"-1","+1","-2","+2","-4","+4","-8","+8"};
const char *xz_extra_pad_text(const struct xz_extra_deck *d,char *out,size_t size){
 if(!out||!size)return "";
 out[0]=0;
 if(!d||!d->loaded)return out;
 int active=d->active_pad>=0&&d->active_pad<XZ_EXTRA_HOT_CUES;
 switch(d->pad_mode){
 case XZ_EXTRA_PAD_HOT_CUE:snprintf(out,size,"HOT CUE");break;
 case XZ_EXTRA_PAD_BEAT_LOOP:
  if(active)snprintf(out,size,"BEAT LOOP %s",beat_loop_sizes[d->active_pad]);else snprintf(out,size,"BEAT LOOP");break;
 case XZ_EXTRA_PAD_SLIP_LOOP:
  if(active)snprintf(out,size,"SLIP LOOP %s",slip_loop_sizes[d->active_pad]);else snprintf(out,size,"SLIP LOOP");break;
 case XZ_EXTRA_PAD_BEAT_JUMP:snprintf(out,size,"BEAT JUMP");break;
 case XZ_EXTRA_PAD_CALL:snprintf(out,size,"HOT CUE CALL");break;
 case XZ_EXTRA_PAD_NONE:snprintf(out,size,"PADS OFF");break;
 default:break;
 }
 return out;
}
const char *xz_extra_pad_label(const struct xz_extra_deck *d,int pad,char *out,size_t size){
 if(!out||!size)return "";
 out[0]=0;
 if(!d||!d->loaded||pad<0||pad>=XZ_EXTRA_HOT_CUES)return out;
 switch(d->pad_mode){
 case XZ_EXTRA_PAD_HOT_CUE:case XZ_EXTRA_PAD_CALL:snprintf(out,size,"%c",'A'+pad);break;
 case XZ_EXTRA_PAD_BEAT_LOOP:snprintf(out,size,"%s",beat_loop_sizes[pad]);break;
 case XZ_EXTRA_PAD_SLIP_LOOP:snprintf(out,size,"%s",slip_loop_sizes[pad]);break;
 case XZ_EXTRA_PAD_BEAT_JUMP:snprintf(out,size,"%s",jump_sizes[pad]);break;
 default:break;
 }
 return out;
}

uint32_t xz_extra_cue_color(const struct xz_extra_cue *cue){
 uint32_t color=cue->rgb&0xffffffu;
 return color?color:cue->end_ms>cue->ms?XZ_EXTRA_PAD_LOOP_DEFAULT_RGB:XZ_EXTRA_PAD_DEFAULT_RGB;
}
static uint16_t rgb(unsigned r,unsigned g,unsigned b){return (uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3));}
/* Rows above the centre line for a native lane row, or -1 outside the body. */
static int body_d(unsigned s){
 if(s>CENTER-EXTENT&&s<=CENTER)return (int)(CENTER-s);
 if(s>CENTER&&s<=CENTER+EXTENT)return (int)(s-CENTER-1);
 return -1;
}
/* The 3-band layer colours, low, mid, high, from the shared renderer rather than a second copy. */
static void three_band(uint16_t out[3]){
 static const struct xz_layered_col probe={{3,2,1},1};
 for(unsigned k=0;k<3;k++)out[k]=(uint16_t)xz_layered_pixel(&(struct xz_layered_data){0},&probe,2-k);
}
static uint16_t three_band_pixel(const uint16_t layer[3],const struct xz_layered_col *c,unsigned k){
 return layer[k<c->h[2]?2:k<c->h[1]?1:0];
}
static int tick_row(unsigned s){return (s>=4&&s<=13)||(s>=118&&s<=127);}
static int64_t floor_div(int64_t a,int64_t b){return a>=0?a/b:-((-a+b-1)/b);}

int xz_extra_lane_position_ms(const struct xz_extra_deck *d,uint32_t now_ms,int32_t *out){
 if(!d||!out)return 0;
 int have=(d->known&K(ELAPSED))!=0;
 int64_t p=have?d->elapsed_ms:0;
 if(d->known&K(TIMELINE)){
  const struct xz_extra_timeline *t=&d->timeline;
  uint32_t age=now_ms-t->at_ms;
  if((t->flags&XZ_EXTRA_TIMELINE_ANCHOR)&&t->source_rate&&t->at_ms&&(int32_t)age>=0&&age<=250u&&
     t->source_frame<=(uint64_t)INT32_MAX*t->source_rate/1000u){
   p=(int64_t)(t->source_frame*1000u/t->source_rate);have=1;
   int moving=t->flags&XZ_EXTRA_TIMELINE_MOTION?t->moving:((d->known&K(PLAYING))&&d->playing);
   if(moving&&(t->flags&XZ_EXTRA_TIMELINE_RATE)){
    if(age>50u)age=50u;
    int64_t advanced=p+(int64_t)age*t->rate_ppm/1000000;
    if((d->known&K(LOOP))&&d->loop.active&&d->loop.out_ms>d->loop.in_ms&&p>=d->loop.in_ms&&p<d->loop.out_ms&&
       (advanced<d->loop.in_ms||advanced>=d->loop.out_ms)){
     int64_t span=(int64_t)d->loop.out_ms-d->loop.in_ms,offset=(advanced-d->loop.in_ms)%span;
     advanced=d->loop.in_ms+(offset<0?offset+span:offset);
    }
    p=advanced;
   }
  }
 }else if((d->known&K(PLAYING))&&d->playing&&d->elapsed_at_ms){
  int32_t age=(int32_t)(now_ms-d->elapsed_at_ms);
  if(age<0)age=0;else if(age>1000)age=1000;
  int64_t speed=10000+((d->known&K(TEMPO))?d->tempo_x100:0);
  if(speed>0)p+=(int64_t)age*speed/10000;
 }
 if(!have)return 0;
 if(p<0)p=0;
 if(!(d->known&K(TIMELINE))&&(d->known&K(LOOP))&&d->loop.active&&d->loop.out_ms>d->loop.in_ms&&p>=d->loop.out_ms)
  p=d->loop.in_ms+(p-d->loop.in_ms)%(d->loop.out_ms-d->loop.in_ms);
 if((d->known&K(TOTAL))&&d->total_ms>0&&p>d->total_ms)p=d->total_ms;
 *out=(int32_t)p;return 1;
}

/* Lanes are LANE rows in four-deck views and a whole native lane (FULL) in a 2 DECKS half; rows[] maps each
   row to the native lane row it shows. */
#define FULL XZ_WAVE_NATIVE_LANE_HEIGHT
static void column_rows(uint16_t *lane,size_t stride,unsigned height,int x,int width,const unsigned *rows,int (*want)(unsigned),uint16_t color){
 for(int dx=0;dx<width;dx++){
  int cx=x+dx;if(cx<0||cx>=WIDTH)continue;
  for(unsigned r=0;r<height;r++)if(want(rows[r]))lane[r*stride+(unsigned)cx]=color;
 }
}
static int any_row(unsigned s){(void)s;return 1;}
static int body_row(unsigned s){return body_d(s)>=0;}
static int x_of(int64_t ms,int32_t pos,uint32_t column_us){return PLAYHEAD+(int)floor_div((ms-pos)*1000,column_us);}

static void beat_ticks(uint16_t *lane,size_t stride,unsigned height,const unsigned *rows,const struct xz_extra_deck *d,int32_t pos,uint32_t column_us){
 if(!(d->known&K(GRID))||!d->grid||!d->grid_count)return;
 int64_t lo=pos+floor_div(-(int64_t)PLAYHEAD*column_us,1000)-1,hi=pos+(int64_t)(WIDTH-PLAYHEAD)*column_us/1000+1;
 uint32_t a=0,b=d->grid_count;
 while(a<b){uint32_t m=a+(b-a)/2;if(d->grid[m].ms<lo)a=m+1;else b=m;}
 for(uint32_t i=a;i<d->grid_count&&d->grid[i].ms<=hi;i++){
  int bar=d->grid[i].beat_in_bar==1;
  column_rows(lane,stride,height,x_of(d->grid[i].ms,pos,column_us),bar?2:1,rows,tick_row,bar?BAR_TICK:BEAT_TICK);
 }
}
static void cue_marks(struct xz_canvas c,const struct xz_extra_deck *d,int32_t pos,uint32_t column_us){
 if(d->known&K(HOT_CUES))for(unsigned i=0;i<XZ_EXTRA_HOT_CUES;i++){
  const struct xz_extra_cue *q=&d->hot_cue[i];if(!q->set)continue;
  int x=x_of(q->ms,pos,column_us);if(x<-9||x>=WIDTH)continue;
  /* A flag hanging right of the cue column in the top cue band, lettered A-H. */
  uint32_t color=xz_extra_cue_color(q);
  xz_rect(c,x,0,9,10,color);xz_rect(c,x,10,1,4,color);
  char letter[2]={(char)('A'+i),0};xz_pixel_text(c,x+2,1,letter,1,1,0x000000);
 }
 if((d->known&K(CUE))&&d->cue_ms>=0){
  int x=x_of(d->cue_ms,pos,column_us);
  for(int h=0;h<6;h++)xz_rect(c,x-h,c.height-6+h,2*h+1,1,0xff8c00);
 }
}
/* LOOP IN / OUT adjust: the edge the jog now moves is drawn wide and labelled in the bottom band. */
static void loop_edit_mark(struct xz_canvas c,uint16_t *lane,size_t stride,const unsigned *rows,const struct xz_extra_deck *d,
 int32_t pos,uint32_t column_us){
 int out=d->loop.edit==2,h=c.height;
 int x=x_of(out?d->loop.out_ms:d->loop.in_ms,pos,column_us)-out;
 column_rows(lane,stride,(unsigned)h,x-1,3,rows,any_row,0xffe0);
 const char *label=out?"OUT ADJUST":"IN ADJUST";
 int w=xz_text_width(c,label,1,12)+6,lx=out?x-2-w:x+3;
 if(lx<PLATE+2)lx=x+3;
 if(lx+w>WIDTH)lx=WIDTH-w;
 xz_rect(c,lx,h-12,w,12,0x302a00);xz_text(c,lx+3,h-11,label,1,12,0xffe600);
}
static void darken(uint16_t *lane,size_t stride,unsigned width){
 for(unsigned r=0;r<LANE;r++)for(unsigned x=0;x<width;x++){
  uint16_t *p=&lane[r*stride+x];*p=(uint16_t)((*p>>2)&0x39e7);
 }
}
static void clock_text(char *out,size_t n,int known,int32_t ms,int remain){
 if(!known){snprintf(out,n,"-:--.-");return;}
 if(ms<0)ms=0;
 snprintf(out,n,"%s%d:%02d.%d",remain?"-":"",(int)(ms/60000),(int)(ms/1000%60),(int)(ms/100%10));
}
/* A caps chip in the native panels' style: fill 0 draws no fill, edge 0 no border. Returns the width. */
static int chip(struct xz_canvas c,int x,int y,int h,const char *text,uint32_t fill,uint32_t edge,uint32_t ink){
 int w=xz_text_width(c,text,1,24)+8;
 if(fill)xz_rect(c,x,y,w,h,fill);
 if(edge)xz_border(c,x,y,w,h,edge);
 xz_text(c,x+4,y+(h-9)/2,text,1,24,ink);
 return w;
}
static int chip_width(struct xz_canvas c,const char *text){return xz_text_width(c,text,1,24)+8;}
void xz_four_number_box(struct xz_canvas c,int x,int y,unsigned number){
 xz_deck_box(c,x,y,XZ_FOUR_NUMBER_BOX,XZ_FOUR_NUMBER_BOX,(int)number-1,1,0,1,0);
}
/* The deck tag shared by software plates and native lane badges: the deck's number box, then DECK, lit
   while that side's controls drive the deck. more adds the arrow that says touching the lane opens the
   deck's detail. Returns the width from x 3. */
static int tag(struct xz_canvas c,unsigned number,int lit,int more){
 int x=3+XZ_FOUR_NUMBER_BOX+1,w=xz_text_width(c,"DECK",1,12)+10+(more?7:0);
 uint32_t ink=lit?0x0a1626:0xb4bcc4;
 xz_four_number_box(c,3,2,number);
 if(lit)xz_rect(c,x,2,w,13,0x9cc8ff);
 else{xz_rect(c,x,2,w,13,0x1c2632);xz_border(c,x,2,w,13,0x55606c);}
 xz_text(c,x+5,4,"DECK",1,12,ink);
 if(more)for(int i=0;i<4;i++)xz_rect(c,x+w-9+i,5+i,1,7-2*i,ink);
 return x-3+w;
}
static void readout(struct xz_canvas c,unsigned number,const struct xz_extra_deck *d,int loaded,int placed,int32_t pos,int lit){
 char text[48];
 int x=3+tag(c,number,lit,1)+4;
 if(!loaded)return;
 /* Line 1: what SYNC follows or MASTER, then source and key at the right edge. */
 enum xz_extra_sync_class k=xz_extra_sync_class(d);
 if(k!=XZ_EXTRA_SYNC_CLASS_NONE){
  uint32_t fill=0,edge=0,ink=INK;
  if(k==XZ_EXTRA_SYNC_CLASS_MASTER)fill=MASTER_ORANGE;
  else if(k==XZ_EXTRA_SYNC_CLASS_LOCKED)fill=SYNC_BLUE;
  else if(k==XZ_EXTRA_SYNC_CLASS_TEMPO)edge=ink=SYNC_INK;
  else if(k==XZ_EXTRA_SYNC_CLASS_WAIT)edge=ink=WAIT_AMBER;
  else{fill=WAIT_AMBER;ink=DARK_INK;}
  chip(c,x,2,13,xz_extra_sync_chip(d,text,sizeof text),fill,edge,ink);
 }
 /* Line 2: the clock in the deck's TIME MODE (remaining with a minus, elapsed without), source and key, the
    playing BPM. Nothing sits right of column 150 on line 1: the header strip covers it when this lane is on top. */
 int mode=(d->known&K(TIME_MODE))!=0,remain=!mode||d->remain;
 int timed=placed&&(!remain||((d->known&K(TOTAL))&&d->total_ms>0));
 clock_text(text,sizeof(text),timed,remain?d->total_ms-pos:pos,remain);
 xz_text(c,4,17,text,2,10,timed?INK:BLANK);
 int left=4+xz_text_width(c,text,2,10)+8;
 int bpm=(d->known&K(BPM))&&d->bpm_x100>0;
 if(bpm)snprintf(text,sizeof(text),"%d.%d",(int)(d->bpm_x100/100),(int)(d->bpm_x100%100/10));
 else snprintf(text,sizeof(text),"---.-");
 int right=PLATE-4-xz_text_width(c,text,2,6);
 xz_text(c,right,17,text,2,6,bpm?INK:BLANK);
 right-=8;
 if((d->known&K(KEY))&&d->key[0]){
  char key[sizeof d->key];memcpy(key,d->key,sizeof key);key[sizeof key-1]=0;
  int w=xz_text_width(c,key,1,8);
  if(right-w>=left){xz_text(c,right-w,21,key,1,8,INK);right-=w+6;}
 }
 if((d->known&K(SOURCE))&&(d->source==1||d->source==2)){
  const char *usb=d->source==1?"USB1":"USB2";int w=xz_text_width(c,usb,1,8);
  if(right-w>=left)xz_text(c,right-w,21,usb,1,8,LABEL);
 }
 /* Line 3: REMAIN or TIME and A.CUE, as the native panel labels them; tempo, its range (or TEMPO RESET) and MT. */
 int lx=4;
 if(mode){
  const char *name=d->remain?"REMAIN":"TIME";
  xz_text(c,lx,34,name,1,8,LABEL);lx+=xz_text_width(c,name,1,8)+5;
  lx+=chip(c,lx,32,11,"A.CUE",0,d->auto_cue?INK:DIM,d->auto_cue?INK:DIM)+4;
 }
 int rx=PLATE-4;
 if(d->known&K(MASTER_TEMPO)){
  rx-=chip_width(c,"MT");chip(c,rx,32,11,"MT",0,d->master_tempo?RED:DIM,d->master_tempo?RED:DIM);rx-=4;
 }
 if(d->tempo_reset){rx-=chip_width(c,"RESET");chip(c,rx,32,11,"RESET",INK,0,DARK_INK);rx-=4;}
 else if(d->known&K(TEMPO_RANGE)){
  if(d->tempo_range==100){rx-=chip_width(c,"WIDE");chip(c,rx,32,11,"WIDE",RED,0,INK);rx-=4;}
  else if(d->tempo_range==6||d->tempo_range==10||d->tempo_range==16){
   snprintf(text,sizeof text,"\xc2\xb1%d",d->tempo_range);
   rx-=chip_width(c,text);chip(c,rx,32,11,text,0,DIM,LABEL);rx-=4;
  }
 }
 if(d->known&K(TEMPO)){
  int32_t t=d->tempo_x100,a=t<0?-t:t;
  snprintf(text,sizeof(text),"%c%d.%02d%%",t<0?'-':t>0?'+':' ',(int)(a/100),(int)(a%100));
 }else snprintf(text,sizeof(text),"--.--%%");
 int tw=xz_text_width(c,text,1,8);
 if(rx-tw>=lx)xz_text(c,rx-tw,34,text,1,8,(d->known&K(TEMPO))?0xd8dde2:BLANK);
 /* Line 4: what the pads do, then REV, SLIP (filled while playback runs on underneath), QUANTIZE, VINYL. */
 lx=3;
 if(xz_extra_pad_text(d,text,sizeof text)[0]){
  int looping=(d->pad_mode==XZ_EXTRA_PAD_BEAT_LOOP||d->pad_mode==XZ_EXTRA_PAD_SLIP_LOOP)&&
   d->active_pad>=0&&d->active_pad<XZ_EXTRA_HOT_CUES;
  if(d->pad_mode==XZ_EXTRA_PAD_NONE)lx+=chip(c,lx,44,11,text,0,DIM,BLANK)+3;
  else if(looping)lx+=chip(c,lx,44,11,text,LOOP_ORANGE,0,DARK_INK)+3;
  else lx+=chip(c,lx,44,11,text,0x2a3440,0,INK)+3;
 }
 lx+=(d->reverse?chip(c,lx,44,11,"REV",0xff5a3c,0,INK):chip(c,lx,44,11,"REV",0,DIM,DIM))+3;
 if(d->slip_active)lx+=chip(c,lx,44,11,"SLIP",SLIP_AMBER,0,DARK_INK)+3;
 else lx+=chip(c,lx,44,11,"SLIP",0,d->slip?SLIP_AMBER:DIM,d->slip?SLIP_AMBER:DIM)+3;
 if(d->known&K(QUANTIZE))lx+=(d->quantize?chip(c,lx,44,11,"Q",RED,0,INK):chip(c,lx,44,11,"Q",0,DIM,DIM))+3;
 if((d->known&K(VINYL))&&lx+chip_width(c,"VINYL")<=PLATE)chip(c,lx,44,11,"VINYL",0,d->vinyl?INK:DIM,d->vinyl?INK:DIM);
 /* Line 5: title, then the artist when there is room. */
 int ty=55,end=4;
 if((d->known&K(TITLE))&&d->title[0]){
  xz_text(c,4,ty,d->title,1,(PLATE-8)/6,0xc8ced4);end=4+xz_text_width(c,d->title,1,(PLATE-8)/6);
 }
 if((d->known&K(ARTIST))&&d->artist[0]&&end+24<PLATE-4){
  int ax=end>4?end+5:4;
  if(end>4){xz_text(c,end+4,ty,"\xc2\xb7",1,1,LABEL);ax=end+11;}
  xz_text(c,ax,ty,d->artist,1,(PLATE-4-ax)/6,0x8a949e);
 }
}

/* What a lane carries besides the waveform: the deck tag (four-deck lanes), the readout plate, or nothing (a
   2 DECKS half, where the side column beside it names the deck as it names a native one). */
enum lane_label { LABEL_TAG, LABEL_PLATE, LABEL_NONE };
static int draw_lane(uint16_t *pixels,size_t stride,size_t count,unsigned lane_y,unsigned height,unsigned number,
 const struct xz_extra_deck *d,const struct xz_extra_lane_view *view,struct xz_extra_lane_marks *marks,enum lane_label plate){
 if(marks)memset(marks,0,sizeof(*marks));
 if(!pixels||!view||(height!=LANE&&height!=FULL)||stride<WIDTH||stride>SIZE_MAX/XZ_WAVE_HEIGHT||lane_y>XZ_WAVE_HEIGHT-height||
    count<stride*(lane_y+height)||number<3||number>4)return 0;
 uint16_t *lane=pixels+(size_t)lane_y*stride;
 unsigned rows[FULL];for(unsigned r=0;r<height;r++)rows[r]=xz_wave_lane_row(r,height);
 int loaded=d&&d->loaded;
 int32_t pos=0;int placed=loaded&&xz_extra_lane_position_ms(d,view->now_ms,&pos);
 uint32_t column_us=view->column_us?view->column_us:XZ_EXTRA_LANE_COLUMN_US;
 struct xz_layered_data look={0};
 int wave=placed&&(d->known&K(WAVE))&&d->wave.columns&&d->wave.count&&d->wave.span_ms;
 if(wave)look=(struct xz_layered_data){(const uint8_t *)d->wave.columns,d->wave.count,
  d->wave.normalization?d->wave.normalization:765,XZ_LAYERED_THREE_BAND,{0,0,0}};
 uint16_t grade[EXTENT];
 for(unsigned k=0;k<EXTENT;k++){unsigned v=EXTENT-k;grade[k]=view->background==1?rgb(v,v,v):rgb(0,v,2*v);}
 uint16_t layer[3];three_band(layer);
 int64_t span_us=wave?(int64_t)d->wave.span_ms*1000:0;
 int looped=placed&&(d->known&K(LOOP))&&d->loop.active&&d->loop.out_ms>d->loop.in_ms;
 static _Thread_local struct xz_layered_col spare[WIDTH];
 struct xz_layered_col *cols=marks?marks->col:spare;
 uint8_t tinted[WIDTH];
 for(unsigned x=0;x<WIDTH;x++){
  struct xz_layered_col *col=&cols[x];col->valid=0;tinted[x]=0;
  int64_t t0=(int64_t)pos*1000+((int64_t)x-PLAYHEAD)*column_us;
  if(wave&&t0>=0&&t0<span_us){
   uint32_t first=(uint32_t)(t0*d->wave.count/span_us),end=(uint32_t)((t0+column_us)*d->wave.count/span_us);
   if(end>d->wave.count)end=d->wave.count;
   if(end<=first)end=first+1;
   xz_layered_heights(&look,first,end,EXTENT,col);
  }
  if(looped)tinted[x]=t0>=(int64_t)d->loop.in_ms*1000&&t0<(int64_t)d->loop.out_ms*1000;
 }
 for(unsigned r=0;r<height;r++){
  uint16_t *row=lane+r*stride;int k=loaded?body_d(rows[r]):-1;
  if(k<0){memset(row,0,WIDTH*sizeof(*row));continue;}
  for(unsigned x=0;x<WIDTH;x++){
   const struct xz_layered_col *c=&cols[x];unsigned u=(unsigned)k;
   row[x]=c->valid&&u<c->h[0]?three_band_pixel(layer,c,u):tinted[x]?LOOP_TINT:grade[u];
  }
 }
 if(marks){marks->look=look;marks->look.bands=NULL;}
 struct xz_canvas c={lane,stride,WIDTH,(int)height,0};
 if(looped)for(int edge=0;edge<2;edge++)
  column_rows(lane,stride,height,x_of(edge?d->loop.out_ms:d->loop.in_ms,pos,column_us)-edge,1,rows,body_row,0xfd20);
 /* A stored loop RELOOP returns to, outside the loop: its ends stay marked, dimmer and untinted. */
 else if(placed&&(d->known&K(LOOP))&&d->loop.stored&&d->loop.out_ms>d->loop.in_ms)for(int edge=0;edge<2;edge++)
  column_rows(lane,stride,height,x_of(edge?d->loop.out_ms:d->loop.in_ms,pos,column_us)-edge,1,rows,body_row,0x8c40);
 if(placed){beat_ticks(lane,stride,height,rows,d,pos,column_us);cue_marks(c,d,pos,column_us);}
 if(looped&&(d->loop.edit==1||d->loop.edit==2))loop_edit_mark(c,lane,stride,rows,d,pos,column_us);
 if(loaded)column_rows(lane,stride,height,PLAYHEAD,2,rows,any_row,(d->known&K(PLAYING))&&d->playing?0xf800:0xffff);
 else{
  const char *none="NO TRACK";
  int w=xz_text_width(c,none,2,12);
  xz_text(c,((plate==LABEL_NONE?0:PLATE)+WIDTH-w)/2,((int)height-14)/2,none,2,12,0x9aa2aa);
 }
 if(plate==LABEL_PLATE){
  if(loaded)darken(lane,stride,PLATE);
  readout(c,number,d,loaded,placed,pos,view->lit);
 }else if(plate==LABEL_TAG)tag(c,number,view->lit,1);
 return 1;
}
int xz_extra_lane_draw(uint16_t *pixels,size_t stride,size_t count,unsigned lane_y,unsigned number,
 const struct xz_extra_deck *d,const struct xz_extra_lane_view *view,struct xz_extra_lane_marks *marks){
 return draw_lane(pixels,stride,count,lane_y,LANE,number,d,view,marks,LABEL_PLATE);
}
int xz_extra_lane_draw_wave(uint16_t *pixels,size_t stride,size_t count,unsigned lane_y,unsigned number,
 const struct xz_extra_deck *d,const struct xz_extra_lane_view *view,struct xz_extra_lane_marks *marks){
 return draw_lane(pixels,stride,count,lane_y,LANE,number,d,view,marks,LABEL_TAG);
}
int xz_extra_lane_draw_full(uint16_t *pixels,size_t stride,size_t count,unsigned lane_y,unsigned number,
 const struct xz_extra_deck *d,const struct xz_extra_lane_view *view,struct xz_extra_lane_marks *marks){
 return draw_lane(pixels,stride,count,lane_y,FULL,number,d,view,marks,LABEL_NONE);
}

int xz_four_badge_draw(uint16_t *pixels,size_t stride,size_t count,unsigned lane_y,unsigned number,int lit){
 if(!pixels||stride<WIDTH||stride>SIZE_MAX/XZ_WAVE_HEIGHT||lane_y>XZ_WAVE_HEIGHT-LANE||
    count<stride*(lane_y+LANE)||number<1||number>2)return 0;
 struct xz_canvas c={pixels+(size_t)lane_y*stride,stride,WIDTH,LANE,0};
 tag(c,number,lit,0);
 return 1;
}

/* Visits every body pixel a lane's column drew in a layer colour, with that colour: lanes of height rows
   (LANE or FULL) at lane_y[i]; a NULL col[i] is skipped. */
static void each_body_pixel(unsigned lanes,unsigned height,const unsigned *lane_y,const struct xz_layered_data *looks,
 const struct xz_layered_col *const *col,const uint16_t *read,uint16_t *write,uint8_t *set,const uint8_t *keep){
 uint16_t layer[3];three_band(layer);
 int k_of[FULL];for(unsigned r=0;r<height;r++)k_of[r]=body_d(xz_wave_lane_row(r,height));
 for(unsigned deck=0;deck<lanes;deck++){
  const struct xz_layered_col *cols=col[deck];if(!cols)continue;
  const struct xz_layered_data *look=&looks[deck];int fast=look->kind==XZ_LAYERED_THREE_BAND;
  for(unsigned r=0;r<height;r++){
   if(k_of[r]<0)continue;
   unsigned k=(unsigned)k_of[r];size_t base=(size_t)(lane_y[deck]+r)*WIDTH;
   for(unsigned x=0;x<WIDTH;x++){
    const struct xz_layered_col *c=&cols[x];
    if(!c->valid||k>=c->h[0])continue;
    size_t i=base+x;
    if(keep&&!(keep[i>>3]&(1u<<(i&7))))continue;
    int want=fast?three_band_pixel(layer,c,k):xz_layered_pixel(look,c,k);if(want<0)continue;
    if(set&&read[i]==(uint16_t)want)set[i>>3]|=(uint8_t)(1u<<(i&7));
    if(write)write[i]=(uint16_t)want;
   }
  }
 }
}
void xz_four_protect(const struct xz_four_layout *g,const struct xz_four_marks *m,const uint16_t *pixels,uint8_t *keep){
 if(!keep)return;
 memset(keep,0,XZ_FOUR_KEEP_BYTES);
 if(g&&m&&pixels)each_body_pixel(XZ_FOUR_DECKS,LANE,g->lane_y,m->look,m->col,pixels,NULL,keep,NULL);
}
void xz_four_restore(const struct xz_four_layout *g,const struct xz_four_marks *m,uint16_t *pixels,const uint8_t *keep){
 if(g&&m&&pixels&&keep)each_body_pixel(XZ_FOUR_DECKS,LANE,g->lane_y,m->look,m->col,NULL,pixels,NULL,keep);
}

/* 2 DECKS with a side on DECK 3 or 4 (extra_lane.h). */
static const unsigned two_lane_y[2]={XZ_TWO_LANE_Y(0),XZ_TWO_LANE_Y(1)};
int xz_two_frame_draw(uint16_t *pixels,size_t count,const struct xz_two_frame *f,struct xz_extra_lane_marks marks[2]){
 if(marks)memset(marks,0,2*sizeof *marks);
 if(!pixels||!f||!f->decks||count<(size_t)WIDTH*XZ_WAVE_HEIGHT||!(f->pair&3u)||(f->pair&~3u))return 0;
 struct xz_extra_lane_view view={f->now_ms,f->column_us,f->background,1};
 for(unsigned side=0;side<2;side++){
  if(!(f->pair&(1u<<side)))continue;
  /* The right half starts at the separator rows above native DECK 2's lane; they stay black, as stock leaves them. */
  if(side)memset(pixels+(size_t)XZ_TWO_HALF_Y(1)*WIDTH,0,(size_t)(XZ_TWO_LANE_Y(1)-XZ_TWO_HALF_Y(1))*WIDTH*sizeof *pixels);
  xz_extra_lane_draw_full(pixels,WIDTH,count,two_lane_y[side],3+side,&f->decks->deck[side],&view,marks?&marks[side]:NULL);
 }
 return 1;
}
static void two_marks(unsigned pair,const struct xz_extra_lane_marks marks[2],struct xz_layered_data look[2],const struct xz_layered_col *col[2]){
 for(unsigned side=0;side<2;side++){look[side]=marks[side].look;col[side]=pair&(1u<<side)?marks[side].col:NULL;}
}
void xz_two_protect(unsigned pair,const struct xz_extra_lane_marks marks[2],const uint16_t *pixels,uint8_t *keep){
 if(!keep)return;
 memset(keep,0,XZ_FOUR_KEEP_BYTES);
 if(!marks||!pixels)return;
 struct xz_layered_data look[2];const struct xz_layered_col *col[2];two_marks(pair,marks,look,col);
 each_body_pixel(2,FULL,two_lane_y,look,col,pixels,NULL,keep,NULL);
}
void xz_two_restore(unsigned pair,const struct xz_extra_lane_marks marks[2],uint16_t *pixels,const uint8_t *keep){
 if(!marks||!pixels||!keep)return;
 struct xz_layered_data look[2];const struct xz_layered_col *col[2];two_marks(pair,marks,look,col);
 each_body_pixel(2,FULL,two_lane_y,look,col,NULL,pixels,NULL,keep);
}
