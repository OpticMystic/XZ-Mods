#include "ui_skin.h"
#include <stdio.h>
#ifndef XZ_LCARS_VIOLET
#define XZ_LCARS_VIOLET 0xa64cff
#define XZ_LCARS_ICE 0x24c4ff
#endif

/* Frame: sidebar 0..124, content 148..792, top bar 0..40, bottom bar 452..480. */
enum { SIDE=124, X0=148, X1=792, CW=X1-X0, TOP=40, BOTTOM=452, CAP=64, GAP=4 };
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
 else{shape(c,0,408,SIDE,72,36,BL,col);xz_rect(c,SIDE,BOTTOM,76,28,col);xz_rect(c,SIDE,BOTTOM-16,16,16,col);shape(c,SIDE,BOTTOM-32,32,32,16,BL,0);}
}
static void chip(struct xz_canvas c,int x,int y,int w,int h,int tab,const char *s,int scale,uint32_t tab_col,uint32_t col){
 shape(c,x,y,tab,h,h/2,LEFT,tab_col);text(c,x+tab+8,y,w-tab-8,h,s,scale,ALIGN_R,col);
}
static void deck_header(struct xz_canvas c,const struct xz_ui *u,const struct xz_ui_deck *d,const struct xz_theme_palette *p,char *buf,size_t len){
 uint32_t ice=XZ_LCARS_ICE;
 block(c,X0,48,452,36,18,ALL,XZ_LCARS_VIOLET,0,d->track?d->track:(u->deck<2?"NO TRACK LOADED":"COMPUTER DECK / EXTERNAL AUDIO"),2,ALIGN_L,14);
 if(d->bpm>0)snprintf(buf,len,"%.1f BPM",(double)d->bpm);else snprintf(buf,len,"---.- BPM");
 chip(c,608,48,184,36,40,buf,2,p->ink,d->bpm>0?ice:p->ink);
 if(d->wave_peaks&&d->wave_count){
  size_t n=d->wave_count<322?d->wave_count:322;
  for(size_t j=0;j<n;j++){int h=d->wave_peaks[j*d->wave_count/n]*60/255;if(h<2)h=2;xz_rect(c,X0+(int)(j*CW/n),124-h/2,2,h,p->ink);}
 }else{
  xz_rect(c,X0,123,CW,2,xz_blend(0,p->ink,96));
  xz_rect(c,X0+CW/2-80,112,160,24,0);text(c,X0,112,CW,24,"WAVEFORM NOT AVAILABLE",1,ALIGN_C,ice);
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
 struct xz_canvas c=s->c;const struct xz_ui *u=s->u;const struct xz_ui_model *m=s->m;
 const struct xz_ui_deck *d=s->d;const struct xz_theme_palette *p=s->p;char buf[96];size_t i;
 uint32_t ink=p->ink,sun=p->accent,red=p->alarm,violet=XZ_LCARS_VIOLET,ice=XZ_LCARS_ICE,dim=xz_blend(0,ink,110);
 int decks=u->page==XZ_UI_STEMS||u->page==XZ_UI_XPAD||u->page==XZ_UI_SETTINGS;
 const char *footer=xz_ui_footer_text(u,m);int alarm=u->notice[0]&&u->page!=XZ_UI_CONTROLS&&u->page!=XZ_UI_THEMES;
 xz_rect(c,0,0,800,480,0);
 elbow(c,1,ink);elbow(c,0,alarm?red:ink);
 xz_text(c,SIDE-8-xz_text_width(c,"XDJ-XZ",1,20),58,"XDJ-XZ",1,20,0);
 xz_text(c,212,13,"XZ MODS",2,12,ink);
 if(!decks){
  block(c,296,0,260,TOP,0,0,ink,0,"XDJ-XZ",1,ALIGN_R,10);
  block(c,560,0,148,TOP,0,0,violet,0,"vj.tools/xzmods",1,ALIGN_R,10);
 }
 shape(c,760,0,40,TOP,20,RIGHT,ink);
 text(c,212,BOTTOM,512,28,footer,1,ALIGN_L,alarm?red:ink);
 shape(c,728,BOTTOM,72,28,14,RIGHT,alarm?red:violet);
 for(i=0;i<s->n;i++){
  struct xz_ui_widget a=s->w[i];int available=s->look[i].available,selected=s->look[i].selected;uint32_t color=s->look[i].color;
  int unavailable=!available;
  switch(a.kind){
  case XZ_UI_PANEL:
   if(a.index==XZ_UI_STEMS||a.index==XZ_UI_XPAD||a.index==XZ_UI_CONNECTION){block(c,a.x,a.y,a.w,a.h,a.h/2,ALL,violet,0,a.label,2,ALIGN_L,22);break;}
   shape(c,a.x,a.y,a.w,a.h,0,0,selected?sun:ink);
   text(c,a.x+8,a.y+a.h-26,a.w-16,20,a.label,fit(c,a.label,a.w-16),ALIGN_R,0);
   if(selected)xz_rect(c,SIDE,a.y+a.h-10,X0-SIDE-8,6,sun);
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
  case XZ_UI_TAKEOVER_TOGGLE:case XZ_UI_HOLD:case XZ_UI_OVERDUB:case XZ_UI_BYPASS:
   if(unavailable)capped(c,a.x,a.y,a.w,a.h,dim,1,a.label,red,0,"NOT READY");
   else if(selected)capped(c,a.x,a.y,a.w,a.h,ink,0,a.label,ink,0,"ON");
   else capped(c,a.x,a.y,a.w,a.h,ink,1,a.label,ink,1,"OFF");
   break;
  case XZ_UI_CONNECTION_ENABLE:case XZ_UI_DISCOVERY:
   if(unavailable)capped(c,a.x,a.y,a.w,a.h,dim,1,a.label,red,0,"READ ONLY");
   else if(selected)capped(c,a.x,a.y,a.w,a.h,ink,0,a.label,ink,0,"ON");
   else capped(c,a.x,a.y,a.w,a.h,ink,1,a.label,ink,1,"OFF");
   break;
  case XZ_UI_STEM_BANK:case XZ_UI_STEM_PAGE:case XZ_UI_TAKEOVER_ASSIGN:case XZ_UI_WAVE_MODE:
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
 if(u->page==XZ_UI_STEMS){
  xz_text(c,X0,164,"GROOVE PADS",2,20,ink);
  xz_text(c,X0+140,168,"REPLACE A STEM WITH AN ASSIGNED GROOVE / USE THE PLAY-SCREEN ROWS TO MIX STEMS",1,100,ice);
  xz_text(c,X0,196,"UNASSIGNED OR UNAVAILABLE PADS ARE MARKED BELOW",1,80,ice);
 }else if(u->page==XZ_UI_XPAD){
  snprintf(buf,sizeof(buf),"%s BEATS / %+.1f KEY",xz_ui_lengths[d->loop_index>=0&&d->loop_index<6?d->loop_index:0],(double)d->pitch);
  chip(c,636,276,156,24,24,buf,1,violet,ice);
  xz_text(c,X0,428,"SAMPLE BANK A-H / CLOSING X-PAD STOPS SOUND / VOL AND HOLD LATCH",1,100,ice);
 }else if(u->page==XZ_UI_SETTINGS){
  xz_text(c,X0,48,"DECK SETTINGS + EXPERIMENTAL TOOLS",1,60,ice);
  xz_text(c,474,300,"SCROLLING WAVEFORM",2,30,ink);
  xz_text(c,474,326,"STEMS SHOWS 3-BAND WITHOUT STEMS",1,60,ice);
  xz_text(c,X0,404,"STEM SERVER: NOT AVAILABLE",1,40,ice);
  xz_text(c,X0,424,"UNAVAILABLE TOOLS ARE MARKED NOT READY",1,60,ice);
 }else if(u->page==XZ_UI_CONTROLS){
  /* Shared graphical setup draws after the skin. */
 }else if(u->page==XZ_UI_CONNECTION){
  const struct xz_ui_connection *v=&m->connection;
  xz_text(c,X0,48,"VJ.TOOLS CONNECTION",2,30,ink);
  xz_text(c,X0,70,"OPTIONAL COMPUTER LINK / DJ FEATURES WORK STANDALONE",1,60,ice);
  shape(c,246,88,8,196,4,ALL,violet);
  static const char *const labels[5]={"STATUS","DEVICE IP","PORT","PROTOCOL","RECEIVED FPS"};
  const char *values[5];
  values[0]=!v->ready?"NOT AVAILABLE":v->connected?"CONNECTED":v->enabled?"WAITING FOR VJ.TOOLS":"DISABLED";
  values[1]=v->device_ip&&*v->device_ip?v->device_ip:"NOT REPORTED";
  char port[16];if(v->port)snprintf(port,sizeof(port),"%u",v->port);else snprintf(port,sizeof(port),"NOT REPORTED");values[2]=port;
  values[3]=v->protocol&&*v->protocol?v->protocol:"NOT REPORTED";
  if(v->ready&&v->connected&&v->stats_valid)snprintf(buf,sizeof(buf),"%.1f HZ",(double)v->frame_hz);else snprintf(buf,sizeof(buf),"NOT REPORTED");values[4]=buf;
  for(int k=0;k<5;k++){
   int y=92+k*38;uint32_t col=k==0?(v->ready&&v->connected?sun:ice):ice;
   text(c,X0,y,238-X0,20,labels[k],1,ALIGN_R,ink);
   text(c,262,y,230,20,values[k],fit(c,values[k],230),ALIGN_L,col);
  }
  xz_text(c,X0,288,"STATUS COMES FROM THE RECEIVER",1,40,ice);
  xz_text(c,X0,306,m->fb_takeover?"COMPUTER SCREEN ACTIVE":"NATIVE PLAY SCREEN ACTIVE",1,40,m->fb_takeover?sun:ice);
  xz_text(c,500,104,"SCREEN SWITCH BUTTON",1,30,ice);
  xz_text(c,500,296,"OPTIONAL VJ.TOOLS LIBRARY CONNECTION",1,48,ice);
  xz_text(c,500,314,"vj.tools",2,12,ink);
  xz_text(c,X0,424,"DJ MODS: CDJ3K-MODS / NSAINTOT + CONTRIBUTORS",1,60,ice);
 }
}
static void render_inline(const struct xz_ui_scene *s){
 struct xz_canvas c=s->c;const struct xz_ui_model *m=s->m;const struct xz_theme_palette *p=s->p;
 uint32_t ink=p->ink,red=p->alarm,dim=xz_blend(0,ink,110);char buf[16];
 xz_rect(c,0,0,c.width,c.height,0);
 for(size_t i=0;i<s->n;i++){
  struct xz_ui_widget a=s->w[i];const struct xz_ui_deck *d=&m->deck[a.deck];
  int available=s->look[i].available,selected=s->look[i].selected;
  int top=18,band=10,low=a.h-top-band;
  if(a.kind==XZ_UI_MUTE){
   uint32_t stem=p->stem[a.index],col=available?stem:dim;int level=clamp100(d->levels[a.index]);
   if(selected||!available)ring(c,a.x,a.y,a.w,top,top/2,TL|TR,2,col);else shape(c,a.x,a.y,a.w,top,top/2,TL|TR,col);
   xz_rect(c,a.x,a.y+top,4,band,col);
   text(c,a.x+9,a.y,a.w-18,top,a.label,1,ALIGN_L,selected||!available?col:0);
   if(!available)block(c,a.x+a.w-62,a.y+1,60,top-2,8,ALL,red,0,"NOT READY",1,ALIGN_C,2);
   else if(d->stem_loading)block(c,a.x+a.w-56,a.y+1,54,top-2,8,ALL,red,0,"LOADING",1,ALIGN_C,2);
   else if(selected)text(c,a.x+9,a.y,a.w-18,top,"MUTE",1,ALIGN_R,col);
   ring(c,a.x,a.y+top+band,a.w,low,low/2,BL|BR,2,col);
   int fill=available&&!selected?(a.w-8)*level/100:0;
   if(fill>0)shape(c,a.x+4,a.y+top+band+4,fill,low-8,(low-8)/2,ALL,col);
   snprintf(buf,sizeof(buf),"%d%%",level);
   if(available)text(c,a.x+8,a.y+top+band,a.w-16,low,buf,1,ALIGN_R,fill>a.w-48?0:col);
   continue;
  }
  uint32_t col=!available?dim:selected?red:ink;
  if(selected)shape(c,a.x,a.y,a.w,top,top/2,TL|TR,col);else ring(c,a.x,a.y,a.w,top,top/2,TL|TR,2,col);
  xz_rect(c,a.x,a.y+top,4,band,col);
  text(c,a.x+9,a.y,a.w-18,top,a.label,1,ALIGN_L,selected?0:col);
  if(!available)block(c,a.x+a.w-62,a.y+1,60,top-2,8,ALL,red,0,"NOT READY",1,ALIGN_C,2);
  else text(c,a.x+9,a.y,a.w-18,top,selected?"ON":"OFF",1,ALIGN_R,selected?0:col);
  if(selected)shape(c,a.x,a.y+top+band,a.w,low,low/2,BL|BR,col);else ring(c,a.x,a.y+top+band,a.w,low,low/2,BL|BR,2,col);
  text(c,a.x+9,a.y+top+band,a.w-18,low,a.deck?"DECK 2":"DECK 1",1,ALIGN_L,selected?0:col);
  text(c,a.x+9,a.y+top+band,a.w-18,low,"BYPASS",1,ALIGN_R,selected?0:col);
 }
}
static void render_buttons(struct xz_canvas c,int stems,int vj_visible,int vj_active){
 const struct xz_theme_palette *p=xz_theme_palette(c.theme);uint32_t ice=XZ_LCARS_ICE;
 struct xz_ui_widget buttons[3]={{744,0,56,24,XZ_UI_NONE,0,0,"MODS",-1},{674,0,68,24,XZ_UI_NONE,0,0,"STEMS",-1},{0,0,112,24,XZ_UI_NONE,0,0,vj_active?"EXIT VJ":"VJ.TOOLS",-1}};
 for(int i=0;i<(vj_visible?3:2);i++){
  struct xz_ui_widget a=buttons[i];int lit=i==1?stems:i==2?vj_active:0;
  xz_rect(c,a.x,a.y,a.w,a.h,0);
  block(c,a.x+1,a.y+1,a.w-2,a.h-2,11,ALL,lit?ice:p->ink,0,a.label,1,ALIGN_C,6);
 }
}
const struct xz_ui_skin xz_skin_lcars={xz_ui_place,render,render_inline,render_buttons};

void xz_ui_render_lcars_play_header(uint16_t *pixels,size_t stride){
 if(!pixels||stride<800)return;
 struct xz_canvas c={pixels,stride,800,480,XZ_THEME_LCARS};uint32_t color=XZ_LCARS_NATIVE_AMBER;
 xz_rect(c,114,0,558,17,0);shape(c,118,3,16,11,5,ALL,color);
 xz_text(c,142,3,"LCARS / PERFORMANCE",1,32,color);shape(c,282,3,385,11,5,RIGHT,color);
}
