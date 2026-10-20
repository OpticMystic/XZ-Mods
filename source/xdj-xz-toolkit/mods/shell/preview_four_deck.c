/* Renders the four-deck view to PPM files in argv[1]. Host only. */
#include "four_deck_fixtures.h"
#include "browse_fixtures.h"
#include "settings_fixtures.h"
#include "shell_info_fixtures.h"
#include <stdio.h>
#include <stdlib.h>

static struct fx_track track;
static struct bx_library lib;
static uint16_t frame[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT];
static uint16_t art[FXI_ART_SIDE*FXI_ART_SIDE];
static const char *out_dir;
static struct xz_shell_action out[XZ_SHELL_ACTIONS];

static void save(const char *name) {
 char path[1024];FILE *f;
 snprintf(path,sizeof path,"%s/%s.ppm",out_dir,name);
 f=fopen(path,"wb");if(!f){perror(path);exit(1);}
 fprintf(f,"P6\n%d %d\n255\n",XZ_SHELL_WIDTH,XZ_SHELL_HEIGHT);
 for(size_t i=0;i<sizeof frame/sizeof *frame;i++){
  unsigned p=frame[i];
  unsigned char rgb[3]={(unsigned char)(((p>>11)&31)*255/31),(unsigned char)(((p>>5)&63)*255/63),(unsigned char)((p&31)*255/31)};
  fwrite(rgb,1,3,f);
 }
 fclose(f);
}
static void shot(const struct xz_shell *u, const struct xz_shell_model *m, const char *name) {
 if(!xz_shell_render(u,m,frame,XZ_SHELL_WIDTH)){fprintf(stderr,"render failed: %s\n",name);exit(1);}
 save(name);
}
static void key(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_key k) {
 xz_shell_input(u,m,&(struct xz_shell_event){XZ_SHELL_KEY,0,0,0,k,-1},out);
}
static void open_screen(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_key k) { xz_shell_init(u);key(u,m,k); }
static void stems_on(struct xz_shell_model *m, int deck, uint8_t muted) {
 m->deck[deck].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_MUTE)|XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_GAIN);
 m->deck[deck].stem_muted=muted;
 m->deck[deck].stem_gain[0]=100;m->deck[deck].stem_gain[1]=70;m->deck[deck].stem_gain[2]=85;
}

int main(int argc, char **argv) {
 struct xz_shell u;struct xz_shell_model m;
 if(argc<2){fputs("usage: preview_four_deck OUTPUT_DIR\n",stderr);return 2;}
 out_dir=argv[1];
 (void)bx_folders;(void)fxs_item;
 fx_track_build(&track,7);
 xz_shell_init(&u);
 fx_model_four(&m,&track);
 shot(&u,&m,"play-4deck-full");
 u.deck=2;shot(&u,&m,"play-4deck-active-3");u.deck=0;

 fx_deck_unloaded(&m.deck[2],XZ_SHELL_SRC_USB2);fx_deck_unloaded(&m.deck[3],XZ_SHELL_SRC_NONE);
 shot(&u,&m,"play-4deck-decks-3-4-unloaded");
 fx_model_four(&m,&track);

 /* Deck 4 takes MASTER and plays its loop; deck 1 follows. */
 m.deck[0].master=0;m.deck[3].master=1;m.deck[3].sync=0;
 shot(&u,&m,"play-4deck-master-4-loop");
 fx_model_four(&m,&track);

 m.deck[0].title="Alone (feat. A Very Long Featured Vocalist Name) [Extended Club Remix]";
 m.deck[2].title="Sunrise Theme For The Last Hour Of The Night (Original Mix)";
 m.deck[3].title="Warehouse Tool Number Seventeen (Dub Version)";
 shot(&u,&m,"play-4deck-long-titles");
 fx_model_four(&m,&track);

 /* Stems on decks 1 and 2 in every stem layout: the four-deck view draws each as a half row. */
 static const char *const layouts[XZ_SHELL_STEM_LAYOUTS]={"hidden","below-full","below-half","outside-full","outside-half"};
 m.capabilities|=XZ_SHELL_CAP_STEM_MUTE|XZ_SHELL_CAP_STEM_GAIN;
 m.stems_known=1;m.stems_on=1;m.stem_layout_known=1;
 stems_on(&m,0,0);stems_on(&m,1,4);
 for(int d=2;d<4;d++)m.deck[d].known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_MUTE);
 for(int l=0;l<XZ_SHELL_STEM_LAYOUTS;l++){
  char name[64];snprintf(name,sizeof name,"play-4deck-stems-%s",layouts[l]);
  m.stem_layout=(enum xz_shell_stem_layout)l;shot(&u,&m,name);
 }
 m.stem_layout_known=0;shot(&u,&m,"play-4deck-stems-layout-unread");
 m.stem_layout_known=1;m.stem_layout=XZ_SHELL_STEMS_BELOW_HALF;

 /* GRID on deck 4: its buttons take deck 4's stem row while they show. */
 m.capabilities|=XZ_SHELL_CAP_GRID_EDIT;
 u.deck=3;u.grid_mode=1;shot(&u,&m,"play-4deck-grid-deck4");
 stems_on(&m,3,2);shot(&u,&m,"play-4deck-grid-deck4-stems");
 u.deck=0;u.grid_mode=0;
 fx_model_four(&m,&track);

 m.deck[2].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_ALERT);
 m.deck[2].alert=(struct xz_shell_alert){XZ_SHELL_ALERT_READ_ERROR,"E-8302",NULL};
 shot(&u,&m,"play-4deck-alert-deck3");
 fx_model_four(&m,&track);

 m.deck[1].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_ON_AIR);m.deck[1].on_air=1;
 m.deck[3].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_ON_AIR);m.deck[3].on_air=1;
 m.settings.needle_lock_known=1;m.settings.needle_lock=1;
 u.display.wave_position=XZ_SHELL_POSITION_LEFT;shot(&u,&m,"play-4deck-on-air-needle-lock-left");
 u.display.wave_position=XZ_SHELL_POSITION_CENTER;
 fx_model_four(&m,&track);

 /* The bottom strip on the other screens: four compact quarters. */
 struct xz_shell_browse_model b;
 bx_build(&lib);bx_tracks(&b,&lib,XZ_SHELL_BROWSE);
 m.capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_LOAD;m.browse=&b;
 open_screen(&u,&m,XZ_SHELL_KEY_BROWSE);u.deck=2;shot(&u,&m,"browse-4deck");
 struct xz_shell_rect row=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_ROW,3);
 xz_shell_input(&u,&m,&(struct xz_shell_event){XZ_SHELL_TOUCH_DOWN,row.x+row.w/2,row.y+row.h/2,0,0,-1},out);
 xz_shell_input(&u,&m,&(struct xz_shell_event){XZ_SHELL_TOUCH_UP,row.x+row.w/2,row.y+row.h/2,0,0,-1},out);
 shot(&u,&m,"browse-4deck-load-cells");
 m.settings_screen=fxs_reset();
 open_screen(&u,&m,XZ_SHELL_KEY_MENU_HOLD);shot(&u,&m,"utility-4deck");
 open_screen(&u,&m,XZ_SHELL_KEY_SHORTCUT);shot(&u,&m,"shortcut-4deck");
 struct xz_shell_info_model im;struct xz_shell_source_model sm;
 fxi_artwork(art);fxi_info_full(&im,art);fxi_source_full(&sm);
 m.info=&im;m.source=&sm;m.capabilities|=XZ_SHELL_CAP_SOURCE;
 open_screen(&u,&m,XZ_SHELL_KEY_INFO);shot(&u,&m,"info-4deck");
 open_screen(&u,&m,XZ_SHELL_KEY_SOURCE);shot(&u,&m,"source-4deck");
 fx_deck_unloaded(&m.deck[3],XZ_SHELL_SRC_NONE);
 open_screen(&u,&m,XZ_SHELL_KEY_BROWSE);shot(&u,&m,"browse-4deck-deck4-unloaded");
 return 0;
}
