#ifdef NDEBUG
#error Shell acceptance requires active assertions
#endif
#include "browse_fixtures.h"
#include "settings_fixtures.h"
#include "shell_source.h"
#include "shell_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct fx_track track;
static struct bx_library lib;
static uint16_t pixels[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT],other[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT];
static struct xz_shell_action out[XZ_SHELL_ACTIONS];
static struct xz_shell_browse_model b;
static struct xz_shell_source_model sources;

static size_t key(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_key k) {
 struct xz_shell_event e={XZ_SHELL_KEY,0,0,0,k,-1};
 memset(out,0,sizeof out);
 return xz_shell_input(u,m,&e,out);
}
static size_t turn(struct xz_shell *u, const struct xz_shell_model *m, int delta) {
 struct xz_shell_event e={XZ_SHELL_ENCODER_TURN,0,0,delta,0,-1};
 memset(out,0,sizeof out);
 return xz_shell_input(u,m,&e,out);
}
static size_t push(struct xz_shell *u, const struct xz_shell_model *m) {
 struct xz_shell_event e={XZ_SHELL_ENCODER_PUSH,0,0,0,0,-1};
 memset(out,0,sizeof out);
 return xz_shell_input(u,m,&e,out);
}
static void render(const struct xz_shell *u, const struct xz_shell_model *m, uint16_t *f) {
 assert(xz_shell_render(u,m,f,XZ_SHELL_WIDTH));
}
static uint16_t rgb565(uint32_t c) { return (uint16_t)(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31)); }
static void model(struct xz_shell_model *m) {
 fx_model_full(m,&track);
 m->capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_SEARCH|XZ_SHELL_CAP_SOURCE|XZ_SHELL_CAP_LOAD;
 bx_tracks(&b,&lib,XZ_SHELL_BROWSE);
 m->browse=&b;m->source=&sources;
 m->mods=&fxm_model;
 m->my_settings=fxm_my_reset();
 m->settings_screen=fxs_reset();
 m->deck[0].playing=0;   /* MY SETTINGS loads onto a paused deck */
}
/* USB 1 picked from PLAY: BROWSE with the category rail holding the encoder. */
static void at_root(struct xz_shell *u, const struct xz_shell_model *m) {
 xz_shell_init(u);xz_shell_browse_reset();
 key(u,m,XZ_SHELL_KEY_SOURCE_USB1);
 assert(xz_shell_screen(u)==XZ_SHELL_BROWSE&&sh_browse_at_root(u));
}

static void test_opens_at_root_only(void) {
 struct xz_shell u;struct xz_shell_model m;model(&m);
 at_root(&u,&m);
 assert(key(&u,&m,XZ_SHELL_KEY_MENU)==0&&u.panel.kind==XZ_SHELL_PANEL_MY_SETTINGS);
 render(&u,&m,pixels);
 assert(key(&u,&m,XZ_SHELL_KEY_MENU)==0&&u.panel.kind==XZ_SHELL_PANEL_NONE);
 /* BACK puts the panel away and leaves BROWSE where it was. */
 int depth=u.depth;
 key(&u,&m,XZ_SHELL_KEY_MENU);
 assert(key(&u,&m,XZ_SHELL_KEY_BACK)==0&&u.panel.kind==XZ_SHELL_PANEL_NONE&&u.depth==depth&&sh_browse_at_root(&u));
 /* Inside a category MENU is the list MENU again. */
 assert(push(&u,&m)==0&&!sh_browse_at_root(&u));
 assert(key(&u,&m,XZ_SHELL_KEY_MENU)==0&&u.panel.kind==XZ_SHELL_PANEL_MENU);
 key(&u,&m,XZ_SHELL_KEY_MENU);
 /* No stick model, or a model for a source that is not a stick: the list MENU. */
 const struct xz_shell_my_settings_model *keep=m.my_settings;
 m.my_settings=NULL;at_root(&u,&m);
 assert(key(&u,&m,XZ_SHELL_KEY_MENU)==0&&u.panel.kind==XZ_SHELL_PANEL_MENU);
 static struct xz_shell_my_settings_model link;link=*keep;link.source=XZ_SHELL_SRC_LINK;
 m.my_settings=&link;at_root(&u,&m);
 assert(key(&u,&m,XZ_SHELL_KEY_MENU)==0&&u.panel.kind==XZ_SHELL_PANEL_MENU);
 puts("PASS MY SETTINGS: MENU on a stick's browse root opens it, MENU or BACK closes it; elsewhere the list MENU");
}

static void test_rows_and_requests(void) {
 struct xz_shell u;struct xz_shell_model m;model(&m);
 at_root(&u,&m);
 key(&u,&m,XZ_SHELL_KEY_MENU);
 /* LOAD TO DECK n: the active deck. */
 assert(push(&u,&m)==1&&out[0].kind==XZ_SHELL_ACT_MY_SETTINGS&&out[0].id==XZ_SHELL_MY_LOAD&&out[0].value==XZ_SHELL_SRC_USB1&&out[0].deck==u.deck);
 /* SAVE arms on the first push, writes on the second; BACK disarms without closing. */
 turn(&u,&m,1);
 assert(push(&u,&m)==0&&u.panel.armed);
 render(&u,&m,pixels);
 assert(key(&u,&m,XZ_SHELL_KEY_BACK)==0&&!u.panel.armed&&u.panel.kind==XZ_SHELL_PANEL_MY_SETTINGS);
 push(&u,&m);
 assert(push(&u,&m)==1&&out[0].kind==XZ_SHELL_ACT_MY_SETTINGS&&out[0].id==XZ_SHELL_MY_SAVE&&out[0].value==XZ_SHELL_SRC_USB1);
 /* Profiles: the active one and a damaged one ask nothing; another one loads. */
 turn(&u,&m,1);assert(push(&u,&m)==0);
 turn(&u,&m,1);
 assert(push(&u,&m)==1&&out[0].kind==XZ_SHELL_ACT_DJ_PROFILE&&out[0].id==XZ_SHELL_PROFILE_LOAD&&out[0].value==1&&out[0].deck==XZ_SHELL_SRC_USB1);
 turn(&u,&m,1);assert(push(&u,&m)==0);
 /* SAVE SETTINGS TO the active profile, armed. */
 turn(&u,&m,1);
 assert(push(&u,&m)==0&&u.panel.armed);
 assert(push(&u,&m)==1&&out[0].kind==XZ_SHELL_ACT_DJ_PROFILE&&out[0].id==XZ_SHELL_PROFILE_SAVE&&out[0].value==0);
 /* WAVEFORM: the shell's own values, the same ones SHORTCUT and UTILITY set. */
 turn(&u,&m,1);
 int before=(int)u.display.wave_color;
 assert(push(&u,&m)==2&&out[0].kind==XZ_SHELL_ACT_SET_SETTING&&out[0].id==XZ_SHELL_ITEM_WAVE_COLOR&&out[0].value==(before+1)%4);
 assert(out[1].id==XZ_SHELL_ITEM_USB_COLORS&&(int)u.display.wave_color==(before+1)%4);
 turn(&u,&m,1);
 assert(push(&u,&m)==1&&out[0].id==XZ_SHELL_ITEM_WAVE_POSITION&&u.display.wave_position==XZ_SHELL_POSITION_LEFT);
 turn(&u,&m,1);
 assert(push(&u,&m)==1&&out[0].kind==XZ_SHELL_ACT_ZOOM&&u.zoom==XZ_SHELL_ZOOM_DEFAULT+1);
 turn(&u,&m,1);
 assert(push(&u,&m)==1&&out[0].id==XZ_SHELL_ITEM_USB_COLORS&&u.display.usb_colors==XZ_SHELL_USB_COLORS_OFF);
 /* The runtime's waveform item goes back to its mod tab. */
 turn(&u,&m,1);
 assert(push(&u,&m)==1&&out[0].kind==XZ_SHELL_ACT_SET_MOD&&out[0].deck==0&&out[0].id==22&&out[0].value==2);
 /* The stick's stored values: a writable one steps, an unwritable one asks nothing. */
 turn(&u,&m,1);
 assert(push(&u,&m)==1&&out[0].kind==XZ_SHELL_ACT_MY_SETTING_SET&&out[0].deck==XZ_SHELL_SRC_USB1&&out[0].id==0x200&&out[0].value==5);
 turn(&u,&m,100);
 assert(push(&u,&m)==0);
 render(&u,&m,pixels);
 puts("PASS MY SETTINGS rows: load, armed save, profile load and save, waveform values, stored values");
}

static void test_stick_states(void) {
 struct xz_shell u;struct xz_shell_model m;model(&m);
 /* A locked stick: no SAVE rows; stored values show but do not change; LOAD still works. */
 fxm_my.writable=0;
 at_root(&u,&m);key(&u,&m,XZ_SHELL_KEY_MENU);
 assert(push(&u,&m)==1&&out[0].id==XZ_SHELL_MY_LOAD);
 for(int i=0;i<30;i++){turn(&u,&m,1);size_t n=push(&u,&m);
  for(size_t k=0;k<n;k++)assert(out[k].kind!=XZ_SHELL_ACT_MY_SETTING_SET&&!(out[k].kind==XZ_SHELL_ACT_MY_SETTINGS&&out[k].id==XZ_SHELL_MY_SAVE)
   &&!(out[k].kind==XZ_SHELL_ACT_DJ_PROFILE&&out[k].id==XZ_SHELL_PROFILE_SAVE));}
 render(&u,&m,pixels);
 /* No file and locked: one line says so; nothing to push there. */
 fxm_my_reset();fxm_my.on_stick=0;fxm_my.writable=0;fxm_my.stored=NULL;fxm_my.profile_count=0;
 at_root(&u,&m);key(&u,&m,XZ_SHELL_KEY_MENU);
 render(&u,&m,pixels);
 /* A legacy single profile does not switch. */
 fxm_my_reset();fxm_my.profiles_switchable=0;
 at_root(&u,&m);key(&u,&m,XZ_SHELL_KEY_MENU);
 turn(&u,&m,3);assert(push(&u,&m)==0);
 /* Wild: NULL names, a huge profile count with few entries is the runtime's bug, so counts stay honest
    here; out-of-range currents, NULL choices. */
 static const struct xz_shell_setting_item wild[]={{1,0,NULL,NULL,5,9,1,1},{2,0,"X",fxs_off_on,2,-3,1,1},{3,0,"Y",fxs_off_on,2,0,0,1}};
 static const struct xz_shell_settings_model wild_model={3,wild,NULL};
 static const struct xz_shell_dj_profile nameless[]={{NULL,0,0,0},{NULL,1,0xffffff,1}};
 fxm_my_reset();fxm_my.stored=&wild_model;fxm_my.waveform=&wild_model;fxm_my.profiles=nameless;fxm_my.profile_count=2;fxm_my.profile_active=7;fxm_my.source_name=NULL;
 at_root(&u,&m);key(&u,&m,XZ_SHELL_KEY_MENU);
 for(int i=0;i<20;i++){render(&u,&m,pixels);turn(&u,&m,1);push(&u,&m);push(&u,&m);}
 /* The stick goes away while the panel is open. */
 m.my_settings=NULL;render(&u,&m,pixels);push(&u,&m);turn(&u,&m,1);
 assert(key(&u,&m,XZ_SHELL_KEY_MENU)==0);
 puts("PASS MY SETTINGS on a locked stick, a stick without a file, a legacy profile, wild values, a stick pulled");
}

static int count_color(const uint16_t *f, struct xz_shell_rect r, uint32_t color) {
 int n=0;
 for(int y=r.y;y<r.y+r.h;y++)for(int x=r.x;x<r.x+r.w;x++)n+=f[y*XZ_SHELL_WIDTH+x]==rgb565(color);
 return n;
}
static void test_usb_colors(void) {
 struct xz_shell u;struct xz_shell_model m;model(&m);
 xz_shell_init(&u);
 struct xz_shell_rect tag=xz_shell_play_rect(&m,XZ_SHELL_R_DECK_TAG,0),lane=xz_shell_play_rect(&m,XZ_SHELL_R_LANE,0);
 render(&u,&m,other);
 assert(count_color(other,tag,SH_BLUE)>100);
 /* Deck 1's track comes from a stick that sets a yellow background and RGB waveforms. */
 m.deck[0].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_SOURCE_LOOK);
 m.deck[0].look=(struct xz_shell_source_look){1,0xf2d42c,XZ_SHELL_WAVE_RGB};
 assert(sh_deck_wave_color(&u,&m.deck[0])==XZ_SHELL_WAVE_RGB&&sh_deck_accent(&u,&m.deck[0],SH_BLUE)==0xf2d42c);
 assert(sh_deck_wave_color(&u,&m.deck[1])==XZ_SHELL_WAVE_3BAND);
 render(&u,&m,pixels);
 assert(count_color(pixels,tag,0xf2d42c)>100&&count_color(pixels,tag,SH_BLUE)==0);
 assert(count_color(pixels,tag,SH_BLACK)>0);                  /* dark text on the light tile */
 assert(memcmp(pixels+lane.y*XZ_SHELL_WIDTH,other+lane.y*XZ_SHELL_WIDTH,sizeof(uint16_t)*XZ_SHELL_WIDTH*lane.h)!=0);
 /* DECK ONLY: the tile stays, the waveform is the unit's again. */
 xz_shell_restore_setting(&u,XZ_SHELL_ITEM_USB_COLORS,XZ_SHELL_USB_COLORS_TILE);
 assert(sh_deck_wave_color(&u,&m.deck[0])==XZ_SHELL_WAVE_3BAND&&sh_deck_accent(&u,&m.deck[0],SH_BLUE)==0xf2d42c);
 /* OFF: neither. */
 xz_shell_restore_setting(&u,XZ_SHELL_ITEM_USB_COLORS,XZ_SHELL_USB_COLORS_OFF);
 render(&u,&m,pixels);
 assert(count_color(pixels,tag,SH_BLUE)>100&&sh_deck_wave_color(&u,&m.deck[0])==XZ_SHELL_WAVE_3BAND);
 assert(!xz_shell_restore_setting(&u,XZ_SHELL_ITEM_USB_COLORS,3)&&u.display.usb_colors==XZ_SHELL_USB_COLORS_OFF);
 /* A stick waveform value out of range, or "none", leaves the unit's colour. */
 xz_shell_restore_setting(&u,XZ_SHELL_ITEM_USB_COLORS,XZ_SHELL_USB_COLORS_ALL);
 m.deck[0].look.wave_color=-1;assert(sh_deck_wave_color(&u,&m.deck[0])==XZ_SHELL_WAVE_3BAND);
 m.deck[0].look.wave_color=9;assert(sh_deck_wave_color(&u,&m.deck[0])==XZ_SHELL_WAVE_3BAND);
 /* Without the known bit the look is not read. */
 m.deck[0].look=(struct xz_shell_source_look){1,0x123456,XZ_SHELL_WAVE_BLUE};
 m.deck[0].known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_SOURCE_LOOK);
 assert(sh_deck_accent(&u,&m.deck[0],SH_BLUE)==SH_BLUE&&sh_deck_wave_color(&u,&m.deck[0])==XZ_SHELL_WAVE_3BAND);
 puts("PASS USB colours: the stick's tile and waveform colours per deck; DECK ONLY and OFF; unknown looks unread");
}

static void test_short_menu(void) {
 struct xz_shell u;struct xz_shell_model m;model(&m);
 xz_shell_init(&u);
 assert(key(&u,&m,XZ_SHELL_KEY_MENU)==0&&xz_shell_screen(&u)==XZ_SHELL_UTILITY&&u.mods_view.tab==1&&u.mods_view.on_tabs);
 assert(key(&u,&m,XZ_SHELL_KEY_MENU)==0&&u.depth==1);
 m.mods=NULL;
 assert(key(&u,&m,XZ_SHELL_KEY_MENU)==0&&xz_shell_screen(&u)==XZ_SHELL_UTILITY&&u.mods_view.tab==0);
 /* Held MENU always goes to UTILITY, including another hold from UTILITY. */
 m.mods=&fxm_model;key(&u,&m,XZ_SHELL_KEY_BACK);key(&u,&m,XZ_SHELL_KEY_MENU);
 assert(u.mods_view.tab==1);
 assert(key(&u,&m,XZ_SHELL_KEY_MENU_HOLD)==0&&u.mods_view.tab==0&&u.mods_view.on_tabs&&u.depth==2);
 assert(key(&u,&m,XZ_SHELL_KEY_MENU_HOLD)==0&&u.depth==2&&u.mods_view.tab==0);
 /* Screens without a MENU panel of their own take MENU the same way. */
 static const enum xz_shell_key openers[]={XZ_SHELL_KEY_SHORTCUT,XZ_SHELL_KEY_SOURCE};
 for(size_t i=0;i<sizeof openers/sizeof *openers;i++){
  xz_shell_init(&u);key(&u,&m,openers[i]);
  enum xz_shell_screen under=xz_shell_screen(&u);assert(under!=XZ_SHELL_PLAY);
  assert(key(&u,&m,XZ_SHELL_KEY_MENU)==0&&xz_shell_screen(&u)==XZ_SHELL_UTILITY&&u.mods_view.tab==1);
  assert(key(&u,&m,XZ_SHELL_KEY_MENU)==0&&xz_shell_screen(&u)==under);
 }
 xz_shell_init(&u);
 /* A probe copy sees the effect: the key is the shell's, never the stock deck's. */
 struct xz_shell probe=u;
 key(&probe,&m,XZ_SHELL_KEY_MENU);
 assert(memcmp(&probe,&u,sizeof u)!=0&&u.depth==1);
 puts("PASS short MENU on PLAY, SHORTCUT and SOURCE opens MODS on the first mod tab (UTILITY without mods); MENU again closes");
}

static void test_mods_title(void) {
 struct xz_shell u;struct xz_shell_model m;model(&m);
 xz_shell_init(&u);key(&u,&m,XZ_SHELL_KEY_MENU_HOLD);
 render(&u,&m,pixels);
 key(&u,&m,XZ_SHELL_KEY_MENU);key(&u,&m,XZ_SHELL_KEY_MENU);
 assert(u.mods_view.tab==1);
 render(&u,&m,other);
 /* UTILITY and a mod tab differ in the title bar (title, the line under it, the version). */
 int differs=0;
 for(int y=0;y<40;y++)for(int x=52;x<XZ_SHELL_HEADER_X;x++)differs|=pixels[y*XZ_SHELL_WIDTH+x]!=other[y*XZ_SHELL_WIDTH+x];
 assert(differs);
 /* Long section names wrap inside the rail. */
 static const char *const long_sections[]={"CLASSIC THEMES","MODERN THEMES AND COLOURS","GROUPED"};
 static const struct xz_shell_setting_item look[]={{1,0,"THEME",fxs_off_on,2,0,1,1},{2,1,"ACCENT",fxs_off_on,2,0,1,1},{3,2,"X",fxs_off_on,2,0,1,1}};
 static const struct xz_shell_settings_model look_model={3,look,NULL};
 static const struct xz_shell_mods_tab tabs[]={{"APPEARANCE",XZ_SHELL_TAB_ITEMS,&look_model,long_sections,3,0,NULL,0}};
 static const struct xz_shell_mods_model mods={1,tabs};
 m.mods=&mods;
 key(&u,&m,XZ_SHELL_KEY_BACK);key(&u,&m,XZ_SHELL_KEY_BACK);
 xz_shell_init(&u);key(&u,&m,XZ_SHELL_KEY_MENU);
 assert(u.mods_view.tab==1);
 render(&u,&m,pixels);
 struct xz_shell_rect rail=xz_shell_settings_rect(XZ_SHELL_SR_RAIL),items=xz_shell_settings_rect(XZ_SHELL_SR_ITEMS);
 /* Nothing of the rail labels spills into the gap before the items pane. */
 for(int y=rail.y;y<rail.y+rail.h;y++)for(int x=rail.x+rail.w;x<items.x;x++)assert(pixels[y*XZ_SHELL_WIDTH+x]==rgb565(SH_BLACK));
 puts("PASS MODS title says which menu is open; long section names stay inside the rail");
}

int main(void) {
 fx_track_build(&track,7);bx_build(&lib);
 (void)bx_folders;(void)fxs_item;   /* shared fixtures this test does not need */
 for(int i=0;i<2;i++){sources.device[i].known=1u<<XZ_SHELL_D_STATUS;sources.device[i].status=XZ_SHELL_DEVICE_READY;}
 test_opens_at_root_only();
 test_rows_and_requests();
 test_stick_states();
 test_usb_colors();
 test_short_menu();
 test_mods_title();
 puts("PASS my settings");
 return 0;
}
