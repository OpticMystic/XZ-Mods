#include "ui_skin.h"
#include <stdio.h>

static float clampf(float v,float lo,float hi){return v<lo?lo:v>hi?hi:v;}


static void deck_card(struct xz_canvas c,const struct xz_ui_widget *a,int selected,const struct xz_theme_palette *p){
 uint32_t edge=selected?p->accent:xz_blend(p->bg,p->ink,112);
 for(int row=0;row<a->h;row++){
  int width=a->w-9+row*9/(a->h-1);
  uint32_t fill=xz_blend(p->bg,selected?p->accent:p->ink,selected?48-row/2:34-row/3);
  xz_rect(c,a->x,a->y+row,width,1,fill);xz_rect(c,a->x+width-1,a->y+row,1,1,edge);
 }
 xz_rect(c,a->x,a->y,a->w-9,1,edge);xz_rect(c,a->x,a->y+a->h-1,a->w,1,edge);
 xz_rect(c,a->x,a->y,1,a->h,edge);xz_rect(c,a->x+5,a->y+6,selected?3:1,20,selected?p->ink:edge);
}
static void theme_card(struct xz_canvas c,const struct xz_ui_widget *a,int selected){
 const struct xz_theme_palette *sample=xz_theme_palette(a->index);
 struct xz_canvas own=c;own.theme=a->index;
 xz_frame(c,a->index,*a,sample->bg,selected);
 const char *state=selected?"SELECTED":"TAP TO APPLY";
 xz_text(own,a->x+10,a->y+7,a->label,1,(a->w-20)/6,sample->ink);
 xz_text(own,a->x+10,a->y+28,state,1,28,sample->ink);
}
static void render(const struct xz_ui_scene *s){
 struct xz_canvas c=s->c;const struct xz_ui *u=s->u;const struct xz_ui_model *m=s->m;
 const struct xz_ui_deck *d=s->d;const struct xz_theme_palette *p=s->p;uint32_t panel,dim;char buf[96];size_t i;
 panel=xz_blend(p->bg,p->ink,28);dim=xz_blend(p->bg,p->ink,154);xz_theme_background(xz_surface(c),m->theme,(struct xz_theme_rect){0,0,800,480});
 xz_theme_frame(xz_surface(c),m->theme,(struct xz_theme_rect){0,0,800,40},xz_blend(p->bg,p->ink,15),0,XZ_THEME_HEADER);
 xz_text(c,148,10,"XZ MODS",2,15,m->theme==9?0xffffff:p->ink);xz_text(c,12,50,"XDJ-XZ",1,20,m->theme==9?0xffffff:dim);
 if(u->page!=XZ_UI_SETTINGS&&u->page!=XZ_UI_STEMS&&u->page!=XZ_UI_XPAD)xz_text(c,580,12,"vj.tools/xzmods",1,18,m->theme==9?0xffffff:p->accent);
 for(i=0;i<s->n;i++){
  struct xz_ui_widget a=s->w[i];int available=s->look[i].available,selected=s->look[i].selected;uint32_t color=s->look[i].color;
  if(a.kind!=XZ_UI_DECK&&a.kind!=XZ_UI_SET_THEME){
   xz_frame(c,m->theme,a,available?(selected?xz_blend(panel,color,38):panel):xz_blend(p->bg,p->ink,14),available&&selected);
  }
  if(a.kind==XZ_UI_PANEL)xz_rect(c,a.x,a.y+a.h-3,a.w,3,selected?p->accent:xz_blend(p->bg,p->ink,58));
  if(a.kind==XZ_UI_MUTE||a.kind==XZ_UI_GROOVE_PAD)xz_rect(c,a.x,a.y,a.w,3,available?color:dim);
  if(a.kind==XZ_UI_PANEL&&a.index==XZ_UI_STEMS&&(m->enabled&XZ_UI_STEM)&&!d->bypass&&
      (d->levels[0]<1||d->levels[1]<1||d->levels[2]<1||d->muted||d->groove_active>=0))
      xz_rect(c,a.x+a.w-8,a.y+7,4,4,p->alarm);
  if(a.kind==XZ_UI_DECK){
   deck_card(c,&a,selected,p);snprintf(buf,sizeof(buf),"DECK %d",a.index+1);
   xz_text(c,a.x+14,a.y+4,buf,2,12,p->ink);xz_text(c,a.x+14,a.y+24,a.label,1,22,dim);
  }
  else if(a.kind==XZ_UI_SET_THEME)theme_card(c,&a,selected);
  else if(a.kind==XZ_UI_LEVEL||a.kind==XZ_UI_VOLUME){
   float value=a.kind==XZ_UI_LEVEL?d->levels[a.index]:d->sample_volume;
   int position=(int)(clampf(value,0,1)*(float)(a.w-16));
   uint32_t bar=a.kind==XZ_UI_LEVEL?p->stem[a.index]:p->accent;
   if(a.kind==XZ_UI_LEVEL&&(d->muted&(1u<<a.index)))bar=dim;
   xz_rect(c,a.x+8,a.y+27,a.w-16,2,dim);xz_rect(c,a.x+8,a.y+27,position,2,available?bar:dim);
   for(int tick=0;tick<5;tick++)xz_rect(c,a.x+8+(a.w-16)*tick/4,a.y+24,1,8,dim);
   xz_rect(c,a.x+5+position,a.y+20,6,16,available?p->ink:dim);
   xz_rect(c,a.x+5+position,a.y+20,6,3,available?bar:dim);
   snprintf(buf,sizeof(buf),"%d%%",(int)(clampf(value,0,1)*100+.5f));xz_text(c,a.x+8,a.y+5,buf,1,10,p->ink);
  }else if(a.kind==XZ_UI_STRIP){
   int k;for(k=0;k<6;k++){xz_rect(c,a.x+k*a.w/6,a.y,2,a.h,dim);xz_text(c,a.x+k*a.w/6+8,a.y+8,xz_ui_lengths[k],2,6,p->ink);}
   xz_rect(c,a.x,a.y+a.h/2,a.w,1,dim);xz_text(c,a.x+8,a.y+38,"+12",1,5,dim);xz_text(c,a.x+8,a.y+118,"-12",1,5,dim);
   if(d->loop_index>=0&&d->loop_index<6){int py=(int)((12-clampf(d->pitch,-12,12))*(a.h-4)/24);xz_rect(c,a.x+d->loop_index*a.w/6+4,a.y+py,a.w/6-8,3,available?p->accent:dim);}
  }else if(a.kind==XZ_UI_KEY_SHIFT&&a.index==0){
   if(available)snprintf(buf,sizeof(buf),"%+d",d->key_semitones);else snprintf(buf,sizeof(buf),"--");
   xz_text(c,a.x+8,a.y+6,buf,2,5,available?p->ink:dim);
   if(available)xz_text(c,a.x+8,a.y+30,"RESET",1,8,dim);
  }else{
   int scale=xz_text_width(c,a.label,2,100)>a.w-20?1:2;
   xz_text(c,a.x+8,a.y+8,a.label,scale,(a.w-16)/(6*scale),available?p->ink:dim);
   if(a.kind==XZ_UI_MUTE&&selected){
    xz_rect(c,a.x+a.w-63,a.y+10,55,17,p->alarm);
    xz_text(c,a.x+a.w-58,a.y+13,"MUTED",1,9,p->bg);
   }
   if(a.kind==XZ_UI_JUMP_ENABLE||a.kind==XZ_UI_JUMP_SHIFT||a.kind==XZ_UI_SHIFT_PAGES||a.kind==XZ_UI_PAD_FEEDBACK||a.kind==XZ_UI_SHIFT_KEYSYNC||a.kind==XZ_UI_TAKEOVER_TOGGLE||a.kind==XZ_UI_STEMS_OVERLAY)
    xz_text(c,a.x+a.w-40,a.y+18,selected?"ON":"OFF",1,6,selected?p->accent:dim);
   if(a.kind==XZ_UI_ENABLE)xz_text(c,a.x+a.w-78,a.y+16,!available?"NOT READY":selected?"ON":"OFF",1,12,available&&selected?p->accent:dim);
   if(a.kind==XZ_UI_CONNECTION_ENABLE||a.kind==XZ_UI_DISCOVERY)
    xz_text(c,a.x+8,a.y+34,m->connection.ready?(selected?"ON":"OFF"):"UNAVAILABLE",1,20,selected?p->accent:dim);
   if(a.kind==XZ_UI_GROOVE_PAD)xz_text(c,a.x+8,a.y+31,available?(d->groove_active==a.index?"PLAYING":"READY"):"NOT READY",1,12,available?color:dim);
   if(a.kind==XZ_UI_HOTCUE_PAD)xz_text(c,a.x+8,a.y+31,"HOT CUE",1,12,dim);
   if(a.kind==XZ_UI_SERVER_AUTO)xz_text(c,a.x+8,a.y+27,m->server_auto?"AUTO":"MANUAL",1,30,p->accent);
   if(a.kind==XZ_UI_SERVER_ADDRESS)xz_text(c,a.x+8,a.y+27,m->server_address&&*m->server_address?m->server_address:"NOT SET",1,58,dim);
  }
  if(!available&&a.kind!=XZ_UI_GROOVE_PAD&&a.kind!=XZ_UI_ENABLE)
   xz_text(c,a.x+a.w-64,a.kind==XZ_UI_LEVEL||a.kind==XZ_UI_VOLUME?a.y+5:a.y+a.h-12,
        a.kind==XZ_UI_CONNECTION_ENABLE||a.kind==XZ_UI_DISCOVERY?"READ ONLY":"NOT READY",1,10,dim);
 }
 xz_ui_page_details(s);
 xz_rect(c,0,451,800,29,xz_blend(p->bg,p->ink,20));xz_rect(c,0,451,800,1,xz_blend(p->bg,p->ink,90));
 xz_text(c,212,461,xz_ui_footer_text(u,m),1,90,u->notice[0]?p->alarm:dim);
}
static void render_inline(const struct xz_ui_scene *s){
 struct xz_canvas c=s->c;const struct xz_ui_model *m=s->m;const struct xz_theme_palette *p=s->p;
 xz_rect(c,0,0,c.width,c.height,p->bg);
 for(size_t i=0;i<s->n;i++){
  struct xz_ui_widget a=s->w[i];const struct xz_ui_deck *d=&m->deck[a.deck];
  int available=s->look[i].available,selected=s->look[i].selected;
  uint32_t color=available?p->ink:xz_blend(p->bg,p->ink,100);
  xz_rect(c,a.x,a.y,a.w,a.h,xz_blend(p->bg,p->ink,22));
  xz_border(c,a.x,a.y,a.w,a.h,xz_blend(p->bg,p->ink,100));
  if(a.kind==XZ_UI_MUTE){
   int on=available&&!selected&&d->levels[a.index]>0;
   xz_rect(c,a.x+1,a.y+1,a.w-2,a.h-2,xz_blend(p->bg,p->stem[a.index],on?150:32));
   xz_border(c,a.x,a.y,a.w,a.h,available?p->stem[a.index]:color);
   if(m->theme>=7)xz_frame(c,m->theme,a,xz_blend(p->bg,p->stem[a.index],on?70:20),on);
   xz_text(c,a.x+8,a.y+3,a.label,2,(a.w-16)/12,color);
   int bar=(int)((a.w-48)*clampf(selected?0:d->levels[a.index],0,1));
   xz_rect(c,a.x+6,a.y+a.h-10,a.w-48,4,xz_blend(p->bg,p->ink,60));
   if(bar)xz_rect(c,a.x+6,a.y+a.h-10,bar,4,on?p->stem[a.index]:color);
   char value[8];snprintf(value,sizeof(value),"%d%%",(int)(clampf(selected?0:d->levels[a.index],0,1)*100+.5f));
   xz_text(c,a.x+a.w-34,a.y+a.h-14,selected?"MUTE":value,1,5,color);
   if(d->stem_loading)xz_text(c,a.x+8,a.y+24,"LOADING",1,(a.w-16)/6,p->alarm);
   continue;
  }
  if(selected){xz_rect(c,a.x+1,a.y+1,a.w-2,a.h-2,xz_blend(p->bg,p->alarm,88));xz_border(c,a.x,a.y,a.w,a.h,p->alarm);}
  if(m->theme>=7)xz_frame(c,m->theme,a,p->bg,selected);
  xz_text(c,a.x+8,a.y+3,a.label,2,(a.w-16)/12,color);
  xz_text(c,a.x+8,a.y+32,selected?"ON":"OFF",1,6,selected?p->alarm:color);
  xz_text(c,a.x+a.w-29,a.y+32,a.deck?"D2":"D1",1,3,color);
 }
}
void xz_ui_render_badge(uint16_t *pixels,size_t stride){
 if(!pixels||stride<800)return;
 struct xz_canvas c={pixels,stride,800,480,0};
 xz_rect(c,744,0,56,24,0x15191e);xz_border(c,744,0,56,24,0xa8ceff);
 xz_text(c,751,5,"MODS",2,4,0xf4f5f6);
}
void xz_ui_render_stems_button(uint16_t *pixels,size_t stride,int enabled){
 if(!pixels||stride<800)return;
 struct xz_canvas c={pixels,stride,800,480,0};uint32_t color=enabled?0x52d794:0xa8ceff;
 xz_rect(c,674,0,68,24,enabled?0x123c2a:0x15191e);xz_border(c,674,0,68,24,color);
 xz_text(c,681,5,"STEMS",2,5,color);
}
void xz_ui_render_vj_button(uint16_t *pixels,size_t stride,int takeover_active){
 if(!pixels||stride<800)return;
 struct xz_canvas c={pixels,stride,800,480,0};
 uint32_t border_col=takeover_active?0x52d794:0xa8ceff;
 xz_rect(c,0,0,112,24,0x15191e);xz_border(c,0,0,112,24,border_col);
 xz_text(c,7,5,takeover_active?"EXIT VJ":"VJ.TOOLS",2,8,takeover_active?0x52d794:0xf4f5f6);
}
static void render_buttons(struct xz_canvas c,int stems,int vj_visible,int vj_active){
 if(!c.theme){xz_ui_render_badge(c.p,c.stride);xz_ui_render_stems_button(c.p,c.stride,stems);if(vj_visible)xz_ui_render_vj_button(c.p,c.stride,vj_active);return;}
 const struct xz_theme_palette *p=xz_theme_palette(c.theme);
 struct xz_ui_widget buttons[3]={{744,0,56,24,XZ_UI_NONE,0,0,"MODS",-1},{674,0,68,24,XZ_UI_NONE,0,0,"STEMS",-1},{0,0,112,24,XZ_UI_NONE,0,0,vj_active?"EXIT VJ":"VJ.TOOLS",-1}};
 for(int i=0;i<(vj_visible?3:2);i++){
  struct xz_ui_widget a=buttons[i];xz_frame(c,c.theme,a,p->bg,i==1?stems:i==2?vj_active:0);
  xz_text(c,a.x+6,a.y+7,a.label,1,(a.w-12)/6,p->ink);
 }
}
const struct xz_ui_skin xz_skin_classic={xz_ui_place,render,render_inline,render_buttons};
