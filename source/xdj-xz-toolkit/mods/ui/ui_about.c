/* SPDX-License-Identifier: MIT */
#include "ui_about.h"
#include "about_assets.h"
#include "logo_assets.h"
#include <string.h>

/* The Cortex-A9 divides in software. Scaling used two divisions per pixel, which made this logo about a third
 * of an idle frame; it also runs every frame while a deck is empty. One division per column and per row, and a
 * straight row copy at 1:1, give the same pixels.
 * A built-in logo (art) stores only its rows that carry the mark; the rows above and below are its background
 * and are filled. Each built-in is made at the size it is shown, so its draw is a fill and a row copy. */
static void draw_rows(struct xz_canvas c,const struct xz_logo_art *art,const uint16_t *pixels,int side,int x,int y,int size){
 if(art)side=art->side;
 if(side<=0||side>512||size<=0||size>800)return;
 int first=x<0?-x:0,last=x+size>c.width?c.width-x:size;
 if(first>=last)return;
 uint16_t column[800];
 if(side!=size)for(int dx=first;dx<last;dx++)column[dx]=(uint16_t)(dx*side/size);
 for(int dy=0;dy<size;dy++){
  if(y+dy<0||y+dy>=c.height)continue;
  int row=side==size?dy:dy*side/size;
  const uint16_t *source;
  if(!art)source=pixels+(size_t)row*side;
  else if(row>=art->top&&row<art->top+art->rows)source=art->band+(size_t)(row-art->top)*side;
  else{xz_rect(c,x,y+dy,size,1,art->background);continue;}
  uint16_t *line=c.p+(size_t)(y+dy)*c.stride;
  if(side==size)memcpy(line+x+first,source+first,(size_t)(last-first)*sizeof(uint16_t));
  else for(int dx=first;dx<last;dx++)line[x+dx]=source[column[dx]];
 }
}
void xz_ui_logo_draw(struct xz_canvas c,const uint16_t *pixels,int side,int x,int y,int size){
 draw_rows(c,pixels?NULL:&xz_logo_idle,pixels,side,x,y,size);
}
void xz_about_place(struct xz_ui_widget *w,int index){
 w->x=148+(index%2)*204;w->y=184+(index/2)*42;w->w=196;w->h=36;
}
static void line(struct xz_canvas c,int x,int y,int width,const char *text,int size,uint32_t ink){
 xz_ui_chrome_fit(c,x,y,width,text,size,ink);
}
void xz_about_render(struct xz_canvas c,int selected,int focused,int lit,const char *loader){
 const uint32_t bg=0x0a0d15,ink=0xf5f7fb,dim=0xb8c3d5,gold=0xffe633;
 if((unsigned)selected>=XZ_ABOUT_LINKS)selected=0;
 xz_rect(c,136,42,656,362,bg);
 /* Made on this panel's colour, so the logo has no box around it. */
 draw_rows(c,&xz_logo_about,NULL,0,148,50,112);
 line(c,276,57,282,"XZMods.com",3,gold);
 line(c,276,94,282,"Created by VJ OpticMystic",1,ink);
 line(c,276,114,282,"Live VJ performances & custom visuals",1,dim);
 /* "Loader L-4F7K-2M9Q": the word dim, the ID that reports from this stick carry in ink. */
 if(loader&&!strncmp(loader,"Loader ",7)&&loader[7]!=':'){
  line(c,276,136,282,"Loader",1,dim);
  line(c,276+xz_text_width(c,"Loader ",1,8),136,282-xz_text_width(c,"Loader ",1,8),loader+7,1,ink);
 }else if(loader&&loader[0])line(c,276,136,282,loader,1,dim);
 line(c,148,164,410,"UPDATES, PROJECTS & BOOKINGS",1,dim);
 for(int i=0;i<XZ_ABOUT_LINKS;i++){
  struct xz_ui_widget w={0};xz_about_place(&w,i);
  xz_round_rect(c,w.x,w.y,w.w,w.h,5,15,i==selected?0x3d3811:0x1c2433);
  if(lit&&i==focused)xz_border(c,w.x,w.y,w.w,w.h,gold);
  line(c,w.x+12,w.y+12,w.w-24,xz_about_links[i].label,1,i==selected?gold:ink);
 }
 const struct xz_about_link *link=&xz_about_links[selected];
 line(c,578,60,202,"SCAN WITH YOUR PHONE",1,dim);
 line(c,578,84,202,link->label,1,gold);
 const struct xz_about_qr *qr=&xz_about_qrs[selected];int scale=196/(qr->size+8),side=(qr->size+8)*scale;
 int left=578+(196-side)/2,top=112+(196-side)/2;
 xz_rect(c,578,112,196,196,0xffffff);
 for(unsigned y=0;y<qr->size;y++)for(unsigned x=0;x<qr->size;x++){
  unsigned bit=(unsigned)(y*qr->size+x);
  if(qr->bits[bit/8]&(1u<<(bit%8)))xz_rect(c,left+(x+4)*scale,top+(y+4)*scale,scale,scale,0);
 }
 line(c,566,320,220,link->line1,1,ink);
 line(c,566,337,220,link->line2,1,ink);
 if(selected==6){
  line(c,148,362,632,"Optional support funds upkeep and future projects.",1,ink);
  line(c,148,380,632,"All features stay available. Thank you for using XZ Mods.",1,dim);
 }else if(selected==5){
  line(c,148,362,632,"Thanks: Sam Leone / OverCue, Tratosca, nsaintot & contributors.",1,ink);
  line(c,148,380,632,"Full credits, licenses and matching source: scan NOTICES.",1,dim);
 }else{
  line(c,148,362,632,"Thanks to the DJ community and everyone who tests XZ Mods.",1,ink);
  line(c,148,380,632,"Independent project. Not affiliated, supported or endorsed.",1,dim);
 }
}
