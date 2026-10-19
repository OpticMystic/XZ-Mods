#include "shell_internal.h"
#include "shell_settings.h"
#include "shell_browse.h"
#include "shell_source.h"
#include <string.h>

static const char *const screen_names[XZ_SHELL_SCREEN_COUNT]={
 "PLAY","BROWSE","PLAYLIST","TAG LIST","SEARCH","INFO","SHORTCUT","UTILITY","SOURCE"
};
const char *xz_shell_screen_name(enum xz_shell_screen s) { return s<XZ_SHELL_SCREEN_COUNT?screen_names[s]:"?"; }

/* What a screen needs before it can be drawn. A missing slot or capability
   hands the screen to the stock UI. */
enum slot { SLOT_NONE, SLOT_BROWSE, SLOT_SETTINGS, SLOT_SOURCE, SLOT_INFO };
static const struct { enum slot slot; uint32_t caps; } needs[XZ_SHELL_SCREEN_COUNT]={
 [XZ_SHELL_PLAY]={SLOT_NONE,0},
 [XZ_SHELL_BROWSE]={SLOT_BROWSE,XZ_SHELL_CAP_BROWSE},
 [XZ_SHELL_PLAYLIST]={SLOT_BROWSE,XZ_SHELL_CAP_BROWSE},
 [XZ_SHELL_TAG_LIST]={SLOT_BROWSE,XZ_SHELL_CAP_TAG_LIST},
 [XZ_SHELL_SEARCH]={SLOT_BROWSE,XZ_SHELL_CAP_SEARCH},
 [XZ_SHELL_INFO]={SLOT_INFO,0},
 [XZ_SHELL_SHORTCUT]={SLOT_SETTINGS,0},
 [XZ_SHELL_UTILITY]={SLOT_SETTINGS,0},
 [XZ_SHELL_SOURCE]={SLOT_SOURCE,XZ_SHELL_CAP_SOURCE},
};
static int slot_present(const struct xz_shell_model *m, enum slot s) {
 switch(s){
 case SLOT_BROWSE:return m->browse!=NULL;
 case SLOT_SETTINGS:return m->settings_screen!=NULL;
 case SLOT_SOURCE:return m->source!=NULL;
 case SLOT_INFO:return m->info!=NULL;
 default:return 1;
 }
}
static int available(const struct xz_shell_model *m, enum xz_shell_screen s) {
 return slot_present(m,needs[s].slot)&&(m->capabilities&needs[s].caps)==needs[s].caps;
}

/* Navigation: one stack of screens, screen x key -> transition (spec section 3.1).
   OPEN replaces everything above PLAY (list keys toggle against PLAY); BROWSE
   and PLAYLIST open with the SOURCE picker beneath them, so BACK from a
   list's top level lands on the picker. PUSH layers over the current screen
   (INFO, SHORTCUT, UTILITY), POP goes up one level, HOME to the top. STICK
   browses one USB stick. PANEL toggles a list screen's slide-over panel.
   A short MENU opens MODS on the first mod tab wherever no MENU panel or
   MODS itself takes it (the stock deck does nothing on PLAY); held MENU
   opens it on UTILITY. BACK runs sh_back: the top
   screen's own levels first, then POP. */
enum nav_kind { NAV_NONE, NAV_OPEN, NAV_PUSH, NAV_POP, NAV_HOME, NAV_HAND_OFF, NAV_SETTING, NAV_TAG, NAV_STICK, NAV_PANEL, NAV_FILTER, NAV_MODS };
/* A transition packs its kind in the low nibble and its screen or setting above it. */
#define NAV(kind,arg) ((uint16_t)((kind)|((arg)<<4)))
#define OPEN(s) NAV(NAV_OPEN,s)
#define PUSH(s) NAV(NAV_PUSH,s)
#define POP NAV(NAV_POP,0)
#define HOME NAV(NAV_HOME,0)
#define NONE NAV(NAV_NONE,0)
#define HAND_OFF NAV(NAV_HAND_OFF,0)
#define PANEL(k) NAV(NAV_PANEL,k)
#define MODS NAV(NAV_MODS,0)
/* ROW re-scans its arguments so LISTS can stand for the five list keys. */
#define ROW(...) ROW_(__VA_ARGS__)
#define ROW_(browse,taglist,playlist,search,source,info,shortcut,utility,menu,back) {  [XZ_SHELL_KEY_BACK]=back, [XZ_SHELL_KEY_BACK_HOLD]=HOME, [XZ_SHELL_KEY_BROWSE]=browse,  [XZ_SHELL_KEY_TAG_LIST]=taglist, [XZ_SHELL_KEY_PLAYLIST]=playlist, [XZ_SHELL_KEY_SEARCH]=search,  [XZ_SHELL_KEY_MENU]=menu, [XZ_SHELL_KEY_MENU_HOLD]=utility, [XZ_SHELL_KEY_SHORTCUT]=shortcut,  [XZ_SHELL_KEY_INFO]=info, [XZ_SHELL_KEY_SOURCE]=source, [XZ_SHELL_KEY_TAG_TRACK]=NAV(NAV_TAG,0), [XZ_SHELL_KEY_TAG_TRACK_HOLD]=NAV(NAV_TAG,1),  [XZ_SHELL_KEY_TIME_MODE]=NAV(NAV_SETTING,XZ_SHELL_SET_TIME_MODE), [XZ_SHELL_KEY_QUANTIZE]=NAV(NAV_SETTING,XZ_SHELL_SET_QUANTIZE),  [XZ_SHELL_KEY_SOURCE_USB1]=NAV(NAV_STICK,XZ_SHELL_SRC_USB1), [XZ_SHELL_KEY_SOURCE_USB2]=NAV(NAV_STICK,XZ_SHELL_SRC_USB2),  [XZ_SHELL_KEY_TRACK_FILTER]=NAV(NAV_FILTER,0), [XZ_SHELL_KEY_SOURCE_LINK]=NAV(NAV_STICK,XZ_SHELL_SRC_LINK),  [XZ_SHELL_KEY_SOURCE_COMPUTER]=NAV(NAV_STICK,XZ_SHELL_SRC_COMPUTER) }
#define LISTS OPEN(XZ_SHELL_BROWSE),OPEN(XZ_SHELL_TAG_LIST),OPEN(XZ_SHELL_PLAYLIST),OPEN(XZ_SHELL_SEARCH),OPEN(XZ_SHELL_SOURCE)
static const uint16_t nav[XZ_SHELL_SCREEN_COUNT][XZ_SHELL_KEY_COUNT]={
 /*                     browse                 taglist                 playlist                search                 source                 info                shortcut                utility                 menu      back */
 [XZ_SHELL_PLAY]    =ROW(OPEN(XZ_SHELL_BROWSE),OPEN(XZ_SHELL_TAG_LIST),OPEN(XZ_SHELL_PLAYLIST),OPEN(XZ_SHELL_SEARCH),OPEN(XZ_SHELL_SOURCE),PUSH(XZ_SHELL_INFO),PUSH(XZ_SHELL_SHORTCUT),PUSH(XZ_SHELL_UTILITY),MODS,    NONE),
 [XZ_SHELL_BROWSE]  =ROW(HOME,                 OPEN(XZ_SHELL_TAG_LIST),OPEN(XZ_SHELL_PLAYLIST),OPEN(XZ_SHELL_SEARCH),OPEN(XZ_SHELL_SOURCE),PANEL(XZ_SHELL_PANEL_INFO),PUSH(XZ_SHELL_SHORTCUT),PUSH(XZ_SHELL_UTILITY),PANEL(XZ_SHELL_PANEL_MENU),POP),
 [XZ_SHELL_PLAYLIST]=ROW(OPEN(XZ_SHELL_BROWSE),OPEN(XZ_SHELL_TAG_LIST),HOME,                   OPEN(XZ_SHELL_SEARCH),OPEN(XZ_SHELL_SOURCE),PANEL(XZ_SHELL_PANEL_INFO),PUSH(XZ_SHELL_SHORTCUT),PUSH(XZ_SHELL_UTILITY),PANEL(XZ_SHELL_PANEL_MENU),POP),
 [XZ_SHELL_TAG_LIST]=ROW(OPEN(XZ_SHELL_BROWSE),HOME,                   OPEN(XZ_SHELL_PLAYLIST),OPEN(XZ_SHELL_SEARCH),OPEN(XZ_SHELL_SOURCE),PANEL(XZ_SHELL_PANEL_INFO),PUSH(XZ_SHELL_SHORTCUT),PUSH(XZ_SHELL_UTILITY),PANEL(XZ_SHELL_PANEL_MENU),POP),
 [XZ_SHELL_SEARCH]  =ROW(OPEN(XZ_SHELL_BROWSE),OPEN(XZ_SHELL_TAG_LIST),OPEN(XZ_SHELL_PLAYLIST),HOME,                 OPEN(XZ_SHELL_SOURCE),PANEL(XZ_SHELL_PANEL_INFO),PUSH(XZ_SHELL_SHORTCUT),PUSH(XZ_SHELL_UTILITY),PANEL(XZ_SHELL_PANEL_MENU),POP),
 [XZ_SHELL_SOURCE]  =ROW(OPEN(XZ_SHELL_BROWSE),OPEN(XZ_SHELL_TAG_LIST),OPEN(XZ_SHELL_PLAYLIST),OPEN(XZ_SHELL_SEARCH),HOME,                 PUSH(XZ_SHELL_INFO),PUSH(XZ_SHELL_SHORTCUT),PUSH(XZ_SHELL_UTILITY),MODS,    POP),
 [XZ_SHELL_INFO]    =ROW(LISTS,                                                                                                            POP,                PUSH(XZ_SHELL_SHORTCUT),PUSH(XZ_SHELL_UTILITY),MODS,    POP),
 [XZ_SHELL_SHORTCUT]=ROW(LISTS,                                                                                                            PUSH(XZ_SHELL_INFO),POP,                    PUSH(XZ_SHELL_UTILITY),MODS,    POP),
 [XZ_SHELL_UTILITY] =ROW(LISTS,                                                                                                            PUSH(XZ_SHELL_INFO),PUSH(XZ_SHELL_SHORTCUT),PUSH(XZ_SHELL_UTILITY),POP,     POP),
};

static void placeholder_render(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 (void)m;
 sh_placeholder(c,xz_shell_screen_name(xz_shell_screen(u)),"PLACEHOLDER: THIS SCREEN IS DRAWN BY A LATER UNIT");
}
static size_t placeholder_input(struct xz_shell *u, const struct xz_shell_model *m, const struct xz_shell_event *e, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 (void)u;(void)m;(void)e;(void)out;return 0;
}
const struct sh_screen xz_shell_placeholder_screen={placeholder_render,placeholder_input};
/* Each screen family owns one file; until it is built that file exports xz_shell_placeholder_screen. */
static const struct sh_screen *const screens[XZ_SHELL_SCREEN_COUNT]={
 [XZ_SHELL_PLAY]=&xz_shell_play_screen,
 [XZ_SHELL_BROWSE]=&xz_shell_browse_screen, [XZ_SHELL_PLAYLIST]=&xz_shell_playlist_screen, [XZ_SHELL_TAG_LIST]=&xz_shell_tag_list_screen,
 [XZ_SHELL_SEARCH]=&xz_shell_search_screen, [XZ_SHELL_INFO]=&xz_shell_info_screen, [XZ_SHELL_SHORTCUT]=&xz_shell_shortcut_screen,
 [XZ_SHELL_UTILITY]=&xz_shell_utility_screen, [XZ_SHELL_SOURCE]=&xz_shell_source_screen,
};

/* List screens answer whether they are below their top level; the BACK key then goes to their input. */
static int (*const lists_back[XZ_SHELL_SCREEN_COUNT])(struct xz_shell *, const struct xz_shell_model *)={
 [XZ_SHELL_BROWSE]=xz_shell_browse_back, [XZ_SHELL_PLAYLIST]=xz_shell_browse_back,
 [XZ_SHELL_TAG_LIST]=xz_shell_browse_back, [XZ_SHELL_SEARCH]=xz_shell_browse_back,
};
/* A screen file that is still a stub exports an empty entry; it renders as the placeholder. */
static const struct sh_screen *screen_of(const struct xz_shell *u) {
 const struct sh_screen *s=screens[xz_shell_screen(u)];
 return s->render&&s->input?s:&xz_shell_placeholder_screen;
}
void xz_shell_init(struct xz_shell *u) {
 memset(u,0,sizeof *u);
 u->stack[0].screen=XZ_SHELL_PLAY;u->depth=1;
 u->zoom=XZ_SHELL_ZOOM_DEFAULT;
 u->display=(struct xz_shell_display){XZ_SHELL_WAVE_3BAND,XZ_SHELL_POSITION_CENTER,XZ_SHELL_KEY_AS_DECK,XZ_SHELL_USB_COLORS_ALL};
}
int xz_shell_restore_setting(struct xz_shell *u, int id, int value) {
 if(!u)return 0;
 switch(id){
 case XZ_SHELL_SET_WAVE_COLOR:if(value<XZ_SHELL_WAVE_BLUE||value>XZ_SHELL_WAVE_STEMS)return 0;u->display.wave_color=(enum xz_shell_wave_color)value;return 1;
 case XZ_SHELL_SET_WAVE_POSITION:if(value!=XZ_SHELL_POSITION_CENTER&&value!=XZ_SHELL_POSITION_LEFT)return 0;u->display.wave_position=(enum xz_shell_wave_position)value;return 1;
 case XZ_SHELL_ITEM_KEY_DISPLAY:if(value<XZ_SHELL_KEY_AS_DECK||value>XZ_SHELL_KEY_ALPHANUMERIC)return 0;u->display.key_display=(enum xz_shell_key_display)value;return 1;
 case XZ_SHELL_ITEM_WAVE_ZOOM:if(value<0||value>XZ_SHELL_ZOOM_MAX)return 0;u->zoom=value;return 1;
 case XZ_SHELL_ITEM_USB_COLORS:if(value<XZ_SHELL_USB_COLORS_ALL||value>XZ_SHELL_USB_COLORS_OFF)return 0;u->display.usb_colors=(enum xz_shell_usb_colors)value;return 1;
 default:return 0;
 }
}

/* ---- mod header buttons, over every screen ---- */
enum xz_shell_action_kind sh_header_hit(int x, int y) {
 if(y<0||y>=XZ_SHELL_HEADER_HIT_H||x<XZ_SHELL_STEMS_X||x>=XZ_SHELL_WIDTH)return XZ_SHELL_ACT_NONE;
 /* The 2 px gap between the buttons splits between them. */
 return x<XZ_SHELL_MODS_X-1?XZ_SHELL_ACT_TOGGLE_STEMS:XZ_SHELL_ACT_OPEN_MODS;
}
static void header_button(struct xz_canvas c, int x, int w, const char *label, int lit) {
 int h=XZ_SHELL_HEADER_BUTTON_H;
 xz_rect(c,x,0,w,h,lit?SH_BLUE:XZ_FLAGSHIP_TILE);
 xz_rect(c,x,h-1,w,1,SH_BLACK);
 /* The flagship's quick-menu tiles carry an indicator bar under the label. */
 xz_rect(c,x+8,h-5,w-16,2,lit?SH_WHITE:XZ_FLAGSHIP_RAIL_SELECTED);
 sh_text_center(c,x+w/2,5,label,SH_SMALL,SH_WHITE);
}
void sh_header_buttons(const struct xz_shell_model *m, struct xz_canvas c) {
 xz_rect(c,XZ_SHELL_HEADER_X-2,0,XZ_SHELL_WIDTH-XZ_SHELL_HEADER_X+2,XZ_SHELL_HEADER_BUTTON_H,SH_CHROME);
 header_button(c,XZ_SHELL_STEMS_X,XZ_SHELL_STEMS_W,"STEMS",m->stems_known&&m->stems_on);
 /* Five marks under STEMS, one per stem row layout, as the classic STEMS button shows its choice. */
 if(m->stem_layout_known&&(unsigned)m->stem_layout<XZ_SHELL_STEM_LAYOUTS){
  int h=XZ_SHELL_HEADER_BUTTON_H,x=XZ_SHELL_STEMS_X+8,w=XZ_SHELL_STEMS_W-16,mw=(w-4*2)/5;
  int lit=m->stems_known&&m->stems_on;
  xz_rect(c,x,h-5,w,2,lit?SH_BLUE:XZ_FLAGSHIP_TILE);
  for(int i=0;i<XZ_SHELL_STEM_LAYOUTS;i++)xz_rect(c,x+i*(mw+2),h-5,mw,2,i==(int)m->stem_layout?SH_WHITE:lit?0x6ea8e8:XZ_FLAGSHIP_RAIL_SELECTED);
 }
 header_button(c,XZ_SHELL_MODS_X,XZ_SHELL_MODS_W,"MODS",0);
}
enum xz_shell_screen xz_shell_screen(const struct xz_shell *u) { return u->stack[u->depth-1].screen; }

static void push(struct xz_shell *u, enum xz_shell_screen s) {
 if(u->depth>=XZ_SHELL_STACK)return;
 u->stack[u->depth]=(struct xz_shell_view){s,0,0,0};u->depth++;
}
static void close_panels(struct xz_shell *u) { memset(&u->panel,0,sizeof u->panel);u->grab.active=0; }
/* A source-rooted list opens over the SOURCE picker, when there is one, so BACK reaches it. */
static int source_rooted(enum xz_shell_screen s) { return s==XZ_SHELL_BROWSE||s==XZ_SHELL_PLAYLIST; }
static void open_screen(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_screen s) {
 u->depth=1;close_panels(u);
 if(source_rooted(s)&&available(m,XZ_SHELL_SOURCE))push(u,XZ_SHELL_SOURCE);
 push(u,s);
}
static int device_ready(const struct xz_shell_model *m, int slot) {
 const struct xz_shell_source_model *sm=m->source;
 if(!sm||slot<XZ_SHELL_SRC_USB1||slot>XZ_SHELL_SRC_COMPUTER)return 0;
 const struct xz_shell_device *d=&sm->device[slot-1];
 return ((d->known>>XZ_SHELL_D_STATUS)&1)&&d->status==XZ_SHELL_DEVICE_READY;
}
static int on_stack(const struct xz_shell *u, enum xz_shell_screen s) {
 for(int i=0;i<u->depth;i++)if(u->stack[i].screen==s)return 1;
 return 0;
}
/* The MODS screen (XZ_SHELL_UTILITY) on a page, with the encoder on its rail. Already open, it
   switches tab in place; lower in the stack, the screens above it close. */
static void open_mods(struct xz_shell *u, int tab) {
 int at=-1;
 for(int i=0;i<u->depth;i++)if(u->stack[i].screen==XZ_SHELL_UTILITY)at=i;
 if(at>=0)u->depth=at+1;else push(u,XZ_SHELL_UTILITY);
 if(xz_shell_screen(u)!=XZ_SHELL_UTILITY)return;
 if(u->mods_view.tab!=tab)u->stack[u->depth-1]=(struct xz_shell_view){XZ_SHELL_UTILITY,0,0,0};
 u->mods_view.tab=tab;u->mods_view.on_tabs=1;
}
static size_t emit(struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t n, struct xz_shell_action a) {
 if(n<XZ_SHELL_ACTIONS)out[n++]=a;
 return n;
}
static enum xz_shell_time_mode shown_time_mode(const struct xz_shell_model *m, int deck) {
 const struct xz_shell_deck *d=&m->deck[deck];
 if(sh_has(d,XZ_SHELL_F_TIME_MODE))return d->time_mode;
 if(sh_setting(m,XZ_SHELL_SET_TIME_MODE))return m->settings.time_mode;
 return XZ_SHELL_TIME_REMAIN;
}
/* A USB button: that stick's browser at its top level when it is ready, else the picker with that
   slot shown empty (the view's focus pins the cursor to a slot that is not connected). */
static size_t stick(struct xz_shell *u, const struct xz_shell_model *m, int slot, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 size_t n=0;
 if(device_ready(m,slot)&&(m->capabilities&XZ_SHELL_CAP_SOURCE)&&(m->capabilities&XZ_SHELL_CAP_BROWSE)){
  n=emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_SET_SOURCE,-1,0,slot});
  u->depth=1;close_panels(u);
  if(available(m,XZ_SHELL_SOURCE)){push(u,XZ_SHELL_SOURCE);u->stack[u->depth-1].cursor=slot;}
  push(u,XZ_SHELL_BROWSE);
  return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_BROWSE,-1,0,XZ_SHELL_BROWSE_TOP});
 }
 if(!available(m,XZ_SHELL_SOURCE))return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_HAND_OFF,-1,0,XZ_SHELL_SOURCE});
 u->depth=1;close_panels(u);push(u,XZ_SHELL_SOURCE);
 u->stack[1].cursor=slot;u->stack[1].focus=1;
 return n;
}
/* A key that cannot act here says why instead of dying silently. */
static void say(struct xz_shell *u, const struct xz_shell_model *m, const char *text) { u->notice=text;u->notice_at_ms=m->clock_ms; }
static int list_screen_of(enum xz_shell_screen s) { return s>=XZ_SHELL_BROWSE&&s<=XZ_SHELL_SEARCH; }
/* TRACK FILTER: the FILTER panel on a list, or BROWSE with it open; the stock browser without the capability. */
static size_t track_filter(struct xz_shell *u, const struct xz_shell_model *m, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 if(!(m->capabilities&XZ_SHELL_CAP_FILTER)||!available(m,XZ_SHELL_BROWSE)){
  say(u,m,"TRACK FILTER OPENS IN THE DECK'S OWN BROWSER");
  return emit(out,0,(struct xz_shell_action){XZ_SHELL_ACT_HAND_OFF,-1,0,XZ_SHELL_BROWSE});
 }
 if(!list_screen_of(xz_shell_screen(u)))open_screen(u,m,XZ_SHELL_BROWSE);
 enum xz_shell_panel_kind was=u->panel.kind;
 close_panels(u);
 if(was!=XZ_SHELL_PANEL_FILTER)u->panel.kind=XZ_SHELL_PANEL_FILTER;
 return 0;
}
/* Panels belong to the list screens; the same key closes the panel it opened. MENU on a USB stick's
   BROWSE with the category rail focused is that stick's MY SETTINGS panel. */
static int my_settings_here(const struct xz_shell *u, const struct xz_shell_model *m) {
 const struct xz_shell_my_settings_model *s=m->my_settings;
 return s&&(s->source==XZ_SHELL_SRC_USB1||s->source==XZ_SHELL_SRC_USB2||(s->source==XZ_SHELL_SRC_LINK&&s->link_player>0))
  &&sh_browse_at_root(u);
}
static void toggle_panel(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_panel_kind k) {
 if(k==XZ_SHELL_PANEL_MENU&&(my_settings_here(u,m)||u->panel.kind==XZ_SHELL_PANEL_MY_SETTINGS))k=XZ_SHELL_PANEL_MY_SETTINGS;
 enum xz_shell_panel_kind was=u->panel.kind;
 close_panels(u);
 if(was!=k)u->panel.kind=k;
}
size_t sh_key(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_key k, int event_deck, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 size_t n=0;
 if((int)k<0||k>=XZ_SHELL_KEY_COUNT)return 0;
 if(k==XZ_SHELL_KEY_BROWSE_HOLD){
  if(xz_shell_screen(u)==XZ_SHELL_SEARCH)return 0;
  k=XZ_SHELL_KEY_SEARCH;
 }
 if(u->popup.kind!=XZ_SHELL_POPUP_NONE&&(k==XZ_SHELL_KEY_BACK||k==XZ_SHELL_KEY_BACK_HOLD||k==XZ_SHELL_KEY_MENU)){
  u->popup.kind=XZ_SHELL_POPUP_NONE;return 0;
 }
 uint16_t packed=nav[xz_shell_screen(u)][k];
 struct { enum nav_kind kind; enum xz_shell_screen screen; enum xz_shell_setting setting; } t={(enum nav_kind)(packed&15),(enum xz_shell_screen)(packed>>4),(enum xz_shell_setting)(packed>>4)};
 int deck=event_deck>=0&&event_deck<XZ_SHELL_DECKS?event_deck:u->deck;
 switch(t.kind){
 case NAV_NONE:break;
 case NAV_OPEN:
  if(!available(m,t.screen))return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_HAND_OFF,-1,0,(int)t.screen});
  open_screen(u,m,t.screen);break;
 case NAV_PUSH:
  /* Held MENU lands on MODS > UTILITY, the rail holding the encoder; held there again it closes
     MODS, as SHORTCUT does, so the key always has an effect and never reaches the stock menu. */
  if(t.screen==XZ_SHELL_UTILITY&&available(m,t.screen)){
   if(xz_shell_screen(u)==XZ_SHELL_UTILITY&&u->mods_view.tab==0&&u->mods_view.on_tabs&&u->depth>1)u->depth--;
   else open_mods(u,0);
   return n;
  }
  if(on_stack(u,t.screen))break;
  if(!available(m,t.screen))return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_HAND_OFF,-1,0,(int)t.screen});
  push(u,t.screen);break;
 case NAV_POP:if(u->depth>1)u->depth--;close_panels(u);break;
 case NAV_HOME:u->depth=1;close_panels(u);break;
 case NAV_STICK:return stick(u,m,(int)(packed>>4),out);
 case NAV_PANEL:toggle_panel(u,m,(enum xz_shell_panel_kind)(packed>>4));break;
 case NAV_MODS:
  if(!available(m,XZ_SHELL_UTILITY))break;
  close_panels(u);
  open_mods(u,m->mods&&m->mods->tabs&&m->mods->count>0?1:0);
  break;
 case NAV_FILTER:return track_filter(u,m,out);
 case NAV_HAND_OFF:return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_HAND_OFF,-1,0,XZ_SHELL_HAND_OFF_MENU});
 case NAV_SETTING:
  if(!sh_writable(m,t.setting))break;
  if(t.setting==XZ_SHELL_SET_TIME_MODE)
   return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_SET_SETTING,deck,shown_time_mode(m,deck)==XZ_SHELL_TIME_REMAIN?XZ_SHELL_TIME_ELAPSED:XZ_SHELL_TIME_REMAIN,XZ_SHELL_SET_TIME_MODE});
  if(t.setting==XZ_SHELL_SET_QUANTIZE){
   const struct xz_shell_deck *d=&m->deck[deck];
   int on=sh_has(d,XZ_SHELL_F_QUANTIZE)?d->quantize_on:0;
   return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_SET_SETTING,deck,!on,XZ_SHELL_SET_QUANTIZE});
  }
  break;
 case NAV_TAG:{
  /* On a list with tag editing the key acts on the highlighted track: added, or removed when it is
     already on the TAG LIST; held on the TAG LIST it removes (XZ manual pp.86, 88). */
  int hold=(int)(packed>>4);
  enum xz_shell_screen here=xz_shell_screen(u);
  if((m->capabilities&XZ_SHELL_CAP_TAG_EDIT)&&list_screen_of(here)){
   const struct xz_shell_browse_row *r=xz_shell_browse_highlight(u,m);
   if(r&&r->kind==XZ_SHELL_ROW_TRACK&&((r->known>>XZ_SHELL_RF_TAGGED)&1)){
    if(hold&&here!=XZ_SHELL_TAG_LIST)break;
    int remove=r->tagged||here==XZ_SHELL_TAG_LIST;
    return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_TAG_LIST,-1,(int32_t)r->id,remove?XZ_SHELL_TAG_REMOVE:XZ_SHELL_TAG_ADD});
   }
  }
  if(hold)break;
  if(m->capabilities&XZ_SHELL_CAP_TAG_TRACK)return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_TAG_TRACK,deck,0,0});
  /* No capability: no change here, so the runtime's key probe sees an inert key and decides (stock tags the
     loaded track from PLAY; on a list it swallows the key and says why). */
  break;
 }
 }
 return n;
}
/* BACK, the one way up, for the physical key and every on-screen back control: a popup closes first,
   then the top screen steps up one of its own levels (a panel, a list level, a UTILITY pane), then
   the stack pops one screen. */
static size_t (*const back_steps[XZ_SHELL_SCREEN_COUNT])(struct xz_shell *, const struct xz_shell_model *, struct xz_shell_action out[XZ_SHELL_ACTIONS], int *taken)={
 [XZ_SHELL_BROWSE]=sh_browse_back_step, [XZ_SHELL_PLAYLIST]=sh_browse_back_step,
 [XZ_SHELL_TAG_LIST]=sh_browse_back_step, [XZ_SHELL_SEARCH]=sh_browse_back_step,
 [XZ_SHELL_UTILITY]=sh_settings_back_step, [XZ_SHELL_SOURCE]=sh_source_back_step,
};
size_t sh_back(struct xz_shell *u, const struct xz_shell_model *m, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 if(u->popup.kind!=XZ_SHELL_POPUP_NONE){u->popup.kind=XZ_SHELL_POPUP_NONE;return 0;}
 enum xz_shell_screen s=xz_shell_screen(u);
 if(back_steps[s]){
  int taken=0;
  size_t n=back_steps[s](u,m,out,&taken);
  if(taken)return n;
 }
 if(u->depth>1)u->depth--;
 close_panels(u);
 return 0;
}
size_t xz_shell_input(struct xz_shell *u, const struct xz_shell_model *m, const struct xz_shell_event *e, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 if(!u||!m||!e||!out)return 0;
 /* The header buttons sit over every screen and popup; the rest of their touch stays with them. */
 if(e->kind==XZ_SHELL_TOUCH_DOWN){
  enum xz_shell_action_kind hit=sh_header_hit(e->x,e->y);
  if(hit!=XZ_SHELL_ACT_NONE){
   u->down=1;u->down_region=XZ_SHELL_TOUCH_HEADER;
   /* With mod tabs to show, MODS opens the shell's own MODS screen on the first of them. */
   if(hit==XZ_SHELL_ACT_OPEN_MODS&&m->mods&&m->mods->count>0&&m->mods->tabs&&available(m,XZ_SHELL_UTILITY)){
    close_panels(u);open_mods(u,1);
    return 0;
   }
   return emit(out,0,(struct xz_shell_action){hit,-1,0,0});
  }
 }
 if((e->kind==XZ_SHELL_TOUCH_MOVE||e->kind==XZ_SHELL_TOUCH_UP)&&u->down&&u->down_region==XZ_SHELL_TOUCH_HEADER){
  if(e->kind==XZ_SHELL_TOUCH_UP){u->down=0;u->down_region=XZ_SHELL_TOUCH_NONE;}
  return 0;
 }
 /* LOAD belongs only to list input; all other screens leave it inert. */
 if(e->kind==XZ_SHELL_KEY&&e->key==XZ_SHELL_KEY_LOAD&&lists_back[xz_shell_screen(u)])
  return screen_of(u)->input(u,m,e,out);
 if(e->kind==XZ_SHELL_KEY&&(e->key==XZ_SHELL_KEY_BACK||e->key==XZ_SHELL_KEY_BACK_HOLD)&&u->popup.kind==XZ_SHELL_POPUP_NONE){
  enum xz_shell_screen s=xz_shell_screen(u);
  if(e->key==XZ_SHELL_KEY_BACK)return sh_back(u,m,out);
  /* A list below its top level goes to its top (BACK_HOLD) through its own input, which emits the browse action. */
  close_panels(u);
  if(lists_back[s]&&lists_back[s](u,m))return screen_of(u)->input(u,m,e,out);
 }
 if(e->kind==XZ_SHELL_KEY)return sh_key(u,m,e->key,e->deck,out);
 if(u->popup.kind!=XZ_SHELL_POPUP_NONE){
  if(e->kind==XZ_SHELL_TOUCH_DOWN||e->kind==XZ_SHELL_ENCODER_PUSH)u->popup.kind=XZ_SHELL_POPUP_NONE;
  return 0;
 }
 return screen_of(u)->input(u,m,e,out);
}
int xz_shell_render(const struct xz_shell *u, const struct xz_shell_model *m, uint16_t *pixels, size_t stride) {
 if(!u||!m||!pixels||stride<XZ_SHELL_WIDTH||u->depth<1||u->depth>XZ_SHELL_STACK)return 0;
 struct xz_canvas c={pixels,stride,XZ_SHELL_WIDTH,XZ_SHELL_HEIGHT,XZ_THEME_FLAGSHIP};
 screen_of(u)->render(u,m,c);
 if(u->popup.kind!=XZ_SHELL_POPUP_NONE){
  xz_rect(c,200,140,400,200,SH_CHROME);xz_border(c,200,140,400,200,SH_RAIL);
  sh_text_center(c,400,230,"POPUP: DRAWN BY A LATER UNIT",SH_SMALL,SH_DIM);
 }
 sh_notice(u,m,c);
 sh_header_buttons(m,c);
 return 1;
}
