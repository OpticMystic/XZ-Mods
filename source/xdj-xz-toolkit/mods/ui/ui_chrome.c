/* SPDX-License-Identifier: MIT */
/* Chrome shared by every MODS skin: section icons, the hardware focus ring,
   toggle switches and the info dock that explains the focused control. */
#include "ui_skin.h"
#include "deck_colour.h"
#include <stdio.h>
#include <string.h>

static void line(struct xz_canvas c,int x0,int y0,int x1,int y1,int width16,uint32_t color){
 xz_line16(c,x0*16,y0*16,x1*16,y1*16,width16,color);
}
/* A 2 px rounded outline: straight runs plus each corner's quadrant of a ring. */
static void outline(struct xz_canvas c,int x,int y,int w,int h,int r,uint32_t color){
 if(w<2*r||h<2*r)r=(w<h?w:h)/2;
 xz_rect(c,x+r,y,w-2*r,2,color);xz_rect(c,x+r,y+h-2,w-2*r,2,color);
 xz_rect(c,x,y+r,2,h-2*r,color);xz_rect(c,x+w-2,y+r,2,h-2*r,color);
 struct xz_canvas q;
 q=xz_sub(c,x,y,r,r);xz_ring(q,r,r,r,2,color);
 q=xz_sub(c,x+w-r,y,r,r);xz_ring(q,0,r,r,2,color);
 q=xz_sub(c,x,y+h-r,r,r);xz_ring(q,r,0,r,2,color);
 q=xz_sub(c,x+w-r,y+h-r,r,r);xz_ring(q,0,0,r,2,color);
}
/* 24 px line icons centred on (cx,cy), drawn from primitives so every theme can tint them. */
void xz_ui_chrome_icon(struct xz_canvas c,enum xz_ui_page page,int cx,int cy,uint32_t color){
 switch(page){
 case XZ_UI_HOME:
  /* A house: roof, walls, door. */
  for(int r=0;r<9;r++)xz_rect(c,cx-r,cy-8+r,2*r+1,1,color);
  xz_rect(c,cx-6,cy+1,12,8,color);xz_rect(c,cx-2,cy+4,4,5,0);
  break;
 case XZ_UI_PLAY:case XZ_UI_BEAT_JUMP:
  /* The pad grid, two rows of four. */
  for(int k=0;k<8;k++)xz_rect(c,cx-10+(k%4)*6,cy-5+(k/4)*6,4,4,color);
  break;
 case XZ_UI_STEM_SECTION:case XZ_UI_STEM_PADS:
  /* Three stem bars. */
  {static const int h[3]={10,16,12};for(int k=0;k<3;k++)xz_rect(c,cx-8+k*6,cy+7-h[k],4,h[k],color);}
  break;
 case XZ_UI_SCREEN:
  xz_border(c,cx-10,cy-7,20,13,color);xz_border(c,cx-9,cy-6,18,11,color);xz_rect(c,cx-4,cy+7,8,2,color);
  break;
 case XZ_UI_FOUR_DECKS:
  /* Four lanes, the lower two dimmer. */
  for(int k=0;k<4;k++)xz_rect(c,cx-9,cy-7+k*4,18,2,k<2?color:xz_blend(0,color,110));
  break;
 case XZ_UI_CONNECT:
  xz_ring(c,cx-4,cy,5,2,color);xz_ring(c,cx+4,cy,5,2,color);
  break;
 case XZ_UI_PROFILE_SETUP:case XZ_UI_PROFILE_EDIT:
  /* Head and shoulders. */
  xz_disc(c,cx,cy-5,5,color);
  {struct xz_canvas q=xz_sub(c,cx-10,cy+2,21,9);xz_disc(q,10,10,10,color);}
  break;
 case XZ_UI_NETWORK:case XZ_UI_NETWORK_SETUP:case XZ_UI_NETWORK_EDIT:
  /* Three linked nodes. */
  line(c,cx-8,cy+6,cx,cy-6,24,color);line(c,cx,cy-6,cx+8,cy+6,24,color);line(c,cx-8,cy+6,cx+8,cy+6,24,color);
  xz_disc(c,cx,cy-6,3,color);xz_disc(c,cx-8,cy+6,3,color);xz_disc(c,cx+8,cy+6,3,color);
  break;
 case XZ_UI_SUPPORT:
  /* A report: a page with lines. */
  xz_border(c,cx-8,cy-10,16,20,color);xz_border(c,cx-7,cy-9,14,18,color);
  for(int k=0;k<3;k++)xz_rect(c,cx-4,cy-5+k*5,8,2,color);
  break;
 case XZ_UI_ABOUT:
  xz_ring(c,cx,cy,10,2,color);xz_rect(c,cx-1,cy-6,3,3,color);xz_rect(c,cx-1,cy-1,3,8,color);
  break;
 case XZ_UI_CONTROLS:case XZ_UI_STEMS:case XZ_UI_XPAD:
  /* Performance pads: two rows of four, split into pairs like the hardware. */
  for(int k=0;k<8;k++){int x=cx-13+(k%4)*6+((k%4)/2)*2,y=cy-6+(k/4)*7;xz_round_rect(c,x,y,5,5,1,15,color);}
  break;
 case XZ_UI_THEMES:case XZ_UI_STEM_COLORS:case XZ_UI_THEME_PICK:case XZ_UI_BAND_COLORS:
  /* Contrast disc: one half solid. */
  xz_ring(c,cx,cy,11,2,color);
  for(int y=-9;y<=9;y++){int half=0;while(half*half+y*y<=81)half++;xz_rect(c,cx,cy+y,half,1,color);}
  break;
 case XZ_UI_SETTINGS:
  /* Three faders at different positions. */
  for(int k=0;k<3;k++){int y=cy-7+k*7,knob=cx-8+((k*11+5)%17);
   xz_rect(c,cx-12,y,24,2,color);xz_round_rect(c,knob-3,y-3,7,8,2,15,color);}
  break;
 case XZ_UI_EXTRAS:
  /* Four-point spark. */
  line(c,cx,cy-12,cx,cy+12,28,color);line(c,cx-12,cy,cx+12,cy,28,color);
  line(c,cx-6,cy-6,cx+6,cy+6,20,color);line(c,cx-6,cy+6,cx+6,cy-6,20,color);
  xz_disc(c,cx,cy,3,color);
  break;
 case XZ_UI_CONNECTION:
  /* Monitor on a stand. */
  xz_rect(c,cx-13,cy-10,26,2,color);xz_rect(c,cx-13,cy+5,26,2,color);
  xz_rect(c,cx-13,cy-10,2,17,color);xz_rect(c,cx+11,cy-10,2,17,color);
  xz_rect(c,cx-1,cy+7,3,5,color);xz_rect(c,cx-7,cy+11,15,2,color);
  break;
 case XZ_UI_GAMES:case XZ_UI_DOOM:case XZ_UI_DOOM_FILES:case XZ_UI_DOOM_SETUP:case XZ_UI_MYHOUSE_SETUP:
  /* Game pad: body, cross and two buttons. */
  xz_round_rect(c,cx-14,cy-8,28,16,8,15,color);
  xz_rect(c,cx-9,cy-1,8,2,0);xz_rect(c,cx-6,cy-4,2,8,0);
  xz_disc(c,cx+6,cy-2,2,0);xz_disc(c,cx+9,cy+2,2,0);
  break;
 case XZ_UI_APPS:case XZ_UI_APPS_SETUP:
  for(int k=0;k<9;k++)xz_round_rect(c,cx-10+(k%3)*8,cy-10+(k/3)*8,5,5,1,15,color);
  break;
 case XZ_UI_UPDATES:case XZ_UI_UPDATE_SETUP:
  /* Download into a tray. */
  xz_rect(c,cx-1,cy-12,3,14,color);line(c,cx-7,cy-3,cx,cy+4,32,color);line(c,cx+7,cy-3,cx,cy+4,32,color);
  xz_rect(c,cx-12,cy+4,2,8,color);xz_rect(c,cx+10,cy+4,2,8,color);xz_rect(c,cx-12,cy+10,24,2,color);
  break;
 case XZ_UI_KEY_SETUP:case XZ_UI_BROWSE_SETUP:
  /* Key: bow and bit. */
  xz_ring(c,cx-6,cy,6,2,color);xz_rect(c,cx,cy-1,13,3,color);xz_rect(c,cx+7,cy+1,2,5,color);xz_rect(c,cx+11,cy+1,2,4,color);
  break;
 case XZ_UI_RAM_SETUP:
  /* Memory chip. */
  xz_round_rect(c,cx-9,cy-8,18,16,2,15,color);
  for(int k=0;k<4;k++){xz_rect(c,cx-7+k*4,cy-12,2,4,color);xz_rect(c,cx-7+k*4,cy+8,2,4,color);}
  break;
 default:
  xz_ring(c,cx,cy,10,2,color);xz_disc(c,cx,cy,3,color);
  break;}
}
void xz_ui_chrome_switch(struct xz_canvas c,int x,int y,int on,int available,uint32_t track,uint32_t knob,uint32_t accent){
 uint32_t fill=!available?xz_blend(track,knob,30):on?accent:track;
 xz_round_rect(c,x,y,36,20,10,15,fill);
 xz_disc(c,on?x+26:x+10,y+10,7,available?knob:xz_blend(track,knob,120));
}
/* Every top-bar painter draws through this canvas, so no frame style can reach the stock furniture below. */
struct xz_canvas xz_top_bar_canvas(uint16_t *pixels,size_t stride,int theme){
 return (struct xz_canvas){pixels,stride,800,XZ_TOP_BAR_H,theme};
}
struct xz_top_face xz_top_bar_label(struct xz_canvas c,int x,int w,const char *label,uint32_t fill,uint32_t ink){
 int tw=xz_text_width(c,label,1,40),tx=x+(w-tw)/2;
 xz_text(c,tx,xz_theme_pixel_text(c.theme)?XZ_TOP_BAR_PIXEL_Y:XZ_TOP_BAR_LABEL_Y,label,1,40,ink);
 return (struct xz_top_face){tx,tw,fill,ink};
}
void xz_top_bar_marks(struct xz_canvas c,struct xz_top_face stems,int layout){
 int current=layout>0&&layout<5?layout-1:4,pitch=stems.text_w/5,x=stems.text_x+(stems.text_w-5*pitch+1)/2;
 for(int i=0;i<5;i++)xz_rect(c,x+i*pitch,XZ_TOP_BAR_MARK_Y,pitch-1,2,i==current?stems.ink:xz_blend(stems.fill,stems.ink,80));
}
/* The 4 DECKS rail cell's amber dot: a four-deck view is chosen and decks 3 and 4 are not connected. */
int xz_ui_four_decks_attention(const struct xz_ui_model *m){
 return m&&m->deck_view!=0&&m->route.state!=XZ_UI_ROUTE_READY&&m->route.state!=XZ_UI_ROUTE_NO_ENGINE;
}
void xz_ui_chrome_rail_marks(const struct xz_ui_scene *s){
 if(!xz_ui_four_decks_attention(s->m))return;
 for(size_t i=0;i<s->n;i++)if(s->w[i].kind==XZ_UI_PANEL&&s->w[i].index==XZ_UI_FOUR_DECKS&&s->w[i].x<XZ_UI_RAIL_W)
  xz_disc(s->c,s->w[i].x+s->w[i].w-18,s->w[i].y+9,3,0xffa82e);
}
void xz_ui_chrome_focus(const struct xz_ui_scene *s,uint32_t color){
 struct xz_ui_widget a;
 if(!xz_ui_focused(s->u,s->m,&a))return;
 int x=a.x-3,y=a.y-3,w=a.w+6,h=a.h+6;
 /* Keep the ring on screen and out of the dock. */
 if(x<0){w+=x;x=0;}if(y<0){h+=y;y=0;}
 if(x+w>XZ_UI_WIDTH)w=XZ_UI_WIDTH-x;if(y+h>XZ_UI_DOCK_Y-2)h=XZ_UI_DOCK_Y-2-y;
 outline(s->c,x,y,w,h,8,color);
 if(s->u->adjusting){
  /* Turn arrows either side say the encoder now edits, not moves. */
  int cy=a.y+a.h/2;
  line(s->c,a.x-8,cy,a.x-3,cy-5,24,color);line(s->c,a.x-8,cy,a.x-3,cy+5,24,color);
  line(s->c,a.x+a.w+8,cy,a.x+a.w+3,cy-5,24,color);line(s->c,a.x+a.w+8,cy,a.x+a.w+3,cy+5,24,color);
 }
}
/* Greedy word wrap into at most `lines` rows of `width` px; returns rows used. */
int xz_ui_wrap_lines(struct xz_canvas c,int width,const char *text){
 char row[160];int used=0;
 while(text&&*text){
  while(*text==' ')text++;
  if(!*text)break;
  size_t fit=0,last_space=0,n=strlen(text);
  for(size_t k=1;k<=n&&k<sizeof(row);k++){
   memcpy(row,text,k);row[k]=0;
   if(xz_text_width(c,row,1,200)>width)break;
   fit=k;if(text[k]==' '||text[k]==0)last_space=k;
  }
  if(!fit)return 99;
  text+=fit==n?fit:last_space?last_space:fit;used++;
 }
 return used;
}
static int wrap(struct xz_canvas c,int x,int y,int width,int lines,int pitch,const char *text,uint32_t color){
 char row[160];int used=0;
 while(text&&*text&&used<lines){
  while(*text==' ')text++;
  size_t fit=0,last_space=0,n=strlen(text);
  for(size_t k=1;k<=n&&k<sizeof(row);k++){
   memcpy(row,text,k);row[k]=0;
   if(xz_text_width(c,row,1,200)>width)break;
   fit=k;if(text[k]==' '||text[k]==0)last_space=k;
  }
  if(!fit)break;
  size_t take=fit==n||used==lines-1?fit:last_space?last_space:fit;
  memcpy(row,text,take);row[take]=0;
  xz_text(c,x,y+used*pitch,row,1,200,color);
  text+=take;used++;
 }
 return used;
}
/* MODERN keeps a known stock counter error in view while MODS is open: one red row in place of the dock's title,
   with each deck's badge, the code and the firmware's own text (as the play screen's alert has them), and SHOW DECK,
   because a tap closes MODS onto that deck's alert and CHOOSE TRACK. The explanation and the right column stay. */
int xz_ui_strip_report(const struct xz_ui *u,const struct xz_ui_model *m,struct xz_ui_widget *out){
 unsigned errors=xz_ui_counter_errors(m);
 if(!u||u->page==XZ_UI_SUPPORT||(!errors&&!xz_ui_usb_hub_notice(m)))return 0;
 struct xz_canvas c={NULL,0,XZ_UI_WIDTH,XZ_UI_HEIGHT,m->theme};
 int right=XZ_UI_COUNTER_W-6,w=xz_text_width(c,"REPORT THIS",1,40)+20;
 if(errors)right-=xz_text_width(c,(errors&1)&&(errors&2)?"SHOW DECKS":"SHOW DECK",1,40)+20+6;
 *out=(struct xz_ui_widget){right-w,XZ_UI_COUNTER_Y,w,XZ_UI_COUNTER_H,XZ_UI_SUPPORT_ALERT,0,0,"REPORT THIS",-1};
 return 1;
}
/* REPORT THIS: an outline, so SHOW DECK stays the strip's main button. */
static int report_button(struct xz_canvas c,const struct xz_ui *u,const struct xz_ui_model *m,int fallback_x){
 struct xz_ui_widget r;
 if(!xz_ui_strip_report(u,m,&r))return fallback_x;
 outline(c,r.x,r.y+4,r.w,r.h-8,4,0xffffff);
 xz_text(c,r.x+10,r.y+(r.h-9)/2,r.label,1,40,0xffffff);
 return r.x-6;
}
static void counter_strip(struct xz_canvas c,const struct xz_ui *u,const struct xz_ui_model *m,unsigned errors){
 enum { FILL=0x5a0c0c, EDGE=0xff4619, TEXT=0xffc8b4, H=XZ_UI_COUNTER_H }; /* the play screen alert's colours */
 int y=XZ_UI_COUNTER_Y,ty=y+(H-9)/2,count=(errors&1)+(errors>>1&1);
 xz_rect(c,0,y,XZ_UI_COUNTER_W,H,FILL);
 xz_rect(c,0,y,4,H,EDGE);
 const char *go=count>1?"SHOW DECKS":"SHOW DECK";
 int go_w=xz_text_width(c,go,1,40)+20,go_x=XZ_UI_COUNTER_W-6-go_w;
 xz_round_rect(c,go_x,y+4,go_w,H-8,4,15,0xffffff);
 xz_text(c,go_x+10,ty,go,1,40,FILL);
 int end=report_button(c,u,m,go_x);
 int x=14,room=(end-12-x-(count-1)*20)/count;
 for(int d=0;d<2;d++){
  if(!(errors>>d&1))continue;
  const struct xz_ui_counter_error *e=&m->counter_error[d];
  xz_deck_box(c,x,y+(H-16)/2,16,16,d,1,FILL,1,0);
  int cx=x+24,code_w=xz_text_width(c,e->code,1,40),tx=cx+code_w+8;
  xz_text(c,cx,ty,e->code,1,40,0xffffff);
  xz_text(c,tx,ty,e->text,1,(x+room-tx)/6,TEXT); /* the limit is room in 6 px steps */
  x+=room+20;
 }
}
/* The E-8307 hub warning in the counter strip's place and colours: the firmware's code and text, then what it is
   about. No control: RESCAN and the cable steps live on the 4 DECKS page. */
static void hub_strip(struct xz_canvas c,const struct xz_ui *u,const struct xz_ui_model *m){
 enum { FILL=0x5a0c0c, EDGE=0xff4619, TEXT=0xffc8b4, H=XZ_UI_COUNTER_H };
 int y=XZ_UI_COUNTER_Y,ty=y+(H-9)/2,x=14;
 xz_rect(c,0,y,XZ_UI_COUNTER_W,H,FILL);
 xz_rect(c,0,y,4,H,EDGE);
 const char *colon=strchr(m->usb_hub,':'),*rest=colon?colon+1:"";
 while(*rest==' ')rest++;
 char code[12];snprintf(code,sizeof code,"%.*s",colon?(int)(colon-m->usb_hub):6,m->usb_hub);
 xz_text(c,x,ty,code,1,40,0xffffff);x+=xz_text_width(c,code,1,40)+8;
 xz_text(c,x,ty,rest,1,40,TEXT);x+=xz_text_width(c,rest,1,40)+14;
 int end=report_button(c,u,m,XZ_UI_COUNTER_W-8),tight=end<XZ_UI_COUNTER_W-8;
 /* With REPORT THIS beside it the reason says the same in fewer words. */
 const char *why=m->route.state==XZ_UI_ROUTE_READY?(tight?"FROM THE LOOPBACK CABLE. DECKS 3 AND 4 READY.":"FROM THE LOOPBACK CABLE. DECKS 3 AND 4 ARE READY."):
  tight?"FROM THE LOOPBACK CABLE OR A HUB IN A TOP PORT.":"FROM THE LOOPBACK CABLE OR A HUB IN A TOP USB PORT.";
 if(x<end-4)xz_text(c,x,ty,why,1,(end-x)/6,0xffffff);
}
void xz_ui_chrome_dock(const struct xz_ui_scene *s,uint32_t bg,uint32_t ink,uint32_t dim,uint32_t accent,uint32_t alarm){
 struct xz_canvas c=s->c;const struct xz_ui *u=s->u;const struct xz_ui_model *m=s->m;
 struct xz_ui_info info;xz_ui_describe(u,m,&info);
 int y=XZ_UI_DOCK_Y;
 xz_rect(c,0,y-8,XZ_UI_WIDTH,8,bg);
 xz_rect(c,0,y,XZ_UI_WIDTH,XZ_UI_HEIGHT-y,xz_blend(bg,ink,16));
 xz_rect(c,0,y,XZ_UI_WIDTH,1,xz_blend(bg,ink,60));
 xz_rect(c,0,y+1,4,XZ_UI_HEIGHT-y-1,info.available?accent:alarm);
 /* Left: what the focused control is and does; a deck's counter error takes the title row (MODERN). */
 unsigned errors=xz_ui_counter_errors(m);int hub=xz_ui_usb_hub_notice(m);
 int title_w=xz_text_width(c,info.title,2,40);if(title_w>360)title_w=360;
 if(errors)counter_strip(c,u,m,errors);
 else if(hub)hub_strip(c,u,m);
 else xz_text(c,16,y+6,info.title,title_w<360?2:1,40,ink);
 if(!errors&&!hub&&info.state&&info.state[0]){
  int chip_w=xz_text_width(c,info.state,1,20)+16,chip_x=16+title_w+12;
  uint32_t tone=!info.available?alarm:(info.selected||!strcmp(info.state,"ADJUSTING"))?accent:dim;
  xz_round_rect(c,chip_x,y+7,chip_w,16,8,15,xz_blend(bg,tone,70));
  xz_text(c,chip_x+8,y+11,info.state,1,20,tone==dim?ink:tone);
 }
 /* Three lines say what it does (the room MODERN's detail pane gave); the last result (a notice) takes the
    third. The save state stays on the right. */
 int noticed=u->notice[0]&&xz_ui_section_page_notice(u);
 wrap(c,16,y+XZ_UI_DOCK_BODY_Y,XZ_UI_DOCK_BODY_W,noticed?XZ_UI_DOCK_LINES-1:XZ_UI_DOCK_LINES,14,info.body,dim);
 if(noticed)wrap(c,16,y+XZ_UI_DOCK_BODY_Y+14*(XZ_UI_DOCK_LINES-1),XZ_UI_DOCK_BODY_W,1,14,u->notice,alarm);
 /* Right: which decks it works on (or the save state), why a deck is left out, then the hardware keys.
    A fresh save result takes the first two lines. */
 xz_rect(c,560,y+10,1,48,xz_blend(bg,ink,40));
 const char *status=xz_ui_footer_text(u,m);
 int news=m->settings_flash||(m->dj&&m->dj->flash&&m->dj->status)||(u->notice[0]&&!noticed);
 int ky=y+40;
 if(info.feature&&!news){
  /* Chips, the save state, then the amber reason a deck is left out: DESIGN 3.2 and 3.4. */
  int x=574;
  x+=xz_text_width(c,"WORKS ON",1,20)+8;xz_text(c,574,y+9,"WORKS ON",1,20,dim);
  static const int order[4]={2,0,1,3};
  for(int k=0;k<4;k++){
   int d=order[k];char num[2]={(char)('1'+d),0};int lit=(info.works_on>>d)&1;
   /* Works there: the deck's badge (deck_colour.h), with an ink edge on a light theme. */
   if(lit)xz_deck_box(c,x,y+6,16,16,d,1,bg,1,xz_theme_luma(bg)>140?ink:0);
   else{xz_border(c,x,y+6,16,16,xz_blend(bg,ink,70));xz_text(c,x+(16-xz_text_width(c,num,1,2))/2,y+9,num,1,2,xz_blend(bg,ink,90));}
   x+=19;
  }
  wrap(c,574,y+26,218,1,13,status,dim);
  if(info.excluded){wrap(c,574,y+39,218,1,13,info.excluded,0xffa82e);ky=y+51;}
 }else wrap(c,574,y+8,218,2,13,status,u->notice[0]?alarm:dim);
 static const char *keys[3][2]={{"TURN","MOVE"},{"PUSH","SELECT"},{"BACK","UP"}};
 int x=574;
 for(int k=0;k<3;k++){
  const char *cap=k==0&&u->adjusting?"TURN":keys[k][0],*word=k==0&&u->adjusting?"EDIT":keys[k][1];
  int cw=xz_text_width(c,cap,1,8)+10;
  xz_round_rect(c,x,ky,cw,16,4,15,xz_blend(bg,ink,48));
  xz_text(c,x+5,ky+4,cap,1,8,ink);
  x+=cw+4;xz_text(c,x,ky+4,word,1,8,dim);x+=xz_text_width(c,word,1,8)+10;
 }
}
/* DJ Profile: the header chip reads like a signed-in user (logo, name, caret)
   and opens a list of every profile on the USBs. */
static const struct xz_ui_dj_entry *dj_entry(const struct xz_ui_model *m,int k){
 return m->dj&&k>=0&&k<m->dj->count&&k<XZ_UI_DJ_ENTRIES?&m->dj->entry[k]:NULL;
}
void xz_ui_chrome_colors(const struct xz_ui_model *m,const struct xz_theme_palette *p,uint32_t *bg,uint32_t *ink,uint32_t *accent){
 *ink=m->theme==XZ_THEME_ANALOG?0x3a3020:m->theme==XZ_THEME_FLAGSHIP?0xffffff:p->ink;
 /* Flagship chrome (profile chip, info dock) sits on the #191919 status band, not the black page. */
 *bg=m->theme==XZ_THEME_ANALOG?0xd9c9a3:m->theme==XZ_THEME_LCARS||m->theme==XZ_THEME_MATRIX?0:m->theme==XZ_THEME_FLAGSHIP?0x191919:p->bg;
 *accent=m->theme==XZ_THEME_MATRIX?0x33cc33:m->theme==XZ_THEME_FLAGSHIP?0x007de1:p->accent;
}
/* A logo scaled from src (src_side square RGB565), masked to a circle or a rounded
   square; a monogram on the profile's accent without one, a head and shoulders with no profile. */
void xz_ui_chrome_logo(struct xz_canvas c,int x,int y,int side,int circle,const uint16_t *src,int src_side,const struct xz_ui_dj_entry *e,uint32_t bg,uint32_t ink,uint32_t accent){
 int r=circle?side/2:side/5;
 if(src&&src_side>0){
  for(int py=0;py<side;py++)for(int px=0;px<side;px++){
   int dx=px<r?r-px:px>=side-r?px-(side-r-1):0,dy=py<r?r-py:py>=side-r?py-(side-r-1):0;
   if(dx&&dy&&dx*dx+dy*dy>r*r)continue;
   int cx=x+px,cy=y+py;if(cx<0||cy<0||cx>=c.width||cy>=c.height)continue;
   c.p[(size_t)cy*c.stride+(size_t)cx]=src[(py*src_side/side)*src_side+px*src_side/side];
  }
  if(circle)xz_ring(c,x+side/2,y+side/2,side/2,1,xz_blend(bg,ink,90));
  return;
 }
 uint32_t fill=e?(e->has_accent?e->accent:accent):xz_blend(bg,ink,60);
 if(circle)xz_disc(c,x+side/2,y+side/2,side/2,fill);else xz_round_rect(c,x,y,side,side,r,15,fill);
 /* Text and silhouette use whichever of black or white reads on the fill. */
 unsigned luma=((fill>>16&255)*3+(fill>>8&255)*6+(fill&255))/10;uint32_t mark=luma>140?0x101010:0xffffff;
 if(!e){
  xz_disc(c,x+side/2,y+side*3/8,side/6,mark);
  struct xz_canvas q=xz_sub(c,x,y+side*5/8,side,side*3/8);xz_disc(q,side/2,side*3/8,side*5/16,mark);
  return;
 }
 char letter[2]={e->name&&e->name[0]>' '&&(unsigned char)e->name[0]<128?e->name[0]:'?',0};
 if(letter[0]>='a'&&letter[0]<='z')letter[0]=(char)(letter[0]-32);
 int scale=side>=120?8:side>=36?3:2,w=xz_text_width(c,letter,scale,1);
 xz_text(c,x+(side-w)/2,y+(side-7*scale)/2,letter,scale,1,mark);
}
static void dj_caret(struct xz_canvas c,int x,int y,int up,uint32_t color){
 int d=up?-1:1;
 xz_line16(c,(x-5)*16,(y-2*d)*16,x*16,(y+3*d)*16,26,color);xz_line16(c,x*16,(y+3*d)*16,(x+5)*16,(y-2*d)*16,26,color);
}
static void dj_chip(const struct xz_ui_scene *s,const struct xz_ui_widget *a,uint32_t bg,uint32_t ink,uint32_t dim,uint32_t accent){
 struct xz_canvas c=s->c;const struct xz_ui_model *m=s->m;const struct xz_ui_dj *dj=m->dj;
 const struct xz_ui_dj_entry *e=dj_entry(m,dj->current);int open=s->u->profile_open,compact=a->w<100;
 xz_round_rect(c,a->x+4,a->y+5,a->w-8,30,15,15,open?xz_blend(bg,accent,70):xz_blend(bg,ink,26));
 if(open)outline(c,a->x+4,a->y+5,a->w-8,30,15,accent);
 xz_ui_chrome_logo(c,a->x+7,a->y+7,26,1,e?e->thumb:NULL,XZ_UI_DJ_THUMB,e,bg,ink,accent);
 dj_caret(c,a->x+a->w-18,a->y+20,open,ink);
 if(compact)return;
 char where[24];const char *name,*line;
 if(e){name=e->name;snprintf(where,sizeof(where),"USB %d",e->usb+1);line=where;}
 else{name="NO PROFILE";line=dj->reading?"READING USB":"TAP TO SET UP";}
 xz_ui_chrome_fit(c,a->x+40,a->y+9,a->w-66,name,1,ink);
 xz_ui_chrome_fit(c,a->x+40,a->y+22,a->w-66,line,1,e?dim:accent);
}
static void dj_check(struct xz_canvas c,int x,int y,uint32_t color){
 xz_line16(c,x*16,y*16,(x+5)*16,(y+5)*16,36,color);xz_line16(c,(x+5)*16,(y+5)*16,(x+14)*16,(y-6)*16,36,color);
}
void xz_ui_chrome_profile(const struct xz_ui_scene *s,uint32_t bg,uint32_t ink,uint32_t dim,uint32_t accent,uint32_t alarm){
 struct xz_canvas c=s->c;const struct xz_ui *u=s->u;const struct xz_ui_model *m=s->m;
 if(!m->dj)return;
 if(u->profile_open){
  /* Everything below the header steps back behind the list. */
  for(int y=XZ_UI_HEAD+1;y<XZ_UI_DOCK_Y&&y<c.height;y++){
   uint16_t *row=c.p+(size_t)y*c.stride;
   for(int x=0;x<c.width;x++)row[x]=(uint16_t)((row[x]>>1)&0x7bef);
  }
  int x=XZ_UI_PROFILE_LIST_X,y=XZ_UI_PROFILE_LIST_Y,w=XZ_UI_PROFILE_LIST_W,h=xz_ui_profile_list_bottom(u,m)-y;
  xz_round_rect(c,x,y,w,h,12,15,xz_blend(bg,ink,70));
  xz_round_rect(c,x+1,y+1,w-2,h-2,11,15,xz_blend(bg,ink,12));
  xz_text(c,x+14,y+11,"DJ PROFILES ON YOUR USB",1,40,dim);
  if(!m->dj->count){
   xz_text(c,x+14,y+34,m->dj->reading?"READING USB...":m->dj->usb_present?"NO DJ PROFILE ON THIS USB":"INSERT YOUR USB",2,30,ink);
   xz_text(c,x+14,y+58,"SET UP ONCE AND IT TRAVELS WITH YOUR MUSIC",1,60,dim);
  }
 }
 for(size_t i=0;i<s->n;i++){
  const struct xz_ui_widget *a=&s->w[i];
  if(a->kind==XZ_UI_PROFILE){dj_chip(s,a,bg,ink,dim,accent);continue;}
  if(a->kind==XZ_UI_PROFILE_PICK){
   const struct xz_ui_dj_entry *e=dj_entry(m,a->index);if(!e)continue;
   int live=a->index==m->dj->current,pending=a->index==m->dj->pending;
   uint32_t tone=e->has_accent?e->accent:accent;
   xz_round_rect(c,a->x,a->y,a->w,a->h,10,15,xz_blend(bg,ink,live?34:22));
   if(live)xz_round_rect(c,a->x,a->y+10,4,a->h-20,2,15,tone);
   xz_ui_chrome_logo(c,a->x+8,a->y+6,40,0,e->thumb,XZ_UI_DJ_THUMB,e,bg,ink,accent);
   int text_w=a->w-56-(e->stick_active?32:12);
   xz_ui_chrome_fit(c,a->x+56,a->y+8,text_w,e->name,xz_text_width(c,e->name,2,99)<=text_w?2:1,e->damaged?dim:ink);
   char line[120];
   if(pending)snprintf(line,sizeof(line),"USB %d / SWITCHING...",e->usb+1);
   else if(e->damaged)snprintf(line,sizeof(line),"USB %d / FILE DAMAGED",e->usb+1);
   else if(e->tagline&&e->tagline[0])snprintf(line,sizeof(line),"USB %d / %s",e->usb+1,e->tagline);
   else snprintf(line,sizeof(line),"USB %d%s",e->usb+1,live?" / ON THE MASTER DECK":"");
   xz_ui_chrome_fit(c,a->x+56,a->y+33,text_w,line,1,e->damaged?alarm:pending?accent:dim);
   if(e->stick_active)dj_check(c,a->x+a->w-26,a->y+a->h/2,tone);
   continue;
  }
  if(a->kind==XZ_UI_PROFILE_PAGE||(a->kind==XZ_UI_PANEL&&a->index==XZ_UI_PROFILE_SETUP&&u->profile_open)){
   int primary=a->kind==XZ_UI_PANEL&&!m->dj->count;
   xz_round_rect(c,a->x,a->y,a->w,a->h,10,15,primary?accent:xz_blend(bg,ink,30));
   int tw=xz_text_width(c,a->label,1,40);
   xz_text(c,a->x+(a->w-tw)/2,a->y+a->h/2-4,a->label,1,40,primary?bg:ink);
  }
 }
}
