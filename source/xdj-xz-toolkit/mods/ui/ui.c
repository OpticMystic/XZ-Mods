#include "ui_skin.h"
#include "ui_about.h"
#include "../key/display.h"
#include "stem_pads.h"
#include "wave_viewport.h"
#include "ui_network.h"
#include "ui_support.h"
#include "ui_sections.h"
#include <stdio.h>
#include <string.h>

static const char *stems[3]={"DRUMS","HARMONICS","VOCALS"};
/* Four-stem rows: harmonics without bass reads INST, same column and colour. */
static const char *stems4[4]={"DRUMS","INST","VOCALS","BASS"};
static const char *key_formats[3]={"AS XZ SETTING","CLASSIC","CAMELOT"};
static const char *pads[8]={"A","B","C","D","E","F","G","H"};
static const char *pc_roles[XZ_UI_PC_ROLES]={"DRUMS","INST","BASS","VOCAL"};
static const char *pc_targets[4]={"PC1","PC2","PC3","PC4"};
/* PC row slots left to right; vocals lead and rhythm follows, like the local row. */
static const int pc_slots[XZ_UI_PC_ROLES]={XZ_UI_PC_VOCAL,XZ_UI_PC_INST,XZ_UI_PC_BASS,XZ_UI_PC_DRUMS};
static const enum xz_ui_page section_pages[4]={XZ_UI_CONTROLS,XZ_UI_BEAT_JUMP,XZ_UI_PC_STEMS,XZ_UI_BUTTONS};
/* The button above LINK. Stored numbers are settings.h enum xz_rekordbox_action; MODS lists them in
   xz_ui_rekordbox_order. The code keeps the name REKORDBOX; the screen never says it. */
static const char *const rekordbox_names[XZ_UI_RB_CHOICES]={"MODS MENU","ITS OWN JOB","INTERFACE","VJ.TOOLS SCREEN","STEMS","NOTHING","DECK VIEW"};
static const char *const rekordbox_help[XZ_UI_RB_CHOICES]={
 "Opens and closes MODS. Works with the encoder and BACK.",
 "What the button does without MODS: the screen for playing a computer's library.",
 "Switches between the MODERN and CLASSIC interfaces.",
 "Switches to the VJ.Tools screen and back. Needs the VJ.Tools link.",
 "Steps the stem buttons on the play screen through their places.",
 "The button does nothing.",
 "Steps DECK VIEW: 2 decks, 4 decks in number order, 4 decks in mixer order."};
const int xz_ui_rekordbox_order[XZ_UI_RB_CHOICES]={XZ_UI_RB_MODS,XZ_UI_RB_STOCK,XZ_UI_RB_INTERFACE,XZ_UI_RB_VJ_SCREEN,XZ_UI_RB_STEMS,XZ_UI_RB_DECK_VIEW,XZ_UI_RB_NOTHING};
const char *xz_ui_rekordbox_choice_name(int choice){return choice>=0&&choice<XZ_UI_RB_CHOICES?rekordbox_names[choice]:NULL;}
const char *xz_ui_rekordbox_choice_help(int choice){return choice>=0&&choice<XZ_UI_RB_CHOICES?rekordbox_help[choice]:NULL;}
const char *const xz_ui_lengths[6]={"1/16","1/8","1/4","1/2","1","2"};
static float clampf(float v,float lo,float hi){return v<lo?lo:v>hi?hi:v;}
static const struct xz_ui_skin *skin_for(int theme){
 return theme==XZ_THEME_LCARS?&xz_skin_lcars:theme==XZ_THEME_MATRIX?&xz_skin_matrix:theme==XZ_THEME_ANALOG?&xz_skin_analog:
  theme==XZ_THEME_FLAGSHIP?&xz_skin_flagship:&xz_skin_classic;
}
struct xz_top_face xz_ui_top_button(struct xz_canvas c,int x,int w,const char *label,int lit){
 return skin_for(c.theme)->top_button(c,x,w,label,lit);
}
void xz_ui_render_native_buttons(uint16_t *pixels,size_t stride,int theme,int stems,int vj_visible,int vj_active){
 if(!pixels||stride<800)return;
 struct xz_canvas c=xz_top_bar_canvas(pixels,stride,theme);
 xz_ui_top_button(c,XZ_TOP_MODS_X,XZ_TOP_MODS_W,"MODS",0);
 xz_top_bar_marks(c,xz_ui_top_button(c,XZ_TOP_STEMS_X,XZ_TOP_STEMS_W,"STEMS",stems!=XZ_STEM_LAYOUT_HIDDEN),stems);
 if(vj_visible)xz_ui_top_button(c,XZ_TOP_VJ_X,XZ_TOP_VJ_W,vj_active?"EXIT VJ":"VJ.TOOLS",vj_active);
}
void xz_ui_render_ram_debug(uint16_t *pixels,size_t stride,int theme,int enabled,int valid,uint32_t free_mib){
 if(!enabled||!pixels||stride<800)return;
 struct xz_canvas c=xz_top_bar_canvas(pixels,stride,theme);const struct xz_theme_palette *p=xz_theme_palette(theme);
 char text[32];
 xz_rect(c,488,0,104,XZ_TOP_BAR_H,xz_blend(p->bg,p->ink,20));xz_border(c,488,0,104,XZ_TOP_BAR_H,xz_blend(p->bg,p->accent,150));
 if(valid)snprintf(text,sizeof(text),"FREE %u MiB",free_mib);else snprintf(text,sizeof(text),"FREE --");
 xz_text(c,494,xz_theme_pixel_text(theme)?XZ_TOP_BAR_PIXEL_Y:XZ_TOP_BAR_LABEL_Y,text,1,15,p->ink);
}
const char *xz_ui_theme_name(int theme){return xz_theme_name(theme);}
uint32_t xz_ui_stem_color(int theme,int index){
 return xz_theme_palette(theme)->stem[index>=0&&index<3?index:0];
}
uint32_t xz_ui_model_stem_color(const struct xz_ui_model *m,int index){
 int stem=index>=0&&index<3?index:0;
 return xz_stem_palette_color(m->stem_colors[stem],xz_ui_stem_color(m->theme,stem));
}
/* STANDARD is the interface's own band colour: MODERN's flagship palette, CLASSIC's layered_wave.c palette. */
uint32_t xz_ui_band_standard(const struct xz_ui_model *m,int band){
 static const uint32_t modern[XZ_BANDS]={XZ_FLAGSHIP_WAVE_LOW,XZ_FLAGSHIP_WAVE_MID,XZ_FLAGSHIP_WAVE_HIGH},classic[XZ_BANDS]={0x2053d9,0xf2aa3c,0xffffff};
 return (m->ui_interface==XZ_UI_INTERFACE_MODERN?modern:classic)[band>=0&&band<XZ_BANDS?band:0];
}
uint32_t xz_ui_model_band_color(const struct xz_ui_model *m,int band){
 int b=band>=0&&band<XZ_BANDS?band:0;
 return xz_band_palette_color(m->band_colors[b],xz_ui_band_standard(m,b));
}
static unsigned distance(uint32_t a,uint32_t b){
 int r=(int)(a>>16&255)-(int)(b>>16&255),g=(int)(a>>8&255)-(int)(b>>8&255),l=(int)(a&255)-(int)(b&255);
 return (unsigned)(r*r+g*g+l*l);
}
static uint32_t pc_palette_role_color(const struct xz_theme_palette *p,int role){
 if(role==XZ_UI_PC_INST)return p->stem[1];
 if(role==XZ_UI_PC_VOCAL)return p->stem[2];
 if(role!=XZ_UI_PC_BASS)return p->stem[0];
 /* Themes carry three stem colours. Bass takes the first existing theme colour
    that stays clear of the other three roles and the background, else the
    clearest one. Monochrome themes still separate roles by label. */
 const uint32_t candidates[3]={p->alarm,xz_blend(p->stem[0],p->stem[2],128),p->accent};
 const uint32_t others[4]={p->stem[0],p->stem[1],p->stem[2],p->bg};
 uint32_t best=candidates[0];unsigned best_gap=0;
 for(int i=0;i<3;i++){
  unsigned gap=~0u;
  for(int k=0;k<4;k++){unsigned d=distance(candidates[i],others[k]);if(d<gap)gap=d;}
  if(gap>=90u*90u)return candidates[i];
  if(gap>best_gap){best_gap=gap;best=candidates[i];}
 }
 return best;
}
uint32_t xz_ui_pc_role_color(int theme,int role){
 return pc_palette_role_color(xz_theme_palette(theme),role);
}
uint32_t xz_ui_model_pc_role_color(const struct xz_ui_model *m,int role){
 struct xz_theme_palette palette=*xz_theme_palette(m->theme);
 for(int stem=0;stem<3;stem++)palette.stem[stem]=xz_ui_model_stem_color(m,stem);
 return pc_palette_role_color(&palette,role);
}
uint32_t xz_ui_model_local_stem_color(const struct xz_ui_model *m,int role){
 return role==XZ_STEM_BASS?xz_ui_model_pc_role_color(m,XZ_UI_PC_BASS):xz_ui_model_stem_color(m,role);
}
int xz_ui_stem_count(const struct xz_ui_model *m,int deck){
 return m&&deck>=0&&deck<2?xz_stem_count(m->deck[deck].stem_count):3;
}
enum xz_ui_pc_reason xz_ui_pc_blocked(const struct xz_ui_model *m,int deck){
 if(!m||deck<0||deck>1)return XZ_UI_PC_WHY_SOURCE;
 const struct xz_ui_pc_stems *pc=&m->deck[deck].pc_stems;
 if(pc->source!=XZ_UI_SOURCE_PC)return XZ_UI_PC_WHY_SOURCE;
 if(!pc->connected)return XZ_UI_PC_WHY_LINK;
 if(!pc->verified||!(pc->caps&XZ_UI_PC_CAP_TOGGLE))return XZ_UI_PC_WHY_UNVERIFIED;
 return XZ_UI_PC_WHY_NONE;
}
const char *xz_ui_pc_reason_text(enum xz_ui_pc_reason why){
 switch(why){
 case XZ_UI_PC_WHY_NONE:return "PC STEMS READY";
 case XZ_UI_PC_WHY_SOURCE:return "SOURCE NOT CONFIRMED / TAP TO SET UP";
 case XZ_UI_PC_WHY_LINK:return "PC LINK LOST / NOTHING SENT";
 case XZ_UI_PC_WHY_UNVERIFIED:return "NOT TESTED YET / TAP TO SET UP";
 case XZ_UI_PC_WHY_NO_LEVEL:return "PC STEMS SWITCH ON/OFF ONLY / NO LEVEL";
 case XZ_UI_PC_WHY_NO_BYPASS:return "THE DJ SOFTWARE HAS NO STEM BYPASS OVER USB";
 case XZ_UI_PC_WHY_ROLE:return "STEM COUNT NOT REPORTED YET / TAP TO SET UP";
 case XZ_UI_PC_WHY_TAG:break;
 }
 return "PC STEMS SETUP";
}
int xz_ui_usb_engine(const struct xz_ui_model *m){
 if(!m)return 0;
 if(m->enabled&XZ_UI_STEM)return 1;
 for(int deck=0;deck<2;deck++){
  enum xz_ui_usb_reason why=m->deck[deck].usb_reason;
  if(why!=XZ_UI_USB_WHY_UNKNOWN&&why!=XZ_UI_USB_WHY_ENGINE&&why<=XZ_UI_USB_WHY_ERROR)return 1;
  if(m->deck[deck].ready&XZ_UI_STEM)return 1;
 }
 return 0;
}
enum xz_ui_usb_reason xz_ui_usb_blocked(const struct xz_ui_model *m,int deck){
 if(!m||deck<0||deck>1)return XZ_UI_USB_WHY_ENGINE;
 const struct xz_ui_deck *d=&m->deck[deck];
 if(d->usb_reason>XZ_UI_USB_WHY_UNKNOWN&&d->usb_reason<=XZ_UI_USB_WHY_ERROR)return d->usb_reason;
 /* Host did not say: derive it from the fields every runtime already fills. */
 if(!xz_ui_usb_engine(m))return XZ_UI_USB_WHY_ENGINE;
 if(!(m->enabled&XZ_UI_STEM))return XZ_UI_USB_WHY_OFF;
 if(d->stem_error&&*d->stem_error)return XZ_UI_USB_WHY_ERROR;
 if(d->ready&XZ_UI_STEM)return d->stem_loading?XZ_UI_USB_WHY_LOADING:XZ_UI_USB_WHY_NONE;
 if(!d->track||!*d->track)return XZ_UI_USB_WHY_NO_TRACK;
 return d->stem_loading?XZ_UI_USB_WHY_LOADING:XZ_UI_USB_WHY_NOT_PREPARED;
}
const char *xz_ui_usb_reason_text(enum xz_ui_usb_reason why){
 switch(why){
 case XZ_UI_USB_WHY_NONE:return "STEMS READY";
 case XZ_UI_USB_WHY_ENGINE:return "NO STEM ENGINE";
 case XZ_UI_USB_WHY_OFF:return "STEMS OFF";
 case XZ_UI_USB_WHY_NO_TRACK:return "NO TRACK LOADED";
 case XZ_UI_USB_WHY_NOT_PREPARED:return "NOT PREPARED";
 case XZ_UI_USB_WHY_LOADING:return "LOADING STEMS";
 case XZ_UI_USB_WHY_ALIGNING:return "PLAY TO ALIGN";
 case XZ_UI_USB_WHY_ERROR:return "STEM SET ERROR";
 case XZ_UI_USB_WHY_UNKNOWN:break;
 }
 return "CHECKING STEMS";
}
const char *xz_ui_usb_action_label(enum xz_ui_usb_reason why){
 switch(why){
 case XZ_UI_USB_WHY_ENGINE:return "SET UP";
 case XZ_UI_USB_WHY_OFF:return "TURN ON";
 case XZ_UI_USB_WHY_NOT_PREPARED:case XZ_UI_USB_WHY_ERROR:return "HOW TO FIX";
 default:return NULL;
 }
}
const char *xz_ui_usb_next_step(enum xz_ui_usb_reason why){
 switch(why){
 case XZ_UI_USB_WHY_NONE:return "STANDALONE STEMS READY ON THIS DECK.";
 case XZ_UI_USB_WHY_ENGINE:return "UPDATE THE USB LOADER WITH XZ MODS ON YOUR COMPUTER, THEN RESTART THE PLAYER.";
 case XZ_UI_USB_WHY_OFF:return "TURN ON USB STEMS FOR NATIVE DECKS 1 AND 2.";
 case XZ_UI_USB_WHY_NO_TRACK:return "LOAD A TRACK FROM USB ON THIS DECK.";
 case XZ_UI_USB_WHY_NOT_PREPARED:return "PREPARE THIS TRACK IN XZ MODS ON YOUR COMPUTER, THEN LOAD IT AGAIN.";
 case XZ_UI_USB_WHY_LOADING:return "STEMS ARE LOADING FROM USB. WAIT A MOMENT.";
 case XZ_UI_USB_WHY_ALIGNING:return "PRESS PLAY. STEMS LOCK TO THE TRACK A FEW SECONDS IN.";
 case XZ_UI_USB_WHY_ERROR:return "VERIFY THIS TRACK IN XZ MODS ON YOUR COMPUTER.";
 case XZ_UI_USB_WHY_UNKNOWN:break;
 }
 return "CHECKING THIS DECK. WAIT A MOMENT.";
}
/* Stem audio flows on this deck, so mutes and bypass act; aligning still plays. */
static int usb_playable(const struct xz_ui_model *m,int deck){
 enum xz_ui_usb_reason why=xz_ui_usb_blocked(m,deck);
 return (m->enabled&XZ_UI_STEM)&&(m->deck[deck].ready&XZ_UI_STEM)&&
  (why==XZ_UI_USB_WHY_NONE||why==XZ_UI_USB_WHY_LOADING||why==XZ_UI_USB_WHY_ALIGNING);
}
/* The runtime reason replaces the built-in text only for a locked deck. */
static const char *pc_reason(const struct xz_ui_model *m,int deck,enum xz_ui_pc_reason why){
 const char *own=deck>=0&&deck<2?m->deck[deck].pc_stems.reason:NULL;
 return why>=XZ_UI_PC_WHY_SOURCE&&why<=XZ_UI_PC_WHY_UNVERIFIED&&own?own:xz_ui_pc_reason_text(why);
}
static int pc_role_available(const struct xz_ui_model *m,int deck,int role){
 return deck>=0&&deck<2&&role>=0&&role<XZ_UI_PC_ROLES&&
  (m->deck[deck].pc_stems.available&(1u<<role));
}
/* Inside the setup page "tap to set up" is noise; say where the deck stands. */
static const char *pc_page_status(const struct xz_ui_model *m,int deck){
 enum xz_ui_pc_reason why=xz_ui_pc_blocked(m,deck);
 if(why!=XZ_UI_PC_WHY_NONE&&m->deck[deck].pc_stems.reason)return m->deck[deck].pc_stems.reason;
 return why==XZ_UI_PC_WHY_SOURCE?"NO PC DECK DETECTED YET":why==XZ_UI_PC_WHY_UNVERIFIED?"STEM SWITCHING NOT TESTED YET":xz_ui_pc_reason_text(why);
}
static int pc_deck(const struct xz_ui *u){return u->deck>=0&&u->deck<2?u->deck:0;}
void xz_ui_init(struct xz_ui *u){memset(u,0,sizeof(*u));u->capture=-1;u->page=XZ_UI_HOME;u->profile_view=-1;u->edit_entry=-1;u->edit_logo=XZ_UI_LOGO_KEEP;}
static void add(struct xz_ui_widget *w,size_t *n,enum xz_ui_action_kind kind,int index,uint32_t cap,const char *label){
 w[*n]=(struct xz_ui_widget){0,0,0,0,kind,index,cap,label,-1};(*n)++;
}
static struct xz_ui_action action(enum xz_ui_action_kind,enum xz_ui_phase,int,int,float,float);
static int root_page(enum xz_ui_page page){
 for(int i=0;i<XZ_UI_SECTIONS;i++)if(xz_ui_sections[i]==page)return 1;
 return 0;
}
void xz_ui_navigate(struct xz_ui *u,enum xz_ui_page page){
 xz_ui_network_invalidate(u);
 page=xz_ui_page_current(page);
 if(!u||(unsigned)page>=XZ_UI_PAGE_COUNT||u->page==page)return;
 if(root_page(page))u->history_count=0;
 else{
  /* DONE / CANCEL may navigate to an ancestor already in the menu trail.
     Returning there removes the editor instead of making BACK reopen it. */
  int ancestor=-1;
  for(unsigned i=0;i<u->history_count;i++)if(u->history[i]==page){ancestor=(int)i;break;}
  if(ancestor>=0)u->history_count=(unsigned)ancestor;
  else{
   if(u->history_count==16){memmove(u->history,u->history+1,15*sizeof(*u->history));u->history_count=15;}
   u->history[u->history_count++]=u->page;
  }
 }
 u->page=page;u->capture=-1;u->notice[0]=0;u->focus_kind=XZ_UI_NONE;u->adjusting=0;u->profile_open=0;
}
int xz_ui_header_decks(enum xz_ui_page page){
 return page==XZ_UI_STEMS||page==XZ_UI_XPAD||page==XZ_UI_KEY_SETUP||page==XZ_UI_LOCAL_STEM_SETUP||page==XZ_UI_FEATURE_SETUP;
}
int xz_ui_profile_window(const struct xz_ui *u,const struct xz_ui_model *m,int *first){
 int count=m&&m->dj?m->dj->count:0,offset=u?u->profile_offset:0;
 if(count>XZ_UI_DJ_ENTRIES)count=XZ_UI_DJ_ENTRIES;
 if(offset>count-XZ_UI_PROFILE_ROWS)offset=count-XZ_UI_PROFILE_ROWS;
 if(offset<0)offset=0;
 if(first)*first=offset;
 return count-offset<XZ_UI_PROFILE_ROWS?count-offset:XZ_UI_PROFILE_ROWS;
}
/* The open list replaces the page's controls: the chip, one row per profile,
   paging when there are more than fit, and the way into setup. */
static size_t profile_controls(const struct xz_ui *u,const struct xz_ui_model *m,struct xz_ui_widget w[XZ_UI_WIDGETS]){
 size_t n=0;int first,rows=xz_ui_profile_window(u,m,&first);
 add(w,&n,XZ_UI_CLOSE,0,0,"CLOSE");
 add(w,&n,XZ_UI_PROFILE,0,0,"DJ PROFILE");
 for(int i=0;i<rows;i++)add(w,&n,XZ_UI_PROFILE_PICK,first+i,0,m->dj->entry[first+i].name?m->dj->entry[first+i].name:"");
 if(m->dj->count>XZ_UI_PROFILE_ROWS){add(w,&n,XZ_UI_PROFILE_PAGE,-1,0,"PREV");add(w,&n,XZ_UI_PROFILE_PAGE,1,0,"NEXT");}
 add(w,&n,XZ_UI_PANEL,XZ_UI_PROFILE_SETUP,0,m->dj->count?"OPEN DJ PROFILE":"SET UP DJ PROFILE");
 return n;
}
int xz_ui_page_is_top(int page){return root_page((enum xz_ui_page)page);}
static enum xz_ui_page parent_page(enum xz_ui_page page);
int xz_ui_page_parent(int page){return (unsigned)page<XZ_UI_PAGE_COUNT?(int)parent_page((enum xz_ui_page)page):page;}
static enum xz_ui_page parent_page(enum xz_ui_page page){
 switch(page){
 case XZ_UI_BEAT_JUMP:case XZ_UI_KEY_SETUP:case XZ_UI_XPAD:case XZ_UI_RAM_SETUP:case XZ_UI_BUTTONS:case XZ_UI_FEATURE_SETUP:return XZ_UI_PLAY;
 case XZ_UI_STEMS:case XZ_UI_PC_STEMS:case XZ_UI_LOCAL_STEM_SETUP:case XZ_UI_STEM_COLORS:case XZ_UI_STEM_PADS:return XZ_UI_STEM_SECTION;
 case XZ_UI_THEME_PICK:case XZ_UI_BROWSE_SETUP:case XZ_UI_METRICS_SETUP:case XZ_UI_SCREEN_LOOK:case XZ_UI_BAND_COLORS:return XZ_UI_SCREEN;
 case XZ_UI_PROFILE_SETUP:case XZ_UI_CONNECTION:case XZ_UI_NETWORK:case XZ_UI_UPDATES:return XZ_UI_CONNECT;
 case XZ_UI_PROFILE_EDIT:return XZ_UI_PROFILE_SETUP;
 case XZ_UI_NETWORK_SETUP:return XZ_UI_NETWORK;
 case XZ_UI_NETWORK_EDIT:return XZ_UI_NETWORK_SETUP;
 case XZ_UI_GAMES:case XZ_UI_APPS:case XZ_UI_ABOUT:case XZ_UI_SUPPORT:return XZ_UI_EXTRAS;
 case XZ_UI_UPDATE_SETUP:return XZ_UI_UPDATES;
 case XZ_UI_GENERAL:case XZ_UI_CONTROLS:case XZ_UI_THEMES:case XZ_UI_SETTINGS:return XZ_UI_HOME;
 case XZ_UI_APPS_SETUP:return XZ_UI_APPS;
 case XZ_UI_DOOM:case XZ_UI_DOOM_FILES:case XZ_UI_MYHOUSE_SETUP:return XZ_UI_GAMES;
 case XZ_UI_DOOM_SETUP:return XZ_UI_DOOM;
 default:return page;
 }
}
size_t xz_ui_back(struct xz_ui *u,struct xz_ui_action out[XZ_UI_ACTIONS]){
 xz_ui_network_invalidate(u);
 /* BACK first leaves an encoder value edit and stays on the page. */
 if(u->adjusting){u->adjusting=0;return 0;}
 /* Then it closes the DJ Profile list and returns focus to its chip. */
 if(u->profile_open){
  size_t n=xz_ui_cancel(u,out);u->profile_open=0;
  u->focus_kind=XZ_UI_PROFILE;u->focus_index=0;u->focus_deck=-1;return n;
 }
 /* In the profile editor and REPORT AN ISSUE BACK goes one step back before it leaves. */
 if(xz_ui_profile_back(u)||xz_ui_support_back(u))return xz_ui_cancel(u,out);
 enum xz_ui_page old=u->page,target=old;size_t n=xz_ui_cancel(u,out);
 if(u->history_count)target=u->history[--u->history_count];else if(!root_page(old))target=parent_page(old);
 u->notice[0]=0;
 if(target==old)out[n++]=action(XZ_UI_CLOSE,XZ_UI_PRESS,u->deck,0,0,0);
 else{
  u->page=target;out[n++]=action(XZ_UI_PANEL,XZ_UI_PRESS,u->deck,(int)old,(float)target,0);
  /* Land on the control that led to the page just left, so BACK then PUSH re-enters it: the section lists and
     HOME link with FEATURE_LINK, the card pages (CONNECT, EXTRAS, GAMES...) with PANEL. */
  int listed=target==XZ_UI_HOME||target==XZ_UI_PLAY||target==XZ_UI_STEM_SECTION||target==XZ_UI_SCREEN;
  u->focus_kind=listed?XZ_UI_FEATURE_LINK:XZ_UI_PANEL;
  u->focus_index=(int)old;u->focus_deck=-1;u->adjusting=0;
  if(old==XZ_UI_FEATURE_SETUP){u->focus_kind=XZ_UI_FEATURE_SETUP_OPEN;u->focus_index=u->feature_cap;}
  if(old==XZ_UI_LOCAL_STEM_SETUP){u->focus_kind=target==XZ_UI_HOME?XZ_UI_HOME_SETUP:XZ_UI_LOCAL_STEM_SETUP_OPEN;u->focus_index=target==XZ_UI_HOME?XZ_UI_STEM_SECTION:XZ_UI_STEM;}
 }
 return n;
}
static size_t page_controls(const struct xz_ui *u,const struct xz_ui_model *m,struct xz_ui_widget w[XZ_UI_WIDGETS]){
 size_t n=0;int i;
 if(u->profile_open&&m->dj)return profile_controls(u,m,w);
 /* MODERN's rail starts with the player's own settings, above the sections. */
 if(xz_ui_modern_menu(m))add(w,&n,XZ_UI_OPEN_UTILITY,0,0,"UTILITY");
 for(i=0;i<XZ_UI_SECTIONS;i++)add(w,&n,XZ_UI_PANEL,xz_ui_sections[i],0,xz_ui_section_label(xz_ui_sections[i]));
 add(w,&n,XZ_UI_CLOSE,0,0,"CLOSE");
 add(w,&n,XZ_UI_BACK,0,0,"< BACK");
 if(m->dj)add(w,&n,XZ_UI_PROFILE,0,0,"DJ PROFILE");
 if(xz_ui_header_decks(u->page))
  for(i=0;i<(u->page==XZ_UI_KEY_SETUP?4:2);i++)add(w,&n,XZ_UI_DECK,i,0,i<2?"USB DECK":"SOFTWARE");
 if(xz_ui_section_page(u->page))return xz_ui_section_controls(u,m,w,n);
 if(u->page==XZ_UI_FEATURE_SETUP){
  static const char *steps[4]={"DETECT FEATURE","USE SELECTED DECK","ENABLE FEATURE","I TESTED IT / DONE"};
  add(w,&n,XZ_UI_FEATURE_SETUP_STEP,u->feature_step,0,steps[u->feature_step>=0&&u->feature_step<4?u->feature_step:0]);
 }else if(u->page==XZ_UI_METRICS_SETUP){
  static const char *steps[]={"DETECT METRICS","USE ON-DEVICE SENSORS","CONNECT READINGS","LIVE VERIFY"};
  int step=u->metrics_step>=0&&u->metrics_step<4?u->metrics_step:0;
  add(w,&n,XZ_UI_METRICS_CHECK,step,0,steps[step]);
 }else if(u->page==XZ_UI_STEMS){
  for(i=0;i<8;i++){
   int assigned=(m->deck[u->deck].groove_assigned&(1u<<i))!=0;
   add(w,&n,assigned?XZ_UI_GROOVE_PAD:XZ_UI_HOTCUE_PAD,i,assigned?(XZ_UI_GROOVE|XZ_UI_STEM):XZ_UI_HOTCUE,pads[i]);
  }
 }else if(u->page==XZ_UI_XPAD){
  add(w,&n,XZ_UI_STRIP,0,XZ_UI_SAMPLE,"");
  add(w,&n,XZ_UI_HOLD,0,XZ_UI_SAMPLE,"HOLD");
  add(w,&n,XZ_UI_OVERDUB,0,XZ_UI_SAMPLE,"OVERDUB");
  add(w,&n,XZ_UI_VOLUME,0,XZ_UI_SAMPLE,"");
  for(i=0;i<8;i++)add(w,&n,XZ_UI_SAMPLE_PAD,i,XZ_UI_SAMPLE,pads[i]);
 }else if(u->page==XZ_UI_LOCAL_STEM_SETUP){
  /* The button is the deck's next real step, so a prepared track never repeats DETECT.
     index stays the runtime step: 0 check engine, 1 turn on, 2 check the track.
     A host without usb_reason reports a switched-off engine as missing; after
     its check passes (step 1) the button turns stems on instead of looping. */
  enum xz_ui_usb_reason why=xz_ui_usb_blocked(m,u->deck<2?u->deck:0);
  int step=why==XZ_UI_USB_WHY_ENGINE?(u->stem_setup_step==1?1:0):why==XZ_UI_USB_WHY_OFF?1:2;
  add(w,&n,XZ_UI_LOCAL_STEM_SETUP_STEP,step,0,step==0?"CHECK STEM ENGINE":step==1?"TURN ON USB STEMS / DECKS 1 + 2":
   why==XZ_UI_USB_WHY_NONE?"STEMS READY / BACK TO PLAY":"CHECK THIS DECK AGAIN");
 }else if(u->page==XZ_UI_BROWSE_SETUP){
  add(w,&n,XZ_UI_KEY_FEEDBACK,0,0,"KEY FEEDBACK");
  /* Browse columns are the stock ones, chosen from the table header (TAP MENU);
     this page keeps only the key format the deck key strips use. */
  for(i=0;i<3;i++)add(w,&n,XZ_UI_KEY_FORMAT,i,0,key_formats[i]);
 }else if(u->page==XZ_UI_RAM_SETUP){
  static const char *names[]={"DECK 1","DECK 2","DECK 3","DECK 4"};
  for(i=0;i<4;i++)add(w,&n,XZ_UI_RAM_DECK,i,0,names[i]);
  int selected=u->ram_deck>=0&&u->ram_deck<4?u->ram_deck:0;
  const struct xz_ui_ram *r=&m->deck[selected].ram;
  if(r->state==XZ_UI_RAM_LOADING)add(w,&n,XZ_UI_RAM_CANCEL,0,0,"CANCEL COPY");
  else if(u->ram_setup&&u->ram_step<3){
   if(u->ram_step==0)add(w,&n,XZ_UI_RAM_SETUP_STEP,0,0,"DETECT RAM AND SOURCE");
   else if(!r->supported)add(w,&n,XZ_UI_RAM_SETUP_STEP,1,0,"CHECK INSTALLED ENGINE");
   else if(selected>=2&&!r->route_up)add(w,&n,XZ_UI_RAM_CONNECT,0,0,"OPEN FOUR-DECK SETUP");
   else if(!r->source_current||r->reason==XZ_RAM_NO_SOURCE||r->reason==XZ_RAM_TOO_LARGE||r->reason==XZ_RAM_PIN_FAILED)add(w,&n,XZ_UI_RAM_CHOOSE,0,0,"CHOOSE TRACK");
   else if(u->ram_step==1)add(w,&n,XZ_UI_RAM_SETUP_STEP,1,0,"USE THIS TRACK");
   else add(w,&n,XZ_UI_RAM_SETUP_STEP,2,0,"VERIFY RAM ATTACHMENT");
  }else if(!r->supported)add(w,&n,XZ_UI_RAM_SETUP_OPEN,0,0,"SET UP RAM TRACKS");
  else if(selected>=2&&!r->route_up)add(w,&n,XZ_UI_RAM_SETUP_OPEN,0,0,"SET UP FOUR DECKS");
  else if(r->state==XZ_UI_RAM_DETACHED)add(w,&n,XZ_UI_RAM_SETUP_STEP,4,0,"BACK TO PLAY");
  else if(u->ram_setup&&u->ram_step==3&&r->state==XZ_UI_RAM_READY)add(w,&n,XZ_UI_RAM_SETUP_STEP,4,0,"RAM VERIFIED / BACK TO PLAY");
  else if(r->can_release)add(w,&n,XZ_UI_RAM_RELEASE,0,0,"RELEASE RAM COPY");
  else if(!r->source_current||r->reason==XZ_RAM_NO_SOURCE||r->reason==XZ_RAM_TOO_LARGE)add(w,&n,XZ_UI_RAM_CHOOSE,0,0,"CHOOSE ANOTHER TRACK");
  else if((r->reason==XZ_RAM_BUDGET_FULL||r->reason==XZ_RAM_FREE_LOW)&&!u->ram_setup)add(w,&n,XZ_UI_RAM_MANAGE,0,0,"MANAGE RAM TRACKS");
  else if(r->reason==XZ_RAM_PIN_FAILED&&!u->ram_setup)add(w,&n,XZ_UI_RAM_SETUP_OPEN,0,0,"OPEN RAM SETUP");
  else if(r->can_request)add(w,&n,XZ_UI_RAM_REQUEST,0,0,r->state==XZ_UI_RAM_ERROR?"RETRY LOAD INTO RAM":"LOAD TRACK INTO RAM");
  else add(w,&n,XZ_UI_RAM_SETUP_OPEN,0,0,"CHECK RAM STATUS");
  for(size_t j=0;j<n;j++)if(w[j].kind==XZ_UI_RAM_REQUEST||w[j].kind==XZ_UI_RAM_CANCEL||w[j].kind==XZ_UI_RAM_RELEASE||
      (w[j].kind>=XZ_UI_RAM_SETUP_OPEN&&w[j].kind<=XZ_UI_RAM_MANAGE))w[j].deck=selected;
 }else if(u->page==XZ_UI_ABOUT){
  for(int i=0;i<XZ_ABOUT_LINKS;i++)add(w,&n,XZ_UI_ABOUT_LINK,i,0,xz_about_links[i].label);
 }else if(u->page==XZ_UI_GAMES){
  add(w,&n,XZ_UI_GAME_SELECT,0,0,"DOOM / CHOOSE WAD");
  add(w,&n,XZ_UI_GAME_SELECT,1,0,"CHEX QUEST");
  add(w,&n,XZ_UI_PANEL,XZ_UI_DOOM_FILES,0,"USB WAD LOADER");
  add(w,&n,XZ_UI_PANEL,XZ_UI_MYHOUSE_SETUP,0,"MY HOUSE / SETUP");
  add(w,&n,XZ_UI_ARCADE_OPEN,0,0,"BEAT ARCADE");
  add(w,&n,XZ_UI_WAVE_RIDER_OPEN,0,0,"WAVE RIDER");
  add(w,&n,XZ_UI_RHYTHM_LAB_OPEN,0,0,"RHYTHM LAB / 10 STUDIES");
  add(w,&n,XZ_UI_GLYPH_RUN_OPEN,0,0,"STATIC SURFER");
 }else if(u->page==XZ_UI_MYHOUSE_SETUP){
  add(w,&n,XZ_UI_MYHOUSE_CHECK,0,0,"CHECK ENGINE");
  add(w,&n,XZ_UI_PANEL,XZ_UI_GAMES,0,"BACK TO GAMES");
 }else if(u->page==XZ_UI_DOOM){
  add(w,&n,XZ_UI_DOOM_START,0,0,m->doom.chex?(m->doom_ready?"START CHEX QUEST":"SET UP CHEX QUEST"):(m->doom_ready?"START DOOM":"SET UP DOOM"));
  add(w,&n,XZ_UI_PANEL,XZ_UI_DOOM_FILES,0,"USB WADS");
 }else if(u->page==XZ_UI_DOOM_SETUP){
  add(w,&n,XZ_UI_PANEL,XZ_UI_DOOM_FILES,0,"CHOOSE USB GAME FILES");
  add(w,&n,XZ_UI_PANEL,XZ_UI_DOOM,0,"BACK TO DOOM");
 }else if(u->page==XZ_UI_DOOM_FILES){
  for(i=0;i<m->doom.count;i++)add(w,&n,XZ_UI_WAD_SELECT,m->doom.offset+i,0,m->doom.labels[i]);
  add(w,&n,XZ_UI_WAD_PAGE,-1,0,"PREV");add(w,&n,XZ_UI_WAD_PAGE,1,0,"NEXT");
  add(w,&n,XZ_UI_WAD_SCAN,0,0,"RESCAN USB");add(w,&n,XZ_UI_WAD_CLEAR,0,0,"NO ADD-ON");
  add(w,&n,XZ_UI_PANEL,XZ_UI_DOOM_SETUP,0,"SETUP");
 }else if(u->page==XZ_UI_UPDATES){
  add(w,&n,XZ_UI_UPDATE_CHECK,0,0,m->update.ready&&m->update.phase!=XZ_UPDATE_ERROR?"CHECK FOR UPDATES":"SET UP NETWORK UPDATES");
  add(w,&n,XZ_UI_UPDATE_INSTALL,0,0,"DOWNLOAD UPDATE");
  add(w,&n,XZ_UI_UPDATE_ROLLBACK,0,0,"PREVIOUS VERSION");
  add(w,&n,XZ_UI_PANEL,XZ_UI_SUPPORT,0,"REPORT AN ISSUE");
  add(w,&n,XZ_UI_PANEL,XZ_UI_UPDATE_SETUP,0,"NETWORK SETUP");
 }else if(u->page==XZ_UI_UPDATE_SETUP){
  add(w,&n,XZ_UI_UPDATE_CHECK,0,0,"DETECT UPDATE SERVER");
  add(w,&n,XZ_UI_PANEL,XZ_UI_UPDATES,0,"BACK TO UPDATES");
 }else if(u->page==XZ_UI_STEM_COLORS){
  for(int stem=0;stem<3;stem++)for(i=0;i<XZ_STEM_COLOR_CHOICES;i++)
   add(w,&n,XZ_UI_STEM_COLOR,stem*XZ_STEM_COLOR_CHOICES+i,0,xz_stem_palette_name(i));
  add(w,&n,XZ_UI_STEM_COLORS_RESET,0,0,"RESET STEM COLORS");
 }else if(u->page==XZ_UI_BAND_COLORS){
  for(int band=0;band<XZ_BANDS;band++)for(i=0;i<XZ_BAND_COLOR_CHOICES;i++)
   add(w,&n,XZ_UI_BAND_COLOR,band*XZ_BAND_COLOR_CHOICES+i,0,xz_band_palette_name(i));
  add(w,&n,XZ_UI_BAND_COLORS_RESET,0,0,"RESET 3-BAND COLOURS");
 }else if(u->page==XZ_UI_THEME_PICK){
  /* Classic themes only: FLAGSHIP is the modern interface's own look. */
  for(i=0;i<XZ_THEME_FLAGSHIP;i++)add(w,&n,XZ_UI_SET_THEME,i,0,xz_theme_name(i));
 }else if(u->page==XZ_UI_CONNECTION){
  add(w,&n,XZ_UI_TAKEOVER_TOGGLE,0,0,"VJ.TOOLS VIEW");
  /* REKORDBOX (1) is retired: CONTROLS > BUTTONS gives that key VJ.TOOLS SCREEN. */
  add(w,&n,XZ_UI_TAKEOVER_ASSIGN,0,0,"LINK");
  add(w,&n,XZ_UI_TAKEOVER_ASSIGN,2,0,"ONSCREEN");
  add(w,&n,XZ_UI_CONNECTION_ENABLE,0,XZ_UI_VJ_CONNECTION,"VJ.TOOLS CONNECTION");
  add(w,&n,XZ_UI_DISCOVERY,0,XZ_UI_VJ_DISCOVERY,"DISCOVERABLE");
 }else if(u->page==XZ_UI_PC_STEMS){
  static const char *sources[3]={"STANDALONE STEMS","PC STEMS","SOURCE UNKNOWN"};
  for(i=0;i<2;i++){int source=m->deck[i].pc_stems.source;add(w,&n,XZ_UI_DECK,i,0,sources[source>=0&&source<3?source:2]);}
  int deck=pc_deck(u),step=m->deck[deck].pc_stems.setup_step;size_t first=n;
  if(step==XZ_UI_PC_STEP_MAPPING)add(w,&n,XZ_UI_PC_CHECK,step,0,"STEMS ON / CONTINUE");
  else if(step==XZ_UI_PC_STEP_TEST)add(w,&n,XZ_UI_PC_TEST,XZ_UI_PC_DRUMS,0,"TEST: SWITCH DRUMS");
  else if(step==XZ_UI_PC_STEP_CONFIRM){
   add(w,&n,XZ_UI_PC_CONFIRM,1,0,"YES, DRUMS SWITCHED");
   add(w,&n,XZ_UI_PC_CONFIRM,0,0,"NO, TEST AGAIN");
  }else if(step==XZ_UI_PC_STEP_DONE)add(w,&n,XZ_UI_PC_CHECK,step,0,"RUN SETUP AGAIN");
  else add(w,&n,XZ_UI_PC_CHECK,XZ_UI_PC_STEP_DETECT,0,"DETECT PC");
  for(size_t k=first;k<n;k++)w[k].deck=deck;
 }else if(xz_ui_support_page(u->page)){
  n=xz_ui_support_controls(u,m,w,n);
 }else if(xz_ui_network_page(u->page)){
  n=xz_ui_network_controls(u,m,w,n);
  if(u->page==XZ_UI_NETWORK)add(w,&n,XZ_UI_PANEL,XZ_UI_UPDATES,0,"NETWORK UPDATES");
 }else if(u->page==XZ_UI_PROFILE_SETUP||u->page==XZ_UI_PROFILE_EDIT){
  /* Without a reader the page says so in one line and offers no dead button. */
  n=xz_ui_profile_controls(u,m,w,n);
 }else if(xz_sdk_ui_page(u->page))n=xz_sdk_ui_controls(u,m,w,n);
 return n;
}
unsigned xz_ui_counter_errors(const struct xz_ui_model *m){
 unsigned bits=0;
 if(xz_ui_modern_menu(m))for(int d=0;d<2;d++)if(m->counter_error[d].code&&m->counter_error[d].text)bits|=1u<<d;
 return bits;
}
int xz_ui_stock_window_notice(const struct xz_ui_model *m){
 return m->stock_window&&m->stock_window[0]&&!xz_ui_counter_errors(m);
}
size_t xz_ui_layout(const struct xz_ui *u,const struct xz_ui_model *m,struct xz_ui_widget w[XZ_UI_WIDGETS]){
 size_t n=page_controls(u,m,w);
 xz_ui_place(u,m,w,n);
 xz_sdk_ui_place(u,m,w,n);
 xz_ui_network_place(u,m,w,n);
 xz_ui_support_place(u,m,w,n);
 /* Last, so it never shifts focus or takes a touch from a control: touch only (CLOSE is not focusable), it closes
    MODS onto the play screen, where the deck shows the error and CHOOSE TRACK. REPORT THIS comes first so its
    touch wins over the strip's; it is touch only too (EXTRAS > REPORT AN ISSUE is the encoder's way in). */
 if(n<XZ_UI_WIDGETS&&xz_ui_strip_report(u,m,&w[n]))n++;
 if(xz_ui_counter_errors(m)&&n<XZ_UI_WIDGETS){
  add(w,&n,XZ_UI_CLOSE,XZ_UI_CLOSE_COUNTER,0,"COUNTER ERROR");
  w[n-1].x=0;w[n-1].y=XZ_UI_COUNTER_Y;w[n-1].w=XZ_UI_COUNTER_W;w[n-1].h=XZ_UI_COUNTER_H;
 }
 return n;
}
static int ready(const struct xz_ui *u,const struct xz_ui_model *m,int deck,const struct xz_ui_widget *w){
 const struct xz_ui_deck *d=&m->deck[deck];
 if(xz_ui_support_kind(w->kind))return xz_ui_support_ready(u,m,w);
 if(w->kind==XZ_UI_CONNECTION_ENABLE)return m->connection.ready&&m->connection.can_enable;
 if(w->kind==XZ_UI_DISCOVERY)return m->connection.ready&&m->connection.can_discover;
 if(xz_ui_profile_owns(w->kind))return xz_ui_profile_ready(u,m,w);
 if(w->kind==XZ_UI_PROFILE_PICK){
  if(!m->dj||w->index<0||w->index>=m->dj->count)return 0;
  return !m->dj->entry[w->index].damaged&&m->dj->pending<0;
 }
 if(w->kind==XZ_UI_PROFILE_PAGE||w->kind==XZ_UI_PROFILE)return m->dj!=NULL;
 if(w->kind==XZ_UI_ENABLE&&w->index==XZ_UI_STEM)return xz_ui_usb_engine(m);
 if(w->kind==XZ_UI_INFO||w->kind==XZ_UI_HOME_SETUP||w->kind==XZ_UI_DECK_VIEW)return 1;
 if(w->kind==XZ_UI_SHELL_LOOK||w->kind==XZ_UI_OPEN_UTILITY)return m->modern_look.known;
 if(w->kind==XZ_UI_ROUTE_RESCAN)return m->route.state!=XZ_UI_ROUTE_SCANNING;
 /* Page 1 on STOCK plays the player's own 1 2 4 8: nothing to edit there. */
 if((w->kind==XZ_UI_JUMP_PAIR||w->kind==XZ_UI_JUMP_SIZE)&&u->page==XZ_UI_BEAT_JUMP&&!u->jump_edit_page&&!m->jump.enabled)return 0;
 if(w->kind==XZ_UI_MUTE||w->kind==XZ_UI_BYPASS||w->kind==XZ_UI_LEVEL)return deck<2&&usb_playable(m,deck);
 uint32_t available=d->ready;
 if(w->kind==XZ_UI_ENABLE)
  available=m->deck[0].ready|m->deck[1].ready;
 if(w->kind==XZ_UI_ENABLE&&(m->enabled&(uint32_t)w->index))return 1;
 if(w->requires && (available&w->requires)!=w->requires)return 0;
 switch(w->kind){
 case XZ_UI_LEVEL:case XZ_UI_MUTE:case XZ_UI_BYPASS:return (m->enabled&XZ_UI_STEM)!=0;
 case XZ_UI_PC_TEST:return xz_ui_pc_blocked(m,deck)!=XZ_UI_PC_WHY_SOURCE&&xz_ui_pc_blocked(m,deck)!=XZ_UI_PC_WHY_LINK&&!(d->pc_stems.pending&(1u<<XZ_UI_PC_DRUMS));
 case XZ_UI_PC_CONFIRM:return xz_ui_pc_blocked(m,deck)!=XZ_UI_PC_WHY_SOURCE&&xz_ui_pc_blocked(m,deck)!=XZ_UI_PC_WHY_LINK;
 case XZ_UI_GROOVE_PAD:return (m->enabled&XZ_UI_STEM)&&(d->groove_loaded&(1u<<w->index));
 case XZ_UI_SAMPLE_PAD:return (d->sample_loaded&(1u<<w->index));
 case XZ_UI_STRIP:case XZ_UI_HOLD:case XZ_UI_OVERDUB:case XZ_UI_VOLUME:return 1;
 default:return 1;}
}
static struct xz_ui_look look(const struct xz_ui *u,const struct xz_ui_model *m,const struct xz_ui_widget *a){
 int deck=a->deck>=0?a->deck:u->deck,k=a->index;const struct xz_ui_deck *d=&m->deck[deck];
 const struct xz_theme_palette *p=xz_theme_palette(m->theme);
 struct xz_ui_look l={ready(u,m,deck,a),0,p->accent};
 if(xz_ui_network_kind(a->kind)){l.selected=xz_ui_network_selected(u,a);return l;}
 if(xz_ui_support_kind(a->kind)){l.selected=xz_ui_support_selected(u,m,a);return l;}
 switch(a->kind){
 case XZ_UI_DECK:l.selected=k==u->deck;break;
 case XZ_UI_FEATURE_LINK:break;
 case XZ_UI_METRICS_TOGGLE:l.selected=m->metrics_enabled!=0;break;
 case XZ_UI_PANEL:{
  /* A section tab stays lit on every page below it. */
  enum xz_ui_page at=u->page;for(int hop=0;hop<4&&!root_page(at);hop++)at=parent_page(at);
  l.selected=k==(int)u->page||(root_page((enum xz_ui_page)k)&&k==(int)at);break;}
 case XZ_UI_RAM_DECK:l.selected=k==u->ram_deck;break;
 case XZ_UI_RAM_REQUEST:l.selected=1;break;
 case XZ_UI_PROFILE:l.selected=u->profile_open;break;
 case XZ_UI_PROFILE_PICK:l.selected=m->dj&&k>=0&&k<m->dj->count&&m->dj->entry[k].stick_active;
  if(l.selected&&m->dj->entry[k].has_accent)l.color=m->dj->entry[k].accent;break;
 case XZ_UI_PROFILE_CHECK:l.selected=1;break;
 case XZ_UI_SET_THEME:l.selected=k==m->classic_theme;break;
 case XZ_UI_SET_INTERFACE:l.selected=k==m->ui_interface;break;
 case XZ_UI_SET_MODERN_THEME:l.selected=k==m->modern_theme;break;
 case XZ_UI_STEM_COLOR:{int stem=k/XZ_STEM_COLOR_CHOICES,choice=k%XZ_STEM_COLOR_CHOICES;
  l.color=xz_stem_palette_color(choice,xz_ui_stem_color(m->theme,stem));
  l.selected=m->stem_colors[stem]==choice;break;}
 case XZ_UI_BAND_COLOR:{int band=k/XZ_BAND_COLOR_CHOICES,choice=k%XZ_BAND_COLOR_CHOICES;
  l.color=xz_band_palette_color(choice,xz_ui_band_standard(m,band));
  l.selected=m->band_colors[band]==choice;break;}

 case XZ_UI_JUMP_ENABLE:l.selected=k==2?!m->jump.enabled:m->jump.enabled;break;
 case XZ_UI_DECK_VIEW:l.selected=k==m->deck_view;break;
 case XZ_UI_HOME_SETUP:l.selected=1;break;
 case XZ_UI_JUMP_SHIFT:l.selected=m->jump.shift_page;break;
 case XZ_UI_JUMP_EDIT_PAGE:l.selected=k==u->jump_edit_page;break;
 case XZ_UI_JUMP_PAIR:l.selected=k/2==u->jump_edit_pair;break;
 case XZ_UI_JUMP_SIZE:l.selected=k==m->jump.sizes[u->jump_edit_page*4+u->jump_edit_pair];break;
 case XZ_UI_STEM_PAGE:l.selected=k==m->stem_page;break;
 case XZ_UI_WAVE_MODE:l.selected=k==m->wave_mode;break;
 case XZ_UI_SHIFT_PAGES:l.selected=m->shift_pages;break;
 case XZ_UI_PAD_FEEDBACK:l.selected=k==2?!m->pad_feedback:m->pad_feedback;break;
 case XZ_UI_FOUR_WAVE_ORDER:l.selected=!!m->four_wave_mixer;break;
 case XZ_UI_STEMS_OVERLAY:{int now=xz_ui_modern_menu(m)?m->modern_stems:m->stems_overlay;l.selected=k?now==k-1:now;break;}
 case XZ_UI_SHELL_LOOK:{
  /* a switch is on while its part shows; a choice is lit while it is the shell's value */
  int item=k/8,choice=k%8;
  l.selected=item==XZ_UI_LOOK_WAVE_COLOR?m->modern_look.wave_color==choice:item==XZ_UI_LOOK_USB_COLORS?m->modern_look.usb_colors==choice:
   item>=0&&item<XZ_UI_LOOK_ITEMS&&!((m->modern_look.hidden>>item)&1u);
  break;}
 case XZ_UI_STEM_ORDER:l.selected=0;break;
 case XZ_UI_SHIFT_KEYSYNC:l.selected=m->shift_keysync;break;
 case XZ_UI_KEY_SYNC:l.color=0x52d794;l.selected=deck<4&&d->key_synced;break;
 case XZ_UI_BROWSE_COLUMN:l.selected=m->browse_columns[k/4]==k%4;break;
 case XZ_UI_BROWSE_KEY_PREFIX:l.selected=m->browse_key_prefix;break;
 case XZ_UI_BROWSE_TRAFFIC:l.selected=m->browse_traffic;break;
 case XZ_UI_KEY_FORMAT:l.selected=k==m->key_format;break;
 case XZ_UI_RAM_DEBUG:l.selected=m->ram_debug;break;
 case XZ_UI_KEY_FEEDBACK:l.selected=m->key_feedback;break;
 case XZ_UI_REKORDBOX_KEY:l.selected=m->rekordbox_assign[0]==XZ_UI_RB_MODS;break;
 case XZ_UI_REKORDBOX_ASSIGN:l.selected=k>=0&&k<2*XZ_UI_RB_CHOICES&&m->rekordbox_assign[k/XZ_UI_RB_CHOICES]==k%XZ_UI_RB_CHOICES;break;
 case XZ_UI_CONTROL_SECTION:l.selected=k>=0&&k<4&&section_pages[k]==u->page;break;
 case XZ_UI_STEM_PAD_CONFIG:l.selected=k/4==m->stem_bank;break;
 case XZ_UI_STEM_BANK:l.selected=m->stem_bank==k;break;
 case XZ_UI_WAD_SELECT:l.selected=k==m->doom.base||k==m->doom.map;break;
 case XZ_UI_TAKEOVER_TOGGLE:l.color=0x52d794;l.selected=m->fb_takeover!=0;break;
 case XZ_UI_TAKEOVER_ASSIGN:l.selected=k==m->takeover_assign;break;
 case XZ_UI_BYPASS:l.selected=d->bypass;break;
 case XZ_UI_HOLD:l.selected=d->hold;break;
 case XZ_UI_OVERDUB:l.selected=d->overdub;break;
 case XZ_UI_ENABLE:l.selected=(m->enabled&(uint32_t)k)!=0;break;
 case XZ_UI_CONNECTION_ENABLE:l.selected=m->connection.ready&&m->connection.enabled;break;
 case XZ_UI_DISCOVERY:l.selected=m->connection.ready&&m->connection.discoverable;break;
 case XZ_UI_MUTE:l.color=xz_ui_model_local_stem_color(m,k);l.selected=k>=0&&k<4&&(d->muted&(1u<<k))!=0;break;
 case XZ_UI_GROOVE_PAD:l.color=xz_ui_model_stem_color(m,d->groove_stem[k]<3?d->groove_stem[k]:0);l.selected=d->groove_active==k;break;
 case XZ_UI_SAMPLE_PAD:l.selected=d->sample_active==k;break;
 default:l.selected=xz_sdk_ui_selected(u,m,a);break;}
 return l;
}
int xz_ui_widget_ready(const struct xz_ui *u,const struct xz_ui_model *m,const struct xz_ui_widget *w){
 return ready(u,m,w->deck>=0?w->deck:u->deck,w);
}
struct xz_ui_look xz_ui_widget_look(const struct xz_ui *u,const struct xz_ui_model *m,const struct xz_ui_widget *w){return look(u,m,w);}
const char *xz_ui_ram_state_text(enum xz_ui_ram_state state){
 switch(state){
 case XZ_UI_RAM_EMPTY:return "NOT IN RAM";
 case XZ_UI_RAM_LOADING:return "COPYING TO RAM";
 case XZ_UI_RAM_READY:return "ORIGINAL AUDIO IN RAM";
 case XZ_UI_RAM_ERROR:return "COPY FAILED";
 case XZ_UI_RAM_DETACHED:return "USB OUT / RAM COPY ONLY";
 default:return "RAM ENGINE NOT DETECTED";}
}
const char *xz_ui_ram_status_text(const struct xz_ui_ram *r){
 if(!r->supported)return "RAM ENGINE NOT DETECTED";
 if(r->software&&!r->route_up&&(r->state==XZ_UI_RAM_UNAVAILABLE||r->state==XZ_UI_RAM_EMPTY))return "LOOPBACK CABLE NOT CONNECTED";
 return xz_ui_ram_state_text(r->state);
}
int xz_ui_section_page_notice(const struct xz_ui *u){return u&&(xz_ui_section_page(u->page)||u->page==XZ_UI_STEM_COLORS||u->page==XZ_UI_BAND_COLORS);}
const char *xz_ui_ram_scope(const struct xz_ui_ram *r){
 if(r->software){
  if(r->retained_assets&XZ_RAM_AUDIO){
   if(!r->route_up)return "RAM retained; the loopback audio connection is down.";
   return r->retained_assets&XZ_RAM_WAVE?"Original audio, loaded waveform and cues are retained.":"Original audio and session cues are retained; no waveform was loaded.";
  }
  return "Up to 64 MiB a track, 128 MiB in all.";
 }
 if(r->state==XZ_UI_RAM_READY||r->state==XZ_UI_RAM_DETACHED)
  return r->retained_stems_bytes?"AUDIO + STEMS IN RAM / WAVES KEPT":"AUDIO IN RAM / STEMS NEED USB";
 return "COPIES AUDIO / STEMS STAY ON USB";
}
const char *xz_ui_footer_text(const struct xz_ui *u,const struct xz_ui_model *m){
 if(xz_ui_support_page(u->page))return xz_ui_support_footer(u,m);
 if(xz_ui_network_page(u->page))return u->notice[0]?u->notice:m->network.detail[0]?m->network.detail:"SET UP NETWORK / CHANGES LAST UNTIL REBOOT";
 const struct xz_ui_deck *d=&m->deck[u->deck];
 const char *saved=m->settings_status?m->settings_status:"INSERT USB TO SAVE SETTINGS";
 if(m->settings_flash)return saved;
 if(m->dj&&m->dj->flash&&m->dj->status)return m->dj->status;
 if((u->profile_open||u->page==XZ_UI_PROFILE_SETUP||u->page==XZ_UI_PROFILE_EDIT)&&m->dj)return xz_ui_profile_footer(u,m);
 if(u->page==XZ_UI_THEME_PICK&&m->theme_status)return m->theme_status;
 if(xz_ui_section_page_notice(u))return xz_ui_save_state(m,NULL)[0]?xz_ui_save_state(m,NULL):saved;
 if(u->notice[0])return u->notice;
 if(u->page==XZ_UI_LOCAL_STEM_SETUP)return xz_ui_usb_reason_text(xz_ui_usb_blocked(m,u->deck<2?u->deck:0));
 if(xz_sdk_ui_page(u->page))return xz_sdk_ui_footer(u,m);
 if(u->page==XZ_UI_BEAT_JUMP)return saved;
 if(u->page==XZ_UI_PC_STEMS)return pc_page_status(m,pc_deck(u));
 if(u->page==XZ_UI_BROWSE_SETUP)return saved;
 if(u->page==XZ_UI_CONNECTION)
  return m->connection.status?m->connection.status:m->connection.ready&&m->connection.connected?"VJ.Tools CONNECTED":"VJ.Tools CONNECTION AVAILABLE";
 if(u->page==XZ_UI_ABOUT)return "TURN TO CHOOSE / PUSH OR TAP TO SHOW QR / SCAN WITH YOUR PHONE";
 if(u->page==XZ_UI_EXTRAS)return "OPTIONAL EXTRAS / DJ CONTROLS STAY AVAILABLE";
 if(u->page==XZ_UI_DOOM||u->page==XZ_UI_DOOM_SETUP)return m->doom_ready?"DOOM READY / GAME RUNS ENTIRELY ON THE XZ":"OPEN SETUP TO CHECK THE USB GAME FILES";
 if(u->page==XZ_UI_DOOM_FILES)return m->doom.status;
 if(u->page==XZ_UI_UPDATES||u->page==XZ_UI_UPDATE_SETUP)return m->update.status;
 if(u->page==XZ_UI_RAM_SETUP)return xz_ui_ram_scope(&m->deck[u->ram_deck>=0&&u->ram_deck<4?u->ram_deck:0].ram);
 return d->status?d->status:"LOAD A TRACK TO USE DECK CONTROLS";
}
static struct xz_ui_scene scene(const struct xz_ui *u,const struct xz_ui_model *m,struct xz_canvas c,
 const struct xz_ui_deck *d,const struct xz_ui_widget *w,struct xz_ui_look *looks,size_t n,struct xz_theme_palette *palette){
 for(size_t i=0;i<n;i++)looks[i]=look(u,m,&w[i]);
 *palette=*xz_theme_palette(m->theme);
 for(int stem=0;stem<3;stem++)palette->stem[stem]=xz_ui_model_stem_color(m,stem);
 return (struct xz_ui_scene){c,u,m,d,palette,w,looks,n};
}
/* Steps come from the runtime snapshot only; the page never advances itself. */
static void pc_setup_render(const struct xz_ui_scene *s){
 struct xz_canvas c=s->c;const struct xz_ui *u=s->u;const struct xz_ui_model *m=s->m;const struct xz_theme_palette *p=s->p;
 uint32_t ink=m->theme==XZ_THEME_ANALOG?0x3a3020:p->ink,bg=m->theme==XZ_THEME_ANALOG?0xd9c9a3:p->bg,dim=xz_blend(bg,ink,150);
 int deck=pc_deck(u);const struct xz_ui_pc_stems *pc=&m->deck[deck].pc_stems;
 int step=pc->setup_step<XZ_UI_PC_STEP_DETECT||pc->setup_step>XZ_UI_PC_STEP_DONE?XZ_UI_PC_STEP_DETECT:pc->setup_step;
 enum xz_ui_pc_reason why=xz_ui_pc_blocked(m,deck);char line[96];
 snprintf(line,sizeof(line),"DECK %d SOURCE: %s",deck+1,pc->source==XZ_UI_SOURCE_PC?"COMPUTER / DJ SOFTWARE":
  pc->source==XZ_UI_SOURCE_LOCAL?"XZ USB / STANDALONE STEMS":"NOT CONFIRMED");
 xz_text(c,560,98,line,1,38,ink);
 snprintf(line,sizeof(line),"PC LINK: %s",pc->source!=XZ_UI_SOURCE_PC?"--":pc->connected?"CONNECTED":"LOST / NOTHING SENT");
 xz_text(c,560,112,line,1,38,pc->source==XZ_UI_SOURCE_PC&&!pc->connected?p->alarm:ink);
 snprintf(line,sizeof(line),"STEM SWITCHING: %s",why==XZ_UI_PC_WHY_NONE?"TESTED":"NOT TESTED");
 xz_text(c,560,126,line,1,38,ink);
 static const char *names[4]={"1 DETECT","2 MAP STEMS","3 LIVE TEST","4 CONFIRM"};
 for(int i=0;i<4;i++){
  int x=XZ_UI_CX+i*166,current=i==step,done=i<step;uint32_t edge=current?p->accent:done?xz_blend(bg,p->accent,150):xz_blend(bg,ink,70);
  xz_round_rect(c,x,148,158,28,8,15,edge);xz_round_rect(c,x+1,149,156,26,7,15,xz_blend(bg,current?p->accent:ink,current?60:done?30:12));
  xz_text(c,x+10,158,names[i],1,18,current||done?ink:dim);
  if(done)xz_text(c,x+158-10-xz_text_width(c,"DONE",1,8),158,"DONE",1,8,ink);
 }
 if(step==XZ_UI_PC_STEP_DONE&&why!=XZ_UI_PC_WHY_NONE){
  xz_text(c,XZ_UI_CX,192,"SETUP WAS COMPLETED, BUT STEMS CANNOT BE SENT NOW.",1,108,ink);
  xz_text(c,XZ_UI_CX,212,"THE DECK ROW STAYS LOCKED UNTIL THE PC LINK RETURNS.",1,108,ink);
  xz_text(c,XZ_UI_CX,292,pc_page_status(m,deck),1,108,why==XZ_UI_PC_WHY_LINK?p->alarm:dim);
  xz_text(c,XZ_UI_CX,312,"STEM SWITCHES ARE READY AFTER TESTING. LEVEL AND BYPASS NEED MORE VERIFICATION.",1,108,dim);
  return;
 }
 static const char *text[5][3]={
  {"CONNECT THE XZ TO THE PC BY USB AND OPEN","THE DJ SOFTWARE IN PERFORMANCE MODE.","DETECT CHECKS THAT THIS DECK IS PLAYING FROM THE PC."},
  {"IMPORT THE XZ STEMS MAP IN THE DJ SOFTWARE.","FILE: XZ-onscreen-stems-4deck.midi.csv","TURN STEMS ON FOR THE LOADED TRACK."},
  {"LOAD A TRACK ON THIS DECK IN THE DJ SOFTWARE.","THE TEST SENDS ONE DRUMS SWITCH.","WATCH THE DRUMS STEM IN THE DJ SOFTWARE."},
  {"DID THE DRUMS STEM SWITCH IN THE DJ SOFTWARE?","TAP DRUMS ON THE DECK ROW LATER TO SWITCH IT BACK.","NO GOES BACK TO THE TEST STEP."},
  {"ONSCREEN STEMS NOW SWITCH THE SOFTWARE'S STEMS ON THIS DECK.","LIT/UNLIT SHOWS ONLY WHEN THE SOFTWARE REPORTS ITS LED.","PADS AND STANDALONE STEMS KEEP THEIR OWN SETTINGS."}};
 xz_text(c,XZ_UI_CX,190,text[step][0],step==XZ_UI_PC_STEP_DONE?1:2,step==XZ_UI_PC_STEP_DONE?108:54,ink);
 for(int i=1;i<3;i++)xz_text(c,XZ_UI_CX,step==XZ_UI_PC_STEP_DONE?190+i*20:196+i*24,text[step][i],1,108,ink);
 if(step==XZ_UI_PC_STEP_TEST&&(pc->pending&(1u<<XZ_UI_PC_DRUMS)))xz_text(c,XZ_UI_CX,292,"DRUMS SENT / WAITING FOR THE DJ SOFTWARE",1,108,p->accent);
 else if(why==XZ_UI_PC_WHY_SOURCE||why==XZ_UI_PC_WHY_LINK||(why!=XZ_UI_PC_WHY_NONE&&pc->reason))
  xz_text(c,XZ_UI_CX,292,pc_page_status(m,deck),1,108,why==XZ_UI_PC_WHY_LINK?p->alarm:dim);
 xz_text(c,XZ_UI_CX,312,"STEM SWITCHES ARE READY AFTER TESTING. LEVEL AND BYPASS NEED MORE VERIFICATION.",1,108,dim);
}
static int profile_kind(enum xz_ui_action_kind kind){
 return kind==XZ_UI_PROFILE||kind==XZ_UI_PROFILE_PICK||kind==XZ_UI_PROFILE_PAGE||xz_ui_profile_owns(kind);
}
static void render_page(const struct xz_ui *u,const struct xz_ui_model *m,uint16_t *pixels,size_t stride,int chrome);
int xz_ui_render(struct xz_ui *u,const struct xz_ui_model *m,uint16_t *pixels,size_t count,size_t stride){
 if(u)u->network_review.painted=0;
 if(!u||!m||!pixels||u->deck<0||u->deck>3||stride<800||stride>count/480)return 0;
 if(!u->profile_open||!m->dj){uint64_t review=xz_ui_network_review_token(u,m);render_page(u,m,pixels,stride,1);xz_ui_network_painted(u,m,review);return 1;}
 /* The open profile list sits over the page it was opened from. */
 struct xz_ui base=*u;base.profile_open=0;
 render_page(&base,m,pixels,stride,0);
 struct xz_ui_widget w[XZ_UI_WIDGETS];struct xz_ui_look looks[XZ_UI_WIDGETS];struct xz_theme_palette palette;
 size_t n=xz_ui_layout(u,m,w);
 struct xz_ui_scene s=scene(u,m,(struct xz_canvas){pixels,stride,800,480,m->theme},&m->deck[u->deck],w,looks,n,&palette);
 uint32_t bg,ink,accent;xz_ui_chrome_colors(m,&palette,&bg,&ink,&accent);
 xz_ui_chrome_profile(&s,bg,ink,xz_blend(bg,ink,160),accent,palette.alarm);
 xz_ui_chrome_focus(&s,accent);
 xz_ui_chrome_dock(&s,bg,ink,xz_blend(bg,ink,160),accent,palette.alarm);
 return 1;
}
static void render_page(const struct xz_ui *u,const struct xz_ui_model *m,uint16_t *pixels,size_t stride,int chrome){
 struct xz_ui_widget w[XZ_UI_WIDGETS],skin_w[XZ_UI_WIDGETS];struct xz_ui_look looks[XZ_UI_WIDGETS],skin_looks[XZ_UI_WIDGETS];struct xz_theme_palette palette;
 size_t n=xz_ui_layout(u,m,w),skin_n=0;
 struct xz_ui_scene s=scene(u,m,(struct xz_canvas){pixels,stride,800,480,m->theme},&m->deck[u->deck],w,looks,n,&palette);
 /* Skins never see the profile chip or the counter error strip; shared chrome draws them the same in every theme. */
 for(size_t i=0;i<n;i++)if(!profile_kind(w[i].kind)&&!(w[i].kind==XZ_UI_CLOSE&&w[i].index==XZ_UI_CLOSE_COUNTER)&&w[i].kind!=XZ_UI_SUPPORT_ALERT){
  skin_w[skin_n]=w[i];skin_looks[skin_n++]=looks[i];
 }
 struct xz_ui_scene skin=s;skin.w=skin_w;skin.look=skin_looks;skin.n=skin_n;
 skin_for(m->theme)->render(&skin);
 if(u->page==XZ_UI_STEM_COLORS)xz_ui_stem_colors(&s);
 if(u->page==XZ_UI_BAND_COLORS)xz_ui_band_colors(&s);
 if(u->page==XZ_UI_ABOUT){struct xz_ui_widget focus;int f=xz_ui_focused(u,m,&focus)&&focus.kind==XZ_UI_ABOUT_LINK?focus.index:-1;
  char loader[40];
  if(m->support.loader_id[0])snprintf(loader,sizeof loader,"Loader %s",m->support.loader_id);
  else snprintf(loader,sizeof loader,"Loader: not identified");
  xz_about_render(s.c,u->about_link,f,1,loader);}
 xz_key_display_panel(pixels,stride,u,m);
 if(u->page>=XZ_UI_EXTRAS&&u->page!=XZ_UI_CONNECTION)xz_ui_page_details(&s);
 xz_ui_profile_render(&s);
 if(xz_ui_network_page(u->page))xz_ui_network_render(&s);
 if(xz_ui_support_page(u->page))xz_ui_support_render(&s);
 if(u->page==XZ_UI_PC_STEMS)pc_setup_render(&s);
 if(u->page==XZ_UI_METRICS_SETUP){
  xz_rect(s.c,136,48,656,278,palette.bg);
  xz_text(s.c,152,66,"ON-DEVICE METRICS",2,50,palette.ink);
  char line[80];snprintf(line,sizeof(line),"STEP %d / 4",u->metrics_step+1);xz_text(s.c,152,102,line,1,40,palette.accent);
  static const char *instructions[]={"DETECT CPU, MEMORY AND THE SOC SENSOR.","USE THE XZ'S OWN READ-ONLY COUNTERS.","CONNECT THE BACKGROUND SAMPLER. NO PC NEEDED.","CHECK THAT FRESH READINGS ARE UPDATING."};
  int step=u->metrics_step>=0&&u->metrics_step<4?u->metrics_step:0;
  xz_text(s.c,152,132,instructions[step],1,90,palette.ink);
  xz_text(s.c,152,166,"CPU 100% = ALL FOUR CORES BUSY",1,90,palette.ink);
  xz_text(s.c,152,190,"RAM EXCLUDES ESTIMATED CACHE; FREE IS SEPARATE",1,90,palette.ink);
  xz_text(s.c,152,214,"80C YELLOW / 90C FLASHING RED + HOT",1,90,palette.ink);
  for(unsigned i=0;i<2;i++){
   const struct xz_worker_load *w=&m->metrics.worker[i];
   if((m->metrics.valid&XZ_METRICS_AUDIO)&&w->samples)
    snprintf(line,sizeof(line),"D%u RENDER P99 %u / MAX %u US%s",i+3,w->p99_us,w->max_us,
     (uint32_t)(m->clock_ms-w->sampled_ms)>3500u?" / LAST RUN":"");
   else snprintf(line,sizeof(line),"D%u RENDER: PLAY TO MEASURE",i+3);
   xz_text(s.c,152,238+(int)i*18,line,1,90,palette.ink);
  }
  unsigned count=0;for(unsigned i=0;i<4;i++)count+=!!(m->metrics.key_active_mask&(1u<<i));
  uint64_t pair=(uint64_t)m->metrics.worker[0].max_us+m->metrics.worker[1].max_us;
  snprintf(line,sizeof(line),"KEY SHIFTERS %u / 4 | D3+D4 MAX %u / %u US",count,
   pair>UINT32_MAX?UINT32_MAX:(unsigned)pair,XZ_RENDER_BLOCK_US);
  xz_text(s.c,152,278,line,1,90,palette.accent);
  snprintf(line,sizeof(line),"RAM USED %u / TOTAL %u / FREE %u MiB",m->metrics.used_mib,m->metrics.total_mib,m->metrics.free_mib);
  xz_text(s.c,152,298,line,1,90,palette.ink);
 }

 if(u->page==XZ_UI_EXTRAS||xz_sdk_ui_page(u->page))xz_sdk_ui_render(&s);
 if(xz_ui_section_page(u->page))xz_ui_section_render(&s);
 uint32_t bg,ink,accent;xz_ui_chrome_colors(m,&palette,&bg,&ink,&accent);
 xz_ui_chrome_profile(&s,bg,ink,xz_blend(bg,ink,160),accent,palette.alarm);
 xz_ui_chrome_rail_marks(&s);
 if(!chrome)return;
 /* Flagship selection is already blue, so its focus ring is white. */
 xz_ui_chrome_focus(&s,u->adjusting?palette.alarm:m->theme==XZ_THEME_FLAGSHIP?0xffffff:palette.accent);
 xz_ui_chrome_dock(&s,bg,ink,xz_blend(bg,ink,160),accent,palette.alarm);
}
static struct xz_ui_action action(enum xz_ui_action_kind kind,enum xz_ui_phase phase,int deck,int index,float value,float second){
 return (struct xz_ui_action){.kind=kind,.phase=phase,.deck=deck,.index=index,.value=value,.secondary=second,.pc_target=-1};
}
size_t xz_ui_cancel(struct xz_ui *u,struct xz_ui_action out[XZ_UI_ACTIONS]){
 size_t n=0;if(u->held_kind!=XZ_UI_NONE)out[n++]=action(u->held_kind,XZ_UI_RELEASE,u->held_deck,u->held_index,0,0);
 u->held_kind=XZ_UI_NONE;u->capture=-1;u->down=0;u->dragged=0;u->network_review.contact_blocked=0;u->network_review.press_armed=0;return n;
}
static size_t touch_widgets(struct xz_ui *u,const struct xz_ui_model *m,int x,int y,int down,struct xz_ui_action out[XZ_UI_ACTIONS],const struct xz_ui_widget *w,size_t count){
 struct xz_ui_widget a;size_t n,i;const struct xz_ui_deck *d;int press,deck;
 if(!u||!m||!out||u->deck<0||u->deck>3)return 0;
 if(!down)return xz_ui_cancel(u,out);
 press=!u->down;u->down=1;n=0;
 if(press){u->capture=-1;for(i=0;i<count;i++)if(x>=w[i].x&&x<w[i].x+w[i].w&&y>=w[i].y&&y<w[i].y+w[i].h){u->capture=(int)i;break;}}
 if(u->capture<0||(size_t)u->capture>=count)return 0;a=w[u->capture];
 deck=a.deck>=0?a.deck:u->deck;d=&m->deck[deck];
 if(press&&xz_ui_focusable(&a)){u->focus_kind=a.kind;u->focus_index=a.index;u->focus_deck=a.deck;u->adjusting=0;}
 if(press&&a.kind==XZ_UI_ABOUT_LINK){u->about_link=a.index;u->capture=-1;return 0;}
 /* Explanations only: the touch moves focus there and the dock says what it is. */
 if(a.kind==XZ_UI_INFO){u->capture=-1;return 0;}
 if(press&&xz_ui_network_kind(a.kind))return xz_ui_network_action(u,m,a.kind,a.index,out);
 if(press&&xz_ui_support_kind(a.kind))return xz_ui_support_action(u,m,a.kind,a.index,out);
 if(!ready(u,m,deck,&a)){
  if(!press)return 0;
  if(a.kind==XZ_UI_CONNECTION_ENABLE||a.kind==XZ_UI_DISCOVERY)snprintf(u->notice,sizeof(u->notice),"%s CONTROL IS NOT AVAILABLE",a.label);
  else if(a.kind==XZ_UI_PC_TEST||a.kind==XZ_UI_PC_CONFIRM){
   enum xz_ui_pc_reason why=xz_ui_pc_blocked(m,deck);
   snprintf(u->notice,sizeof(u->notice),"%s",why==XZ_UI_PC_WHY_SOURCE?"NO PC DECK FOUND / TAP DETECT PC FIRST":
    why==XZ_UI_PC_WHY_LINK?pc_reason(m,deck,why):"DRUMS SENT / WATCH THE DJ SOFTWARE");
  }
  else if(xz_ui_profile_owns(a.kind))snprintf(u->notice,sizeof(u->notice),"%s",xz_ui_profile_blocked(u,m,&a));
  else if(a.kind==XZ_UI_PROFILE_PICK)snprintf(u->notice,sizeof(u->notice),"%s",m->dj&&m->dj->pending>=0?
   "STILL SWITCHING THE LAST PROFILE":"THIS PROFILE FILE IS DAMAGED / SAVE IT AGAIN FROM VJ.TOOLS");
  else if(a.kind==XZ_UI_MUTE||a.kind==XZ_UI_BYPASS||a.kind==XZ_UI_LEVEL){
   /* Locked stems name their one reason; the runtime opens STANDALONE STEMS setup at it. */
   enum xz_ui_usb_reason why=xz_ui_usb_blocked(m,deck);const char *error=deck<2?m->deck[deck].stem_error:NULL;
   snprintf(u->notice,sizeof(u->notice),"%s",why==XZ_UI_USB_WHY_ERROR&&error&&*error?error:xz_ui_usb_next_step(why));
   u->notice_deck=deck;out[n++]=action(XZ_UI_UNAVAILABLE,XZ_UI_PRESS,deck,XZ_UI_STEM,0,0);return n;
  }
  else if(a.kind==XZ_UI_JUMP_PAIR||a.kind==XZ_UI_JUMP_SIZE)snprintf(u->notice,sizeof(u->notice),"PAGE 1 USES THE PLAYER'S OWN JUMPS. CHOOSE MY SIZES TO CHANGE THEM.");
  else if(a.kind==XZ_UI_ROUTE_RESCAN)snprintf(u->notice,sizeof(u->notice),"%s",m->route.state==XZ_UI_ROUTE_SCANNING?"STILL LOOKING FOR THE CABLE.":"DECKS 3 AND 4 ARE NOT RUNNING ON THIS PLAYER.");
  else snprintf(u->notice,sizeof(u->notice),"%s / NOT READY - CHECK FEATURE, MEDIA AND RUNTIME",a.label[0]?a.label:"CONTROL");
  out[n++]=action(XZ_UI_UNAVAILABLE,XZ_UI_PRESS,deck,(int)a.requires,0,0);return n;
 }
 if(press)u->notice[0]=0;
 if(a.kind==XZ_UI_LEVEL||a.kind==XZ_UI_VOLUME){out[n++]=action(a.kind,press?XZ_UI_PRESS:XZ_UI_MOVE,deck,a.index,clampf((float)(x-a.x-8)/(float)(a.w-16),0,1),0);return n;}
 if(a.kind==XZ_UI_STRIP){static const float beats[6]={.0625f,.125f,.25f,.5f,1,2};int column=(x-a.x)*6/a.w;float pitch=clampf(12-24*(float)(y-a.y)/(float)(a.h-1),-12,12);
  column=column<0?0:column>5?5:column;
  u->held_kind=a.kind;u->held_deck=deck;u->held_index=column;out[n++]=action(a.kind,press?XZ_UI_PRESS:XZ_UI_MOVE,deck,column,beats[column],pitch);return n;}
 if(!press)return 0;
 if(xz_sdk_ui_kind(a.kind))return xz_sdk_ui_touch(u,m,&a,out);
 if(xz_ui_profile_owns(a.kind)){int done=xz_ui_profile_touch(u,m,&a,out);if(done>=0)return (size_t)done;}
 if(a.kind==XZ_UI_PROFILE){
  /* Opening lands on the profile in the header, so one push then one turn switches. */
  int open=!u->profile_open,current=m->dj->current;
  u->profile_open=open;u->capture=-1;u->adjusting=0;
  if(open){
   u->profile_offset=current>0?current/XZ_UI_PROFILE_ROWS*XZ_UI_PROFILE_ROWS:0;
   if(m->dj->count){u->focus_kind=XZ_UI_PROFILE_PICK;u->focus_index=current>=0?current:0;}
   else{u->focus_kind=XZ_UI_PANEL;u->focus_index=XZ_UI_PROFILE_SETUP;}
  }else{u->focus_kind=XZ_UI_PROFILE;u->focus_index=0;}
  u->focus_deck=-1;
  out[0]=action(XZ_UI_PROFILE,XZ_UI_PRESS,deck,0,(float)open,0);return 1;
 }
 if(a.kind==XZ_UI_PROFILE_PICK){
  u->profile_open=0;u->capture=-1;u->focus_kind=XZ_UI_PROFILE;u->focus_index=0;u->focus_deck=-1;
  out[0]=action(a.kind,XZ_UI_PRESS,deck,a.index,0,0);return 1;
 }
 if(a.kind==XZ_UI_PROFILE_PAGE){
  int first;xz_ui_profile_window(u,m,&first);
  u->profile_offset=first+a.index*XZ_UI_PROFILE_ROWS;xz_ui_profile_window(u,m,&u->profile_offset);
  u->capture=-1;return 0;
 }
 if(a.kind==XZ_UI_BACK){size_t count=xz_ui_back(u,out);u->down=1;return count;}
 if(a.kind==XZ_UI_CONTROL_SECTION){xz_ui_navigate(u,section_pages[a.index>=0&&a.index<4?a.index:0]);u->capture=-1;out[0]=action(a.kind,XZ_UI_PRESS,deck,a.index,0,0);return 1;}
 if(a.kind==XZ_UI_PC_SETUP||a.kind==XZ_UI_PC_TOGGLE||a.kind==XZ_UI_PC_BYPASS){
  /* Inline touch captures sendable toggles itself. Anything that reaches here
     is locked or informational and leads to setup with its reason. */
  enum xz_ui_pc_reason why=a.kind==XZ_UI_PC_BYPASS?XZ_UI_PC_WHY_NO_BYPASS:a.kind==XZ_UI_PC_SETUP?XZ_UI_PC_WHY_TAG:xz_ui_pc_blocked(m,deck);
  if(why==XZ_UI_PC_WHY_NONE&&!pc_role_available(m,deck,a.index))why=XZ_UI_PC_WHY_ROLE;
  snprintf(u->notice,sizeof(u->notice),"%s",pc_reason(m,deck,why));
  u->notice_deck=deck;u->capture=-1;out[0]=action(XZ_UI_PC_SETUP,XZ_UI_PRESS,deck,(int)why,0,0);return 1;
 }
 if(a.kind==XZ_UI_JUMP_EDIT_PAGE){u->jump_edit_page=a.index;u->capture=-1;out[0]=action(a.kind,XZ_UI_PRESS,deck,a.index,0,0);return 1;}
 if(a.kind==XZ_UI_HOME_SETUP){
  /* The one fix a HOME tile needs: USB stems' setup, or the 4 DECKS page with its RESCAN. */
  enum xz_ui_page old=u->page;
  if(a.index==XZ_UI_STEM_SECTION){xz_ui_navigate(u,XZ_UI_LOCAL_STEM_SETUP);if(u->deck>1)u->deck=0;}
  else{xz_ui_navigate(u,XZ_UI_FOUR_DECKS);u->history[u->history_count++]=old;}
  u->capture=-1;out[0]=action(XZ_UI_PANEL,XZ_UI_PRESS,u->deck,(int)old,(float)u->page,0);return 1;
 }
 if(a.kind==XZ_UI_STEMS_OVERLAY&&a.index>0&&xz_ui_modern_menu(m)){
  /* MODERN's six layouts: the layout itself (value = layout + 1); the runtime sets it. */
  u->capture=-1;out[0]=action(a.kind,XZ_UI_PRESS,deck,0,(float)a.index,0);return 1;
 }
 if(a.kind==XZ_UI_SHELL_LOOK){
  int item=a.index/8,choice=a.index%8;
  if(item!=XZ_UI_LOOK_WAVE_COLOR&&item!=XZ_UI_LOOK_USB_COLORS)choice=(int)((m->modern_look.hidden>>item)&1u)?0:1; /* 0 SHOW, 1 HIDE */
  u->capture=-1;out[0]=action(a.kind,XZ_UI_PRESS,deck,item,(float)choice,0);return 1;
 }
 if(a.kind==XZ_UI_STEMS_OVERLAY&&a.index>0){
  /* A layout picture: the STEMS button's own step, as many times as it takes to reach this layout. */
  int now=m->stems_overlay;size_t k=0;
  for(int step=0;step<XZ_STEM_LAYOUT_COUNT&&now!=a.index-1&&k<XZ_UI_ACTIONS;step++){out[k++]=action(a.kind,XZ_UI_PRESS,deck,0,0,0);now=xz_wave_layout_next(now);}
  u->capture=-1;return k;
 }
 if(a.kind==XZ_UI_JUMP_PAIR){u->jump_edit_pair=a.index/2;u->capture=-1;out[0]=action(a.kind,XZ_UI_PRESS,deck,a.index,0,0);return 1;}
 if(a.kind==XZ_UI_STEM_COLOR){out[0]=action(a.kind,XZ_UI_PRESS,deck,a.index/XZ_STEM_COLOR_CHOICES,(float)(a.index%XZ_STEM_COLOR_CHOICES),0);return 1;}
 if(a.kind==XZ_UI_BAND_COLOR){out[0]=action(a.kind,XZ_UI_PRESS,deck,a.index/XZ_BAND_COLOR_CHOICES,(float)(a.index%XZ_BAND_COLOR_CHOICES),0);return 1;}
 if(a.kind==XZ_UI_REKORDBOX_ASSIGN){out[0]=action(a.kind,XZ_UI_PRESS,deck,a.index/XZ_UI_RB_CHOICES,(float)(a.index%XZ_UI_RB_CHOICES),0);return 1;}
 if(a.kind==XZ_UI_JUMP_SIZE){out[0]=action(a.kind,XZ_UI_PRESS,deck,u->jump_edit_page*4+u->jump_edit_pair,(float)a.index,0);return 1;}
 if(a.kind==XZ_UI_FEATURE_SETUP_OPEN){
  enum xz_ui_page old=u->page;xz_ui_navigate(u,XZ_UI_FEATURE_SETUP);u->feature_cap=a.index;u->feature_step=0;
  if(u->deck>1)u->deck=0;
  out[0]=action(XZ_UI_PANEL,XZ_UI_PRESS,u->deck,(int)old,(float)u->page,0);return 1;
 }
 if(a.kind==XZ_UI_FEATURE_SETUP_STEP){
  uint32_t cap=(uint32_t)u->feature_cap;
  if(!(d->ready&cap)){
   snprintf(u->notice,sizeof(u->notice),"CUE FEATURE NOT READY / LOAD A USB TRACK OR UPDATE THE USB LOADER");return 0;
  }
  if(u->feature_step==2){u->feature_step=3;out[0]=action(XZ_UI_ENABLE,XZ_UI_PRESS,deck,u->feature_cap,1,0);return 1;}
  if(u->feature_step==3){
   if(!(m->enabled&cap)||!d->track||!*d->track){snprintf(u->notice,sizeof(u->notice),"LOAD A TRACK ON THIS DECK, ENABLE THE FEATURE, THEN TEST IT");return 0;}
   return xz_ui_back(u,out);
  }
  u->feature_step++;return 0;
 }
 if(a.kind==XZ_UI_RAM_DECK){
  u->ram_deck=a.index;u->capture=-1;
  out[0]=action(a.kind,XZ_UI_PRESS,u->ram_deck,a.index,0,0);return 1;
 }
 if(a.kind==XZ_UI_RAM_SETUP_OPEN||a.kind==XZ_UI_RAM_MANAGE){
  u->ram_setup=1;u->ram_step=a.kind==XZ_UI_RAM_MANAGE?4:0;u->capture=-1;return 0;
 }
 if(a.kind==XZ_UI_RAM_SETUP_STEP){
  if(a.index==4){u->ram_setup=0;out[0]=action(XZ_UI_CLOSE,XZ_UI_PRESS,u->ram_deck,0,0,0);return 1;}
  const struct xz_ui_ram *r=&m->deck[u->ram_deck].ram;
  if(r->supported&&u->ram_step<3)u->ram_step++;
  out[0]=action(a.kind,XZ_UI_PRESS,u->ram_deck,u->ram_step,0,0);return 1;
 }
 if(a.kind==XZ_UI_RAM_CONNECT){xz_ui_navigate(u,XZ_UI_FOUR_DECKS);u->history[u->history_count++]=XZ_UI_RAM_SETUP;return 0;}
 if(a.kind==XZ_UI_RAM_REQUEST||a.kind==XZ_UI_RAM_CANCEL||a.kind==XZ_UI_RAM_RELEASE||a.kind==XZ_UI_RAM_CHOOSE){
  out[0]=action(a.kind,XZ_UI_PRESS,u->ram_deck,a.index,1,0);
  out[0].ram_key=m->deck[u->ram_deck].ram.key;return 1;
 }
 if(a.kind==XZ_UI_PANEL||a.kind==XZ_UI_FEATURE_LINK){
  enum xz_ui_page old=u->page;xz_ui_navigate(u,(enum xz_ui_page)a.index);
  if(a.kind==XZ_UI_FEATURE_LINK&&root_page(u->page))u->history[u->history_count++]=old;
  if(u->page==XZ_UI_SUPPORT&&old!=XZ_UI_SUPPORT)xz_ui_support_opened(u,m,old);
  u->capture=-1;out[n++]=action(XZ_UI_PANEL,XZ_UI_PRESS,u->deck,(int)old,(float)u->page,0);
 }
 else if(a.kind==XZ_UI_DECK){int old=u->deck;u->deck=a.index;u->capture=-1;out[n++]=action(a.kind,XZ_UI_PRESS,old,a.index,0,0);}
 else if(a.kind==XZ_UI_MUTE||a.kind==XZ_UI_SAMPLE_PAD||a.kind==XZ_UI_HOTCUE_PAD){u->held_kind=a.kind;u->held_deck=deck;u->held_index=a.index;out[n++]=action(a.kind,XZ_UI_PRESS,deck,a.index,1,0);}
 else{float value=1;
  switch(a.kind){case XZ_UI_ENABLE:value=(m->enabled&(uint32_t)a.index)?0:1;break;case XZ_UI_BYPASS:value=d->bypass?0:1;break;
  case XZ_UI_HOLD:value=d->hold?0:1;break;case XZ_UI_OVERDUB:value=d->overdub?0:1;break;case XZ_UI_SERVER_AUTO:value=m->server_auto?0:1;break;
  case XZ_UI_CONNECTION_ENABLE:value=m->connection.enabled?0:1;break;case XZ_UI_DISCOVERY:value=m->connection.discoverable?0:1;break;
  case XZ_UI_JUMP_ENABLE:value=a.index==1?1:a.index==2?0:m->jump.enabled?0:1;break;
  case XZ_UI_DECK_VIEW:value=(float)a.index;break;
  case XZ_UI_JUMP_SHIFT:value=m->jump.shift_page?0:1;break;
  case XZ_UI_SHIFT_PAGES:value=m->shift_pages?0:1;break;
  case XZ_UI_PAD_FEEDBACK:value=a.index==1?1:a.index==2?0:m->pad_feedback?0:1;break;
  case XZ_UI_SHIFT_KEYSYNC:value=m->shift_keysync?0:1;break;
  case XZ_UI_RAM_DEBUG:value=m->ram_debug?0:1;break;
  case XZ_UI_KEY_FEEDBACK:value=m->key_feedback?0:1;break;
  case XZ_UI_FOUR_WAVE_ORDER:value=m->four_wave_mixer?0:1;break;
  case XZ_UI_REKORDBOX_KEY:value=m->rekordbox_assign[0]==XZ_UI_RB_MODS?0:1;break;
  case XZ_UI_BROWSE_KEY_PREFIX:value=m->browse_key_prefix?0:1;break;
  case XZ_UI_BROWSE_TRAFFIC:value=m->browse_traffic?0:1;break;
  case XZ_UI_BROWSE_COLUMN:case XZ_UI_KEY_FORMAT:value=(float)a.index;break;
  case XZ_UI_TAKEOVER_TOGGLE:value=m->fb_takeover?0:1;break;
  case XZ_UI_TAKEOVER_ASSIGN:value=(float)a.index;break;
  case XZ_UI_STEM_PAGE:value=(float)a.index;break;
  case XZ_UI_PC_CONFIRM:value=(float)a.index;break;
  case XZ_UI_WAVE_MODE:value=(float)a.index;break;
  case XZ_UI_KEY_SHIFT:value=(float)a.index;break;case XZ_UI_SET_THEME:case XZ_UI_SET_INTERFACE:case XZ_UI_SET_MODERN_THEME:value=(float)a.index;break;default:break;}
  out[n++]=action(a.kind,XZ_UI_PRESS,deck,a.index,value,
   (a.kind==XZ_UI_PC_TEST||a.kind==XZ_UI_PC_CONFIRM)?(float)d->pc_stems.target:0);
  if(a.kind==XZ_UI_PC_TEST||a.kind==XZ_UI_PC_CONFIRM){out[n-1].pc_epoch=d->pc_stems.epoch;out[n-1].pc_target=d->pc_stems.target;}
 }
 return n;
}
size_t xz_ui_touch(struct xz_ui *u,const struct xz_ui_model *m,int x,int y,int down,struct xz_ui_action out[XZ_UI_ACTIONS]){
 struct xz_ui_widget w[XZ_UI_WIDGETS];
 if(!u||!m||u->deck<0||u->deck>3)return 0;
 u->network_review.press_armed=down&&!u->down;
 size_t count=xz_ui_layout(u,m,w);
 if(u->profile_open&&m->dj&&down&&!u->down){
  int hit=0;for(size_t i=0;i<count;i++)hit|=x>=w[i].x&&x<w[i].x+w[i].w&&y>=w[i].y&&y<w[i].y+w[i].h;
  /* A tap outside the list only closes it; the finger stays captured until release. */
  if(!hit){u->profile_open=0;u->down=1;u->capture=-1;u->focus_kind=XZ_UI_PROFILE;u->focus_index=0;u->focus_deck=-1;return 0;}
 }
 size_t result=touch_widgets(u,m,x,y,down,out,w,count);u->network_review.press_armed=0;return result;
}
/* A row is equal slots: stems, then the trailing control. Three stems in
   four slots is the USB grid, so a 3-stem PC row lines up with a USB row. */
static struct xz_ui_widget row_slot(int width,int slots,int slot,int y,int h,enum xz_ui_action_kind kind,int index,uint32_t requires,const char *label,int deck){
 int x=width*slot/slots,end=width*(slot+1)/slots;
 return (struct xz_ui_widget){x+1,y,end-x-2,h,kind,index,requires,label,deck};
}
/* Roles a PC row shows, left to right. An unreported mode shows the three
   roles both modes share; pc_role_available keeps them locked. */
static int pc_row_roles(const struct xz_ui_pc_stems *pc,int roles[XZ_UI_PC_ROLES]){
 int n=0;
 for(int slot=0;slot<XZ_UI_PC_ROLES;slot++)if(pc->available&(1u<<pc_slots[slot]))roles[n++]=pc_slots[slot];
 if(!n){roles[n++]=XZ_UI_PC_VOCAL;roles[n++]=XZ_UI_PC_INST;roles[n++]=XZ_UI_PC_DRUMS;}
 return n;
}
size_t xz_ui_inline_layout(const struct xz_ui *u,const struct xz_ui_model *m,int width,int height,struct xz_ui_widget w[XZ_UI_WIDGETS]){
 size_t n=0;
 if(!u||!m||width<400||width>800||(height!=XZ_WAVE_INLINE_HEIGHT&&height!=XZ_WAVE_HALF_CONTROL_HEIGHT*2)||u->deck<0||u->deck>1)return 0;
 for(int deck=0;deck<2;deck++){
  int row_height=height/2,y=deck*row_height+1,h=row_height-2;
  const struct xz_ui_pc_stems *pc=&m->deck[deck].pc_stems;
  if(pc->source==XZ_UI_SOURCE_LOCAL){
   /* Slots match the pads: A-C the saved order, BASS on D/H when four-stem. */
   int count=xz_ui_stem_count(m,deck);
   for(int slot=0;slot<count;slot++){
    int stem=xz_stem_pad_role(m->stem_order,count,slot);
    w[n++]=row_slot(width,count+1,slot,y,h,XZ_UI_MUTE,stem,XZ_UI_STEM,count==4?stems4[stem]:stems[stem],deck);
   }
   /* The trailing slot is BYPASS while stems play, else the deck's one reason:
      switched off turns straight on for every deck; a real fix opens setup at it. */
   enum xz_ui_usb_reason why=xz_ui_usb_blocked(m,deck);
   const char *fix=xz_ui_usb_action_label(why);
   if(usb_playable(m,deck))w[n++]=row_slot(width,count+1,count,y,h,XZ_UI_BYPASS,0,XZ_UI_STEM,"BYPASS",deck);
   else if(why==XZ_UI_USB_WHY_OFF&&xz_ui_usb_engine(m))w[n++]=row_slot(width,count+1,count,y,h,XZ_UI_ENABLE,XZ_UI_STEM,0,fix,deck);
   else w[n++]=row_slot(width,count+1,count,y,h,XZ_UI_LOCAL_STEM_SETUP_OPEN,0,0,fix?fix:xz_ui_usb_reason_text(why),deck);
   continue;
  }
  int roles[XZ_UI_PC_ROLES],count=pc_row_roles(pc,roles);
  for(int slot=0;slot<count;slot++)w[n++]=row_slot(width,count+1,slot,y,h,XZ_UI_PC_TOGGLE,roles[slot],0,pc_roles[roles[slot]],deck);
  w[n++]=row_slot(width,count+1,count,y,h,XZ_UI_PC_SETUP,XZ_UI_PC_WHY_TAG,0,
   pc->source==XZ_UI_SOURCE_PC?(pc->target>=0&&pc->target<4?pc_targets[pc->target]:"PC ?"):"SRC ?",deck);
 }
 return n;
}
size_t xz_ui_inline_touch(struct xz_ui *u,const struct xz_ui_model *m,int width,int height,int x,int y,int down,struct xz_ui_action out[XZ_UI_ACTIONS]){
 struct xz_ui_widget w[XZ_UI_WIDGETS];
 if(!u||!m)return 0;
 size_t count=xz_ui_inline_layout(u,m,width,height,w);
 if(down&&!u->down)for(size_t i=0;i<count;i++){
  struct xz_ui_widget a=w[i];
  int hit=x>=a.x&&x<a.x+a.w&&y>=a.y&&y<a.y+a.h;
  if(a.kind==XZ_UI_MUTE&&hit&&ready(u,m,a.deck,&a)){
   u->down=1;u->capture=(int)i;u->held_kind=XZ_UI_MUTE;u->held_deck=a.deck;u->held_index=a.index;
   u->touch_start_x=x;u->touch_start_level=(m->deck[a.deck].muted&(1u<<a.index))?0:m->deck[a.deck].levels[a.index];u->dragged=0;return 0;
  }
  if(a.kind==XZ_UI_PC_TOGGLE&&hit&&xz_ui_pc_blocked(m,a.deck)==XZ_UI_PC_WHY_NONE&&pc_role_available(m,a.deck,a.index)){
   u->down=1;u->notice[0]=0;u->notice_deck=a.deck;
   if(m->deck[a.deck].pc_stems.pending&(1u<<a.index)){
    /* One gesture per role until rekordbox answers or the send times out. */
    snprintf(u->notice,sizeof(u->notice),"%s SENT / WAITING FOR THE DJ SOFTWARE",a.label);u->capture=-1;return 0;
   }
   u->capture=(int)i;u->held_kind=XZ_UI_PC_TOGGLE;u->held_deck=a.deck;u->held_index=a.index;
   u->held_pc_target=m->deck[a.deck].pc_stems.target;u->held_pc_epoch=m->deck[a.deck].pc_stems.epoch;
   u->held_pc_available=m->deck[a.deck].pc_stems.available;
   u->touch_start_x=x;u->dragged=0;return 0;
  }
 }
 if(u->held_kind==XZ_UI_PC_TOGGLE&&u->capture>=0&&(size_t)u->capture<count){
  const struct xz_ui_pc_stems *pc=&m->deck[u->held_deck].pc_stems;
  /* A new role mask moves the buttons under the finger, so the gesture ends. */
  if(pc->source!=XZ_UI_SOURCE_PC||pc->target!=u->held_pc_target||pc->epoch!=u->held_pc_epoch||pc->available!=u->held_pc_available||
     xz_ui_pc_blocked(m,u->held_deck)!=XZ_UI_PC_WHY_NONE||!pc_role_available(m,u->held_deck,u->held_index)){
   struct xz_ui_action ignored[XZ_UI_ACTIONS];xz_ui_cancel(u,ignored);return 0;
  }
  int dx=x-u->touch_start_x;
  if(down){
   if(!u->dragged&&(dx>8||dx< -8)){
    u->dragged=1;snprintf(u->notice,sizeof(u->notice),"%s",xz_ui_pc_reason_text(XZ_UI_PC_WHY_NO_LEVEL));
   }
   return 0;
  }
  size_t n=0;if(!u->dragged){
   out[n++]=action(XZ_UI_PC_TOGGLE,XZ_UI_PRESS,u->held_deck,u->held_index,1,(float)u->held_pc_target);
   out[n-1].pc_epoch=u->held_pc_epoch;out[n-1].pc_target=u->held_pc_target;
  }
  struct xz_ui_action ignored[XZ_UI_ACTIONS];xz_ui_cancel(u,ignored);return n;
 }
 if(u->held_kind==XZ_UI_MUTE&&u->capture>=0&&(size_t)u->capture<count){
  struct xz_ui_widget a=w[u->capture];int dx=x-u->touch_start_x;
  if(down){
   if(dx>8||dx< -8)u->dragged=1;
   if(!u->dragged)return 0;
   out[0]=action(XZ_UI_LEVEL,XZ_UI_MOVE,u->held_deck,u->held_index,clampf(u->touch_start_level+(float)dx/(a.w-12),0,1),0);return 1;
  }
  size_t n=0;if(!u->dragged)out[n++]=action(XZ_UI_MUTE,XZ_UI_PRESS,u->held_deck,u->held_index,1,0);
  struct xz_ui_action ignored[XZ_UI_ACTIONS];xz_ui_cancel(u,ignored);return n;
 }
 return touch_widgets(u,m,x,y,down,out,w,count);
}
/* PC rows share one look across skins: theme palette, role colour, plain text. */
static void pc_row_render(struct xz_canvas c,const struct xz_ui *u,const struct xz_ui_model *m,const struct xz_ui_widget *w,size_t n,int deck){
 const struct xz_theme_palette *p=xz_theme_palette(m->theme);
 /* The analog skin paints paper, not its palette background; match ui_layout.c. */
 uint32_t ink=m->theme==XZ_THEME_ANALOG?0x3a3020:p->ink,bg=m->theme==XZ_THEME_ANALOG?0xd9c9a3:p->bg;
 const struct xz_ui_pc_stems *pc=&m->deck[deck].pc_stems;
 enum xz_ui_pc_reason why=xz_ui_pc_blocked(m,deck);
 uint32_t dim=xz_blend(bg,ink,110),panel=xz_blend(bg,ink,22);
 int feedback=(pc->caps&XZ_UI_PC_CAP_FEEDBACK)!=0,line_x=-1,line_end=0,line_y=0;
 int held=u->down&&!u->dragged&&u->held_kind==XZ_UI_PC_TOGGLE&&u->held_deck==deck?u->held_index:-1;
 xz_rect(c,0,deck*(c.height/2),c.width,c.height/2,bg);
 for(size_t i=0;i<n;i++){
  struct xz_ui_widget a=w[i];if(a.deck!=deck)continue;
  if(a.kind==XZ_UI_PC_SETUP){
   /* Ready: the source tag. Needs setup: the one primary button into setup. */
   int setup=why==XZ_UI_PC_WHY_SOURCE||why==XZ_UI_PC_WHY_UNVERIFIED,lost=why==XZ_UI_PC_WHY_LINK;
   unsigned roles=pc->available&15u;int count=0;for(unsigned r=roles;r;r&=r-1)count++;
   uint32_t edge=setup?p->accent:lost?p->alarm:xz_blend(bg,ink,100);
   xz_rect(c,a.x,a.y,a.w,a.h,setup?xz_blend(bg,p->accent,110):lost?xz_blend(bg,p->alarm,36):panel);xz_border(c,a.x,a.y,a.w,a.h,edge);
   /* Half-height rows drop the detail line, so a lost link names itself. */
   xz_text(c,a.x+6,a.y+5,setup?"SET UP":lost&&a.h<32?"NO LINK":a.label,a.h<32?1:2,(a.w-12)/(a.h<32?6:12),ink);
   char count_text[12];snprintf(count_text,sizeof(count_text),"%d STEMS",count);
   const char *detail=setup?a.label:lost?"NO LINK":count==3||count==4?count_text:"SETUP";
   if(a.h>=32){
    xz_text(c,a.x+8,a.y+a.h-14,detail,1,(a.w-36)/6,lost?p->alarm:ink);
    xz_text(c,a.x+a.w-20,a.y+a.h-14,deck?"D2":"D1",1,2,ink);
   }
  }else if(a.kind==XZ_UI_PC_TOGGLE){
   unsigned bit=1u<<a.index;uint32_t role=xz_ui_model_pc_role_color(m,a.index);
   int unavailable=!pc_role_available(m,deck,a.index),locked=why!=XZ_UI_PC_WHY_NONE||unavailable;
   /* Pending keeps the last known state on screen; only the marker changes. */
   int pending=(pc->pending&bit)!=0,known=feedback&&(pc->known&bit),lit=known&&(pc->active&bit);
   int pressed=!locked&&held==a.index;
   uint32_t fill=locked?xz_blend(bg,ink,12):lit?xz_blend(bg,role,150):known?xz_blend(bg,role,32):panel;
   if(pressed)fill=xz_blend(fill,role,96);
   xz_rect(c,a.x,a.y,a.w,a.h,fill);
   xz_border(c,a.x,a.y,a.w,a.h,locked?dim:role);
   if(pressed)xz_border(c,a.x+1,a.y+1,a.w-2,a.h-2,ink);
   if(!locked){
    if(pending)for(int x=a.x+1;x<a.x+a.w-1;x+=8)xz_rect(c,x,a.y+1,x+4<a.x+a.w-1?4:a.x+a.w-1-x,3,role);
    else xz_rect(c,a.x+1,a.y+1,a.w-2,3,role);
   }
   xz_text(c,a.x+6,a.y+(a.h<32?5:6),a.label,a.h<32?1:2,(a.w-12)/(a.h<32?6:12),locked?dim:ink);
   if(locked){if(line_x<0){line_x=a.x+8;line_y=a.y+a.h-14;}line_end=a.x+a.w-8;}
   else if(a.h>=32)xz_text(c,a.x+8,a.y+a.h-14,pending?"SENT":known?(lit?"LED ON":"LED OFF"):"TOGGLE",1,(a.w-16)/6,!known?dim:ink);
  }
 }
 const char *line=u->notice[0]&&u->notice_deck==deck?u->notice:why!=XZ_UI_PC_WHY_NONE?pc_reason(m,deck,why):NULL;
 if(line&&(c.height>=64||(u->notice[0]&&u->notice_deck==deck))){
  /* Over the locked roles if any, else across the role buttons' status line. */
  if(line_x<0){
   line_x=8;line_y=deck*(c.height/2)+(c.height<64?6:c.height/2-15);line_end=8;
   for(size_t i=0;i<n;i++)if(w[i].deck==deck&&w[i].kind==XZ_UI_PC_TOGGLE)line_end=w[i].x+w[i].w-8;
  }
  xz_rect(c,line_x-3,line_y-2,line_end-line_x+6,12,bg);
  xz_text(c,line_x,line_y,line,1,(line_end-line_x)/6,why==XZ_UI_PC_WHY_LINK?p->alarm:ink);
 }
}
int xz_ui_inline_render(const struct xz_ui *u,const struct xz_ui_model *m,uint16_t *pixels,size_t count,size_t stride,int width,int height){
 struct xz_ui_widget w[XZ_UI_WIDGETS],local[XZ_UI_WIDGETS];struct xz_ui_look looks[XZ_UI_WIDGETS];struct xz_theme_palette palette;
 if(!u||!m||!pixels||height<=0||width<=0||stride<(size_t)width||stride>count/(size_t)height)return 0;
 size_t n=xz_ui_inline_layout(u,m,width,height,w);if(!n)return 0;
 /* Skins draw local rows exactly as before; PC rows are drawn over them here. */
 size_t local_count=0;for(size_t i=0;i<n;i++)if(w[i].kind==XZ_UI_MUTE||w[i].kind==XZ_UI_BYPASS||w[i].kind==XZ_UI_LOCAL_STEM_SETUP_OPEN||w[i].kind==XZ_UI_ENABLE)local[local_count++]=w[i];
 struct xz_canvas c={pixels,stride,width,height,m->theme};
 /* With no local row the skin would only paint a background the PC rows cover. */
 if(local_count){
  struct xz_ui_scene s=scene(u,m,c,&m->deck[0],local,looks,local_count,&palette);
  skin_for(m->theme)->render_inline(&s);
 }
 for(int deck=0;deck<2;deck++)if(m->deck[deck].pc_stems.source!=XZ_UI_SOURCE_LOCAL)pc_row_render(c,u,m,w,n,deck);
 return 1;
}
