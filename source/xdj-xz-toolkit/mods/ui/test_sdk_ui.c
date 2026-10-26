/* SPDX-License-Identifier: MIT
 * XZ SDK APPLICATIONS browser and SET UP APPS wizard, through the real MODS
 * panel (ui.c, ui_layout.c and every skin). Copied into mods/ui/test_sdk_ui.c. */
#include "ui_skin.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#ifdef NDEBUG
#error "SDK UI acceptance requires assertions; compile with -UNDEBUG"
#endif

static uint16_t frame[800*480+16];
static struct xz_ui_model scratch;
enum state { ABSENT, EMPTY, SCANNING, CATALOG, BUSY, STATES };

static void item(struct xz_ui_model *m,unsigned i,const char *id,const char *name,unsigned kind,unsigned state,const char *detail){
 struct xz_sdk_item *it=&m->sdk.snapshot.items[i];
 memset(it,0,sizeof(*it));
 snprintf(it->id,sizeof(it->id),"%s",id);snprintf(it->name,sizeof(it->name),"%s",name);
 snprintf(it->version,sizeof(it->version),"0.1.%u",i);snprintf(it->detail,sizeof(it->detail),"%s",detail?detail:"");
 it->kind=kind;it->state=state;
 if(i>=m->sdk.snapshot.count)m->sdk.snapshot.count=i+1;
}
static void setup(struct xz_ui *u,struct xz_ui_model *m,enum state state){
 memset(m,0,sizeof(*m));xz_ui_init(u);
 for(int d=0;d<4;d++){m->deck[d].groove_active=m->deck[d].sample_active=-1;m->deck[d].sample_volume=1;for(int k=0;k<3;k++)m->deck[d].levels[k]=1;}
 m->sdk.host=state==ABSENT?XZ_SDK_UI_ABSENT:XZ_SDK_UI_RUNNING;m->sdk.roots=2;
 m->sdk.snapshot.generation=7;m->sdk.snapshot.autoload_enabled=1;
 m->sdk.snapshot.scanning=state==SCANNING;
 if(state<CATALOG)return;
 snprintf(m->sdk.snapshot.status,sizeof(m->sdk.snapshot.status),"USB1: 14 PACKAGES");
 item(m,0,"com.example.clock","Beat Clock",XZ_SDK_APPLICATION,XZ_SDK_READY,NULL);
 item(m,1,"com.example.tunnel","Tunnel With An Unusually Long Display Name For Rows",XZ_SDK_APPLICATION,XZ_SDK_RUNNING,"PID 812");
 item(m,2,"com.example.crash","Crashy",XZ_SDK_APPLICATION,XZ_SDK_FAILED,"EXITED WITH CODE 3");
 item(m,3,"com.example.meter","Level Meter",XZ_SDK_EXTENSION,XZ_SDK_READY,NULL);
 item(m,4,"com.example.osc","OSC Bridge",XZ_SDK_EXTENSION,XZ_SDK_RUNNING,NULL);
 item(m,5,"com.example.club","Club Profile",XZ_SDK_PROFILE,XZ_SDK_READY,NULL);
 item(m,6,"com.example.twin","Twin",XZ_SDK_APPLICATION,XZ_SDK_BLOCKED,"SAME ID, DIFFERENT CONTENT ON USB2");
 item(m,7,XZ_SDK_UI_TEST_ID,"Test card",XZ_SDK_APPLICATION,XZ_SDK_READY,NULL);
 for(unsigned i=8;i<14;i++){char id[32];snprintf(id,sizeof(id),"com.example.extra%u",i);item(m,i,id,"Extra",XZ_SDK_EXTENSION,XZ_SDK_READY,NULL);}
 m->sdk.snapshot.items[0].autoload=1;
 m->sdk.display_busy=state==BUSY;
}
static size_t layout(struct xz_ui *u,struct xz_ui_model *m,struct xz_ui_widget w[XZ_UI_WIDGETS]){
 size_t n=xz_ui_layout(u,m,w);assert(n<=XZ_UI_WIDGETS);return n;
}
static int lookup(struct xz_ui *u,struct xz_ui_model *m,int kind,int index,struct xz_ui_widget *out){
 struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=layout(u,m,w);
 for(size_t i=0;i<n;i++)if((int)w[i].kind==kind&&w[i].index==index){if(out)*out=w[i];return 1;}
 return 0;
}
static struct xz_ui_widget find(struct xz_ui *u,struct xz_ui_model *m,int kind,int index){
 struct xz_ui_widget w;int found=lookup(u,m,kind,index,&w);assert(found);return w;
}
static struct xz_ui_action tap(struct xz_ui *u,struct xz_ui_model *m,int kind,int index){
 struct xz_ui_widget w=find(u,m,kind,index);struct xz_ui_action a[XZ_UI_ACTIONS];
 assert(xz_ui_touch(u,m,w.x+w.w/2,w.y+w.h/2,1,a)==1);
 struct xz_ui_action result=a[0];assert(result.kind==(enum xz_ui_action_kind)kind&&result.phase==XZ_UI_PRESS);
 assert(xz_ui_touch(u,m,0,0,0,a)==0&&u->held_kind==XZ_UI_NONE);
 return result;
}
static struct xz_sdk_ui_plan plan(struct xz_ui_model *m,struct xz_ui_action a){
 struct xz_sdk_ui_plan p;xz_sdk_ui_plan(&m->sdk,&a,&p);return p;
}
/* Bounds, touch size, footer clearance, no overlap, unique controls and one action per press. */
static void sane(const struct xz_ui *u,struct xz_ui_model *m){
 struct xz_ui_widget w[XZ_UI_WIDGETS];struct xz_ui copy=*u;size_t n=layout(&copy,m,w);
 for(size_t i=0;i<n;i++){
  assert(w[i].x>=0&&w[i].y>=0&&w[i].x+w[i].w<=800&&w[i].y+w[i].h<=404&&w[i].w>=56&&w[i].h>=36);
  assert(w[i].label&&w[i].label[0]);
  for(size_t k=i+1;k<n;k++){
   assert(w[i].x+w[i].w<=w[k].x||w[k].x+w[k].w<=w[i].x||w[i].y+w[i].h<=w[k].y||w[k].y+w[k].h<=w[i].y);
   assert(w[i].kind!=w[k].kind||w[i].index!=w[k].index);
  }
  struct xz_ui t=*u;struct xz_ui_action a[XZ_UI_ACTIONS];
  scratch=*m;
  /* BACK reports where it went (PANEL or CLOSE), not itself. */
  assert(xz_ui_touch(&t,m,w[i].x+w[i].w/2,w[i].y+w[i].h/2,1,a)==1&&(a[0].kind==w[i].kind||w[i].kind==XZ_UI_BACK)&&a[0].phase==XZ_UI_PRESS);
  if(xz_sdk_ui_kind(a[0].kind))assert(a[0].sdk_generation==m->sdk.snapshot.generation);
  assert(xz_ui_touch(&t,m,0,0,0,a)==0&&t.held_kind==XZ_UI_NONE&&t.capture<0&&!t.down);
  assert(!memcmp(&scratch,m,sizeof(*m))); /* touch never edits the snapshot */
 }
}
static void render(struct xz_ui *u,struct xz_ui_model *m){
 for(int i=0;i<16;i++)frame[800*480+i]=0xdead;
 scratch=*m;
 assert(xz_ui_render(u,m,frame,800*480,800));
 for(int i=0;i<16;i++)assert(frame[800*480+i]==0xdead);
 assert(!memcmp(&scratch,m,sizeof(*m)));
}
/* Every page, wizard step, choice and catalog state, in all registered themes. */
static void all_themes(void){
 static const int pages[3]={XZ_UI_EXTRAS,XZ_UI_APPS,XZ_UI_APPS_SETUP};
 struct xz_ui u;struct xz_ui_model m;
 for(int state=0;state<STATES;state++)for(int p=0;p<3;p++)for(int step=0;step<XZ_SDK_UI_STEPS;step++)
 for(int variant=0;variant<3;variant++){
  if(pages[p]!=XZ_UI_APPS_SETUP&&step)continue;
  setup(&u,&m,(enum state)state);u.page=(enum xz_ui_page)pages[p];u.sdk_step=step;
  u.sdk_choice=variant&1;u.sdk_offset=variant*XZ_SDK_UI_ROWS;
  if(variant==2&&state>=CATALOG){m.sdk.snapshot.items[7].state=XZ_SDK_RUNNING;m.sdk.foreground=1;m.sdk.snapshot.autoload_enabled=0;}
  struct xz_ui_widget base[XZ_UI_WIDGETS];size_t count=layout(&u,&m,base);
  for(int theme=0;theme<XZ_THEME_COUNT;theme++){
   struct xz_ui_widget w[XZ_UI_WIDGETS];m.theme=theme;
   size_t n=layout(&u,&m,w);assert(n==count);
   for(size_t i=0;i<n;i++)assert(w[i].kind==base[i].kind&&w[i].index==base[i].index&&!strcmp(w[i].label,base[i].label)&&
    w[i].x==base[i].x&&w[i].y==base[i].y&&w[i].w==base[i].w&&w[i].h==base[i].h);
   render(&u,&m);
  }
  m.theme=0;sane(&u,&m);
 }
}
static void extras_tile(void){
 struct xz_ui u;struct xz_ui_model m;setup(&u,&m,EMPTY);
 u.page=XZ_UI_EXTRAS;
 struct xz_ui_widget games=find(&u,&m,XZ_UI_PANEL,XZ_UI_GAMES),apps=find(&u,&m,XZ_UI_PANEL,XZ_UI_APPS);
 /* EXTRAS is a 2x2 grid of equal launch cards: GAMES, USB APPS beside it, then REPORT AN ISSUE and ABOUT. */
 assert(!strcmp(apps.label,"USB APPS")&&games.w==apps.w&&games.h==apps.h&&apps.y==games.y&&apps.x>=games.x+games.w);
 assert(lookup(&u,&m,XZ_UI_PANEL,XZ_UI_SUPPORT,NULL)&&lookup(&u,&m,XZ_UI_PANEL,XZ_UI_ABOUT,NULL));
 assert(tap(&u,&m,XZ_UI_PANEL,XZ_UI_APPS).value==XZ_UI_APPS&&u.page==XZ_UI_APPS);
 u.page=XZ_UI_EXTRAS;assert(tap(&u,&m,XZ_UI_PANEL,XZ_UI_GAMES).value==XZ_UI_GAMES&&u.page==XZ_UI_GAMES);
}
/* Empty and absent states: one primary button that opens setup and does real work. */
static void empty_setup(void){
 struct xz_ui u;struct xz_ui_model m;int kind,index;struct xz_sdk_ui_plan p;struct xz_ui_action a;
 for(int state=ABSENT;state<=SCANNING;state++){
  setup(&u,&m,(enum state)state);u.page=XZ_UI_APPS;
  assert(xz_sdk_ui_primary(&u,&m,&kind,&index)&&kind==XZ_UI_SDK_SETUP&&index==XZ_SDK_UI_DETECT);
  assert(!strcmp(find(&u,&m,XZ_UI_SDK_SETUP,XZ_SDK_UI_DETECT).label,"SET UP APPS"));
  assert(lookup(&u,&m,XZ_UI_SDK_PAGE,-1,NULL)&&lookup(&u,&m,XZ_UI_SDK_PAGE,1,NULL)&&lookup(&u,&m,XZ_UI_SDK_SCAN,0,NULL));
  {struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=layout(&u,&m,w);
   for(size_t i=0;i<n;i++)assert(w[i].kind!=XZ_UI_SDK_RUN&&w[i].kind!=XZ_UI_SDK_STOP);}
  a=tap(&u,&m,XZ_UI_SDK_SETUP,XZ_SDK_UI_DETECT);
  assert(u.page==XZ_UI_APPS_SETUP&&u.sdk_step==XZ_SDK_UI_DETECT&&a.index==XZ_SDK_UI_DETECT);
  p=plan(&m,a);
  if(state==ABSENT)assert(!p.submit&&strstr(p.notice,"NOT RUNNING"));
  else assert(p.submit&&p.command.kind==XZ_SDK_SCAN&&p.command.generation==7);
  /* Setup's first step keeps one primary: CHECK AGAIN without a manager, NEXT with one. */
  assert(xz_sdk_ui_primary(&u,&m,&kind,&index));
  if(state==ABSENT)assert(kind==XZ_UI_SDK_SCAN&&!strcmp(find(&u,&m,XZ_UI_SDK_SCAN,0).label,"CHECK AGAIN")&&
   !lookup(&u,&m,XZ_UI_SDK_GOTO,XZ_SDK_UI_FOLDERS,NULL));
  else assert(kind==XZ_UI_SDK_GOTO&&index==XZ_SDK_UI_FOLDERS&&lookup(&u,&m,XZ_UI_SDK_SCAN,0,NULL));
  assert(strstr(xz_ui_footer_text(&u,&m),m.sdk.snapshot.autoload_enabled?"AUTOLOAD IS ON":"AUTOLOAD IS OFF"));
 }
 /* Without a manager, list controls lead into setup instead of doing nothing. */
 setup(&u,&m,ABSENT);u.page=XZ_UI_APPS;
 assert(!strcmp(xz_ui_footer_text(&u,&m),"APP MANAGER NOT RUNNING / TAP SET UP APPS"));
 a=tap(&u,&m,XZ_UI_SDK_SCAN,0);assert(u.page==XZ_UI_APPS_SETUP&&u.sdk_step==XZ_SDK_UI_DETECT);
 p=plan(&m,a);assert(!p.submit&&strstr(p.notice,"RESTART THE XZ"));
 u.page=XZ_UI_APPS;a=tap(&u,&m,XZ_UI_SDK_AUTOLOAD,0);assert(u.page==XZ_UI_APPS_SETUP&&!plan(&m,a).submit);
 u.sdk_step=XZ_SDK_UI_VERIFY;assert(xz_sdk_ui_primary(&u,&m,&kind,&index)&&kind==XZ_UI_SDK_GOTO&&index==XZ_SDK_UI_DETECT);
 /* The empty-list primary is accented on screen; the accent is the theme's. */
 setup(&u,&m,EMPTY);u.page=XZ_UI_APPS;render(&u,&m);
 {struct xz_ui_widget w=find(&u,&m,XZ_UI_SDK_SETUP,XZ_SDK_UI_DETECT);
  assert(frame[(w.y+w.h-2)*800+w.x+w.w/2]==xz_theme_rgb565(xz_theme_palette(0)->accent));}
}
static void row_labels(void){
 struct xz_ui u;struct xz_ui_model m;struct xz_sdk_ui_plan p;struct xz_ui_action a;
 setup(&u,&m,CATALOG);u.page=XZ_UI_APPS;
 assert(!strcmp(find(&u,&m,XZ_UI_SDK_RUN,0).label,"RUN"));
 assert(!strcmp(find(&u,&m,XZ_UI_SDK_STOP,1).label,"STOP")&&!lookup(&u,&m,XZ_UI_SDK_RUN,1,NULL));
 assert(!strcmp(find(&u,&m,XZ_UI_SDK_RUN,2).label,"RETRY"));
 assert(!strcmp(find(&u,&m,XZ_UI_SDK_RUN,3).label,"RUN"));
 assert(!strcmp(find(&u,&m,XZ_UI_SDK_STOP,4).label,"STOP"));
 assert(!strcmp(find(&u,&m,XZ_UI_SDK_RUN,5).label,"APPLY")&&!strcmp(find(&u,&m,XZ_UI_SDK_SAVE,5).label,"SAVE"));
 assert(!lookup(&u,&m,XZ_UI_SDK_RUN,6,NULL)&&!lookup(&u,&m,XZ_UI_SDK_RUN,7,NULL)); /* second page */
 /* Running rows read as selected; others do not. */
 {struct xz_ui_widget stop=find(&u,&m,XZ_UI_SDK_STOP,1),run=find(&u,&m,XZ_UI_SDK_RUN,0);
  assert(xz_sdk_ui_selected(&u,&m,&stop)&&!xz_sdk_ui_selected(&u,&m,&run));}
 /* Rows sit in order, one per 44 px, buttons on the right edge. */
 for(int i=0;i<6;i++){struct xz_ui_widget w;
  if(!lookup(&u,&m,XZ_UI_SDK_RUN,i,&w))w=find(&u,&m,XZ_UI_SDK_STOP,i);
  assert(w.y==96+i*44&&w.x+w.w==(i==5?676:792));}
 a=tap(&u,&m,XZ_UI_SDK_RUN,2);p=plan(&m,a);
 assert(p.submit&&p.command.kind==XZ_SDK_RUN&&p.command.index==2&&p.command.generation==7&&p.command.value==0);
 assert(strstr(p.notice,"STARTING Crashy"));
 a=tap(&u,&m,XZ_UI_SDK_STOP,1);p=plan(&m,a);assert(p.submit&&p.command.kind==XZ_SDK_STOP&&p.command.index==1);
 /* APPLY is for this session; SAVE asks the manager to pass persist=1. */
 a=tap(&u,&m,XZ_UI_SDK_RUN,5);p=plan(&m,a);assert(p.submit&&p.command.kind==XZ_SDK_RUN&&p.command.value==0&&strstr(p.notice,"FOR THIS SESSION"));
 a=tap(&u,&m,XZ_UI_SDK_SAVE,5);p=plan(&m,a);assert(p.submit&&p.command.kind==XZ_SDK_RUN&&p.command.index==5&&p.command.value==1&&strstr(p.notice,"APPLYING AND SAVING"));
 /* Applications never send a save request. */
 a=(struct xz_ui_action){.kind=XZ_UI_SDK_SAVE,.phase=XZ_UI_PRESS,.index=0,.value=1,.pc_target=-1,.sdk_generation=7};
 p=plan(&m,a);assert(p.submit&&p.command.value==0);
 a=tap(&u,&m,XZ_UI_SDK_RUN,3);p=plan(&m,a);assert(p.submit&&p.command.index==3);
 /* A running app is never started twice, even from a forged action. */
 a=(struct xz_ui_action){.kind=XZ_UI_SDK_RUN,.phase=XZ_UI_PRESS,.index=1,.pc_target=-1,.sdk_generation=7};
 p=plan(&m,a);assert(!p.submit&&strstr(p.notice,"ALREADY RUNNING"));
 a.kind=XZ_UI_SDK_STOP;a.index=0;p=plan(&m,a);assert(!p.submit&&strstr(p.notice,"NOT RUNNING"));
 a.kind=XZ_UI_SDK_RUN;a.phase=XZ_UI_RELEASE;a.index=0;p=plan(&m,a);assert(!p.submit&&!p.notice[0]);
 /* A game on screen: applications wait, services and profiles still run. */
 m.sdk.display_busy=1;
 a=tap(&u,&m,XZ_UI_SDK_RUN,0);p=plan(&m,a);assert(!p.submit&&strstr(p.notice,"EXIT THE GAME"));
 a=tap(&u,&m,XZ_UI_SDK_RUN,3);assert(plan(&m,a).submit);
 m.sdk.display_busy=0;
 m.sdk.snapshot.foreground=1;
 a=tap(&u,&m,XZ_UI_SDK_RUN,0);p=plan(&m,a);assert(!p.submit&&strstr(p.notice,"STOP THE RUNNING APP"));
 a=tap(&u,&m,XZ_UI_SDK_RUN,3);assert(plan(&m,a).submit);
 m.sdk.snapshot.foreground=0;
 /* Blocked rows open setup at INSTALL with the manager's own reason. */
 u.sdk_offset=6;
 assert(!strcmp(find(&u,&m,XZ_UI_SDK_FIX,6).label,"SET UP"));
 a=tap(&u,&m,XZ_UI_SDK_FIX,6);
 assert(u.page==XZ_UI_APPS_SETUP&&u.sdk_step==XZ_SDK_UI_INSTALL&&strstr(u.notice,"SAME ID, DIFFERENT CONTENT"));
 assert(!plan(&m,a).submit&&!strcmp(xz_ui_footer_text(&u,&m),u.notice));
 a=(struct xz_ui_action){.kind=XZ_UI_SDK_RUN,.phase=XZ_UI_PRESS,.index=6,.pc_target=-1,.sdk_generation=7};
 p=plan(&m,a);assert(!p.submit&&strstr(p.notice,"SAME ID"));
 /* Blocked profiles also lead to setup. */
 m.sdk.snapshot.items[5].state=XZ_SDK_BLOCKED;u.page=XZ_UI_APPS;u.sdk_offset=0;
 assert(lookup(&u,&m,XZ_UI_SDK_FIX,5,NULL)&&!lookup(&u,&m,XZ_UI_SDK_RUN,5,NULL)&&!lookup(&u,&m,XZ_UI_SDK_SAVE,5,NULL));sane(&u,&m);
}
/* Actions carry the catalog generation; a changed list never runs the wrong row. */
static void stale(void){
 struct xz_ui u;struct xz_ui_model m;struct xz_sdk_ui_plan p;
 setup(&u,&m,CATALOG);u.page=XZ_UI_APPS;
 struct xz_ui_action a=tap(&u,&m,XZ_UI_SDK_RUN,0);assert(a.sdk_generation==7&&a.index==0);
 m.sdk.snapshot.generation=8;
 p=plan(&m,a);assert(!p.submit&&!strcmp(p.notice,"APP LIST CHANGED / CHECK THE ROW AND TAP AGAIN"));
 a=tap(&u,&m,XZ_UI_SDK_RUN,0);assert(a.sdk_generation==8&&plan(&m,a).submit&&plan(&m,a).command.generation==8);
 m.sdk.snapshot.count=0;p=plan(&m,a);assert(!p.submit&&strstr(p.notice,"APP LIST CHANGED"));
 a.index=-1;m.sdk.snapshot.count=14;assert(!plan(&m,a).submit);
 a=tap(&u,&m,XZ_UI_SDK_STOP,1);m.sdk.snapshot.generation=9;assert(!plan(&m,a).submit);
}
static void paging(void){
 struct xz_ui u;struct xz_ui_model m;setup(&u,&m,CATALOG);u.page=XZ_UI_APPS;
 static const int firsts[]={6,12,12,6,0,0};static const int directions[]={1,1,1,-1,-1,-1};
 for(int i=0;i<6;i++){
  struct xz_ui_action a=tap(&u,&m,XZ_UI_SDK_PAGE,directions[i]);assert(a.index==directions[i]);
  assert(u.sdk_offset==firsts[i]&&u.page==XZ_UI_APPS);
  int first=firsts[i],last=first+6<14?first+6:14;
  for(int k=0;k<14;k++){int shown=lookup(&u,&m,XZ_UI_SDK_RUN,k,NULL)||lookup(&u,&m,XZ_UI_SDK_STOP,k,NULL)||lookup(&u,&m,XZ_UI_SDK_FIX,k,NULL);
   assert(shown==(k>=first&&k<last));}
  sane(&u,&m);
 }
 /* A list that shrank under a stale offset shows its last page, not nothing. */
 u.sdk_offset=12;m.sdk.snapshot.count=3;
 assert(lookup(&u,&m,XZ_UI_SDK_RUN,0,NULL)&&lookup(&u,&m,XZ_UI_SDK_STOP,1,NULL));sane(&u,&m);render(&u,&m);
 u.sdk_offset=-40;assert(lookup(&u,&m,XZ_UI_SDK_RUN,0,NULL));
}
/* Autoload: the list switch, the folder choice that enables it, and live detection. */
static void autoload(void){
 struct xz_ui u;struct xz_ui_model m;struct xz_sdk_ui_plan p;struct xz_ui_action a;uint16_t on[800*24];
 setup(&u,&m,CATALOG);u.page=XZ_UI_APPS;
 struct xz_ui_widget w=find(&u,&m,XZ_UI_SDK_AUTOLOAD,0);
 assert(!strcmp(w.label,"AUTOLOAD: ON")&&xz_sdk_ui_selected(&u,&m,&w));
 a=tap(&u,&m,XZ_UI_SDK_AUTOLOAD,0);p=plan(&m,a);
 assert(a.value==0&&p.submit&&p.command.kind==XZ_SDK_AUTOLOAD&&p.command.value==0&&strstr(p.notice,"AUTOLOAD OFF"));
 m.sdk.snapshot.autoload_enabled=0;w=find(&u,&m,XZ_UI_SDK_AUTOLOAD,0);
 assert(!strcmp(w.label,"AUTOLOAD: OFF")&&!xz_sdk_ui_selected(&u,&m,&w));
 a=tap(&u,&m,XZ_UI_SDK_AUTOLOAD,0);p=plan(&m,a);assert(a.value==1&&p.submit&&p.command.value==1);
 /* Wizard FOLDERS: the choice says what it changes and turns autoload on when needed. */
 u.page=XZ_UI_APPS_SETUP;u.sdk_step=XZ_SDK_UI_FOLDERS;
 assert(!strcmp(find(&u,&m,XZ_UI_SDK_AUTOLOAD,0).label,"AUTOLOAD: OFF"));
 a=tap(&u,&m,XZ_UI_SDK_CHOICE,1);p=plan(&m,a);
 assert(u.sdk_choice==1&&a.value==1&&p.submit&&p.command.kind==XZ_SDK_AUTOLOAD&&p.command.value==1&&strstr(p.notice,"CONFIGURED FOLDER"));
 {struct xz_ui_widget c=find(&u,&m,XZ_UI_SDK_CHOICE,1);assert(xz_sdk_ui_selected(&u,&m,&c));}
 render(&u,&m);memcpy(on,frame+800*280,sizeof(on));
 m.sdk.snapshot.autoload_enabled=1;
 a=tap(&u,&m,XZ_UI_SDK_CHOICE,1);assert(a.value==0&&!plan(&m,a).submit);
 render(&u,&m);assert(memcmp(on,frame+800*280,sizeof(on))); /* AUTOLOAD IS ON/OFF follows the manager */
 assert(!strcmp(find(&u,&m,XZ_UI_SDK_AUTOLOAD,0).label,"AUTOLOAD: ON"));
 a=tap(&u,&m,XZ_UI_SDK_CHOICE,0);p=plan(&m,a);
 assert(u.sdk_choice==0&&a.value==1&&p.submit&&p.command.kind==XZ_SDK_AUTOLOAD&&p.command.value==0);
 /* The install command names the chosen folder. */
 u.sdk_step=XZ_SDK_UI_INSTALL;render(&u,&m);memcpy(on,frame+800*186,sizeof(on));
 u.sdk_choice=1;render(&u,&m);assert(memcmp(on,frame+800*186,sizeof(on)));
}
static void wizard(void){
 struct xz_ui u;struct xz_ui_model m;int kind,index;struct xz_ui_action a;struct xz_sdk_ui_plan p;
 setup(&u,&m,CATALOG);u.page=XZ_UI_APPS_SETUP;
 for(int step=0;step<XZ_SDK_UI_STEPS;step++){
  a=tap(&u,&m,XZ_UI_SDK_STEP,step);assert(u.page==XZ_UI_APPS_SETUP&&u.sdk_step==step);
  p=plan(&m,a);assert(p.submit==(step==XZ_SDK_UI_DETECT));
  {struct xz_ui_widget t=find(&u,&m,XZ_UI_SDK_STEP,step);assert(xz_sdk_ui_selected(&u,&m,&t));}
  assert(xz_sdk_ui_primary(&u,&m,&kind,&index));
  struct xz_ui_widget primary=find(&u,&m,kind,index);assert(primary.x==136&&primary.y==336&&primary.w==432);
  render(&u,&m);assert(frame[(primary.y+primary.h-2)*800+primary.x+10]==xz_theme_rgb565(xz_theme_palette(0)->accent));
  assert(lookup(&u,&m,XZ_UI_PANEL,XZ_UI_APPS,NULL));sane(&u,&m);
 }
 u.sdk_step=XZ_SDK_UI_DETECT;a=tap(&u,&m,XZ_UI_SDK_GOTO,XZ_SDK_UI_FOLDERS);assert(u.sdk_step==XZ_SDK_UI_FOLDERS&&!plan(&m,a).submit);
 a=tap(&u,&m,XZ_UI_SDK_GOTO,XZ_SDK_UI_INSTALL);assert(u.sdk_step==XZ_SDK_UI_INSTALL);
 a=tap(&u,&m,XZ_UI_SDK_GOTO,XZ_SDK_UI_VERIFY);assert(u.sdk_step==XZ_SDK_UI_VERIFY);
 /* LIVE TEST prefers the SDK example over an earlier application. */
 assert(xz_sdk_ui_primary(&u,&m,&kind,&index)&&kind==XZ_UI_SDK_RUN&&index==7);
 assert(!strcmp(find(&u,&m,XZ_UI_SDK_RUN,7).label,"RUN TEST APP"));
 a=tap(&u,&m,XZ_UI_SDK_RUN,7);p=plan(&m,a);assert(p.submit&&p.command.kind==XZ_SDK_RUN&&p.command.index==7);
 /* Status is the manager's: still READY until it reports RUNNING. */
 assert(u.page==XZ_UI_APPS_SETUP&&m.sdk.snapshot.items[7].state==XZ_SDK_READY);
 m.sdk.snapshot.items[7].state=XZ_SDK_RUNNING;
 assert(!strcmp(find(&u,&m,XZ_UI_SDK_STOP,7).label,"STOP TEST APP")&&!strcmp(find(&u,&m,XZ_UI_SDK_DONE,0).label,"IT WORKS"));
 /* The receiver's frame rate is shown only when frames arrive. */
 render(&u,&m);uint16_t waiting[800*12];memcpy(waiting,frame+800*270,sizeof(waiting));
 m.connection.ready=m.connection.connected=m.connection.stats_valid=1;m.connection.frame_hz=29.8f;
 render(&u,&m);assert(memcmp(waiting,frame+800*270,sizeof(waiting)));
 a=tap(&u,&m,XZ_UI_SDK_STOP,7);assert(plan(&m,a).command.kind==XZ_SDK_STOP);
 a=tap(&u,&m,XZ_UI_SDK_DONE,0);assert(u.page==XZ_UI_APPS&&strstr(u.notice,"LIVE TEST CONFIRMED")&&!plan(&m,a).submit);
 u.page=XZ_UI_APPS_SETUP;u.sdk_step=XZ_SDK_UI_VERIFY;
 m.sdk.snapshot.items[7].state=XZ_SDK_FAILED;assert(!strcmp(find(&u,&m,XZ_UI_SDK_RUN,7).label,"RUN AGAIN"));
 m.sdk.snapshot.items[7].state=XZ_SDK_BLOCKED;assert(!strcmp(find(&u,&m,XZ_UI_SDK_FIX,7).label,"HOW TO FIX"));
 /* No example: the first application is the test target. */
 snprintf(m.sdk.snapshot.items[7].id,sizeof(m.sdk.snapshot.items[7].id),"com.example.other");
 assert(xz_sdk_ui_primary(&u,&m,&kind,&index)&&kind==XZ_UI_SDK_RUN&&index==0);
 /* No application at all: detect again, or go back to the install step. */
 for(unsigned i=0;i<14;i++)if(m.sdk.snapshot.items[i].kind==XZ_SDK_APPLICATION)m.sdk.snapshot.items[i].kind=XZ_SDK_EXTENSION;
 assert(xz_sdk_ui_primary(&u,&m,&kind,&index)&&kind==XZ_UI_SDK_SCAN);
 a=tap(&u,&m,XZ_UI_SDK_GOTO,XZ_SDK_UI_INSTALL);assert(u.sdk_step==XZ_SDK_UI_INSTALL);
 /* INSTALL detects first, then offers the live test once something is found. */
 m.sdk.snapshot.count=0;assert(xz_sdk_ui_primary(&u,&m,&kind,&index)&&kind==XZ_UI_SDK_SCAN);
 assert(!strcmp(find(&u,&m,XZ_UI_SDK_SCAN,0).label,"DETECT INSTALLED APPS"));
 a=tap(&u,&m,XZ_UI_SDK_SCAN,0);p=plan(&m,a);assert(p.submit&&p.command.kind==XZ_SDK_SCAN);
}
/* Display takeover: captured once, forced once with one panel close, restored once. */
static void takeover(void){
 for(int previous=0;previous<2;previous++){
  struct xz_sdk_ui_session s={0};struct xz_sdk_ui_change c;int shown=previous;
  c=xz_sdk_ui_session_step(&s,0,shown);assert(!c.set_takeover&&!c.close_panel&&!s.active);
  c=xz_sdk_ui_session_step(&s,1,shown);assert(c.set_takeover&&c.takeover==1&&c.close_panel&&s.active&&s.previous==previous);
  shown=c.takeover;
  assert(xz_sdk_ui_saved_takeover(&s,shown)==previous);
  /* The user peeks at the DJ screen; the session never forces it back. */
  shown=0;c=xz_sdk_ui_session_step(&s,1,shown);assert(!c.set_takeover&&!c.close_panel&&s.previous==previous);
  shown=1;c=xz_sdk_ui_session_step(&s,1,shown);assert(!c.set_takeover&&!c.close_panel);
  /* A profile applied mid-session decides what returns afterwards. */
  assert(xz_sdk_ui_session_profile(&s,!previous)==1&&s.previous==!previous);
  assert(xz_sdk_ui_session_profile(&s,previous)==1&&s.previous==previous);
  c=xz_sdk_ui_session_step(&s,0,shown);assert(c.set_takeover&&c.takeover==previous&&!c.close_panel&&!s.active);
  shown=c.takeover;
  c=xz_sdk_ui_session_step(&s,0,!shown);assert(!c.set_takeover); /* restored exactly once */
  assert(xz_sdk_ui_saved_takeover(&s,1)==1&&xz_sdk_ui_session_profile(&s,0)==0&&!s.active);
 }
}
static void roots(void){
 const char *r[XZ_SDK_ROOTS];char storage[64];int extra;
 static const struct {const char *usb,*added;} cases[]={
  {NULL,NULL},{"",NULL},{"/media/usb1",NULL},{"/media/usb2/",NULL},{"/media/usb2//",NULL},
  {"/media/usb1/sda1","/media/usb1/sda1"},{"/media/usb2/sdb1/","/media/usb2/sdb1"},
  {"/media/usb10/sdc1","/media/usb10/sdc1"},{"/tmp/xz-dev-usb/","/tmp/xz-dev-usb"},{"/",NULL},
  {"relative/usb",NULL},{"/media/../etc",NULL},{"/tmp/a\nb",NULL},{"/tmp\\usb",NULL},
  {"/tmp/this-path-is-much-too-long-for-the-storage-buffer-given-to-roots",NULL}};
 for(size_t i=0;i<sizeof(cases)/sizeof(*cases);i++){
  unsigned n=xz_sdk_ui_roots(cases[i].usb,r,storage,sizeof(storage),&extra);
  assert(!strcmp(r[0],"/media/usb1")&&!strcmp(r[1],"/media/usb2"));
  assert(n==(cases[i].added?3u:2u)&&extra==(cases[i].added!=NULL));
  if(cases[i].added)assert(r[2]==storage&&!strcmp(r[2],cases[i].added));
  for(unsigned k=n;k<XZ_SDK_ROOTS;k++)assert(!r[k]);
 }
}
static void model_read(void){
 static struct xz_sdk_snapshot raw;static struct xz_sdk_ui_model model;
 memset(&raw,'A',sizeof(raw));raw.count=999;raw.generation=3;
 xz_sdk_ui_model_read(&model,&raw,1);
 assert(model.fresh&&model.foreground==1&&model.display_busy==1&&model.snapshot.count==XZ_SDK_PACKAGES&&model.snapshot.generation==3);
 assert(strlen(model.snapshot.status)==sizeof(raw.status)-1);
 for(unsigned i=0;i<XZ_SDK_PACKAGES;i++){const struct xz_sdk_item *it=&model.snapshot.items[i];
  assert(strlen(it->id)<sizeof(it->id)&&strlen(it->name)<sizeof(it->name)&&strlen(it->version)<sizeof(it->version)&&strlen(it->detail)<sizeof(it->detail));}
 /* A busy manager lock keeps the last snapshot and foreground on screen. */
 xz_sdk_ui_model_read(&model,NULL,0);
 assert(!model.fresh&&model.foreground&&!model.display_busy&&model.snapshot.generation==3&&model.snapshot.count==XZ_SDK_PACKAGES);
 raw.foreground=0;xz_sdk_ui_model_read(&model,&raw,0);assert(model.fresh&&!model.foreground);
 memset(&raw,'A',sizeof(raw));raw.count=999;raw.generation=3;xz_sdk_ui_model_read(&model,&raw,1);
 /* The panel renders a sanitized hostile snapshot without reading past a string. */
 struct xz_ui u;setup(&u,&scratch,CATALOG);scratch.sdk=model;scratch.sdk.host=XZ_SDK_UI_RUNNING;
 static struct xz_ui_model hostile;hostile=scratch;
 for(int page=XZ_UI_APPS;page<=XZ_UI_APPS_SETUP;page++)for(int step=0;step<XZ_SDK_UI_STEPS;step++){
  u.page=(enum xz_ui_page)page;u.sdk_step=step;render(&u,&hostile);sane(&u,&hostile);
 }
}
static void kinds(void){
 static const enum xz_ui_action_kind games[]={XZ_UI_DOOM_START,XZ_UI_MYHOUSE_START,XZ_UI_ARCADE_OPEN,XZ_UI_WAVE_RIDER_OPEN,XZ_UI_RHYTHM_LAB_OPEN,XZ_UI_GLYPH_RUN_OPEN};
 for(size_t i=0;i<sizeof(games)/sizeof(*games);i++)assert(xz_sdk_ui_game_launch(games[i]));
 assert(!xz_sdk_ui_game_launch(XZ_UI_GAME_SELECT)&&!xz_sdk_ui_game_launch(XZ_UI_TAKEOVER_TOGGLE)&&!xz_sdk_ui_game_launch(XZ_UI_SDK_RUN));
 for(int k=XZ_UI_SDK_RUN;k<=XZ_UI_SDK_DONE;k++)assert(xz_sdk_ui_kind(k));
 assert(!xz_sdk_ui_kind(XZ_UI_STEM_COLORS_RESET)&&!xz_sdk_ui_kind(XZ_UI_PANEL)&&!xz_sdk_ui_kind(XZ_UI_NONE));
 assert(xz_sdk_ui_page(XZ_UI_APPS)&&xz_sdk_ui_page(XZ_UI_APPS_SETUP)&&!xz_sdk_ui_page(XZ_UI_EXTRAS)&&!xz_sdk_ui_page(XZ_UI_GAMES));
 /* Notices override the SDK footer; the footer otherwise reports the manager. */
 struct xz_ui u;struct xz_ui_model m;setup(&u,&m,CATALOG);u.page=XZ_UI_APPS;
 assert(!strcmp(xz_ui_footer_text(&u,&m),"USB1: 14 PACKAGES"));
 m.sdk.snapshot.scanning=1;assert(!strcmp(xz_ui_footer_text(&u,&m),"SEARCHING USB FOR APPS"));
 snprintf(u.notice,sizeof(u.notice),"STARTING Beat Clock");assert(!strcmp(xz_ui_footer_text(&u,&m),"STARTING Beat Clock"));
}
static void ppm(const char *folder,const char *name){
 char path[512];snprintf(path,sizeof(path),"%s/%s.ppm",folder,name);
 FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n800 480\n255\n");
 for(size_t i=0;i<800*480;i++){unsigned v=frame[i];
  unsigned char b[3]={(unsigned char)(((v>>11)&31)*255/31),(unsigned char)(((v>>5)&63)*255/63),(unsigned char)((v&31)*255/31)};
  assert(fwrite(b,1,3,f)==3);}
 assert(fclose(f)==0);
}
/* Review images: browser, empty and absent lists, every wizard step and EXTRAS. */
static void previews(const char *folder){
 static const int themes[]={0,3,7,9,10,XZ_THEME_LCARS,XZ_THEME_MATRIX,XZ_THEME_ANALOG};
 struct xz_ui u;struct xz_ui_model m;char name[96];
 for(size_t t=0;t<sizeof(themes)/sizeof(*themes);t++){
  static const struct {enum state state;int page,step;const char *name;} shots[]={
   {CATALOG,XZ_UI_APPS,0,"browser"},{EMPTY,XZ_UI_APPS,0,"empty"},{ABSENT,XZ_UI_APPS,0,"absent"},
   {CATALOG,XZ_UI_APPS_SETUP,0,"detect"},{CATALOG,XZ_UI_APPS_SETUP,1,"folders"},{EMPTY,XZ_UI_APPS_SETUP,2,"install"},
   {CATALOG,XZ_UI_APPS_SETUP,3,"verify"},{ABSENT,XZ_UI_APPS_SETUP,0,"detect-absent"},{CATALOG,XZ_UI_EXTRAS,0,"extras"}};
  for(size_t s=0;s<sizeof(shots)/sizeof(*shots);s++){
   setup(&u,&m,shots[s].state);m.theme=themes[t];u.page=(enum xz_ui_page)shots[s].page;u.sdk_step=shots[s].step;
   if(shots[s].step==3){m.sdk.snapshot.items[7].state=XZ_SDK_RUNNING;m.sdk.foreground=1;
    m.connection.ready=m.connection.connected=m.connection.stats_valid=1;m.connection.frame_hz=29.8f;}
   if(shots[s].page==XZ_UI_EXTRAS)m.sdk.foreground=1;
   render(&u,&m);snprintf(name,sizeof(name),"theme-%d-%s",themes[t],shots[s].name);ppm(folder,name);
  }
 }
}
static void preview(const char *path){
 struct xz_ui u;struct xz_ui_model m;setup(&u,&m,EMPTY);u.page=XZ_UI_APPS;m.theme=0;
 snprintf(m.sdk.snapshot.status,sizeof(m.sdk.snapshot.status),"3 PACKAGES / READY TO RUN");
 item(&m,0,"io.vjtools.test-card","Framebuffer test card",XZ_SDK_APPLICATION,XZ_SDK_READY,"RGB565 DISPLAY AND TOUCH EXIT");
 item(&m,1,"io.vjtools.heartbeat","Background heartbeat",XZ_SDK_EXTENSION,XZ_SDK_READY,"RUNS WITHOUT TAKING THE DISPLAY");
 item(&m,2,"io.vjtools.performance-profile","Performance appearance",XZ_SDK_PROFILE,XZ_SDK_READY,"APPLY FOR SESSION OR REQUEST SAVE");
 for(unsigned i=0;i<3;i++)snprintf(m.sdk.snapshot.items[i].version,sizeof(m.sdk.snapshot.items[i].version),"0.1.0");
 render(&u,&m);FILE *out=fopen(path,"wb");assert(out);fprintf(out,"P6\n800 480\n255\n");
 for(unsigned i=0;i<800*480;i++){unsigned px=frame[i];unsigned char rgb[3]={(unsigned char)(((px>>11)&31)*255/31),(unsigned char)(((px>>5)&63)*255/63),(unsigned char)((px&31)*255/31)};assert(fwrite(rgb,1,3,out)==3);}
 assert(!fclose(out));
}
int main(int argc,char **argv){
 if(argc==3&&!strcmp(argv[1],"--preview")){preview(argv[2]);return 0;}
 if(argc>1&&!strcmp(argv[1],"--prove-assertions")){assert(!"assertions-active");return 1;}
 all_themes();extras_tile();empty_setup();row_labels();stale();paging();autoload();wizard();takeover();roots();model_read();kinds();
 if(argc>1)previews(argv[1]);
 puts("PASS SDK UI: 24 themes x pages/steps/states, bounds, overlap, one action per press, empty-setup primary, stale rows, "
      "row labels, paging, autoload detection, live test, takeover once/restore once, roots, snapshot sanitizing");
 return 0;
}
