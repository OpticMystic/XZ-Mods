/* SPDX-License-Identifier: MIT */
#include "classic_column.h"
#include "ui_skin.h"
#include "deck_colour.h"
#include <stdio.h>
#include <string.h>

/* The stock tag look: navy tab, pale outline; the deck a side drives is pale blue with dark text. The deck's
   number sits in its badge (deck_colour.h) where the stock tag has its bar, as on its channel below. */
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
/* ---- 2 DECKS: a side's half in the stock look (classic_column.h) ----
   Measured on the retained stock capture: the DECK 2 tab is rows 161..199, a vertical navy gradient with a
   lighter top row, its right edge slanting from x 100 to x 114; source, key and the key line follow 46, 74
   and 101 rows below the tab's top, parted by dotted rules; DECK 1's are 143 rows higher. */
enum { STOCK_TOP=0x41506a, STOCK_A=0x293c52, STOCK_B=0x203041, STOCK_BOTTOM=0x394c62, STOCK_RULE=0x4a596a,
       STOCK_ICON=0xe6e6e6, STOCK_LABEL=0x9cb2cd, STOCK_DIM=0x7a8290, STOCK_ACCENT=0xacceff };
enum { HALF_TAB_H=39, HALF_BOX_X=4, HALF_BOX_Y=5, HALF_BOX_W=24, HALF_BOX_H=28 };
static int half_tab_y(int side) { return 18+side*XZ_CLASSIC_HALF_STEP; }
static void stock_tab(struct xz_canvas c, int ty) {
 for(int r=0;r<HALF_TAB_H;r++){
  uint32_t fill=r==0?STOCK_TOP:r==HALF_TAB_H-1?STOCK_BOTTOM:xz_blend(STOCK_A,STOCK_B,(unsigned)(255*r/(HALF_TAB_H-1)));
  xz_rect(c,0,ty+r,101+14*r/(HALF_TAB_H-1),1,fill);
 }
}
static void dotted(struct xz_canvas c, int y) { for(int x=2;x<116;x+=2)xz_rect(c,x,y,1,1,STOCK_RULE); }
int xz_classic_column_half_draw(uint16_t *pixels, size_t stride, size_t count, int deck, const struct xz_classic_column_deck *d) {
 if(!pixels||!d||(deck!=2&&deck!=3)||stride<800||stride>SIZE_MAX/480||count<stride*480)return 0;
 int side=deck-2,y0=xz_classic_column_half_y(side),ty=half_tab_y(side);char text[24];
 struct xz_canvas c={pixels,stride,XZ_CLASSIC_COLUMN_W,y0+xz_classic_column_half_h(side),0};
 xz_rect(c,0,y0,XZ_CLASSIC_COLUMN_W,xz_classic_column_half_h(side),0x000000);
 stock_tab(c,ty);
 xz_deck_box(c,HALF_BOX_X,ty+HALF_BOX_Y,HALF_BOX_W,HALF_BOX_H,deck,d->live,STOCK_A,2,0);
 snprintf(text,sizeof text,"DECK %d",deck+1);xz_text(c,HALF_BOX_X+HALF_BOX_W+6,ty+12,text,2,99,INK);
 usb(c,6,ty+48,STOCK_ICON);xz_ui_chrome_fit(c,26,ty+48,100,source_name(d->source),2,INK);
 dotted(c,ty+70);
 /* The stock key mark: a filled box with b# cut out of it. */
 xz_rect(c,4,ty+76,17,17,STOCK_ICON);xz_text(c,6,ty+80,"b#",1,99,0x000000);
 xz_ui_chrome_fit(c,28,ty+77,100,d->key[0]?d->key:"--",2,INK);
 dotted(c,ty+99);
 /* The key slot (play_key_classic.h): opens this deck's key controls, as the stock key strip does. */
 int ky=ty+101,x=6;
 xz_text(c,x,ky+3,"KEY",1,99,STOCK_LABEL);x+=xz_text_width(c,"KEY",1,99)+6;
 xz_ui_chrome_fit(c,x,ky+3,60,d->key[0]?d->key:"---",1,INK);
 xz_text(c,100,ky+3,">",1,99,STOCK_ACCENT);
 xz_text(c,6,ky+19,"SOFTWARE DECK",1,99,STOCK_DIM);
 return 1;
}
void xz_classic_column_half_identity(uint16_t *pixels, size_t stride, size_t count, int deck) {
 if(!pixels||(deck!=2&&deck!=3)||stride<800||stride>SIZE_MAX/480||count<stride*480)return;
 struct xz_canvas c={pixels,stride,XZ_CLASSIC_COLUMN_W,480,0};
 xz_deck_box(c,HALF_BOX_X,half_tab_y(deck-2)+HALF_BOX_Y,HALF_BOX_W,HALF_BOX_H,deck,1,STOCK_A,2,0);
}
int xz_classic_column_draw(uint16_t *pixels, size_t stride, size_t count, const struct xz_classic_column_view *v) {
 if(!pixels||!v||v->layout==XZ_LAYOUT_TWO||(unsigned)v->layout>=XZ_LAYOUT_COUNT||stride<800||stride>SIZE_MAX/480||count<stride*480)return 0;
 struct xz_canvas c={pixels,stride,XZ_CLASSIC_COLUMN_W,XZ_CLASSIC_COLUMN_Y+XZ_CLASSIC_COLUMN_H,0};
 xz_rect(c,0,XZ_CLASSIC_COLUMN_Y,XZ_CLASSIC_COLUMN_W,XZ_CLASSIC_COLUMN_H,0x000000);
 const unsigned char *order=xz_deck_layout_order(v->layout);
 for(int k=0;k<4;k++)deck_tag(c,k,order[k],&v->deck[order[k]]);
 return 1;
}
