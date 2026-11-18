/* Renders the round-5 states to PPM files in argv[1]: browse panels (MENU,
   FILTER, INFO), playlist editing, the absent stick, the source picker
   reached by BACK or a USB button, stem mute buttons, the slip ghost, the key
   traffic light and the graphical SHORTCUT. Host only. */
#include "browse_fixtures.h"
#include "shell_source.h"
#include "shell_settings.h"
#include "settings_fixtures.h"
#include <stdio.h>
#include <stdlib.h>

static struct fx_track track;
static struct bx_library lib;
static uint16_t frame[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT];
static const char *out_dir;
static struct xz_shell_action out[XZ_SHELL_ACTIONS];
static struct xz_shell_source_model sources;

static void shot(const struct xz_shell *u, const struct xz_shell_model *m, const char *name) {
 char path[1024];
 if(!xz_shell_render(u,m,frame,XZ_SHELL_WIDTH)){fprintf(stderr,"render failed: %s\n",name);exit(1);}
 snprintf(path,sizeof path,"%s/r5-%s.ppm",out_dir,name);
 FILE *f=fopen(path,"wb");if(!f){perror(path);exit(1);}
 fprintf(f,"P6\n%d %d\n255\n",XZ_SHELL_WIDTH,XZ_SHELL_HEIGHT);
 for(size_t i=0;i<sizeof frame/sizeof *frame;i++){
  unsigned p=frame[i];
  unsigned char rgb[3]={(unsigned char)(((p>>11)&31)*255/31),(unsigned char)(((p>>5)&63)*255/63),(unsigned char)((p&31)*255/31)};
  fwrite(rgb,1,3,f);
 }
 fclose(f);
}
static size_t key(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_key k) {
 return xz_shell_input(u,m,&(struct xz_shell_event){XZ_SHELL_KEY,0,0,0,k,-1},out);
}
static size_t event(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_event_kind kind, int x, int y, int delta) {
 return xz_shell_input(u,m,&(struct xz_shell_event){kind,x,y,delta,0,-1},out);
}
/* Walk MENU until a push on a copy reaches the wanted page or grab. */
static void menu_to(struct xz_shell *u, const struct xz_shell_model *m, int page, int grab, int armed) {
 for(int i=0;i<80;i++){
  struct xz_shell probe=*u;
  event(&probe,m,XZ_SHELL_ENCODER_PUSH,0,0,0);
  if((page&&probe.panel.page==page)||(grab&&probe.grab.active)||(armed&&probe.panel.armed&&probe.panel.page==0)){
   if(armed)return;   /* stop on the row: the shot shows it focused */
   event(u,m,XZ_SHELL_ENCODER_PUSH,0,0,0);return;
  }
  event(u,m,XZ_SHELL_ENCODER_TURN,0,0,1);
 }
 fprintf(stderr,"menu row not found\n");exit(1);
}
int main(int argc, char **argv) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;
 if(argc<2){fputs("usage: preview_round5 OUTPUT_DIR\n",stderr);return 2;}
 out_dir=argv[1];
 fx_track_build(&track,7);bx_build(&lib);
 fx_model_full(&m,&track);
 m.capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_TAG_LIST|XZ_SHELL_CAP_SEARCH|XZ_SHELL_CAP_LOAD|XZ_SHELL_CAP_SOURCE|
  XZ_SHELL_CAP_PLAYLIST_EDIT|XZ_SHELL_CAP_FILTER|XZ_SHELL_CAP_STEM_MUTE;
 for(int i=0;i<2;i++){sources.device[i].known=(1u<<XZ_SHELL_D_STATUS)|(1u<<XZ_SHELL_D_NAME)|(1u<<XZ_SHELL_D_COLOR);sources.device[i].status=XZ_SHELL_DEVICE_READY;}
 sources.device[0].name="XZ SET 2026";sources.device[0].color=0x8c7a00;sources.device[1].name="BACKUP";sources.device[1].color=0x2a6cf0;
 sources.current_known=1;sources.current=XZ_SHELL_SRC_USB1;
 m.source=&sources;
 bx_tracks(&b,&lib,XZ_SHELL_BROWSE);bx_panels(&b,&lib);m.browse=&b;
 m.settings_screen=fxs_reset();(void)fxs_item;

 /* Browse: the key traffic light against deck 1's Fm, two-line rows, the INFO panel by key. */
 xz_shell_init(&u);xz_shell_browse_reset();
 key(&u,&m,XZ_SHELL_KEY_SOURCE_USB1);
 event(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,5);
 shot(&u,&m,"browse-traffic-keys");
 key(&u,&m,XZ_SHELL_KEY_INFO);shot(&u,&m,"browse-info-panel");
 key(&u,&m,XZ_SHELL_KEY_INFO);
 /* MENU: sort and columns. */
 key(&u,&m,XZ_SHELL_KEY_MENU);
 event(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,3);
 shot(&u,&m,"browse-menu-sort");
 event(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,11);
 shot(&u,&m,"browse-menu-columns");
 key(&u,&m,XZ_SHELL_KEY_MENU);
 /* FILTER: a few filters on, the panel open, then the chip on the header. */
 key(&u,&m,XZ_SHELL_KEY_TRACK_FILTER);
 u.filter.bpm=XZ_SHELL_FILTER_BPM_MASTER;u.filter.bpm_spread=6;u.filter.key_compatible=1;u.filter.rating_min=3;
 u.filter.colors=(1u<<XZ_SHELL_TAG_COLOR_RED)|(1u<<XZ_SHELL_TAG_COLOR_AQUA);u.filter.my_tags=2;
 event(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,2);
 shot(&u,&m,"browse-filter-panel");
 key(&u,&m,XZ_SHELL_KEY_BACK);
 shot(&u,&m,"browse-filter-chip");
 memset(&u.filter,0,sizeof u.filter);
 /* Playlist editing at the playlist list: MENU, NEW with the keyboard, DELETE armed. */
 bx_folders(&b,&lib,XZ_SHELL_BROWSE);bx_panels(&b,&lib);b.edit=XZ_SHELL_EDIT_PLAYLISTS;
 xz_shell_init(&u);xz_shell_browse_reset();key(&u,&m,XZ_SHELL_KEY_BROWSE);
 event(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,2);
 key(&u,&m,XZ_SHELL_KEY_MENU);
 menu_to(&u,&m,0,0,1);
 shot(&u,&m,"playlist-menu");
 event(&u,&m,XZ_SHELL_ENCODER_PUSH,0,0,0);
 shot(&u,&m,"playlist-delete-armed");
 key(&u,&m,XZ_SHELL_KEY_BACK);
 for(int i=0;i<12;i++)event(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,-1);
 menu_to(&u,&m,2,0,0);
 shot(&u,&m,"playlist-new-name");
 key(&u,&m,XZ_SHELL_KEY_BACK);key(&u,&m,XZ_SHELL_KEY_BACK);
 /* A playlist's tracks: ADD TO PLAYLIST page, then a track picked up and moved. */
 bx_tracks(&b,&lib,XZ_SHELL_PLAYLIST);bx_panels(&b,&lib);b.edit=XZ_SHELL_EDIT_TRACKS;b.edit_playlist=1;b.screen=XZ_SHELL_BROWSE;
 b.level_name="Peak Time";b.parent_name="PLAYLIST";b.depth=1;b.known|=1u<<XZ_SHELL_BF_PARENT;
 lib.folders[0].known|=1u<<XZ_SHELL_RF_EDITED;
 xz_shell_init(&u);xz_shell_browse_reset();key(&u,&m,XZ_SHELL_KEY_BROWSE);
 event(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,2);
 key(&u,&m,XZ_SHELL_KEY_MENU);menu_to(&u,&m,1,0,0);
 shot(&u,&m,"playlist-add-to");
 key(&u,&m,XZ_SHELL_KEY_BACK);
 menu_to(&u,&m,0,1,0);
 event(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,3);
 shot(&u,&m,"playlist-move-track");
 key(&u,&m,XZ_SHELL_KEY_BACK);
 /* The playlist list with a sidecar-edited playlist marked. */
 bx_folders(&b,&lib,XZ_SHELL_BROWSE);bx_panels(&b,&lib);b.edit=XZ_SHELL_EDIT_PLAYLISTS;
 lib.folders[1].known|=1u<<XZ_SHELL_RF_EDITED;
 xz_shell_init(&u);xz_shell_browse_reset();key(&u,&m,XZ_SHELL_KEY_BROWSE);
 shot(&u,&m,"playlist-edited-marks");
 /* A stick known to be gone: no stale rows. */
 bx_tracks(&b,&lib,XZ_SHELL_BROWSE);bx_panels(&b,&lib);
 sources.current=XZ_SHELL_SRC_USB2;sources.device[1].status=XZ_SHELL_DEVICE_NOT_CONNECTED;
 xz_shell_init(&u);xz_shell_browse_reset();key(&u,&m,XZ_SHELL_KEY_BROWSE);
 shot(&u,&m,"browse-absent-usb2");
 /* The picker: reached by BACK from the top level, and from the USB 2 button with nothing in it. */
 key(&u,&m,XZ_SHELL_KEY_BACK);
 shot(&u,&m,"source-via-back");
 xz_shell_init(&u);key(&u,&m,XZ_SHELL_KEY_SOURCE_USB2);
 shot(&u,&m,"source-usb2-missing");
 sources.current=XZ_SHELL_SRC_USB1;sources.device[1].status=XZ_SHELL_DEVICE_READY;

 /* PLAY: stem buttons (deck 1 all playing, deck 2 vocal muted), the slip ghost and VINYL, the traffic light. */
 xz_shell_init(&u);
 for(int d=0;d<2;d++){m.deck[d].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_MUTE);m.deck[d].stem_muted=0;}
 m.deck[1].stem_muted=4;m.deck[1].key="Cm";
 m.deck[0].slip=1;m.deck[0].slip_ms=m.deck[0].elapsed_ms+1800;m.deck[0].vinyl=1;
 shot(&u,&m,"play-stems-slip-traffic");
 m.deck[1].stem_muted=3;m.deck[1].key="Ab";m.deck[0].slip=0;
 m.stem_rgb_known=1;m.stem_rgb[0]=0x2cb8ff;m.stem_rgb[1]=0xff3c3c;m.stem_rgb[2]=0x3cd25a;
 shot(&u,&m,"play-stems-muted-green-key");
 for(int d=0;d<2;d++)m.deck[d].known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_MUTE);
 shot(&u,&m,"play-no-stems-hot-cue-row");

 /* SHORTCUT, graphical. */
 xz_shell_init(&u);key(&u,&m,XZ_SHELL_KEY_SHORTCUT);
 m.deck[0].slip=1;
 shot(&u,&m,"shortcut-graphic");
 event(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,4);
 u.display.wave_color=XZ_SHELL_WAVE_STEMS;
 shot(&u,&m,"shortcut-graphic-stems");
 return 0;
}
