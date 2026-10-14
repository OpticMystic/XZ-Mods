/* SPDX-License-Identifier: MIT */
#include "ui_skin.h"
#include <stdio.h>

static void caption(struct xz_canvas c,const struct xz_ui_widget *a,const char *value,int start,uint32_t color) {
 int compact=a->h<32,scale=compact?1:2;
 /* A long label (a reason such as NO TRACK LOADED) drops to the small face to stay in its slot. */
 if(scale==2&&xz_text_width(c,a->label,2,32)>a->w-12)scale=1;
 int y=compact?(a->h-12)/2:scale==1?8:4;
 xz_text(c,6-start,y,a->label,scale,32,color);
 int width=xz_text_width(c,value,1,16);
 xz_text(c,a->w-6-width-start,compact?y:a->h-14,value,1,16,color);
}
/* Compact words for a reason when the full status would run into the label. */
static const char *usb_short(enum xz_ui_usb_reason why){
 switch(why){
 case XZ_UI_USB_WHY_ENGINE:return "NO ENGINE";
 case XZ_UI_USB_WHY_OFF:return "OFF";
 case XZ_UI_USB_WHY_NO_TRACK:return "NO TRACK";
 case XZ_UI_USB_WHY_NOT_PREPARED:return "NO STEMS";
 case XZ_UI_USB_WHY_LOADING:return "LOAD";
 case XZ_UI_USB_WHY_ALIGNING:return "ALIGN";
 case XZ_UI_USB_WHY_ERROR:return "ERROR";
 case XZ_UI_USB_WHY_NONE:return "";
 case XZ_UI_USB_WHY_UNKNOWN:break;
 }
 return "WAIT";
}
/* The full reason when it fits beside (compact) or under (tall) the label. */
static const char *usb_fit(struct xz_canvas c,const struct xz_ui_widget *a,enum xz_ui_usb_reason why){
 const char *full=xz_ui_usb_reason_text(why);
 int room=a->w-12;
 if(a->h<32)room-=xz_text_width(c,a->label,1,32)+8;
 return xz_text_width(c,full,1,16)<=room?full:usb_short(why);
}
void xz_ui_stem_rows(const struct xz_ui_scene *s) {
 const struct xz_theme_palette *p=s->p;
 for(size_t i=0;i<s->n;i++) {
  struct xz_ui_widget a=s->w[i];const struct xz_ui_deck *d=&s->m->deck[a.deck];
  int available=s->look[i].available,selected=s->look[i].selected;
  /* Role 3 is BASS on 4-stem decks; the palette carries only the three shared roles. */
  int role=a.kind==XZ_UI_MUTE&&a.index>=0&&a.index<4?a.index:0;
  int action=a.kind==XZ_UI_LOCAL_STEM_SETUP_OPEN||a.kind==XZ_UI_ENABLE;
  uint32_t color=a.kind==XZ_UI_MUTE?(role<3?p->stem[role]:xz_ui_model_local_stem_color(s->m,role)):action?p->accent:p->alarm;
  float level=a.kind==XZ_UI_MUTE?d->levels[role]:action?1:selected?1:0;
  if(level<0)level=0;else if(level>1)level=1;
  int stored=(int)(level*100+.5f);
  if(!available||(a.kind==XZ_UI_MUTE&&selected))level=0;
  int fill=(int)(level*(a.w-4)+.5f);
  uint32_t bg=xz_blend(p->bg,p->ink,20);
  xz_frame(s->c,s->m->theme,a,bg,0);
  struct xz_canvas body=xz_sub(s->c,a.x+2,a.y+2,a.w-4,a.h-4);
  int radius=s->m->theme==XZ_THEME_LCARS?body.height/2:3;
  xz_round_rect(body,0,0,body.width,body.height,radius,15,bg);
  if(fill>0) {
   struct xz_canvas filled=body;filled.width=fill;
   xz_round_rect(filled,0,0,body.width,body.height,radius,15,color);
  }
  xz_border(s->c,a.x,a.y,a.w,a.h,available?color:xz_blend(p->bg,p->ink,96));
  /* The deck's one reason sits on its trailing slot; locked stems only dim. */
  enum xz_ui_usb_reason why=xz_ui_usb_blocked(s->m,a.deck);
  const char *fix=xz_ui_usb_action_label(why);
  char value[24];
  if(a.kind==XZ_UI_ENABLE)snprintf(value,sizeof(value),"ALL DECKS");
  else if(a.kind==XZ_UI_LOCAL_STEM_SETUP_OPEN)snprintf(value,sizeof(value),"%s",fix?usb_fit(s->c,&a,why):a.deck?"D2":"D1");
  else if(!available)snprintf(value,sizeof(value),"--");
  else if(a.kind==XZ_UI_BYPASS){
   if(why==XZ_UI_USB_WHY_ALIGNING||why==XZ_UI_USB_WHY_LOADING)snprintf(value,sizeof(value),"%s",usb_fit(s->c,&a,why));
   else snprintf(value,sizeof(value),"D%d %s",a.deck+1,selected?"ON":"OFF");
  }
  else if(selected)snprintf(value,sizeof(value),"MUTE %d%%",stored);
  else snprintf(value,sizeof(value),"%d%%",(int)(level*100+.5f));
  /* Text crossing the level boundary remains readable on both halves. */
  int split=fill?fill+2:0;
  if(split>0)caption(xz_sub(s->c,a.x,a.y,split,a.h),&a,value,0,
      xz_theme_luma(color)>142?0x000000:0xffffff);
  if(split<a.w)caption(xz_sub(s->c,a.x+split,a.y,a.w-split,a.h),&a,value,split,p->ink);
 }
}
