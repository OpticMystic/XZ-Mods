/* SPDX-License-Identifier: MIT */
#include "ram_notice.h"

int xz_ram_notice_height(int lane_h, int edge_rows) {
 int h=lane_h-2*edge_rows;
 return h>52?52:h<30?30:h;
}
/* A memory module seen from the front: a rounded board, three chips on it and a row of contacts below. */
void xz_ram_glyph(struct xz_canvas c, int x, int y, uint32_t ink, uint32_t fill) {
 xz_round_rect(c,x,y,30,16,3,15,ink);
 xz_rect(c,x+2,y+2,26,12,fill);
 for(int k=0;k<3;k++)xz_rect(c,x+4+k*8,y+4,6,8,ink);
 xz_rect(c,x+13,y+14,4,2,fill);                 /* the key notch in the contact edge */
 for(int k=0;k<7;k++)if(k!=3)xz_rect(c,x+2+k*4,y+17,2,5,ink);
}
static int fits(struct xz_canvas c, const char *s, int scale, int room) {
 return xz_text_width(c,s,scale,200)<=room;
}
void xz_ram_notice_draw(struct xz_canvas c, int x, int y, int w, int h) {
 if(w<120||h<24)return;
 xz_rect(c,x,y,w,h,XZ_CALM_FILL);
 xz_rect(c,x,y,w,1,XZ_CALM_LINE);xz_rect(c,x,y+h-1,w,1,XZ_CALM_LINE);
 xz_rect(c,x,y,4,h,XZ_CALM_EDGE);
 int two=h>=38,head_h=14,body_h=9,gap=8;
 int block=two?head_h+gap+body_h:head_h,top=y+(h-block)/2;
 int gx=x+14;
 xz_ram_glyph(c,gx,y+(h-22)/2,XZ_CALM_EDGE,XZ_CALM_FILL);
 int tx=gx+30+12,room=x+w-10-tx;
 if(room<40)return;
 const char *head=XZ_RAM_NOTICE_HEAD;
 int scale=fits(c,head,2,room)?2:1;
 xz_text(c,tx,top-1,head,scale,room/6,XZ_CALM_INK);
 if(!two)return;
 const char *body=fits(c,XZ_RAM_NOTICE_FULL,1,room)?XZ_RAM_NOTICE_FULL:XZ_RAM_NOTICE_SHORT;
 xz_text(c,tx,top+head_h+gap,body,1,room/6+1,XZ_CALM_SOFT);
}
void xz_ram_notice_two_lane(struct xz_canvas surface, int lane_y, int lane_h) {
 int h=xz_ram_notice_height(lane_h,10);
 xz_ram_notice_draw(surface,2,lane_y+(lane_h-h)/2,surface.width-4,h);
}
void xz_ram_notice_four_lane(struct xz_canvas surface, int lane_y) {
 xz_ram_notice_draw(surface,2,lane_y+18,surface.width-4,40);
}
/* A plug and its cord, 26 x 16. */
static void plug_glyph(struct xz_canvas c, int x, int y, uint32_t ink) {
 xz_rect(c,x,y+7,8,2,ink);                       /* cord */
 xz_round_rect(c,x+8,y+2,12,12,2,15,ink);         /* plug body */
 xz_rect(c,x+20,y+4,6,2,ink);xz_rect(c,x+20,y+10,6,2,ink); /* contacts */
}
void xz_hub_card_draw(struct xz_canvas c) {
 int w=c.width,h=c.height;
 if(w<60||h<16)return;
 xz_rect(c,0,0,w,h,XZ_CALM_FILL);
 xz_border(c,0,0,w,h,XZ_CALM_LINE);
 xz_rect(c,0,0,4,h,XZ_CALM_EDGE);
 int two=h>=44,head_h=14,body_h=9,gap=7,glyph=w>=200&&h>=20;
 int tx=glyph?14+26+10:12,room=w-8-tx;
 if(glyph)plug_glyph(c,14,(h-16)/2,XZ_CALM_EDGE);
 int scale=xz_text_width(c,XZ_HUB_CARD_HEAD,2,200)<=room&&h>=20?2:1;
 if(scale==1)head_h=9;
 int block=two?head_h+gap+body_h:head_h,top=(h-block)/2;
 xz_text(c,tx,top-(scale==2),XZ_HUB_CARD_HEAD,scale,room/6+1,XZ_CALM_INK);
 if(two)xz_text(c,tx,top+head_h+gap,XZ_HUB_CARD_LINE,1,room/6+1,XZ_CALM_SOFT);
}
