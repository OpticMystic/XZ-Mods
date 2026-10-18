/* Renders the BROWSE family's states to PPM files in argv[1]. Host only. */
#include "browse_fixtures.h"
#include "shell_internal.h"
#include <stdio.h>
#include <stdlib.h>

static struct fx_track track;
static struct bx_library lib;
static uint16_t frame[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT];
static const char *out_dir;

static void save(const char *name) {
 char path[1024];FILE *f;
 snprintf(path,sizeof path,"%s/browse-%s.ppm",out_dir,name);
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
static struct xz_shell_action out[XZ_SHELL_ACTIONS];
static void key(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_key k) {
 struct xz_shell_event e={XZ_SHELL_KEY,0,0,0,k,-1};xz_shell_input(u,m,&e,out);
}
static void event(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_event_kind kind, int x, int y, int delta) {
 struct xz_shell_event e={kind,x,y,delta,0,-1};xz_shell_input(u,m,&e,out);
}
static void tap(struct xz_shell *u, const struct xz_shell_model *m, struct xz_shell_rect r) {
 event(u,m,XZ_SHELL_TOUCH_DOWN,r.x+r.w/2,r.y+r.h/2,0);event(u,m,XZ_SHELL_TOUCH_UP,r.x+r.w/2,r.y+r.h/2,0);
}
static void type(struct xz_shell *u, const struct xz_shell_model *m, const char *text) {
 for(;*text;text++)for(int i=0;i<XZ_SHELL_BROWSE_KEYS;i++){
  const char *l=xz_shell_browse_key_label(i);
  if((l[0]==*text&&!l[1])||(*text==' '&&!strcmp(l,"SPACE"))){tap(u,m,xz_shell_browse_rect(u,m,XZ_SHELL_BR_KEY,i));break;}
 }
}
static void open_screen(struct xz_shell *u, struct xz_shell_model *m, enum xz_shell_key k) {
 xz_shell_init(u);xz_shell_browse_reset();key(u,m,k);
}

int main(int argc, char **argv) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;
 if(argc<2){fputs("usage: preview_browse OUTPUT_DIR\n",stderr);return 2;}
 out_dir=argv[1];
 fx_track_build(&track,7);
 bx_build(&lib);
 fx_model_full(&m,&track);
 m.capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_TAG_LIST|XZ_SHELL_CAP_SEARCH|XZ_SHELL_CAP_LOAD;
 m.browse=&b;

 /* BROWSE: the TRACK category, cursor a few rows down. */
 bx_tracks(&b,&lib,XZ_SHELL_BROWSE);
 open_screen(&u,&m,XZ_SHELL_KEY_BROWSE);
 event(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,3);
 shot(&u,&m,"full");
 /* Touch a row: highlight plus the LOAD chip. */
 tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_ROW,6));
 shot(&u,&m,"touched-load");
 u.deck=1;shot(&u,&m,"touched-load-deck2");u.deck=0;
 m.capabilities&=~(uint32_t)XZ_SHELL_CAP_LOAD;shot(&u,&m,"touched-load-off");
 m.notice="SELECT A TRACK, THEN PRESS LOAD";m.notice_at_ms=m.clock_ms;shot(&u,&m,"touched-load-off-notice");
 m.notice=NULL;m.capabilities|=XZ_SHELL_CAP_LOAD;
 /* INFO pane on the highlighted track. */
 tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_INFO,0));
 shot(&u,&m,"info");
 tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_INFO,0));
 /* PREVIEW off with a wide column set. */
 b.columns[0]=XZ_SHELL_COL_NUMBER;b.columns[1]=XZ_SHELL_COL_TITLE;b.columns[2]=XZ_SHELL_COL_ARTIST;b.columns[3]=XZ_SHELL_COL_BPM;
 b.columns[4]=XZ_SHELL_COL_KEY;b.columns[5]=XZ_SHELL_COL_RATING;b.column_count=6;b.sort_column=XZ_SHELL_COL_BPM;b.sort_descending=1;
 tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_PREVIEW,0));
 shot(&u,&m,"preview-off-columns");
 b.columns[2]=XZ_SHELL_COL_COLOR;b.columns[3]=XZ_SHELL_COL_TIME;b.columns[4]=XZ_SHELL_COL_BPM;b.columns[5]=XZ_SHELL_COL_KEY;
 shot(&u,&m,"preview-off-color-time");
 tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_PREVIEW,0));
 bx_tracks(&b,&lib,XZ_SHELL_BROWSE);

 /* Deep cursor: near the end, window moved by the runtime. */
 open_screen(&u,&m,XZ_SHELL_KEY_BROWSE);
 event(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,140);
 bx_window(&b,&lib,BX_TRACKS-XZ_SHELL_BROWSE_WINDOW);
 shot(&u,&m,"deep");
 /* The view ran ahead of the window: rows the runtime has not sent stay blank. */
 bx_window(&b,&lib,0);
 shot(&u,&m,"window-gap");
 bx_window(&b,&lib,0);

 /* Letter jump while the encoder is held, then a page jump without an index. */
 open_screen(&u,&m,XZ_SHELL_KEY_BROWSE);
 event(&u,&m,XZ_SHELL_ENCODER_HOLD,0,0,0);
 event(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,12);
 bx_window(&b,&lib,u.stack[1].scroll-11);
 shot(&u,&m,"jump-letter");
 bx_tracks(&b,&lib,XZ_SHELL_BROWSE);b.letters=NULL;b.letter_count=0;
 open_screen(&u,&m,XZ_SHELL_KEY_BROWSE);
 event(&u,&m,XZ_SHELL_ENCODER_HOLD,0,0,0);
 event(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,3);
 bx_window(&b,&lib,u.stack[1].scroll-11);
 shot(&u,&m,"jump-page");

 /* PLAYLIST category in BROWSE: folders; then the rail scrolled to its blocked entries. */
 bx_folders(&b,&lib,XZ_SHELL_BROWSE);
 open_screen(&u,&m,XZ_SHELL_KEY_BROWSE);
 event(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,2);
 shot(&u,&m,"folders");
 struct xz_shell_rect rail=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_RAIL,5);
 event(&u,&m,XZ_SHELL_TOUCH_DOWN,rail.x+20,rail.y+10,0);
 event(&u,&m,XZ_SHELL_TOUCH_MOVE,rail.x+20,rail.y-200,0);
 event(&u,&m,XZ_SHELL_TOUCH_UP,rail.x+20,rail.y-200,0);
 shot(&u,&m,"rail-scrolled");

 /* Long strings everywhere, and a mostly unknown row. */
 bx_tracks(&b,&lib,XZ_SHELL_BROWSE);
 static char wide[400];memset(wide,'W',sizeof wide-1);
 static struct xz_shell_browse_row odd[XZ_SHELL_BROWSE_WINDOW];
 memcpy(odd,lib.tracks,sizeof odd);
 odd[0].title=wide;odd[1].title="\xc3\x89t\xc3\xa9 \xc3\xa0 Caf\xc3\xa9 \xe2\x99\xaa odd bytes \xff\xfe and a long tail";odd[1].key="Abmaj7/Eb";
 odd[2].known=0;odd[3].known=1u<<XZ_SHELL_RF_TITLE;odd[4].bpm_x100=999999;odd[4].title="Short";
 b.rows=odd;b.window_count=XZ_SHELL_BROWSE_WINDOW;
 b.source_name="REMOTE LIBRARY ON A LONG NAME";b.level_name=wide;b.parent_name="A PARENT FOLDER WITH A LONG NAME";b.depth=2;
 b.known|=1u<<XZ_SHELL_BF_PARENT;
 open_screen(&u,&m,XZ_SHELL_KEY_BROWSE);
 shot(&u,&m,"long-strings");

 /* Nothing known: chrome, blank rows, dashes in the strip. */
 memset(&b,0,sizeof b);b.screen=XZ_SHELL_BROWSE;
 open_screen(&u,&m,XZ_SHELL_KEY_BROWSE);
 shot(&u,&m,"unknown");
 struct xz_shell_model bare;memset(&bare,0,sizeof bare);bare.capabilities=m.capabilities;
 xz_shell_init(&u);u.stack[1]=(struct xz_shell_view){XZ_SHELL_BROWSE,0,0,0};u.depth=2;
 shot(&u,&bare,"null-slot");

 /* PLAYLIST: a playlist's tracks one level down, no rail. */
 bx_tracks(&b,&lib,XZ_SHELL_PLAYLIST);
 open_screen(&u,&m,XZ_SHELL_KEY_PLAYLIST);
 event(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,1);
 shot(&u,&m,"playlist");
 bx_folders(&b,&lib,XZ_SHELL_PLAYLIST);
 open_screen(&u,&m,XZ_SHELL_KEY_PLAYLIST);
 shot(&u,&m,"playlist-folders");

 /* TAG LIST: check marks and the n/100 count; full at 100. */
 bx_tracks(&b,&lib,XZ_SHELL_TAG_LIST);b.total=12;b.window_count=12;
 open_screen(&u,&m,XZ_SHELL_KEY_TAG_LIST);
 tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_ROW,1));
 shot(&u,&m,"tag-list");
 bx_tracks(&b,&lib,XZ_SHELL_TAG_LIST);b.total=100;
 open_screen(&u,&m,XZ_SHELL_KEY_TAG_LIST);
 shot(&u,&m,"tag-list-full");

 /* SEARCH: empty, typing with live results, results with the keyboard put away. */
 bx_tracks(&b,&lib,XZ_SHELL_SEARCH);b.total=0;b.window_count=0;b.level_name="SEARCH";
 open_screen(&u,&m,XZ_SHELL_KEY_SEARCH);
 shot(&u,&m,"search-empty");
 bx_tracks(&b,&lib,XZ_SHELL_SEARCH);b.level_name="SEARCH";
 b.total=11;bx_window(&b,&lib,88);b.window_count=11;b.window_start=0;
 b.columns[0]=XZ_SHELL_COL_TITLE;b.columns[1]=XZ_SHELL_COL_ARTIST;b.columns[2]=XZ_SHELL_COL_BPM;b.columns[3]=XZ_SHELL_COL_KEY;b.column_count=4;
 type(&u,&m,"NEO");
 struct xz_shell_rect n_key=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_KEY,37);
 event(&u,&m,XZ_SHELL_TOUCH_DOWN,n_key.x+10,n_key.y+10,0);
 m.clock_ms=0;
 shot(&u,&m,"search-typing");
 event(&u,&m,XZ_SHELL_TOUCH_UP,0,0,0);
 type(&u,&m,"\x7f");
 for(int i=0;i<XZ_SHELL_BROWSE_KEYS;i++)if(!strcmp(xz_shell_browse_key_label(i),"DELETE"))tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_KEY,i));
 for(int i=0;i<XZ_SHELL_BROWSE_KEYS;i++)if(!strcmp(xz_shell_browse_key_label(i),"SEARCH"))tap(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_KEY,i));
 event(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,2);
 shot(&u,&m,"search-results");

 /* Deck 2 not loaded under a list. */
 bx_tracks(&b,&lib,XZ_SHELL_BROWSE);
 m.deck[1].loaded=0;
 open_screen(&u,&m,XZ_SHELL_KEY_BROWSE);
 shot(&u,&m,"deck2-not-loaded");
 return 0;
}
