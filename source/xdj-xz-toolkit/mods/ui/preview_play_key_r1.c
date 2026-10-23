/* SPDX-License-Identifier: MIT */
/* C-rendered fixture evidence only. Never loaded on a controller. */
#include "play_key.h"
#include "../shell/four_deck_fixtures.h"
#include "../shell/menu_fixtures.h"
#include <assert.h>
#include <stdlib.h>
static uint16_t frame[800*480];
static struct fx_track track;
static const char *directory;
static void save(const char *name){
 char path[1024];snprintf(path,sizeof path,"%s/%s.ppm",directory,name);FILE *f=fopen(path,"wb");assert(f);
 fprintf(f,"P6\n800 480\n255\n");
 for(int i=0;i<800*480;i++){unsigned p=frame[i];unsigned char rgb[3]={((p>>11)&31)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};assert(fwrite(rgb,1,3,f)==3);}
 assert(!fclose(f));
}
static void overlay(struct xz_ui_model *m,int deck,int setup){
 struct xz_play_key p;struct xz_play_key_view v;xz_play_key_open(&p,deck);p.setup=setup;p.step=3;
 xz_play_key_view(&p,m,NULL,&v);v.native_engine=v.native_sync=1; /* explicit host fixture engine */
 if(deck>1)snprintf(v.key,sizeof v.key,"%s",deck==2?"Am":"Gm"); /* fx_model_four metadata */
 xz_play_key_render(&p,&v,frame,800);
}
int main(int argc,char **argv){
 assert(argc==2);directory=argv[1];struct xz_shell s;struct xz_shell_model m;struct xz_ui u;struct xz_ui_model um;
 fx_track_build(&track,7);mfx_model(&um);um.dj=NULL;xz_shell_init(&s);fx_model_full(&m,&track);
 assert(xz_shell_render(&s,&m,frame,800));overlay(&um,0,0);save("modern-two-play-key-controls");
 fx_model_four(&m,&track);s.play_decks=4;assert(xz_shell_render(&s,&m,frame,800));overlay(&um,1,0);save("modern-four-play-local-key-controls");
 m.wave_order=1;assert(xz_shell_render(&s,&m,frame,800));save("modern-four-mixer-baseline-unchanged");
 s.play_decks=2;m.sides_extra=3;assert(xz_shell_render(&s,&m,frame,800));overlay(&um,2,1);save("modern-selected-three-four-key-route-unavailable");
 fx_model_full(&m,&track);s.play_decks=2;m.deck[0].known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_KEY);
 um.deck[0].ready=0;snprintf(um.deck[0].key_readout,sizeof um.deck[0].key_readout,"---");
 assert(xz_shell_render(&s,&m,frame,800));overlay(&um,0,1);save("modern-unknown-key-live-verify-blocked");
 um.theme=0;memset(frame,0,sizeof frame);overlay(&um,0,1);save("classic-key-popup-component-only");
 mfx_model(&um);um.dj=NULL;um.theme=0;um.ui_interface=XZ_UI_INTERFACE_CLASSIC;
 xz_ui_init(&u);u.page=XZ_UI_SETTINGS;assert(xz_ui_render(&u,&um,frame,800*480,800));save("classic-advanced-key-page-removed-ram-retained");
 u.page=XZ_UI_THEMES;assert(xz_ui_render(&u,&um,frame,800*480,800));save("classic-appearance-key-format");
 u.page=XZ_UI_SETTINGS;um.theme=XZ_THEME_FLAGSHIP;um.ui_interface=XZ_UI_INTERFACE_MODERN;
 struct xz_shell_menu menu;assert(xz_ui_menu_build(&u,&um,&menu));fx_model_full(&m,&track);m.menu=&menu;m.mods=&mfx_mods;
 xz_shell_init(&s);xz_shell_mods_show(&s,&m,mfx_tab_of(menu.root));assert(xz_shell_render(&s,&m,frame,800));save("modern-advanced-key-page-removed-ram-retained");
 puts("PASS C-rendered key popup and actual settings layouts; classic popup backdrop intentionally empty");return 0;
}
