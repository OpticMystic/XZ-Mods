#include "ui_skin.h"
#include <stdio.h>
#include <string.h>

enum { METAL=0xd9c9a3, INK=0x3a3020, GLASS=0x1a1408, CREAM=0xf2e6c9, AMBER=0xffa33c, RED=0xcc3322 };

static int percent(float v){return v<=0?0:v>=1?100:(int)(v*100+.5f);}
static void txt(struct xz_canvas c,int x,int y,int w,const char *s,int scale,uint32_t color){struct xz_canvas box=xz_sub(c,x,y,w,c.height-y);xz_text(box,0,0,s,scale,200,color);}
static void engraved(struct xz_canvas c,int x,int y,int w,const char *s){txt(c,x+1,y+1,w,s,2,CREAM);txt(c,x,y,w,s,2,INK);}

static void lamp(struct xz_canvas c,int x,int y,int on,uint32_t color){xz_disc(c,x,y,5,INK);xz_disc(c,x,y,3,on?color:0x766644);if(on)xz_rect(c,x-1,y-2,2,1,CREAM);}
static int toggle(enum xz_ui_action_kind k){return k==XZ_UI_ENABLE||k==XZ_UI_STEMS_OVERLAY||k==XZ_UI_PAD_FEEDBACK||k==XZ_UI_JUMP_ENABLE||k==XZ_UI_JUMP_SHIFT||k==XZ_UI_SHIFT_PAGES||k==XZ_UI_SHIFT_KEYSYNC||k==XZ_UI_TAKEOVER_TOGGLE||k==XZ_UI_HOLD||k==XZ_UI_OVERDUB||k==XZ_UI_CONNECTION_ENABLE||k==XZ_UI_DISCOVERY;}
static void metal(struct xz_canvas c){
 for(int y=0;y<c.height;y++){unsigned h=(unsigned)y*2654435761u;uint32_t color=h%7==0?0xd2c29c:h%11==0?0xe0d0aa:METAL;xz_rect(c,0,y,c.width,1,color);}
}
static void well(struct xz_canvas c,int x,int y,int w,int h){xz_rect(c,x,y,w,h,INK);xz_rect(c,x+2,y+2,w-4,h-4,GLASS);xz_rect(c,x,y+h-1,w,1,CREAM);}
static void fader(struct xz_canvas c,int x,int y,int w,int value){
 xz_rect(c,x,y,w,3,GLASS);for(int k=0;k<5;k++)xz_rect(c,x+k*(w-1)/4,y-4,1,11,INK);
 int px=x+(w-8)*value/100;xz_rect(c,px,y-5,8,13,INK);xz_rect(c,px+1,y-4,6,11,CREAM);xz_rect(c,px+3,y-3,2,9,RED);
}

static void render(const struct xz_ui_scene *s){
 struct xz_canvas c=s->c;const struct xz_ui *u=s->u;const struct xz_ui_model *m=s->m;const struct xz_ui_deck *d=s->d;char buf[128];metal(c);
 engraved(c,148,10,140,"XZ MODS");txt(c,12,50,108,"XDJ-XZ",1,INK);
 if(u->page!=XZ_UI_SETTINGS&&u->page!=XZ_UI_STEMS&&u->page!=XZ_UI_XPAD)txt(c,500,12,208,"ANALOG / vj.tools/xzmods",1,INK);
 xz_rect(c,148,39,644,1,INK);
 for(int k=0;k<4;k++){int x=k&1?786:13,y=k&2?438:14;xz_disc(c,x,y,5,INK);xz_disc(c,x,y,4,0xae9e78);xz_line16(c,(x-2)*16,(y+2)*16,(x+2)*16,(y-2)*16,16,INK);}
 for(size_t i=0;i<s->n;i++){
  struct xz_ui_widget a=s->w[i];int available=s->look[i].available,selected=s->look[i].selected;
  if(a.kind==XZ_UI_STRIP){
   well(c,a.x,a.y,a.w,a.h);
   for(int k=0;k<6;k++){
    int x=a.x+k*a.w/6;txt(c,x+12,a.y+8,a.w/6-16,xz_ui_lengths[k],2,CREAM);
    for(int tick=0;tick<6;tick++)xz_rect(c,x+tick*a.w/36,a.y+35,1,tick?7:14,CREAM);
    xz_rect(c,x,a.y+48,1,a.h-50,0x584a30);
   }
   xz_rect(c,a.x,a.y+a.h/2,a.w,1,0x584a30);
   if(available&&d->loop_index>=0&&d->loop_index<6){float pitch=d->pitch<-12?-12:d->pitch>12?12:d->pitch;int x=a.x+(d->loop_index*2+1)*a.w/12,y=a.y+(int)((12-pitch)*(a.h-4)/24);xz_rect(c,x,a.y+2,2,a.h-4,RED);xz_rect(c,x-12,y,26,3,AMBER);}
   txt(c,a.x+8,a.y+a.h-16,a.w-16,available?"PITCH +12 / -12":"NOT READY",1,available?CREAM:AMBER);continue;
  }
  if(a.kind==XZ_UI_VOLUME){
   snprintf(buf,sizeof(buf),"VOLUME %d%%",percent(d->sample_volume));txt(c,a.x+8,a.y+2,a.w-16,available?buf:"NOT READY",1,INK);fader(c,a.x+8,a.y+30,a.w-16,available?percent(d->sample_volume):0);continue;
  }
  xz_rect(c,a.x,a.y,a.w,a.h,selected?0xb5a580:0xe0d0aa);xz_border(c,a.x,a.y,a.w,a.h,INK);
  xz_rect(c,a.x+1,a.y+1,a.w-2,2,selected?0x897953:CREAM);xz_rect(c,a.x+2,a.y+a.h-3,a.w-4,2,selected?CREAM:0x897953);
  struct xz_canvas box=xz_sub(c,a.x+10,a.y+4,a.w-20,a.h-8);const char *label=a.label,*state="";
  if(a.kind==XZ_UI_DECK){snprintf(buf,sizeof(buf),"DECK %d",a.index+1);label=buf;}
  else if(a.kind==XZ_UI_KEY_SHIFT&&a.index==0){snprintf(buf,sizeof(buf),"%+d RESET",d->key_semitones);label=buf;}
  if(!available)state=a.kind==XZ_UI_CONNECTION_ENABLE||a.kind==XZ_UI_DISCOVERY?"READ ONLY":"NOT READY";
  else if(toggle(a.kind))state=selected?"ON":"OFF";
  else if(a.kind==XZ_UI_SET_THEME)state=selected?"SELECTED":"";
  else if(a.kind==XZ_UI_GROOVE_PAD)state=d->groove_active==a.index?"PLAYING":"READY";
  else if(a.kind==XZ_UI_HOTCUE_PAD)state="HOT CUE";
  else if(a.kind==XZ_UI_SAMPLE_PAD)state=selected?"PLAYING":"READY";
  int scale=xz_text_width(c,label,2,120)<=box.width-22?2:1;
  lamp(c,a.x+a.w-16,a.y+13,available&&selected,a.kind==XZ_UI_CLOSE?RED:a.kind==XZ_UI_GROOVE_PAD?s->look[i].color:AMBER);
  txt(box,0,2,box.width-20,label,scale,INK);
  if(toggle(a.kind)&&available){
   int y=box.height-10;xz_round_rect(box,0,y-1,30,9,4,15,GLASS);xz_round_rect(box,selected?18:2,y,10,7,3,15,CREAM);
   txt(box,38,y-1,box.width-40,state,1,INK);
  }else if(*state){if(!available)xz_rect(box,0,box.height-13,box.width,13,RED);txt(box,3,box.height-11,box.width-6,state,1,available?INK:CREAM);}
 }
 xz_ui_page_details(s);
 well(c,0,452,800,28);txt(c,212,462,580,xz_ui_footer_text(u,m),1,u->notice[0]?AMBER:CREAM);
}
static void render_inline(const struct xz_ui_scene *s){
 struct xz_canvas c=s->c;metal(c);
 for(size_t i=0;i<s->n;i++){
  struct xz_ui_widget a=s->w[i];const struct xz_ui_deck *d=&s->m->deck[a.deck];int available=s->look[i].available,selected=s->look[i].selected;char buf[24];
  xz_border(c,a.x,a.y,a.w,a.h,INK);txt(c,a.x+6,a.y+3,a.w-26,a.label,1,INK);
  int value=a.kind==XZ_UI_MUTE?percent(d->levels[a.index]):0;
  lamp(c,a.x+a.w-12,a.y+8,available&&(a.kind==XZ_UI_MUTE?!selected&&value>0:selected),a.kind==XZ_UI_MUTE?s->look[i].color:RED);
  if(a.kind==XZ_UI_MUTE){
   if(available&&!selected&&!d->stem_loading)fader(c,a.x+7,a.y+36,a.w-54,value);snprintf(buf,sizeof(buf),"%d%%",value);
   txt(c,a.x+a.w-38,a.y+32,34,buf,1,INK);
   const char *state=!available?"NOT READY":d->stem_loading?"LOADING":selected?"MUTED":"";
   txt(c,a.x+6,a.y+32,a.w-50,state,1,RED);
  }else {snprintf(buf,sizeof(buf),"D%d  %s",a.deck+1,selected?"ON":"OFF");txt(c,a.x+8,a.y+32,a.w-16,available?buf:"NOT READY",1,INK);}
 }
}
static void render_buttons(struct xz_canvas c,int stems,int vj_visible,int vj_active){
 const int x[3]={744,674,0},w[3]={56,68,112};const char *labels[3]={"MODS","STEMS",vj_active?"EXIT VJ":"VJ.TOOLS"};
 for(int i=0;i<(vj_visible?3:2);i++){int on=i==1?stems:i==2?vj_active:0;xz_rect(c,x[i],0,w[i],24,METAL);xz_border(c,x[i],0,w[i],24,INK);xz_rect(c,x[i]+3,3,4,18,on?AMBER:INK);txt(c,x[i]+12,8,w[i]-14,labels[i],1,INK);}
}
const struct xz_ui_skin xz_skin_analog={xz_ui_place,render,render_inline,render_buttons};
