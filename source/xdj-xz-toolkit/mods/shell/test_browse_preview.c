#ifdef NDEBUG
#error Shell acceptance requires active assertions
#endif
#include "browse_fixtures.h"
#include "shell_internal.h"
#include <assert.h>
#include <stdio.h>

static struct fx_track track;
static struct bx_library lib;
static uint16_t pixels[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT];
static struct xz_shell_action out[XZ_SHELL_ACTIONS];
static size_t send(struct xz_shell *u,const struct xz_shell_model *m,enum xz_shell_event_kind kind,int x,int y,int delta) {
 struct xz_shell_event e={kind,x,y,delta,0,-1};return xz_shell_input(u,m,&e,out);
}
static void open(struct xz_shell *u,const struct xz_shell_model *m) {
 xz_shell_init(u);xz_shell_browse_reset();
 struct xz_shell_event e={XZ_SHELL_KEY,0,0,0,XZ_SHELL_KEY_BROWSE,-1};
 assert(!xz_shell_input(u,m,&e,out));
 assert(xz_shell_render(u,m,pixels,XZ_SHELL_WIDTH));
}
static void tap_preview(struct xz_shell *u,struct xz_shell_model *m,int shown) {
 struct xz_shell_rect r=xz_shell_browse_rect(u,m,XZ_SHELL_BR_PREVIEW,0);
 assert(r.w==70&&r.h==36);
 int x=r.x+r.w/2,y=r.y+r.h/2;
 assert(send(u,m,XZ_SHELL_TOUCH_DOWN,x,y,0)==1);
 assert(out[0].kind==XZ_SHELL_ACT_BROWSE_PREVIEW&&out[0].value==shown&&out[0].deck==-1);
 /* The host applies the queued action before the next input/frame. */
 m->browse_preview=out[0].value;
 assert(!send(u,m,XZ_SHELL_TOUCH_UP,x,y,0));
 assert(xz_shell_render(u,m,pixels,XZ_SHELL_WIDTH));
}
static void columns_tile(const struct xz_shell *u,const struct xz_shell_model *m) {
 struct xz_shell_rect list=xz_shell_browse_rect(u,m,XZ_SHELL_BR_LIST,0);
 int x=list.x;
 for(int i=0;i<XZ_SHELL_BROWSE_COLUMNS+2;i++){
  struct xz_shell_rect r=xz_shell_browse_rect(u,m,XZ_SHELL_BR_COLUMN,i);
  if(!r.w)break;
  assert(r.x==x);x+=r.w;
 }
 assert(x==list.x+list.w);
}
int main(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;
 fx_track_build(&track,7);bx_build(&lib);fx_model_full(&m,&track);
 m.capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_LOAD;m.browse=&b;
 bx_tracks(&b,&lib,XZ_SHELL_BROWSE);open(&u,&m);
 assert(!m.browse_preview);columns_tile(&u,&m);
 struct xz_shell_rect number=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_COLUMN,0);
 struct xz_shell_rect title=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_COLUMN,1);
 assert(number.w==60&&number.x==64);
 tap_preview(&u,&m,1);columns_tile(&u,&m);
 assert(xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_COLUMN,0).w==112);
 assert(xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_COLUMN,1).x==number.x+112);
 assert(xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_COLUMN,2).w==title.w-112);
 open(&u,&m);assert(m.browse_preview); /* screen/module reset follows the saved host choice */
 tap_preview(&u,&m,0);columns_tile(&u,&m);
 assert(xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_COLUMN,0).x==number.x);
 assert(xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_COLUMN,1).w==title.w);
 /* Both settings leave the rail and track encoder paths identical. PREVIEW remains touch-only. */
 struct xz_shell expected={0};struct xz_shell_action load={0};
 for(int shown=0;shown<=1;shown++){
  m.browse_preview=shown;open(&u,&m);
  assert(!send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,-100));
  assert(!send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,XZ_SHELL_CAT_TRACK));
  assert(!send(&u,&m,XZ_SHELL_ENCODER_PUSH,0,0,0));
  assert(!send(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,2));
  assert(!send(&u,&m,XZ_SHELL_ENCODER_PUSH,0,0,0)&&u.load_choice.mode==2);
  assert(send(&u,&m,XZ_SHELL_ENCODER_PUSH,0,0,0)==1&&out[0].kind==XZ_SHELL_ACT_LOAD);
  if(!shown){expected=u;load=out[0];}
  else {assert(!memcmp(&u,&expected,sizeof u));assert(!memcmp(&load,&out[0],sizeof load));}
 }
 bx_folders(&b,&lib,XZ_SHELL_BROWSE);m.browse_preview=1;open(&u,&m);
 assert(xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_COLUMN,0).w!=112);
 puts("PASS browse preview: default collapsed, one tap each way, reset follows host, columns reflow both ways, encoder unchanged and touch-only tile");
}
