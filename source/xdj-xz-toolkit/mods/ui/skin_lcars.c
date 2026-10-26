#include "ui_skin.h"
#include <stdio.h>
#ifndef XZ_LCARS_VIOLET
#define XZ_LCARS_VIOLET 0xa64cff
#define XZ_LCARS_ICE 0x24c4ff
#endif

/* Frame: sidebar 0..124, content 136..792, top bar 0..40, info dock 412..480. */
enum { SIDE=124, X0=XZ_UI_CX, X1=792, CW=X1-X0, TOP=40, CAP=64, GAP=4 };
enum { TL=1, TR=2, BL=4, BR=8, LEFT=TL|BL, RIGHT=TR|BR, ALL=15 };
enum { ALIGN_L, ALIGN_C, ALIGN_R };


static int clamp100(float v){int n=(int)(v*100+.5f);return n<0?0:n>100?100:n;}
static unsigned isq(unsigned v){unsigned r=0,b=1u<<30;while(b>v)b>>=2;while(b){if(v>=r+b){v-=r+b;r=(r>>1)+b;}else r>>=1;b>>=2;}return r;}
/* Row-run rounded block; only the edge pixel of each corner row is blended. */
static void shape(struct xz_canvas c,int x,int y,int w,int h,int r,unsigned k,uint32_t col){
 int lim=(w<h?w:h)/2;if(r>lim)r=lim;
 if(r<=0||!(k&ALL)){xz_rect(c,x,y,w,h,col);return;}
 int r16=r*16;
 for(int j=0;j<h;j++){
  int up=j<r,lo=j>=h-r;
  if(!up&&!lo){xz_rect(c,x,y+j,w,1,col);continue;}
  int dy=up?(r-j)*16-8:(j-(h-r))*16+8;
  int in16=r16-(int)isq((unsigned)(r16*r16-dy*dy)),in=in16>>4,a=255-(in16&15)*16;
  int l=up?(k&TL):(k&BL),rt=up?(k&TR):(k&BR);
  int from=l?x+in+1:x,to=rt?x+w-in-1:x+w;
  if(l)xz_plot(c,x+in,y+j,col,a);if(rt)xz_plot(c,x+w-in-1,y+j,col,a);
  if(to>from)xz_rect(c,from,y+j,to-from,1,col);
 }
}
static void ring(struct xz_canvas c,int x,int y,int w,int h,int r,unsigned k,int t,uint32_t col){
 shape(c,x,y,w,h,r,k,col);shape(c,x+t,y+t,w-2*t,h-2*t,r-t,k,0);
}
static int fit(struct xz_canvas c,const char *s,int w){return xz_text_width(c,s,2,200)<=w?2:1;}
static void text(struct xz_canvas c,int x,int y,int w,int h,const char *s,int scale,int align,uint32_t col){
 if(!s||w<=0||h<=0)return;
 if(xz_text_width(c,s,scale,200)>w)scale=1;
 int tw=xz_text_width(c,s,scale,200);if(tw>w)tw=w;
 int tx=align==ALIGN_L?0:align==ALIGN_C?(w-tw)/2:w-tw;
 struct xz_canvas box=xz_sub(c,x,y,w,h);
 xz_text(box,tx,(h-(scale>1?14:9))/2,s,scale,200,col);
}
/* A block is solid colour with black text, or (outline) a black slab with a coloured ring and coloured text. */
static void block(struct xz_canvas c,int x,int y,int w,int h,int r,unsigned k,uint32_t col,int outline,const char *s,int scale,int align,int pad){
 if(outline)ring(c,x,y,w,h,r,k,2,col);else shape(c,x,y,w,h,r,k,col);
 if(s&&*s)text(c,x+pad,y,w-2*pad,h,s,scale,align,outline?col:0);
}
/* Body plus a rounded end cap carrying a state word, separated by a black gutter. */
static void capped(struct xz_canvas c,int x,int y,int w,int h,uint32_t col,int outline,const char *s,uint32_t cap_col,int cap_outline,const char *word){
 if(w<220){
  block(c,x,y,w,h,12,ALL,col,outline,NULL,1,ALIGN_L,0);
  text(c,x+8,y+3,w-16,h/2,s,1,ALIGN_L,outline?col:0);
  block(c,x+6,y+h-20,w-12,17,8,ALL,cap_col,cap_outline,word,1,ALIGN_C,3);
  return;
 }
 int cw=CAP;
 block(c,x,y,w-cw-GAP,h,h/2,LEFT,col,outline,s,fit(c,s,w-cw-GAP-h),ALIGN_L,h/2-2);
 block(c,x+w-cw,y,cw,h,h/2,RIGHT,cap_col,cap_outline,word,1,ALIGN_C,4);
}

static void elbow(struct xz_canvas c,int top,uint32_t col){
 if(top){shape(c,0,0,SIDE,72,36,TL,col);xz_rect(c,SIDE,0,76,TOP,col);xz_rect(c,SIDE,TOP,16,16,col);shape(c,SIDE,TOP,32,32,16,TL,0);}
 else{shape(c,0,XZ_UI_DOCK_Y-56,SIDE,48,24,BL,col);}
}
static void chip(struct xz_canvas c,int x,int y,int w,int h,int tab,const char *s,int scale,uint32_t tab_col,uint32_t col){
 shape(c,x,y,tab,h,h/2,LEFT,tab_col);text(c,x+tab+8,y,w-tab-8,h,s,scale,ALIGN_R,col);
}
static void deck_header(struct xz_canvas c,const struct xz_ui *u,const struct xz_ui_deck *d,const struct xz_theme_palette *p,char *buf,size_t len){
 uint32_t ice=XZ_LCARS_ICE;int wave_y=u->page==XZ_UI_STEMS?108:100,half=u->page==XZ_UI_STEMS?22:14;
 block(c,X0,48,452,36,18,ALL,XZ_LCARS_VIOLET,0,d->track?d->track:(u->deck<2?"NO TRACK LOADED":"COMPUTER DECK / EXTERNAL AUDIO"),2,ALIGN_L,14);
 if(d->bpm>0)snprintf(buf,len,"%.1f BPM",(double)d->bpm);else snprintf(buf,len,"---.- BPM");
 chip(c,608,48,184,36,40,buf,2,p->ink,d->bpm>0?ice:p->ink);
 if(d->wave_peaks&&d->wave_count){
  size_t n=d->wave_count<322?d->wave_count:322;
  for(size_t j=0;j<n;j++){int h=d->wave_peaks[j*d->wave_count/n]*half*2/255;if(h<2)h=2;xz_rect(c,X0+(int)(j*CW/n),wave_y-h/2,2,h,p->ink);}
 }else{
  xz_rect(c,X0,wave_y-1,CW,2,xz_blend(0,p->ink,96));
  xz_rect(c,X0+CW/2-80,wave_y-12,160,24,0);text(c,X0,wave_y-12,CW,24,"WAVEFORM NOT AVAILABLE",1,ALIGN_C,ice);
 }
}
static void pad(struct xz_canvas c,const struct xz_ui_widget *a,uint32_t cap_col,uint32_t body,int outline,const char *state,uint32_t alarm,int not_ready){
 int cap=a->h>80?16:12,r=a->h>80?20:14;uint32_t dim=xz_blend(0,body,140);
 shape(c,a->x,a->y,a->w,cap,cap,TL|TR,not_ready?dim:cap_col);
 int by=a->y+cap+GAP,bh=a->h-cap-GAP;
 if(outline)ring(c,a->x,by,a->w,bh,r,BL|BR,2,dim);else shape(c,a->x,by,a->w,bh,r,BL|BR,body);
 xz_text(c,a->x+10,by+6,a->label,2,4,outline?dim:0);
 if(not_ready)block(c,a->x,a->y+a->h-22,a->w,22,11,BL|BR,alarm,0,state,1,ALIGN_C,4);
 else text(c,a->x+8,a->y+a->h-24,a->w-16,18,state,1,ALIGN_R,outline?dim:0);
}
static void render(const struct xz_ui_scene *s){
 struct xz_canvas c=s->c;const struct xz_ui *u=s->u;
 const struct xz_ui_deck *d=s->d;const struct xz_theme_palette *p=s->p;char buf[96];size_t i;
 uint32_t ink=p->ink,sun=p->accent,red=p->alarm,violet=XZ_LCARS_VIOLET,ice=XZ_LCARS_ICE,dim=xz_blend(0,ink,110);
 int decks=u->page==XZ_UI_STEMS||u->page==XZ_UI_XPAD||u->page==XZ_UI_SETTINGS;
 int alarm=u->notice[0]&&u->page!=XZ_UI_CONTROLS&&u->page!=XZ_UI_THEMES;
 xz_rect(c,0,0,800,480,0);
 elbow(c,1,ink);elbow(c,0,alarm?red:ink);
 xz_text(c,212,13,"XZ MODS",2,12,ink);
 if(!decks){
  block(c,296,0,260,TOP,0,0,ink,0,"XDJ-XZ",1,ALIGN_R,10);
  /* The DJ Profile chip owns the corner when there is one. */
  if(!s->m->dj)block(c,560,0,148,TOP,0,0,violet,0,"xzmods.com",1,ALIGN_R,10);
 }
 shape(c,760,0,40,TOP,20,RIGHT,ink);
 for(i=0;i<s->n;i++){
  struct xz_ui_widget a=s->w[i];int available=s->look[i].available,selected=s->look[i].selected;uint32_t color=s->look[i].color;
  int unavailable=!available;
  switch(a.kind){
  case XZ_UI_FEATURE_LINK:
   block(c,a.x,a.y,a.w,a.h,a.h/2,ALL,violet,0,a.label,fit(c,a.label,a.w-32),ALIGN_L,16);break;
  case XZ_UI_PANEL:
   if(a.index==XZ_UI_STEMS||a.index==XZ_UI_XPAD||a.index==XZ_UI_CONNECTION){block(c,a.x,a.y,a.w,a.h,a.h/2,ALL,violet,0,a.label,2,ALIGN_L,22);break;}
   shape(c,a.x,a.y,a.w,a.h,0,0,selected?sun:ink);
   text(c,a.x+8,a.y+a.h-26,a.w-16,20,a.label,fit(c,a.label,a.w-16),ALIGN_R,0);
   if(selected)xz_rect(c,SIDE,a.y+a.h-10,X0-SIDE-4,6,sun);
   break;
  case XZ_UI_CLOSE:
   shape(c,a.x,a.y,a.w,a.h,0,0,red);text(c,a.x+8,a.y+a.h-26,a.w-16,20,a.label,2,ALIGN_R,0);break;
  case XZ_UI_DECK:
   shape(c,a.x,a.y,a.w,a.h,0,0,selected?sun:ink);
   snprintf(buf,sizeof(buf),"DECK %d",a.index+1);
   text(c,a.x+10,a.y+a.h-26,a.w-20,20,buf,2,ALIGN_R,0);
   text(c,a.x+10,a.y+a.h-22,a.w-20,14,a.label,1,ALIGN_L,0);
   break;
  case XZ_UI_JUMP_ENABLE:case XZ_UI_JUMP_SHIFT:case XZ_UI_ENABLE:case XZ_UI_STEMS_OVERLAY:case XZ_UI_PAD_FEEDBACK:case XZ_UI_SHIFT_PAGES:case XZ_UI_SHIFT_KEYSYNC:
  case XZ_UI_TAKEOVER_TOGGLE:case XZ_UI_HOLD:case XZ_UI_OVERDUB:case XZ_UI_BYPASS:case XZ_UI_METRICS_TOGGLE:
  case XZ_UI_KEY_FEEDBACK:case XZ_UI_RAM_DEBUG:case XZ_UI_REKORDBOX_KEY:
   if(unavailable)capped(c,a.x,a.y,a.w,a.h,dim,1,a.label,red,0,"NOT READY");
   else if(selected)capped(c,a.x,a.y,a.w,a.h,ink,0,a.label,ink,0,"ON");
   else capped(c,a.x,a.y,a.w,a.h,ink,1,a.label,ink,1,"OFF");
   break;
  case XZ_UI_CONNECTION_ENABLE:case XZ_UI_DISCOVERY:
   if(unavailable)capped(c,a.x,a.y,a.w,a.h,dim,1,a.label,red,0,"READ ONLY");
   else if(selected)capped(c,a.x,a.y,a.w,a.h,ink,0,a.label,ink,0,"ON");
   else capped(c,a.x,a.y,a.w,a.h,ink,1,a.label,ink,1,"OFF");
   break;
  case XZ_UI_STEM_BANK:case XZ_UI_STEM_PAGE:case XZ_UI_TAKEOVER_ASSIGN:case XZ_UI_WAVE_MODE:case XZ_UI_RAM_DECK:
  case XZ_UI_SET_INTERFACE:case XZ_UI_SET_MODERN_THEME:case XZ_UI_REKORDBOX_ASSIGN:
   block(c,a.x,a.y,a.w,a.h,a.h/2,ALL,selected?sun:violet,0,a.label,fit(c,a.label,a.w-a.h),ALIGN_C,a.h/2-4);
   break;
  case XZ_UI_KEY_SHIFT:case XZ_UI_KEY_SYNC:
   if(a.kind==XZ_UI_KEY_SHIFT&&a.index==0){
    if(available)snprintf(buf,sizeof(buf),"%+d",d->key_semitones);else snprintf(buf,sizeof(buf),"--");
    ring(c,a.x,a.y,a.w,a.h,a.h/2,ALL,2,available?ink:dim);
    text(c,a.x+12,a.y,a.w-24,a.h-14,buf,2,ALIGN_L,available?ice:dim);
    text(c,a.x+12,a.y+a.h-22,a.w-24,14,available?"RESET":"NOT READY",1,ALIGN_R,available?ink:red);
    break;
   }
   if(unavailable)capped(c,a.x,a.y,a.w,a.h,dim,1,a.label,red,0,"NOT READY");
   else block(c,a.x,a.y,a.w,a.h,a.h/2,ALL,ink,0,a.label,fit(c,a.label,a.w-a.h),ALIGN_C,a.h/2-4);
   break;
  case XZ_UI_GROOVE_PAD:
   pad(c,&a,color,d->groove_active==a.index?sun:ink,unavailable,available?(d->groove_active==a.index?"PLAYING":"READY"):"NOT READY",red,unavailable);
   break;
  case XZ_UI_HOTCUE_PAD:pad(c,&a,violet,violet,unavailable,available?"HOT CUE":"NOT READY",red,unavailable);break;
  case XZ_UI_SAMPLE_PAD:
   pad(c,&a,violet,selected?sun:ink,unavailable,available?(selected?"PLAYING":"READY"):"NOT READY",red,unavailable);
   break;
  case XZ_UI_STRIP:{
   int k,cw=a.w/6;uint32_t line=available?ink:dim;
   for(k=0;k<6;k++){
    int x=a.x+k*cw,active=available&&d->loop_index==k;
    block(c,x,a.y-28,cw-GAP,24,12,ALL,active?sun:violet,0,xz_ui_lengths[k],2,ALIGN_C,6);
    ring(c,x,a.y,cw-GAP,a.h,10,ALL,active?3:1,active?ink:line);
   }
   xz_rect(c,a.x,a.y+a.h/2,a.w-GAP,1,xz_blend(0,violet,160));
   xz_text(c,a.x+8,a.y+6,"+12",1,4,ice);xz_text(c,a.x+8,a.y+a.h-16,"-12",1,4,ice);
   if(available&&d->loop_index>=0&&d->loop_index<6){
    float pitch=d->pitch<-12?-12:d->pitch>12?12:d->pitch;
    int py=a.y+(int)((12-pitch)*(float)(a.h-1)/24);if(py>a.y+a.h-6)py=a.y+a.h-6;
    xz_rect(c,a.x+d->loop_index*cw+4,py,cw-GAP-8,6,sun);
   }
   if(unavailable)block(c,a.x+a.w-GAP-96,a.y+a.h-26,96,22,11,ALL,red,0,"NOT READY",1,ALIGN_C,4);
   break;}
  case XZ_UI_VOLUME:{
   int pos=(a.w-16)*clamp100(d->sample_volume)/100;
   ring(c,a.x,a.y,a.w,a.h,a.h/2,ALL,2,available?ink:dim);
   if(available&&pos>0)shape(c,a.x+8,a.y+8,pos,a.h-16,(a.h-16)/2,ALL,ink);
   snprintf(buf,sizeof(buf),"%d%%",clamp100(d->sample_volume));
   if(unavailable)text(c,a.x+12,a.y,a.w-24,a.h,"NOT READY",1,ALIGN_R,red);
   else if(pos>a.w/2)text(c,a.x+8,a.y,pos,a.h,buf,2,ALIGN_R,0);
   else text(c,a.x+8+pos,a.y,a.w-16-pos,a.h,buf,2,ALIGN_R,ice);
   break;}
  case XZ_UI_SET_THEME:{
   const struct xz_theme_palette *sample=xz_theme_palette(a.index);uint32_t body=selected?sun:(a.index/3)&1?violet:ink;
   shape(c,a.x,a.y,40,a.h,a.h/2,LEFT,sample->accent);
   shape(c,a.x+40+GAP,a.y,a.w-40-GAP,a.h,a.h/2,RIGHT,body);
   text(c,a.x+52,a.y+3,a.w-66,20,a.label,1,ALIGN_L,0);
   text(c,a.x+52,a.y+a.h-17,a.w-66,14,selected?"SELECTED":a.index>=XZ_THEME_LCARS?"FULL SKIN":"",1,ALIGN_R,0);
   break;}
  default:block(c,a.x,a.y,a.w,a.h,a.h/2,ALL,available?ink:dim,!available||((a.kind==XZ_UI_CONTROL_SECTION||a.kind==XZ_UI_JUMP_EDIT_PAGE||a.kind==XZ_UI_JUMP_PAIR||a.kind==XZ_UI_JUMP_SIZE)&&!selected),a.label,1,ALIGN_L,10);break;}
 }
 if(u->page==XZ_UI_STEMS||u->page==XZ_UI_XPAD)deck_header(c,u,d,p,buf,sizeof(buf));
 /* Control descriptions live in the info dock; connection status is shared. */
 if(u->page==XZ_UI_XPAD){
  snprintf(buf,sizeof(buf),"LOOP %s BEATS / %+.1f KEY",xz_ui_lengths[d->loop_index>=0&&d->loop_index<6?d->loop_index:0],(double)d->pitch);
  xz_text(c,X0,124,buf,1,80,ice);
 }else if(u->page==XZ_UI_CONNECTION)xz_ui_page_details(s);
}
static void render_inline(const struct xz_ui_scene *s){
 xz_rect(s->c,0,0,s->c.width,s->c.height,0);
 xz_ui_stem_rows(s);
}

/* A pill like the panel's blocks: butterscotch, ice while on, black label. */
static struct xz_top_face top_button(struct xz_canvas c,int x,int w,const char *label,int lit){
 uint32_t col=lit?XZ_LCARS_ICE:xz_theme_palette(c.theme)->ink;
 xz_rect(c,x,0,w,XZ_TOP_BAR_H,0);block(c,x+1,1,w-2,XZ_TOP_BAR_H-2,11,ALL,col,0,NULL,1,ALIGN_C,6);
 return xz_top_bar_label(c,x,w,label,col,0);
}
const struct xz_ui_skin xz_skin_lcars={xz_ui_place,render,render_inline,top_button};

/* Drawn under the top bar: an end cap in the black between VJ.TOOLS and DECKS, and the title bar in the
   metrics readout's slot, which the readout covers while METRICS shows. */
void xz_ui_render_lcars_play_header(uint16_t *pixels,size_t stride){
 if(!pixels||stride<800)return;
 struct xz_canvas c=xz_top_bar_canvas(pixels,stride,XZ_THEME_LCARS);uint32_t color=XZ_LCARS_NATIVE_AMBER;
 xz_rect(c,114,0,558,17,0);shape(c,118,3,16,11,5,ALL,color);
 shape(c,XZ_METRICS_READOUT_X,3,XZ_METRICS_READOUT_W,11,5,RIGHT,color);
 xz_text(c,XZ_METRICS_READOUT_X+10,XZ_TOP_BAR_LABEL_Y,"LCARS / PERFORMANCE",1,32,0);
}
