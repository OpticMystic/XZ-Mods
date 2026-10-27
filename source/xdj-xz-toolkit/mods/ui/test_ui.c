#include "ui.h"
#include "ui_skin.h"
#include "play_key.h"
#include "wave_viewport.h"
#include "ui_network.h"
#include "ui_support.h"
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
static size_t press(struct xz_ui *u,struct xz_ui_model *m,enum xz_ui_action_kind kind,int index,struct xz_ui_action a[XZ_UI_ACTIONS]);
static void ppm(const char *path){
 FILE *f=fopen(path,"wb");size_t i;assert(f);fprintf(f,"P6\n800 480\n255\n");
 for(i=0;i<800*480;i++){unsigned v=frame[i];unsigned char b[3]={(unsigned char)(((v>>11)&31)*255/31),(unsigned char)(((v>>5)&63)*255/63),(unsigned char)((v&31)*255/31)};assert(fwrite(b,1,3,f)==3);}
 assert(fclose(f)==0);
}
static void semantics(struct xz_ui *u,struct xz_ui_model *m,int theme){
 struct xz_ui_action a[XZ_UI_ACTIONS],result;struct xz_ui_widget w;int i,j;
 memset(m,0,sizeof(*m));xz_ui_init(u);m->theme=theme;m->server_auto=1;m->blink=1;
 assert(xz_ui_stem_color(0,0)==0x2997ff&&xz_ui_stem_color(0,1)==0xff3b30&&xz_ui_stem_color(0,2)==0x30d158);
 assert(xz_ui_stem_color(-1,9)==xz_ui_stem_color(0,0));
 for(i=0;i<4;i++){m->deck[i].groove_active=-1;m->deck[i].sample_active=-1;m->deck[i].sample_volume=1;for(j=0;j<3;j++)m->deck[i].levels[j]=1;}
 assert(u->page==XZ_UI_HOME);
 /* MODS opens on HOME: the seven-section rail, six tiles, no deck controls. */
 {struct xz_ui_widget widgets[XZ_UI_WIDGETS];size_t count=xz_ui_layout(u,m,widgets);int tabs=0,tiles=0;
  for(size_t k=0;k<count;k++){
   if(widgets[k].kind==XZ_UI_PANEL){tabs++;assert(xz_ui_page_is_top(widgets[k].index));}
   if(widgets[k].kind==XZ_UI_FEATURE_LINK){tiles++;assert(xz_ui_page_is_top(widgets[k].index)&&widgets[k].index!=XZ_UI_HOME);}
   assert(widgets[k].kind!=XZ_UI_DECK&&widgets[k].kind!=XZ_UI_LEVEL&&widgets[k].kind!=XZ_UI_MUTE);
  }assert(tabs==XZ_UI_SECTIONS&&tiles==XZ_UI_SECTIONS-1);}
 /* No stem engine: the STEMS tile carries the one SET UP button, straight into USB STEMS setup. */
 result=tap(u,m,XZ_UI_HOME_SETUP,XZ_UI_STEM_SECTION);assert(result.kind==XZ_UI_PANEL&&u->page==XZ_UI_LOCAL_STEM_SETUP);
 assert(xz_ui_back(u,a)==1&&u->page==XZ_UI_HOME&&xz_ui_focused(u,m,&w)&&w.kind==XZ_UI_HOME_SETUP);
 m->enabled=XZ_UI_STEM|XZ_UI_SAMPLE;m->deck[0].ready=0x3fff;
 {struct xz_ui_widget widgets[XZ_UI_WIDGETS];size_t count=xz_ui_layout(u,m,widgets);
  for(size_t k=0;k<count;k++)assert(widgets[k].kind!=XZ_UI_HOME_SETUP);}
 /* A tile opens its section; BACK returns HOME. */
 result=tap(u,m,XZ_UI_FEATURE_LINK,XZ_UI_STEM_SECTION);assert(result.kind==XZ_UI_PANEL&&u->page==XZ_UI_STEM_SECTION);
 assert(tap(u,m,XZ_UI_ENABLE,XZ_UI_STEM).kind==XZ_UI_ENABLE);
 /* STEM BUTTONS ON SCREEN: a picture runs the STEMS button's own step until its layout is reached. */
 m->stems_overlay=XZ_STEM_LAYOUT_HIDDEN;
 {size_t n=press(u,m,XZ_UI_STEMS_OVERLAY,1+XZ_STEM_LAYOUT_BELOW,a);assert(n==1&&a[0].kind==XZ_UI_STEMS_OVERLAY);
  n=press(u,m,XZ_UI_STEMS_OVERLAY,1+XZ_STEM_LAYOUT_OUTER_HALF,a);assert(n==4);
  n=press(u,m,XZ_UI_STEMS_OVERLAY,1+XZ_STEM_LAYOUT_HIDDEN,a);assert(n==0);}
 /* No groove engine runs in this runtime: even with every ready bit set, GROOVE PADS explains itself and opens
    nothing. Its page's own pads are checked directly, for a runtime that adds the engine. */
 find(u,m,XZ_UI_INFO,XZ_UI_INFO_GROOVE);
 {struct xz_ui_widget widgets[XZ_UI_WIDGETS];size_t count=xz_ui_layout(u,m,widgets);
  for(size_t k=0;k<count;k++)assert(!(widgets[k].kind==XZ_UI_FEATURE_LINK&&widgets[k].index==XZ_UI_STEMS));}
 xz_ui_navigate(u,XZ_UI_STEMS);assert(u->page==XZ_UI_STEMS);
 m->deck[0].groove_assigned=255;m->deck[0].groove_loaded=255;m->deck[0].sample_loaded=255;m->deck[0].loop_index=3;
 for(i=0;i<8;i++)assert(tap(u,m,XZ_UI_GROOVE_PAD,i).index==i);
 m->deck[0].groove_assigned=254;
 assert(tap(u,m,XZ_UI_HOTCUE_PAD,0).kind==XZ_UI_HOTCUE_PAD);
 m->deck[0].groove_assigned=255;m->deck[0].groove_loaded=254;
 assert(tap(u,m,XZ_UI_GROOVE_PAD,0).kind==XZ_UI_UNAVAILABLE);
 m->deck[0].groove_loaded=255;
 /* The same for the X-PAD sampler: PLAY explains it; the page is checked directly. */
 tap(u,m,XZ_UI_PANEL,XZ_UI_PLAY);find(u,m,XZ_UI_INFO,XZ_UI_INFO_XPAD);find(u,m,XZ_UI_INFO,XZ_UI_INFO_PREVIEW);
 xz_ui_navigate(u,XZ_UI_XPAD);assert(u->page==XZ_UI_XPAD);
 for(i=0;i<6;i++){
  w=find(u,m,XZ_UI_STRIP,0);int x=w.x+i*(w.w/6)+w.w/12;
  assert(xz_ui_touch(u,m,x,w.y,1,a)==1&&a[0].index==i&&a[0].secondary==12);
  assert(xz_ui_touch(u,m,x,w.y+w.h-1,1,a)==1&&a[0].index==i&&a[0].secondary==-12);
  assert(xz_ui_cancel(u,a)==1&&a[0].kind==XZ_UI_STRIP&&a[0].phase==XZ_UI_RELEASE);
 }
 assert(tap(u,m,XZ_UI_HOLD,0).value==1);
 assert(tap(u,m,XZ_UI_OVERDUB,0).value==1);
 m->enabled&=~XZ_UI_SAMPLE; /* Loaded samples on a ready new page need no master gate. */
 for(i=0;i<8;i++)assert(tap(u,m,XZ_UI_SAMPLE_PAD,i).index==i);
 m->deck[0].sample_loaded=1;
 assert(tap(u,m,XZ_UI_SAMPLE_PAD,7).kind==XZ_UI_UNAVAILABLE);
 tap(u,m,XZ_UI_PANEL,XZ_UI_PLAY);
 for(i=0;i<2;i++){int flags[2]={XZ_UI_GATE,XZ_UI_SMART};assert(tap(u,m,XZ_UI_ENABLE,flags[i]).kind==XZ_UI_ENABLE);}
 /* Without an engine, hot cue preview and the sampler explain themselves; they offer no dead control. */
 {struct xz_ui_model none=*m;for(int d=0;d<4;d++)none.deck[d].ready&=~(uint32_t)(XZ_UI_PREVIEW|XZ_UI_SAMPLE);
  struct xz_ui_widget controls[XZ_UI_WIDGETS];size_t count=xz_ui_layout(u,&none,controls);int infos=0;
  for(size_t k=0;k<count;k++){
   if(controls[k].kind==XZ_UI_INFO)infos++;
   assert(!(controls[k].kind==XZ_UI_FEATURE_LINK&&controls[k].index==XZ_UI_XPAD));
   assert(!(controls[k].kind==XZ_UI_FEATURE_SETUP_OPEN&&controls[k].index==XZ_UI_PREVIEW));
  }
  assert(infos==2);
  w=find(u,&none,XZ_UI_INFO,XZ_UI_INFO_XPAD);assert(xz_ui_touch(u,&none,w.x+5,w.y+5,1,a)==0);xz_ui_touch(u,&none,0,0,0,a);
  struct xz_ui_info info;assert(xz_ui_describe(u,&none,&info)&&!strcmp(info.state,"NOT IN THIS VERSION")&&strstr(info.body,"Not in this version"));}
 /* KEYS lives under PLAY: its key buttons only there, and only PLAY links to it. */
 for(int page=0;page<XZ_UI_PAGE_COUNT;page++){
  struct xz_ui copy=*u;copy.page=(enum xz_ui_page)page;struct xz_ui_widget controls[XZ_UI_WIDGETS];
  size_t count=xz_ui_layout(&copy,m,controls);
  for(size_t k=0;k<count;k++){
   if(page!=XZ_UI_KEY_SETUP)assert(controls[k].kind!=XZ_UI_KEY_SHIFT&&controls[k].kind!=XZ_UI_KEY_SYNC);
   if(page!=XZ_UI_PLAY)assert(!((controls[k].kind==XZ_UI_PANEL||controls[k].kind==XZ_UI_FEATURE_LINK)&&controls[k].index==XZ_UI_KEY_SETUP));
  }
 }
 assert(tap(u,m,XZ_UI_FEATURE_LINK,XZ_UI_KEY_SETUP).value==XZ_UI_KEY_SETUP&&u->page==XZ_UI_KEY_SETUP);
 m->deck[0].ready|=XZ_UI_KEY|XZ_UI_KEYSYNC;
 for(i=-1;i<=1;i++){result=tap(u,m,XZ_UI_KEY_SHIFT,i);assert(result.kind==XZ_UI_KEY_SHIFT&&result.index==i&&result.deck==0);}
 assert(tap(u,m,XZ_UI_KEY_SYNC,0).kind==XZ_UI_KEY_SYNC);
 m->deck[0].ready&=~(uint32_t)XZ_UI_KEYSYNC;assert(tap(u,m,XZ_UI_KEY_SYNC,0).kind==XZ_UI_UNAVAILABLE);
 tap(u,m,XZ_UI_PANEL,XZ_UI_PLAY);
 for(int deck=0;deck<2;deck++){
  struct xz_play_key play;xz_play_key_open(&play,deck);xz_play_key_touch(&play,m,0,0,0,&result);
  m->deck[deck].ready|=XZ_UI_KEY|XZ_UI_KEYSYNC;
  struct xz_play_key_view key_view;xz_play_key_view(&play,m,NULL,&key_view);
  xz_play_key_render(&play,&key_view,frame,800);xz_play_key_published(&play,&play);
  for(int button=0;button<4;button++){
   w=xz_play_key_button(button);assert(!xz_play_key_touch(&play,m,w.x+10,w.y+10,1,&result));
   assert(xz_play_key_touch(&play,m,0,0,0,&result)&&result.deck==deck);
   assert(result.kind==(button==3?XZ_UI_KEY_SYNC:XZ_UI_KEY_SHIFT)&&result.index==(button==3?0:button-1));
  }
  m->deck[deck].ready&=~XZ_UI_KEYSYNC;w=xz_play_key_button(3);
  assert(!xz_play_key_touch(&play,m,w.x+10,w.y+10,1,&result)&&!xz_play_key_touch(&play,m,0,0,0,&result));
 }
 /* STEM PADS: pad mode, bank and the pad lights as two pictures. */
 tap(u,m,XZ_UI_PANEL,XZ_UI_STEM_SECTION);tap(u,m,XZ_UI_FEATURE_LINK,XZ_UI_STEM_PADS);assert(u->page==XZ_UI_STEM_PADS);
 for(i=0;i<4;i++){
  result=tap(u,m,XZ_UI_STEM_PAGE,i);
  assert(result.kind==XZ_UI_STEM_PAGE&&result.index==i&&result.value==i);
 }
 assert(m->stem_page==0);
 for(i=0;i<2;i++){result=tap(u,m,XZ_UI_STEM_BANK,i);assert(result.kind==XZ_UI_STEM_BANK&&result.index==i);}
 assert(tap(u,m,XZ_UI_STEM_ORDER,0).kind==XZ_UI_STEM_ORDER);
 assert(tap(u,m,XZ_UI_PAD_FEEDBACK,1).value==1&&tap(u,m,XZ_UI_PAD_FEEDBACK,2).value==0);
 m->pad_feedback=1;assert(tap(u,m,XZ_UI_PAD_FEEDBACK,1).value==1&&tap(u,m,XZ_UI_PAD_FEEDBACK,2).value==0);

 /* Settings stay editable with no loaded track, and never mutate the snapshot. */
 m->deck[0].ready=0;
 assert(tap(u,m,XZ_UI_STEM_PAGE,2).value==2&&m->stem_page==0);
 m->settings_status="USB IS READ ONLY / SETTINGS NOT SAVED";
 assert(xz_ui_render(u,m,frame,800*480,800));
 m->deck[0].ready=0x7ff;
 tap(u,m,XZ_UI_PANEL,XZ_UI_SCREEN);
 /* SCREEN: INTERFACE first; THEME lists only the selected interface's themes. */
 {int saved_interface=m->ui_interface;
  m->ui_interface=XZ_UI_INTERFACE_MODERN;m->modern_look.known=1;
  result=tap(u,m,XZ_UI_SET_INTERFACE,XZ_UI_INTERFACE_CLASSIC);assert(result.kind==XZ_UI_SET_INTERFACE&&result.value==XZ_UI_INTERFACE_CLASSIC);
  /* MODERN has one look: no THEME row; its slot leads to LAMPS AND TITLES, and WAVEFORM COLOURS has RGB. */
  {struct xz_ui_widget all[XZ_UI_WIDGETS];size_t count=xz_ui_layout(u,m,all);
   for(size_t k=0;k<count;k++)assert(all[k].kind!=XZ_UI_SET_THEME&&all[k].kind!=XZ_UI_SET_MODERN_THEME&&all[k].kind!=XZ_UI_WAVE_MODE&&
    !(all[k].kind==XZ_UI_PANEL&&all[k].index==XZ_UI_THEME_PICK));}
  for(i=0;i<XZ_UI_WAVE_COLORS;i++){result=tap(u,m,XZ_UI_SHELL_LOOK,XZ_UI_LOOK_WAVE_COLOR*8+i);assert(result.kind==XZ_UI_SHELL_LOOK&&result.index==XZ_UI_LOOK_WAVE_COLOR&&result.value==i);}
  m->modern_look.wave_color=XZ_UI_WAVE_RGB;
  {struct xz_ui_widget rgb=find(u,m,XZ_UI_SHELL_LOOK,XZ_UI_LOOK_WAVE_COLOR*8+XZ_UI_WAVE_RGB);assert(xz_ui_widget_look(u,m,&rgb).selected);}
  assert(xz_ui_render(u,m,frame,800*480,800));
  /* LAMPS AND TITLES: one switch per part (on = shown) and TRACK COLOURS' three choices; BACK returns to SCREEN. */
  assert(tap(u,m,XZ_UI_FEATURE_LINK,XZ_UI_SCREEN_LOOK).kind==XZ_UI_PANEL&&u->page==XZ_UI_SCREEN_LOOK);
  m->modern_look.hidden=1u<<XZ_UI_LOOK_MODE_LAMPS;
  result=tap(u,m,XZ_UI_SHELL_LOOK,XZ_UI_LOOK_MODE_LAMPS*8);assert(result.index==XZ_UI_LOOK_MODE_LAMPS&&result.value==0);
  result=tap(u,m,XZ_UI_SHELL_LOOK,XZ_UI_LOOK_QUANTIZE_LAMP*8);assert(result.index==XZ_UI_LOOK_QUANTIZE_LAMP&&result.value==1);
  result=tap(u,m,XZ_UI_SHELL_LOOK,XZ_UI_LOOK_WAVE_TITLES*8);assert(result.index==XZ_UI_LOOK_WAVE_TITLES&&result.value==1);
  for(i=0;i<3;i++){result=tap(u,m,XZ_UI_SHELL_LOOK,XZ_UI_LOOK_USB_COLORS*8+i);assert(result.index==XZ_UI_LOOK_USB_COLORS&&result.value==i);}
  assert(xz_ui_render(u,m,frame,800*480,800));
  assert(xz_ui_back(u,a)==1&&u->page==XZ_UI_SCREEN&&xz_ui_focused(u,m,&w)&&w.kind==XZ_UI_FEATURE_LINK&&w.index==XZ_UI_SCREEN_LOOK);
  /* 3-BAND COLOURS: nine choices per band, each naming its band and choice; STANDARD is MODERN's own colour. The
     preview strip draws the chosen colours; RESET clears all three; BACK returns to its SCREEN row. */
  assert(tap(u,m,XZ_UI_FEATURE_LINK,XZ_UI_BAND_COLORS).kind==XZ_UI_PANEL&&u->page==XZ_UI_BAND_COLORS);
  for(int band=0;band<XZ_BANDS;band++)for(i=0;i<XZ_BAND_COLOR_CHOICES;i++){
   result=tap(u,m,XZ_UI_BAND_COLOR,band*XZ_BAND_COLOR_CHOICES+i);assert(result.kind==XZ_UI_BAND_COLOR&&result.index==band&&(int)result.value==i);}
  m->band_colors[XZ_BAND_MID]=7;
  {struct xz_ui_widget pick=find(u,m,XZ_UI_BAND_COLOR,XZ_BAND_MID*XZ_BAND_COLOR_CHOICES+7);struct xz_ui_look l=xz_ui_widget_look(u,m,&pick);
   assert(l.selected&&l.color==xz_stem_palette_color(7,0));
   pick=find(u,m,XZ_UI_BAND_COLOR,XZ_BAND_LOW*XZ_BAND_COLOR_CHOICES);l=xz_ui_widget_look(u,m,&pick);
   assert(l.selected&&l.color==XZ_FLAGSHIP_WAVE_LOW&&xz_ui_model_band_color(m,XZ_BAND_LOW)==XZ_FLAGSHIP_WAVE_LOW);}
  assert(xz_ui_render(u,m,frame,800*480,800)&&frame[319*800+XZ_UI_CX]==xz_theme_rgb565(xz_stem_palette_color(7,0)));
  assert(tap(u,m,XZ_UI_BAND_COLORS_RESET,0).kind==XZ_UI_BAND_COLORS_RESET);
  m->band_colors[XZ_BAND_MID]=0;
  assert(xz_ui_back(u,a)==1&&u->page==XZ_UI_SCREEN&&xz_ui_focused(u,m,&w)&&w.kind==XZ_UI_FEATURE_LINK&&w.index==XZ_UI_BAND_COLORS);
  /* Without the shell the MODERN rows stay on screen but say NOT READY instead of acting. */
  m->modern_look.known=0;
  {struct xz_ui_widget rgb=find(u,m,XZ_UI_SHELL_LOOK,XZ_UI_LOOK_WAVE_COLOR*8+XZ_UI_WAVE_RGB);assert(!xz_ui_widget_ready(u,m,&rgb));}
  m->modern_look.known=1;
  m->ui_interface=XZ_UI_INTERFACE_CLASSIC;
  result=tap(u,m,XZ_UI_SET_INTERFACE,XZ_UI_INTERFACE_MODERN);assert(result.value==XZ_UI_INTERFACE_MODERN);
  {struct xz_ui_widget all[XZ_UI_WIDGETS];size_t count=xz_ui_layout(u,m,all);
   for(size_t k=0;k<count;k++)assert(all[k].kind!=XZ_UI_SET_MODERN_THEME);}
  /* DECK VIEW needs the four-deck connector; with it, three pictures and the DECKS button switch. */
  {struct xz_ui_widget all[XZ_UI_WIDGETS];size_t count=xz_ui_layout(u,m,all);
   for(size_t k=0;k<count;k++)assert(all[k].kind!=XZ_UI_DECK_VIEW&&all[k].kind!=XZ_UI_DECK_VIEW_BUTTON);}
  m->route.state=XZ_UI_ROUTE_NOT_DETECTED;
  for(i=0;i<3;i++){result=tap(u,m,XZ_UI_DECK_VIEW,i);assert(result.kind==XZ_UI_DECK_VIEW&&result.value==i);}
  /* The DECKS button is always on the play screen: no setting hides it. */
  {struct xz_ui_widget all[XZ_UI_WIDGETS];size_t count=xz_ui_layout(u,m,all);for(size_t k=0;k<count;k++)assert(all[k].kind!=XZ_UI_DECK_VIEW_BUTTON);}
  for(i=0;i<3;i++){assert(tap(u,m,XZ_UI_WAVE_MODE,i).value==i);assert(tap(u,m,XZ_UI_KEY_FORMAT,i).kind==XZ_UI_KEY_FORMAT);}
  assert(tap(u,m,XZ_UI_KEY_FEEDBACK,0).kind==XZ_UI_KEY_FEEDBACK&&tap(u,m,XZ_UI_BROWSE_TRAFFIC,0).kind==XZ_UI_BROWSE_TRAFFIC);
  assert(tap(u,m,XZ_UI_BROWSE_KEY_PREFIX,0).kind==XZ_UI_BROWSE_KEY_PREFIX);
  m->route.state=XZ_UI_ROUTE_NO_ENGINE;
  assert(tap(u,m,XZ_UI_PANEL,XZ_UI_THEME_PICK).value==XZ_UI_THEME_PICK&&u->page==XZ_UI_THEME_PICK);
  {struct xz_ui_widget all[XZ_UI_WIDGETS];size_t count=xz_ui_layout(u,m,all);int tiles=0;
   for(size_t k=0;k<count;k++)if(all[k].kind==XZ_UI_SET_THEME){tiles++;assert(all[k].index!=XZ_THEME_FLAGSHIP);}
   assert(tiles==XZ_THEME_FLAGSHIP);}
  for(i=0;i<XZ_THEME_FLAGSHIP;i++){result=tap(u,m,XZ_UI_SET_THEME,i);assert(result.kind==XZ_UI_SET_THEME&&result.value==i);}
  assert(xz_ui_back(u,a)==1&&a[0].kind==XZ_UI_PANEL&&u->page==XZ_UI_SCREEN);
  m->ui_interface=saved_interface;}
 tap(u,m,XZ_UI_PANEL,XZ_UI_CONNECT);
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
 result=tap(u,m,XZ_UI_TAKEOVER_ASSIGN,2);
 assert(result.kind==XZ_UI_TAKEOVER_ASSIGN&&result.index==2);
 xz_ui_render_vj_button(frame,800,1);
 xz_ui_render_vj_button(frame,800,0);
 u->deck=3;
 assert(tap(u,m,XZ_UI_CONNECTION_ENABLE,0).kind==XZ_UI_CONNECTION_ENABLE);
 u->deck=0;
}
/* RAM TRACKS: entry from PLAY, one next step per state, release only while the USB source is present. */
static int has(struct xz_ui *u,struct xz_ui_model *m,enum xz_ui_action_kind kind){
 struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(u,m,w);
 for(size_t i=0;i<n;i++)if(w[i].kind==kind)return 1;
 return 0;
}
static void ram(void){
 struct xz_ui u;struct xz_ui_model m;struct xz_ui_action a;
 static const enum xz_ui_action_kind next[6]={XZ_UI_RAM_REQUEST,XZ_UI_RAM_REQUEST,XZ_UI_RAM_CANCEL,XZ_UI_RAM_RELEASE,XZ_UI_RAM_REQUEST,XZ_UI_NONE};
 memset(&m,0,sizeof(m));xz_ui_init(&u);
 tap(&u,&m,XZ_UI_PANEL,XZ_UI_PLAY);
 a=tap(&u,&m,XZ_UI_FEATURE_LINK,XZ_UI_RAM_SETUP);
 assert(a.kind==XZ_UI_PANEL&&a.value==XZ_UI_RAM_SETUP&&u.page==XZ_UI_RAM_SETUP);
 /* Deck choice is local UI state and reports the deck it left. */
 a=tap(&u,&m,XZ_UI_RAM_DECK,1);assert(a.kind==XZ_UI_RAM_DECK&&a.deck==0&&a.index==1&&u.deck==1);
 for(int state=0;state<6;state++){
  m.deck[1].ram.state=(enum xz_ui_ram_state)state;
  for(int k=XZ_UI_RAM_REQUEST;k<=XZ_UI_RAM_RELEASE;k++)
   assert(has(&u,&m,(enum xz_ui_action_kind)k)==(k==(int)next[state]));
  if(next[state]!=XZ_UI_NONE){a=tap(&u,&m,next[state],0);assert(a.kind==next[state]&&a.deck==1&&a.phase==XZ_UI_PRESS);}
  m.deck[1].ram.copied=state*1048576u;m.deck[1].ram.bytes=5*1048576u;
  for(int theme=0;theme<XZ_THEME_COUNT;theme++){m.theme=theme;assert(xz_ui_render(&u,&m,frame,800*480,800));}
  m.theme=0;
 }
 /* Statuses never claim that removing the USB is safe. */
 for(int state=0;state<6;state++)assert(!strstr(xz_ui_ram_state_text((enum xz_ui_ram_state)state),"SAFE"));
 assert(!strcmp(xz_ui_ram_state_text(XZ_UI_RAM_READY),"ORIGINAL AUDIO IN RAM"));
 /* A detached copy offers no release; the other deck keeps its own state. */
 m.deck[1].ram.state=XZ_UI_RAM_DETACHED;m.deck[0].ram.state=XZ_UI_RAM_READY;
 assert(!has(&u,&m,XZ_UI_RAM_RELEASE));
 a=tap(&u,&m,XZ_UI_RAM_DECK,0);assert(u.deck==0&&has(&u,&m,XZ_UI_RAM_RELEASE));
 /* Copied bytes beyond the total and long backend messages stay inside the frame. */
 m.deck[0].ram.copied=UINT32_MAX;m.deck[0].ram.bytes=7;memset(m.deck[0].ram.message,'W',sizeof(m.deck[0].ram.message)-1);
 assert(xz_ui_render(&u,&m,frame,800*480,800));
 u.deck=3;assert(!has(&u,&m,XZ_UI_RAM_REQUEST)&&!has(&u,&m,XZ_UI_RAM_CANCEL)&&xz_ui_render(&u,&m,frame,800*480,800));
}
static int held(enum xz_ui_action_kind kind){
 return kind==XZ_UI_MUTE||kind==XZ_UI_SAMPLE_PAD||kind==XZ_UI_HOTCUE_PAD||kind==XZ_UI_STRIP;
}
/* Skins own geometry only. Every theme lays out theme 0's controls, touchably. */
static void parity(void){
 static const unsigned grooves[3]={0,255,0x5a};
 /* With and without the DJ Profile chip in the header. */
 static struct xz_ui_dj dj={.current=-1,.pending=-1};
 /* Both interfaces: MODERN adds UTILITY to the rail and its own look rows, which act only with the shell (live). */
 for(int iface=0;iface<2;iface++)for(int chip=0;chip<2;chip++)for(int g=0;g<3;g++)for(int live=0;live<2;live++)for(int page=0;page<XZ_UI_PAGE_COUNT;page++){
  struct xz_ui u;struct xz_ui_model m;struct xz_ui_widget base[XZ_UI_WIDGETS];
  memset(&m,0,sizeof(m));xz_ui_init(&u);u.page=(enum xz_ui_page)page;m.dj=chip?&dj:NULL;
  m.ui_interface=iface?XZ_UI_INTERFACE_MODERN:XZ_UI_INTERFACE_CLASSIC;m.modern_look.known=iface&&live;
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
    /* Every control is a full touch target, the beat jump size chips included. */
    assert(w[i].x>=0&&w[i].y>=0&&w[i].x+w[i].w<=800&&w[i].y+w[i].h<=480&&w[i].w>=56&&w[i].h>=36);
    /* Controls stay out of the info dock (y 412..480) in every theme. */
    assert(w[i].y+w[i].h<=404);
    for(size_t k=i+1;k<n;k++)
     assert(w[i].x+w[i].w<=w[k].x||w[k].x+w[k].w<=w[i].x||w[i].y+w[i].h<=w[k].y||w[k].y+w[k].h<=w[i].y);
    struct xz_ui t=u;struct xz_ui_action a[XZ_UI_ACTIONS];
    size_t pressed=xz_ui_touch(&t,&m,w[i].x+w[i].w/2,w[i].y+w[i].h/2,1,a);
    if(w[i].kind==XZ_UI_FEATURE_SETUP_STEP){assert(pressed==0);xz_ui_touch(&t,&m,0,0,0,a);continue;}
    /* Explanations act on nothing; a layout picture runs as many STEMS steps as it takes. */
    if(w[i].kind==XZ_UI_INFO){assert(pressed==0&&t.focus_kind==XZ_UI_INFO);xz_ui_touch(&t,&m,0,0,0,a);continue;}
    /* The network wizard runs its own steps (ui_network.c): a press emits its step's request or nothing. */
    if(xz_ui_network_kind(w[i].kind)){assert(pressed<=1&&(!pressed||xz_ui_network_kind(a[0].kind)));xz_ui_touch(&t,&m,0,0,0,a);continue;}
    if(xz_ui_support_kind(w[i].kind)){assert(pressed<=1);xz_ui_touch(&t,&m,0,0,0,a);continue;}
    /* ABOUT shows the touched link's QR on the page itself. */
    if(w[i].kind==XZ_UI_ABOUT_LINK){assert(pressed==0&&t.about_link==w[i].index);xz_ui_touch(&t,&m,0,0,0,a);continue;}
    if(w[i].kind==XZ_UI_STEMS_OVERLAY&&w[i].index){assert(pressed<XZ_UI_ACTIONS+1);for(size_t k=0;k<pressed;k++)assert(a[k].kind==XZ_UI_STEMS_OVERLAY);xz_ui_touch(&t,&m,0,0,0,a);continue;}
    if(pressed!=1)fprintf(stderr,"page %d widget kind %d index %d: %zu actions\n",page,w[i].kind,w[i].index,pressed);
    assert(pressed==1);
    enum xz_ui_action_kind expected=live||!w[i].requires?w[i].kind:XZ_UI_UNAVAILABLE;
    /* Profile controls depend on the USBs, not the decks. */
    if(xz_ui_profile_owns(w[i].kind))expected=xz_ui_widget_ready(&t,&m,&w[i])?w[i].kind:XZ_UI_UNAVAILABLE;
    /* BACK reports where it went: the parent page, or CLOSE from a root page. */
    if(w[i].kind==XZ_UI_FEATURE_LINK||w[i].kind==XZ_UI_FEATURE_SETUP_OPEN||w[i].kind==XZ_UI_HOME_SETUP)expected=XZ_UI_PANEL;
    /* STOCK page 1 is the player's own 1 2 4 8: its pads and sizes say so instead of editing. */
    if((w[i].kind==XZ_UI_JUMP_PAIR||w[i].kind==XZ_UI_JUMP_SIZE)&&!m.jump.enabled&&!t.jump_edit_page)expected=XZ_UI_UNAVAILABLE;
    if(w[i].kind==XZ_UI_BACK)expected=xz_ui_page_is_top(page)?XZ_UI_CLOSE:XZ_UI_PANEL;
    if((w[i].kind==XZ_UI_SHELL_LOOK||w[i].kind==XZ_UI_OPEN_UTILITY)&&!m.modern_look.known)expected=XZ_UI_UNAVAILABLE;
    if(a[0].kind!=expected)fprintf(stderr,"page %d widget kind %d index %d: got %d, expected %d\n",page,w[i].kind,w[i].index,a[0].kind,expected);
    assert(a[0].kind==expected&&a[0].phase==XZ_UI_PRESS);
    size_t released=xz_ui_touch(&t,&m,0,0,0,a);
    if(expected!=XZ_UI_UNAVAILABLE&&held(expected))assert(released==1&&a[0].kind==expected&&a[0].phase==XZ_UI_RELEASE);
    else assert(released==0);
    assert(t.held_kind==XZ_UI_NONE&&t.capture<0&&!t.down);
   }
  }
 }
}
/* STEMS IN DJ SOFTWARE setup page: reachable from STEMS, one primary action per step,
   every button emits its own action and nothing advances without the runtime. */
static void pc_setup(void){
 struct xz_ui u;struct xz_ui_model m,snapshot;struct xz_ui_action result;
 memset(&m,0,sizeof(m));xz_ui_init(&u);
 for(int d=0;d<4;d++){m.deck[d].groove_active=m.deck[d].sample_active=-1;for(int k=0;k<3;k++)m.deck[d].levels[k]=1;}
 tap(&u,&m,XZ_UI_PANEL,XZ_UI_STEM_SECTION);
 result=tap(&u,&m,XZ_UI_FEATURE_LINK,XZ_UI_PC_STEMS);
 assert(result.kind==XZ_UI_PANEL&&u.page==XZ_UI_PC_STEMS);
 {struct xz_ui_action back[XZ_UI_ACTIONS];assert(xz_ui_back(&u,back)==1&&u.page==XZ_UI_STEM_SECTION);}
 tap(&u,&m,XZ_UI_FEATURE_LINK,XZ_UI_PC_STEMS);assert(u.page==XZ_UI_PC_STEMS);
 /* Each step offers exactly one primary action (confirm adds its "no" answer). */
 static const enum xz_ui_action_kind primary[5]={XZ_UI_PC_CHECK,XZ_UI_PC_CHECK,XZ_UI_PC_TEST,XZ_UI_PC_CONFIRM,XZ_UI_PC_CHECK};
 static const int primary_index[5]={XZ_UI_PC_STEP_DETECT,XZ_UI_PC_STEP_MAPPING,XZ_UI_PC_DRUMS,1,XZ_UI_PC_STEP_DONE};
 for(int deck=0;deck<2;deck++)for(int step=0;step<5;step++){
  u.deck=deck;u.notice[0]=0;
  m.deck[deck].pc_stems=(struct xz_ui_pc_stems){.source=XZ_UI_SOURCE_PC,.connected=1,.verified=step==XZ_UI_PC_STEP_DONE,.caps=step==XZ_UI_PC_STEP_DONE?XZ_UI_PC_CAP_TOGGLE:0u,.known=0,.active=0,.pending=0,.reason=NULL,.setup_step=step,.available=15,.target=0,.epoch=1};
  struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(&u,&m,w);int actions=0;
  for(size_t i=0;i<n;i++)if(w[i].kind>=XZ_UI_PC_TOGGLE&&w[i].kind<=XZ_UI_PC_BYPASS){actions++;assert(w[i].deck==deck);}
  assert(actions==(step==XZ_UI_PC_STEP_CONFIRM?2:1));
  snapshot=m;
  result=tap(&u,&m,primary[step],primary_index[step]);
  assert(result.kind==primary[step]&&result.deck==deck&&result.index==primary_index[step]&&result.phase==XZ_UI_PRESS);
  if(step==XZ_UI_PC_STEP_CONFIRM){
   assert(result.value==1);
   result=tap(&u,&m,XZ_UI_PC_CONFIRM,0);assert(result.kind==XZ_UI_PC_CONFIRM&&result.value==0&&result.deck==deck);
  }
  /* No fake progress: the page and model stay put until the runtime answers. */
  assert(u.page==XZ_UI_PC_STEMS&&!memcmp(&snapshot,&m,sizeof(m)));
  for(int theme=0;theme<XZ_THEME_COUNT;theme++){m.theme=theme;assert(xz_ui_render(&u,&m,frame,800*480,800));}
  m.theme=0;
 }
 /* Live test and confirm need a PC deck with a link; the reason is specific. */
 u.deck=0;m.deck[0].pc_stems=(struct xz_ui_pc_stems){.source=XZ_UI_SOURCE_UNKNOWN,.connected=0,.verified=0,.caps=0,.known=0,.active=0,.pending=0,.reason=NULL,.setup_step=XZ_UI_PC_STEP_TEST,.available=15,.target=0,.epoch=1};
 result=tap(&u,&m,XZ_UI_PC_TEST,XZ_UI_PC_DRUMS);assert(result.kind==XZ_UI_UNAVAILABLE&&strstr(u.notice,"DETECT PC"));
 m.deck[0].pc_stems.source=XZ_UI_SOURCE_PC;
 result=tap(&u,&m,XZ_UI_PC_TEST,XZ_UI_PC_DRUMS);assert(result.kind==XZ_UI_UNAVAILABLE&&!strcmp(u.notice,xz_ui_pc_reason_text(XZ_UI_PC_WHY_LINK)));
 m.deck[0].pc_stems.reason="USB MIDI PORT CLOSED";
 result=tap(&u,&m,XZ_UI_PC_TEST,XZ_UI_PC_DRUMS);assert(result.kind==XZ_UI_UNAVAILABLE&&!strcmp(u.notice,"USB MIDI PORT CLOSED"));
 m.deck[0].pc_stems.reason=NULL;m.deck[0].pc_stems.connected=1;m.deck[0].pc_stems.pending=1u<<XZ_UI_PC_DRUMS;
 result=tap(&u,&m,XZ_UI_PC_TEST,XZ_UI_PC_DRUMS);assert(result.kind==XZ_UI_UNAVAILABLE&&strstr(u.notice,"DRUMS SENT"));
 m.deck[0].pc_stems.pending=0;
 assert(tap(&u,&m,XZ_UI_PC_TEST,XZ_UI_PC_DRUMS).kind==XZ_UI_PC_TEST);
 m.deck[0].pc_stems.setup_step=XZ_UI_PC_STEP_CONFIRM;m.deck[0].pc_stems.connected=0;
 assert(tap(&u,&m,XZ_UI_PC_CONFIRM,1).kind==XZ_UI_UNAVAILABLE);
 /* Deck choice on the page; source tags name the store each deck drives. */
 m.deck[1].pc_stems=(struct xz_ui_pc_stems){0};m.deck[1].pc_stems.source=XZ_UI_SOURCE_LOCAL;
 {struct xz_ui_widget w=find(&u,&m,XZ_UI_DECK,1);assert(!strcmp(w.label,"STANDALONE STEMS"));
  assert(!strcmp(find(&u,&m,XZ_UI_DECK,0).label,"PC STEMS"));}
 result=tap(&u,&m,XZ_UI_DECK,1);assert(result.kind==XZ_UI_DECK&&u.deck==1);
 assert(find(&u,&m,XZ_UI_PC_CHECK,XZ_UI_PC_STEP_DETECT).deck==1);
 /* External decks 3/4 have no PC stems; the page shows deck 1 instead of failing. */
 u.deck=3;assert(xz_ui_render(&u,&m,frame,800*480,800));assert(find(&u,&m,XZ_UI_PC_CONFIRM,1).deck==0);
 /* Out-of-range steps render as DETECT rather than reading past the table. */
 u.deck=0;m.deck[0].pc_stems.setup_step=99;assert(xz_ui_render(&u,&m,frame,800*480,800));
 m.deck[0].pc_stems.setup_step=-3;assert(xz_ui_render(&u,&m,frame,800*480,800));
 /* The footer states where the deck stands without "tap to set up" on the setup page. */
 {uint16_t first[800*30];m.deck[0].pc_stems=(struct xz_ui_pc_stems){0};
  m.deck[0].pc_stems.source=XZ_UI_SOURCE_PC;m.deck[0].pc_stems.connected=1;
  assert(xz_ui_render(&u,&m,frame,800*480,800));memcpy(first,frame+800*416,sizeof(first));
  m.deck[0].pc_stems.connected=0;assert(xz_ui_render(&u,&m,frame,800*480,800));assert(memcmp(first,frame+800*416,sizeof(first)));}
 /* Local stem controls never appear on this page. */
 {struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(&u,&m,w);
  for(size_t i=0;i<n;i++)assert(w[i].kind!=XZ_UI_MUTE&&w[i].kind!=XZ_UI_LEVEL&&w[i].kind!=XZ_UI_BYPASS&&w[i].kind!=XZ_UI_PC_TOGGLE);}
}
/* Hardware navigation: every control on every page is reachable with the encoder,
   push does what a tap does, BACK leaves value edits first, and the info box
   always has something to say about the focused control. */
static void encoder(void){
 struct xz_ui u;struct xz_ui_model m;struct xz_ui_action a[XZ_UI_ACTIONS];struct xz_ui_widget f,w[XZ_UI_WIDGETS];struct xz_ui_info info;
 memset(&m,0,sizeof(m));xz_ui_init(&u);
 for(int d=0;d<4;d++){m.deck[d].groove_active=m.deck[d].sample_active=-1;m.deck[d].sample_volume=.5f;for(int k=0;k<3;k++)m.deck[d].levels[k]=1;}
 m.enabled=XZ_UI_STEM;m.deck[0].ready=m.deck[1].ready=0x3fff;m.rekordbox_assign[0]=XZ_UI_RB_INTERFACE;m.rekordbox_assign[1]=XZ_UI_RB_STOCK;
 /* HOME starts on its first tile. */
 assert(xz_ui_focused(&u,&m,&f)&&f.kind==XZ_UI_FEATURE_LINK&&f.index==XZ_UI_PLAY);
 for(int page=0;page<XZ_UI_PAGE_COUNT;page++){
  u.page=(enum xz_ui_page)page;u.focus_kind=XZ_UI_NONE;u.adjusting=0;
  size_t n=xz_ui_layout(&u,&m,w),focusable=0;
  for(size_t i=0;i<n;i++)focusable+=w[i].kind!=XZ_UI_BACK&&w[i].kind!=XZ_UI_CLOSE;
  /* One full turn visits each focusable control once and returns to the start. */
  struct xz_ui_widget start;assert(xz_ui_focused(&u,&m,&start));
  for(size_t k=0;k<focusable;k++){
   assert(xz_ui_focused(&u,&m,&f)&&f.kind!=XZ_UI_BACK&&f.kind!=XZ_UI_CLOSE);
   assert(xz_ui_describe(&u,&m,&info)&&info.title&&info.title[0]&&info.body&&info.body[0]);
   assert(xz_ui_encoder_turn(&u,&m,1,a)==0);
  }
  assert(xz_ui_focused(&u,&m,&f)&&f.kind==start.kind&&f.index==start.index&&f.deck==start.deck);
  assert(xz_ui_encoder_turn(&u,&m,-1,a)==0&&xz_ui_encoder_turn(&u,&m,1,a)==0);
  assert(xz_ui_focused(&u,&m,&f)&&f.kind==start.kind&&f.index==start.index);
 }
 /* Push equals tap: a choice reports its slot and value. */
 xz_ui_init(&u);u.page=XZ_UI_BUTTONS;u.focus_kind=XZ_UI_REKORDBOX_ASSIGN;u.focus_index=XZ_UI_RB_CHOICES+XZ_UI_RB_STEMS;u.focus_deck=-1;
 assert(xz_ui_encoder_push(&u,&m,a)==1&&a[0].kind==XZ_UI_REKORDBOX_ASSIGN&&a[0].index==1&&a[0].value==XZ_UI_RB_STEMS&&!u.down);
 u.focus_index=XZ_UI_RB_NOTHING;assert(xz_ui_encoder_push(&u,&m,a)==1&&a[0].index==0&&a[0].value==XZ_UI_RB_NOTHING);
 xz_ui_init(&u);u.page=XZ_UI_PLAY;u.focus_deck=-1;
 /* A link opens its page; BACK returns to the link so push re-enters it. */
 u.focus_kind=XZ_UI_FEATURE_LINK;u.focus_index=XZ_UI_RAM_SETUP;
 assert(xz_ui_encoder_push(&u,&m,a)==1&&a[0].kind==XZ_UI_PANEL&&u.page==XZ_UI_RAM_SETUP);
 assert(xz_ui_focused(&u,&m,&f)&&f.y>=40&&f.x>=124);
 assert(xz_ui_back(&u,a)==1&&a[0].kind==XZ_UI_PANEL&&u.page==XZ_UI_PLAY);
 assert(xz_ui_focused(&u,&m,&f)&&f.kind==XZ_UI_FEATURE_LINK&&f.index==XZ_UI_RAM_SETUP);
 /* Rail tabs open their section at its first control. */
 u.focus_kind=XZ_UI_PANEL;u.focus_index=XZ_UI_SCREEN;
 assert(xz_ui_encoder_push(&u,&m,a)==1&&u.page==XZ_UI_SCREEN);
 assert(xz_ui_focused(&u,&m,&f)&&f.x>=124);
 /* Beat jump pairs: push selects and starts editing, turning sends size steps, BACK stops editing. */
 u.page=XZ_UI_BEAT_JUMP;m.jump.enabled=1;
 u.focus_kind=XZ_UI_JUMP_PAIR;u.focus_index=4;
 assert(xz_ui_encoder_push(&u,&m,a)==1&&a[0].kind==XZ_UI_JUMP_PAIR&&u.jump_edit_pair==2&&u.adjusting);
 assert(xz_ui_encoder_turn(&u,&m,3,a)==1&&a[0].kind==XZ_UI_JUMP_STEP&&a[0].index==3);
 assert(xz_ui_encoder_turn(&u,&m,-1,a)==1&&a[0].kind==XZ_UI_JUMP_STEP&&a[0].index==-1);
 assert(xz_ui_describe(&u,&m,&info)&&!strcmp(info.state,"ADJUSTING"));
 assert(xz_ui_back(&u,a)==0&&!u.adjusting&&u.page==XZ_UI_BEAT_JUMP);
 assert(xz_ui_encoder_turn(&u,&m,1,a)==0&&xz_ui_focused(&u,&m,&f)&&f.kind==XZ_UI_JUMP_PAIR&&f.index==6);
 /* Page 1 on STOCK plays the player's own sizes: push on a greyed pair says so and does not start editing. */
 m.jump.enabled=0;u.jump_edit_page=0;u.focus_kind=XZ_UI_JUMP_PAIR;u.focus_index=0;u.focus_deck=-1;
 assert(xz_ui_encoder_push(&u,&m,a)>=1&&a[0].kind==XZ_UI_UNAVAILABLE&&!u.adjusting);
 assert(xz_ui_encoder_turn(&u,&m,1,a)==0);
 m.jump.enabled=1;
 /* The X-PAD volume slider is edited by turning, never jumped to the middle. */
 u.page=XZ_UI_XPAD;m.enabled|=XZ_UI_SAMPLE;u.focus_kind=XZ_UI_VOLUME;u.focus_index=0;u.focus_deck=-1;
 assert(xz_ui_encoder_push(&u,&m,a)==0&&u.adjusting);
 assert(xz_ui_encoder_turn(&u,&m,1,a)==1&&a[0].kind==XZ_UI_VOLUME&&a[0].value>.5f&&a[0].value<.6f);
 assert(xz_ui_encoder_push(&u,&m,a)==0&&!u.adjusting);
 /* A touch moves focus to what was touched. */
 u.page=XZ_UI_SCREEN;{struct xz_ui_widget t=find(&u,&m,XZ_UI_KEY_FEEDBACK,0);
  xz_ui_touch(&u,&m,t.x+t.w/2,t.y+t.h/2,1,a);xz_ui_touch(&u,&m,0,0,0,a);
  assert(xz_ui_focused(&u,&m,&f)&&f.kind==XZ_UI_KEY_FEEDBACK);}
 /* The focus ring and info dock render in every theme. */
 for(int theme=0;theme<XZ_THEME_COUNT;theme++){m.theme=theme;u.adjusting=theme&1;assert(xz_ui_render(&u,&m,frame,800*480,800));}
}
/* DJ Profile: the header chip opens a list of every profile on the USBs; a pick
   names its entry; a tap outside or BACK closes it; the encoder walks the list. */
static void profile(void){
 static uint16_t thumb[XZ_UI_DJ_THUMB*XZ_UI_DJ_THUMB];
 struct xz_ui u;struct xz_ui_model m;struct xz_ui_action a[XZ_UI_ACTIONS];struct xz_ui_widget w[XZ_UI_WIDGETS];
 struct xz_ui_dj dj;memset(&dj,0,sizeof(dj));
 memset(&m,0,sizeof(m));xz_ui_init(&u);
 /* No reader: no chip, deck pickers keep their full width. */
 u.page=XZ_UI_KEY_SETUP;size_t n=xz_ui_layout(&u,&m,w);
 for(size_t i=0;i<n;i++)assert(w[i].kind!=XZ_UI_PROFILE);
 for(int d=0;d<4;d++){
  struct xz_ui_widget key_deck=find(&u,&m,XZ_UI_DECK,d);
  assert(key_deck.x==252+d*98&&key_deck.w==94);
 }
 dj.current=-1;dj.pending=-1;m.dj=&dj;
 struct xz_ui_widget chip=find(&u,&m,XZ_UI_PROFILE,0),deck=find(&u,&m,XZ_UI_DECK,1);
 assert(chip.y==0&&chip.x+chip.w<=720&&deck.x+deck.w<=chip.x);
 u.page=XZ_UI_EXTRAS;chip=find(&u,&m,XZ_UI_PROFILE,0);assert(chip.w>100&&chip.x+chip.w<=720);
 /* No profile: the list holds only the setup button, which is the focused control. */
 struct xz_ui_action r=tap(&u,&m,XZ_UI_PROFILE,0);
 assert(r.kind==XZ_UI_PROFILE&&r.value==1&&u.profile_open);
 assert(xz_ui_focused(&u,&m,w)&&w[0].kind==XZ_UI_PANEL&&w[0].index==XZ_UI_PROFILE_SETUP);
 assert(!strcmp(find(&u,&m,XZ_UI_PANEL,XZ_UI_PROFILE_SETUP).label,"SET UP DJ PROFILE"));
 n=xz_ui_layout(&u,&m,w);for(size_t i=0;i<n;i++)assert(w[i].kind!=XZ_UI_PANEL||w[i].index==XZ_UI_PROFILE_SETUP);
 assert(xz_ui_render(&u,&m,frame,800*480,800));
 r=tap(&u,&m,XZ_UI_PANEL,XZ_UI_PROFILE_SETUP);assert(u.page==XZ_UI_PROFILE_SETUP&&!u.profile_open);
 assert(!strcmp(find(&u,&m,XZ_UI_PROFILE_CHECK,0).label,"CHECK USB FOR PROFILES"));
 r=tap(&u,&m,XZ_UI_PROFILE_CHECK,0);assert(r.kind==XZ_UI_PROFILE_CHECK&&!u.profile_open);
 assert(xz_ui_render(&u,&m,frame,800*480,800));
 /* Seven profiles over two sticks: two pages of rows. */
 static const char *names[7]={"DJ KAI","KAI B2B LENA","ALIAS 3","ALIAS 4","ALIAS 5","LENA B","LENA LIVE"};
 dj.count=7;dj.current=5;dj.usb_present=3;
 for(int i=0;i<7;i++)dj.entry[i]=(struct xz_ui_dj_entry){names[i],"",names[i],i>=5,i==0||i==5,0,i==3,0xff2a6d,i&1,i==0?thumb:NULL,0};
 xz_ui_init(&u);u.page=XZ_UI_SCREEN;
 r=tap(&u,&m,XZ_UI_PROFILE,0);assert(u.profile_open&&u.profile_offset==5);
 assert(xz_ui_focused(&u,&m,w)&&w[0].kind==XZ_UI_PROFILE_PICK&&w[0].index==5);
 struct xz_ui_info info;assert(xz_ui_describe(&u,&m,&info)&&!strcmp(info.state,"ACTIVE"));
 /* The window fills a whole page, so it ends on the last profile. */
 int first;assert(xz_ui_profile_window(&u,&m,&first)==5&&first==2);
 assert(xz_ui_render(&u,&m,frame,800*480,800));
 /* Every list control sits inside the panel and clear of the dock. */
 n=xz_ui_layout(&u,&m,w);
 for(size_t i=0;i<n;i++)if(w[i].kind!=XZ_UI_CLOSE&&w[i].kind!=XZ_UI_PROFILE)
  assert(w[i].x>=XZ_UI_PROFILE_LIST_X&&w[i].x+w[i].w<=XZ_UI_PROFILE_LIST_X+XZ_UI_PROFILE_LIST_W&&w[i].y+w[i].h<=xz_ui_profile_list_bottom(&u,&m)&&w[i].y+w[i].h<=XZ_UI_CB);
 {struct xz_ui_widget prev=find(&u,&m,XZ_UI_PROFILE_PAGE,-1);
  assert(xz_ui_touch(&u,&m,prev.x+5,prev.y+5,1,a)==0);xz_ui_touch(&u,&m,0,0,0,a);}
 assert(u.profile_open&&u.profile_offset==0);
 assert(xz_ui_profile_window(&u,&m,&first)==5&&first==0);
 assert(xz_ui_render(&u,&m,frame,800*480,800));
 /* A damaged profile cannot be picked and says why. */
 struct xz_ui_widget bad=find(&u,&m,XZ_UI_PROFILE_PICK,3);
 assert(xz_ui_touch(&u,&m,bad.x+5,bad.y+5,1,a)==1&&a[0].kind==XZ_UI_UNAVAILABLE&&strstr(u.notice,"DAMAGED"));xz_ui_touch(&u,&m,0,0,0,a);
 assert(u.profile_open);
 /* Picking names the entry, closes the list and returns focus to the chip. */
 r=tap(&u,&m,XZ_UI_PROFILE_PICK,1);assert(r.kind==XZ_UI_PROFILE_PICK&&r.index==1&&!u.profile_open);
 assert(xz_ui_focused(&u,&m,w)&&w[0].kind==XZ_UI_PROFILE);
 assert(u.page==XZ_UI_SCREEN);
 /* A tap outside closes without acting on the page underneath. */
 tap(&u,&m,XZ_UI_PROFILE,0);assert(u.profile_open);
 assert(xz_ui_touch(&u,&m,200,200,1,a)==0&&!u.profile_open);xz_ui_touch(&u,&m,0,0,0,a);
 assert(u.page==XZ_UI_SCREEN);
 /* BACK closes the list before it leaves the page. */
 tap(&u,&m,XZ_UI_PROFILE,0);assert(xz_ui_back(&u,a)==0&&!u.profile_open&&u.page==XZ_UI_SCREEN);
 /* Encoder: push opens on the header's profile, a turn moves, push picks. */
 u.focus_kind=XZ_UI_PROFILE;u.focus_index=0;u.focus_deck=-1;
 xz_ui_encoder_push(&u,&m,a);assert(u.profile_open);
 xz_ui_encoder_turn(&u,&m,1,a);assert(xz_ui_focused(&u,&m,w)&&w[0].kind==XZ_UI_PROFILE_PICK&&w[0].index==6);
 size_t got=xz_ui_encoder_push(&u,&m,a);assert(got>=1&&a[0].kind==XZ_UI_PROFILE_PICK&&a[0].index==6&&!u.profile_open);
 /* A switch in progress blocks a second pick; its result shows on every page for a moment. */
 dj.pending=1;tap(&u,&m,XZ_UI_PROFILE,0);
 struct xz_ui_widget busy=find(&u,&m,XZ_UI_PROFILE_PICK,6);
 assert(xz_ui_touch(&u,&m,busy.x+5,busy.y+5,1,a)==1&&a[0].kind==XZ_UI_UNAVAILABLE&&strstr(u.notice,"SWITCHING"));xz_ui_touch(&u,&m,0,0,0,a);
 xz_ui_back(&u,a);dj.pending=-1;
 dj.status="LENA LIVE IS ACTIVE ON USB 2";dj.flash=1;u.page=XZ_UI_PLAY;
 assert(!strcmp(xz_ui_footer_text(&u,&m),dj.status));dj.flash=0;assert(strcmp(xz_ui_footer_text(&u,&m),dj.status));
 /* Every theme draws the chip and the open list. */
 for(int t=0;t<XZ_THEME_COUNT;t++){m.theme=t;u.profile_open=0;assert(xz_ui_render(&u,&m,frame,800*480,800));u.profile_open=1;assert(xz_ui_render(&u,&m,frame,800*480,800));}
}
/* Taps a widget; returns how many actions the press produced. */
static size_t press(struct xz_ui *u,struct xz_ui_model *m,enum xz_ui_action_kind kind,int index,struct xz_ui_action a[XZ_UI_ACTIONS]){
 struct xz_ui_widget w=find(u,m,kind,index);size_t n=xz_ui_touch(u,m,w.x+w.w/2,w.y+w.h/2,1,a);
 struct xz_ui_action released[XZ_UI_ACTIONS];xz_ui_touch(u,m,0,0,0,released);return n;
}
static int listed(struct xz_ui *u,struct xz_ui_model *m,enum xz_ui_action_kind kind,int index){
 struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(u,m,w);
 for(size_t i=0;i<n;i++)if(w[i].kind==kind&&w[i].index==index)return 1;
 return 0;
}
/* DJ PROFILE page and the on-device editor: keyboard, steps, logo picker, save. */
static void editor(void){
 static uint16_t tile[XZ_UI_DJ_TILE*XZ_UI_DJ_TILE];
 static const char images[8][256]={"logo.png","Logos/kai.jpg","a/b/c.jpeg","4.png","5.png","6.png","7.png","8.png"};
 struct xz_ui u;struct xz_ui_model m;struct xz_ui_action a[XZ_UI_ACTIONS];struct xz_ui_widget w[XZ_UI_WIDGETS];
 struct xz_ui_dj dj;memset(&dj,0,sizeof(dj));memset(&m,0,sizeof(m));xz_ui_init(&u);
 dj.current=-1;dj.pending=-1;dj.detail=-1;dj.preview=-1;dj.images_usb=-1;m.dj=&dj;
 /* No USB: the page explains and creating is refused with the reason. */
 u.page=XZ_UI_PROFILE_SETUP;
 assert(press(&u,&m,XZ_UI_PROFILE_EDIT_OPEN,-1,a)==1&&a[0].kind==XZ_UI_UNAVAILABLE&&strstr(u.notice,"INSERT A USB"));
 assert(xz_ui_render(&u,&m,frame,800*480,800));
 /* A USB with no profile: CREATE ON THIS XZ opens the name step with capitals on. */
 dj.usb_present=2;u.notice[0]=0;
 assert(press(&u,&m,XZ_UI_PROFILE_EDIT_OPEN,-1,a)==1&&a[0].kind==XZ_UI_PROFILE_EDIT_OPEN&&a[0].deck==1);
 assert(u.page==XZ_UI_PROFILE_EDIT&&u.edit_step==XZ_UI_EDIT_NAME&&u.kb_shift&&u.edit_usb==1&&u.edit_entry<0&&!u.edit_id[0]);
 assert(u.edit_logo==XZ_UI_LOGO_NONE);
 /* Forward needs a name; BACK from the first step leaves the editor. */
 assert(!listed(&u,&m,XZ_UI_EDIT_STEP,-1)&&listed(&u,&m,XZ_UI_EDIT_STEP,XZ_UI_EDIT_LOGO));
 assert(press(&u,&m,XZ_UI_EDIT_STEP,XZ_UI_EDIT_LOGO,a)==1&&a[0].kind==XZ_UI_UNAVAILABLE&&strstr(u.notice,"NAME"));
 assert(press(&u,&m,XZ_UI_KEYBOARD,'D',a)==1&&a[0].kind==XZ_UI_KEYBOARD);press(&u,&m,XZ_UI_KEYBOARD,'J',a);
 press(&u,&m,XZ_UI_KEYBOARD,' ',a);press(&u,&m,XZ_UI_KEYBOARD,XZ_UI_KB_SHIFT,a);assert(!u.kb_shift&&listed(&u,&m,XZ_UI_KEYBOARD,'k'));
 press(&u,&m,XZ_UI_KEYBOARD,'k',a);press(&u,&m,XZ_UI_KEYBOARD,'q',a);press(&u,&m,XZ_UI_KEYBOARD,XZ_UI_KB_DELETE,a);
 press(&u,&m,XZ_UI_KEYBOARD,XZ_UI_KB_SYMBOLS,a);assert(listed(&u,&m,XZ_UI_KEYBOARD,'&')&&!listed(&u,&m,XZ_UI_KEYBOARD,'q'));
 press(&u,&m,XZ_UI_KEYBOARD,'&',a);press(&u,&m,XZ_UI_KEYBOARD,XZ_UI_KB_SYMBOLS,a);
 assert(!strcmp(u.edit_name,"DJ k&"));
 /* Every key sits in the content area, clear of the others and of the dock. */
 size_t n=xz_ui_layout(&u,&m,w);
 for(size_t i=0;i<n;i++)if(w[i].kind==XZ_UI_KEYBOARD||w[i].kind==XZ_UI_EDIT_STEP){
  assert(w[i].x>=XZ_UI_CX&&w[i].x+w[i].w<=XZ_UI_CX+XZ_UI_CW&&w[i].y>=XZ_UI_CY&&w[i].y+w[i].h<=XZ_UI_CB);
  for(size_t k=i+1;k<n;k++)assert(w[i].x+w[i].w<=w[k].x||w[k].x+w[k].w<=w[i].x||w[i].y+w[i].h<=w[k].y||w[k].y+w[k].h<=w[i].y);
 }
 assert(xz_ui_render(&u,&m,frame,800*480,800));
 /* The name stops at its typed limit. */
 {struct xz_ui full=u;memset(full.edit_name,'x',XZ_UI_DJ_NAME_TYPED);full.edit_name[XZ_UI_DJ_NAME_TYPED]=0;
  assert(press(&full,&m,XZ_UI_KEYBOARD,'x',a)==1&&a[0].kind==XZ_UI_UNAVAILABLE&&strlen(full.edit_name)==XZ_UI_DJ_NAME_TYPED);}
 /* The encoder alone types: the first key is focused and a push types it. */
 {struct xz_ui enc=u;enc.focus_kind=XZ_UI_NONE;assert(xz_ui_focused(&enc,&m,w)&&w[0].kind==XZ_UI_KEYBOARD&&w[0].index=='1');
  xz_ui_encoder_turn(&enc,&m,1,a);xz_ui_encoder_push(&enc,&m,a);assert(!strcmp(enc.edit_name,"DJ k&2"));}
 /* Logo step: reading, then a page of six images, paging, a pick asks for a preview. */
 assert(press(&u,&m,XZ_UI_EDIT_STEP,XZ_UI_EDIT_LOGO,a)==1&&a[0].kind==XZ_UI_EDIT_STEP&&u.edit_step==XZ_UI_EDIT_LOGO);
 assert(!listed(&u,&m,XZ_UI_LOGO_PICK,0)&&listed(&u,&m,XZ_UI_LOGO_PICK,XZ_UI_LOGO_NONE)&&!listed(&u,&m,XZ_UI_LOGO_PICK,XZ_UI_LOGO_KEEP));
 assert(xz_ui_render(&u,&m,frame,800*480,800));
 dj.images_usb=1;dj.image_count=8;dj.images=images;
 /* The logo in the DJ folder named like the typed name is picked for a new profile, never over the DJ's own pick. */
 assert(xz_ui_profile_autopick(&u,&m)==-1&&u.edit_logo==XZ_UI_LOGO_NONE);
 static const char named_images[8][256]={"DJPROFILE/Other/logo.png","DJPROFILES/dj K&/flyer.png","djprofiles/DJ K&/Logo.JPG",
  "DJPROFILES/DJ K&/sub/logo.png","DJPROFILES/DJ K&x/logo.png","DJ K&/logo.png","7.png","8.png"};
 dj.images=named_images;
 {struct xz_ui own=u;press(&own,&m,XZ_UI_LOGO_PICK,XZ_UI_LOGO_NONE,a);assert(xz_ui_profile_autopick(&own,&m)==-1&&own.edit_logo==XZ_UI_LOGO_NONE);}
 dj.images_reading=1;assert(xz_ui_profile_autopick(&u,&m)==-1);dj.images_reading=0;
 assert(xz_ui_profile_autopick(&u,&m)==2&&u.edit_logo==2&&xz_ui_profile_autopick(&u,&m)==-1);
 /* With that logo the list has six rows. */
 assert(listed(&u,&m,XZ_UI_LOGO_PICK,5)&&!listed(&u,&m,XZ_UI_LOGO_PICK,6)&&find(&u,&m,XZ_UI_LOGO_PICK,0).y==52);
 assert(xz_ui_render(&u,&m,frame,800*480,800));
 /* A folder with only a flyer is still the DJ's: its first image. */
 {static const char flyer[2][256]={"x.png","DJPROFILE/dj k&/flyer.png"};struct xz_ui own=u;own.edit_logo=XZ_UI_LOGO_NONE;own.edit_logo_chosen=0;
  dj.images=flyer;dj.image_count=2;assert(xz_ui_profile_autopick(&own,&m)==1);dj.images=named_images;dj.image_count=8;}
 /* Without a logo in the DJ's folder, the copy instructions take two rows. */
 dj.images=images;u.edit_logo=XZ_UI_LOGO_NONE;u.image_offset=0;
 assert(listed(&u,&m,XZ_UI_LOGO_PICK,3)&&!listed(&u,&m,XZ_UI_LOGO_PICK,4)&&listed(&u,&m,XZ_UI_LOGO_PAGE,1));
 assert(find(&u,&m,XZ_UI_LOGO_PICK,0).y==124&&find(&u,&m,XZ_UI_LOGO_PICK,3).y+42<find(&u,&m,XZ_UI_LOGO_RESCAN,0).y);
 assert(xz_ui_render(&u,&m,frame,800*480,800));
 press(&u,&m,XZ_UI_LOGO_PAGE,1,a);assert(u.image_offset==4&&listed(&u,&m,XZ_UI_LOGO_PICK,7));
 assert(press(&u,&m,XZ_UI_LOGO_PICK,7,a)==1&&a[0].kind==XZ_UI_LOGO_PICK&&a[0].index==7&&u.edit_logo==7);
 dj.preview=7;dj.preview_state=XZ_UI_PREVIEW_LOADING;assert(xz_ui_render(&u,&m,frame,800*480,800));
 dj.preview_state=XZ_UI_PREVIEW_FAILED;dj.preview_error="NOT A PNG OR JPEG IMAGE";
 assert(!strcmp(xz_ui_footer_text(&u,&m),"NOT A PNG OR JPEG IMAGE"));assert(xz_ui_render(&u,&m,frame,800*480,800));
 /* BACK goes to the previous step, the name is kept. */
 assert(xz_ui_back(&u,a)==0&&u.page==XZ_UI_PROFILE_EDIT&&u.edit_step==XZ_UI_EDIT_NAME&&!strcmp(u.edit_name,"DJ k&"));
 press(&u,&m,XZ_UI_EDIT_STEP,XZ_UI_EDIT_LOGO,a);press(&u,&m,XZ_UI_EDIT_STEP,XZ_UI_EDIT_TAGLINE,a);
 assert(u.edit_step==XZ_UI_EDIT_TAGLINE&&!strcmp(find(&u,&m,XZ_UI_EDIT_STEP,XZ_UI_EDIT_REVIEW).label,"SKIP TAGLINE"));
 press(&u,&m,XZ_UI_EDIT_STEP,XZ_UI_EDIT_REVIEW,a);assert(u.edit_step==XZ_UI_EDIT_REVIEW);
 /* Review: the failed logo blocks saving with its reason; a ready preview allows it. */
 assert(xz_ui_focused(&u,&m,w)&&w[0].kind==XZ_UI_PROFILE_SAVE&&w[0].index==1);
 assert(press(&u,&m,XZ_UI_PROFILE_SAVE,1,a)==1&&a[0].kind==XZ_UI_UNAVAILABLE&&strstr(u.notice,"LOGO"));
 assert(xz_ui_render(&u,&m,frame,800*480,800));
 dj.preview_state=XZ_UI_PREVIEW_READY;dj.preview_tile=tile;dj.usb_present=3;
 assert(listed(&u,&m,XZ_UI_PROFILE_SAVE,0)&&listed(&u,&m,XZ_UI_PROFILE_SAVE,1));
 assert(xz_ui_render(&u,&m,frame,800*480,800));
 assert(press(&u,&m,XZ_UI_PROFILE_SAVE,0,a)==1&&a[0].kind==XZ_UI_PROFILE_SAVE&&a[0].index==0);
 assert(u.edit_usb==0&&u.page==XZ_UI_PROFILE_SETUP&&!strcmp(u.edit_name,"DJ k&")&&u.edit_logo==7);
 /* The page with profiles: the shown one, paging between them, edit and activate. */
 dj.count=2;dj.current=1;dj.saving=0;
 dj.entry[0]=(struct xz_ui_dj_entry){"DJ KAI","Deep tech","djp_kai",0,1,0,0,0xff2a6d,1,NULL,0};
 dj.entry[1]=(struct xz_ui_dj_entry){"LENA B","","djp_lena",1,0,0,0,0,0,NULL,0};
 xz_ui_init(&u);u.page=XZ_UI_PROFILE_SETUP;
 assert(xz_ui_profile_shown(&u,&m)==1&&listed(&u,&m,XZ_UI_PROFILE_PICK,1)&&listed(&u,&m,XZ_UI_PROFILE_EDIT_OPEN,1));
 dj.detail=1;dj.detail_tile=tile;
 for(int t=0;t<XZ_THEME_COUNT;t++){
  m.theme=t;assert(xz_ui_render(&u,&m,frame,800*480,800));
  /* QR sharing is removed: its former panel has no code or scan prompts. */
  for(int y=52;y<324;y++)for(int x=568;x<792;x++)assert(frame[y*800+x]==frame[330*800+788]);
 }m.theme=0;
 assert(press(&u,&m,XZ_UI_PROFILE_VIEW,1,a)==1&&a[0].kind==XZ_UI_PROFILE_VIEW&&u.profile_view==0&&xz_ui_profile_shown(&u,&m)==0);
 assert(!listed(&u,&m,XZ_UI_PROFILE_PICK,0));
 /* Editing opens on the review with the profile's own text, USB and id. */
 assert(press(&u,&m,XZ_UI_PROFILE_EDIT_OPEN,0,a)==1&&a[0].kind==XZ_UI_PROFILE_EDIT_OPEN&&a[0].index==0);
 assert(u.edit_step==XZ_UI_EDIT_REVIEW&&u.edit_origin==XZ_UI_EDIT_REVIEW&&!strcmp(u.edit_name,"DJ KAI")&&!strcmp(u.edit_tagline,"Deep tech"));
 assert(!strcmp(u.edit_id,"djp_kai")&&u.edit_usb==0&&u.edit_logo==XZ_UI_LOGO_KEEP&&!listed(&u,&m,XZ_UI_PROFILE_SAVE,1));
 assert(press(&u,&m,XZ_UI_PROFILE_SAVE,0,a)==1&&a[0].kind==XZ_UI_PROFILE_SAVE&&u.page==XZ_UI_PROFILE_SETUP&&!u.history_count);
 u.profile_view=0;press(&u,&m,XZ_UI_PROFILE_EDIT_OPEN,0,a);assert(u.page==XZ_UI_PROFILE_EDIT);
 press(&u,&m,XZ_UI_EDIT_STEP,XZ_UI_EDIT_TAGLINE,a);assert(!strcmp(find(&u,&m,XZ_UI_EDIT_STEP,XZ_UI_EDIT_REVIEW).label,"DONE"));
 press(&u,&m,XZ_UI_KEYBOARD,XZ_UI_KB_DELETE,a);assert(!strcmp(u.edit_tagline,"Deep tec"));
 /* BACK from a sub-step returns to the review, then leaves. */
 xz_ui_back(&u,a);assert(u.edit_step==XZ_UI_EDIT_REVIEW&&u.page==XZ_UI_PROFILE_EDIT);
 press(&u,&m,XZ_UI_EDIT_STEP,XZ_UI_EDIT_LOGO,a);assert(listed(&u,&m,XZ_UI_LOGO_PICK,XZ_UI_LOGO_KEEP));
 xz_ui_back(&u,a);xz_ui_back(&u,a);assert(u.page==XZ_UI_PROFILE_SETUP);
 /* Damaged and older cards cannot be edited, and say why. */
 dj.entry[0].damaged=1;u.profile_view=0;
 assert(press(&u,&m,XZ_UI_PROFILE_EDIT_OPEN,0,a)==1&&a[0].kind==XZ_UI_UNAVAILABLE&&strstr(u.notice,"DAMAGED"));
 dj.entry[0].damaged=0;dj.entry[0].locked=1;u.notice[0]=0;
 assert(press(&u,&m,XZ_UI_PROFILE_EDIT_OPEN,0,a)==1&&a[0].kind==XZ_UI_UNAVAILABLE&&strstr(u.notice,"OLDER"));
 /* Every step draws in every theme. */
 dj.entry[0].locked=0;u.notice[0]=0;press(&u,&m,XZ_UI_PROFILE_EDIT_OPEN,0,a);
 for(int step=0;step<4;step++)for(int t=0;t<XZ_THEME_COUNT;t++){u.edit_step=step;m.theme=t;assert(xz_ui_render(&u,&m,frame,800*480,800));}
 /* A folder profile edits like a new one: its in-memory id never reaches the writer. */
 m.theme=0;dj.entry[1].folder=1;dj.entry[1].dj_id="folder:0123456789abcdef";
 xz_ui_init(&u);u.page=XZ_UI_PROFILE_SETUP;u.profile_view=1;
 for(int t=0;t<XZ_THEME_COUNT;t++){m.theme=t;assert(xz_ui_render(&u,&m,frame,800*480,800));}m.theme=0;
 assert(press(&u,&m,XZ_UI_PROFILE_EDIT_OPEN,1,a)==1&&a[0].kind==XZ_UI_PROFILE_EDIT_OPEN);
 assert(u.page==XZ_UI_PROFILE_EDIT&&u.edit_entry==1&&!u.edit_id[0]&&!strcmp(u.edit_name,"LENA B")&&u.edit_logo==XZ_UI_LOGO_KEEP);
}
static int rb_overlap(struct xz_ui_widget a,struct xz_ui_widget b){return a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h;}
/* PLAY > BUTTON NEXT TO LINK: two slots of seven choices, no overlap in any theme, labels that fit, one help
   line per slot; no brand name on screen; PLAY and VJ.TOOLS no longer offer the old switches. */
static void rekordbox_buttons(void){
 struct xz_ui u;struct xz_ui_model m;struct xz_ui_widget w[XZ_UI_WIDGETS];struct xz_ui_info info;
 memset(&m,0,sizeof(m));xz_ui_init(&u);m.rekordbox_assign[0]=XZ_UI_RB_INTERFACE;m.rekordbox_assign[1]=XZ_UI_RB_STOCK;
 static const char *const names[XZ_UI_RB_CHOICES]={"MODS MENU","ITS OWN JOB","INTERFACE","VJ.TOOLS SCREEN","STEMS","NOTHING","DECK VIEW"};
 for(int k=0;k<XZ_UI_RB_CHOICES;k++)assert(!strcmp(xz_ui_rekordbox_choice_name(k),names[k])&&xz_ui_rekordbox_choice_help(k)[0]&&!strstr(xz_ui_rekordbox_choice_help(k),"REKORDBOX"));
 assert(!xz_ui_rekordbox_choice_name(XZ_UI_RB_CHOICES)&&!xz_ui_rekordbox_choice_help(-1));
 u.page=XZ_UI_PLAY;{struct xz_ui_action a=tap(&u,&m,XZ_UI_FEATURE_LINK,XZ_UI_BUTTONS);assert(a.kind==XZ_UI_PANEL&&u.page==XZ_UI_BUTTONS);}
 struct xz_ui_action back[XZ_UI_ACTIONS];assert(xz_ui_back(&u,back)==1&&u.page==XZ_UI_PLAY);
 u.page=XZ_UI_BUTTONS;
 for(int theme=0;theme<XZ_THEME_COUNT;theme++){
  m.theme=theme;size_t n=xz_ui_layout(&u,&m,w);int choices=0;
  for(size_t i=0;i<n;i++){
   for(size_t j=i+1;j<n;j++)assert(!rb_overlap(w[i],w[j]));
   if(w[i].kind!=XZ_UI_REKORDBOX_ASSIGN)continue;
   choices++;
   assert(w[i].x>=XZ_UI_CX&&w[i].x+w[i].w<=XZ_UI_CX+XZ_UI_CW&&w[i].y>=XZ_UI_HEAD+48&&w[i].y+w[i].h<=XZ_UI_CB-48&&w[i].h>=40);
   struct xz_canvas c={frame,800,800,480,theme};
   assert(xz_text_width(c,w[i].label,1,100)<=w[i].w-24);
   assert(xz_ui_widget_look(&u,&m,&w[i]).selected==(m.rekordbox_assign[w[i].index/XZ_UI_RB_CHOICES]==w[i].index%XZ_UI_RB_CHOICES));
  }
  assert(choices==2*XZ_UI_RB_CHOICES);
  assert(xz_ui_render(&u,&m,frame,800*480,800));
 }
 /* A tap sets that slot: index = slot, value = choice. */
 for(int slot=0;slot<2;slot++)for(int k=0;k<XZ_UI_RB_CHOICES;k++){
  struct xz_ui_action a=tap(&u,&m,XZ_UI_REKORDBOX_ASSIGN,slot*XZ_UI_RB_CHOICES+k);
  assert(a.kind==XZ_UI_REKORDBOX_ASSIGN&&a.index==slot&&(int)a.value==k&&u.page==XZ_UI_BUTTONS);
 }
 /* The info box names the slot and says what the button will do. */
 u.focus_kind=XZ_UI_REKORDBOX_ASSIGN;u.focus_index=XZ_UI_RB_CHOICES+XZ_UI_RB_STOCK;u.focus_deck=-1;
 assert(xz_ui_describe(&u,&m,&info)&&!strcmp(info.title,"SHIFT + PRESS: ITS OWN JOB")&&!strcmp(info.state,"SELECTED"));
 assert(!strcmp(info.body,xz_ui_rekordbox_choice_help(XZ_UI_RB_STOCK)));
 u.focus_index=XZ_UI_RB_MODS;assert(xz_ui_describe(&u,&m,&info)&&!strcmp(info.title,"PRESS: MODS MENU")&&!info.state[0]);
 /* The retired switches are gone from PLAY and VJ.TOOLS. */
 u.page=XZ_UI_PLAY;{size_t n=xz_ui_layout(&u,&m,w);for(size_t i=0;i<n;i++)assert(w[i].kind!=XZ_UI_REKORDBOX_KEY);}
 u.page=XZ_UI_CONNECTION;{size_t n=xz_ui_layout(&u,&m,w);int assign=0;
  for(size_t i=0;i<n;i++)if(w[i].kind==XZ_UI_TAKEOVER_ASSIGN){assert(w[i].index!=1);assign++;
   for(size_t j=0;j<n;j++)if(j!=i)assert(!rb_overlap(w[i],w[j]));}
  assert(assign==2);}
 puts("PASS PLAY > BUTTON NEXT TO LINK: 2 x 7 choices, 25 themes without overlap, slot/value taps, help, no brand name, retired switches gone");
}
/* Every explanation the dock shows fits its three lines, so nothing is cut mid-word (the room MODERN's detail pane
   gave): every focusable control of every page, both interfaces, every theme's face. */
static void dock_fits(void){
 static struct xz_ui_dj dj={.count=1,.current=0,.pending=-1,.detail=-1,.preview=-1,.images_usb=-1};
 dj.entry[0]=(struct xz_ui_dj_entry){.name="ANNA",.usb=0,.stick_active=1};
 int bad=0,checked=0;
 for(int iface=0;iface<2;iface++)for(int route=0;route<2;route++)for(int theme=0;theme<XZ_THEME_COUNT;theme++)for(int page=0;page<XZ_UI_PAGE_COUNT;page++){
  if(iface!=(theme==XZ_THEME_FLAGSHIP))continue;   /* each interface draws its own looks */
  struct xz_ui u;struct xz_ui_model m;memset(&m,0,sizeof m);xz_ui_init(&u);u.page=(enum xz_ui_page)page;
  m.theme=theme;m.ui_interface=iface?XZ_UI_INTERFACE_MODERN:XZ_UI_INTERFACE_CLASSIC;m.modern_look.known=1;m.dj=&dj;
  m.enabled=XZ_UI_STEM|XZ_UI_GATE;m.deck[0].ready=m.deck[1].ready=0x3fff;m.connection.ready=1;
  m.route.state=route?XZ_UI_ROUTE_READY:XZ_UI_ROUTE_NOT_DETECTED;m.settings_status="SETTINGS SAVED TO USB 1";
  for(int d=0;d<4;d++){m.deck[d].groove_active=m.deck[d].sample_active=-1;m.deck[d].sample_volume=1;}
  struct xz_canvas c={frame,800,800,480,theme};
  struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(&u,&m,w);
  for(size_t i=0;i<n;i++){
   if(!xz_ui_focusable(&w[i]))continue;
   struct xz_ui_info info;xz_ui_describe_widget(&u,&m,&w[i],&info);
   int lines=xz_ui_wrap_lines(c,XZ_UI_DOCK_BODY_W,info.body);checked++;
   /* the amber line on the right, why a deck is left out, is one line of the right column */
   if(info.excluded&&xz_ui_wrap_lines(c,218,info.excluded)>1){bad++;fprintf(stderr,"dock: theme %d page %d %s: excluded too long: %s\n",theme,page,info.title,info.excluded);}
   if(lines>XZ_UI_DOCK_LINES){bad++;fprintf(stderr,"dock: theme %d page %d %s: %d lines: %s\n",theme,page,info.title,lines,info.body);}
  }
 }
 assert(!bad);
 printf("PASS dock: %d explanations fit three lines in both interfaces and every theme\n",checked);
}
static uint16_t rgb565(uint32_t c){return (uint16_t)((c>>19&31)<<11|(c>>10&63)<<5|(c>>3&31));}
/* The encoder's stops from the page's first control, 40 turns on. */
static void stops(struct xz_ui u,struct xz_ui_model *m,struct xz_ui_widget out[40]){
 struct xz_ui_action a[XZ_UI_ACTIONS];
 for(int i=0;i<40;i++){xz_ui_encoder_turn(&u,m,1,a);memset(&out[i],0,sizeof out[i]);xz_ui_focused(&u,m,&out[i]);}
}
/* MODERN keeps a known stock counter error in the dock's title row while MODS is open (stock errors are never
   covered). Nothing outside the strip changes, the encoder stops where it did, every control answers a touch as
   before and a touch on the strip closes MODS. CLASSIC (the stock deck area shows there) draws and acts as before. */
static void counter_strip(void){
 static uint16_t plain[800*480];
 static const enum xz_ui_page pages[]={XZ_UI_HOME,XZ_UI_PLAY,XZ_UI_BEAT_JUMP,XZ_UI_STEM_SECTION,XZ_UI_SCREEN,XZ_UI_SCREEN_LOOK,XZ_UI_FOUR_DECKS,XZ_UI_CONNECT,XZ_UI_EXTRAS};
 static const struct xz_ui_counter_error unsupported={"E-8304","UNSUPPORTED"},cannot={"E-8302","CANNOT PLAY TRACK"};
 int shown=0;
 for(int iface=0;iface<2;iface++)for(size_t p=0;p<sizeof pages/sizeof pages[0];p++)for(int which=1;which<4;which++){
  struct xz_ui u;struct xz_ui_model m;memset(&m,0,sizeof m);xz_ui_init(&u);u.page=pages[p];
  m.theme=iface?XZ_THEME_FLAGSHIP:0;m.ui_interface=iface?XZ_UI_INTERFACE_MODERN:XZ_UI_INTERFACE_CLASSIC;m.modern_look.known=1;
  m.enabled=XZ_UI_STEM|XZ_UI_GATE;m.deck[0].ready=m.deck[1].ready=0x3fff;m.settings_status="SETTINGS SAVED TO USB 1";
  for(int d=0;d<4;d++){m.deck[d].groove_active=m.deck[d].sample_active=-1;m.deck[d].sample_volume=1;}
  struct xz_ui_model err=m;
  if(which&1)err.counter_error[0]=unsupported;
  if(which&2)err.counter_error[1]=cannot;
  struct xz_ui_widget before[XZ_UI_WIDGETS],after[XZ_UI_WIDGETS],stop_plain[40],stop_err[40];
  size_t nb=xz_ui_layout(&u,&m,before),na=xz_ui_layout(&u,&err,after);
  assert(!xz_ui_counter_errors(&m));
  assert(xz_ui_render(&u,&m,frame,800*480,800));memcpy(plain,frame,sizeof plain);
  assert(xz_ui_render(&u,&err,frame,800*480,800));
  stops(u,&m,stop_plain);stops(u,&err,stop_err);
  assert(!memcmp(stop_plain,stop_err,sizeof stop_plain));
  if(!iface){assert(!xz_ui_counter_errors(&err)&&na==nb&&!memcmp(before,after,nb*sizeof *before)&&!memcmp(plain,frame,sizeof plain));continue;}
  assert(xz_ui_counter_errors(&err)==(unsigned)which);
  /* One more widget, last: the strip on the dock's title row, touch only. Every other widget stays where it was. */
  assert(na==nb+1&&!memcmp(before,after,nb*sizeof *before));
  struct xz_ui_widget s=after[nb];
  assert(s.kind==XZ_UI_CLOSE&&s.index==XZ_UI_CLOSE_COUNTER&&!xz_ui_focusable(&s));
  assert(s.x==0&&s.y==XZ_UI_COUNTER_Y&&s.w==XZ_UI_COUNTER_W&&s.h==XZ_UI_COUNTER_H&&s.y+s.h<=XZ_UI_DOCK_Y+XZ_UI_DOCK_BODY_Y);
  /* Only the strip's pixels change: page, rail, header, explanation and the right column stay as they were. */
  for(int y=0;y<480;y++)for(int x=0;x<800;x++)
   if(!(x<s.x+s.w&&y>=s.y&&y<s.y+s.h))assert(frame[y*800+x]==plain[y*800+x]);
  assert(frame[(s.y+2)*800+1]==rgb565(0xff4619)&&frame[(s.y+1)*800+300]==rgb565(0x5a0c0c));
  assert(memcmp(frame+s.y*800,plain+s.y*800,(size_t)s.h*800*sizeof *frame));
  /* Each deck's code is drawn: the strip differs between one deck and the other. */
  /* Every control answers a touch exactly as it did without the error. */
  for(size_t i=0;i<nb;i++){
   struct xz_ui t1=u,t2=u;struct xz_ui_action a1[XZ_UI_ACTIONS],a2[XZ_UI_ACTIONS];memset(a1,0,sizeof a1);memset(a2,0,sizeof a2);
   int x=before[i].x+before[i].w/2,y=before[i].y+before[i].h/2;
   size_t n1=xz_ui_touch(&t1,&m,x,y,1,a1),n2=xz_ui_touch(&t2,&err,x,y,1,a2);
   assert(n1==n2&&!memcmp(a1,a2,n1*sizeof *a1)&&t1.focus_kind==t2.focus_kind&&t1.focus_index==t2.focus_index&&t1.page==t2.page);
  }
  /* A touch anywhere on the strip closes MODS (the runtime's CLOSE), and moves no focus. */
  int xs[3]={2,s.w/2,s.w-8};
  for(int k=0;k<3;k++){
   struct xz_ui t=u;struct xz_ui_action a[XZ_UI_ACTIONS];
   assert(xz_ui_touch(&t,&err,xs[k],s.y+s.h/2,1,a)==1&&a[0].kind==XZ_UI_CLOSE&&a[0].phase==XZ_UI_PRESS);
   assert(t.focus_kind==u.focus_kind&&t.focus_index==u.focus_index&&t.page==u.page);xz_ui_touch(&t,&err,0,0,0,a);
  }
  /* The error leaving takes the strip with it. */
  struct xz_ui_model clear=err;memset(clear.counter_error,0,sizeof clear.counter_error);
  assert(xz_ui_layout(&u,&clear,after)==nb&&xz_ui_render(&u,&clear,frame,800*480,800)&&!memcmp(plain,frame,sizeof plain));
  shown++;
 }
 /* The two decks' rows differ: each shows its own badge, code and text. */
 {
  struct xz_ui u;struct xz_ui_model m;memset(&m,0,sizeof m);xz_ui_init(&u);u.page=XZ_UI_PLAY;
  m.theme=XZ_THEME_FLAGSHIP;m.ui_interface=XZ_UI_INTERFACE_MODERN;m.modern_look.known=1;
  static uint16_t one[800*XZ_UI_COUNTER_H];
  m.counter_error[0]=unsupported;assert(xz_ui_render(&u,&m,frame,800*480,800));memcpy(one,frame+XZ_UI_COUNTER_Y*800,sizeof one);
  memset(m.counter_error,0,sizeof m.counter_error);m.counter_error[1]=unsupported;assert(xz_ui_render(&u,&m,frame,800*480,800));
  assert(memcmp(one,frame+XZ_UI_COUNTER_Y*800,sizeof one));
  /* A code or text missing is not an error to show. */
  m.counter_error[1].text=NULL;assert(!xz_ui_counter_errors(&m));
 }
 printf("PASS counter strip: %d MODERN cases (one deck, the other, both) change only the dock's title row; CLASSIC unchanged\n",shown);
}
/* The player's E-8307 hub warning (the loopback cable) takes the dock's title row in both interfaces, adds no
   control and changes nothing else; a deck's counter error keeps the row; READY changes only the reason. */
static void hub_strip(void){
 static uint16_t plain[800*480],ready[800*480];
 static const enum xz_ui_page pages[]={XZ_UI_HOME,XZ_UI_PLAY,XZ_UI_FOUR_DECKS,XZ_UI_SCREEN};
 int shown=0;
 for(int iface=0;iface<2;iface++)for(size_t p=0;p<sizeof pages/sizeof pages[0];p++){
  struct xz_ui u;struct xz_ui_model m;memset(&m,0,sizeof m);xz_ui_init(&u);u.page=pages[p];
  m.theme=iface?XZ_THEME_FLAGSHIP:0;m.ui_interface=iface?XZ_UI_INTERFACE_MODERN:XZ_UI_INTERFACE_CLASSIC;m.modern_look.known=1;
  m.route.state=XZ_UI_ROUTE_STARTING;
  for(int d=0;d<4;d++){m.deck[d].groove_active=m.deck[d].sample_active=-1;m.deck[d].sample_volume=1;}
  struct xz_ui_model hub=m;hub.usb_hub="E-8307: HUBS ARE NOT SUPPORTED";
  struct xz_ui_widget before[XZ_UI_WIDGETS],after[XZ_UI_WIDGETS];
  size_t nb=xz_ui_layout(&u,&m,before),na=xz_ui_layout(&u,&hub,after);
  assert(!xz_ui_usb_hub_notice(&m)&&xz_ui_usb_hub_notice(&hub)&&na==nb&&!memcmp(before,after,nb*sizeof *before));
  assert(xz_ui_render(&u,&m,frame,800*480,800));memcpy(plain,frame,sizeof plain);
  assert(xz_ui_render(&u,&hub,frame,800*480,800));
  for(int y=0;y<480;y++)for(int x=0;x<800;x++)
   if(!(x<XZ_UI_COUNTER_W&&y>=XZ_UI_COUNTER_Y&&y<XZ_UI_COUNTER_Y+XZ_UI_COUNTER_H))assert(frame[y*800+x]==plain[y*800+x]);
  assert(frame[(XZ_UI_COUNTER_Y+2)*800+1]==rgb565(0xff4619)&&frame[(XZ_UI_COUNTER_Y+1)*800+300]==rgb565(0x5a0c0c));
  memcpy(plain,frame,sizeof plain);
  hub.route.state=XZ_UI_ROUTE_READY;assert(xz_ui_render(&u,&hub,frame,800*480,800));memcpy(ready,frame,sizeof ready);
  assert(memcmp(plain,ready,sizeof plain));
  if(getenv("XZ_HUB_PREVIEW")&&p<2){
   char path[512];
   for(int r=0;r<2;r++){
    snprintf(path,sizeof path,"%s/hub-%s-%s-%s.ppm",getenv("XZ_HUB_PREVIEW"),iface?"modern":"classic",p?"play":"home",r?"ready":"starting");
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n800 480\n255\n");
    const uint16_t *px=r?ready:plain;
    for(int i=0;i<800*480;i++){unsigned v=px[i];unsigned char c[3]={(unsigned char)((v>>11)*255/31),(unsigned char)((v>>5&63)*255/63),(unsigned char)((v&31)*255/31)};fwrite(c,1,3,f);}
    fclose(f);
   }
  }
  if(iface){
   struct xz_ui_model both=hub;both.counter_error[0]=(struct xz_ui_counter_error){"E-8304","UNSUPPORTED"};
   assert(xz_ui_counter_errors(&both)&&!xz_ui_usb_hub_notice(&both));
  }
  shown++;
 }
 printf("PASS hub strip: %d cases, both interfaces, title row only, no new control, counter errors keep the row\n",shown);
}
int main(int argc,char **argv){
 struct xz_ui u;struct xz_ui_model m;int i,j;
 if(argc>1&&strcmp(argv[1],"--prove-assertions")==0){assert(!"assertions-active");return 1;}
 static const int semantic_themes[4]={0,XZ_THEME_LCARS,XZ_THEME_MATRIX,XZ_THEME_ANALOG};
 for(i=0;i<4;i++)semantics(&u,&m,semantic_themes[i]);
 parity();
 pc_setup();
 ram();
 encoder();
 rekordbox_buttons();
 profile();
 editor();
 dock_fits();
 counter_strip();
 hub_strip();
 assert(!xz_ui_render(&u,&m,frame,10,800));assert(!xz_ui_render(&u,&m,frame,800*480,799));
 for(i=0;i<16;i++)frame[800*480+i]=0xdead;
 for(i=0;i<XZ_THEME_COUNT;i++)for(j=0;j<XZ_UI_PAGE_COUNT;j++){m.theme=i;u.page=(enum xz_ui_page)j;assert(xz_ui_render(&u,&m,frame,800*480,800));}
 for(i=0;i<16;i++)assert(frame[800*480+i]==0xdead);
 /* Save status remains visible even when another notice occupies the footer. */
 {
  uint16_t saved[800*30];
  u.page=XZ_UI_PLAY;strcpy(u.notice,"OTHER NOTICE");m.settings_status="SETTINGS SAVED TO USB";
  assert(xz_ui_render(&u,&m,frame,800*480,800));memcpy(saved,frame+800*416,sizeof(saved));
  m.settings_status="SETTINGS NOT SAVED: CHECK USB";
  assert(xz_ui_render(&u,&m,frame,800*480,800));assert(memcmp(saved,frame+800*416,sizeof(saved)));
  u.notice[0]=0;
 }
 /* A fresh save result takes the footer on every page, then the page's own text returns. */
 {
  enum xz_ui_page pages[]={XZ_UI_THEME_PICK,XZ_UI_GAMES,XZ_UI_CONNECTION};
  m.settings_status="SAVED TO USB 2";m.theme_status="THEME READY";
  for(unsigned i=0;i<sizeof(pages)/sizeof(pages[0]);i++){
   u.page=pages[i];strcpy(u.notice,"OTHER NOTICE");
   m.settings_flash=1;assert(!strcmp(xz_ui_footer_text(&u,&m),"SAVED TO USB 2"));
   m.settings_flash=0;assert(strcmp(xz_ui_footer_text(&u,&m),"SAVED TO USB 2"));
  }
  u.notice[0]=0;m.theme_status=NULL;
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
 puts("UI: touch semantics, readiness, all registered themes, layout parity, bounds, PC stems setup steps, encoder navigation, DJ profile chip, profile page and editor PASS");return 0;
}
