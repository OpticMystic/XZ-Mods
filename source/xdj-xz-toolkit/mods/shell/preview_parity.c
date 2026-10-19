/* Renders the stock-parity workflows (tag list, history, cue list, deck states, linked players, LINK
   picker, MY SETTINGS on linked media) to PPM files in argv[1]. Host only. */
#include "browse_fixtures.h"
#include "settings_fixtures.h"
#include "shell_info_fixtures.h"
#include "shell_info.h"
#include "shell_source.h"
#include <stdio.h>
#include <stdlib.h>

static struct fx_track track;
static struct bx_library lib;
static struct xz_shell_browse_model b;
static uint16_t frame[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT];
static const char *out_dir;
static struct xz_shell_action out[XZ_SHELL_ACTIONS];

static void shot(const struct xz_shell *u, const struct xz_shell_model *m, const char *name) {
 if(!xz_shell_render(u,m,frame,XZ_SHELL_WIDTH)){fprintf(stderr,"render failed: %s\n",name);exit(1);}
 char path[1024];snprintf(path,sizeof path,"%s/%s.ppm",out_dir,name);
 FILE *f=fopen(path,"wb");if(!f){perror(path);exit(1);}
 fprintf(f,"P6\n%d %d\n255\n",XZ_SHELL_WIDTH,XZ_SHELL_HEIGHT);
 for(size_t i=0;i<sizeof frame/sizeof *frame;i++){
  unsigned p=frame[i];
  unsigned char rgb[3]={(unsigned char)(((p>>11)&31)*255/31),(unsigned char)(((p>>5)&63)*255/63),(unsigned char)((p&31)*255/31)};
  fwrite(rgb,1,3,f);
 }
 fclose(f);
}
static void send(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_event_kind k, int x, int y, int d) {
 xz_shell_input(u,m,&(struct xz_shell_event){k,x,y,d,0,-1},out);
}
static void key(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_key k) {
 xz_shell_input(u,m,&(struct xz_shell_event){XZ_SHELL_KEY,0,0,0,k,-1},out);
}
static void tap(struct xz_shell *u, const struct xz_shell_model *m, struct xz_shell_rect r) {
 send(u,m,XZ_SHELL_TOUCH_DOWN,r.x+r.w/2,r.y+r.h/2,0);send(u,m,XZ_SHELL_TOUCH_UP,r.x+r.w/2,r.y+r.h/2,0);
}
static void open_list(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_key k) {
 xz_shell_init(u);xz_shell_browse_reset();key(u,m,k);
 if(xz_shell_screen(u)==XZ_SHELL_BROWSE)u->stack[u->depth-1].focus=1;
}
static const struct xz_shell_cue_entry cues[]={
 {0,32000,0,0,"INTRO"},{1,64000,72000,0x28e028,"DROP LOOP"},{0,180000,0,0xff3c3c,NULL},{0,250000,0,0x20c8ff,"BREAKDOWN"},
};
static const struct xz_shell_link_media media[]={
 {3,XZ_SHELL_LINK_USB,"PARTY STICK",0xff8c1a,1},{3,XZ_SHELL_LINK_SD,NULL,0,0},{4,XZ_SHELL_LINK_USB,"BACKUP",0x3c64ff,1},
 {4,XZ_SHELL_LINK_COMPUTER,"STUDIO-PC",0,1},
};
static uint16_t art[FXI_ART_SIDE*FXI_ART_SIDE];

int main(int argc, char **argv) {
 struct xz_shell u;struct xz_shell_model m;
 if(argc<2){fputs("usage: preview_parity OUTPUT_DIR\n",stderr);return 2;}
 out_dir=argv[1];
 (void)fxs_item;(void)fxs_reset;
 fx_track_build(&track,7);bx_build(&lib);
 fx_model_full(&m,&track);
 m.capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_TAG_LIST|XZ_SHELL_CAP_SEARCH|XZ_SHELL_CAP_LOAD|XZ_SHELL_CAP_TAG_EDIT
  |XZ_SHELL_CAP_HISTORY_EDIT|XZ_SHELL_CAP_CUE_LIST|XZ_SHELL_CAP_GRID|XZ_SHELL_CAP_GRID_EDIT;
 m.browse=&b;

 /* TAG LIST: marks in the track list, the track menu, the TAG LIST menu, the armed clear. */
 bx_tracks(&b,&lib,XZ_SHELL_BROWSE);
 lib.tracks[1].tagged=lib.tracks[4].tagged=lib.tracks[5].tagged=1;
 b.cue_track=1000;b.cues=cues;b.cue_count=4;
 open_list(&u,&m,XZ_SHELL_KEY_BROWSE);shot(&u,&m,"parity-tag-marks");
 key(&u,&m,XZ_SHELL_KEY_MENU);shot(&u,&m,"parity-track-menu");
 send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,1);send(&u,&m,XZ_SHELL_ENCODER_PUSH,0,0,0);shot(&u,&m,"parity-cue-list");
 bx_tracks(&b,&lib,XZ_SHELL_TAG_LIST);b.total=12;b.window_count=12;
 open_list(&u,&m,XZ_SHELL_KEY_TAG_LIST);key(&u,&m,XZ_SHELL_KEY_MENU);shot(&u,&m,"parity-tag-list-menu");
 send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,2);send(&u,&m,XZ_SHELL_ENCODER_PUSH,0,0,0);shot(&u,&m,"parity-tag-to-playlist");
 open_list(&u,&m,XZ_SHELL_KEY_TAG_LIST);key(&u,&m,XZ_SHELL_KEY_MENU);
 send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,3);send(&u,&m,XZ_SHELL_ENCODER_PUSH,0,0,0);shot(&u,&m,"parity-tag-clear-armed");
 /* HISTORY top level */
 bx_folders(&b,&lib,XZ_SHELL_BROWSE);b.category=XZ_SHELL_CAT_HISTORY;b.level_name="HISTORY";
 open_list(&u,&m,XZ_SHELL_KEY_BROWSE);key(&u,&m,XZ_SHELL_KEY_MENU);shot(&u,&m,"parity-history-menu");

 /* PLAY: ON AIR, a loop point under the jog, cues waiting for CALL, LIBRARY auto load, FX target + Q,
    needle lock, grid mode buttons, a deck alert. */
 bx_tracks(&b,&lib,XZ_SHELL_BROWSE);
 xz_shell_init(&u);
 struct xz_shell_model p=m;
 p.deck[0].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_ON_AIR)|XZ_SHELL_KNOWN(XZ_SHELL_F_LOOP_EDIT);p.deck[0].on_air=1;
 p.deck[0].loop_edit=(struct xz_shell_loop_edit){1,p.deck[0].elapsed_ms+700};
 p.deck[1].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_HOT_CUE_CALL);p.deck[1].hot_cue_called=0x03;
 p.settings.hot_cue_auto_load=XZ_SHELL_HCAL_LIBRARY;
 p.fx.known|=(1u<<XZ_SHELL_FX_F_CHANNEL)|(1u<<XZ_SHELL_FX_F_QUANTIZE);p.fx.channel=XZ_SHELL_FX_CH_XF_A;p.fx.quantize=1;
 p.settings.needle_lock_known=1;p.settings.needle_lock=1;
 shot(&u,&p,"parity-play-states");
 send(&u,&p,XZ_SHELL_ENCODER_HOLD,0,0,0);shot(&u,&p,"parity-grid-mode");
 send(&u,&p,XZ_SHELL_ENCODER_HOLD,0,0,0);
 p.deck[1].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_ALERT);
 p.deck[1].alert=(struct xz_shell_alert){XZ_SHELL_ALERT_READ_ERROR,"E-8302",NULL};
 shot(&u,&p,"parity-alert-read");
 p.deck[1].alert=(struct xz_shell_alert){XZ_SHELL_ALERT_EMERGENCY_LOOP,NULL,NULL};
 shot(&u,&p,"parity-alert-emergency");

 /* INFO with linked players 3 and 4 */
 struct xz_shell_info_model im;fxi_artwork(art);fxi_info_full(&im,art);
 im.linked_count=2;im.linked[0].player=3;im.linked[0].track=im.deck[1];im.linked[1].player=4;im.linked[1].track=im.deck[0];
 im.linked[1].track.loaded=0;
 struct xz_shell_model i=m;i.info=&im;
 xz_shell_init(&u);key(&u,&i,XZ_SHELL_KEY_INFO);
 tap(&u,&i,xz_shell_info_rect(XZ_SHELL_INFO_R_TAB,2+im.has_highlight));shot(&u,&i,"parity-info-player3");
 tap(&u,&i,xz_shell_info_rect(XZ_SHELL_INFO_R_TAB,3+im.has_highlight));shot(&u,&i,"parity-info-player4");

 /* SOURCE: the LINK picker */
 struct xz_shell_source_model sm;fxi_source_full(&sm);
 sm.device[2].known=1u<<XZ_SHELL_D_STATUS;sm.device[2].status=XZ_SHELL_DEVICE_READY;
 sm.link_media=media;sm.link_media_count=4;sm.link_current=2;
 struct xz_shell_model s=m;s.source=&sm;s.capabilities|=XZ_SHELL_CAP_SOURCE;
 xz_shell_init(&u);key(&u,&s,XZ_SHELL_KEY_SOURCE);
 tap(&u,&s,xz_shell_source_rect(2));tap(&u,&s,xz_shell_source_rect(2));shot(&u,&s,"parity-link-picker");

 /* MY SETTINGS from a linked player: waiting for PAUSE, then the result */
 s.my_settings=fxm_my_reset();fxm_my.source=XZ_SHELL_SRC_LINK;fxm_my.link_player=3;fxm_my.source_name="PLAYER 3 USB";
 xz_shell_init(&u);xz_shell_browse_reset();key(&u,&s,XZ_SHELL_KEY_SOURCE_USB1);key(&u,&s,XZ_SHELL_KEY_MENU);
 shot(&u,&s,"parity-my-settings-pause");
 s.deck[0].playing=0;fxm_my.state=XZ_SHELL_MY_DONE;fxm_my.result="LOADED: 31 APPLIED, 7 NOT ON THIS DECK";
 shot(&u,&s,"parity-my-settings-done");
 return 0;
}
