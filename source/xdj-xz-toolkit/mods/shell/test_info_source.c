#ifdef NDEBUG
#error Shell acceptance requires active assertions
#endif
#include "shell_info_fixtures.h"
#include "shell_info_chrome.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define STRIDE 804
#define GUARD 64
#define PIXELS (STRIDE*(XZ_SHELL_HEIGHT+2))
static uint16_t buffer_a[GUARD+PIXELS+GUARD], buffer_b[GUARD+PIXELS+GUARD];
static struct fx_track track;
static uint16_t art[FXI_ART_SIDE*FXI_ART_SIDE];
static int browse_slot;

static uint16_t *frame(uint16_t *buffer) { return buffer+GUARD+STRIDE; }
static void fill(uint16_t *buffer) { for(size_t i=0;i<GUARD+PIXELS+GUARD;i++)buffer[i]=0xa55a; }
static void check_guards(uint16_t *buffer) {
 for(size_t i=0;i<GUARD+STRIDE;i++)assert(buffer[i]==0xa55a);
 for(size_t i=GUARD+STRIDE+(size_t)STRIDE*XZ_SHELL_HEIGHT;i<GUARD+PIXELS+GUARD;i++)assert(buffer[i]==0xa55a);
 for(int y=0;y<XZ_SHELL_HEIGHT;y++)for(int x=XZ_SHELL_WIDTH;x<STRIDE;x++)assert(frame(buffer)[y*STRIDE+x]==0xa55a);
}
static uint16_t at(const uint16_t *f, int x, int y) { return f[(size_t)y*STRIDE+(size_t)x]; }
static uint16_t rgb565(uint32_t c) { return (uint16_t)(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31)); }
static void render_checked(const struct xz_shell *u, const struct xz_shell_model *m, uint16_t *buffer) {
 fill(buffer);
 assert(xz_shell_render(u,m,frame(buffer),STRIDE));
 check_guards(buffer);
}
static int count_color(const uint16_t *f, struct xz_shell_rect r, uint32_t color) {
 int n=0;
 for(int y=r.y;y<r.y+r.h;y++)for(int x=r.x;x<r.x+r.w;x++)n+=at(f,x,y)==rgb565(color);
 return n;
}
static int rows_equal(const uint16_t *a, const uint16_t *b, int y0, int y1) {
 for(int y=y0;y<y1;y++)if(memcmp(a+(size_t)y*STRIDE,b+(size_t)y*STRIDE,XZ_SHELL_WIDTH*sizeof *a))return 0;
 return 1;
}
static size_t press(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_key k, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 struct xz_shell_event e={XZ_SHELL_KEY,0,0,0,k,-1};
 return xz_shell_input(u,m,&e,out);
}
static size_t tap(struct xz_shell *u, const struct xz_shell_model *m, struct xz_shell_rect r, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 struct xz_shell_event e={XZ_SHELL_TOUCH_DOWN,r.x+r.w/2,r.y+r.h/2,0,0,-1};
 size_t n=xz_shell_input(u,m,&e,out);
 /* Release only on our own screens: a jump may have moved to a screen whose model is a test stand-in. */
 enum xz_shell_screen now=xz_shell_screen(u);
 if(now==XZ_SHELL_INFO||now==XZ_SHELL_SOURCE||now==XZ_SHELL_PLAY){
  struct xz_shell_event up={XZ_SHELL_TOUCH_UP,r.x+r.w/2,r.y+r.h/2,0,0,-1};
  struct xz_shell_action ignored[XZ_SHELL_ACTIONS];
  assert(xz_shell_input(u,m,&up,ignored)==0);
 }
 return n;
}
static size_t turn(struct xz_shell *u, const struct xz_shell_model *m, int delta, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 struct xz_shell_event e={XZ_SHELL_ENCODER_TURN,0,0,delta,0,-1};
 return xz_shell_input(u,m,&e,out);
}
static size_t push(struct xz_shell *u, const struct xz_shell_model *m, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 struct xz_shell_event e={XZ_SHELL_ENCODER_PUSH,0,0,0,0,-1};
 return xz_shell_input(u,m,&e,out);
}
static struct xz_shell_rect value_rect(enum xz_shell_info_field f) { return xz_shell_info_rect(XZ_SHELL_INFO_R_VALUE,(int)f); }
static void model(struct xz_shell_model *m, struct xz_shell_info_model *im, struct xz_shell_source_model *sm) {
 fx_model_full(m,&track);
 fxi_info_full(im,art);
 fxi_source_full(sm);
 m->info=im;m->source=sm;
 m->browse=(const struct xz_shell_browse_model *)&browse_slot;
 m->capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_SOURCE;
}
static void open_screen(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_key k, enum xz_shell_screen s) {
 struct xz_shell_action out[XZ_SHELL_ACTIONS];
 xz_shell_init(u);
 /* BROWSE and PLAYLIST open over the SOURCE picker when there is one. */
 int over_source=(s==XZ_SHELL_BROWSE||s==XZ_SHELL_PLAYLIST)&&m->source&&(m->capabilities&XZ_SHELL_CAP_SOURCE);
 assert(press(u,m,k,out)==0&&xz_shell_screen(u)==s&&u->depth==(over_source?3:2));
}

static void test_info_navigation(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_info_model im;struct xz_shell_source_model sm;
 struct xz_shell_action out[XZ_SHELL_ACTIONS];
 model(&m,&im,&sm);
 open_screen(&u,&m,XZ_SHELL_KEY_INFO,XZ_SHELL_INFO);
 render_checked(&u,&m,buffer_a);
 assert(press(&u,&m,XZ_SHELL_KEY_INFO,out)==0&&xz_shell_screen(&u)==XZ_SHELL_PLAY);
 open_screen(&u,&m,XZ_SHELL_KEY_INFO,XZ_SHELL_INFO);
 assert(press(&u,&m,XZ_SHELL_KEY_BACK,out)==0&&xz_shell_screen(&u)==XZ_SHELL_PLAY);
 open_screen(&u,&m,XZ_SHELL_KEY_INFO,XZ_SHELL_INFO);
 assert(tap(&u,&m,xz_shell_info_rect(XZ_SHELL_INFO_R_CLOSE,0),out)==0&&xz_shell_screen(&u)==XZ_SHELL_PLAY);
 /* On a list INFO slides the info panel over it instead; INFO again or BACK puts it away. */
 open_screen(&u,&m,XZ_SHELL_KEY_BROWSE,XZ_SHELL_BROWSE);
 int depth=u.depth;
 assert(press(&u,&m,XZ_SHELL_KEY_INFO,out)==0&&xz_shell_screen(&u)==XZ_SHELL_BROWSE&&u.depth==depth&&u.panel.kind==XZ_SHELL_PANEL_INFO);
 assert(press(&u,&m,XZ_SHELL_KEY_INFO,out)==0&&u.panel.kind==XZ_SHELL_PANEL_NONE);
 press(&u,&m,XZ_SHELL_KEY_INFO,out);
 assert(press(&u,&m,XZ_SHELL_KEY_BACK,out)==0&&xz_shell_screen(&u)==XZ_SHELL_BROWSE&&u.panel.kind==XZ_SHELL_PANEL_NONE);
 /* No info slot: the stock screen takes over. */
 struct xz_shell_model bare=m;bare.info=NULL;
 xz_shell_init(&u);
 assert(press(&u,&bare,XZ_SHELL_KEY_INFO,out)==1&&out[0].kind==XZ_SHELL_ACT_HAND_OFF&&out[0].id==XZ_SHELL_INFO&&u.depth==1);
 /* A stale stack over a NULL slot still renders and ignores input except close. */
 open_screen(&u,&m,XZ_SHELL_KEY_INFO,XZ_SHELL_INFO);
 render_checked(&u,&bare,buffer_a);
 assert(tap(&u,&bare,value_rect(XZ_SHELL_I_ARTIST),out)==0&&turn(&u,&bare,1,out)==0&&push(&u,&bare,out)==0);
 assert(tap(&u,&bare,xz_shell_info_rect(XZ_SHELL_INFO_R_CLOSE,0),out)==0&&xz_shell_screen(&u)==XZ_SHELL_PLAY);
 puts("PASS info opens, closes by INFO, BACK and close, hands off without a slot");
}
static void test_info_tabs(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_info_model im;struct xz_shell_source_model sm;
 struct xz_shell_action out[XZ_SHELL_ACTIONS];
 model(&m,&im,&sm);
 /* The default tab is the active deck; DECK tabs are local and never move it. */
 open_screen(&u,&m,XZ_SHELL_KEY_INFO,XZ_SHELL_INFO);
 render_checked(&u,&m,buffer_a);
 open_screen(&u,&m,XZ_SHELL_KEY_INFO,XZ_SHELL_INFO);u.deck=1;
 render_checked(&u,&m,buffer_b);
 assert(!rows_equal(frame(buffer_a),frame(buffer_b),SI_HEAD_H,SI_STRIP_Y));
 assert(tap(&u,&m,xz_shell_info_rect(XZ_SHELL_INFO_R_TAB,0),out)==0&&u.deck==1);
 render_checked(&u,&m,buffer_b);
 assert(rows_equal(frame(buffer_a),frame(buffer_b),SI_HEAD_H,SI_STRIP_Y));
 /* Touching a deck strip shows that deck's track too. */
 assert(tap(&u,&m,xz_shell_info_rect(XZ_SHELL_INFO_R_STRIP,1),out)==0&&u.stack[1].focus==2&&u.deck==1);
 /* With a highlighted list track it is the default, and LIST is a third tab. */
 im.has_highlight=1;
 open_screen(&u,&m,XZ_SHELL_KEY_INFO,XZ_SHELL_INFO);
 render_checked(&u,&m,buffer_a);
 assert(tap(&u,&m,xz_shell_info_rect(XZ_SHELL_INFO_R_TAB,0),out)==0);
 render_checked(&u,&m,buffer_b);
 assert(!rows_equal(frame(buffer_a),frame(buffer_b),SI_HEAD_H,SI_STRIP_Y));
 assert(tap(&u,&m,xz_shell_info_rect(XZ_SHELL_INFO_R_TAB,2),out)==0&&u.stack[1].focus==3);
 render_checked(&u,&m,buffer_b);
 assert(rows_equal(frame(buffer_a),frame(buffer_b),0,XZ_SHELL_HEIGHT));
 /* The LIST tab is not there without a highlighted track. */
 im.has_highlight=0;
 render_checked(&u,&m,buffer_b);   /* focus 3 falls back to the active deck */
 assert(tap(&u,&m,xz_shell_info_rect(XZ_SHELL_INFO_R_TAB,2),out)==0&&u.stack[1].focus==3);
 puts("PASS info tabs: default deck or highlighted track, local tab state, strip touch");
}
static void test_info_jump(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_info_model im;struct xz_shell_source_model sm;
 struct xz_shell_action out[XZ_SHELL_ACTIONS];
 model(&m,&im,&sm);
 /* First touch highlights, second confirms: INFO_JUMP then the BROWSE navigation. */
 open_screen(&u,&m,XZ_SHELL_KEY_INFO,XZ_SHELL_INFO);
 assert(tap(&u,&m,value_rect(XZ_SHELL_I_ARTIST),out)==0&&u.stack[1].cursor==1);
 render_checked(&u,&m,buffer_a);
 assert(count_color(frame(buffer_a),value_rect(XZ_SHELL_I_ARTIST),SH_BLUE)>1000);
 assert(tap(&u,&m,value_rect(XZ_SHELL_I_ARTIST),out)==1);
 assert(out[0].kind==(enum xz_shell_action_kind)XZ_SHELL_ACT_INFO_JUMP&&out[0].id==XZ_SHELL_I_ARTIST&&out[0].value==11&&out[0].deck==0);
 assert(xz_shell_screen(&u)==XZ_SHELL_BROWSE&&u.depth==3&&u.stack[1].screen==XZ_SHELL_SOURCE);   /* over the picker */
 /* Touching another link moves the highlight instead of confirming. */
 open_screen(&u,&m,XZ_SHELL_KEY_INFO,XZ_SHELL_INFO);
 assert(tap(&u,&m,value_rect(XZ_SHELL_I_ARTIST),out)==0&&tap(&u,&m,value_rect(XZ_SHELL_I_BPM),out)==0&&u.stack[1].cursor==4);
 assert(tap(&u,&m,value_rect(XZ_SHELL_I_BPM),out)==1&&out[0].id==XZ_SHELL_I_BPM&&out[0].value==12800);
 /* Encoder: turn moves the focus in reading order and clamps; push confirms. */
 open_screen(&u,&m,XZ_SHELL_KEY_INFO,XZ_SHELL_INFO);u.deck=1;
 assert(push(&u,&m,out)==0);                       /* nothing focused yet */
 assert(turn(&u,&m,1,out)==0&&u.stack[1].cursor==1);
 assert(turn(&u,&m,100,out)==0&&u.stack[1].cursor==5);   /* ARTIST ALBUM KEY BPM GENRE */
 assert(turn(&u,&m,-100,out)==0&&u.stack[1].cursor==1);
 assert(turn(&u,&m,2,out)==0&&u.stack[1].cursor==3);
 assert(push(&u,&m,out)==1&&out[0].kind==(enum xz_shell_action_kind)XZ_SHELL_ACT_INFO_JUMP&&out[0].id==XZ_SHELL_I_KEY&&out[0].value==42&&out[0].deck==1);
 assert(xz_shell_screen(&u)==XZ_SHELL_BROWSE);
 /* The highlighted list track jumps with deck -1. */
 im.has_highlight=1;
 open_screen(&u,&m,XZ_SHELL_KEY_INFO,XZ_SHELL_INFO);
 turn(&u,&m,5,out);
 assert(push(&u,&m,out)==1&&out[0].id==XZ_SHELL_I_GENRE&&out[0].value==33&&out[0].deck==-1);
 im.has_highlight=0;
 /* BROWSE unavailable as a shell screen: the jump still goes out, then HAND_OFF. */
 struct xz_shell_model stock=m;stock.browse=NULL;
 open_screen(&u,&stock,XZ_SHELL_KEY_INFO,XZ_SHELL_INFO);
 turn(&u,&stock,1,out);
 assert(push(&u,&stock,out)==2&&out[0].kind==(enum xz_shell_action_kind)XZ_SHELL_ACT_INFO_JUMP);
 assert(out[1].kind==XZ_SHELL_ACT_HAND_OFF&&out[1].id==XZ_SHELL_BROWSE&&xz_shell_screen(&u)==XZ_SHELL_INFO);
 /* A field without its link bit is plain text and skipped by the focus. */
 im.deck[0].links&=~XZ_SHELL_KNOWN(XZ_SHELL_I_ALBUM);
 open_screen(&u,&m,XZ_SHELL_KEY_INFO,XZ_SHELL_INFO);
 assert(tap(&u,&m,value_rect(XZ_SHELL_I_ALBUM),out)==0&&u.stack[1].cursor==0);
 turn(&u,&m,2,out);
 assert(push(&u,&m,out)==1&&out[0].id==XZ_SHELL_I_KEY);
 render_checked(&u,&m,buffer_a);
 assert(count_color(frame(buffer_a),value_rect(XZ_SHELL_I_ALBUM),SH_BLUE)==0);
 /* No BROWSE capability: nothing is a link, nothing is styled as one, taps do nothing. */
 model(&m,&im,&sm);
 open_screen(&u,&m,XZ_SHELL_KEY_INFO,XZ_SHELL_INFO);
 render_checked(&u,&m,buffer_a);
 assert(count_color(frame(buffer_a),value_rect(XZ_SHELL_I_ARTIST),SH_BLUE)>0);
 assert(count_color(frame(buffer_a),value_rect(XZ_SHELL_I_KEY),SH_BLUE)>0);
 struct xz_shell_model plain=m;plain.capabilities&=~(uint32_t)XZ_SHELL_CAP_BROWSE;
 render_checked(&u,&plain,buffer_a);
 for(int f=0;f<XZ_SHELL_INFO_FIELD_COUNT;f++){
  struct xz_shell_rect r=value_rect((enum xz_shell_info_field)f);
  if(r.w)assert(count_color(frame(buffer_a),r,SH_BLUE)==0);
 }
 for(int i=0;i<2;i++){
  assert(tap(&u,&plain,value_rect(XZ_SHELL_I_ARTIST),out)==0&&tap(&u,&plain,value_rect(XZ_SHELL_I_BPM),out)==0);
  assert(turn(&u,&plain,1,out)==0&&push(&u,&plain,out)==0&&u.stack[1].cursor==0&&xz_shell_screen(&u)==XZ_SHELL_INFO);
 }
 puts("PASS information jump: highlight then confirm, encoder focus clamps, link styling follows the capability");
}
static void test_info_rating(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_info_model im;struct xz_shell_source_model sm;
 struct xz_shell_action out[XZ_SHELL_ACTIONS];
 model(&m,&im,&sm);
 /* Read-only by default. */
 open_screen(&u,&m,XZ_SHELL_KEY_INFO,XZ_SHELL_INFO);
 for(int s=0;s<XZ_SHELL_RATING_MAX;s++)assert(tap(&u,&m,xz_shell_info_rect(XZ_SHELL_INFO_R_STAR,s),out)==0);
 assert(u.stack[1].cursor==0);
 /* Writable: the rating joins the focus order; a star sets it, the lit top star clears one. */
 im.deck[0].rating_writable=1;
 open_screen(&u,&m,XZ_SHELL_KEY_INFO,XZ_SHELL_INFO);
 assert(tap(&u,&m,xz_shell_info_rect(XZ_SHELL_INFO_R_STAR,2),out)==0&&u.stack[1].cursor==6);
 render_checked(&u,&m,buffer_a);
 assert(tap(&u,&m,xz_shell_info_rect(XZ_SHELL_INFO_R_STAR,1),out)==1);
 assert(out[0].kind==(enum xz_shell_action_kind)XZ_SHELL_ACT_SET_RATING&&out[0].value==2&&out[0].id==1&&out[0].deck==0);
 assert(xz_shell_screen(&u)==XZ_SHELL_INFO);
 assert(tap(&u,&m,xz_shell_info_rect(XZ_SHELL_INFO_R_STAR,3),out)==1&&out[0].value==3);
 assert(push(&u,&m,out)==1&&out[0].value==5);
 im.deck[0].rating=5;assert(push(&u,&m,out)==1&&out[0].value==0);
 /* Without a known track id the stars stay read-only. */
 im.deck[0].known&=~XZ_SHELL_KNOWN(XZ_SHELL_I_TRACK_ID);
 open_screen(&u,&m,XZ_SHELL_KEY_INFO,XZ_SHELL_INFO);
 assert(tap(&u,&m,xz_shell_info_rect(XZ_SHELL_INFO_R_STAR,0),out)==0&&tap(&u,&m,xz_shell_info_rect(XZ_SHELL_INFO_R_STAR,0),out)==0);
 puts("PASS rating is read-only unless writable; writable stars emit SET_RATING");
}
static void test_info_unknown_and_bounds(void) {
 struct xz_shell u;struct xz_shell_model m,z;struct xz_shell_info_model im,zero;struct xz_shell_source_model sm;
 struct xz_shell_action out[XZ_SHELL_ACTIONS];
 model(&m,&im,&sm);
 /* Garbage values with clear bits draw exactly what an all-zero model draws. */
 memset(&im,0xa5,sizeof im);
 for(int i=0;i<3;i++){
  struct xz_shell_info_track *t=i<2?&im.deck[i]:&im.highlight;
  t->known=0;t->links=~0ull;
  t->title=t->artist=t->album=t->genre=t->label=t->key=t->comment=t->date_added=t->file_name=NULL;t->artwork=NULL;
 }
 im.has_highlight=0;
 memset(&zero,0,sizeof zero);
 memset(&z,0,sizeof z);z.info=&zero;
 memset(&m.deck,0,sizeof m.deck);m.settings.known=0;m.capabilities=XZ_SHELL_CAP_BROWSE;m.link_known=0;m.clock_ms=0;
 z.capabilities=XZ_SHELL_CAP_BROWSE;
 xz_shell_init(&u);u.stack[1]=(struct xz_shell_view){XZ_SHELL_INFO,0,0,0};u.depth=2;
 render_checked(&u,&m,buffer_a);
 render_checked(&u,&z,buffer_b);
 assert(!memcmp(buffer_a,buffer_b,sizeof buffer_a));
 assert(tap(&u,&m,value_rect(XZ_SHELL_I_ARTIST),out)==0&&turn(&u,&m,3,out)==0&&u.stack[1].cursor==0);
 /* Known bits over NULL strings and wild numbers are tolerated, and are not links. */
 for(int i=0;i<2;i++){im.deck[i].known=fxi_all();im.deck[i].rating=99;im.deck[i].color=(enum xz_shell_color_tag)77;
  im.deck[i].source=(enum xz_shell_source)9;im.deck[i].artwork_side=-4;im.deck[i].bpm_x100=-5;im.deck[i].duration_ms=-1;
  im.deck[i].bitrate_kbps=2000000000;}
 render_checked(&u,&m,buffer_a);
 assert(tap(&u,&m,value_rect(XZ_SHELL_I_ARTIST),out)==0&&u.stack[1].cursor==0);
 /* Not loaded: the page says so and offers no links. */
 model(&m,&im,&sm);
 im.deck[0].loaded=0;
 open_screen(&u,&m,XZ_SHELL_KEY_INFO,XZ_SHELL_INFO);
 render_checked(&u,&m,buffer_a);
 assert(count_color(frame(buffer_a),(struct xz_shell_rect){0,SI_HEAD_H,XZ_SHELL_WIDTH,SI_STRIP_Y-SI_HEAD_H},SH_RED)>50);
 assert(tap(&u,&m,value_rect(XZ_SHELL_I_ARTIST),out)==0&&tap(&u,&m,value_rect(XZ_SHELL_I_ARTIST),out)==0);
 /* Loaded state falls back to the deck model when INFO does not know it. */
 im.deck[0].known&=~XZ_SHELL_KNOWN(XZ_SHELL_I_LOADED);m.deck[0].loaded=0;
 render_checked(&u,&m,buffer_b);
 assert(rows_equal(frame(buffer_a),frame(buffer_b),SI_HEAD_H,SI_STRIP_Y-1));
 /* Long strings stay inside their cells: the gutters and the right margin stay black. */
 model(&m,&im,&sm);
 static char wide[600];memset(wide,'W',sizeof wide-1);
 static char words[600];for(size_t i=0;i<sizeof words-1;i++)words[i]=i%7==6?' ':'M';
 struct xz_shell_info_track *t=&im.deck[0];
 t->title=words;t->artist=wide;t->album=wide;t->genre=wide;t->label=wide;t->key="Abmaj7/Eb (alt)";t->comment=words;
 t->file_name=wide;t->date_added=wide;t->bpm_x100=99999;t->bitrate_kbps=99999;t->duration_ms=999*60000;
 t->title="\xc3\x89t\xc3\xa9 \xe2\x99\xaa \xf0\x9f\x8e\xb5 odd \xff\xfe bytes and a very long run of words to wrap onto a second line and then some more";
 open_screen(&u,&m,XZ_SHELL_KEY_INFO,XZ_SHELL_INFO);
 for(int pass=0;pass<2;pass++){
  render_checked(&u,&m,buffer_a);
  const uint16_t *f=frame(buffer_a);
  for(int y=SI_HEAD_H;y<SI_STRIP_Y;y++){
   for(int x=XZ_SHELL_WIDTH-7;x<XZ_SHELL_WIDTH;x++)assert(at(f,x,y)==rgb565(SH_BLACK));
   for(int x=228;x<240;x++)assert(at(f,x,y)==rgb565(SH_BLACK));
  }
  t->title=words;turn(&u,&m,100,out);   /* second pass: focused last link, wrapped words */
 }
 puts("PASS info unknown fields, not loaded, long text stays in its cells");
}
static void test_source(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_info_model im;struct xz_shell_source_model sm;
 struct xz_shell_action out[XZ_SHELL_ACTIONS];
 model(&m,&im,&sm);
 open_screen(&u,&m,XZ_SHELL_KEY_SOURCE,XZ_SHELL_SOURCE);
 render_checked(&u,&m,buffer_a);
 assert(press(&u,&m,XZ_SHELL_KEY_SOURCE,out)==0&&xz_shell_screen(&u)==XZ_SHELL_PLAY);
 open_screen(&u,&m,XZ_SHELL_KEY_SOURCE,XZ_SHELL_SOURCE);
 assert(press(&u,&m,XZ_SHELL_KEY_BACK,out)==0&&xz_shell_screen(&u)==XZ_SHELL_PLAY);
 open_screen(&u,&m,XZ_SHELL_KEY_SOURCE,XZ_SHELL_SOURCE);
 assert(tap(&u,&m,xz_shell_source_rect(-1),out)==0&&xz_shell_screen(&u)==XZ_SHELL_PLAY);
 /* The cursor starts on the current browse source; push selects it and opens BROWSE. */
 open_screen(&u,&m,XZ_SHELL_KEY_SOURCE,XZ_SHELL_SOURCE);
 assert(push(&u,&m,out)==1&&out[0].kind==(enum xz_shell_action_kind)XZ_SHELL_ACT_SET_SOURCE&&out[0].id==XZ_SHELL_SRC_USB1&&out[0].deck==-1);
 assert(xz_shell_screen(&u)==XZ_SHELL_BROWSE&&u.depth==3&&u.stack[1].screen==XZ_SHELL_SOURCE);   /* over the picker */
 /* Turning walks connected tiles only, skipping NOT CONNECTED, and clamps. */
 open_screen(&u,&m,XZ_SHELL_KEY_SOURCE,XZ_SHELL_SOURCE);
 assert(turn(&u,&m,1,out)==0&&u.stack[1].cursor==2);
 assert(push(&u,&m,out)==0&&xz_shell_screen(&u)==XZ_SHELL_SOURCE);     /* LOADING is not ready */
 assert(turn(&u,&m,1,out)==0&&u.stack[1].cursor==4);                   /* LINK skipped */
 assert(turn(&u,&m,1,out)==0&&u.stack[1].cursor==4);
 assert(turn(&u,&m,-9,out)==0&&u.stack[1].cursor==1);
 assert(turn(&u,&m,-1,out)==0&&u.stack[1].cursor==1);
 render_checked(&u,&m,buffer_a);
 /* Touch: NOT CONNECTED does nothing; first touch highlights, second selects. */
 assert(tap(&u,&m,xz_shell_source_rect(2),out)==0&&u.stack[1].cursor==1);
 assert(tap(&u,&m,xz_shell_source_rect(3),out)==0&&u.stack[1].cursor==4);
 render_checked(&u,&m,buffer_b);
 assert(count_color(frame(buffer_b),xz_shell_source_rect(3),SH_BLUE)>count_color(frame(buffer_a),xz_shell_source_rect(3),SH_BLUE));
 assert(tap(&u,&m,xz_shell_source_rect(3),out)==1&&out[0].id==XZ_SHELL_SRC_COMPUTER&&xz_shell_screen(&u)==XZ_SHELL_BROWSE);
 open_screen(&u,&m,XZ_SHELL_KEY_SOURCE,XZ_SHELL_SOURCE);
 assert(tap(&u,&m,xz_shell_source_rect(1),out)==0&&tap(&u,&m,xz_shell_source_rect(1),out)==0&&xz_shell_screen(&u)==XZ_SHELL_SOURCE);
 /* BROWSE unavailable as a shell screen: SET_SOURCE then HAND_OFF. */
 struct xz_shell_model stock=m;stock.browse=NULL;
 open_screen(&u,&stock,XZ_SHELL_KEY_SOURCE,XZ_SHELL_SOURCE);
 assert(push(&u,&stock,out)==2&&out[0].kind==(enum xz_shell_action_kind)XZ_SHELL_ACT_SET_SOURCE&&out[1].kind==XZ_SHELL_ACT_HAND_OFF&&out[1].id==XZ_SHELL_BROWSE);
 /* Current source unknown or disconnected: the first connected tile is the default. */
 sm.current_known=0;
 open_screen(&u,&m,XZ_SHELL_KEY_SOURCE,XZ_SHELL_SOURCE);
 assert(push(&u,&m,out)==1&&out[0].id==XZ_SHELL_SRC_USB1);
 sm.current_known=1;sm.current=XZ_SHELL_SRC_LINK;sm.device[0].status=XZ_SHELL_DEVICE_NOT_CONNECTED;
 open_screen(&u,&m,XZ_SHELL_KEY_SOURCE,XZ_SHELL_SOURCE);
 assert(push(&u,&m,out)==0&&turn(&u,&m,1,out)==0&&u.stack[1].cursor==4&&push(&u,&m,out)==1&&out[0].id==XZ_SHELL_SRC_COMPUTER);
 /* No source slot or capability: hand off. */
 struct xz_shell_model bare=m;bare.source=NULL;
 xz_shell_init(&u);
 assert(press(&u,&bare,XZ_SHELL_KEY_SOURCE,out)==1&&out[0].kind==XZ_SHELL_ACT_HAND_OFF&&out[0].id==XZ_SHELL_SOURCE);
 bare=m;bare.capabilities&=~(uint32_t)XZ_SHELL_CAP_SOURCE;
 assert(press(&u,&bare,XZ_SHELL_KEY_SOURCE,out)==1&&out[0].id==XZ_SHELL_SOURCE&&u.depth==1);
 puts("PASS source: default cursor, skip not connected, highlight then select, BROWSE or hand-off");
}
static void test_source_unknown_and_bounds(void) {
 struct xz_shell u;struct xz_shell_model m,z;struct xz_shell_info_model im;struct xz_shell_source_model sm,zero;
 struct xz_shell_action out[XZ_SHELL_ACTIONS];
 model(&m,&im,&sm);
 memset(&sm,0x5a,sizeof sm);
 for(int i=0;i<XZ_SHELL_SOURCE_TILES;i++){sm.device[i].known=0;sm.device[i].name=NULL;}
 sm.current_known=0;
 memset(&zero,0,sizeof zero);
 memset(&m.deck,0,sizeof m.deck);m.settings.known=0;m.link_known=0;m.clock_ms=0;
 memset(&z,0,sizeof z);z.source=&zero;z.capabilities=m.capabilities;
 xz_shell_init(&u);u.stack[1]=(struct xz_shell_view){XZ_SHELL_SOURCE,0,0,0};u.depth=2;
 render_checked(&u,&m,buffer_a);
 render_checked(&u,&z,buffer_b);
 assert(!memcmp(buffer_a,buffer_b,sizeof buffer_a));
 for(int i=0;i<XZ_SHELL_SOURCE_TILES;i++)assert(tap(&u,&m,xz_shell_source_rect(i),out)==0);
 assert(turn(&u,&m,1,out)==0&&push(&u,&m,out)==0&&xz_shell_screen(&u)==XZ_SHELL_SOURCE);
 /* Long names, huge sizes and counts stay inside their tiles. */
 model(&m,&im,&sm);
 static char wide[400];memset(wide,'W',sizeof wide-1);
 for(int i=0;i<XZ_SHELL_SOURCE_TILES;i++){
  sm.device[i].known=(1u<<XZ_SHELL_DEVICE_FIELD_COUNT)-1;sm.device[i].status=(enum xz_shell_device_status)(i%3);
  sm.device[i].name=wide;sm.device[i].free_bytes=~0ull;sm.device[i].total_bytes=~0ull;sm.device[i].tracks=2147483647;
  sm.device[i].color=0xffffffff;
 }
 sm.device[3].free_bytes=5;sm.device[3].total_bytes=0;
 open_screen(&u,&m,XZ_SHELL_KEY_SOURCE,XZ_SHELL_SOURCE);
 render_checked(&u,&m,buffer_a);
 const uint16_t *f=frame(buffer_a);
 for(int i=0;i<=XZ_SHELL_SOURCE_TILES;i++){
  int gap=i==0?0:xz_shell_source_rect(i-1).x+xz_shell_source_rect(i-1).w;
  int end=i==XZ_SHELL_SOURCE_TILES?XZ_SHELL_WIDTH:xz_shell_source_rect(i).x;
  for(int y=SI_HEAD_H;y<SI_STRIP_Y;y++)for(int x=gap;x<end;x++)assert(at(f,x,y)==rgb565(SH_BLACK));
 }
 for(int i=0;i<XZ_SHELL_SOURCE_TILES;i++){
  struct xz_shell_rect r=xz_shell_source_rect(i);
  assert(r.x>=0&&r.y>=SI_HEAD_H&&r.x+r.w<=XZ_SHELL_WIDTH&&r.y+r.h<=SI_STRIP_Y&&r.h>=44);
 }
 /* Rendering never writes the model. */
 struct xz_shell_source_model copy=sm;
 render_checked(&u,&m,buffer_b);
 assert(!memcmp(&copy,&sm,sizeof sm)&&!memcmp(buffer_a,buffer_b,sizeof buffer_a));
 puts("PASS source unknown fields and bounds");
}
static void test_strip_and_targets(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_info_model im;struct xz_shell_source_model sm;
 model(&m,&im,&sm);
 /* Both screens keep the two-deck strip; a not-loaded deck says so in red. */
 m.deck[1].loaded=0;
 static const enum xz_shell_key keys[]={XZ_SHELL_KEY_INFO,XZ_SHELL_KEY_SOURCE};
 static const enum xz_shell_screen screens[]={XZ_SHELL_INFO,XZ_SHELL_SOURCE};
 for(int i=0;i<2;i++){
  open_screen(&u,&m,keys[i],screens[i]);
  render_checked(&u,&m,buffer_a);
  assert(count_color(frame(buffer_a),xz_shell_info_rect(XZ_SHELL_INFO_R_STRIP,1),SH_RED)>50);
  assert(count_color(frame(buffer_a),xz_shell_info_rect(XZ_SHELL_INFO_R_STRIP,0),SH_PLAYHEAD)>10);
 }
 /* Every touch target is at least 44 px tall and inside the frame. */
 for(int i=0;i<3;i++){struct xz_shell_rect r=xz_shell_info_rect(XZ_SHELL_INFO_R_TAB,i);assert(r.h>=44&&r.x+r.w<=si_close_rect().x);}
 static const enum xz_shell_info_field links[]={XZ_SHELL_I_ARTIST,XZ_SHELL_I_ALBUM,XZ_SHELL_I_GENRE,XZ_SHELL_I_KEY,XZ_SHELL_I_BPM,XZ_SHELL_I_RATING};
 for(size_t i=0;i<sizeof links/sizeof *links;i++){
  struct xz_shell_rect r=value_rect(links[i]);
  assert(r.h>=44&&r.y>=SI_HEAD_H&&r.y+r.h<=SI_STRIP_Y&&r.x>=0&&r.x+r.w<=XZ_SHELL_WIDTH);
 }
 for(int s=0;s<XZ_SHELL_RATING_MAX;s++)assert(xz_shell_info_rect(XZ_SHELL_INFO_R_STAR,s).h>=44);
 assert(si_close_rect().h>=44&&xz_shell_info_rect(XZ_SHELL_INFO_R_TAB,3).w==0&&xz_shell_info_rect(XZ_SHELL_INFO_R_VALUE,XZ_SHELL_I_TRACK_ID).w==0);
 /* Rendering is deterministic and leaves the model alone. */
 struct xz_shell_info_model copy=im;
 open_screen(&u,&m,XZ_SHELL_KEY_INFO,XZ_SHELL_INFO);
 render_checked(&u,&m,buffer_a);render_checked(&u,&m,buffer_b);
 assert(!memcmp(buffer_a,buffer_b,sizeof buffer_a)&&!memcmp(&copy,&im,sizeof im));
 puts("PASS deck strip on both screens, touch targets, determinism");
}
int main(void) {
 fx_track_build(&track,7);
 fxi_artwork(art);
 test_info_navigation();
 test_info_tabs();
 test_info_jump();
 test_info_rating();
 test_info_unknown_and_bounds();
 test_source();
 test_source_unknown_and_bounds();
 test_strip_and_targets();
 puts("PASS info and source");
 return 0;
}
