#include "ui_skin.h"
#include <stdio.h>
#include <string.h>

enum { GREEN=0x33cc33, HEAD=0xccffcc, TAIL=0x1a801a, DARK=0x062e06, WARN=0xffb000 };

static int percent(float v){return v<=0?0:v>=1?100:(int)(v*100+.5f);}
static void txt(struct xz_canvas c,int x,int y,int width,const char *s,int size,uint32_t color){xz_pixel_text(c,x,y,s,size,width/(size*6),color);}

static int toggle(enum xz_ui_action_kind k){
 return k==XZ_UI_ENABLE||k==XZ_UI_STEMS_OVERLAY||k==XZ_UI_PAD_FEEDBACK||k==XZ_UI_JUMP_ENABLE||k==XZ_UI_JUMP_SHIFT||k==XZ_UI_SHIFT_PAGES||k==XZ_UI_SHIFT_KEYSYNC||k==XZ_UI_TAKEOVER_TOGGLE||k==XZ_UI_HOLD||k==XZ_UI_OVERDUB||k==XZ_UI_CONNECTION_ENABLE||k==XZ_UI_DISCOVERY;
}

/* Static glyph trails stay in the margins, outside every control and readout. */
static void rain(struct xz_canvas c,int seed){
 static const unsigned char glyph[6][7]={{31,4,31,4,20,12,4},{17,31,1,31,16,16,31},{4,31,4,14,21,4,4},{31,17,31,4,31,4,4},{1,2,4,8,16,31,4},{21,21,31,4,31,4,28}};
 for(int side=0;side<2;side++)for(int row=0;row<36;row++){
  unsigned hash=(unsigned)(row+seed*7+side*13);uint32_t col=hash%9==0?HEAD:hash%3==0?TAIL:DARK;
  for(int y=0;y<7;y++)for(int x=0;x<5;x++)if(glyph[hash%6][y]&(16u>>x))xz_rect(c,side?788+x:6+x,34+row*11+y,1,1,col);
 }
}
static void bar(struct xz_canvas c,int x,int y,int w,int value,uint32_t color){
 int cells=w/8;for(int k=0;k<cells;k++)xz_rect(c,x+k*8,y,6,8,k*100<value*cells?color:DARK);
}
static void render(const struct xz_ui_scene *s){
 struct xz_canvas c=s->c;const struct xz_ui *u=s->u;const struct xz_ui_model *m=s->m;const struct xz_ui_deck *d=s->d;char buf[128];
 xz_rect(c,0,0,800,480,0);rain(c,(int)u->page+u->deck);
 xz_rect(c,0,0,800,40,GREEN);txt(c,148,10,140,"XZ MODS",2,0);txt(c,12,50,108,"XDJ-XZ",1,GREEN);
 if(u->page!=XZ_UI_SETTINGS&&u->page!=XZ_UI_STEMS&&u->page!=XZ_UI_XPAD)txt(c,520,12,184,"vj.tools/xzmods",1,0);
 for(size_t i=0;i<s->n;i++){
  struct xz_ui_widget a=s->w[i];int available=s->look[i].available,selected=s->look[i].selected;
  uint32_t ink=selected&&available?0:available?GREEN:WARN,bg=selected&&available?GREEN:0;
  xz_rect(c,a.x,a.y,a.w,a.h,bg);xz_border(c,a.x,a.y,a.w,a.h,available?GREEN:TAIL);
  struct xz_canvas box=xz_sub(c,a.x+8,a.y+4,a.w-16,a.h-8);
  if(a.kind==XZ_UI_STRIP){
   for(int k=0;k<6;k++){int x=a.x+k*a.w/6;xz_rect(c,x,a.y,1,a.h,TAIL);txt(c,x+8,a.y+6,a.w/6-12,xz_ui_lengths[k],2,GREEN);}
   xz_rect(c,a.x,a.y+a.h/2,a.w,1,TAIL);
   if(available&&d->loop_index>=0&&d->loop_index<6){float pitch=d->pitch<-12?-12:d->pitch>12?12:d->pitch;int y=a.y+(int)((12-pitch)*(a.h-4)/24);xz_rect(c,a.x+d->loop_index*a.w/6+4,y,a.w/6-8,4,HEAD);}
   txt(c,a.x+8,a.y+a.h-16,a.w-16,available?"PITCH +12 / -12":"NOT READY",1,available?GREEN:WARN);continue;
  }
  if(a.kind==XZ_UI_VOLUME){
   snprintf(buf,sizeof(buf),"VOLUME %d%%",percent(d->sample_volume));txt(box,0,4,box.width,available?buf:"NOT READY",1,ink);bar(box,0,26,box.width,available?percent(d->sample_volume):0,GREEN);continue;
  }
  const char *label=a.label,*state="";
  if(a.kind==XZ_UI_DECK){snprintf(buf,sizeof(buf),"DECK %d",a.index+1);label=buf;}
  else if(a.kind==XZ_UI_KEY_SHIFT&&a.index==0){snprintf(buf,sizeof(buf),"%+d RESET",d->key_semitones);label=buf;}
  if(!available)state=a.kind==XZ_UI_CONNECTION_ENABLE||a.kind==XZ_UI_DISCOVERY?"READ ONLY":"NOT READY";
  else if(toggle(a.kind))state=selected?"ON":"OFF";
  else if(a.kind==XZ_UI_SET_THEME)state=selected?"SELECTED":"";
  else if(a.kind==XZ_UI_GROOVE_PAD)state=d->groove_active==a.index?"PLAYING":"READY";
  else if(a.kind==XZ_UI_HOTCUE_PAD)state="HOT CUE";
  else if(a.kind==XZ_UI_SAMPLE_PAD)state=selected?"PLAYING":"READY";
  int scale=(int)strlen(label)*12<=box.width?2:1;
  txt(box,0,4,box.width,label,scale,ink);
  if(*state)txt(box,0,box.height-10,box.width,state,1,ink);
  if(a.kind==XZ_UI_GROOVE_PAD)xz_rect(c,a.x+a.w-8,a.y+3,5,a.h-6,s->look[i].color);
 }
 xz_ui_page_details(s);
 xz_rect(c,0,452,800,28,DARK);txt(c,212,462,580,xz_ui_footer_text(u,m),1,u->notice[0]?WARN:HEAD);
}
static void render_inline(const struct xz_ui_scene *s){
 struct xz_canvas c=s->c;xz_rect(c,0,0,c.width,c.height,0);
 for(size_t i=0;i<s->n;i++){
  struct xz_ui_widget a=s->w[i];const struct xz_ui_deck *d=&s->m->deck[a.deck];int available=s->look[i].available,selected=s->look[i].selected;char buf[24];
  xz_border(c,a.x,a.y,a.w,a.h,TAIL);
  if(selected)xz_rect(c,a.x+2,a.y+2,a.w-4,16,GREEN);
  txt(c,a.x+6,a.y+4,a.w-12,a.label,1,selected?0:s->look[i].color);
  if(a.kind==XZ_UI_MUTE){
   int value=percent(d->levels[a.index]);if(available&&!selected&&!d->stem_loading)bar(c,a.x+6,a.y+34,a.w-48,value,GREEN);
   snprintf(buf,sizeof(buf),"%d%%",value);txt(c,a.x+a.w-36,a.y+34,32,buf,1,HEAD);
   const char *state=!available?"NOT READY":d->stem_loading?"LOADING":selected?"MUTED":"";
   txt(c,a.x+6,a.y+34,a.w-48,state,1,WARN);
  }else {snprintf(buf,sizeof(buf),"D%d BYPASS %s",a.deck+1,selected?"ON":"OFF");txt(c,a.x+6,a.y+34,a.w-12,available?buf:"NOT READY",1,available?HEAD:WARN);}
 }
}
static void render_buttons(struct xz_canvas c,int stems,int vj_visible,int vj_active){
 const int x[3]={744,674,0},w[3]={56,68,112};const char *label[3]={"MODS","STEMS",vj_active?"EXIT VJ":"VJ.TOOLS"};
 for(int i=0;i<(vj_visible?3:2);i++){int on=i==1?stems:i==2?vj_active:0;xz_rect(c,x[i],0,w[i],24,on?GREEN:0);xz_border(c,x[i],0,w[i],24,GREEN);txt(c,x[i]+6,8,w[i]-12,label[i],1,on?0:HEAD);}
}
const struct xz_ui_skin xz_skin_matrix={xz_ui_place,render,render_inline,render_buttons};
