#include "ui_skin.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#ifdef NDEBUG
#error "Menu acceptance requires assertions"
#endif

static uint16_t frame[800*480+16];
static struct xz_ui_widget find(const struct xz_ui *u,const struct xz_ui_model *m,enum xz_ui_action_kind kind,int index){
 struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(u,m,w);
 for(size_t i=0;i<n;i++)if(w[i].kind==kind&&w[i].index==index)return w[i];
 assert(!"missing menu control");return w[0];
}
static size_t tap(struct xz_ui *u,const struct xz_ui_model *m,enum xz_ui_action_kind kind,int index,struct xz_ui_action out[XZ_UI_ACTIONS]){
 struct xz_ui_widget w=find(u,m,kind,index);struct xz_ui_action release[XZ_UI_ACTIONS];
 size_t n=xz_ui_touch(u,m,w.x+w.w/2,w.y+w.h/2,1,out);
 xz_ui_touch(u,m,w.x+w.w/2,w.y+w.h/2,0,release);return n;
}
static void geometry(const struct xz_ui *u,const struct xz_ui_model *m){
 struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(u,m,w);int rails=0;
 assert(n<=XZ_UI_WIDGETS);
 for(size_t i=0;i<n;i++){
  assert(w[i].x>=0&&w[i].y>=0&&w[i].x+w[i].w<=800&&w[i].y+w[i].h<=404);
  assert(w[i].w>=56&&w[i].h>=36);
  if(w[i].kind==XZ_UI_PANEL&&w[i].x==0&&w[i].y>=40)rails++;
  for(size_t k=i+1;k<n;k++)assert(w[i].x+w[i].w<=w[k].x||w[k].x+w[k].w<=w[i].x||w[i].y+w[i].h<=w[k].y||w[k].y+w[k].h<=w[i].y);
 }
 assert(rails==5);
}
static void capture(const struct xz_ui *u,const struct xz_ui_model *m,const char *name){
 for(int i=800*480;i<800*480+16;i++)frame[i]=0xa55a;
 assert(xz_ui_render(u,m,frame,800*480,800));
 for(int i=800*480;i<800*480+16;i++)assert(frame[i]==0xa55a);
 char file[80];snprintf(file,sizeof(file),"theme-%02d-%s.ppm",m->theme,name);
 FILE *f=fopen(file,"wb");assert(f);fprintf(f,"P6\n800 480\n255\n");
 for(int i=0;i<800*480;i++){
  unsigned v=frame[i];unsigned char p[3]={(unsigned char)(((v>>11)&31)*255/31),(unsigned char)(((v>>5)&63)*255/63),(unsigned char)((v&31)*255/31)};
  assert(fwrite(p,1,3,f)==3);
 }
 assert(!fclose(f));
}
int main(void){
 const int caps[]={XZ_UI_STEM,XZ_UI_GATE,XZ_UI_SMART,XZ_UI_PREVIEW,XZ_UI_SAMPLE};
 const enum xz_ui_page links[]={XZ_UI_CONTROLS,XZ_UI_BEAT_JUMP,XZ_UI_STEM_COLORS,XZ_UI_XPAD,XZ_UI_METRICS_SETUP,XZ_UI_PC_STEMS,XZ_UI_SETTINGS};
 struct xz_ui u;struct xz_ui_model m={0};struct xz_ui_action a[XZ_UI_ACTIONS];
 m.deck[0].track="TEST TRACK";m.deck[0].groove_active=m.deck[0].sample_active=-1;
 m.settings_status="SETTINGS SAVED TO USB";
 for(int theme=0;theme<XZ_THEME_COUNT;theme++){
  xz_ui_init(&u);assert(u.page==XZ_UI_GENERAL);m.theme=theme;
  m.deck[0].ready=m.deck[1].ready=0x3fff;m.enabled=0;m.metrics_enabled=0;m.jump.enabled=0;
  geometry(&u,&m);capture(&u,&m,"general-off");
  for(size_t k=0;k<sizeof(caps)/sizeof(*caps);k++){
   struct xz_ui_model saved=m;
   assert(tap(&u,&m,XZ_UI_ENABLE,caps[k],a)==1&&a[0].kind==XZ_UI_ENABLE&&a[0].index==caps[k]&&a[0].value==1);
   assert(!memcmp(&m,&saved,sizeof(m)));
  }
  assert(tap(&u,&m,XZ_UI_JUMP_ENABLE,0,a)==1&&a[0].value==1);
  {struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(&u,&m,w);for(size_t i=0;i<n;i++)assert(w[i].kind!=XZ_UI_METRICS_TOGGLE&&w[i].kind!=XZ_UI_RAM_DEBUG&&w[i].kind!=XZ_UI_JUMP_SHIFT);}
  m.enabled=0x3fff;m.jump.enabled=1;m.metrics_enabled=1;
  capture(&u,&m,"general-on");
  for(size_t k=0;k<sizeof(links)/sizeof(*links);k++){
   assert(tap(&u,&m,XZ_UI_FEATURE_LINK,links[k],a)==1&&u.page==links[k]);
   assert(u.history_count==1&&u.history[0]==XZ_UI_GENERAL);geometry(&u,&m);
   struct xz_ui_widget rail=find(&u,&m,XZ_UI_PANEL,links[k]==XZ_UI_CONTROLS||links[k]==XZ_UI_BEAT_JUMP||links[k]==XZ_UI_PC_STEMS?XZ_UI_CONTROLS:links[k]==XZ_UI_STEM_COLORS?XZ_UI_THEMES:XZ_UI_SETTINGS);
   assert(xz_ui_widget_look(&u,&m,&rail).selected);
   assert(xz_ui_back(&u,a)==1&&u.page==XZ_UI_GENERAL&&!u.history_count);
   struct xz_ui_widget focused;assert(xz_ui_focused(&u,&m,&focused)&&focused.kind==XZ_UI_FEATURE_LINK&&focused.index==(int)links[k]);
   assert(xz_ui_encoder_push(&u,&m,a)==1&&u.page==links[k]);
   assert(xz_ui_back(&u,a)==1&&u.page==XZ_UI_GENERAL);
  }
  assert(tap(&u,&m,XZ_UI_PANEL,XZ_UI_CONTROLS,a)==1&&!u.history_count);
  struct xz_ui_widget widgets[XZ_UI_WIDGETS];size_t count=xz_ui_layout(&u,&m,widgets);
  for(size_t i=0;i<count;i++)assert(widgets[i].kind!=XZ_UI_ENABLE);
  capture(&u,&m,"stem-settings");
  xz_ui_init(&u);m.enabled=0;m.deck[0].ready=m.deck[1].ready=0;geometry(&u,&m);
  capture(&u,&m,"general-setup");
  const struct xz_ui_widget unavailable=find(&u,&m,XZ_UI_LOCAL_STEM_SETUP_OPEN,XZ_UI_STEM);
  m.deck[1].ready=XZ_UI_STEM;
  const struct xz_ui_widget available=find(&u,&m,XZ_UI_ENABLE,XZ_UI_STEM);
  assert(available.x==unavailable.x&&available.y==unavailable.y&&available.w==unavailable.w&&available.h==unavailable.h);
  /* Row 0: USB STEMS for all decks beside PC STEMS SETUP; stem settings move to the bottom links. */
  const struct xz_ui_widget pc=find(&u,&m,XZ_UI_FEATURE_LINK,XZ_UI_PC_STEMS),settings=find(&u,&m,XZ_UI_FEATURE_LINK,XZ_UI_CONTROLS);
  const struct xz_ui_widget keys=find(&u,&m,XZ_UI_FEATURE_LINK,XZ_UI_SETTINGS);
  assert(pc.y==available.y&&pc.x>available.x&&strstr(available.label,"ALL DECKS"));
  assert(settings.y==keys.y&&settings.x<keys.x&&settings.y>available.y);
  /* An explicit OFF turns on in one tap even though the host cleared every ready bit. */
  m.deck[1].ready=0;m.deck[0].usb_reason=m.deck[1].usb_reason=XZ_UI_USB_WHY_OFF;
  assert(tap(&u,&m,XZ_UI_ENABLE,XZ_UI_STEM,a)==1&&a[0].kind==XZ_UI_ENABLE&&a[0].index==XZ_UI_STEM&&a[0].value==1);
  m.deck[0].usb_reason=m.deck[1].usb_reason=XZ_UI_USB_WHY_ENGINE;
  find(&u,&m,XZ_UI_LOCAL_STEM_SETUP_OPEN,XZ_UI_STEM);
  m.deck[0].usb_reason=m.deck[1].usb_reason=XZ_UI_USB_WHY_UNKNOWN;
 }
 for(size_t k=1;k<sizeof(caps)/sizeof(*caps);k++){
  xz_ui_init(&u);memset(&m,0,sizeof(m));
  assert(tap(&u,&m,XZ_UI_FEATURE_SETUP_OPEN,caps[k],a)==1&&u.page==XZ_UI_FEATURE_SETUP&&u.feature_cap==caps[k]);
  assert(tap(&u,&m,XZ_UI_FEATURE_SETUP_STEP,0,a)==0&&u.feature_step==0&&u.notice[0]);
  m.deck[1].ready=(uint32_t)caps[k];
  assert(tap(&u,&m,XZ_UI_DECK,1,a)==1&&u.deck==1);
  assert(tap(&u,&m,XZ_UI_FEATURE_SETUP_STEP,0,a)==0&&u.feature_step==1);
  assert(tap(&u,&m,XZ_UI_FEATURE_SETUP_STEP,1,a)==0&&u.feature_step==2);
  assert(tap(&u,&m,XZ_UI_FEATURE_SETUP_STEP,2,a)==1&&a[0].kind==XZ_UI_ENABLE&&a[0].value==1&&a[0].deck==1&&u.feature_step==3);
  assert(tap(&u,&m,XZ_UI_FEATURE_SETUP_STEP,3,a)==0&&u.page==XZ_UI_FEATURE_SETUP);
  m.enabled=(uint32_t)caps[k];m.deck[1].track="LOADED USB TRACK";
  assert(tap(&u,&m,XZ_UI_FEATURE_SETUP_STEP,3,a)==1&&u.page==XZ_UI_GENERAL);
 }
 puts("PASS General: feature actions, setup gates, fixed touch bounds, encoder and Back history in every theme");
 return 0;
}
