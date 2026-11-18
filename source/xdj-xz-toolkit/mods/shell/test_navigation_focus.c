#ifdef NDEBUG
#error Shell acceptance requires active assertions
#endif
#include "browse_fixtures.h"
#include "shell_source.h"
#include "shell_internal.h"
#include <assert.h>
#include <stdio.h>

static struct fx_track track;
static struct bx_library lib;
static uint16_t pixels[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT];
static struct xz_shell_action out[XZ_SHELL_ACTIONS];
static size_t key(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_key k) {
 struct xz_shell_event e={XZ_SHELL_KEY,0,0,0,k,-1};
 return xz_shell_input(u,m,&e,out);
}
static size_t encoder(struct xz_shell *u, const struct xz_shell_model *m, int delta, int push) {
 struct xz_shell_event e={push?XZ_SHELL_ENCODER_PUSH:XZ_SHELL_ENCODER_TURN,0,0,delta,0,-1};
 return xz_shell_input(u,m,&e,out);
}
static void focused_tile(struct xz_shell *u, const struct xz_shell_model *m, int slot) {
 assert(xz_shell_render(u,m,pixels,XZ_SHELL_WIDTH));
 struct xz_shell_rect r=xz_shell_browse_rect(u,m,XZ_SHELL_BR_RAIL,slot);
 /* The focused tile and the list cursor must not compete for the blue highlight. */
 uint16_t blue=(uint16_t)(((SH_BLUE>>8)&0xf800)|((SH_BLUE>>5)&0x7e0)|((SH_BLUE>>3)&31));
 assert(pixels[(r.y+3)*XZ_SHELL_WIDTH+3]==blue);
 r=xz_shell_browse_rect(u,m,XZ_SHELL_BR_ROW,0);
 assert(pixels[(r.y+3)*XZ_SHELL_WIDTH+r.x+3]!=blue);
}
int main(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;
 struct xz_shell_source_model sources={0};
 fx_track_build(&track,7);bx_build(&lib);fx_model_full(&m,&track);
 m.capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_SEARCH|XZ_SHELL_CAP_SOURCE|XZ_SHELL_CAP_LOAD;
 m.browse=&b;m.source=&sources;
 for(int i=0;i<2;i++){
  sources.device[i].known=1u<<XZ_SHELL_D_STATUS;
  sources.device[i].status=XZ_SHELL_DEVICE_READY;
 }
 for(int usb=0;usb<2;usb++){
  bx_tracks(&b,&lib,XZ_SHELL_BROWSE);xz_shell_init(&u);xz_shell_browse_reset();
  assert(key(&u,&m,usb?XZ_SHELL_KEY_SOURCE_USB2:XZ_SHELL_KEY_SOURCE_USB1)==2);
  assert(xz_shell_screen(&u)==XZ_SHELL_BROWSE&&u.depth==3);
  assert(encoder(&u,&m,XZ_SHELL_CAT_ALBUM-XZ_SHELL_CAT_TRACK,0)==0&&u.stack[2].cursor==0);
  focused_tile(&u,&m,XZ_SHELL_CAT_ALBUM);
  assert(encoder(&u,&m,0,1)==1&&out[0].id==XZ_SHELL_BROWSE_CATEGORY&&out[0].value==XZ_SHELL_CAT_ALBUM);
  b.category=XZ_SHELL_CAT_ALBUM;
  assert(encoder(&u,&m,3,0)==0&&u.stack[2].cursor==3);
  assert(key(&u,&m,XZ_SHELL_KEY_BACK)==0&&xz_shell_screen(&u)==XZ_SHELL_BROWSE);
  assert(encoder(&u,&m,XZ_SHELL_CAT_TRACK-XZ_SHELL_CAT_ALBUM,0)==0&&u.stack[2].cursor==3);
  assert(encoder(&u,&m,0,1)==1&&out[0].id==XZ_SHELL_BROWSE_CATEGORY&&out[0].value==XZ_SHELL_CAT_TRACK);
  b.category=XZ_SHELL_CAT_TRACK;
  assert(encoder(&u,&m,2,0)==0&&u.stack[2].cursor==2);
  assert(key(&u,&m,XZ_SHELL_KEY_BACK)==0&&xz_shell_screen(&u)==XZ_SHELL_BROWSE);
  assert(key(&u,&m,XZ_SHELL_KEY_BACK)==0&&xz_shell_screen(&u)==XZ_SHELL_SOURCE);
 }
 /* Selecting the current category enters its rows without resetting the cursor. */
 bx_tracks(&b,&lib,XZ_SHELL_BROWSE);xz_shell_init(&u);xz_shell_browse_reset();
 key(&u,&m,XZ_SHELL_KEY_BROWSE);
 assert(encoder(&u,&m,0,1)==0);
 assert(encoder(&u,&m,4,0)==0&&u.stack[2].cursor==4);
 key(&u,&m,XZ_SHELL_KEY_BACK);
 assert(encoder(&u,&m,0,1)==0&&u.stack[2].cursor==4);
 /* Nested BACK still climbs the list before returning to the category rail. */
 bx_folders(&b,&lib,XZ_SHELL_BROWSE);
 assert(encoder(&u,&m,0,1)==1&&out[0].id==XZ_SHELL_BROWSE_OPEN);
 bx_tracks(&b,&lib,XZ_SHELL_BROWSE);
 b.depth=1;
 assert(key(&u,&m,XZ_SHELL_KEY_BACK)==1&&out[0].id==XZ_SHELL_BROWSE_UP);
 b.depth=0;
 assert(key(&u,&m,XZ_SHELL_KEY_BACK)==0&&xz_shell_screen(&u)==XZ_SHELL_BROWSE);
 /* The encoder reaches SEARCH beyond the eight visible sidebar slots. */
 assert(encoder(&u,&m,100,0)==0);
 focused_tile(&u,&m,7);
 assert(encoder(&u,&m,0,1)==0&&xz_shell_screen(&u)==XZ_SHELL_SEARCH);
 /* Holding BROWSE is idempotent on SEARCH and opens it from PLAY or another screen. */
 assert(key(&u,&m,XZ_SHELL_KEY_BROWSE_HOLD)==0&&xz_shell_screen(&u)==XZ_SHELL_SEARCH);
 key(&u,&m,XZ_SHELL_KEY_BACK_HOLD);
 assert(xz_shell_screen(&u)==XZ_SHELL_PLAY);
 assert(key(&u,&m,XZ_SHELL_KEY_BROWSE_HOLD)==0&&xz_shell_screen(&u)==XZ_SHELL_SEARCH);
 puts("PASS USB sidebar focus, category push, list BACK then source BACK, nested BACK, scrolled SEARCH and BROWSE hold");
 return 0;
}
