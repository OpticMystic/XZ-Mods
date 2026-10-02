#include "ui.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef NDEBUG
#error "UI acceptance requires assertions; compile with -UNDEBUG"
#endif

static uint16_t frame[800*480+16];
static struct xz_ui_widget find(struct xz_ui *u,struct xz_ui_model *m,enum xz_ui_action_kind kind,int index){
 struct xz_ui_widget w[XZ_UI_WIDGETS];size_t i,n=xz_ui_layout(u,m,w);
 assert(n<=XZ_UI_WIDGETS);
 for(i=0;i<n;i++){assert(w[i].x>=0&&w[i].y>=0&&w[i].x+w[i].w<=800&&w[i].y+w[i].h<=480);
  if(w[i].kind==kind&&w[i].index==index)return w[i];}
 assert(!"missing widget");return w[0];
}
static struct xz_ui_action tap(struct xz_ui *u,struct xz_ui_model *m,enum xz_ui_action_kind kind,int index){
 struct xz_ui_widget w=find(u,m,kind,index);struct xz_ui_action a[XZ_UI_ACTIONS],result;
 size_t n=xz_ui_touch(u,m,w.x+w.w/2,w.y+w.h/2,1,a);assert(n==1);result=a[0];
 xz_ui_touch(u,m,0,0,0,a);return result;
}
static void ppm(const char *path){
 FILE *f=fopen(path,"wb");size_t i;assert(f);fprintf(f,"P6\n800 480\n255\n");
 for(i=0;i<800*480;i++){unsigned v=frame[i];unsigned char b[3]={(unsigned char)(((v>>11)&31)*255/31),(unsigned char)(((v>>5)&63)*255/63),(unsigned char)((v&31)*255/31)};assert(fwrite(b,1,3,f)==3);}
 assert(fclose(f)==0);
}
static void semantics(struct xz_ui *u,struct xz_ui_model *m,int theme){
 struct xz_ui_action a[XZ_UI_ACTIONS],result;struct xz_ui_widget w;int i,j;
 memset(m,0,sizeof(*m));xz_ui_init(u);m->theme=theme;m->server_auto=1;m->blink=1;
 assert(xz_ui_stem_color(0,0)==0xff3b30&&xz_ui_stem_color(0,1)==0x2997ff&&xz_ui_stem_color(0,2)==0x30d158);
 assert(xz_ui_stem_color(-1,9)==xz_ui_stem_color(0,0));
 for(i=0;i<4;i++){m->deck[i].groove_active=-1;m->deck[i].sample_active=-1;m->deck[i].sample_volume=1;for(j=0;j<3;j++)m->deck[i].levels[j]=1;}
 assert(u->page==XZ_UI_CONTROLS);
 {struct xz_ui_widget widgets[XZ_UI_WIDGETS];size_t count=xz_ui_layout(u,m,widgets);int tabs=0;
  for(size_t k=0;k<count;k++){
   if(widgets[k].kind==XZ_UI_PANEL){tabs++;assert(widgets[k].index!=XZ_UI_STEMS&&widgets[k].index!=XZ_UI_XPAD&&widgets[k].index!=XZ_UI_CONNECTION);}
   assert(widgets[k].kind!=XZ_UI_DECK&&widgets[k].kind!=XZ_UI_LEVEL&&widgets[k].kind!=XZ_UI_MUTE);
  }assert(tabs==4);}
 assert(tap(u,m,XZ_UI_ENABLE,XZ_UI_STEM).kind==XZ_UI_UNAVAILABLE);
 m->enabled=XZ_UI_STEM|XZ_UI_SAMPLE;m->deck[0].ready=0x3fff;
 assert(tap(u,m,XZ_UI_ENABLE,XZ_UI_STEM).kind==XZ_UI_ENABLE);
 assert(tap(u,m,XZ_UI_STEMS_OVERLAY,0).kind==XZ_UI_STEMS_OVERLAY);
 tap(u,m,XZ_UI_PANEL,XZ_UI_SETTINGS);tap(u,m,XZ_UI_PANEL,XZ_UI_STEMS);
 m->deck[0].groove_assigned=255;m->deck[0].groove_loaded=255;m->deck[0].sample_loaded=255;m->deck[0].loop_index=3;
 for(i=0;i<8;i++)assert(tap(u,m,XZ_UI_GROOVE_PAD,i).index==i);
 m->deck[0].groove_assigned=254;
 assert(tap(u,m,XZ_UI_HOTCUE_PAD,0).kind==XZ_UI_HOTCUE_PAD);
 m->deck[0].groove_assigned=255;m->deck[0].groove_loaded=254;
 assert(tap(u,m,XZ_UI_GROOVE_PAD,0).kind==XZ_UI_UNAVAILABLE);
 m->deck[0].groove_loaded=255;
 tap(u,m,XZ_UI_PANEL,XZ_UI_SETTINGS);assert(tap(u,m,XZ_UI_PANEL,XZ_UI_XPAD).value==XZ_UI_XPAD);
 for(i=0;i<6;i++){
  w=find(u,m,XZ_UI_STRIP,0);int x=w.x+i*(w.w/6)+w.w/12;
  assert(xz_ui_touch(u,m,x,w.y,1,a)==1&&a[0].index==i&&a[0].secondary==12);
  assert(xz_ui_touch(u,m,x,w.y+w.h-1,1,a)==1&&a[0].index==i&&a[0].secondary==-12);
  assert(xz_ui_cancel(u,a)==1&&a[0].kind==XZ_UI_STRIP&&a[0].phase==XZ_UI_RELEASE);
 }
 assert(tap(u,m,XZ_UI_HOLD,0).value==1);
 assert(tap(u,m,XZ_UI_OVERDUB,0).value==1);
 for(i=0;i<8;i++)assert(tap(u,m,XZ_UI_SAMPLE_PAD,i).index==i);
 m->deck[0].sample_loaded=1;
 assert(tap(u,m,XZ_UI_SAMPLE_PAD,7).kind==XZ_UI_UNAVAILABLE);
 tap(u,m,XZ_UI_PANEL,XZ_UI_SETTINGS);
 for(i=0;i<4;i++){int flags[4]={XZ_UI_GATE,XZ_UI_SMART,XZ_UI_PREVIEW,XZ_UI_SAMPLE};assert(tap(u,m,XZ_UI_ENABLE,flags[i]).kind==XZ_UI_ENABLE);}
 assert(tap(u,m,XZ_UI_KEY_SHIFT,-1).value==-1);
 assert(tap(u,m,XZ_UI_KEY_SHIFT,0).value==0);
 m->deck[0].ready&=~XZ_UI_KEYSYNC;assert(tap(u,m,XZ_UI_KEY_SYNC,0).kind==XZ_UI_UNAVAILABLE);
 m->deck[0].ready|=XZ_UI_KEYSYNC;
 assert(tap(u,m,XZ_UI_KEY_SYNC,0).kind==XZ_UI_KEY_SYNC);
 tap(u,m,XZ_UI_PANEL,XZ_UI_CONTROLS);
 for(i=0;i<4;i++){
  result=tap(u,m,XZ_UI_STEM_PAGE,i);
  assert(result.kind==XZ_UI_STEM_PAGE&&result.index==i&&result.value==i);
 }
 assert(m->stem_page==0);
 tap(u,m,XZ_UI_PANEL,XZ_UI_SETTINGS);assert(tap(u,m,XZ_UI_SHIFT_PAGES,0).value==1);
 m->shift_pages=1;assert(tap(u,m,XZ_UI_SHIFT_PAGES,0).value==0);
 tap(u,m,XZ_UI_PANEL,XZ_UI_CONTROLS);assert(tap(u,m,XZ_UI_PAD_FEEDBACK,0).value==1);
 m->pad_feedback=1;assert(tap(u,m,XZ_UI_PAD_FEEDBACK,0).value==0);
 tap(u,m,XZ_UI_PANEL,XZ_UI_SETTINGS);assert(tap(u,m,XZ_UI_SHIFT_KEYSYNC,0).value==1);
 m->shift_keysync=1;assert(tap(u,m,XZ_UI_SHIFT_KEYSYNC,0).value==0);
 /* Settings stay editable with no loaded track, and never mutate the snapshot. */
 m->deck[0].ready=0;tap(u,m,XZ_UI_PANEL,XZ_UI_CONTROLS);
 assert(tap(u,m,XZ_UI_STEM_PAGE,2).value==2&&m->stem_page==0);
 m->settings_status="USB IS READ ONLY / SETTINGS NOT SAVED";
 assert(xz_ui_render(u,m,frame,800*480,800));
 m->deck[0].ready=0x7ff;
 tap(u,m,XZ_UI_PANEL,XZ_UI_THEMES);
 for(i=0;i<XZ_THEME_COUNT;i++){result=tap(u,m,XZ_UI_SET_THEME,i);assert(result.kind==XZ_UI_SET_THEME&&result.value==i);}
 tap(u,m,XZ_UI_PANEL,XZ_UI_EXTRAS);
 assert(tap(u,m,XZ_UI_PANEL,XZ_UI_CONNECTION).value==XZ_UI_CONNECTION);
 assert(u->page==XZ_UI_CONNECTION);
 assert(tap(u,m,XZ_UI_CONNECTION_ENABLE,0).kind==XZ_UI_UNAVAILABLE);
 m->connection.can_enable=1;
 assert(tap(u,m,XZ_UI_CONNECTION_ENABLE,0).kind==XZ_UI_UNAVAILABLE);
 m->connection.ready=1;
 result=tap(u,m,XZ_UI_CONNECTION_ENABLE,0);
 assert(result.kind==XZ_UI_CONNECTION_ENABLE&&result.value==1);
 assert(m->connection.enabled==0);
 m->connection.enabled=1;
 assert(tap(u,m,XZ_UI_CONNECTION_ENABLE,0).value==0);
 assert(tap(u,m,XZ_UI_DISCOVERY,0).kind==XZ_UI_UNAVAILABLE);
 m->connection.can_discover=1;
 result=tap(u,m,XZ_UI_DISCOVERY,0);
 assert(result.kind==XZ_UI_DISCOVERY&&result.value==1);
 m->connection.discoverable=1;
 assert(tap(u,m,XZ_UI_DISCOVERY,0).value==0);
 result=tap(u,m,XZ_UI_TAKEOVER_TOGGLE,0);
 assert(result.kind==XZ_UI_TAKEOVER_TOGGLE);
 result=tap(u,m,XZ_UI_TAKEOVER_ASSIGN,0);
 assert(result.kind==XZ_UI_TAKEOVER_ASSIGN&&result.index==0);
 result=tap(u,m,XZ_UI_TAKEOVER_ASSIGN,1);
 assert(result.kind==XZ_UI_TAKEOVER_ASSIGN&&result.index==1);
 result=tap(u,m,XZ_UI_TAKEOVER_ASSIGN,2);
 assert(result.kind==XZ_UI_TAKEOVER_ASSIGN&&result.index==2);
 xz_ui_render_vj_button(frame,800,1);
 xz_ui_render_vj_button(frame,800,0);
 u->deck=3;
 assert(tap(u,m,XZ_UI_CONNECTION_ENABLE,0).kind==XZ_UI_CONNECTION_ENABLE);
 u->deck=0;
}
static int held(enum xz_ui_action_kind kind){
 return kind==XZ_UI_MUTE||kind==XZ_UI_SAMPLE_PAD||kind==XZ_UI_HOTCUE_PAD||kind==XZ_UI_STRIP;
}
/* Skins own geometry only. Every theme lays out theme 0's controls, touchably. */
static void parity(void){
 static const unsigned grooves[3]={0,255,0x5a};
 for(int g=0;g<3;g++)for(int live=0;live<2;live++)for(int page=0;page<XZ_UI_PAGE_COUNT;page++){
  struct xz_ui u;struct xz_ui_model m;struct xz_ui_widget base[XZ_UI_WIDGETS];
  memset(&m,0,sizeof(m));xz_ui_init(&u);u.page=(enum xz_ui_page)page;
  for(int d=0;d<4;d++){m.deck[d].groove_active=m.deck[d].sample_active=-1;m.deck[d].sample_volume=1;for(int k=0;k<3;k++)m.deck[d].levels[k]=1;}
  m.deck[0].groove_assigned=grooves[g];
  if(live){
   m.enabled=XZ_UI_STEM|XZ_UI_SAMPLE;m.deck[0].ready=m.deck[1].ready=0x3fff;
   m.deck[0].groove_loaded=m.deck[0].sample_loaded=255;
   m.connection.ready=m.connection.can_enable=m.connection.can_discover=1;
  }
  size_t count=xz_ui_layout(&u,&m,base);
  for(int theme=0;theme<XZ_THEME_COUNT;theme++){
   struct xz_ui_widget w[XZ_UI_WIDGETS];m.theme=theme;
   size_t n=xz_ui_layout(&u,&m,w);assert(n==count&&n<=XZ_UI_WIDGETS);
   for(size_t i=0;i<n;i++){
    assert(w[i].kind==base[i].kind&&w[i].index==base[i].index&&w[i].requires==base[i].requires&&!strcmp(w[i].label,base[i].label));
    if(w[i].kind==XZ_UI_CLOSE)assert(w[i].x==720&&w[i].y==0);
    assert(w[i].x==base[i].x&&w[i].y==base[i].y&&w[i].w==base[i].w&&w[i].h==base[i].h);
    assert(w[i].x>=0&&w[i].y>=0&&w[i].x+w[i].w<=800&&w[i].y+w[i].h<=480&&w[i].w>=56&&w[i].h>=36);
    if(theme<XZ_THEME_LCARS)assert(w[i].y+w[i].h<=451);
    for(size_t k=i+1;k<n;k++)
     assert(w[i].x+w[i].w<=w[k].x||w[k].x+w[k].w<=w[i].x||w[i].y+w[i].h<=w[k].y||w[k].y+w[k].h<=w[i].y);
    struct xz_ui t=u;struct xz_ui_action a[XZ_UI_ACTIONS];
    assert(xz_ui_touch(&t,&m,w[i].x+w[i].w/2,w[i].y+w[i].h/2,1,a)==1);
    enum xz_ui_action_kind expected=live||!w[i].requires?w[i].kind:XZ_UI_UNAVAILABLE;
    assert(a[0].kind==expected&&a[0].phase==XZ_UI_PRESS);
    size_t released=xz_ui_touch(&t,&m,0,0,0,a);
    if(expected!=XZ_UI_UNAVAILABLE&&held(expected))assert(released==1&&a[0].kind==expected&&a[0].phase==XZ_UI_RELEASE);
    else assert(released==0);
    assert(t.held_kind==XZ_UI_NONE&&t.capture<0&&!t.down);
   }
  }
 }
}
int main(int argc,char **argv){
 struct xz_ui u;struct xz_ui_model m;int i,j;
 if(argc>1&&strcmp(argv[1],"--prove-assertions")==0){assert(!"assertions-active");return 1;}
 static const int semantic_themes[4]={0,XZ_THEME_LCARS,XZ_THEME_MATRIX,XZ_THEME_ANALOG};
 for(i=0;i<4;i++)semantics(&u,&m,semantic_themes[i]);
 parity();
 assert(!xz_ui_render(&u,&m,frame,10,800));assert(!xz_ui_render(&u,&m,frame,800*480,799));
 for(i=0;i<16;i++)frame[800*480+i]=0xdead;
 for(i=0;i<XZ_THEME_COUNT;i++)for(j=0;j<XZ_UI_PAGE_COUNT;j++){m.theme=i;u.page=(enum xz_ui_page)j;assert(xz_ui_render(&u,&m,frame,800*480,800));}
 for(i=0;i<16;i++)assert(frame[800*480+i]==0xdead);
 /* Save status remains visible even when another notice occupies the footer. */
 {
  uint16_t saved[800*20];
  u.page=XZ_UI_CONTROLS;strcpy(u.notice,"OTHER NOTICE");m.settings_status="SETTINGS SAVED TO USB";
  assert(xz_ui_render(&u,&m,frame,800*480,800));memcpy(saved,frame+800*458,sizeof(saved));
  m.settings_status="SETTINGS NOT SAVED: CHECK USB";
  assert(xz_ui_render(&u,&m,frame,800*480,800));assert(memcmp(saved,frame+800*458,sizeof(saved)));
  u.notice[0]=0;
 }
 if(argc>1){char path[512];m.theme=4;m.deck[0].bpm=128;m.deck[0].track="DECK 1 / USB STEM CACHE";
  m.deck[0].status="HOST RENDER DEMO / DEVICE AUDIO AND DISPLAY NOT VERIFIED";m.deck[0].levels[0]=.7f;m.deck[0].levels[1]=1;m.deck[0].levels[2]=.9f;m.deck[0].groove_active=2;m.deck[0].sample_loaded=255;
  for(i=0;i<8;i++)m.deck[0].groove_stem[i]=(unsigned char)(i%3);
  memset(&m.connection,0,sizeof(m.connection));
  m.connection.status="HOST RENDER DEMO / NO RECEIVER CONNECTION REPORTED";
  for(i=0;i<XZ_UI_PAGE_COUNT;i++){u.page=(enum xz_ui_page)i;u.notice[0]=0;assert(xz_ui_render(&u,&m,frame,800*480,800));snprintf(path,sizeof(path),"%s/page-%d.ppm",argv[1],i);ppm(path);}
  u.page=XZ_UI_STEMS;
  for(i=0;i<XZ_THEME_COUNT;i++){m.theme=i;assert(xz_ui_render(&u,&m,frame,800*480,800));snprintf(path,sizeof(path),"%s/theme-%d.ppm",argv[1],i);ppm(path);}
  m.theme=0;m.enabled=0;m.deck[0].ready=0;m.deck[0].status="RUNTIME NOT CONNECTED / FEATURES REMAIN VISIBLE";
  assert(xz_ui_render(&u,&m,frame,800*480,800));snprintf(path,sizeof(path),"%s/not-ready.ppm",argv[1]);ppm(path);
 }
 puts("UI: touch semantics, readiness, all registered themes, layout parity, bounds PASS");return 0;
}
