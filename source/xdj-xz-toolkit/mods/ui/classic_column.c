/* SPDX-License-Identifier: MIT */
#include "classic_column.h"
#include "ui_skin.h"
#include "deck_colour.h"
#include <stdio.h>
#include <string.h>

/* The stock tag look: navy tab, pale outline; the deck a side drives is pale blue with dark text. The deck's
   number sits in its colour box (deck_colour.h) where the stock tag has its bar, as on its channel below. */
enum { TAB=0x29344a, TAB_EDGE=0xacaeb4, DRIVEN=0x94bef6, DRIVEN_INK=0x081420, INK=0xffffff, SOFT=0xc8ccd0,
       CENTS=0xffb020, SOFTWARE=0x7a8290, OFF_TAB=0x161c26, OFF_EDGE=0x3a4250, OFF_INK=0x5a6270,
       AMBER=0xffa82e, OFF_NOTE=0x6a7280 };

/* The tab: (2,ty) (104,ty) (115,ty+21) (2,ty+21), filled, with a one-pixel outline. */
static void tab(struct xz_canvas c, int ty, uint32_t fill, uint32_t edge) {
 for(int r=0;r<22;r++){
  int right=104+11*r/21;
  xz_rect(c,2,ty+r,right-2,1,fill);
  xz_rect(c,right,ty+r,1,1,edge);
 }
 xz_rect(c,2,ty,103,1,edge);xz_rect(c,2,ty+21,114,1,edge);xz_rect(c,2,ty,1,22,edge);
}
/* The stock USB mark, 11 x 15. */
static void usb(struct xz_canvas c, int x, int y, uint32_t col) {
 xz_rect(c,x+4,y+2,2,10,col);xz_disc(c,x+5,y+13,2,col);
 xz_rect(c,x+4,y,2,2,col);xz_rect(c,x+3,y+1,4,1,col);
 xz_rect(c,x,y+5,4,2,col);xz_rect(c,x,y+5,2,4,col);xz_disc(c,x+1,y+9,1,col);
 xz_rect(c,x+6,y+3,4,2,col);xz_rect(c,x+8,y+3,2,4,col);xz_rect(c,x+8,y+7,2,2,col);
}
static const char *source_name(int source) {
 return source==1?"USB1":source==2?"USB2":source==3?"LINK":source==4?"PC":"--";
}
static void deck_tag(struct xz_canvas c, int k, int id, const struct xz_classic_column_deck *d) {
 int ty=22+68*k;char text[24];
 int live=d->live,driven=live&&d->driven;
 uint32_t fill=driven?DRIVEN:live?TAB:OFF_TAB;
 tab(c,ty,fill,live?TAB_EDGE:OFF_EDGE);
 xz_deck_box(c,4,ty+2,20,18,id,live,fill,2,0);
 xz_text(c,28,ty+4,"DECK",2,99,driven?DRIVEN_INK:live?INK:OFF_INK);
 if(driven)xz_text(c,74,ty+8,(id&1)?"RIGHT":"LEFT",1,99,DRIVEN_INK);
 if(!live){
  xz_text(c,6,ty+28,"NOT CONNECTED",1,99,AMBER);
  xz_text(c,6,ty+43,"SEE THE CARD",1,99,OFF_NOTE);
  return;
 }
 usb(c,4,ty+27,INK);
 xz_ui_chrome_fit(c,19,ty+28,45,source_name(d->source),2,INK);
 xz_border(c,66,ty+27,15,15,INK);xz_text(c,68,ty+30,"b#",1,99,INK);
 xz_ui_chrome_fit(c,86,ty+28,42,d->key[0]?d->key:"--",2,INK);
 if(id>=2){xz_text(c,6,ty+48,"SOFTWARE DECK",1,99,SOFTWARE);return;}
 int x=6;
 if(d->original[0]){
  xz_text(c,x,ty+48,d->original,1,99,SOFT);x+=xz_text_width(c,d->original,1,99)+4;
  for(int r=0;r<7;r++){int w=r<4?r+1:7-r;xz_rect(c,x,ty+49+r,w,1,SOFT);}
  x+=8;
 }
 if(d->key[0]){xz_text(c,x,ty+48,d->key,1,99,INK);x+=xz_text_width(c,d->key,1,99)+8;}
 if(d->cents_known&&d->cents){
  int cents=d->cents<-50?-50:d->cents>50?50:d->cents;
  snprintf(text,sizeof text,"%+dc",cents);
  xz_text(c,x>70?x:70,ty+48,text,1,99,CENTS);
 }
}
int xz_classic_column_draw(uint16_t *pixels, size_t stride, size_t count, const struct xz_classic_column_view *v) {
 if(!pixels||!v||v->layout==XZ_LAYOUT_TWO||(unsigned)v->layout>=XZ_LAYOUT_COUNT||stride<800||stride>SIZE_MAX/480||count<stride*480)return 0;
 struct xz_canvas c={pixels,stride,XZ_CLASSIC_COLUMN_W,XZ_CLASSIC_COLUMN_Y+XZ_CLASSIC_COLUMN_H,0};
 xz_rect(c,0,XZ_CLASSIC_COLUMN_Y,XZ_CLASSIC_COLUMN_W,XZ_CLASSIC_COLUMN_H,0x000000);
 const unsigned char *order=xz_deck_layout_order(v->layout);
 for(int k=0;k<4;k++)deck_tag(c,k,order[k],&v->deck[order[k]]);
 return 1;
}
