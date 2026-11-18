#ifdef NDEBUG
#error Shell acceptance requires active assertions
#endif
/* SOURCE screen: the rear USB port and the DECK 3/4 loopback cable, the top port that holds the cable, an
   unreadable stick, and the screen's bounds. The older navigation tests stay in test_info_source.c. */
#include "fixtures.h"
#include "shell_source.h"
#include "shell_info_chrome.h"
#include "../extra_decks.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define STRIDE 804
#define GUARD 64
#define PIXELS (STRIDE*(XZ_SHELL_HEIGHT+2))
static uint16_t buffer_a[GUARD+PIXELS+GUARD], buffer_b[GUARD+PIXELS+GUARD];
static struct fx_track track;
static char browse_stand_in;

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
static size_t press(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_key k, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 struct xz_shell_event e={XZ_SHELL_KEY,0,0,0,k,-1};
 return xz_shell_input(u,m,&e,out);
}
static size_t tap(struct xz_shell *u, const struct xz_shell_model *m, struct xz_shell_rect r, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 struct xz_shell_event e={XZ_SHELL_TOUCH_DOWN,r.x+r.w/2,r.y+r.h/2,0,0,-1};
 size_t n=xz_shell_input(u,m,&e,out);
 if(xz_shell_screen(u)==XZ_SHELL_SOURCE){
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
/* The runtime's working set: USB 1 holds the music, USB 2 is empty, LINK and REKORDBOX open stock's screens. */
static void model(struct xz_shell_model *m, struct xz_shell_source_model *sm) {
 fx_model_full(m,&track);
 memset(sm,0,sizeof *sm);
 struct xz_shell_device *u1=&sm->device[0];
 u1->known=(1u<<XZ_SHELL_DEVICE_FIELD_COUNT)-1;u1->status=XZ_SHELL_DEVICE_READY;u1->name="XZ SET 2026";
 u1->color=0xff8c1a;u1->free_bytes=44345298944ull;u1->total_bytes=63999836160ull;u1->tracks=1284;
 sm->device[1].known=1u<<XZ_SHELL_D_STATUS;sm->device[1].status=XZ_SHELL_DEVICE_NOT_CONNECTED;
 for(int i=2;i<4;i++){sm->device[i].known=1u<<XZ_SHELL_D_STATUS;sm->device[i].status=XZ_SHELL_DEVICE_STOCK;}
 sm->current_known=1;sm->current=XZ_SHELL_SRC_USB1;
 m->source=sm;m->browse=(const struct xz_shell_browse_model *)&browse_stand_in;
 m->capabilities|=XZ_SHELL_CAP_SOURCE|XZ_SHELL_CAP_BROWSE;
}
static void open_source(struct xz_shell *u, const struct xz_shell_model *m) {
 struct xz_shell_action out[XZ_SHELL_ACTIONS];
 xz_shell_init(u);
 assert(press(u,m,XZ_SHELL_KEY_SOURCE,out)==0&&xz_shell_screen(u)==XZ_SHELL_SOURCE);
}

static void test_rear_states(void) {
 struct xz_shell_model m;memset(&m,0,sizeof m);
 assert(xz_shell_source_rear(NULL)==XZ_SHELL_REAR_UNUSED);
 assert(xz_shell_source_rear(&m)==XZ_SHELL_REAR_UNUSED);                      /* zero model: NO_ENGINE */
 static const struct { int state; enum xz_shell_rear_port rear; } map[]={
  {XZ_EXTRA_ROUTE_NO_ENGINE,XZ_SHELL_REAR_UNUSED},{XZ_EXTRA_ROUTE_NOT_DETECTED,XZ_SHELL_REAR_FREE},
  {XZ_EXTRA_ROUTE_SCANNING,XZ_SHELL_REAR_FREE},{XZ_EXTRA_ROUTE_STARTING,XZ_SHELL_REAR_STARTING},
  {XZ_EXTRA_ROUTE_WAITING_NATIVE_STOP,XZ_SHELL_REAR_STARTING},{XZ_EXTRA_ROUTE_READY,XZ_SHELL_REAR_CABLE},
  {XZ_EXTRA_ROUTE_FAILED,XZ_SHELL_REAR_FAILED},{XZ_EXTRA_ROUTE_LOST,XZ_SHELL_REAR_FREE},
  {-1,XZ_SHELL_REAR_UNUSED},{99,XZ_SHELL_REAR_UNUSED}};
 for(size_t i=0;i<sizeof map/sizeof *map;i++){m.route_state=map[i].state;assert(xz_shell_source_rear(&m)==map[i].rear);}
 puts("PASS rear USB port: every route state, only READY claims the cable");
}

static void test_mark_loopback(void) {
 struct xz_shell_model m;struct xz_shell_source_model sm;
 model(&m,&sm);
 /* Not READY: never marked. */
 for(int s=XZ_EXTRA_ROUTE_NO_ENGINE;s<=XZ_EXTRA_ROUTE_LOST;s++)if(s!=XZ_EXTRA_ROUTE_READY){
  struct xz_shell_source_model c=sm;assert(xz_shell_source_mark_loopback(&c,s)==-1&&!memcmp(&c,&sm,sizeof c));
 }
 /* READY, music in USB 1: the cable is in USB 2, and only its status bit stays known. */
 struct xz_shell_source_model c=sm;c.device[1].known|=1u<<XZ_SHELL_D_NAME;c.device[1].name="stale";
 assert(xz_shell_source_mark_loopback(&c,XZ_EXTRA_ROUTE_READY)==1);
 assert(c.device[1].status==XZ_SHELL_DEVICE_LOOPBACK&&c.device[1].known==1u<<XZ_SHELL_D_STATUS&&c.device[0].status==XZ_SHELL_DEVICE_READY);
 assert(xz_shell_source_mark_loopback(&c,XZ_EXTRA_ROUTE_READY)==1);              /* idempotent */
 /* The other way round, with a loading or unreadable stick as the music. */
 for(int k=0;k<3;k++){
  static const enum xz_shell_device_status music[]={XZ_SHELL_DEVICE_READY,XZ_SHELL_DEVICE_LOADING,XZ_SHELL_DEVICE_UNREADABLE};
  c=sm;c.device[0].status=XZ_SHELL_DEVICE_NOT_CONNECTED;c.device[1].status=music[k];
  assert(xz_shell_source_mark_loopback(&c,XZ_EXTRA_ROUTE_READY)==0&&c.device[0].status==XZ_SHELL_DEVICE_LOOPBACK);
 }
 /* No music, two sticks, or a status not read: it cannot know. */
 c=sm;c.device[0].status=XZ_SHELL_DEVICE_NOT_CONNECTED;assert(xz_shell_source_mark_loopback(&c,XZ_EXTRA_ROUTE_READY)==-1);
 c=sm;c.device[1].status=XZ_SHELL_DEVICE_READY;assert(xz_shell_source_mark_loopback(&c,XZ_EXTRA_ROUTE_READY)==-1);
 c=sm;c.device[1].known=0;assert(xz_shell_source_mark_loopback(&c,XZ_EXTRA_ROUTE_READY)==-1);
 c=sm;c.device[1].status=(enum xz_shell_device_status)42;assert(xz_shell_source_mark_loopback(&c,XZ_EXTRA_ROUTE_READY)==-1);
 assert(xz_shell_source_mark_loopback(NULL,XZ_EXTRA_ROUTE_READY)==-1);
 puts("PASS cable port inferred only from READY with music in exactly one top port");
}

static void test_navigation(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_source_model sm;struct xz_shell_action out[XZ_SHELL_ACTIONS];
 model(&m,&sm);
 sm.device[1].status=XZ_SHELL_DEVICE_LOOPBACK;m.route_state=XZ_EXTRA_ROUTE_READY;
 /* The encoder skips the cable's port: USB 1 -> LINK -> REKORDBOX and back. */
 open_source(&u,&m);
 assert(u.stack[1].cursor==0);
 assert(turn(&u,&m,1,out)==0&&u.stack[1].cursor==3);
 assert(turn(&u,&m,1,out)==0&&u.stack[1].cursor==4);
 assert(turn(&u,&m,-2,out)==0&&u.stack[1].cursor==1);
 /* Touch on the cable's port does nothing; LINK still opens stock's screen: linked players work over the LAN. */
 assert(tap(&u,&m,xz_shell_source_rect(1),out)==0&&tap(&u,&m,xz_shell_source_rect(1),out)==0&&u.stack[1].cursor==1);
 assert(tap(&u,&m,xz_shell_source_rect(2),out)==0&&u.stack[1].cursor==3);
 assert(tap(&u,&m,xz_shell_source_rect(2),out)==1&&out[0].kind==(enum xz_shell_action_kind)XZ_SHELL_ACT_SET_SOURCE&&
  out[0].id==XZ_SHELL_SRC_LINK&&out[0].value==0&&xz_shell_screen(&u)==XZ_SHELL_SOURCE);
 /* USB 2 pressed while it holds the cable: SOURCE with that port highlighted; a push there does nothing. */
 xz_shell_init(&u);
 assert(press(&u,&m,XZ_SHELL_KEY_SOURCE_USB2,out)==0&&xz_shell_screen(&u)==XZ_SHELL_SOURCE&&u.stack[1].cursor==2&&u.stack[1].focus);
 assert(push(&u,&m,out)==0&&xz_shell_screen(&u)==XZ_SHELL_SOURCE);
 /* The bar takes no input. */
 open_source(&u,&m);
 struct xz_shell_view before=u.stack[1];
 assert(tap(&u,&m,xz_shell_source_rect(XZ_SHELL_SOURCE_BAR),out)==0&&!memcmp(&before,&u.stack[1],sizeof before));
 /* An unreadable stick: not selectable either. */
 sm.device[1].status=XZ_SHELL_DEVICE_UNREADABLE;
 open_source(&u,&m);
 assert(turn(&u,&m,1,out)==0&&u.stack[1].cursor==3);
 assert(tap(&u,&m,xz_shell_source_rect(1),out)==0&&tap(&u,&m,xz_shell_source_rect(1),out)==0&&u.stack[1].cursor==3);
 puts("PASS cable port and unreadable stick are skipped by encoder and touch; LINK still opens");
}

static void test_render(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_source_model sm;
 model(&m,&sm);
 const struct xz_shell_rect link=xz_shell_source_rect(2),rb=xz_shell_source_rect(3),bar=xz_shell_source_rect(XZ_SHELL_SOURCE_BAR);
 /* Geometry: the bar spans the tiles' width, below them and above the deck strip. */
 assert(bar.x==xz_shell_source_rect(0).x&&bar.x+bar.w==rb.x+rb.w);
 for(int i=0;i<XZ_SHELL_SOURCE_TILES;i++){
  struct xz_shell_rect r=xz_shell_source_rect(i);
  assert(r.y>=SI_HEAD_H&&r.y+r.h<bar.y&&r.h>=44);
 }
 assert(bar.y+bar.h<=SI_STRIP_Y&&bar.h>=44);
 /* No cable: no amber in LINK or REKORDBOX, a dim dot in the bar. */
 m.route_state=XZ_EXTRA_ROUTE_NOT_DETECTED;
 open_source(&u,&m);render_checked(&u,&m,buffer_a);
 assert(count_color(frame(buffer_a),link,SH_KEY_AMBER)==0&&count_color(frame(buffer_a),rb,SH_KEY_AMBER)==0);
 assert(count_color(frame(buffer_a),bar,SH_GREEN)==0&&count_color(frame(buffer_a),bar,SH_KEY_AMBER)==0);
 /* Cable in: both stock tiles mark their USB part, the bar says it in green and amber. */
 m.route_state=XZ_EXTRA_ROUTE_READY;
 render_checked(&u,&m,buffer_b);
 assert(count_color(frame(buffer_b),link,SH_KEY_AMBER)>50&&count_color(frame(buffer_b),rb,SH_KEY_AMBER)>50);
 assert(count_color(frame(buffer_b),bar,SH_GREEN)>20&&count_color(frame(buffer_b),bar,SH_KEY_AMBER)>50);
 /* The tiles above are the same: only the stock tiles' marks and the bar changed. */
 assert(!memcmp(frame(buffer_a)+(size_t)xz_shell_source_rect(0).y*STRIDE,frame(buffer_b)+(size_t)xz_shell_source_rect(0).y*STRIDE,
  (size_t)STRIDE*(size_t)(link.y+96+40-xz_shell_source_rect(0).y)*sizeof(uint16_t)));
 /* STARTING and FAILED do not claim the cable: no amber marks in the stock tiles. */
 m.route_state=XZ_EXTRA_ROUTE_STARTING;render_checked(&u,&m,buffer_b);
 assert(count_color(frame(buffer_b),link,SH_KEY_AMBER)==0);
 m.route_state=XZ_EXTRA_ROUTE_FAILED;
 render_checked(&u,&m,buffer_b);
 assert(count_color(frame(buffer_b),link,SH_KEY_AMBER)==0&&count_color(frame(buffer_b),bar,SH_KEY_AMBER)>0);
 /* The runtime's longest sentence stays inside the bar; gaps and margins stay black. */
 memset(m.route_reason,'W',sizeof m.route_reason-1);m.route_reason[sizeof m.route_reason-1]=0;
 sm.device[1].status=XZ_SHELL_DEVICE_LOOPBACK;
 render_checked(&u,&m,buffer_a);
 const uint16_t *f=frame(buffer_a);
 for(int y=SI_HEAD_H;y<SI_STRIP_Y;y++){
  for(int x=0;x<bar.x;x++)assert(at(f,x,y)==rgb565(SH_BLACK));
  for(int x=bar.x+bar.w;x<XZ_SHELL_WIDTH;x++)assert(at(f,x,y)==rgb565(SH_BLACK));
 }
 for(int i=1;i<XZ_SHELL_SOURCE_TILES;i++){
  struct xz_shell_rect l=xz_shell_source_rect(i-1),r=xz_shell_source_rect(i);
  for(int y=SI_HEAD_H;y<bar.y;y++)for(int x=l.x+l.w;x<r.x;x++)assert(at(f,x,y)==rgb565(SH_BLACK));
 }
 /* Rendering never writes the model and is deterministic. */
 struct xz_shell_source_model copy=sm;struct xz_shell_model mcopy=m;
 render_checked(&u,&m,buffer_b);
 assert(!memcmp(&copy,&sm,sizeof sm)&&!memcmp(&mcopy,&m,sizeof m)&&!memcmp(buffer_a,buffer_b,sizeof buffer_a));
 /* Every state with every device status renders inside the guards. */
 for(int s=-1;s<=XZ_EXTRA_ROUTE_LOST+1;s++)for(int d=0;d<=XZ_SHELL_DEVICE_LOOPBACK+1;d++){
  for(int i=0;i<XZ_SHELL_SOURCE_TILES;i++)sm.device[i].status=(enum xz_shell_device_status)((d+i)%(XZ_SHELL_DEVICE_LOOPBACK+2));
  m.route_state=s;open_source(&u,&m);render_checked(&u,&m,buffer_a);
  struct xz_shell_action out[XZ_SHELL_ACTIONS];turn(&u,&m,1,out);render_checked(&u,&m,buffer_a);
 }
 puts("PASS rear USB bar and stock tiles: marks only with the cable in, bounds, determinism");
}

int main(void) {
 fx_track_build(&track,7);
 test_rear_states();
 test_mark_loopback();
 test_navigation();
 test_render();
 puts("PASS source");
 return 0;
}
