/* SPDX-License-Identifier: MIT */
/* The native Shortcut screen's WAVEFORM COLOR group (BLUE | RGB, x 350-489), redrawn
 * in its own style as four segments that also offer the layered 3-BAND and STEMS
 * scrolling waveforms. Drawn over the stock surface just before each flip. */
#include "ui_skin.h"

enum {SX=350,SY=226,SW=62,SH=53,SEGS=4,ICON_Y=SY+12,ICON_H=11};
#define BORDER 0x9caaacu
#define SELECTED_FILL 0x9caaacu
#define SELECTED_INK 0x101418u
#define IDLE_TOP 0x293c52u
#define IDLE_BOTTOM 0x203041u
#define IDLE_INK 0x94999cu
#define ICON_EDGE 0x181c20u

int xz_ui_shortcut_wave_hit(int x,int y){
 if(y<SY||y>=SY+SH||x<SX||x>=SX+SW*SEGS)return -1;
 return (x-SX)/SW;
}
static void icon(struct xz_canvas c,int segment,int x,int w,const uint32_t stem_rgb[3]){
 if(segment==0){xz_rect(c,x,ICON_Y,w,ICON_H,ICON_EDGE);xz_rect(c,x+1,ICON_Y+1,w-2,ICON_H-2,0x206dc5);return;}
 if(segment==1){
  static const uint32_t rainbow[10]={0xff1a1a,0xff7a10,0xe8e810,0x80d020,0x10c040,0x10d8d0,0x1070e0,0x1020a0,0x6010b0,0xc010c0};
  xz_rect(c,x,ICON_Y,w,ICON_H,ICON_EDGE);
  for(int i=0;i<10;i++){int a=x+1+(w-2)*i/10,b=x+1+(w-2)*(i+1)/10;xz_rect(c,a,ICON_Y+1,b-a,ICON_H-2,rainbow[i]);}
  return;
 }
 /* Three solid blocks like the RGB bar: the 3-band layer colours, or the stem pad colours. */
 static const uint32_t bands[3]={0x2053d9,0xf2aa3c,0xffffff};
 const uint32_t *colors=segment==2?bands:stem_rgb;
 xz_rect(c,x,ICON_Y,w,ICON_H,ICON_EDGE);
 for(int i=0;i<3;i++){int a=x+1+(w-2)*i/3,b=x+1+(w-2)*(i+1)/3;xz_rect(c,a,ICON_Y+1,b-a,ICON_H-2,colors[i]);}
 /* Dark separators keep neighbouring colours (white beside amber, two blues) distinct. */
 for(int i=1;i<3;i++)xz_rect(c,x+1+(w-2)*i/3,ICON_Y+1,1,ICON_H-2,ICON_EDGE);
}
void xz_ui_render_shortcut_wave(uint16_t *pixels,size_t stride,int selected,const uint32_t stem_rgb[3]){
 if(!pixels||stride<800||!stem_rgb)return;
 struct xz_canvas c={pixels,stride,800,480,0};
 static const char *const labels[SEGS]={"BLUE","RGB","3BAND","STEMS"};
 for(int s=0;s<SEGS;s++){
  int x=SX+s*SW,on=s==selected;
  if(on)xz_rect(c,x,SY,SW,SH,SELECTED_FILL);
  else for(int y=0;y<SH;y++)xz_rect(c,x,SY+y,SW,1,xz_blend(IDLE_TOP,IDLE_BOTTOM,(unsigned)(y*256/SH)));
  icon(c,s,x+8,SW-16,stem_rgb);
  int scale=xz_text_width(c,labels[s],2,SW)<=SW-6?2:1;
  int tw=xz_text_width(c,labels[s],scale,SW);
  xz_text(c,x+(SW-tw)/2,SY+29,labels[s],scale,SW,on?SELECTED_INK:IDLE_INK);
 }
 for(int s=1;s<SEGS;s++)xz_rect(c,SX+s*SW,SY+1,1,SH-2,0x181c20);
 xz_border(c,SX,SY,SW*SEGS,SH,BORDER);
}
