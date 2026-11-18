/* The physical source keys (USB 1 0x203, USB 2 0x204, LINK 0x205) in MODERN through the real shell runtime: each
   press and release as ui_runtime.c's onKey hook hands it over (xz_shell_runtime_key), and out what the stock player
   would see (the swallow verdict) and what MODERN shows. The state is the owner's on 2026-11-17: MODERN, four decks
   loaded from the stick in USB 2, USB 1 empty, the SOURCE model built by the runtime's own fill_source with stock's
   LINK and REKORDBOX handlers hooked. No source key opens a stock screen unless the DJ asks for the deck's own LINK
   screen a second time, and a source key pressed while the shell is handed off to stock brings MODERN back. */
#ifdef NDEBUG
#error Source key route tests require assertions
#endif
#include "../shell_runtime.c"
#include "../shell/shell_internal.h"
#include "../shell/browse_fixtures.h"
#include "../shell/four_deck_fixtures.h"
#include <assert.h>

static struct bx_library library;
static struct fx_track fixture_track;
static struct xz_shell_browse_model browse;
int xz_extra_deck_input(const struct xz_extra_deck_event *e){(void)e;return 1;}
int xz_extra_order_matches(const struct xz_extra_order *o,int source,uint64_t generation,uint32_t row,uint32_t id){
 (void)o;(void)source;(void)generation;(void)row;(void)id;return 0;
}
int xz_native_four_mixer_order(void){return 0;}
unsigned xz_native_browse_mode(void){return 0;}
int xz_read_memory(uint32_t address,void *out,size_t length){(void)address;(void)out;(void)length;return -1;}
void xz_native_skin_request_repaint(void){}
uint32_t xz_extra_decks_epoch(void){return 1;}
void xz_extra_order_release(struct xz_extra_order *order){(void)order;}
void xz_log(const char *message){(void)message;}
/* ui_runtime.c holds stock's LINK (0) and REKORDBOX (1) handlers, as on the unit: those tiles open the deck's screen. */
int xz_ui_runtime_stock_source_key(int which,int run){(void)which;return run?0:1;}

static const int theme=XZ_THEME_FLAGSHIP;
enum { USB1=0x203, USB2=0x204, LINK=0x205 };
/* The owner's state: MODERN owns the screen on PLAY, four decks loaded from USB 2, USB 1 empty. */
static void start(void){
 memset(&model,0,sizeof model);memset(&queue,0,sizeof queue);
 __atomic_store_n(&enabled,1,__ATOMIC_RELEASE);__atomic_store_n(&interface_modern,1,__ATOMIC_RELEASE);
 refreshed=1;memset(&handoff,0,sizeof handoff);memset(&contacts,0,sizeof contacts);
 quick_return=menu_passed=0;
 fx_model_four(&model,&fixture_track);
 for(int d=0;d<4;d++){model.deck[d].source=XZ_SHELL_SRC_USB2;model.deck[d].playing=1;}
 bx_tracks(&browse,&library,XZ_SHELL_BROWSE);browse.media_generation=7;browse.source_name="USB2";
 memset(usb,0,sizeof usb);
 usb[0].status=USB_NONE;
 usb[1].status=USB_READY;usb[1].lib=(struct xz_shlib *)&library;usb[1].tracks=BX_TRACKS;usb[1].generation=5;
 media_published[1]=5;browse_source=1;
 model.browse=&browse;model.clock_ms=1000;
 model.capabilities|=XZ_SHELL_CAP_SOURCE|XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_SEARCH|XZ_SHELL_CAP_FILTER|XZ_SHELL_CAP_LOAD;
 fill_source();model.source=&source_model;   /* the runtime's own SOURCE model */
 xz_shell_init(&shell);xz_shell_browse_reset();
 assert(source_model.device[0].status==XZ_SHELL_DEVICE_NOT_CONNECTED&&source_model.device[1].status==XZ_SHELL_DEVICE_READY);
 assert(source_model.device[2].status==XZ_SHELL_DEVICE_STOCK&&source_model.device[3].status==XZ_SHELL_DEVICE_STOCK);
}
/* One physical press and release, as the onKey hook sends them (op 0 then op 2, channel 0). 1 = stock never saw it. */
static int tap(int code){
 int a=xz_shell_runtime_key(1,theme,(uint16_t)code,0,0),b=xz_shell_runtime_key(1,theme,(uint16_t)code,0,2);
 assert(a==b);
 return a;
}
static int stock_saw,wrong_screen,stuck_handed,checks;
static const char *name_of(int code){return code==USB1?"USB 1":code==USB2?"USB 2":"LINK";}
/* A press MODERN must take: swallowed, MODERN on screen afterwards, showing want (with SOURCE's tile pinned). */
static void expect_modern(const char *when,int code,enum xz_shell_screen want,int tile){
 checks++;
 int swallowed=tap(code);
 int screen_ok=xz_shell_screen(&shell)==want&&(tile<0||shell.stack[shell.depth-1].cursor==tile);
 if(!swallowed)stock_saw++;
 if(!screen_ok)wrong_screen++;
 if(handoff.handed)stuck_handed++;
 if(!swallowed||!screen_ok||handoff.handed)
  fprintf(stderr,"WRONG %s: %s %s, MODERN shows %s%s\n",when,name_of(code),swallowed?"taken by MODERN":"reached stock",
   xz_shell_screen_name(xz_shell_screen(&shell)),handoff.handed?", screen handed to stock":"");
}
int main(void){
 (void)&bx_folders;bx_build(&library);fx_track_build(&fixture_track,1);

 /* 1. Each key from MODERN's PLAY screen. */
 start();expect_modern("from PLAY",USB1,XZ_SHELL_SOURCE,XZ_SHELL_SRC_USB1);
 start();expect_modern("from PLAY",USB2,XZ_SHELL_BROWSE,-1);
 start();expect_modern("from PLAY",LINK,XZ_SHELL_SOURCE,XZ_SHELL_SRC_LINK);
 /* Four decks need the loopback cable in a top port: with the route READY, USB 1 is the cable's port. */
 start();model.route_state=XZ_EXTRA_ROUTE_READY;fill_source();
 assert(source_model.device[0].status==XZ_SHELL_DEVICE_LOOPBACK);
 expect_modern("cable in USB 1",USB1,XZ_SHELL_SOURCE,XZ_SHELL_SRC_USB1);
 expect_modern("cable in USB 1",LINK,XZ_SHELL_SOURCE,XZ_SHELL_SRC_LINK);
 /* 2. The owner's order, one after another: USB 1, USB 2, LINK. */
 start();
 expect_modern("in turn",USB1,XZ_SHELL_SOURCE,XZ_SHELL_SRC_USB1);
 expect_modern("in turn",USB2,XZ_SHELL_BROWSE,-1);
 expect_modern("in turn",LINK,XZ_SHELL_SOURCE,XZ_SHELL_SRC_LINK);
 /* 3. LINK pressed again over its tile: the deck's own LINK screen, asked for, with a visible hand-off. */
 checks++;
 if(tap(LINK)||!handoff.handed){fprintf(stderr,"WRONG second LINK: stock's LINK screen did not open\n");wrong_screen++;}
 /* 4. Handed off to stock (its LINK screen, as r76 always did at the first LINK): every source key brings MODERN
    back and does its job there; none reaches the stock screen. */
 expect_modern("over stock's LINK screen",USB1,XZ_SHELL_SOURCE,XZ_SHELL_SRC_USB1);
 start();hand_off_locked(1000,"test",LINK);
 expect_modern("over stock's LINK screen",USB2,XZ_SHELL_BROWSE,-1);
 start();hand_off_locked(1000,"test",LINK);
 expect_modern("over stock's LINK screen",LINK,XZ_SHELL_SOURCE,XZ_SHELL_SRC_LINK);
 /* Any other hand-off (stock's UTILITY from a held MENU, TAG TRACK on PLAY) gives way to a source key the same way. */
 start();hand_off_locked(1000,"menu_hold",0x20b);
 expect_modern("over stock's UTILITY",USB2,XZ_SHELL_BROWSE,-1);
 /* 5. The owner's case on r76 as it most likely ran: LINK first (stock's screen), then USB 1, USB 2, LINK. */
 start();
 int owner_stock=0;
 owner_stock+=!tap(LINK);
 owner_stock+=!tap(USB1);
 owner_stock+=!tap(USB2);
 owner_stock+=!tap(LINK);
 checks++;
 if(owner_stock)fprintf(stderr,"WRONG owner sequence LINK, USB 1, USB 2, LINK: %d of 4 presses reached stock\n",owner_stock);
 stock_saw+=owner_stock;
 /* 6. Not MODERN: CLASSIC, and MODERN while the shell cannot own the screen (MODS open, a game), leave every source
    key to stock, as before. */
 start();__atomic_store_n(&interface_modern,0,__ATOMIC_RELEASE);
 int classic_taken=tap(USB1)+tap(USB2)+tap(LINK);
 start();hand_off_locked(1000,"test",LINK);
 int ineligible_taken=xz_shell_runtime_key(0,theme,USB2,0,0);xz_shell_runtime_key(0,theme,USB2,0,2);
 int ineligible_resumed=!handoff.handed;
 checks+=2;
 if(classic_taken||ineligible_taken||ineligible_resumed){
  fprintf(stderr,"WRONG outside MODERN: CLASSIC took %d, ineligible took %d, ineligible ended the hand-off %d\n",classic_taken,ineligible_taken,ineligible_resumed);
  wrong_screen++;
 }
 int bad=stock_saw||wrong_screen||stuck_handed;
 printf("%s source keys through the runtime: %d checks (USB 1 empty, USB 2 ready, LINK, four decks, MODERN; from PLAY, in "
        "turn, over stock's LINK screen and UTILITY): %d presses reached stock, %d wrong screens, %d left handed to stock\n",
        bad?"FAIL":"PASS",checks,stock_saw,wrong_screen,stuck_handed);
 return bad;
}
