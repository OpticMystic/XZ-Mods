#ifndef XZ_SHELL_MENU_FIXTURES_H
#define XZ_SHELL_MENU_FIXTURES_H
/* The MODS panel's model with every page filled (decks, link, DJ profiles, apps, Doom files, updates,
   readings), and the MODS rail the runtime publishes (shell_runtime.c mod_tabs), for the MENU tab tests
   and previews. Built with the MODS panel sources (ui/ui_sources.py). */
#include "../ui/ui_skin.h"
#include "../ui/ui_menu.h"
#include "shell_settings.h"
#include <stdio.h>
#include <string.h>

static struct xz_ui_dj mfx_dj;
static const char *const mfx_images[3]={"DJPROFILE/ANNA/logo.png","PHOTOS/flyer-2026.jpg","DJPROFILE/ANNA/alt-logo.png"};
static const char (*mfx_image_list)[256];
static char mfx_image_store[3][256];
static void mfx_model(struct xz_ui_model *m) {
 memset(m,0,sizeof *m);
 m->theme=XZ_THEME_FLAGSHIP;m->ui_interface=XZ_UI_INTERFACE_MODERN;m->modern_theme=0;
 static const char *const tracks[2]={"Alone - Mixed","Night Drive (feat. Example Artist)"};
 for(int d=0;d<2;d++){
  struct xz_ui_deck *k=&m->deck[d];
  k->ready=XZ_UI_GATE|XZ_UI_SMART|XZ_UI_THEME|XZ_UI_STEM|XZ_UI_KEY|XZ_UI_KEYSYNC;
  k->track=tracks[d];k->bpm=d?130.8f:129.6f;k->stem_count=3;k->usb_reason=XZ_UI_USB_WHY_NONE;
  k->levels[0]=k->levels[1]=k->levels[2]=1;k->groove_active=k->sample_active=-1;
  snprintf(k->key_readout,sizeof k->key_readout,"%s",d?"Bbm +1":"Fm");
  k->pc_stems=(struct xz_ui_pc_stems){.source=XZ_UI_SOURCE_LOCAL,.target=-1,.setup_step=XZ_UI_PC_STEP_DETECT};
  k->stem_format="OVERCUE 3";
 }
 m->deck[0].ram=(struct xz_ui_ram){.state=XZ_UI_RAM_LOADING,.copied=3u<<20,.bytes=9u<<20};
 m->enabled=XZ_UI_STEM|XZ_UI_GATE;
 m->connection=(struct xz_ui_connection){1,1,1,1,1,1,1,"192.168.1.40","VJFS / VJVP / VJTE + VJXZ","CONNECTION SERVICE CONFIGURED BY THE MOD LOADER",50005,29.97f};
 m->takeover_assign=2;m->stems_overlay=1;m->pad_feedback=1;m->stem_page=1;m->stem_order=0;
 m->stem_colors[0]=1;m->rekordbox_assign[0]=XZ_UI_RB_INTERFACE;m->rekordbox_assign[1]=XZ_UI_RB_STOCK;
 m->jump=xz_jump_defaults();m->jump_encoder=1;m->deck_view_button=1;m->jump_page[0]=m->jump_page[1]=m->jump_page[2]=m->jump_page[3]=0;m->key_feedback=1;m->browse_traffic=1;m->key_format=2;
 m->settings_status="SETTINGS SAVED TO USB 1";
 m->doom_ready=1;
 m->doom=(struct xz_doom_browser){{"DOOM2.WAD","SIGIL_V1_21.WAD","ALIENS-TC.WAD"},{"BASE GAME / 14.6 MB","ADD-ON MAP / 2.1 MB","ADD-ON / 5.0 MB"},
  "DOOM2.WAD + SIGIL_V1_21.WAD","3 FILES FOUND",3,0,3,0,1,0,1,0};
 m->update=(struct xz_update_model){0,1,"UP TO DATE: 0.3.0-PREVIEW.17"};
 m->sdk.host=XZ_SDK_UI_RUNNING;m->sdk.fresh=1;
 struct xz_sdk_snapshot *s=&m->sdk.snapshot;
 s->generation=4;s->count=3;s->autoload_enabled=1;snprintf(s->status,sizeof s->status,"3 APPS ON USB 1");
 static const char *const names[3]={"Beat Clock","Crowd Lights Profile","Set Timer"};
 static const unsigned kinds[3]={XZ_SDK_APPLICATION,XZ_SDK_PROFILE,XZ_SDK_APPLICATION};
 static const unsigned states[3]={XZ_SDK_RUNNING,XZ_SDK_READY,XZ_SDK_BLOCKED};
 for(int i=0;i<3;i++){
  struct xz_sdk_item *it=&s->items[i];
  snprintf(it->id,sizeof it->id,"app.%d",i);snprintf(it->name,sizeof it->name,"%s",names[i]);
  snprintf(it->version,sizeof it->version,"1.0.%d",i);it->kind=kinds[i];it->state=states[i];
  if(states[i]==XZ_SDK_BLOCKED)snprintf(it->detail,sizeof it->detail,"NEEDS SDK 2: UPDATE THE USB LOADER");
 }
 m->metrics_enabled=1;m->metrics=(struct xz_metrics_sample){1000,3,412,{400,420,380,450},612,1024,412,180,58};
 memset(&mfx_dj,0,sizeof mfx_dj);
 static const char *const dj[3]={"ANNA","DJ NORTHWIND","LOOPS & LIGHT"};
 static const uint32_t accents[3]={0xff375f,0x64d2ff,0x30d158};
 for(int i=0;i<3;i++){
  struct xz_ui_dj_entry *e=&mfx_dj.entry[i];
  e->name=dj[i];e->tagline=i?NULL:"DEEP HOUSE / BERLIN";e->usb=i==2;e->stick_active=i!=1;e->has_accent=1;e->accent=accents[i];
 }
 mfx_dj.count=3;mfx_dj.current=0;mfx_dj.usb_present=3;mfx_dj.pending=-1;mfx_dj.detail=-1;mfx_dj.preview=-1;
 for(int i=0;i<3;i++)snprintf(mfx_image_store[i],256,"%s",mfx_images[i]);
 mfx_image_list=(const char (*)[256])mfx_image_store;
 mfx_dj.images=mfx_image_list;mfx_dj.image_count=3;mfx_dj.images_usb=0;
 m->dj=&mfx_dj;
}
/* The rail shell_runtime.c publishes: one MENU tab per MODS section. */
static struct xz_shell_mods_tab mfx_tabs[7]={
 {"HOME",XZ_SHELL_TAB_MENU,NULL,NULL,0,XZ_UI_HOME,NULL,0},
 {"PLAY",XZ_SHELL_TAB_MENU,NULL,NULL,0,XZ_UI_PLAY,NULL,0},
 {"STEMS",XZ_SHELL_TAB_MENU,NULL,NULL,0,XZ_UI_STEM_SECTION,NULL,0},
 {"SCREEN",XZ_SHELL_TAB_MENU,NULL,NULL,0,XZ_UI_SCREEN,NULL,0},
 {"4 DECKS",XZ_SHELL_TAB_MENU,NULL,NULL,0,XZ_UI_FOUR_DECKS,NULL,0},
 {"CONNECT",XZ_SHELL_TAB_MENU,NULL,NULL,0,XZ_UI_CONNECT,NULL,0},
 {"EXTRAS",XZ_SHELL_TAB_MENU,NULL,NULL,0,XZ_UI_EXTRAS,NULL,0}};
static const struct xz_shell_mods_model mfx_mods={7,mfx_tabs};
/* What shell_runtime.c does to a published page: the rows for the shell's own settings show its values
   (here WAVEFORM COLOURS 3-BAND, TRACK COLOURS DECK + WAVE, every lamp and button shown). */
static inline void mfx_look_values(struct xz_shell_menu *p) {
 for(int i=0;i<p->row_count&&i<XZ_SHELL_MENU_ROWS;i++)
  if(p->row[i].target.kind==XZ_UI_SHELL_LOOK)p->row[i].current=(int16_t)(p->row[i].target.index==XZ_SHELL_SET_WAVE_COLOR?XZ_SHELL_WAVE_3BAND:0);
}
/* The rail entry (1..) of the MENU tab a top page belongs to; 0 when none. */
static int mfx_tab_of(int top) { for(int t=0;t<7;t++)if(mfx_tabs[t].kind==XZ_SHELL_TAB_MENU&&mfx_tabs[t].page==top)return t+1; return 0; }
#endif
