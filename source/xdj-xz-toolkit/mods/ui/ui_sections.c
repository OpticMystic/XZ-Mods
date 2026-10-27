/* SPDX-License-Identifier: MIT */
/* The MODS sections and their picture pages (ui_sections.h). */
#include "ui_sections.h"
#include "stem_pads.h"
#include "wave_viewport.h"
#include "layout_cycle.h"
#include "cable_anim.h"
#include "loopback_port.h"
#include <stdio.h>
#include <string.h>

const enum xz_ui_page xz_ui_sections[XZ_UI_SECTIONS]={XZ_UI_HOME,XZ_UI_PLAY,XZ_UI_STEM_SECTION,XZ_UI_SCREEN,XZ_UI_FOUR_DECKS,XZ_UI_CONNECT,XZ_UI_EXTRAS};
const char *xz_ui_section_label(int page){
 switch(page){
 case XZ_UI_HOME:return "HOME";case XZ_UI_PLAY:return "PLAY";case XZ_UI_STEM_SECTION:return "STEMS";case XZ_UI_SCREEN:return "SCREEN";
 case XZ_UI_FOUR_DECKS:return "4 DECKS";case XZ_UI_CONNECT:return "CONNECT";case XZ_UI_EXTRAS:return "EXTRAS";default:return NULL;}
}
enum xz_ui_page xz_ui_page_current(enum xz_ui_page page){
 switch(page){
 case XZ_UI_GENERAL:return XZ_UI_HOME;
 case XZ_UI_CONTROLS:return XZ_UI_STEM_PADS;
 case XZ_UI_THEMES:return XZ_UI_SCREEN;
 case XZ_UI_SETTINGS:return XZ_UI_PLAY;
 default:return page;
 }
}
/* DESIGN 3.4: the runtime's own words, said the same way everywhere. */
const char *xz_ui_save_state(const struct xz_ui_model *m, int *alert){
 const char *s=m?m->settings_status:NULL;int bad=0;const char *out="";
 if(s&&*s){
  if(strstr(s,"SAVING"))out="SAVING...";
  else if(!strcmp(s,"CHANGES LAST UNTIL REBOOT"))out="NOT SAVED: CHANGES LAST UNTIL POWER OFF.";
  else if(strstr(s,"NOT SAVED")||strstr(s,"INVALID")||strstr(s,"UNAVAILABLE")){out=s;bad=1;}
  else if(!strcmp(s,"SETTINGS NOT SAVED: INSERT A USB")||!strcmp(s,"INSERT USB TO SAVE SETTINGS")){out="NO SETTINGS USB: CHANGES LAST UNTIL POWER OFF.";bad=1;}
  else out=s;
 }
 if(alert)*alert=bad;
 return out;
}
/* Through xz_deck_caps, so a ready bit for an engine this runtime does not run never opens its controls. */
int xz_ui_has_engine(const struct xz_ui_model *m, uint32_t cap){
 unsigned feature=cap==XZ_UI_PREVIEW?XZ_DECK_CAN_CUE_PREVIEW:cap==XZ_UI_SAMPLE?XZ_DECK_CAN_XPAD:cap==XZ_UI_GROOVE?XZ_DECK_CAN_GROOVE:0u;
 if(!m||!feature)return 0;
 for(int d=0;d<4;d++)if(xz_deck_caps(m,d)&feature)return 1;
 return 0;
}
int xz_ui_section_page(int page){
 switch(page){
 case XZ_UI_HOME:case XZ_UI_PLAY:case XZ_UI_STEM_SECTION:case XZ_UI_SCREEN:case XZ_UI_FOUR_DECKS:case XZ_UI_CONNECT:
 case XZ_UI_EXTRAS:case XZ_UI_STEM_PADS:case XZ_UI_BEAT_JUMP:case XZ_UI_BUTTONS:case XZ_UI_KEY_SETUP:case XZ_UI_SCREEN_LOOK:return 1;
 default:return 0;
 }
}
static void add(struct xz_ui_widget *w, size_t *n, enum xz_ui_action_kind kind, int index, uint32_t requires, const char *label){
 if(*n<XZ_UI_WIDGETS)w[(*n)++]=(struct xz_ui_widget){0,0,0,0,kind,index,requires,label,-1};
}
static int four_needs_you(const struct xz_ui_model *m){
 int s=m->route.state;return s==XZ_UI_ROUTE_NOT_DETECTED||s==XZ_UI_ROUTE_LOST||s==XZ_UI_ROUTE_FAILED;
}
/* The STEMS button's order for each interface's own layouts. */
static const int classic_layouts[5]={XZ_STEM_LAYOUT_BELOW,XZ_STEM_LAYOUT_BELOW_HALF,XZ_STEM_LAYOUT_OUTER,XZ_STEM_LAYOUT_OUTER_HALF,XZ_STEM_LAYOUT_HIDDEN};
int xz_ui_modern_menu(const struct xz_ui_model *m){return m&&m->ui_interface==XZ_UI_INTERFACE_MODERN;}
/* The interface's own stem layouts, in the order its STEMS button steps them; returns how many. */
static int stem_layouts_of(const struct xz_ui_model *m,const int **order){
 if(xz_ui_modern_menu(m)){*order=xz_modern_stems_order;return XZ_MODERN_STEMS_COUNT;}
 *order=classic_layouts;return 5;
}
static const char *stem_layout_label(const struct xz_ui_model *m,int layout){
 static const char *const modern[XZ_MODERN_STEMS_COUNT]={"HIDDEN","BELOW / FULL","BELOW / HALF","OUTER / FULL","OUTER / HALF","BY THE CUES"};
 if(xz_ui_modern_menu(m)&&layout>=0&&layout<XZ_MODERN_STEMS_COUNT)return modern[layout];
 return xz_wave_layout_name(layout);
}
/* MODERN's look rows: one switch per part (on = shown), or the choices of WAVEFORM COLOURS and TRACK COLOURS. */
static const char *const wave_names[XZ_UI_WAVE_COLORS]={"STOCK","RGB","3-BAND","STEMS"};
static const char *const usb_names[3]={"DECK + WAVE","DECK ONLY","OFF"};
static void look_switch(struct xz_ui_widget *w,size_t *n,int item,const char *label){add(w,n,XZ_UI_SHELL_LOOK,item*8,0,label);}
size_t xz_ui_section_controls(const struct xz_ui *u, const struct xz_ui_model *m, struct xz_ui_widget *w, size_t n){
 int i;
 switch(u->page){
 case XZ_UI_HOME:
  for(i=1;i<XZ_UI_SECTIONS;i++)add(w,&n,XZ_UI_FEATURE_LINK,xz_ui_sections[i],0,xz_ui_section_label(xz_ui_sections[i]));
  if(!xz_ui_usb_engine(m))add(w,&n,XZ_UI_HOME_SETUP,XZ_UI_STEM_SECTION,0,"SET UP USB STEMS");
  if(four_needs_you(m))add(w,&n,XZ_UI_HOME_SETUP,XZ_UI_FOUR_DECKS,0,"SET UP FOUR DECKS");
  break;
 case XZ_UI_PLAY:{
  uint32_t available=m->deck[0].ready|m->deck[1].ready|m->enabled;
  add(w,&n,XZ_UI_FEATURE_LINK,XZ_UI_BEAT_JUMP,0,"BEAT JUMP PADS");
  if(available&XZ_UI_GATE)add(w,&n,XZ_UI_ENABLE,XZ_UI_GATE,XZ_UI_GATE,"GATE CUE");else add(w,&n,XZ_UI_FEATURE_SETUP_OPEN,XZ_UI_GATE,0,"SET UP GATE CUE");
  if(available&XZ_UI_SMART)add(w,&n,XZ_UI_ENABLE,XZ_UI_SMART,XZ_UI_SMART,"SMART CUE");else add(w,&n,XZ_UI_FEATURE_SETUP_OPEN,XZ_UI_SMART,0,"SET UP SMART CUE");
  if(xz_ui_has_engine(m,XZ_UI_PREVIEW))add(w,&n,XZ_UI_FEATURE_SETUP_OPEN,XZ_UI_PREVIEW,0,"HOT CUE PREVIEW");
  else add(w,&n,XZ_UI_INFO,XZ_UI_INFO_PREVIEW,0,"HOT CUE PREVIEW");
  add(w,&n,XZ_UI_FEATURE_LINK,XZ_UI_KEY_SETUP,0,"KEYS");
  if(xz_ui_has_engine(m,XZ_UI_SAMPLE))add(w,&n,XZ_UI_FEATURE_LINK,XZ_UI_XPAD,0,"X-PAD SAMPLER");
  else add(w,&n,XZ_UI_INFO,XZ_UI_INFO_XPAD,0,"X-PAD SAMPLER");
  add(w,&n,XZ_UI_FEATURE_LINK,XZ_UI_RAM_SETUP,0,"RAM TRACKS");
  add(w,&n,XZ_UI_FEATURE_LINK,XZ_UI_BUTTONS,0,"BUTTON NEXT TO LINK");
  break;}
 case XZ_UI_STEM_SECTION:
  if(xz_ui_usb_engine(m))add(w,&n,XZ_UI_ENABLE,XZ_UI_STEM,0,"USB STEMS");
  else add(w,&n,XZ_UI_LOCAL_STEM_SETUP_OPEN,XZ_UI_STEM,0,"SET UP USB STEMS");
  add(w,&n,XZ_UI_FEATURE_LINK,XZ_UI_PC_STEMS,0,"STEMS IN DJ SOFTWARE");
  add(w,&n,XZ_UI_FEATURE_LINK,XZ_UI_STEM_PADS,0,"STEM PADS");
  if(xz_ui_has_engine(m,XZ_UI_GROOVE))add(w,&n,XZ_UI_FEATURE_LINK,XZ_UI_STEMS,0,"GROOVE PADS");
  else add(w,&n,XZ_UI_INFO,XZ_UI_INFO_GROOVE,0,"GROOVE PADS");
  {const int *order;int count=stem_layouts_of(m,&order);
   for(i=0;i<count;i++)add(w,&n,XZ_UI_STEMS_OVERLAY,1+order[i],0,stem_layout_label(m,order[i]));}
  add(w,&n,XZ_UI_FEATURE_LINK,XZ_UI_STEM_COLORS,0,"STEM COLOURS");
  if(xz_ui_modern_menu(m))look_switch(w,&n,XZ_UI_LOOK_BYPASS,"BYPASS BUTTON");
  break;
 case XZ_UI_SCREEN:
  add(w,&n,XZ_UI_SET_INTERFACE,XZ_UI_INTERFACE_CLASSIC,0,"CLASSIC");
  add(w,&n,XZ_UI_SET_INTERFACE,XZ_UI_INTERFACE_MODERN,0,"MODERN");
  if(m->route.state!=XZ_UI_ROUTE_NO_ENGINE){
   for(i=0;i<XZ_LAYOUT_COUNT;i++)add(w,&n,XZ_UI_DECK_VIEW,i,0,i?"4 DECKS":"2 DECKS");
  }
  /* MODERN has one look, so no THEME: its slot leads to the MODERN play screen's own parts instead. */
  if(xz_ui_modern_menu(m)){
   add(w,&n,XZ_UI_FEATURE_LINK,XZ_UI_SCREEN_LOOK,0,"LAMPS AND TITLES");
   add(w,&n,XZ_UI_FEATURE_LINK,XZ_UI_BAND_COLORS,0,"3-BAND COLOURS");
   for(i=0;i<XZ_UI_WAVE_COLORS;i++)add(w,&n,XZ_UI_SHELL_LOOK,XZ_UI_LOOK_WAVE_COLOR*8+i,0,wave_names[i]);
  }else{
   add(w,&n,XZ_UI_PANEL,XZ_UI_THEME_PICK,0,"THEME");
   add(w,&n,XZ_UI_FEATURE_LINK,XZ_UI_BAND_COLORS,0,"3-BAND COLOURS");
   add(w,&n,XZ_UI_WAVE_MODE,0,0,"STOCK");add(w,&n,XZ_UI_WAVE_MODE,1,0,"3-BAND");add(w,&n,XZ_UI_WAVE_MODE,2,0,"STEMS");
  }
  add(w,&n,XZ_UI_KEY_FORMAT,0,0,"AS ON THE USB");add(w,&n,XZ_UI_KEY_FORMAT,1,0,"MUSICAL");add(w,&n,XZ_UI_KEY_FORMAT,2,0,"WHEEL");
  add(w,&n,XZ_UI_KEY_FEEDBACK,0,0,"PLAYING KEY");
  add(w,&n,XZ_UI_BROWSE_TRAFFIC,0,0,"KEY MATCH COLOURS");
  add(w,&n,XZ_UI_BROWSE_KEY_PREFIX,0,0,"KEY BEFORE TITLE");
  add(w,&n,XZ_UI_INFO,XZ_UI_INFO_COLUMNS,0,"TRACK LIST COLUMNS");
  add(w,&n,XZ_UI_FEATURE_LINK,XZ_UI_METRICS_SETUP,0,"SYSTEM READOUT");
  break;
 case XZ_UI_SCREEN_LOOK:
  if(!xz_ui_modern_menu(m))break;   /* CLASSIC has none of these parts */
  look_switch(w,&n,XZ_UI_LOOK_MODE_LAMPS,"MODE LAMPS");look_switch(w,&n,XZ_UI_LOOK_QUANTIZE_LAMP,"QUANTIZE LAMP");
  look_switch(w,&n,XZ_UI_LOOK_WAVE_TITLES,"TITLES IN WAVEFORMS");
  for(i=0;i<3;i++)add(w,&n,XZ_UI_SHELL_LOOK,XZ_UI_LOOK_USB_COLORS*8+i,0,usb_names[i]);
  break;
 case XZ_UI_FOUR_DECKS:
  if(m->route.state!=XZ_UI_ROUTE_NO_ENGINE){
   if(m->route.state!=XZ_UI_ROUTE_READY)add(w,&n,XZ_UI_ROUTE_RESCAN,0,0,m->route.state==XZ_UI_ROUTE_FAILED?"TRY AGAIN":"RESCAN");
   for(i=0;i<XZ_LAYOUT_COUNT;i++)add(w,&n,XZ_UI_DECK_VIEW,i,0,i?"4 DECKS":"2 DECKS");
  }
  for(i=0;i<XZ_UI_GRID_ROWS;i++)add(w,&n,XZ_UI_INFO,XZ_UI_INFO_GRID+i,0,xz_ui_grid_name(i));
  break;
 case XZ_UI_CONNECT:
  add(w,&n,XZ_UI_PANEL,XZ_UI_PROFILE_SETUP,0,"DJ PROFILE");
  add(w,&n,XZ_UI_PANEL,XZ_UI_CONNECTION,0,"VJ.TOOLS");
  add(w,&n,XZ_UI_PANEL,XZ_UI_NETWORK,0,"NETWORK");
  add(w,&n,XZ_UI_PANEL,XZ_UI_UPDATES,0,"UPDATES");
  break;
 case XZ_UI_EXTRAS:
  add(w,&n,XZ_UI_PANEL,XZ_UI_GAMES,0,"GAMES");
  add(w,&n,XZ_UI_PANEL,XZ_UI_APPS,0,"USB APPS");
  add(w,&n,XZ_UI_PANEL,XZ_UI_SUPPORT,0,"REPORT AN ISSUE");
  add(w,&n,XZ_UI_PANEL,XZ_UI_ABOUT,0,"ABOUT XZ MODS");
  break;
 case XZ_UI_STEM_PADS:{
  static const char *const modes[4]={"HOT CUE","BEAT LOOP","SLIP LOOP","BEAT JUMP"};
  for(i=0;i<4;i++)add(w,&n,XZ_UI_STEM_PAGE,i,0,modes[i]);
  add(w,&n,XZ_UI_STEM_BANK,0,0,"PADS A-D");add(w,&n,XZ_UI_STEM_BANK,1,0,"PADS E-H");
  add(w,&n,XZ_UI_STEM_ORDER,0,0,"ORDER");
  add(w,&n,XZ_UI_PAD_FEEDBACK,1,0,"STEM COLOURS");add(w,&n,XZ_UI_PAD_FEEDBACK,2,0,"OFF");
  break;}
 case XZ_UI_BEAT_JUMP:
  add(w,&n,XZ_UI_JUMP_EDIT_PAGE,0,0,"PAGE 1");add(w,&n,XZ_UI_JUMP_EDIT_PAGE,1,0,"PAGE 2");
  add(w,&n,XZ_UI_JUMP_ENABLE,2,0,"PAGE 1: STOCK");add(w,&n,XZ_UI_JUMP_ENABLE,1,0,"PAGE 1: MY SIZES");
  for(i=0;i<4;i++)add(w,&n,XZ_UI_JUMP_PAIR,2*i,0,"");
  for(i=0;i<XZ_PIC_JUMP_CHIPS;i++)add(w,&n,XZ_UI_JUMP_SIZE,xz_pic_jump_ids[i],0,xz_jump_label(xz_pic_jump_ids[i]));
  break;
 case XZ_UI_BUTTONS:
  for(int slot=0;slot<2;slot++)for(i=0;i<XZ_UI_RB_CHOICES;i++){
   int choice=xz_ui_rekordbox_order[i];add(w,&n,XZ_UI_REKORDBOX_ASSIGN,slot*XZ_UI_RB_CHOICES+choice,0,xz_ui_rekordbox_choice_name(choice));
  }
  break;
 case XZ_UI_KEY_SETUP:
  add(w,&n,XZ_UI_KEY_SHIFT,-1,XZ_UI_KEY,"KEY DOWN");add(w,&n,XZ_UI_KEY_SHIFT,0,XZ_UI_KEY,"RESET");add(w,&n,XZ_UI_KEY_SHIFT,1,XZ_UI_KEY,"KEY UP");
  add(w,&n,XZ_UI_KEY_SYNC,0,XZ_UI_KEYSYNC,"KEY SYNC");
  break;
 default:break;
 }
 return n;
}

/* ---- CLASSIC geometry ---- */
enum { X0=XZ_UI_CX, CW=XZ_UI_CW, HALF=(XZ_UI_CW-8)/2, RH=40 };
static void put(struct xz_ui_widget *a, int x, int y, int w, int h){a->x=x;a->y=y;a->w=w;a->h=h;}
static void full(struct xz_ui_widget *a, int y){put(a,X0,y,CW,RH);}
static void left(struct xz_ui_widget *a, int y){put(a,X0,y,HALF,RH);}
static void right(struct xz_ui_widget *a, int y){put(a,X0+HALF+8,y,HALF,RH);}
static void segment(struct xz_ui_widget *a, int x, int w, int y, int h, int count, int i){
 int pitch=(w+8)/count;put(a,x+i*pitch,y,pitch-8,h);
}
/* HOME tiles: three across, two down. */
static struct xz_rect home_tile(int section){
 int k=0;for(int i=1;i<XZ_UI_SECTIONS;i++)if((int)xz_ui_sections[i]==section)k=i-1;
 return (struct xz_rect){X0+(k%3)*222,48+(k/3)*180,213,172};
}
static int screen_layout_y(void){return 66;}
void xz_ui_section_place(const struct xz_ui *u, const struct xz_ui_model *m, struct xz_ui_widget *w, size_t n){
 (void)m;
 struct xz_pic_jump_geo jg;struct xz_pic_stem_geo sg;struct xz_pic_grid_geo gg;
 if(u->page==XZ_UI_BEAT_JUMP)xz_pic_jump_geometry(XZ_UI_JUMP_X,XZ_UI_JUMP_Y,XZ_UI_JUMP_W,XZ_UI_JUMP_BOTTOM,&jg);
 if(u->page==XZ_UI_STEM_PADS)xz_pic_stem_geometry(XZ_UI_STEM_X,XZ_UI_STEM_W,&sg);
 if(u->page==XZ_UI_FOUR_DECKS)xz_pic_grid_geometry(X0+4,204,CW-8,36,&gg);
 for(size_t i=0;i<n;i++){
  struct xz_ui_widget *a=&w[i];int k=a->index;
  if(a->kind==XZ_UI_PANEL&&xz_ui_page_is_top(k))continue; /* the rail */
  if(a->kind==XZ_UI_BACK||a->kind==XZ_UI_CLOSE||a->kind==XZ_UI_PROFILE||a->kind==XZ_UI_DECK||a->kind==XZ_UI_PROFILE_PICK||
     a->kind==XZ_UI_PROFILE_PAGE||(a->kind==XZ_UI_PANEL&&k==XZ_UI_PROFILE_SETUP&&u->profile_open))continue;
  switch(u->page){
  case XZ_UI_HOME:
   if(a->kind==XZ_UI_FEATURE_LINK){
    /* A tile that needs the DJ keeps its bottom for the SET UP button: the two never overlap. */
    struct xz_rect r=home_tile(k);int setup=0;
    for(size_t j=0;j<n;j++)setup|=w[j].kind==XZ_UI_HOME_SETUP&&w[j].index==k;
    put(a,r.x,r.y,r.w,setup?r.h-56:r.h);
   }
   else if(a->kind==XZ_UI_HOME_SETUP){struct xz_rect r=home_tile(k);put(a,r.x+10,r.y+r.h-50,r.w-20,40);}
   break;
  case XZ_UI_PLAY:
   if(a->kind==XZ_UI_FEATURE_LINK&&k==XZ_UI_BEAT_JUMP)full(a,64);
   else if(a->kind==XZ_UI_INFO)left(a,k==XZ_UI_INFO_PREVIEW?160:224);
   else if((a->kind==XZ_UI_ENABLE||a->kind==XZ_UI_FEATURE_SETUP_OPEN)&&k==XZ_UI_GATE)left(a,112);
   else if((a->kind==XZ_UI_ENABLE||a->kind==XZ_UI_FEATURE_SETUP_OPEN)&&k==XZ_UI_SMART)right(a,112);
   else if(a->kind==XZ_UI_FEATURE_SETUP_OPEN&&k==XZ_UI_PREVIEW)left(a,160);
   else if(a->kind==XZ_UI_FEATURE_LINK&&k==XZ_UI_KEY_SETUP)right(a,160);
   else if(a->kind==XZ_UI_FEATURE_LINK&&k==XZ_UI_XPAD)left(a,224);
   else if(a->kind==XZ_UI_FEATURE_LINK&&k==XZ_UI_RAM_SETUP)right(a,224);
   else if(a->kind==XZ_UI_FEATURE_LINK&&k==XZ_UI_BUTTONS)full(a,288);
   break;
  case XZ_UI_STEM_SECTION:
   if(a->kind==XZ_UI_ENABLE||a->kind==XZ_UI_LOCAL_STEM_SETUP_OPEN)left(a,64);
   else if(a->kind==XZ_UI_FEATURE_LINK&&k==XZ_UI_PC_STEMS)right(a,64);
   else if(a->kind==XZ_UI_FEATURE_LINK&&k==XZ_UI_STEM_PADS)left(a,128);
   else if((a->kind==XZ_UI_FEATURE_LINK&&k==XZ_UI_STEMS)||(a->kind==XZ_UI_INFO&&k==XZ_UI_INFO_GROOVE))right(a,128);
   else if(a->kind==XZ_UI_STEMS_OVERLAY){
    const int *order;int count=stem_layouts_of(m,&order),at=0;
    for(int j=0;j<count;j++)if(order[j]==k-1)at=j;
    segment(a,X0,CW,192,86,count,at);
   }
   /* MODERN: STEM COLOURS shares its row with the BYPASS BUTTON switch. */
   else if(a->kind==XZ_UI_FEATURE_LINK&&k==XZ_UI_STEM_COLORS){if(xz_ui_modern_menu(m))left(a,296);else full(a,296);}
   else if(a->kind==XZ_UI_SHELL_LOOK)right(a,296);
   break;
  case XZ_UI_SCREEN:{
   int y=screen_layout_y();
   if(a->kind==XZ_UI_SET_INTERFACE)segment(a,X0,CW,y,70,5,k==XZ_UI_INTERFACE_CLASSIC?0:1);
   else if(a->kind==XZ_UI_DECK_VIEW)segment(a,X0,CW,y,70,5,2+k);
   else if((a->kind==XZ_UI_PANEL&&k==XZ_UI_THEME_PICK)||(a->kind==XZ_UI_FEATURE_LINK&&k==XZ_UI_SCREEN_LOOK))left(a,144);
   else if(a->kind==XZ_UI_WAVE_MODE)segment(a,X0,HALF,208,RH,3,k);
   else if(a->kind==XZ_UI_SHELL_LOOK)segment(a,X0,HALF,208,RH,XZ_UI_WAVE_COLORS,k%8);
   else if(a->kind==XZ_UI_KEY_FORMAT){
    /* AS ON THE USB needs the widest box; MUSICAL and WHEEL share the rest with their example key */
    int x=X0+HALF+8,w0=128,w1=(HALF-w0-16)/2;
    put(a,k==0?x:k==1?x+w0+8:x+w0+8+w1+8,208,k==0?w0:w1,RH);
   }
   else if(a->kind==XZ_UI_KEY_FEEDBACK)left(a,272);
   else if(a->kind==XZ_UI_BROWSE_TRAFFIC)right(a,272);
   else if(a->kind==XZ_UI_BROWSE_KEY_PREFIX)left(a,318);
   else if(a->kind==XZ_UI_FEATURE_LINK&&k==XZ_UI_BAND_COLORS)right(a,144);
   else if(a->kind==XZ_UI_FEATURE_LINK&&k==XZ_UI_METRICS_SETUP)full(a,364);
   else if(a->kind==XZ_UI_INFO)right(a,318);
   break;}
  case XZ_UI_SCREEN_LOOK:
   if(a->kind!=XZ_UI_SHELL_LOOK)break;
   switch(k/8){
   case XZ_UI_LOOK_MODE_LAMPS:left(a,64);break;case XZ_UI_LOOK_QUANTIZE_LAMP:right(a,64);break;
   case XZ_UI_LOOK_WAVE_TITLES:left(a,128);break;
   default:segment(a,X0,CW,192,RH,3,k%8);break;
   }
   break;
  case XZ_UI_FOUR_DECKS:
   if(a->kind==XZ_UI_ROUTE_RESCAN)put(a,X0+CW-160,90,156,40);
   else if(a->kind==XZ_UI_DECK_VIEW)segment(a,X0,CW,158,RH,3,k);
   else if(a->kind==XZ_UI_INFO&&k>=XZ_UI_INFO_GRID){struct xz_rect r=gg.rows[k-XZ_UI_INFO_GRID];put(a,r.x,r.y,r.w,r.h);}
   break;
  case XZ_UI_CONNECT:case XZ_UI_EXTRAS:{
   int slot=k==XZ_UI_PROFILE_SETUP||k==XZ_UI_GAMES?0:k==XZ_UI_CONNECTION||k==XZ_UI_APPS?1:k==XZ_UI_NETWORK||k==XZ_UI_SUPPORT?2:3;
   if(a->kind==XZ_UI_PANEL)put(a,slot%2?468:X0,slot/2?226:48,324,170);
   break;}
  case XZ_UI_STEM_PADS:
   if(a->kind==XZ_UI_STEM_PAGE&&k>=0&&k<4){struct xz_rect r=sg.modes[k];put(a,r.x,r.y,r.w,r.h);}
   else if(a->kind==XZ_UI_STEM_BANK&&k>=0&&k<2){struct xz_rect r=sg.bank[k];put(a,r.x,r.y,r.w,r.h);}
   else if(a->kind==XZ_UI_STEM_ORDER)put(a,sg.order[0].x,sg.order[0].y,sg.order[2].x+sg.order[2].w-sg.order[0].x,sg.order[0].h);
   else if(a->kind==XZ_UI_PAD_FEEDBACK&&(k==1||k==2)){struct xz_rect r=sg.lights[k-1];put(a,r.x,r.y,r.w,r.h);}
   break;
  case XZ_UI_BEAT_JUMP:
   if(a->kind==XZ_UI_JUMP_EDIT_PAGE&&(k==0||k==1)){struct xz_rect r=jg.tabs[k];put(a,r.x,r.y,r.w,r.h);}
   else if(a->kind==XZ_UI_JUMP_ENABLE){struct xz_rect r=k==1?jg.mine:jg.stock;put(a,r.x,r.y,r.w,r.h);}
   else if(a->kind==XZ_UI_JUMP_PAIR&&k>=0&&k<8){struct xz_rect r=jg.frames[k/2];put(a,r.x,r.y,r.w,r.h);}
   else if(a->kind==XZ_UI_JUMP_SIZE){for(int j=0;j<XZ_PIC_JUMP_CHIPS;j++)if(xz_pic_jump_ids[j]==k){struct xz_rect r=jg.chips[j];put(a,r.x,r.y,r.w,r.h);}}
   break;
  case XZ_UI_BUTTONS:
   if(a->kind==XZ_UI_REKORDBOX_ASSIGN){
    int slot=k/XZ_UI_RB_CHOICES,choice=k%XZ_UI_RB_CHOICES,at=0;
    for(int j=0;j<XZ_UI_RB_CHOICES;j++)if(xz_ui_rekordbox_order[j]==choice)at=j;
    int y=(slot?240:104)+(at/4)*48,cols=at<4?4:3;
    segment(a,X0,CW,y,RH,cols,at<4?at:at-4);
   }
   break;
  case XZ_UI_KEY_SETUP:
   if(a->kind==XZ_UI_KEY_SHIFT){if(k<0)put(a,X0,200,120,40);else if(!k)put(a,X0+128,200,140,40);else put(a,X0+276,200,120,40);}
   else if(a->kind==XZ_UI_KEY_SYNC)put(a,X0+404,200,CW-404,40);
   break;
  default:break;
  }
 }
}

/* ---- live values and HOME state ---- */
static const char *wave_name(int mode){return mode==2?"STEMS":mode==1?"3-BAND":"STOCK";}
const char *xz_ui_key_example(int format){return format==2?"8A":format==1?"Am":"AS USB";}
static void jump_sizes(const struct xz_ui_model *m, char *out, size_t cap){
 struct xz_jump_settings d=xz_jump_defaults();
 const int *p1=m->jump.enabled?m->jump.sizes:d.sizes;
 snprintf(out,cap,"%d %d %d %d  |  %d %d %d %d",xz_jump_beats(p1[0]),xz_jump_beats(p1[1]),xz_jump_beats(p1[2]),xz_jump_beats(p1[3]),
  xz_jump_beats(m->jump.sizes[4]),xz_jump_beats(m->jump.sizes[5]),xz_jump_beats(m->jump.sizes[6]),xz_jump_beats(m->jump.sizes[7]));
}
const char *xz_ui_section_value(const struct xz_ui_model *m, const struct xz_ui_widget *a, char *buf, size_t cap){
 static const char *const modes[4]={"HOT CUE","BEAT LOOP","SLIP LOOP","BEAT JUMP"};
 if(a->kind==XZ_UI_FEATURE_LINK)switch(a->index){
 case XZ_UI_BEAT_JUMP:jump_sizes(m,buf,cap);return buf;
 case XZ_UI_STEM_PADS:snprintf(buf,cap,"%s, %s",modes[m->stem_page>=0&&m->stem_page<4?m->stem_page:0],m->stem_bank?"E-H":"A-D");return buf;
 case XZ_UI_BUTTONS:{
  const char *a0=xz_ui_rekordbox_choice_name(m->rekordbox_assign[0]),*a1=xz_ui_rekordbox_choice_name(m->rekordbox_assign[1]);
  snprintf(buf,cap,"%s  |  SHIFT: %s",a0?a0:"---",a1?a1:"---");return buf;}
 case XZ_UI_PC_STEMS:{
  int done=m->deck[0].pc_stems.setup_step==XZ_UI_PC_STEP_DONE||m->deck[1].pc_stems.setup_step==XZ_UI_PC_STEP_DONE;
  return done?"READY":"SET UP";}
 case XZ_UI_METRICS_SETUP:return m->metrics.valid?"ON":"SET UP";
 case XZ_UI_SCREEN_LOOK:{
  int hidden=0;for(int i=XZ_UI_LOOK_MODE_LAMPS;i<XZ_UI_LOOK_ITEMS;i++)hidden+=i!=XZ_UI_LOOK_BYPASS&&((m->modern_look.hidden>>i)&1u);
  if(!hidden)return "ALL SHOWN";
  snprintf(buf,cap,"%d HIDDEN",hidden);return buf;}
 case XZ_UI_KEY_SETUP:return NULL;
 default:return NULL;
 }
 if(a->kind==XZ_UI_PANEL&&a->index==XZ_UI_THEME_PICK)return xz_theme_name(m->classic_theme>=0&&m->classic_theme<XZ_THEME_FLAGSHIP?m->classic_theme:0);
 if(a->kind==XZ_UI_INFO)return a->index==XZ_UI_INFO_COLUMNS?NULL:a->index<XZ_UI_INFO_GRID?"NOT IN THIS VERSION":NULL;
 return NULL;
}
const char *xz_ui_section_purpose(int section){
 switch(section){
 case XZ_UI_PLAY:return "Beat jump pads, cue tricks, key sync, the sampler and RAM tracks.";
 case XZ_UI_STEM_SECTION:return "Mute vocals, harmonics and drums from the pads or the screen.";
 case XZ_UI_SCREEN:return "Interface, deck view, waveform colours and how keys are written.";
 case XZ_UI_FOUR_DECKS:return "Play decks 3 and 4 over the loopback cable.";
 case XZ_UI_CONNECT:return "DJ profile, VJ.Tools, network and updates.";
 case XZ_UI_EXTRAS:return "Games, USB apps, report an issue and about.";
 default:return "";
 }
}
static const char *usb_word(enum xz_ui_usb_reason why){
 switch(why){
 case XZ_UI_USB_WHY_NONE:return "ready";case XZ_UI_USB_WHY_LOADING:case XZ_UI_USB_WHY_ALIGNING:return "loading";
 case XZ_UI_USB_WHY_NO_TRACK:return "no track";case XZ_UI_USB_WHY_NOT_PREPARED:return "not prepared";
 case XZ_UI_USB_WHY_ERROR:return "stem set error";default:return "checking";
 }
}
int xz_ui_home_status(const struct xz_ui_model *m, int section, char *out, size_t cap, int *tone){
 *tone=0;out[0]=0;
 switch(section){
 case XZ_UI_PLAY:{char s[48];jump_sizes(m,s,sizeof s);snprintf(out,cap,"Beat jump  %s",s);return 0;}
 case XZ_UI_STEM_SECTION:
  if(!xz_ui_usb_engine(m)){snprintf(out,cap,"Not set up");*tone=2;return 1;}
  if(!(m->enabled&XZ_UI_STEM)){snprintf(out,cap,"Off");return 0;}
  snprintf(out,cap,"On  |  deck 1 %s, deck 2 %s",usb_word(xz_ui_usb_blocked(m,0)),usb_word(xz_ui_usb_blocked(m,1)));*tone=1;return 0;
 case XZ_UI_SCREEN:
  snprintf(out,cap,"%s  |  %s  |  %s keys",m->ui_interface==XZ_UI_INTERFACE_MODERN?"Modern":"Classic",
   m->wave_mode==2?"Stems":m->wave_mode==1?"3-band":"Stock",xz_ui_key_example(m->key_format));return 0;
 case XZ_UI_FOUR_DECKS:
  switch(m->route.state){
  case XZ_UI_ROUTE_READY:snprintf(out,cap,"Ready  |  %s",m->deck_view?"4 decks shown":"2 decks shown");*tone=1;return 0;
  case XZ_UI_ROUTE_NO_ENGINE:snprintf(out,cap,"Not available on this player");return 0;
  case XZ_UI_ROUTE_SCANNING:snprintf(out,cap,"Looking for the cable");return 0;
  case XZ_UI_ROUTE_STARTING:case XZ_UI_ROUTE_WAITING_NATIVE_STOP:snprintf(out,cap,"Starting decks 3 and 4");return 0;
  case XZ_UI_ROUTE_FAILED:snprintf(out,cap,"Could not start");*tone=2;return 1;
  case XZ_UI_ROUTE_LOST:snprintf(out,cap,"Disconnected");*tone=2;return 1;
  default:snprintf(out,cap,"Not connected");*tone=2;return 1;
  }
 case XZ_UI_CONNECT:{
  const char *vj=!m->connection.ready?"VJ.Tools off":m->connection.connected?"VJ.Tools connected":m->connection.enabled?"VJ.Tools waiting":"VJ.Tools off";
  if(m->dj&&m->dj->current>=0&&m->dj->current<m->dj->count){
   const struct xz_ui_dj_entry *e=&m->dj->entry[m->dj->current];
   snprintf(out,cap,"%.20s (USB %d)  |  %s",e->name&&e->name[0]?e->name:"DJ",e->usb+1,vj);
  }else snprintf(out,cap,"No DJ profile  |  %s",vj);
  return 0;}
 case XZ_UI_EXTRAS:{
  unsigned apps=m->sdk.snapshot.count<XZ_SDK_PACKAGES?m->sdk.snapshot.count:XZ_SDK_PACKAGES;
  if(apps)snprintf(out,cap,"6 games  |  %u app%s on USB",apps,apps==1?"":"s");else snprintf(out,cap,"6 games  |  no USB apps");
  return 0;}
 default:return 0;
 }
}
void xz_ui_jump_picture(const struct xz_ui *u, const struct xz_ui_model *m, int focus_pair, struct xz_pic_jump *j){
 memset(j,0,sizeof *j);
 memcpy(j->sizes,m->jump.sizes,sizeof j->sizes);
 j->enabled=m->jump.enabled;j->edit_page=u->jump_edit_page?1:0;
 j->focus_pair=focus_pair>=0?focus_pair:u->jump_edit_pair;j->adjusting=u->adjusting;
}
void xz_ui_stem_picture(const struct xz_ui_model *m, int focus, struct xz_pic_stem_pads *p){
 memset(p,0,sizeof *p);
 p->mode=m->stem_page;p->bank=m->stem_bank;p->order=m->stem_order;p->lights=m->pad_feedback;
 p->four=xz_ui_stem_count(m,0)==4||xz_ui_stem_count(m,1)==4;
 p->jump_wins=m->stem_page==3;p->focus=focus; /* page 2 jumps always win BEAT JUMP's pads */
 for(int i=0;i<4;i++)p->rgb[i]=xz_ui_model_local_stem_color(m,i);
}

/* ---- CLASSIC drawing over the skin ---- */
static void caption(struct xz_canvas c, const struct xz_pic_style *s, int x, int y, int w, const char *text){
 int tw=xz_pic_text(c,x,y,text,12,s->dim);
 if(w>tw+10)xz_rect(c,x+tw+10,y+5,w-tw-10,1,s->row_edge);
}
static void row_value(struct xz_canvas c, const struct xz_pic_style *s, const struct xz_ui_widget *a, const char *value, int link){
 if(!value||!value[0])return;
 int right=a->x+a->w-(link?30:14),max=a->w/2;
 int tw=xz_pic_text_width(c,value,12);if(tw>max)return;
 xz_pic_text(c,right-tw,a->y+(a->h-9)/2,value,12,s->dim);
}
void xz_ui_home_picture(const struct xz_ui_model *m, struct xz_pic_home *h){
 memset(h,0,sizeof *h);
 for(int i=0;i<3;i++)h->stem_rgb[i]=xz_ui_model_stem_color(m,i);
 h->modern=m->ui_interface==XZ_UI_INTERFACE_MODERN;h->decks34=m->route.state==XZ_UI_ROUTE_READY;
 if(m->dj&&m->dj->current>=0&&m->dj->current<m->dj->count){
  const struct xz_ui_dj_entry *e=&m->dj->entry[m->dj->current];
  if(e->has_accent)h->accent=e->accent;
  if(e->name&&e->name[0]>' '&&(unsigned char)e->name[0]<128)h->letter=e->name[0]>='a'&&e->name[0]<='z'?(char)(e->name[0]-32):e->name[0];
 }
}
const char *xz_ui_route_title(int state){
 /* The play screen's card titles (ui/route_card.c): STARTING and WAITING never claim the cable was found,
    because the connector reports them before it checks the cable. */
 static const char *const titles[XZ_UI_ROUTE_STATES]={"FOUR DECKS ARE NOT AVAILABLE","NO CABLE FOUND","LOOKING FOR THE CABLE...",
  "STARTING DECKS 3 AND 4","STOP DECKS 1 AND 2","DECKS 3 AND 4 ARE READY","COULD NOT START DECKS 3 AND 4","DECKS 3 AND 4 DISCONNECTED"};
 return state>=0&&state<XZ_UI_ROUTE_STATES?titles[state]:titles[1];
}
/* Where only the connector knows the cause (scanning, starting, failed, lost, not available) its own sentence
   (model.route.reason) is the line; the fixed lines are the design's, as on the play screen's card. */
const char *xz_ui_route_line(const struct xz_ui_model *m){
 int state=m->route.state;const char *why=m->route.reason[0]?m->route.reason:NULL;
 return state==XZ_UI_ROUTE_READY?"Loopback cable found. Set CH3 and CH4 to USB to hear them. Press SHIFT twice on a side to switch it between decks 1 and 3 or 2 and 4.":
  state==XZ_UI_ROUTE_WAITING_NATIVE_STOP?"Decks 3 and 4 start once decks 1 and 2 are both stopped. This changes by itself once they stop.":
  state==XZ_UI_ROUTE_NOT_DETECTED?"The XZ keeps looking on its own. RESCAN checks right now. Keep the music stick in USB 2.":
  why?why:
  state==XZ_UI_ROUTE_NO_ENGINE?"Decks 3 and 4 are not running on this player.":
  state==XZ_UI_ROUTE_SCANNING?"Checking for the loopback cable now.":
  state==XZ_UI_ROUTE_STARTING?"This takes a few seconds.":
  state==XZ_UI_ROUTE_LOST?"The loopback cable was unplugged or lost power.":
  "Rear USB socket to " XZ_LOOPBACK_PORT " on top. CH3 and CH4 input switches to USB.";
}
static void home(const struct xz_ui_scene *sc, const struct xz_pic_style *s){
 struct xz_canvas c=sc->c;const struct xz_ui_model *m=sc->m;char status[96];
 struct xz_pic_home pic;xz_ui_home_picture(m,&pic);
 for(int i=1;i<XZ_UI_SECTIONS;i++){
  int section=xz_ui_sections[i],tone;struct xz_rect r=home_tile(section);
  int setup=xz_ui_home_status(m,section,status,sizeof status,&tone);
  xz_round_rect(c,r.x,r.y,r.w,r.h,s->radius,15,s->row_edge);xz_round_rect(c,r.x+1,r.y+1,r.w-2,r.h-2,s->radius-1,15,s->row);
  xz_pic_text(c,r.x+12,r.y+12,xz_ui_section_label(section),20,s->ink);
  xz_pic_home_icon(c,s,section,r.x+r.w-86,r.y+12,&pic);
  xz_pic_wrap(c,r.x+12,r.y+64,r.w-24,15,3,xz_ui_section_purpose(section),s->dim);
  uint32_t dot=tone==1?s->good:tone==2?s->warn:s->faint;
  int sy=setup?r.y+r.h-69:r.y+r.h-27;
  xz_disc(c,r.x+16,sy+5,4,dot);
  struct xz_canvas box=xz_sub(c,r.x+26,sy,r.w-36,14);
  xz_pic_text(box,0,0,status,12,tone==2?s->warn:tone==1?s->ink:s->dim);
 }
 for(size_t i=0;i<sc->n;i++){
  const struct xz_ui_widget *a=&sc->w[i];if(a->kind!=XZ_UI_HOME_SETUP)continue;
  xz_round_rect(c,a->x,a->y,a->w,a->h,s->radius,15,s->select);
  xz_pic_text_center(c,a->x+a->w/2,a->y+13,a->label,20,s->on_select);
 }
}
/* The 4 DECKS cable picture: its memory, and its clock's rest. It moves (30 fps steps) until 60 s after the
   page appeared, its focus last moved or the route changed state, then plays on to its main shot and rests. The panel
   draws on one thread at a time. */
static uint16_t four_px[196*74];static uint8_t four_cov[196*74];
static struct xz_pic_route_anim four_anim={.px=four_px,.cov=four_cov,.cap=196*74};
static struct { uint32_t since, drawn; int state, focus, shown; } four_motion;
enum { FOUR_REST_MS=60000 };   /* the play card's rest (route_card.h) */
static void route_card(const struct xz_ui_scene *sc, const struct xz_pic_style *s){
 struct xz_canvas c=sc->c;const struct xz_ui_model *m=sc->m;int state=m->route.state;
 uint32_t accent=state==XZ_UI_ROUTE_READY?s->good:state==XZ_UI_ROUTE_FAILED||state==XZ_UI_ROUTE_LOST?s->alarm:
  state==XZ_UI_ROUTE_SCANNING?s->select:state==XZ_UI_ROUTE_NO_ENGINE?s->faint:s->warn;
 xz_round_rect(c,X0,48,CW,88,s->radius,15,s->row_edge);xz_round_rect(c,X0+1,49,CW-2,86,s->radius-1,15,s->row);
 xz_rect(c,X0+6,49,CW-12,2,accent);
 const char *title=xz_ui_route_title(state);
 int tx=X0+12;
 if(state!=XZ_UI_ROUTE_NO_ENGINE){
  struct xz_ui_widget f;int focus=xz_ui_focused(sc->u,m,&f)?(int)f.kind*64+f.index:-1;uint32_t now=m->clock_ms;
  if(!four_motion.shown||now-four_motion.drawn>500u||state!=four_motion.state||focus!=four_motion.focus)four_motion.since=now;
  four_motion.shown=1;four_motion.drawn=now;four_motion.state=state;four_motion.focus=focus;
  four_anim.now_ms=now;four_anim.step_ms=33u;four_anim.bg=s->row;
  four_anim.moving=state==XZ_UI_ROUTE_SCANNING||now-four_motion.since<(uint32_t)FOUR_REST_MS;
  xz_pic_route(c,s,X0+10,56,196,74,state,m->route.fail_part,600,0,&four_anim);tx=X0+218;
 }
 xz_disc(c,tx+5,66,5,accent);
 xz_pic_text(c,tx+16,59,title,20,state==XZ_UI_ROUTE_READY?s->good:s->ink);
 const char *line=xz_ui_route_line(m);
 int wrap_w=state==XZ_UI_ROUTE_READY||state==XZ_UI_ROUTE_NO_ENGINE?X0+CW-12-tx:X0+CW-176-tx;
 xz_pic_wrap(c,tx,82,wrap_w,14,4,line,s->dim);
}
static void four_decks(const struct xz_ui_scene *sc, const struct xz_pic_style *s){
 struct xz_canvas c=sc->c;const struct xz_ui_model *m=sc->m;
 route_card(sc,s);
 if(m->route.state!=XZ_UI_ROUTE_NO_ENGINE)caption(c,s,X0,142,CW,"DECK VIEW");
 for(size_t i=0;i<sc->n;i++){
  const struct xz_ui_widget *a=&sc->w[i];
  if(a->kind==XZ_UI_ROUTE_RESCAN){
   int on=sc->look[i].available;
   xz_round_rect(c,a->x,a->y,a->w,a->h,s->radius,15,on?s->select:s->row_edge);
   xz_pic_text_center(c,a->x+a->w/2,a->y+13,on?a->label:"SCANNING",20,on?s->on_select:s->dim);
  }
  if(a->kind==XZ_UI_DECK_VIEW){
   int sel=a->index==m->deck_view;
   xz_pic_box(c,s,(struct xz_rect){a->x,a->y,a->w,a->h},sel,0);
   xz_pic_text_center(c,a->x+a->w/2,a->y+13,a->index==0?"2 DECKS":a->index==1?"4  |  1 2 3 4":"4  |  3 1 2 4",20,sel?s->on_select:s->ink);
  }
 }
 caption(c,s,X0,204,180,"WHAT WORKS ON EACH DECK");
 struct xz_pic_grid_geo g;xz_pic_grid_geometry(X0+4,204,CW-8,36,&g);
 unsigned caps[4];for(int d=0;d<4;d++)caps[d]=xz_deck_caps(m,d);
 struct xz_ui_widget f;int focus=-1;
 if(xz_ui_focused(sc->u,m,&f)&&f.kind==XZ_UI_INFO&&f.index>=XZ_UI_INFO_GRID)focus=f.index-XZ_UI_INFO_GRID;
 xz_pic_grid_draw(c,s,&g,caps,focus);
}
static void screen(const struct xz_ui_scene *sc, const struct xz_pic_style *s){
 struct xz_canvas c=sc->c;const struct xz_ui_model *m=sc->m;
 int y=screen_layout_y();
 caption(c,s,X0,y-16,2*(CW+8)/5-8,"INTERFACE");
 if(m->route.state!=XZ_UI_ROUTE_NO_ENGINE)caption(c,s,X0+2*(CW+8)/5,y-16,3*(CW+8)/5-8,"DECK VIEW");
 caption(c,s,X0,192,HALF,"WAVEFORM COLOURS");caption(c,s,X0+HALF+8,192,HALF,"KEY NOTATION");
 caption(c,s,X0,256,CW,"KEYS AND THE TRACK LIST");
 uint32_t rgb[3],band[XZ_BANDS];for(int i=0;i<3;i++){rgb[i]=xz_ui_model_stem_color(m,i);band[i]=xz_ui_model_band_color(m,i);}
 for(size_t i=0;i<sc->n;i++){
  const struct xz_ui_widget *a=&sc->w[i];int sel=sc->look[i].selected;
  if(a->kind==XZ_UI_SET_INTERFACE||a->kind==XZ_UI_DECK_VIEW){
   xz_pic_box(c,s,(struct xz_rect){a->x,a->y,a->w,a->h},0,1);
   if(sel){xz_round_rect(c,a->x,a->y,a->w,a->h,s->radius,15,s->select);xz_round_rect(c,a->x+2,a->y+2,a->w-4,a->h-4,s->radius-1,15,xz_blend(s->row,s->select,40));}
   int view=a->kind==XZ_UI_DECK_VIEW?a->index:0,modern=a->kind==XZ_UI_SET_INTERFACE?a->index==XZ_UI_INTERFACE_MODERN:m->ui_interface==XZ_UI_INTERFACE_MODERN;
   xz_pic_mini_screen(c,a->x+(a->w-84)/2,a->y+5,84,44,view,modern,m->route.state==XZ_UI_ROUTE_READY);
   const char *name=a->kind==XZ_UI_SET_INTERFACE?a->label:a->index==0?"2 DECKS":a->index==1?"4  |  1 2 3 4":"4  |  3 1 2 4";
   xz_pic_text_center(c,a->x+a->w/2,a->y+a->h-16,name,12,sel?s->ink:s->dim);
  }else if(a->kind==XZ_UI_WAVE_MODE){
   xz_pic_wave_swatch(c,a->x+a->w-38,a->y+8,30,a->h-16,a->index,rgb,band);
  }else if(a->kind==XZ_UI_SHELL_LOOK){
   /* MODERN's four tiles are narrow: the name on top, the swatch as a strip under it (scheme 3 is RGB) */
   static const int scheme[XZ_UI_WAVE_COLORS]={0,3,1,2};
   if(a->index/8==XZ_UI_LOOK_WAVE_COLOR)xz_pic_wave_swatch(c,a->x+8,a->y+20,a->w-16,a->h-24,scheme[a->index%8&3],rgb,band);
  }else if(a->kind==XZ_UI_KEY_FORMAT){
   /* the example key, clear of the label and of the selected mark; the dock says it when it does not fit */
   const char *key=xz_ui_key_example(a->index);int right=a->x+a->w-(sel?28:10);
   if(a->index&&a->x+12+xz_pic_text_width(c,a->label,12)+6+xz_pic_text_width(c,key,12)<=right)xz_pic_text_right(c,right,a->y+13,key,12,sel?s->ink:s->dim);
  }
 }
 for(size_t i=0;i<sc->n;i++){
  const struct xz_ui_widget *a=&sc->w[i];char buf[64];
  if(a->kind==XZ_UI_PANEL&&a->index==XZ_UI_THEME_PICK)row_value(c,s,a,xz_ui_section_value(m,a,buf,sizeof buf),1);
  if(a->kind==XZ_UI_FEATURE_LINK)row_value(c,s,a,xz_ui_section_value(m,a,buf,sizeof buf),1);
  /* 3-BAND COLOURS shows its three colours, lows to highs, where other rows write a value */
  if(a->kind==XZ_UI_FEATURE_LINK&&a->index==XZ_UI_BAND_COLORS)
   for(int b=0;b<XZ_BANDS;b++){
    int x=a->x+a->w-30-(XZ_BANDS-b)*20,y=a->y+(a->h-14)/2;
    xz_round_rect(c,x,y,14,14,3,15,s->row_edge);xz_round_rect(c,x+1,y+1,12,12,2,15,band[b]);
   }
 }
 (void)wave_name;
}
static void list_values(const struct xz_ui_scene *sc, const struct xz_pic_style *s){
 for(size_t i=0;i<sc->n;i++){
  const struct xz_ui_widget *a=&sc->w[i];char buf[64];
  if(a->kind==XZ_UI_FEATURE_LINK||a->kind==XZ_UI_INFO)row_value(sc->c,s,a,xz_ui_section_value(sc->m,a,buf,sizeof buf),a->kind!=XZ_UI_INFO);
 }
}
static void stem_layouts(const struct xz_ui_scene *sc, const struct xz_pic_style *s){
 uint32_t rgb[3];for(int i=0;i<3;i++)rgb[i]=xz_ui_model_stem_color(sc->m,i);
 for(size_t i=0;i<sc->n;i++){
  const struct xz_ui_widget *a=&sc->w[i];if(a->kind!=XZ_UI_STEMS_OVERLAY)continue;
  int sel=sc->look[i].selected;
  if(sel){xz_round_rect(sc->c,a->x,a->y,a->w,a->h,s->radius,15,s->select);xz_round_rect(sc->c,a->x+2,a->y+2,a->w-4,a->h-4,s->radius-1,15,xz_blend(s->row,s->select,40));}
  else xz_pic_box(sc->c,s,(struct xz_rect){a->x,a->y,a->w,a->h},0,1);
  xz_pic_stem_layout(sc->c,a->x+6,a->y+6,a->w-12,a->h-28,a->index-1,rgb);
  xz_pic_text_center(sc->c,a->x+a->w/2,a->y+a->h-17,a->label,12,sel?s->ink:s->dim);
 }
}
void xz_ui_section_render(const struct xz_ui_scene *sc){
 const struct xz_ui *u=sc->u;const struct xz_ui_model *m=sc->m;struct xz_canvas c=sc->c;
 struct xz_pic_style s=xz_pic_style_classic(m->theme);
 switch(u->page){
 case XZ_UI_HOME:home(sc,&s);break;
 case XZ_UI_PLAY:
  caption(c,&s,X0,48,CW,"PADS AND CUES");caption(c,&s,X0,208,CW,"SAMPLER AND MEMORY");caption(c,&s,X0,272,CW,"BUTTONS");
  list_values(sc,&s);break;
 case XZ_UI_STEM_SECTION:
  caption(c,&s,X0,48,CW,"STEM AUDIO");caption(c,&s,X0,112,CW,"PADS");caption(c,&s,X0,176,CW,"STEM BUTTONS ON SCREEN");
  list_values(sc,&s);stem_layouts(sc,&s);break;
 case XZ_UI_SCREEN:screen(sc,&s);break;
 case XZ_UI_FOUR_DECKS:four_decks(sc,&s);break;
 case XZ_UI_STEM_PADS:{
  struct xz_pic_stem_geo g;xz_pic_stem_geometry(XZ_UI_STEM_X,XZ_UI_STEM_W,&g);
  struct xz_ui_widget f;int focus=-1;
  if(xz_ui_focused(u,m,&f))focus=f.kind==XZ_UI_STEM_PAGE?XZ_PIC_STEM_MODE:f.kind==XZ_UI_STEM_BANK?XZ_PIC_STEM_BANK:f.kind==XZ_UI_STEM_ORDER?XZ_PIC_STEM_ORDER:f.kind==XZ_UI_PAD_FEEDBACK?XZ_PIC_STEM_LIGHTS:-1;
  struct xz_pic_stem_pads p;xz_ui_stem_picture(m,u->adjusting?focus:-1,&p);
  xz_rect(c,X0,XZ_UI_CY,CW,XZ_UI_CB-XZ_UI_CY,s.bg);
  xz_pic_stem_draw(c,&s,&g,&p);break;}
 case XZ_UI_BEAT_JUMP:{
  struct xz_pic_jump_geo g;xz_pic_jump_geometry(XZ_UI_JUMP_X,XZ_UI_JUMP_Y,XZ_UI_JUMP_W,XZ_UI_JUMP_BOTTOM,&g);
  struct xz_pic_jump j;xz_ui_jump_picture(u,m,-1,&j);
  xz_rect(c,X0,XZ_UI_CY,CW,XZ_UI_CB-XZ_UI_CY,s.bg);
  xz_pic_jump_draw(c,&s,&g,&j);break;}
 case XZ_UI_BUTTONS:{
  xz_rect(c,X0,XZ_UI_CY,CW,XZ_UI_DOCK_Y-8-XZ_UI_CY,s.bg);
  xz_pic_media_buttons(c,&s,X0,50,260,32);
  xz_pic_wrap(c,X0+276,52,CW-276,15,2,"The media button next to LINK: what it does on a press, and while SHIFT is held on either deck.",s.dim);
  caption(c,&s,X0,88,CW,"PRESS");caption(c,&s,X0,224,CW,"SHIFT + PRESS");
  for(size_t i=0;i<sc->n;i++){
   const struct xz_ui_widget *a=&sc->w[i];if(a->kind!=XZ_UI_REKORDBOX_ASSIGN)continue;
   int sel=sc->look[i].selected;xz_pic_box(c,&s,(struct xz_rect){a->x,a->y,a->w,a->h},sel,1);
   xz_pic_text_center(c,a->x+a->w/2,a->y+13,a->label,a->w>=150?20:12,sel?s.on_select:s.ink);
  }
  for(int slot=0;slot<2;slot++){
   const char *help=xz_ui_rekordbox_choice_help(m->rekordbox_assign[slot]);
   xz_pic_wrap(c,X0,slot?336:200,CW,14,1,help?help:"",s.ink);
  }
  break;}
 case XZ_UI_KEY_SETUP:{
  xz_pic_text(c,X0,58,"KEYS",20,s.ink);
  xz_pic_wrap(c,X0,86,CW,15,3,"Shift the chosen deck's key a semitone at a time, put it back with RESET, or match it to the master deck with KEY SYNC. SHIFT + SYNC on a deck does the same match.",s.dim);
  xz_pic_wrap(c,X0,140,CW,15,2,"With MASTER TEMPO on, the key holds while the tempo moves; without it, the tempo changes the key too.",s.dim);
  break;}
 case XZ_UI_SCREEN_LOOK:
  caption(c,&s,X0,48,CW,"TOP BAR");caption(c,&s,X0,112,CW,"WAVEFORMS");caption(c,&s,X0,176,CW,"TRACK COLOURS");
  xz_pic_wrap(c,X0,244,CW,15,2,"What the MODERN play screen shows. A hidden lamp also stops answering touch.",s.dim);
  break;
 case XZ_UI_CONNECT:case XZ_UI_EXTRAS:break;
 default:break;
 }
}
