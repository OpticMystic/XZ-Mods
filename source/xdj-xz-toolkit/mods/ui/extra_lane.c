/* SPDX-License-Identifier: MIT */
#include "extra_lane.h"
#include "ui_skin.h"
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(struct xz_extra_column)==3,"PWV7 columns are three packed bytes");
#define K(f) XZ_EXTRA_KNOWN(XZ_EXTRA_F_##f)
#define LANE XZ_FOUR_LANE_HEIGHT
#define WIDTH XZ_WAVE_WIDTH
#define PLAYHEAD XZ_EXTRA_LANE_PLAYHEAD_X
/* Native lane geometry (LAYERED_WAVE.md): body rows 14..117 around the centre line under row 65,
   beat ticks in rows 4..13 and 118..127, cue bands in the outer ten rows. */
#define CENTER 65u
#define EXTENT 52u
#define LOOP_TINT 0xb300
#define BEAT_TICK 0xce59
#define BAR_TICK 0xf800

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
 if(!d||!out||!(d->known&K(ELAPSED)))return 0;
 int64_t p=d->elapsed_ms;
 if((d->known&K(PLAYING))&&d->playing&&d->elapsed_at_ms){
  int32_t age=(int32_t)(now_ms-d->elapsed_at_ms);
  if(age<0)age=0;else if(age>1000)age=1000;
  int64_t speed=10000+((d->known&K(TEMPO))?d->tempo_x100:0);
  if(speed>0)p+=(int64_t)age*speed/10000;
 }
 if(p<0)p=0;
 if((d->known&K(TOTAL))&&d->total_ms>0&&p>d->total_ms)p=d->total_ms;
 *out=(int32_t)p;return 1;
}

static void column_rows(uint16_t *lane,size_t stride,int x,int width,const unsigned *rows,int (*want)(unsigned),uint16_t color){
 for(int dx=0;dx<width;dx++){
  int cx=x+dx;if(cx<0||cx>=WIDTH)continue;
  for(unsigned r=0;r<LANE;r++)if(want(rows[r]))lane[r*stride+(unsigned)cx]=color;
 }
}
static int any_row(unsigned s){(void)s;return 1;}
static int body_row(unsigned s){return body_d(s)>=0;}
static int x_of(int64_t ms,int32_t pos,uint32_t column_us){return PLAYHEAD+(int)floor_div((ms-pos)*1000,column_us);}

static void beat_ticks(uint16_t *lane,size_t stride,const unsigned *rows,const struct xz_extra_deck *d,int32_t pos,uint32_t column_us){
 if(!(d->known&K(GRID))||!d->grid||!d->grid_count)return;
 int64_t lo=pos+floor_div(-(int64_t)PLAYHEAD*column_us,1000)-1,hi=pos+(int64_t)(WIDTH-PLAYHEAD)*column_us/1000+1;
 uint32_t a=0,b=d->grid_count;
 while(a<b){uint32_t m=a+(b-a)/2;if(d->grid[m].ms<lo)a=m+1;else b=m;}
 for(uint32_t i=a;i<d->grid_count&&d->grid[i].ms<=hi;i++){
  int bar=d->grid[i].beat_in_bar==1;
  column_rows(lane,stride,x_of(d->grid[i].ms,pos,column_us),bar?2:1,rows,tick_row,bar?BAR_TICK:BEAT_TICK);
 }
}
static void cue_marks(struct xz_canvas c,const struct xz_extra_deck *d,int32_t pos,uint32_t column_us){
 if(d->known&K(HOT_CUES))for(unsigned i=0;i<XZ_EXTRA_HOT_CUES;i++){
  const struct xz_extra_cue *q=&d->hot_cue[i];if(!q->set)continue;
  int x=x_of(q->ms,pos,column_us);if(x<-9||x>=WIDTH)continue;
  /* A flag hanging right of the cue column in the top cue band, lettered A-H. */
  xz_rect(c,x,0,9,10,q->rgb&0xffffff);xz_rect(c,x,10,1,4,q->rgb&0xffffff);
  char letter[2]={(char)('A'+i),0};xz_pixel_text(c,x+2,1,letter,1,1,0x000000);
 }
 if((d->known&K(CUE))&&d->cue_ms>=0){
  int x=x_of(d->cue_ms,pos,column_us);
  for(int h=0;h<6;h++)xz_rect(c,x-h,LANE-6+h,2*h+1,1,0xff8c00);
 }
}
static void darken(uint16_t *lane,size_t stride,unsigned width){
 for(unsigned r=0;r<LANE;r++)for(unsigned x=0;x<width;x++){
  uint16_t *p=&lane[r*stride+x];*p=(uint16_t)((*p>>2)&0x39e7);
 }
}
static void clock_text(char *out,size_t n,int known,int32_t ms){
 if(!known){snprintf(out,n,"-:--.-");return;}
 if(ms<0)ms=0;
 snprintf(out,n,"-%d:%02d.%d",(int)(ms/60000),(int)(ms/1000%60),(int)(ms/100%10));
}
static void readout(struct xz_canvas c,unsigned number,const struct xz_extra_deck *d,int loaded,int placed,int32_t pos,int lit){
 char text[48];snprintf(text,sizeof(text),"DECK %u",number);
 int tag=xz_text_width(c,text,1,12)+10;
 if(lit){xz_rect(c,3,2,tag,13,0x9cc8ff);xz_text(c,8,4,text,1,12,0x0a1626);}
 else{xz_rect(c,3,2,tag,13,0x1c2632);xz_border(c,3,2,tag,13,0x55606c);xz_text(c,8,4,text,1,12,0xb4bcc4);}
 if(!loaded)return;
 if((d->known&K(MASTER))&&d->master){
  int w=xz_text_width(c,"MASTER",1,12)+8;
  xz_rect(c,3+tag+4,2,w,13,0xff6a00);xz_text(c,3+tag+8,4,"MASTER",1,12,0xffffff);
 }
 int timed=placed&&(d->known&K(TOTAL))&&d->total_ms>0;
 clock_text(text,sizeof(text),timed,timed?d->total_ms-pos:0);
 xz_text(c,4,18,text,2,8,timed?0xffffff:0x9aa2aa);
 int bpm=(d->known&K(BPM))&&d->bpm_x100>0;
 if(bpm)snprintf(text,sizeof(text),"%d.%d",(int)(d->bpm_x100/100),(int)(d->bpm_x100%100/10));
 else snprintf(text,sizeof(text),"---.-");
 xz_text(c,4,35,text,2,6,bpm?0xffffff:0x9aa2aa);
 int x=4+xz_text_width(c,text,2,6)+5;
 if(d->known&K(TEMPO)){
  int32_t t=d->tempo_x100,a=t<0?-t:t;
  snprintf(text,sizeof(text),"%c%d.%02d%%",t<0?'-':t>0?'+':' ',(int)(a/100),(int)(a%100));
 }else snprintf(text,sizeof(text),"--.--%%");
 xz_text(c,x,40,text,1,8,(d->known&K(TEMPO))?0xd8dde2:0x9aa2aa);
 if((d->known&K(TITLE))&&d->title[0])xz_text(c,4,52,d->title,1,(XZ_EXTRA_LANE_READOUT_W-8)/6,0xc8ced4);
}

int xz_extra_lane_draw(uint16_t *pixels,size_t stride,size_t count,unsigned lane_y,unsigned number,
 const struct xz_extra_deck *d,const struct xz_extra_lane_view *view,struct xz_extra_lane_marks *marks){
 if(marks)memset(marks,0,sizeof(*marks));
 if(!pixels||!view||stride<WIDTH||stride>SIZE_MAX/XZ_WAVE_HEIGHT||lane_y>XZ_WAVE_HEIGHT-LANE||
    count<stride*(lane_y+LANE)||number<3||number>4)return 0;
 uint16_t *lane=pixels+(size_t)lane_y*stride;
 unsigned rows[LANE];for(unsigned r=0;r<LANE;r++)rows[r]=xz_wave_lane_row(r,LANE);
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
 for(unsigned r=0;r<LANE;r++){
  uint16_t *row=lane+r*stride;int k=loaded?body_d(rows[r]):-1;
  if(k<0){memset(row,0,WIDTH*sizeof(*row));continue;}
  for(unsigned x=0;x<WIDTH;x++){
   const struct xz_layered_col *c=&cols[x];unsigned u=(unsigned)k;
   row[x]=c->valid&&u<c->h[0]?three_band_pixel(layer,c,u):tinted[x]?LOOP_TINT:grade[u];
  }
 }
 if(marks){marks->look=look;marks->look.bands=NULL;}
 struct xz_canvas c={lane,stride,WIDTH,LANE,0};
 if(looped)for(int edge=0;edge<2;edge++)
  column_rows(lane,stride,x_of(edge?d->loop.out_ms:d->loop.in_ms,pos,column_us)-edge,1,rows,body_row,0xfd20);
 if(placed){beat_ticks(lane,stride,rows,d,pos,column_us);cue_marks(c,d,pos,column_us);}
 if(loaded)column_rows(lane,stride,PLAYHEAD,2,rows,any_row,(d->known&K(PLAYING))&&d->playing?0xf800:0xffff);
 else{
  const char *none="NO TRACK";
  int w=xz_text_width(c,none,2,12);
  xz_text(c,(XZ_EXTRA_LANE_READOUT_W+WIDTH-w)/2,(LANE-14)/2,none,2,12,0x9aa2aa);
 }
 if(loaded)darken(lane,stride,XZ_EXTRA_LANE_READOUT_W);
 readout(c,number,d,loaded,placed,pos,view->lit);
 return 1;
}

/* Visits every body pixel a deck's column drew in a layer colour, with that colour. */
static void each_body_pixel(const struct xz_four_layout *g,const struct xz_four_marks *m,const uint16_t *read,uint16_t *write,uint8_t *set,const uint8_t *keep){
 uint16_t layer[3];three_band(layer);
 int k_of[LANE];for(unsigned r=0;r<LANE;r++)k_of[r]=body_d(xz_wave_lane_row(r,LANE));
 for(unsigned deck=0;deck<XZ_FOUR_DECKS;deck++){
  const struct xz_layered_col *cols=m->col[deck];if(!cols)continue;
  const struct xz_layered_data *look=&m->look[deck];int fast=look->kind==XZ_LAYERED_THREE_BAND;
  for(unsigned r=0;r<LANE;r++){
   if(k_of[r]<0)continue;
   unsigned k=(unsigned)k_of[r];size_t base=(size_t)(g->lane_y[deck]+r)*WIDTH;
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
 if(g&&m&&pixels)each_body_pixel(g,m,pixels,NULL,keep,NULL);
}
void xz_four_restore(const struct xz_four_layout *g,const struct xz_four_marks *m,uint16_t *pixels,const uint8_t *keep){
 if(g&&m&&pixels&&keep)each_body_pixel(g,m,NULL,pixels,NULL,keep);
}
