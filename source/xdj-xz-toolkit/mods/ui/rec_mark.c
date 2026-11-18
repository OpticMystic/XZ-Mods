/* SPDX-License-Identifier: MIT */
/* The REC mark (rec_mark.h). */
#include "rec_mark.h"
#include "top_bar.h"
#include "l10n.h"
#include <stdio.h>
#include <string.h>

/* Fixed in every theme, as the metrics HOT warning and the LOAD ERR badge are: REC is a state, not
   decoration. REC red is a true red, clear of the flagship FX strip's orange red (0xff4619) beside
   it; the long state's crimson carries a light edge so it never merges with a lit FX strip. */
enum {
 REC_RED=0xff2a2a,
 CALM_FILL=0x000000, CALM_EDGE=0x8a2020,
 LONG_FILL=0xc4161c, LONG_EDGE=0xffa09a,
 REC_INK=0xffffff
};

struct xz_rec_reading xz_rec_observe(struct xz_rec_tracker *t, const struct xz_recorder_state *s, uint32_t now_ms) {
 struct xz_rec_reading r={0};
 if(!t)return r;
 if(!s||!s->known){
  if(t->last.known&&now_ms-t->known_at_ms<XZ_REC_HOLD_MS)return t->last;
  /* Lost sight of it: the next start is not seen. */
  memset(t,0,sizeof *t);return r;
 }
 r.known=1;r.read_at_ms=now_ms;r.generation=s->generation;t->known_at_ms=now_ms;
 if(!s->recording){t->active=t->timed=0;t->saw_idle=1;t->last=r;return r;}
 r.recording=1;
 if(!t->active){t->active=1;t->timed=t->saw_idle||s->elapsed_ms!=0;t->start_ms=now_ms-s->elapsed_ms;}
 else if(!t->timed&&s->elapsed_ms){t->timed=1;t->start_ms=now_ms-s->elapsed_ms;}
 r.elapsed_ms=now_ms-t->start_ms;r.elapsed_known=t->timed;
 t->last=r;return r;
}

struct xz_rec_view xz_rec_view_at(const struct xz_rec_reading *r, uint32_t now_ms) {
 struct xz_rec_view v={XZ_REC_NONE,0,0,0};
 if(!r||!r->known||!r->recording)return v;
 /* The clock runs on from the reading, for a while: a stalled reader holds its last second. */
 int32_t since=(int32_t)(now_ms-r->read_at_ms);
 uint32_t age=since<0?0u:(uint32_t)since>XZ_REC_EXTRAPOLATE_MS?XZ_REC_EXTRAPOLATE_MS:(uint32_t)since;
 uint32_t elapsed=r->elapsed_ms+age;
 if(elapsed<r->elapsed_ms)elapsed=UINT32_MAX;
 v.elapsed_ms=elapsed;v.time_known=r->elapsed_known;
 v.level=elapsed>=XZ_REC_LONG_MS?XZ_REC_LONG:XZ_REC_ON;
 /* The long state's dot lights with each second and goes out at its half: one 9 px disc changes. */
 v.dot_lit=v.level==XZ_REC_LONG?elapsed%1000u<XZ_REC_BLINK_MS:1;
 return v;
}

void xz_rec_clock(char out[12], uint32_t ms) {
 uint32_t s=ms/1000u,h=s/3600u,m=s/60u%60u;s%=60u;
 if(h>99u){h=99u;m=59u;s=59u;}
 /* Under an hour MM:SS, as the decks' own times read; the hour digit arrives with the long state. */
 if(h)snprintf(out,12,"%u:%02u:%02u",(unsigned)h,(unsigned)m,(unsigned)s);
 else snprintf(out,12,"%02u:%02u",(unsigned)m,(unsigned)s);
}

/* ---- geometry ---- */
static int text_y(struct xz_canvas c, int y, int h) { return y+(h-(xz_theme_pixel_text(c.theme)?7:9))/2; }
static int dot_r(int h) { return h>=18?4:3; }
static const char *word(void) { return xz_l10n_authored("REC"); }
/* Digits sit in cells as wide as the widest digit, so the ticking clock never shivers or resizes. */
static int digit_cell(struct xz_canvas c) {
 int cell=0;char d[2]={0,0};
 for(char ch='0';ch<='9';ch++){d[0]=ch;int w=xz_text_width(c,d,1,1);if(w>cell)cell=w;}
 return cell;
}
static int clock_width(struct xz_canvas c, const char *s) {
 int w=0,cell=digit_cell(c);char one[2]={0,0};
 for(;*s;s++){if(*s>='0'&&*s<='9')w+=cell;else{one[0]=*s;w+=xz_text_width(c,one,1,1);}}
 return w;
}
static void clock_text(struct xz_canvas c, int x, int y, const char *s, uint32_t color) {
 int cell=digit_cell(c);char one[2]={0,0};
 for(;*s;s++){
  one[0]=*s;int w=xz_text_width(c,one,1,1);
  if(*s>='0'&&*s<='9'){xz_text(c,x+(cell-w)/2,y,one,1,1,color);x+=cell;}
  else{xz_text(c,x,y,one,1,1,color);x+=w;}
 }
}
static int lead(int h) { return h/2+dot_r(h)+5; }   /* left edge to the first letter */
static int tail(int h) { return h/2-1; }            /* last letter to the right edge */
static int width_of(struct xz_canvas c, unsigned parts, int h, const char *clock) {
 if(!(parts&(XZ_REC_PART_WORD|XZ_REC_PART_TIME)))return h;
 int w=lead(h)+tail(h);
 if(parts&XZ_REC_PART_WORD)w+=xz_text_width(c,word(),1,8);
 if((parts&XZ_REC_PART_WORD)&&(parts&XZ_REC_PART_TIME))w+=6;
 if(parts&XZ_REC_PART_TIME)w+=clock_width(c,clock);
 return w;
}
static unsigned parts_for(struct xz_canvas c, int room, int h, int time_known, const char *clock) {
 static const unsigned order[4]={XZ_REC_PART_WORD|XZ_REC_PART_TIME,XZ_REC_PART_TIME,XZ_REC_PART_WORD,0};
 for(int i=0;i<4;i++){
  if(!time_known&&(order[i]&XZ_REC_PART_TIME))continue;
  if(width_of(c,order[i],h,clock)<=room)return order[i];
 }
 return 0;
}
int xz_rec_width(struct xz_canvas c, unsigned parts, int h, uint32_t elapsed_ms) {
 char clock[12];xz_rec_clock(clock,elapsed_ms);
 return width_of(c,parts,h,clock);
}
unsigned xz_rec_parts(struct xz_canvas c, int room, int h, const struct xz_rec_view *v) {
 char clock[12];xz_rec_clock(clock,v?v->elapsed_ms:0);
 return parts_for(c,room,h,v&&v->time_known,clock);
}
int xz_rec_box(struct xz_canvas c, int room, int h, const struct xz_rec_view *v) {
 if(!v||v->level==XZ_REC_NONE||room<h)return 0;
 char clock[12];xz_rec_clock(clock,v->elapsed_ms);
 return width_of(c,parts_for(c,room,h,v->time_known,clock),h,clock);
}

int xz_rec_draw(struct xz_canvas c, int right, int y, int h, int room, const struct xz_rec_view *v) {
 int w=xz_rec_box(c,room,h,v);
 if(!w)return right;
 char clock[12];xz_rec_clock(clock,v->elapsed_ms);
 unsigned parts=parts_for(c,room,h,v->time_known,clock);
 int x=right-w,long_state=v->level==XZ_REC_LONG;
 /* A pill: rounded like no chip or strip on these screens, edged so it reads on any fill under it. */
 xz_round_rect(c,x,y,w,h,h/2,15,long_state?LONG_EDGE:CALM_EDGE);
 xz_round_rect(c,x+1,y+1,w-2,h-2,(h-2)/2,15,long_state?LONG_FILL:CALM_FILL);
 if(v->dot_lit)xz_disc(c,x+h/2,y+h/2,dot_r(h),long_state?REC_INK:REC_RED);
 if(!parts)return x;
 int tx=x+lead(h),ty=text_y(c,y,h);
 if(parts&XZ_REC_PART_WORD){
  xz_text(c,tx,ty,word(),1,8,REC_INK);
  tx+=xz_text_width(c,word(),1,8)+6;
 }
 if(parts&XZ_REC_PART_TIME)clock_text(c,tx,ty,clock,REC_INK);
 return x;
}

/* ---- CLASSIC top band ---- */
/* FNV-1a over the box, row by row. */
static uint32_t box_sum(const uint16_t *pixels, size_t stride, int x, int w) {
 uint32_t h=2166136261u;
 for(int y=0;y<XZ_TOP_BAR_H;y++)for(int i=0;i<w;i++){uint16_t v=pixels[(size_t)y*stride+(size_t)(x+i)];h=(h^(v&255u))*16777619u;h=(h^(v>>8))*16777619u;}
 return h;
}
void xz_rec_band_restore(struct xz_rec_band_pixels *saved, uint16_t *pixels, size_t stride) {
 if(!saved||!saved->valid||!pixels)return;
 saved->valid=0;
 if(box_sum(pixels,stride,saved->x,saved->w)!=saved->paint_sum)return;
 for(int y=0;y<XZ_TOP_BAR_H;y++)memcpy(pixels+(size_t)y*stride+saved->x,saved->base[y],(size_t)saved->w*sizeof(uint16_t));
}
int xz_rec_band_draw(uint16_t *pixels, size_t stride, int theme, int vj_shown, const struct xz_rec_view *v, struct xz_rec_band_pixels *saved) {
 if(!pixels||stride<800||!saved||!v||v->level==XZ_REC_NONE)return 0;
 /* As xz_top_bar_canvas (ui_chrome.c), kept here so the shell closure needs no MODS chrome: clipped to the band. */
 struct xz_canvas c={pixels,stride,800,XZ_TOP_BAR_H,theme};
 int left=vj_shown?XZ_REC_BAND_VJ_LEFT:XZ_REC_BAND_RIGHT-XZ_REC_BAND_W,room=XZ_REC_BAND_RIGHT-left;
 int w=xz_rec_box(c,room,XZ_TOP_BAR_H,v);
 if(!w)return 0;
 int x=XZ_REC_BAND_RIGHT-w;
 for(int y=0;y<XZ_TOP_BAR_H;y++)memcpy(saved->base[y],pixels+(size_t)y*stride+x,(size_t)w*sizeof(uint16_t));
 xz_rec_draw(c,XZ_REC_BAND_RIGHT,0,XZ_TOP_BAR_H,room,v);
 saved->paint_sum=box_sum(pixels,stride,x,w);saved->x=x;saved->w=w;saved->valid=1;
 return w;
}
