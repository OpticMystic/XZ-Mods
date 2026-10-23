/* SPDX-License-Identifier: MIT */
#include "ui_about.h"
#include "about_assets.h"

void xz_about_place(struct xz_ui_widget *w,int index){
 w->x=148+(index%2)*204;w->y=184+(index/2)*42;w->w=196;w->h=36;
}
static void line(struct xz_canvas c,int x,int y,int width,const char *text,int size,uint32_t ink){
 xz_ui_chrome_fit(c,x,y,width,text,size,ink);
}
void xz_about_render(struct xz_canvas c,int selected,int focused,int lit){
 const uint32_t bg=0x0a0d15,ink=0xf5f7fb,dim=0xb8c3d5,gold=0xffe633;
 if((unsigned)selected>=XZ_ABOUT_LINKS)selected=0;
 xz_rect(c,136,42,656,362,bg);
 for(int y=0;y<112;y++)for(int x=0;x<112;x++){
  unsigned v=xz_about_logo[y*112+x];
  xz_rect(c,148+x,50+y,1,1,(((v>>11)&31)*255/31<<16)|(((v>>5)&63)*255/63<<8)|((v&31)*255/31));
 }
 line(c,276,57,282,"XZMods.com",3,gold);
 line(c,276,94,282,"Created by VJ OpticMystic",1,ink);
 line(c,276,116,282,"Live VJ performances",1,dim);
 line(c,276,134,282,"& custom visuals",1,dim);
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
  line(c,148,362,632,"Thanks to AlphaTheta / Pioneer DJ and the DJ community.",1,ink);
  line(c,148,380,632,"Independent project. Not affiliated, supported or endorsed.",1,dim);
 }
}
