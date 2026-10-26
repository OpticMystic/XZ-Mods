#include "ui_skin.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#ifdef NDEBUG
#error "Menu acceptance requires assertions"
#endif

/* The MODS sections: HOME's tiles, the feature switches on PLAY and STEMS, their setup gates, and every link with
   its BACK history, the rail's lit section and the encoder, in every theme. (The file keeps its old name: it was
   the GENERAL page's test, and GENERAL's features now live in these sections.) */
static uint16_t frame[800*480+16];
static struct xz_ui_widget find(const struct xz_ui *u,const struct xz_ui_model *m,enum xz_ui_action_kind kind,int index){
 struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(u,m,w);
 for(size_t i=0;i<n;i++)if(w[i].kind==kind&&w[i].index==index)return w[i];
 assert(!"missing menu control");return w[0];
}
static int listed(const struct xz_ui *u,const struct xz_ui_model *m,enum xz_ui_action_kind kind,int index){
 struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(u,m,w);
 for(size_t i=0;i<n;i++)if(w[i].kind==kind&&w[i].index==index)return 1;
 return 0;
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
  if(w[i].kind==XZ_UI_PANEL&&w[i].x<XZ_UI_RAIL_W&&w[i].y>=40){rails++;assert(w[i].h>=40);}
  for(size_t k=i+1;k<n;k++)assert(w[i].x+w[i].w<=w[k].x||w[k].x+w[k].w<=w[i].x||w[i].y+w[i].h<=w[k].y||w[k].y+w[k].h<=w[i].y);
 }
 assert(rails==XZ_UI_SECTIONS);
}
static void capture(struct xz_ui *u,const struct xz_ui_model *m,const char *name){
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
static void no_retired_switches(const struct xz_ui *u,const struct xz_ui_model *m){
 struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(u,m,w);
 for(size_t i=0;i<n;i++)assert(w[i].kind!=XZ_UI_METRICS_TOGGLE&&w[i].kind!=XZ_UI_RAM_DEBUG&&w[i].kind!=XZ_UI_JUMP_SHIFT&&
  w[i].kind!=XZ_UI_SHIFT_PAGES&&w[i].kind!=XZ_UI_SHIFT_KEYSYNC&&w[i].kind!=XZ_UI_FOUR_WAVE_ORDER&&w[i].kind!=XZ_UI_CONTROL_SECTION);
}
int main(void){
 const int caps[]={XZ_UI_STEM,XZ_UI_GATE,XZ_UI_SMART,XZ_UI_PREVIEW,XZ_UI_SAMPLE};
 static const struct { enum xz_ui_page from, link; } links[]={
  {XZ_UI_PLAY,XZ_UI_BEAT_JUMP},{XZ_UI_PLAY,XZ_UI_KEY_SETUP},{XZ_UI_PLAY,XZ_UI_RAM_SETUP},{XZ_UI_PLAY,XZ_UI_BUTTONS},
  {XZ_UI_STEM_SECTION,XZ_UI_PC_STEMS},{XZ_UI_STEM_SECTION,XZ_UI_STEM_PADS},{XZ_UI_STEM_SECTION,XZ_UI_STEM_COLORS},
  {XZ_UI_SCREEN,XZ_UI_METRICS_SETUP}};
 struct xz_ui u;struct xz_ui_model m={0};struct xz_ui_action a[XZ_UI_ACTIONS];
 m.deck[0].track="TEST TRACK";m.deck[0].groove_active=m.deck[0].sample_active=-1;
 m.settings_status="SETTINGS SAVED TO USB";
 for(int theme=0;theme<XZ_THEME_COUNT;theme++){
  xz_ui_init(&u);assert(u.page==XZ_UI_HOME);m.theme=theme;
  m.deck[0].ready=m.deck[1].ready=0x3fff;m.enabled=0;m.metrics_enabled=0;m.jump.enabled=0;m.route.state=XZ_UI_ROUTE_NOT_DETECTED;
  geometry(&u,&m);no_retired_switches(&u,&m);capture(&u,&m,"home");
  /* HOME: each tile opens its section; BACK returns HOME onto that tile. */
  for(int s=1;s<XZ_UI_SECTIONS;s++){
   int section=xz_ui_sections[s];
   assert(tap(&u,&m,XZ_UI_FEATURE_LINK,section,a)==1&&a[0].kind==XZ_UI_PANEL&&u.page==(enum xz_ui_page)section);
   assert(u.history_count==1&&u.history[0]==XZ_UI_HOME);geometry(&u,&m);no_retired_switches(&u,&m);
   assert(xz_ui_widget_look(&u,&m,&(struct xz_ui_widget){0,0,0,0,XZ_UI_PANEL,section,0,"",-1}).selected);
   assert(xz_ui_back(&u,a)==1&&u.page==XZ_UI_HOME&&!u.history_count);
   struct xz_ui_widget focused;assert(xz_ui_focused(&u,&m,&focused)&&focused.kind==XZ_UI_FEATURE_LINK&&focused.index==section);
  }
  /* The cable is not there: the 4 DECKS tile's one button opens 4 DECKS. */
  assert(tap(&u,&m,XZ_UI_HOME_SETUP,XZ_UI_FOUR_DECKS,a)==1&&u.page==XZ_UI_FOUR_DECKS);
  assert(listed(&u,&m,XZ_UI_ROUTE_RESCAN,0));xz_ui_back(&u,a);assert(u.page==XZ_UI_HOME);
  /* PLAY and STEMS: one switch per stock-changing feature; each tap asks for ON without touching the snapshot. */
  tap(&u,&m,XZ_UI_PANEL,XZ_UI_PLAY,a);geometry(&u,&m);capture(&u,&m,"play-off");
  for(size_t k=1;k<3;k++){
   struct xz_ui_model saved=m;
   assert(tap(&u,&m,XZ_UI_ENABLE,caps[k],a)==1&&a[0].kind==XZ_UI_ENABLE&&a[0].index==caps[k]&&a[0].value==1);
   assert(!memcmp(&m,&saved,sizeof(m)));
  }
  /* No preview or sampler engine runs in this runtime (MENU-AUDIT defect 5): every ready bit set still gives
     one explanation per row and nothing to press. */
  assert(listed(&u,&m,XZ_UI_INFO,XZ_UI_INFO_PREVIEW)&&listed(&u,&m,XZ_UI_INFO,XZ_UI_INFO_XPAD));
  assert(!listed(&u,&m,XZ_UI_FEATURE_SETUP_OPEN,XZ_UI_PREVIEW)&&!listed(&u,&m,XZ_UI_FEATURE_LINK,XZ_UI_XPAD));
  assert(!listed(&u,&m,XZ_UI_ENABLE,XZ_UI_PREVIEW)&&!listed(&u,&m,XZ_UI_ENABLE,XZ_UI_SAMPLE));
  tap(&u,&m,XZ_UI_PANEL,XZ_UI_STEM_SECTION,a);
  {struct xz_ui_model saved=m;assert(tap(&u,&m,XZ_UI_ENABLE,XZ_UI_STEM,a)==1&&a[0].value==1&&!memcmp(&m,&saved,sizeof(m)));}
  m.enabled=0x3fff;m.jump.enabled=1;m.metrics_enabled=1;
  capture(&u,&m,"stems-on");
  /* Every link opens its page under its section, lights that section, and BACK lands on the link. */
  for(size_t k=0;k<sizeof(links)/sizeof(*links);k++){
   xz_ui_init(&u);tap(&u,&m,XZ_UI_PANEL,links[k].from,a);
   assert(tap(&u,&m,XZ_UI_FEATURE_LINK,links[k].link,a)==1&&u.page==links[k].link);
   assert(u.history_count==1&&u.history[0]==links[k].from);geometry(&u,&m);
   struct xz_ui_widget rail=find(&u,&m,XZ_UI_PANEL,links[k].from);
   assert(xz_ui_widget_look(&u,&m,&rail).selected);
   assert(xz_ui_back(&u,a)==1&&u.page==links[k].from&&!u.history_count);
   struct xz_ui_widget focused;assert(xz_ui_focused(&u,&m,&focused)&&focused.kind==XZ_UI_FEATURE_LINK&&focused.index==(int)links[k].link);
   assert(xz_ui_encoder_push(&u,&m,a)==1&&u.page==links[k].link);
   assert(xz_ui_back(&u,a)==1&&u.page==links[k].from);
  }
  /* No loaded track and no engines: each feature row turns into its one setup button, in the same place. */
  xz_ui_init(&u);m.enabled=0;m.deck[0].ready=m.deck[1].ready=0;
  tap(&u,&m,XZ_UI_PANEL,XZ_UI_PLAY,a);geometry(&u,&m);capture(&u,&m,"play-setup");
  {struct xz_ui_widget gate=find(&u,&m,XZ_UI_FEATURE_SETUP_OPEN,XZ_UI_GATE);m.deck[0].ready=XZ_UI_GATE;
   struct xz_ui_widget on=find(&u,&m,XZ_UI_ENABLE,XZ_UI_GATE);
   assert(gate.x==on.x&&gate.y==on.y&&gate.w==on.w&&gate.h==on.h);m.deck[0].ready=0;}
  /* Without the preview or sampler engine, each row says so and offers nothing to press. */
  assert(listed(&u,&m,XZ_UI_INFO,XZ_UI_INFO_PREVIEW)&&listed(&u,&m,XZ_UI_INFO,XZ_UI_INFO_XPAD));
  assert(!listed(&u,&m,XZ_UI_FEATURE_SETUP_OPEN,XZ_UI_PREVIEW)&&!listed(&u,&m,XZ_UI_FEATURE_LINK,XZ_UI_XPAD));
  tap(&u,&m,XZ_UI_PANEL,XZ_UI_STEM_SECTION,a);geometry(&u,&m);
  const struct xz_ui_widget unavailable=find(&u,&m,XZ_UI_LOCAL_STEM_SETUP_OPEN,XZ_UI_STEM);
  m.deck[1].ready=XZ_UI_STEM;
  const struct xz_ui_widget available=find(&u,&m,XZ_UI_ENABLE,XZ_UI_STEM);
  assert(available.x==unavailable.x&&available.y==unavailable.y&&available.w==unavailable.w&&available.h==unavailable.h);
  /* An explicit OFF turns on in one tap even though the host cleared every ready bit. */
  m.deck[1].ready=0;m.deck[0].usb_reason=m.deck[1].usb_reason=XZ_UI_USB_WHY_OFF;
  assert(tap(&u,&m,XZ_UI_ENABLE,XZ_UI_STEM,a)==1&&a[0].kind==XZ_UI_ENABLE&&a[0].index==XZ_UI_STEM&&a[0].value==1);
  m.deck[0].usb_reason=m.deck[1].usb_reason=XZ_UI_USB_WHY_ENGINE;
  find(&u,&m,XZ_UI_LOCAL_STEM_SETUP_OPEN,XZ_UI_STEM);
  m.deck[0].usb_reason=m.deck[1].usb_reason=XZ_UI_USB_WHY_UNKNOWN;
 }
 /* GATE / SMART setup: detect, choose a deck, enable, test; it ends back on PLAY. */
 for(size_t k=1;k<3;k++){
  xz_ui_init(&u);memset(&m,0,sizeof(m));tap(&u,&m,XZ_UI_PANEL,XZ_UI_PLAY,a);
  assert(tap(&u,&m,XZ_UI_FEATURE_SETUP_OPEN,caps[k],a)==1&&u.page==XZ_UI_FEATURE_SETUP&&u.feature_cap==caps[k]);
  assert(tap(&u,&m,XZ_UI_FEATURE_SETUP_STEP,0,a)==0&&u.feature_step==0&&u.notice[0]);
  m.deck[1].ready=(uint32_t)caps[k];
  assert(tap(&u,&m,XZ_UI_DECK,1,a)==1&&u.deck==1);
  assert(tap(&u,&m,XZ_UI_FEATURE_SETUP_STEP,0,a)==0&&u.feature_step==1);
  assert(tap(&u,&m,XZ_UI_FEATURE_SETUP_STEP,1,a)==0&&u.feature_step==2);
  assert(tap(&u,&m,XZ_UI_FEATURE_SETUP_STEP,2,a)==1&&a[0].kind==XZ_UI_ENABLE&&a[0].value==1&&a[0].deck==1&&u.feature_step==3);
  assert(tap(&u,&m,XZ_UI_FEATURE_SETUP_STEP,3,a)==0&&u.page==XZ_UI_FEATURE_SETUP);
  m.enabled=(uint32_t)caps[k];m.deck[1].track="LOADED USB TRACK";
  assert(tap(&u,&m,XZ_UI_FEATURE_SETUP_STEP,3,a)==1&&u.page==XZ_UI_PLAY);
  /* It lands on the slot it left from, which now holds the switch. */
  struct xz_ui_widget focused;assert(xz_ui_focused(&u,&m,&focused)&&focused.kind==XZ_UI_ENABLE&&focused.index==caps[k]);
 }
 /* A ready bit is not an engine: with PREVIEW reported, PLAY still explains HOT CUE PREVIEW and opens no check. */
 {xz_ui_init(&u);memset(&m,0,sizeof(m));m.deck[0].ready=XZ_UI_PREVIEW|XZ_UI_SAMPLE|XZ_UI_GROOVE;m.deck[0].track="READY ACTION TRACK";
  tap(&u,&m,XZ_UI_PANEL,XZ_UI_PLAY,a);
  assert(listed(&u,&m,XZ_UI_INFO,XZ_UI_INFO_PREVIEW)&&!listed(&u,&m,XZ_UI_FEATURE_SETUP_OPEN,XZ_UI_PREVIEW));
  for(int d=0;d<4;d++)assert(!(xz_deck_caps(&m,d)&(XZ_DECK_CAN_CUE_PREVIEW|XZ_DECK_CAN_XPAD|XZ_DECK_CAN_GROOVE)));}
 puts("PASS sections: HOME tiles and SET UP, PLAY and STEMS switches and setup gates, links with BACK history and the lit rail, encoder, bounds in every theme");
 return 0;
}
