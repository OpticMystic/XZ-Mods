/* SPDX-License-Identifier: MIT */
#ifndef XZ_PLAY_KEY_INCLUDED
#define XZ_PLAY_KEY_INCLUDED
#include "ui_skin.h"
#include <stdio.h>
#include <string.h>

/* Transient PLAY controls. The engine still owns readiness and applied key text.
 * No settings, native addresses, software-player commands or new action IDs. */
struct xz_play_key { int open, deck, down, capture, cancelled, setup, step, classic_side; };
struct xz_play_key_view {
 uint32_t ready,native_ready[2]; int theme,synced,native_engine,native_sync;
 char key[48],notice[128],native_key[2][48];
};
enum { XZ_PLAY_KEY_X=124, XZ_PLAY_KEY_Y=84, XZ_PLAY_KEY_W=620, XZ_PLAY_KEY_H=276 };
static inline void xz_play_key_open(struct xz_play_key *p,int deck){
 *p=(struct xz_play_key){.open=1,.deck=deck,.down=1,.capture=-1,.classic_side=-1};
}
static inline void xz_play_key_view(const struct xz_play_key *p,const struct xz_ui_model *m,const char *notice,struct xz_play_key_view *v){
 memset(v,0,sizeof *v);v->theme=m->theme;v->native_engine=-1;
 for(int d=0;d<2;d++){
  v->native_ready[d]=m->deck[d].ready;
  snprintf(v->native_key[d],sizeof v->native_key[d],"%s",m->deck[d].key_readout[0]?m->deck[d].key_readout:"---");
 }
 if(p->deck>=0&&p->deck<2){
  v->ready=m->deck[p->deck].ready;v->synced=m->deck[p->deck].key_synced;
  snprintf(v->key,sizeof v->key,"%s",m->deck[p->deck].key_readout[0]?m->deck[p->deck].key_readout:"---");
 }else snprintf(v->key,sizeof v->key,"---");
 snprintf(v->notice,sizeof v->notice,"%s",notice?notice:"");
}
static inline int xz_play_key_ready(const struct xz_play_key *p,uint32_t ready,int which){
 if(p->deck<0||p->deck>1)return 0;
 return (ready&(which==3?XZ_UI_KEYSYNC:XZ_UI_KEY))!=0;
}
/* 0..3 KEY-/RESET/KEY+/SYNC, 4 setup/next, 5 close. Software-deck setup
 * explains the missing route and never sends a local-deck engine action. */
static inline struct xz_ui_widget xz_play_key_button(int which){
 static const char *const names[6]={"KEY -","RESET","KEY +","KEY SYNC","SET UP KEYS","CLOSE"};
 struct xz_ui_widget w={0};w.label=names[which];w.w=136;w.h=48;
 w.x=XZ_PLAY_KEY_X+16+which*148;w.y=XZ_PLAY_KEY_Y+92;
 if(which==4){w.x=XZ_PLAY_KEY_X+16;w.y=XZ_PLAY_KEY_Y+204;w.w=436;}
 if(which==5){w.x=XZ_PLAY_KEY_X+468;w.y=XZ_PLAY_KEY_Y+204;}
 return w;
}
static inline int xz_play_key_hit(int x,int y){
 for(int i=0;i<6;i++){struct xz_ui_widget w=xz_play_key_button(i);if(x>=w.x&&x<w.x+w.w&&y>=w.y&&y<w.y+w.h)return i;}
 return -1;
}
static inline int xz_play_key_touch(struct xz_play_key *p,const struct xz_ui_model *m,int x,int y,int down,struct xz_ui_action *out){
 if(!p->open)return 0;
 int hit=xz_play_key_hit(x,y);
 if(down){
  if(!p->down){
   p->capture=hit;p->cancelled=0;
   if(x<XZ_PLAY_KEY_X||x>=XZ_PLAY_KEY_X+XZ_PLAY_KEY_W||y<XZ_PLAY_KEY_Y||y>=XZ_PLAY_KEY_Y+XZ_PLAY_KEY_H)p->open=0;
  }else if(hit!=p->capture)p->cancelled=1;
  p->down=1;return 0;
 }
 if(!p->down)return 0;
 p->down=0;
 int at=p->capture;p->capture=-1;
 if(p->cancelled||at<0||((x||y)&&hit!=at))return 0;
 if(at==5){p->open=0;return 0;}
 if(p->deck>1){
  if(at==4){p->setup=1;return 0;}
  if(p->setup&&at<2){p->deck=at;p->setup=0;p->classic_side=-1;}
  return 0;
 }
 uint32_t ready=p->deck>=0&&p->deck<2?m->deck[p->deck].ready:0;
 if(at==4){
  if(p->deck<0||p->deck>3)return 0;
  if(!p->setup&&(ready&(XZ_UI_KEY|XZ_UI_KEYSYNC))==(XZ_UI_KEY|XZ_UI_KEYSYNC))return 0;
  if(!p->setup){p->setup=1;p->step=0;return 0;}
  if(p->step<3)p->step++;
  else if((ready&(XZ_UI_KEY|XZ_UI_KEYSYNC))==(XZ_UI_KEY|XZ_UI_KEYSYNC)){p->setup=0;return 0;}
  *out=(struct xz_ui_action){.kind=XZ_UI_KEY_CHECK,.phase=XZ_UI_PRESS,.deck=p->deck};return 1;
 }
 if(p->setup||!xz_play_key_ready(p,ready,at))return 0;
 *out=(struct xz_ui_action){.kind=at==3?XZ_UI_KEY_SYNC:XZ_UI_KEY_SHIFT,.phase=XZ_UI_PRESS,.deck=p->deck,.index=at==3?0:at-1,.value=at==3?1.f:(float)(at-1)};
 return 1;
}
static inline void xz_play_key_render(const struct xz_play_key *p,const struct xz_play_key_view *v,uint16_t *pixels,size_t stride){
 if(!p->open||!pixels||stride<800)return;
 const struct xz_theme_palette *pal=xz_theme_palette(v->theme);
 struct xz_canvas c={pixels,stride,800,480,v->theme};
 uint32_t bg=pal->bg,ink=pal->ink,dim=xz_blend(bg,ink,150),edge=xz_blend(bg,pal->accent,160);
 xz_round_rect(c,XZ_PLAY_KEY_X,XZ_PLAY_KEY_Y,XZ_PLAY_KEY_W,XZ_PLAY_KEY_H,8,15,edge);
 xz_round_rect(c,XZ_PLAY_KEY_X+2,XZ_PLAY_KEY_Y+2,XZ_PLAY_KEY_W-4,XZ_PLAY_KEY_H-4,6,15,bg);
 char title[80];snprintf(title,sizeof title,p->deck>1?"DECK %d TRACK KEY  %s%s":"NATIVE DECK %d KEY  %s%s",p->deck+1,v->key,v->synced?"  SYNC":"");
 xz_ui_chrome_fit(c,XZ_PLAY_KEY_X+16,XZ_PLAY_KEY_Y+16,XZ_PLAY_KEY_W-32,title,2,ink);
 const char *reason=p->deck>1?"TRACK METADATA ONLY / SOFTWARE PITCH SHIFT IS NOT IMPLEMENTED":
  v->native_engine==0?"NATIVE KEY ENGINE UNAVAILABLE IN THIS RUNTIME":
  !(v->ready&XZ_UI_KEY)?"NO ANALYZED LOCAL KEY / KEY ENGINE NOT VERIFIED":
  !(v->ready&XZ_UI_KEYSYNC)?(v->native_sync?"KEY SYNC NEEDS TWO ANALYZED LOCAL TRACKS":"KEY SYNC: NATIVE MASTER TEMPO ROUTE UNAVAILABLE"):
  "SHIFT + SYNC ALSO MATCHES THE OTHER LOCAL DECK";
 xz_ui_chrome_fit(c,XZ_PLAY_KEY_X+16,XZ_PLAY_KEY_Y+56,XZ_PLAY_KEY_W-32,reason,1,dim);
 if(p->deck>1){
  const char *status=v->native_engine<0?"NATIVE KEY ENGINE: STATUS UNREPORTED":v->native_engine?
   (v->native_sync?"NATIVE KEY ENGINE: DETECTED IN THIS RUNTIME":"NATIVE KEY ENGINE DETECTED / KEY SYNC ROUTE UNAVAILABLE"):
   "NATIVE KEY ENGINE: UNAVAILABLE IN THIS RUNTIME";
  xz_ui_chrome_fit(c,XZ_PLAY_KEY_X+16,XZ_PLAY_KEY_Y+156,XZ_PLAY_KEY_W-32,status,1,ink);
  if(p->setup)for(int d=0;d<2;d++){
   struct xz_ui_widget w=xz_play_key_button(d);char label[64];
   snprintf(label,sizeof label,"NATIVE DECK %d: %s",d+1,v->native_key[d]);
   xz_round_rect(c,w.x,w.y,w.w,w.h,4,15,edge);xz_ui_chrome_fit(c,w.x+8,w.y+16,w.w-16,label,1,ink);
  }
  xz_ui_chrome_fit(c,XZ_PLAY_KEY_X+16,XZ_PLAY_KEY_Y+178,XZ_PLAY_KEY_W-32,
   p->setup?"CHOOSE A NATIVE DECK EXPLICITLY / SOFTWARE AUDIO IS UNCHANGED":"KEY SHIFT / KEY SYNC REQUIRE A FUTURE SOFTWARE AUDIO ENGINE",1,dim);
  struct xz_ui_widget primary=xz_play_key_button(4),close=xz_play_key_button(5);
  xz_round_rect(c,primary.x,primary.y,primary.w,primary.h,4,15,edge);
  xz_text(c,primary.x+12,primary.y+15,p->setup?"RECHECK INSTALLED CAPABILITY":"AVAILABLE NATIVE DECKS",1,48,ink);
  xz_round_rect(c,close.x,close.y,close.w,close.h,4,15,edge);xz_text(c,close.x+12,close.y+15,close.label,1,20,ink);
  return;
 }
 for(int i=0;i<4;i++){
  struct xz_ui_widget w=xz_play_key_button(i);int available=!p->setup&&xz_play_key_ready(p,v->ready,i);
  xz_round_rect(c,w.x,w.y,w.w,w.h,4,15,available?edge:xz_blend(bg,ink,35));
  xz_text(c,w.x+10,w.y+15,w.label,1,20,available?ink:dim);
 }
 const char *line=v->notice;
 static const char *const steps[4]={"1 DETECT KEY ENGINE + ANALYZED TRACKS","2 CHOOSE THE TAPPED LOCAL DECK","3 CONNECT: LOAD ANALYZED TRACKS / CURRENT USB LOADER","4 LIVE VERIFY: BOTH LOCAL KEYS + MASTER TEMPO"};
 if(p->setup)line=steps[p->step];
 xz_ui_chrome_fit(c,XZ_PLAY_KEY_X+16,XZ_PLAY_KEY_Y+156,XZ_PLAY_KEY_W-32,line,1,ink);
 if(p->setup&&v->notice[0])xz_ui_chrome_fit(c,XZ_PLAY_KEY_X+16,XZ_PLAY_KEY_Y+178,XZ_PLAY_KEY_W-32,v->notice,1,dim);
 if(p->setup||(v->ready&(XZ_UI_KEY|XZ_UI_KEYSYNC))!=(XZ_UI_KEY|XZ_UI_KEYSYNC)){
  struct xz_ui_widget w=xz_play_key_button(4);
  const char *label=!p->setup?"SET UP KEYS":p->step==0?"DETECT":p->step==1?"USE THIS DECK":p->step==2?"VERIFY LOADER + TRACKS":
   (v->ready&(XZ_UI_KEY|XZ_UI_KEYSYNC))==(XZ_UI_KEY|XZ_UI_KEYSYNC)?"LIVE VERIFIED / DONE":"LIVE VERIFY AGAIN";
  xz_round_rect(c,w.x,w.y,w.w,w.h,4,15,edge);xz_text(c,w.x+12,w.y+15,label,1,48,ink);
 }
 struct xz_ui_widget close=xz_play_key_button(5);
 xz_round_rect(c,close.x,close.y,close.w,close.h,4,15,edge);xz_text(c,close.x+12,close.y+15,close.label,1,20,ink);
}
#endif
