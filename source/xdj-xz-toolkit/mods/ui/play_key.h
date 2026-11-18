/* SPDX-License-Identifier: MIT */
#ifndef XZ_PLAY_KEY_INCLUDED
#define XZ_PLAY_KEY_INCLUDED
#include "ui_skin.h"
#include <stdio.h>
#include <string.h>

/* Transient PLAY controls. The engine still owns readiness and applied key text.
 * No settings, native addresses, software-player commands or new action IDs. */
struct xz_play_key {
 int open,deck,down,capture,cancelled,setup,step;
 int classic_side,selected_deck; /* implicit side/selection for either host; -1 side after explicit choice */
 uint32_t source_frame; /* MODERN implicit origin; zero after an explicit native choice */
 uint64_t instance,revision,painted_revision,contact_revision;
};
struct xz_play_key_view {
 uint32_t ready,native_ready[2]; int theme,synced,native_engine,native_sync,live_verified;
 char key[48],notice[128],native_key[2][48];
};
enum { XZ_PLAY_KEY_X=124, XZ_PLAY_KEY_Y=84, XZ_PLAY_KEY_W=620, XZ_PLAY_KEY_H=276 };
static inline void xz_play_key_open(struct xz_play_key *p,int deck){
 *p=(struct xz_play_key){.open=1,.deck=deck,.down=1,.capture=-1,.classic_side=-1,.revision=1};
}
static inline void xz_play_key_changed(struct xz_play_key *p){p->revision++;p->painted_revision=0;}
/* Called by the host only after this exact popup state was drawn successfully.
 * A render of an old copy must never authorize a newer choice or popup instance. */
static inline void xz_play_key_published(struct xz_play_key *live,const struct xz_play_key *painted){
 if(live->open&&painted->open&&live->instance==painted->instance&&live->revision==painted->revision&&
    live->deck==painted->deck&&live->setup==painted->setup&&live->step==painted->step)
  live->painted_revision=painted->revision;
}
static inline void xz_play_key_view(const struct xz_play_key *p,const struct xz_ui_model *m,const char *notice,struct xz_play_key_view *v){
 memset(v,0,sizeof *v);v->theme=m->theme;v->native_engine=-1;
 for(int d=0;d<2;d++){
  v->native_ready[d]=m->deck[d].ready;
  snprintf(v->native_key[d],sizeof v->native_key[d],"%s",m->deck[d].key_readout[0]?m->deck[d].key_readout:"---");
 }
 if(p->deck>=0&&p->deck<4){
  v->ready=m->deck[p->deck].ready;v->synced=m->deck[p->deck].key_synced;
  snprintf(v->key,sizeof v->key,"%s",m->deck[p->deck].key_readout[0]?m->deck[p->deck].key_readout:"---");
 }else snprintf(v->key,sizeof v->key,"---");
 snprintf(v->notice,sizeof v->notice,"%s",notice?notice:"");
}
static inline int xz_play_key_ready(const struct xz_play_key *p,uint32_t ready,int which){
 if(p->deck<0||p->deck>3)return 0;
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
   p->capture=hit;p->cancelled=0;p->contact_revision=p->revision;
   int native=hit>=0&&hit<4&&p->deck<4&&!p->setup;
   int choice=0;
   if((native||choice||hit==4)&&p->painted_revision!=p->revision)p->capture=-1;
   if(x<XZ_PLAY_KEY_X||x>=XZ_PLAY_KEY_X+XZ_PLAY_KEY_W||y<XZ_PLAY_KEY_Y||y>=XZ_PLAY_KEY_Y+XZ_PLAY_KEY_H)p->open=0;
  }else if(hit!=p->capture)p->cancelled=1;
  p->down=1;return 0;
 }
 if(!p->down)return 0;
 p->down=0;
 int at=p->capture;p->capture=-1;
 if(p->cancelled||at<0||p->contact_revision!=p->revision||((x||y)&&hit!=at))return 0;
 if(at==5){p->open=0;return 0;}
 uint32_t ready=p->deck>=0&&p->deck<4?m->deck[p->deck].ready:0;
 if(at==4){
  if(p->deck<0||p->deck>3)return 0;
  if(!p->setup&&(ready&(XZ_UI_KEY|XZ_UI_KEYSYNC))==(XZ_UI_KEY|XZ_UI_KEYSYNC))return 0;
  if(!p->setup){p->setup=1;p->step=0;xz_play_key_changed(p);return 0;}
  if(p->step<3){p->step++;xz_play_key_changed(p);}
  
  *out=(struct xz_ui_action){.kind=XZ_UI_KEY_CHECK,.phase=XZ_UI_PRESS,.deck=p->deck};return 1;
 }
 if(p->setup||p->painted_revision!=p->revision||!xz_play_key_ready(p,ready,at))return 0;
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
 char title[80];snprintf(title,sizeof title,"DECK %d KEY  %s%s",p->deck+1,v->key,v->synced?"  SYNC":"");
 xz_ui_chrome_fit(c,XZ_PLAY_KEY_X+16,XZ_PLAY_KEY_Y+16,XZ_PLAY_KEY_W-32,title,2,ink);
 const char *reason=!(v->ready&XZ_UI_KEY)?"LOAD AN ANALYZED TRACK / SET UP KEYS":
  !(v->ready&XZ_UI_KEYSYNC)?"SELECT A LOADED LOCAL MASTER / SET UP KEYS":
  "SHIFT + SYNC CAPTURES THE SELECTED MASTER KEY";
 xz_ui_chrome_fit(c,XZ_PLAY_KEY_X+16,XZ_PLAY_KEY_Y+56,XZ_PLAY_KEY_W-32,reason,1,dim);
 for(int i=0;i<4;i++){
  struct xz_ui_widget w=xz_play_key_button(i);int available=!p->setup&&xz_play_key_ready(p,v->ready,i);
  xz_round_rect(c,w.x,w.y,w.w,w.h,4,15,available?edge:xz_blend(bg,ink,35));
  xz_text(c,w.x+10,w.y+15,w.label,1,20,available?ink:dim);
 }
 const char *line=v->notice;
 static const char *const steps[4]={"1 DETECT KEY ENGINE + TRACK","2 USE THIS DECK / SELECT A LOCAL MASTER","3 CONNECT ROUTE / LOAD ANALYZED TRACK","4 LIVE VERIFY: REQUEST MUST REACH PROCESSED OUTPUT"};
 if(p->setup)line=steps[p->step];
 xz_ui_chrome_fit(c,XZ_PLAY_KEY_X+16,XZ_PLAY_KEY_Y+156,XZ_PLAY_KEY_W-32,line,1,ink);
 if(p->setup&&v->notice[0])xz_ui_chrome_fit(c,XZ_PLAY_KEY_X+16,XZ_PLAY_KEY_Y+178,XZ_PLAY_KEY_W-32,v->notice,1,dim);
 if(p->setup||(v->ready&(XZ_UI_KEY|XZ_UI_KEYSYNC))!=(XZ_UI_KEY|XZ_UI_KEYSYNC)){
  struct xz_ui_widget w=xz_play_key_button(4);
  const char *label=!p->setup?"SET UP KEYS":p->step==0?"DETECT":p->step==1?"USE THIS DECK":p->step==2?"VERIFY LOADER + TRACKS":
   v->live_verified?"LIVE VERIFIED / DONE":"LIVE VERIFY AGAIN";
  xz_round_rect(c,w.x,w.y,w.w,w.h,4,15,edge);xz_text(c,w.x+12,w.y+15,label,1,48,ink);
 }
 struct xz_ui_widget close=xz_play_key_button(5);
 xz_round_rect(c,close.x,close.y,close.w,close.h,4,15,edge);xz_text(c,close.x+12,close.y+15,close.label,1,20,ink);
}
#endif
