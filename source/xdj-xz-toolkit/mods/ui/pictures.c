/* SPDX-License-Identifier: MIT */
/* The MODS menu's pictures (pictures.h). Ported from the design mockups (design/_src, the Python scripts): the same
   geometry, drawn with the renderer's own primitives and fonts. */
#include "pictures.h"
#include "cable_anim.h"
#include "theme_flagship.h"
#include "wave_viewport.h"
#include "stem_pads.h"
#include "stem_colors.h"
#include "deck_colour.h"
#include "../shell/shell_internal.h"
#include <stdio.h>
#include <string.h>

/* The shell's numeral faces, where the shell is linked (the device library, the shell's tests); the CLASSIC
   panel's own tests draw numbers in the 20 px face instead. */
void sh_num(struct xz_canvas c, int x, int y, const char *s, enum sh_num_face face, uint32_t color) __attribute__((weak));
int sh_num_width(enum sh_num_face face, const char *s) __attribute__((weak));
int sh_num_drawable(enum sh_num_face face, const char *s) __attribute__((weak));

enum {
 M_ROW=0x181818, M_EDGE=0x3c3c3c, M_DIM=0xa4a4a4, M_GREY=0x8c8c8c, M_BLUE=0x007de1, M_GREEN=0x28e028,
 M_AMBER=0xffa82e, M_RED=0xff4619, STEM_DRUM=0x2f8cf0, STEM_HARM=0xf23a3a, STEM_VOC=0x2ecc52
};
struct xz_pic_style xz_pic_style_modern(void) {
 return (struct xz_pic_style){1,0,0x000000,M_ROW,M_EDGE,0xffffff,M_DIM,0x6a6a6a,M_BLUE,0xffffff,0xffffff,M_GREEN,M_AMBER,M_RED};
}
static unsigned luma(uint32_t c) { return (((c>>16)&255)*3+((c>>8)&255)*6+(c&255))/10; }
struct xz_pic_style xz_pic_style_classic(int theme) {
 const struct xz_theme_palette *p=xz_theme_palette(theme);
 uint32_t ink=theme==XZ_THEME_ANALOG?0x3a3020:p->ink,bg=theme==XZ_THEME_ANALOG?0xd9c9a3:p->bg;
 uint32_t accent=theme==XZ_THEME_MATRIX?0x33cc33:p->accent;
 struct xz_pic_style s={0,6,bg,xz_blend(bg,ink,16),xz_blend(bg,ink,48),ink,xz_blend(bg,ink,150),xz_blend(bg,ink,80),accent,
  luma(accent)>140?0x081008:0xffffff,accent,0x52d794,M_AMBER,p->alarm};
 if(theme==XZ_THEME_FLAGSHIP){s=xz_pic_style_modern();}
 return s;
}

/* ---- text ---- */
static int face_of(int size) { return size>=40?SH_NUM_BIG:size>=34?SH_NUM_BPM:SH_NUM_MID; }
static int numerals(struct xz_canvas c, const char *s, int size) {
 return size>=28&&sh_num&&sh_num_width&&sh_num_drawable&&!xz_theme_pixel_text(c.theme)&&sh_num_drawable((enum sh_num_face)face_of(size),s);
}
int xz_pic_text_width(struct xz_canvas c, const char *s, int size) {
 if(!s)return 0;
 if(numerals(c,s,size))return sh_num_width((enum sh_num_face)face_of(size),s);
 return xz_text_width(c,s,size>12?2:1,200);
}
int xz_pic_text(struct xz_canvas c, int x, int y, const char *s, int size, uint32_t color) {
 if(!s)return 0;
 if(numerals(c,s,size)){sh_num(c,x,y,s,(enum sh_num_face)face_of(size),color);return sh_num_width((enum sh_num_face)face_of(size),s);}
 /* Without the numeral faces a big number is the 20 px face, lowered to sit on the same baseline. */
 int drop=size>=40?14:size>=34?10:size>=28?6:0;
 xz_text(c,x,y+drop,s,size>12?2:1,200,color);
 return xz_text_width(c,s,size>12?2:1,200);
}
void xz_pic_text_right(struct xz_canvas c, int right, int y, const char *s, int size, uint32_t color) {
 xz_pic_text(c,right-xz_pic_text_width(c,s,size),y,s,size,color);
}
void xz_pic_text_center(struct xz_canvas c, int cx, int y, const char *s, int size, uint32_t color) {
 xz_pic_text(c,cx-xz_pic_text_width(c,s,size)/2,y,s,size,color);
}
int xz_pic_wrap(struct xz_canvas c, int x, int y, int width, int pitch, int lines, const char *text, uint32_t color) {
 char row[200];int used=0;
 while(text&&*text&&used<lines){
  while(*text==' ')text++;
  size_t n=strlen(text),fit=0,space=0;
  for(size_t k=1;k<=n&&k<sizeof row;k++){
   memcpy(row,text,k);row[k]=0;
   if(xz_text_width(c,row,1,200)>width)break;
   fit=k;if(text[k]==' '||!text[k])space=k;
  }
  if(!fit)break;
  size_t take=fit==n?fit:space?space:fit;
  memcpy(row,text,take);row[take]=0;
  xz_text(c,x,y+used*pitch,row,1,200,color);
  text+=take;used++;
 }
 return used;
}
/* A solid triangle pointing l, r, u or d, centred on (cx, cy), half = half its height. */
void xz_pic_triangle(struct xz_canvas c, int cx, int cy, int half, char d, uint32_t color) {
 for(int k=-half;k<=half;k++){
  int a=k<0?-k:k,len=2*(half-a)+1;
  if(d=='l')xz_rect(c,cx-half+2*a,cy+k,len>0?len:1,1,color);
  else if(d=='r')xz_rect(c,cx-half,cy+k,len>0?len:1,1,color);
  else if(d=='u')xz_rect(c,cx+k,cy-half+2*a,1,len>0?len:1,color);
  else xz_rect(c,cx+k,cy-half,1,len>0?len:1,color);
 }
}
void xz_pic_check(struct xz_canvas c, int x, int y, uint32_t color) {
 xz_line16(c,x*16,(y+4)*16,(x+3)*16,(y+7)*16+8,28,color);
 xz_line16(c,(x+3)*16,(y+7)*16+8,(x+9)*16,y*16,28,color);
}
void xz_pic_box(struct xz_canvas c, const struct xz_pic_style *s, struct xz_rect r, int selected, int outlined) {
 if(s->radius){
  if(selected)xz_round_rect(c,r.x,r.y,r.w,r.h,s->radius,15,s->select);
  else{xz_round_rect(c,r.x,r.y,r.w,r.h,s->radius,15,s->row_edge);xz_round_rect(c,r.x+1,r.y+1,r.w-2,r.h-2,s->radius-1,15,s->row);}
  return;
 }
 xz_rect(c,r.x,r.y,r.w,r.h,selected?s->select:s->row);
 if(outlined&&!selected)xz_border(c,r.x,r.y,r.w,r.h,s->row_edge);
}
/* A deck number box on a menu (deck_colour.h); a light theme gets a one-pixel ink edge round it. */
static void deck_box(struct xz_canvas c, const struct xz_pic_style *s, int x, int y, int size, int deck) {
 xz_deck_box(c,x,y,size,size,deck,1,s->bg,1,luma(s->bg)>140?s->ink:0);
}
int xz_pic_works_on(struct xz_canvas c, const struct xz_pic_style *s, int x, int y, int size, unsigned works_on, int label) {
 static const int order[4]={2,0,1,3};
 if(label)x+=xz_pic_text(c,x,y+(size-9)/2,"WORKS ON",12,s->dim)+8;
 for(int k=0;k<4;k++){
  int d=order[k];char n[2]={(char)('1'+d),0};
  /* Works there: the deck's colour box, as on the four-deck screens. Not there: a plain grey outline. */
  if(works_on&(1u<<d))deck_box(c,s,x,y,size,d);
  else{
   xz_border(c,x,y,size,size,s->modern?0x4a4a4a:s->faint);
   xz_pic_text_center(c,x+size/2,y+(size-9)/2,n,12,s->modern?M_GREY:s->faint);
  }
  x+=size+3;
 }
 return x;
}

/* ---- BEAT JUMP PADS ---- */
const int xz_pic_jump_ids[XZ_PIC_JUMP_CHIPS]={0,1,2,3,4,5,6,8,9};
void xz_pic_jump_geometry(int x, int y, int w, int bottom, struct xz_pic_jump_geo *g) {
 memset(g,0,sizeof *g);
 g->tabs[0]=(struct xz_rect){x,y,176,40};g->tabs[1]=(struct xz_rect){x+182,y,176,40};
 g->stock=(struct xz_rect){x+w-290,y,140,40};g->mine=(struct xz_rect){x+w-146,y,146,40};
 /* The sizes across the whole width at the bottom, one tap each; taller where the screen has the room. */
 int chip_h=bottom-y>320?52:44,cy=bottom-chip_h;
 for(int i=0;i<XZ_PIC_JUMP_CHIPS;i++){
  int a=x+i*(w+6)/XZ_PIC_JUMP_CHIPS,b=x+(i+1)*(w+6)/XZ_PIC_JUMP_CHIPS;
  g->chips[i]=(struct xz_rect){a,cy,b-a-6,chip_h};
 }
 /* The pads as on the unit, A B C D over E F G H, filling the height between the tabs and the sizes:
    8 apart inside a pair, 20 between pairs, the pair's frame 4 outside its pads. */
 int top=y+52,pw=(w-230)/4,ph=(cy-14-top-16)/2;
 for(int pad=0;pad<8;pad++){int col=pad%4;g->pads[pad]=(struct xz_rect){x+4+col*(pw+8)+(col/2)*12,top+(pad/4)*(ph+16),pw,ph};}
 for(int p=0;p<4;p++){struct xz_rect a=g->pads[2*p];g->frames[p]=(struct xz_rect){a.x-4,a.y-4,2*pw+16,ph+8};}
 g->name_x=g->pads[3].x+pw+24;g->name_y=top;g->name_h=2*ph+16;g->right=x+w;
}
static const char *const pad_letters="ABCDEFGH";
/* One pad: its letter and direction, the jump in beats, and under it in bars (4 beats = 1 bar). */
static void jump_pad(struct xz_canvas c, const struct xz_pic_style *s, struct xz_rect r, int pad, int beats, int focused, int stock) {
 uint32_t fill=focused?(s->modern?0x1f3550:xz_blend(s->row,s->select,60)):s->row;
 uint32_t well=s->modern?(focused?0x2a4668:0x262626):xz_blend(fill,s->ink,36);
 if(s->radius){xz_round_rect(c,r.x,r.y,r.w,r.h,s->radius,15,focused?s->select:s->row_edge);xz_round_rect(c,r.x+2,r.y+2,r.w-4,r.h-4,s->radius-1,15,fill);}
 else xz_rect(c,r.x,r.y,r.w,r.h,fill);
 uint32_t ink=stock?(s->modern?0x6a6a6a:s->faint):s->ink,dim=stock?ink:(s->modern?M_GREY:s->dim);
 char text[16]={pad_letters[pad],0};
 xz_pic_text(c,r.x+9,r.y+9,text,12,dim);
 xz_pic_triangle(c,r.x+24,r.y+13,4,pad&1?'r':'l',ink);
 xz_pic_text(c,r.x+32,r.y+9,pad&1?"FWD":"BACK",12,dim);
 int well_y=r.y+r.h-32,num_y=r.y+18+(well_y-r.y-18-20)/2;
 snprintf(text,sizeof text,"%d",beats);
 int nx=r.x+9+xz_pic_text(c,r.x+9,num_y,text,28,ink)+6;
 xz_pic_text(c,nx,num_y+11,beats==1?"BEAT":"BEATS",12,ink);
 if(s->radius)xz_round_rect(c,r.x+4,well_y,r.w-8,28,6,15,well);else xz_rect(c,r.x+4,well_y,r.w-8,28,well);
 if(beats>=4){
  snprintf(text,sizeof text,"%d",beats/4);
  nx=r.x+9+xz_pic_text(c,r.x+9,well_y+7,text,20,ink)+6;
  xz_pic_text(c,nx,well_y+12,beats==4?"BAR":"BARS",12,ink);
 }else xz_pic_text(c,r.x+9,well_y+10,beats==1?"1/4 BAR":"1/2 BAR",12,ink);
}
void xz_pic_jump_draw(struct xz_canvas c, const struct xz_pic_style *s, const struct xz_pic_jump_geo *g, const struct xz_pic_jump *given) {
 static const char *const tab_a[2]={"PAGE 1","PAGE 2"},*const tab_b[2]={"BEAT JUMP","SHIFT + BEAT JUMP"};
 /* the MODERN menu carries this data across threads: draw only what it can mean */
 struct xz_pic_jump safe=*given;const struct xz_pic_jump *j=&safe;
 safe.edit_page=safe.edit_page==1;if(safe.focus_pair<-1||safe.focus_pair>3)safe.focus_pair=-1;
 for(int i=0;i<2;i++){
  int sel=i==j->edit_page;struct xz_rect r=g->tabs[i];
  xz_pic_box(c,s,r,sel,0);
  int w=xz_pic_text(c,r.x+10,r.y+13,tab_a[i],20,sel?s->on_select:s->ink);
  xz_pic_text(c,r.x+10+w+7,r.y+17,tab_b[i],12,sel?s->on_select:s->dim);
 }
 xz_pic_box(c,s,g->stock,!j->enabled,0);
 xz_pic_text_center(c,g->stock.x+g->stock.w/2,g->stock.y+15,"PAGE 1: STOCK",12,!j->enabled?s->on_select:s->ink);
 xz_pic_box(c,s,g->mine,j->enabled,0);
 xz_pic_text_center(c,g->mine.x+g->mine.w/2,g->mine.y+15,"PAGE 1: MY SIZES",12,j->enabled?s->on_select:s->ink);
 /* Page 1 on STOCK plays the player's own sizes: shown, greyed, nothing chosen. */
 int stock=j->edit_page==0&&!j->enabled;
 struct xz_jump_settings defaults=xz_jump_defaults();
 int pair=j->focus_pair>=0?j->focus_pair:0;
 for(int p=0;p<4;p++){
  int foc=p==pair&&!stock;struct xz_rect f=g->frames[p];
  if(s->radius){
   uint32_t edge=foc?s->select:s->row_edge;
   xz_round_rect(c,f.x,f.y,f.w,f.h,9,15,edge);xz_round_rect(c,f.x+(foc?2:1),f.y+(foc?2:1),f.w-(foc?4:2),f.h-(foc?4:2),foc?7:8,15,s->bg);
  }else{
   xz_rect(c,f.x,f.y,f.w,f.h,s->bg);
   xz_border(c,f.x,f.y,f.w,f.h,foc?0xffffff:M_EDGE);if(foc)xz_border(c,f.x+1,f.y+1,f.w-2,f.h-2,0xffffff);
  }
  int beats=xz_jump_beats(stock?defaults.sizes[p]:j->sizes[j->edit_page*4+p]);
  jump_pad(c,s,g->pads[2*p],2*p,beats,foc,stock);
  jump_pad(c,s,g->pads[2*p+1],2*p+1,beats,foc,stock);
 }
 /* Beside the pads, centred on them: the chosen pair and how far it jumps. */
 int beats=xz_jump_beats(stock?defaults.sizes[pair]:j->sizes[j->edit_page*4+pair]);
 char text[48];
 if(stock){
  int y=g->name_y+(g->name_h-90)/2;
  xz_pic_text(c,g->name_x,y,"PAGE 1: STOCK",20,s->ink);
  xz_pic_wrap(c,g->name_x,y+30,g->right-g->name_x,15,4,"Page 1 jumps 1, 2, 4 and 8 beats, as the player does. Choose MY SIZES to set your own.",s->dim);
 }else{
  int y=g->name_y+(g->name_h-56)/2;
  snprintf(text,sizeof text,"PADS %c / %c",pad_letters[pair*2],pad_letters[pair*2+1]);
  xz_pic_text(c,g->name_x,y,text,20,s->ink);
  snprintf(text,sizeof text,"%d",beats);
  int vx=g->name_x+xz_pic_text(c,g->name_x,y+28,text,34,j->adjusting?s->select:s->ink)+8;
  xz_pic_text(c,vx,y+38,beats==1?"BEAT":"BEATS",20,s->ink);
 }
 for(int i=0;i<XZ_PIC_JUMP_CHIPS;i++){
  int sel=!stock&&xz_jump_beats(xz_pic_jump_ids[i])==beats;struct xz_rect r=g->chips[i];
  xz_pic_box(c,s,r,sel,0);
  snprintf(text,sizeof text,"%d",xz_jump_beats(xz_pic_jump_ids[i]));
  xz_pic_text_center(c,r.x+r.w/2,r.y+(r.h-14)/2,text,20,sel?s->on_select:stock?(s->modern?0x6a6a6a:s->faint):s->ink);
 }
}

/* ---- STEM COLOURS ---- */
void xz_pic_colour_geometry(int x, int y, int w, struct xz_pic_colour_geo *g) {
 memset(g,0,sizeof *g);
 int cw=(w-24)/3,sw=(cw-12)/3;
 for(int s=0;s<3;s++){
  int cx=x+s*(cw+12);g->name_x[s]=cx;
  for(int i=0;i<9;i++)g->swatches[s][i]=(struct xz_rect){cx+(i%3)*(sw+6),y+24+(i/3)*46,sw,40};
 }
 g->name_y=y+4;
 g->preview=(struct xz_rect){x,y+168,w,60};
 g->reset=(struct xz_rect){x,y+244,200,44};
 g->bass_x=x+216;g->bass_y=y+260;
}
static uint32_t ink_on(uint32_t rgb) { return luma(rgb)>140?0x000000:0xffffff; }
void xz_pic_colour_draw(struct xz_canvas c, const struct xz_pic_style *s, const struct xz_pic_colour_geo *g, const struct xz_pic_colours *p) {
 static const char *const names[3]={"DRUMS","HARMONICS","VOCALS"};
 uint32_t now[3];
 for(int stem=0;stem<3;stem++){
  int cur=p->current[stem]>=0&&p->current[stem]<9?p->current[stem]:0;now[stem]=p->rgb[stem][cur]&0xffffff;
  /* the stem's name in its colour, as the pads and buttons show it */
  xz_rect(c,g->name_x[stem],g->name_y+2,8,12,now[stem]);
  xz_pic_text(c,g->name_x[stem]+14,g->name_y+3,names[stem],12,now[stem]);
  for(int i=0;i<9;i++){
   struct xz_rect r=g->swatches[stem][i];uint32_t rgb=p->rgb[stem][i]&0xffffff;
   xz_rect(c,r.x,r.y,r.w,r.h,rgb);
   if(i==cur){for(int k=0;k<3;k++)xz_border(c,r.x-3+k,r.y-3+k,r.w+6-2*k,r.h+6-2*k,k==2?0x000000:s->select);xz_pic_check(c,r.x+r.w-14,r.y+5,ink_on(rgb));}
   xz_pic_text_center(c,r.x+r.w/2,r.y+r.h-14,xz_stem_palette_name(i),12,ink_on(rgb));
  }
 }
 /* the STEMS waveform with these colours: drums outside, harmonics, vocals inside */
 struct xz_rect w=g->preview;int mid=w.y+w.h/2;
 xz_rect(c,w.x,w.y,w.w,w.h,s->modern?0x101010:s->row);
 for(int x=0;x<w.w;x+=2){
  int a=w.h/2-4;int h=a/3+(int)((unsigned)(x*37+x/9*11)%(unsigned)(a-a/3+1));
  xz_rect(c,w.x+x,mid-h,1,h*2,now[0]);xz_rect(c,w.x+x,mid-h*2/3,1,h*4/3,now[1]);xz_rect(c,w.x+x,mid-h/3,1,h*2/3,now[2]);
 }
 xz_rect(c,g->reset.x,g->reset.y,g->reset.w,g->reset.h,s->modern?0x2a2a2a:s->row);
 xz_pic_text_center(c,g->reset.x+g->reset.w/2,g->reset.y+(g->reset.h-14)/2,"RESET",20,s->ink);
 xz_rect(c,g->bass_x,g->bass_y,8,12,p->bass&0xffffff);
 xz_pic_text(c,g->bass_x+14,g->bass_y+1,"BASS ON 4-STEM TRACKS FOLLOWS THE THEME",12,s->dim);
}

/* ---- STEM PADS ---- */
void xz_pic_stem_geometry(int x, int w, struct xz_pic_stem_geo *g) {
 memset(g,0,sizeof *g);g->x=x;g->w=w;
 int mw=(w-18)/4;
 for(int i=0;i<4;i++)g->modes[i]=(struct xz_rect){x+i*(mw+6),66,mw,44};
 for(int k=0;k<8;k++)g->pads[k]=(struct xz_rect){x+(k%4)*88+((k%4)/2)*8,156+(k/4)*78,82,70};
 int rx=x+370,rw=x+w-rx,half=(rw-4)/2,third=(rw-8)/3;
 for(int i=0;i<2;i++){g->bank[i]=(struct xz_rect){rx+i*(half+4),154,half,40};g->lights[i]=(struct xz_rect){rx+i*(half+4),298,half,40};}
 for(int i=0;i<3;i++)g->order[i]=(struct xz_rect){rx+i*(third+4),216,third,40};
 g->caption_y[0]=50;g->caption_y[1]=140;g->caption_y[2]=202;g->caption_y[3]=284;g->rule_y=326;
}
static void stem_pad(struct xz_canvas c, const struct xz_pic_style *s, struct xz_rect r, char letter, const char *title, const char *sub, uint32_t col, int lit) {
 if(!lit){
  if(s->radius){xz_round_rect(c,r.x,r.y,r.w,r.h,s->radius,15,s->row_edge);xz_round_rect(c,r.x+1,r.y+1,r.w-2,r.h-2,s->radius-1,15,s->row);}
  else xz_rect(c,r.x,r.y,r.w,r.h,s->row);
 }else{
  xz_rect(c,r.x,r.y,r.w,r.h,xz_blend(s->bg,col,70));xz_border(c,r.x,r.y,r.w,r.h,col);xz_border(c,r.x+1,r.y+1,r.w-2,r.h-2,col);
  xz_rect(c,r.x+6,r.y+r.h-5,r.w-12,2,col);
 }
 uint32_t ink=lit?(s->modern?0xffffff:s->ink):(s->modern?0x9a9a9a:s->dim),dim=lit?(s->modern?0xd0d0d0:s->ink):(s->modern?0x6a6a6a:s->faint);
 char l[2]={letter,0};
 xz_pic_text(c,r.x+8,r.y+7,l,12,dim);
 xz_pic_text(c,r.x+8,r.y+28,title,12,ink);
 xz_pic_text(c,r.x+8,r.y+r.h-18,sub,12,dim);
}
void xz_pic_stem_draw(struct xz_canvas c, const struct xz_pic_style *s, const struct xz_pic_stem_geo *g, const struct xz_pic_stem_pads *given) {
 struct xz_pic_stem_pads safe=*given;const struct xz_pic_stem_pads *p=&safe;
 if((unsigned)safe.order>=XZ_STEM_ORDER_COUNT)safe.order=0;
 safe.bank=safe.bank==1;
 static const char *const modes[4]={"HOT CUE","BEAT LOOP","SLIP LOOP","BEAT JUMP"};
 static const char *const stays[4]={"HOT CUES STAY","BEAT LOOPS STAY","SLIP LOOPS STAY","JUMPS STAY"};
 static const char *const normal[4]={"HOT CUE","LOOP","SLIP LOOP","JUMP"};
 static const char *const names[3]={"DRUMS","HARMONICS","VOCALS"};
 char text[80];
 int mode=p->mode>=0&&p->mode<4?p->mode:0;
 xz_pic_text(c,g->x,g->caption_y[0],"1   PICK THE PAD MODE BUTTON THAT GIVES STEM PADS",12,s->dim);
 for(int i=0;i<4;i++){
  struct xz_rect r=g->modes[i];int sel=i==mode;
  xz_pic_box(c,s,r,sel,0);
  xz_pic_text_center(c,r.x+r.w/2,r.y+10,modes[i],20,sel?s->on_select:s->ink);
  if(sel){int bw=(r.w-16)/3;for(int k=0;k<3;k++){int stem=xz_stem_at(p->order,k);xz_rect(c,r.x+8+k*(bw+2),r.y+r.h-6,bw,3,p->rgb[stem]);}}
  const char *sub=sel?(p->jump_wins&&i==3?"JUMPS WIN":p->bank?"E-H BECOME STEMS":"A-D BECOME STEMS"):stays[i];
  xz_pic_text_center(c,r.x+r.w/2,r.y+r.h+6,sub,12,sel&&p->jump_wins&&i==3?s->warn:sel?s->ink:s->dim);
 }
 snprintf(text,sizeof text,"2   IN %s MODE THE PADS DO THIS",modes[mode]);
 xz_pic_text(c,g->x,g->caption_y[1],text,12,s->dim);
 for(int k=0;k<8;k++){
  int slot=k%4,stems=k/4==p->bank;char letter=pad_letters[k];
  if(stems&&slot<3){int stem=xz_stem_at(p->order,slot);stem_pad(c,s,g->pads[k],letter,names[stem],"MUTE",p->rgb[stem],1);}
  else if(stems&&p->four)stem_pad(c,s,g->pads[k],letter,"BASS","MUTE (4-STEM)",p->rgb[3],1);
  else if(stems)stem_pad(c,s,g->pads[k],letter,"BYPASS","ORIGINAL",s->modern?0xffffff:s->ink,1);
  else stem_pad(c,s,g->pads[k],letter,normal[mode],"AS NORMAL",0,0);
 }
 xz_pic_text(c,g->bank[0].x,g->caption_y[1],"STEMS ON",12,s->dim);
 static const char *const banks[2]={"PADS A-D","PADS E-H"},*const lights[2]={"STEM COLOURS","OFF"};
 for(int i=0;i<2;i++){
  xz_pic_box(c,s,g->bank[i],i==p->bank,0);
  xz_pic_text_center(c,g->bank[i].x+g->bank[i].w/2,g->bank[i].y+13,banks[i],20,i==p->bank?s->on_select:s->ink);
  int on=i==0?!!p->lights:!p->lights;
  xz_pic_box(c,s,g->lights[i],on,0);
  xz_pic_text_center(c,g->lights[i].x+g->lights[i].w/2,g->lights[i].y+13,lights[i],20,on?s->on_select:s->ink);
 }
 xz_pic_text(c,g->bank[0].x,g->caption_y[2],"ORDER ON THE PADS AND THE SCREEN",12,s->dim);
 for(int i=0;i<3;i++){
  struct xz_rect r=g->order[i];int stem=xz_stem_at(p->order,i);uint32_t col=p->rgb[stem];
  xz_rect(c,r.x,r.y,r.w,r.h,xz_blend(s->bg,col,80));xz_border(c,r.x,r.y,r.w,r.h,col);xz_border(c,r.x+1,r.y+1,r.w-2,r.h-2,col);
  xz_pic_text_center(c,r.x+r.w/2,r.y+15,names[stem],12,s->modern?0xffffff:s->ink);
 }
 xz_pic_text(c,g->bank[0].x,g->caption_y[2]+62,p->focus==XZ_PIC_STEM_ORDER?"TURN TO CHANGE THE ORDER":"PUSH, THEN TURN TO CHANGE THE ORDER",12,s->modern?M_GREY:s->dim);
 xz_pic_text(c,g->bank[0].x,g->caption_y[3],"STEM PAD LIGHTS, DECKS 1 AND 2",12,s->dim);
 xz_pic_text(c,g->x,g->rule_y,"SHIFT + HOT CUE, BEAT LOOP OR SLIP LOOP ALWAYS GIVES STEM PADS.",12,s->modern?0xc8c8c8:s->ink);
}

/* ---- small pictures ---- */
static int wave_amp(int k, int seed, int half) {
 /* |sin| * |cos| shaped columns, deterministic, no floating point. */
 int a=xz_sin1024((k+seed*13)*12),b=xz_cos1024((k+seed*7)*3);
 a=a<0?-a:a;b=b<0?-b:b;
 return half*(358+(563*((a*b)>>10)>>10))>>10;
}
void xz_pic_mini_screen(struct xz_canvas c, int x, int y, int w, int h, int view, int modern, int decks34) {
 xz_rect(c,x,y,w,h,0x000000);xz_border(c,x,y,w,h,0x5a5a5a);
 int top=h*26/480<3?3:h*26/480;
 xz_rect(c,x+1,y+1,w-2,top,modern?0x181818:0x101010);
 static const int lanes2[2]={1,2},lanes4[4]={1,2,3,4},mixer[4]={3,1,2,4};
 const int *lanes=view==0?lanes2:view==1?lanes4:mixer;int n=view==0?2:4;
 int wave_h=modern?h*(view?312:236)/480:h*268/480,col_w=modern?w*14/100:w*16/100,right=modern?0:w*15/100;
 int bottom_y=y+1+top+wave_h+(modern?0:h*20/480);
 for(int i=0;i<n;i++){
  int d=lanes[i],ly=y+1+top+i*wave_h/n,lh=(i+1)*wave_h/n-i*wave_h/n-1,alive=d<=2||decks34;
  xz_rect(c,x+1,ly,col_w,lh,alive?(modern?0x1e1e1e:0x1a2638):0x101010);
  char num[2]={(char)('0'+d),0};
  if(lh>=11)xz_pic_text_center(c,x+1+col_w/2,ly+(lh-9)/2,num,12,alive?0xffffff:0x555555);
  else xz_rect(c,x+3,ly+1,col_w-4,lh-2>1?lh-2:1,alive?0x5a5a5a:0x262626);
  int wx=x+col_w+2,ww=w-col_w-3-right,mid=ly+lh/2;
  if(alive){
   for(int k=0;k<ww;k+=2){
    int amp=wave_amp(k,d,lh/2-1);if(amp<1)amp=1;
    xz_rect(c,wx+k,mid-amp,1,amp*2,0x2a6fe0);
    int inner=amp*45/100;if(inner>=1)xz_rect(c,wx+k,mid-inner,1,inner*2,0xffa028);
   }
   xz_rect(c,wx+ww/2,ly,1,lh,0xff2020);
  }else{xz_rect(c,wx,ly,ww,lh,0x141414);xz_rect(c,wx,mid,ww,1,0x2e2e2e);}
 }
 if(right){xz_rect(c,x+w-right-1,y+1+top,right,wave_h,0x22262c);xz_rect(c,x+w-right+2,y+4+top,right-6,3,0x5a6270);}
 static const int panels2[2]={1,2},panels4[4]={3,1,2,4};
 const int *panels=view==0?panels2:panels4;int pn=view==0?2:4,ph=y+h-1-bottom_y;
 for(int i=0;i<pn;i++){
  int d=panels[i],px=x+1+i*(w-2)/pn,pw=(i+1)*(w-2)/pn-i*(w-2)/pn-1,alive=d<=2||decks34;
  if(ph<4)break;
  xz_rect(c,px,bottom_y+1,pw,ph-1,alive?0x1a1a1a:0x0e0e0e);
  if(!modern)xz_rect(c,px,bottom_y+1,pw,4,alive?0x103462:0x161c26);
  if(ph<16)continue;
  if(alive){xz_rect(c,px+3,bottom_y+7,pw-6,2,0x8c8c8c);xz_rect(c,px+3,bottom_y+11,(pw-6)/2,3,0xffffff);xz_rect(c,px+3,bottom_y+ph-7,pw-6,4,0x2a6fe0);}
  else xz_rect(c,px+3,bottom_y+7,pw-6,2,0x2a2a2a);
 }
}
void xz_pic_stem_layout(struct xz_canvas c, int x, int y, int w, int h, int layout, const uint32_t rgb[3]) {
 int sx=x,sy=y,sw=w,sh=h,lane_h=sh*30/100;
 xz_rect(c,sx,sy,sw,sh,0x000000);
 const uint32_t cols[3]={rgb[2],rgb[1],rgb[0]};
 int ly1=sy+2,ly2=sy+2+lane_h+2;
 #define LANE(ly,lh) do{int _ly=(ly),_lh=(lh);xz_rect(c,sx+1,_ly,12,_lh,0x2a2a2a);int _mid=_ly+_lh/2;\
  for(int k=0;k<sw-16;k+=2){int a=(_lh/2-1)*(410+(512*(xz_sin1024(k*17)<0?-xz_sin1024(k*17):xz_sin1024(k*17))>>10))>>10;if(a<1)a=1;xz_rect(c,sx+15+k,_mid-a,1,2*a,0x2a6fe0);}}while(0)
 #define ROW(ly,rh,x0,x1) do{int _bw=((x1)-(x0)-6)/3;for(int i=0;i<3;i++)xz_rect(c,(x0)+i*(_bw+3),(ly),_bw,(rh),cols[i]);}while(0)
 if(layout==XZ_STEM_LAYOUT_BELOW||layout==XZ_STEM_LAYOUT_BELOW_HALF){
  int rh=layout==XZ_STEM_LAYOUT_BELOW?5:3;
  LANE(ly1,lane_h-rh-1);ROW(ly1+lane_h-rh,rh,sx+15,sx+sw-2);
  LANE(ly2,lane_h-rh-1);ROW(ly2+lane_h-rh,rh,sx+15,sx+sw-2);
 }else if(layout==XZ_STEM_LAYOUT_OUTER||layout==XZ_STEM_LAYOUT_OUTER_HALF){
  int rh=layout==XZ_STEM_LAYOUT_OUTER?5:3;
  ROW(ly1,rh,sx+15,sx+sw-2);LANE(ly1+rh+1,lane_h-rh-1);
  LANE(ly2,lane_h-rh-1);ROW(ly2+lane_h-rh,rh,sx+15,sx+sw-2);
 }else{LANE(ly1,lane_h);LANE(ly2,lane_h);}
 #undef LANE
 #undef ROW
 int by=sy+2+2*lane_h+6;
 xz_rect(c,sx+1,by,sw/2-2,sy+sh-by-1,0x1e1e1e);xz_rect(c,sx+sw/2+1,by,sw/2-2,sy+sh-by-1,0x1e1e1e);
 if(layout==XZ_MODERN_STEMS_PADS)for(int d=0;d<2;d++){int bx=sx+4+d*sw/2;for(int i=0;i<3;i++)xz_rect(c,bx+i*7,by-5,5,4,cols[i]);}
}
void xz_pic_wave_swatch(struct xz_canvas c, int x, int y, int w, int h, int scheme, const uint32_t rgb[3]) {
 xz_rect(c,x,y,w,h,0x000000);
 int mid=y+h/2;
 for(int k=0;k<w;k+=2){
  int amp=wave_amp(k,3,h/2-2);if(amp<1)amp=1;
  if(scheme==0){xz_rect(c,x+k,mid-amp,1,amp*2,0x2a6fe0);xz_rect(c,x+k,mid-amp/3,1,amp*2/3,0x7fb6ff);}
  else if(scheme==1){xz_rect(c,x+k,mid-amp,1,amp*2,XZ_FLAGSHIP_WAVE_LOW);xz_rect(c,x+k,mid-amp*2/3,1,amp*4/3,XZ_FLAGSHIP_WAVE_MID);xz_rect(c,x+k,mid-amp/3,1,amp*2/3,XZ_FLAGSHIP_WAVE_HIGH);}
  else if(scheme==2){xz_rect(c,x+k,mid-amp,1,amp*2,rgb[0]);xz_rect(c,x+k,mid-amp*2/3,1,amp*4/3,rgb[1]);xz_rect(c,x+k,mid-amp/3,1,amp*2/3,rgb[2]);}
  else{int hue=(k*7)%360;uint32_t col=hue<120?0xff4040:hue<240?0x40ff60:0x4080ff;xz_rect(c,x+k,mid-amp,1,amp*2,col);}
 }
}
void xz_pic_media_buttons(struct xz_canvas c, const struct xz_pic_style *s, int x, int y, int w, int h) {
 static const char *const labels[3]={"USB 1","USB 2","LINK"};
 int bw=(w-18)/4;
 for(int i=0;i<4;i++){
  int bx=x+i*(bw+6),lit=i==3;
  uint32_t fill=lit?s->select:s->row,edge=lit?s->select:s->row_edge,ink=lit?s->on_select:s->dim;
  xz_round_rect(c,bx,y,bw,h,6,15,edge);xz_round_rect(c,bx+1,y+1,bw-2,h-2,5,15,fill);
  if(i<3)xz_pic_text_center(c,bx+bw/2,y+(h-9)/2,labels[i],12,ink);
  else{ /* a computer: screen and stand */
   int cx=bx+bw/2,cy=y+h/2-2;
   xz_border(c,cx-11,cy-7,22,14,ink);xz_border(c,cx-10,cy-6,20,12,ink);
   xz_rect(c,cx-1,cy+7,3,3,ink);xz_rect(c,cx-6,cy+10,13,2,ink);
  }
 }
}
/* The explainer in the menu's colours: MODERN as the play card, CLASSIC in the theme's own (dark or light). */
static void route_anim(struct xz_canvas c, const struct xz_pic_style *s, int x, int y, int w, int h, int state, int part, int labels,
 struct xz_pic_route_anim *a) {
 uint32_t bg=a->bg,ink=s->modern?0xffffff:s->ink;
 struct xz_cable_palette pal={bg,xz_blend(bg,ink,14),xz_blend(bg,0x000000,160),s->modern?0x9a9a9a:s->dim,M_AMBER,
  s->modern?M_BLUE:s->select,s->modern?0xffffff:s->ink,s->modern?M_GREEN:s->good,s->modern?M_RED:s->alarm,
  s->modern?0x5a5a5a:s->faint,labels?(s->modern?0xb8b8b8:s->dim):0,0xffffff,c.theme};
 struct xz_cable_look look;xz_cable_look_of(&look,&pal,state,part);
 struct xz_cable_picture pic={a->px,a->cov,w,h};
 uint32_t t,h32=2166136261u;int signal;
 const uint32_t v[6]={(uint32_t)state<<4|(uint32_t)part,bg,s->select,s->ink,(uint32_t)c.theme,(uint32_t)(w<<12|h)};
 for(int i=0;i<6;i++){h32^=v[i];h32*=16777619u;}
 uint32_t base=h32|1u,key=xz_cable_frame_of(state,part,a->now_ms,a->moving,a->step_ms,&a->clock,&t,&signal)^base*2654435761u;
 if(a->key!=key||a->base!=base){
  int live=signal!=XZ_CABLE_SIGNAL_NONE&&a->base==base;
  if(xz_cable_draw(&pic,t,&look,signal,live?XZ_PIC_ROUTE_BUDGET:0u)){a->key=key;a->base=base;}
 }
 xz_cable_blit(c,x,y,&pic);
}
/* The route: state as enum xz_ui_route_state. */
void xz_pic_route(struct xz_canvas c, const struct xz_pic_style *s, int x, int y, int w, int h, int state, int part, int phase, int labels,
 struct xz_pic_route_anim *anim) {
 if(state==XZ_UI_ROUTE_NO_ENGINE)return;
 if(anim&&anim->px&&anim->cov&&w>0&&h>0&&w*h<=anim->cap){route_anim(c,s,x,y,w,h,state,part,labels,anim);return;}
 int modern=s->modern;
 uint32_t edge=modern?0x9a9a9a:0xa8b0b8,body=modern?0x161616:0x0e1216,ok=M_GREEN,warn=M_AMBER,bad=M_RED,accent=modern?M_BLUE:s->select;
 int ready=state==XZ_UI_ROUTE_READY,failed=state==XZ_UI_ROUTE_FAILED||state==XZ_UI_ROUTE_LOST;
 int scan=state==XZ_UI_ROUTE_SCANNING,starting=state==XZ_UI_ROUTE_STARTING||state==XZ_UI_ROUTE_WAITING_NATIVE_STOP;
 int uh=h*52/100<30?30:h*52/100,uw=w-26,ux=x+4,uy=y+h-uh-2;
 xz_round_rect(c,ux,uy,uw,uh,5,15,edge);xz_round_rect(c,ux+1,uy+1,uw-2,uh-2,4,15,body);
 int jr=uh*34/100;
 for(int k=0;k<2;k++){int jx=k?ux+uw-uh*55/100:ux+uh*55/100;xz_ring(c,jx,uy+uh/2+1,jr,2,edge);xz_disc(c,jx,uy+uh/2+1,2,edge);}
 int mx0=ux+uh*115/100,mx1=ux+uw-uh*115/100,sw=(mx1-mx0)/4;
 static const int strips[4]={3,1,2,4};
 for(int i=0;i<4;i++){
  int sx=mx0+sw*i+sw/2,outer=strips[i]==3||strips[i]==4;
  uint32_t col=outer?(ready?ok:failed&&part==XZ_UI_ROUTE_PART_SELECTOR?bad:warn):0x5a5a5a;
  xz_rect(c,sx,uy+uh*42/100,1,uh*46/100,0x5a5a5a);xz_rect(c,sx-3,uy+uh*70/100,7,3,0x8c8c8c);
  if(outer)xz_round_rect(c,sx-4,uy+4,9,7,2,15,col);
 }
 int px=ux+uw/2,sx=ux+uw*78/100;
 xz_rect(c,px-5,uy+3,10,5,failed&&part==XZ_UI_ROUTE_PART_PORT?bad:0xffffff);
 xz_rect(c,sx-6,uy-3,12,6,failed&&part==XZ_UI_ROUTE_PART_SOCKET?bad:0xffffff);
 /* the cable: up from USB 1, an arc over the rear edge, down into the rear socket (16 points) */
 int top=y+4,pts[20][2],n=0;
 pts[n][0]=px;pts[n][1]=uy+3;n++;pts[n][0]=px;pts[n][1]=top+8;n++;
 for(int i=0;i<=14;i++){pts[n][0]=px+(sx-px)*i/14;pts[n][1]=top+8-6*xz_sin1024(180*i/14)/1024;n++;}
 pts[n][0]=sx;pts[n][1]=uy-3;n++;
 uint32_t cable=ready?ok:failed?(part==XZ_UI_ROUTE_PART_CABLE||state==XZ_UI_ROUTE_LOST?bad:0x6a6a6a):starting?0xffffff:0x6a6a6a;
 int dashed=!ready&&!failed&&!starting;
 for(int i=0;i+1<n;i++){
  if(dashed&&(i&1))continue;
  xz_line16(c,pts[i][0]*16,pts[i][1]*16,pts[i+1][0]*16,pts[i+1][1]*16,dashed?26:32,cable);
 }
 if(scan||starting||state==XZ_UI_ROUTE_NOT_DETECTED){
  /* the pulse: three dots along the cable, the trailing two dimmer (pre-blended, no alpha) */
  uint32_t col=scan?accent:starting?0xffffff:warn;
  static const int back[3]={0,1,2},radius[3]={3,2,2},shade[3]={255,150,80};
  int head=phase*(n-1)/1024;
  for(int k=0;k<3;k++){int i=head-back[k];if(i>=0&&i<n)xz_disc(c,pts[i][0],pts[i][1],radius[k],xz_blend(modern?0:body,col,(unsigned)shade[k]));}
 }
 if(labels){
  uint32_t lc=modern?0xb8b8b8:s->dim;
  xz_pic_text_right(c,px-6,top+6,"USB 1",12,lc);xz_pic_text(c,sx+9,top+6,"REAR USB",12,lc);
 }
 if(failed&&part!=XZ_UI_ROUTE_PART_NONE){
  int fx=part==XZ_UI_ROUTE_PART_PORT?px:part==XZ_UI_ROUTE_PART_SOCKET?sx:part==XZ_UI_ROUTE_PART_CABLE?(px+sx)/2:mx0+sw/2;
  int fy=part==XZ_UI_ROUTE_PART_PORT?uy+5:part==XZ_UI_ROUTE_PART_SOCKET?uy-1:part==XZ_UI_ROUTE_PART_CABLE?top+2:uy+7;
  xz_disc(c,fx+9,fy-6,6,bad);
  xz_line16(c,(fx+7)*16,(fy-8)*16,(fx+11)*16,(fy-4)*16,22,0xffffff);xz_line16(c,(fx+11)*16,(fy-8)*16,(fx+7)*16,(fy-4)*16,22,0xffffff);
 }
}
void xz_pic_home_icon(struct xz_canvas c, const struct xz_pic_style *s, int section, int x, int y, const struct xz_pic_home *h) {
 uint32_t fg=s->modern?0x8c8c8c:0x6e7a86;
 switch(section){
 case XZ_UI_PLAY:
  for(int r=0;r<2;r++)for(int k=0;k<4;k++){
   int px=x+k*17+(k/2)*4,py=y+r*20;
   xz_round_rect(c,px,py,15,17,3,15,s->modern?0x3c3c3c:0x26313c);
   xz_pic_triangle(c,px+7,py+8,3,k%2?'r':'l',0xffffff);
  }
  break;
 case XZ_UI_STEM_SECTION:{
  uint32_t rgb[3];for(int i=0;i<3;i++)rgb[i]=h?h->stem_rgb[i]:0x808080;
  int mid=y+4+17;
  for(int k=0;k<74;k+=2){int a=wave_amp(k,5,16);if(a<2)a=2;
   xz_rect(c,x+k,mid-a,1,2*a,rgb[0]);xz_rect(c,x+k,mid-a*2/3,1,a*4/3,rgb[1]);xz_rect(c,x+k,mid-a/3,1,a*2/3,rgb[2]);}
  break;}
 case XZ_UI_SCREEN:xz_pic_mini_screen(c,x,y,74,44,0,h&&h->modern,1);break;
 case XZ_UI_FOUR_DECKS:xz_pic_mini_screen(c,x,y,74,44,1,s->modern,h&&h->decks34);break;
 case XZ_UI_CONNECT:{
  uint32_t accent=h&&h->accent?h->accent:0xff345a;char letter[2]={h?h->letter:0,0};
  if(letter[0]){xz_disc(c,x+20,y+20,17,accent);xz_pic_text_center(c,x+20,y+13,letter,20,0xffffff);}
  else xz_ring(c,x+20,y+20,16,2,fg);
  xz_ring(c,x+54,y+20,7,2,fg);xz_ring(c,x+64,y+20,7,2,fg);
  break;}
 case XZ_UI_EXTRAS:{
  uint32_t bg=s->modern?0x000000:s->row;
  xz_round_rect(c,x+10,y+6,54,28,12,15,fg);
  xz_rect(c,x+20,y+18,12,4,bg);xz_rect(c,x+24,y+14,4,12,bg);
  xz_disc(c,x+48,y+16,3,bg);xz_disc(c,x+54,y+23,3,bg);
  break;}
 default:break;
 }
}
const unsigned xz_ui_grid_features[XZ_UI_GRID_ROWS]={XZ_DECK_CAN_BEAT_JUMP,XZ_DECK_CAN_GATE_CUE,XZ_DECK_CAN_SMART_CUE,XZ_DECK_CAN_CUE_PREVIEW,
 XZ_DECK_CAN_PLAYING_KEY,XZ_DECK_CAN_KEY_SYNC,XZ_DECK_CAN_STEMS,XZ_DECK_CAN_XPAD,XZ_DECK_CAN_GROOVE,XZ_DECK_CAN_RAM};
const char *xz_ui_grid_name(int row){
 static const char *const names[XZ_UI_GRID_ROWS]={"BEAT JUMP PADS","GATE CUE","SMART CUE","HOT CUE PREVIEW","PLAYING KEY","KEY SYNC",
  "STEMS","X-PAD SAMPLER","GROOVE PADS","RAM TRACKS"};
 return row>=0&&row<XZ_UI_GRID_ROWS?names[row]:"";
}
void xz_pic_grid_geometry(int x, int y, int w, int row_h, struct xz_pic_grid_geo *g) {
 memset(g,0,sizeof *g);
 int bw=w/2;g->header_y=y;
 for(int b=0;b<2;b++){g->x[b]=x+b*bw;for(int r=0;r<5;r++)g->rows[b*5+r]=(struct xz_rect){x+b*bw-4,y+16+r*row_h,bw-10,row_h};}
}
void xz_pic_grid_draw(struct xz_canvas c, const struct xz_pic_style *s, const struct xz_pic_grid_geo *g, const unsigned caps[4], int focus) {
 static const int order[4]={2,0,1,3};
 for(int b=0;b<2;b++){
  int col0=g->x[b]+(g->rows[b*5].w+4)-128;
  for(int j=0;j<4;j++)deck_box(c,s,col0+j*30-6,g->header_y-2,13,order[j]);
  for(int r=0;r<5;r++){
   int row=b*5+r;struct xz_rect rr=g->rows[row];
   uint32_t fill=row==focus?(s->modern?0x0c2a48:xz_blend(s->bg,s->select,60)):r%2==0?s->row:s->bg;
   xz_rect(c,rr.x,rr.y,rr.w,rr.h,fill);
   xz_pic_text(c,rr.x+8,rr.y+(rr.h-9)/2,xz_ui_grid_name(row),12,s->modern?0xffffff:s->ink);
   for(int j=0;j<4;j++){
    int cx=col0+j*30,cy=rr.y+rr.h/2;
    if(caps[order[j]]&xz_ui_grid_features[row])xz_pic_check(c,cx-4,cy-4,s->good);
    else xz_rect(c,cx-6,cy,12,2,s->modern?0x6a6a6a:s->faint);
   }
  }
 }
}
