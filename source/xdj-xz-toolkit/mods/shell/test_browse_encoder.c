#ifdef NDEBUG
#error Browse encoder acceptance requires active assertions
#endif
/* MODERN's browser from the hardware alone, as the owner reported it (2026-11-16):
   1. BROWSE, then a push of the rotary selector: the highlighted tab is taken and the selection moves into the
      track list; turning moves through tracks; a push or LOAD picks one.
   2. BROWSE held: SEARCH opens while the button is still down; the release does nothing more.
   The keys and the selector go through the pieces the runtime uses, in its order (shell_runtime.c
   xz_shell_runtime_key and hold_tick_locked, ui_runtime.c encoder_push_hook): the key table, the press verdict,
   the contacts with their timed hold, stock's browse key entry for the selector (encoder_push.h) and the
   click-or-hold reading. */
#include "browse_fixtures.h"
#include "shell_internal.h"
#include "../shell_runtime_core.c"
#include <assert.h>
#include <limits.h>
#include <stdio.h>

static struct xz_shell u;
static struct xz_shell_model m;
static struct fx_track track;
static struct bx_library lib;
static struct xz_shell_browse_model b;
static struct xz_shell_action out[XZ_SHELL_ACTIONS];
static uint32_t clock_ms;
static struct xz_shrt_contacts contacts;
static struct xz_shrt_push push_state;
static uint32_t entry[4];            /* stock's browse key entry for the selector: {flags, 0, state, handler} */

/* The runtime refreshes the browse model for the screen it shows; the fixture stands in for it. */
static void follow_screen(void) {
 enum xz_shell_screen s=xz_shell_screen(&u);
 if(s>=XZ_SHELL_BROWSE&&s<=XZ_SHELL_SEARCH&&b.screen!=s){bx_tracks(&b,&lib,s);b.media_generation=17;}
}
static size_t deliver(struct xz_shell_event e) { size_t n=xz_shell_input(&u,&m,&e,out);follow_screen();return n; }
static size_t shell_key(int key,int deck) { return deliver((struct xz_shell_event){.kind=XZ_SHELL_KEY,.key=(enum xz_shell_key)key,.deck=deck}); }
/* hold_tick_locked: a swallowed key held past stock's time delivers its hold, once. */
static size_t tick(void) {
 size_t n=0;const struct xz_shrt_key_map *k;
 while((k=xz_shrt_contact_due(&contacts,clock_ms))!=NULL)n+=shell_key(k->hold,k->deck);
 return n;
}
/* xz_shell_runtime_key: op 0 press, 1 hold (stock's, only for a passed key), 2 release. */
static size_t hw_key(uint16_t code,unsigned channel,unsigned op) {
 const struct xz_shrt_key_map *map=xz_shrt_key_lookup(code,channel);assert(map);
 enum xz_shrt_phase phase=xz_shrt_phase_of(op);
 size_t n=tick();
 if(phase==XZ_SHRT_PRESS){
  int swallow=xz_shrt_key_verdict(&u,&m,map)==XZ_SHRT_SWALLOW;
  xz_shrt_contact_press(&contacts,code,channel,swallow,clock_ms);
  assert(swallow);   /* the shell owns every key these flows use */
  int k=xz_shrt_key_for_phase(map,phase,0);if(k>=0)n+=shell_key(k,map->deck);
  return n;
 }
 int held=0;
 if(xz_shrt_contact_follow(&contacts,code,channel,phase,&held)>0){int k=xz_shrt_key_for_phase(map,phase,held);if(k>=0)n+=shell_key(k,map->deck);}
 return n;
}
/* A key pressed at the clock and released after ms, the owner tick running every 10 ms in between. */
static size_t hw_tap(uint16_t code,unsigned channel,uint32_t ms) {
 size_t n=hw_key(code,channel,0);
 for(uint32_t t=10;t<ms;t+=10){clock_ms+=10;n+=tick();}
 clock_ms+=ms%10?ms%10:10;
 return n+hw_key(code,channel,2);
}
/* The selector: UiKey_KeyPush keeps the entry, BrowseKeyProcessing calls the hook while its state is set,
   the hook reads the phase and writes the state back. */
static size_t hw_pass(void) {
 if(!entry[2])return 0;
 struct xz_push_step s=xz_push_read(entry[0],entry[2]);entry[2]=s.state;
 int k=xz_shrt_push_event(&push_state,(int)s.phase);
 return k<0?0:deliver((struct xz_shell_event){.kind=(enum xz_shell_event_kind)k,.deck=-1});
}
static size_t push_down(void) { entry[0]=1;if(entry[2])return 0;entry[2]=2;return hw_pass(); }
static size_t push_hold(void) { entry[0]=1;if(!entry[2])return 0;entry[2]=3;return hw_pass(); }
static size_t push_up(void) { entry[0]=0;return entry[2]?hw_pass():0; }
static size_t click(void) { size_t n=push_down();return n+push_up(); }
static size_t long_push(void) { size_t n=push_down();n+=push_hold();return n+push_up(); }
static size_t turn(int delta) { entry[2]=0;return deliver((struct xz_shell_event){.kind=XZ_SHELL_ENCODER_TURN,.delta=delta,.deck=-1}); }
static size_t tap(enum xz_shell_browse_region region,int i) {
 struct xz_shell_rect r=xz_shell_browse_rect(&u,&m,region,i);assert(r.w&&r.h);
 size_t n=deliver((struct xz_shell_event){.kind=XZ_SHELL_TOUCH_DOWN,.x=r.x+r.w/2,.y=r.y+r.h/2,.deck=-1});
 struct xz_shell_action keep[XZ_SHELL_ACTIONS];memcpy(keep,out,sizeof keep);
 deliver((struct xz_shell_event){.kind=XZ_SHELL_TOUCH_UP,.x=r.x+r.w/2,.y=r.y+r.h/2,.deck=-1});
 memcpy(out,keep,sizeof keep);return n;
}
static void start(void) {
 fx_model_full(&m,&track);bx_tracks(&b,&lib,XZ_SHELL_BROWSE);b.media_generation=17;m.browse=&b;
 m.capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_SEARCH|XZ_SHELL_CAP_LOAD|XZ_SHELL_CAP_TAG_LIST;
 static struct xz_shell_settings_model settings;m.settings_screen=&settings;
 xz_shell_init(&u);xz_shell_browse_reset();
 memset(&contacts,0,sizeof contacts);memset(&push_state,0,sizeof push_state);memset(entry,0,sizeof entry);clock_ms=1000;
}
static const struct xz_shell_view *view(void) { return &u.stack[u.depth-1]; }
static int is_op(size_t n,enum xz_shell_browse_op op) { return n>=1&&out[0].kind==XZ_SHELL_ACT_BROWSE&&out[0].id==(int)op; }
static void render(void) { static uint16_t p[800*480];assert(xz_shell_render(&u,&m,p,800)); }

static void browse_then_push(void) {
 start();
 assert(hw_tap(0x209,0,120)==0&&xz_shell_screen(&u)==XZ_SHELL_BROWSE);
 render();
 /* The tab (the rail's current category, TRACK) is highlighted; the push takes it and moves into the list. */
 assert(sh_browse_at_root(&u));
 assert(push_down()==0&&sh_browse_at_root(&u));          /* the press alone does nothing, as stock */
 assert(push_up()==0&&!sh_browse_at_root(&u)&&entry[2]==0);
 render();
 /* Turning moves through tracks. */
 assert(turn(3)==0&&view()->cursor==3);
 assert(turn(-1)==0&&view()->cursor==2);
 /* A push on a track offers the decks (MODERN's own choice; LOAD 1/2 load at once), a second push loads. */
 u.deck=1;
 assert(click()==0&&u.load_choice.mode);
 assert(click()==1&&out[0].kind==XZ_SHELL_ACT_LOAD&&out[0].deck==1&&out[0].id==(int)lib.tracks[2].id&&xz_shell_screen(&u)==XZ_SHELL_PLAY);
 /* Again with LOAD 1, the hardware key, after the push into the list. */
 hw_tap(0x209,0,100);assert(xz_shell_screen(&u)==XZ_SHELL_BROWSE);
 if(sh_browse_at_root(&u))click();
 assert(!sh_browse_at_root(&u));
 turn(5);
 size_t n=hw_key(0x430f,1,0);hw_key(0x430f,1,2);
 assert(n==1&&out[0].kind==XZ_SHELL_ACT_LOAD&&out[0].deck==0);
 /* Many clicks in a row, no turn between: each one acts (stock left the entry set before; a second push was
    lost until something turned the selector). */
 start();hw_tap(0x209,0,100);
 assert(sh_browse_at_root(&u));click();assert(!sh_browse_at_root(&u));
 shell_key(XZ_SHELL_KEY_BACK,-1);assert(sh_browse_at_root(&u));      /* BACK: the list back to the rail */
 click();assert(!sh_browse_at_root(&u));
 shell_key(XZ_SHELL_KEY_BACK,-1);click();assert(!sh_browse_at_root(&u)&&entry[2]==0);
 puts("PASS BROWSE then push: the highlighted tab is taken, the list has the selector, turn moves, push or LOAD picks");
}

static void push_on_another_tab(void) {
 start();hw_tap(0x209,0,100);
 /* Turning on the rail highlights another tab; a push opens it and moves into its list. */
 int at=-1;
 for(int i=0;i<8&&at<0;i++){turn(1);struct xz_shell_browse_model probe_b=b;(void)probe_b;
  struct xz_shell probe=u;struct xz_shell_event e={.kind=XZ_SHELL_ENCODER_PUSH,.deck=-1};
  size_t n=xz_shell_input(&probe,&m,&e,out);
  if(n==1&&out[0].kind==XZ_SHELL_ACT_BROWSE&&out[0].id==XZ_SHELL_BROWSE_CATEGORY)at=out[0].value;}
 assert(at>=0);
 size_t n=click();
 assert(is_op(n,XZ_SHELL_BROWSE_CATEGORY)&&out[0].value==at&&!sh_browse_at_root(&u));
 /* A folder level: a push enters the folder, BACK climbs out, a held BACK goes to the top. */
 bx_folders(&b,&lib,XZ_SHELL_BROWSE);b.media_generation=17;
 n=click();
 assert(is_op(n,XZ_SHELL_BROWSE_OPEN));
 b.depth=1;b.known|=1u<<XZ_SHELL_BF_LEVEL;
 n=hw_tap(0x4208,0,120);
 assert(is_op(n,XZ_SHELL_BROWSE_UP));
 b.depth=2;
 n=hw_tap(0x4208,0,700);   /* held: the top at 500 ms, the release does nothing more */
 assert(is_op(n,XZ_SHELL_BROWSE_TOP)&&xz_shell_screen(&u)==XZ_SHELL_BROWSE);
 puts("PASS rail: turn picks another tab, push opens its list; folders: push enters, BACK up, BACK held to the top");
}

static void browse_held_opens_search(void) {
 /* From PLAY: SEARCH at 330 ms, while BROWSE is still down; nothing at 320. */
 start();
 assert(hw_key(0x209,0,0)==0&&xz_shell_screen(&u)==XZ_SHELL_PLAY);
 clock_ms+=320;tick();assert(xz_shell_screen(&u)==XZ_SHELL_PLAY);
 clock_ms+=10;tick();assert(xz_shell_screen(&u)==XZ_SHELL_SEARCH);
 render();
 /* The release does nothing more: no short BROWSE after it. */
 clock_ms+=500;tick();
 assert(hw_key(0x209,0,2)==0&&xz_shell_screen(&u)==XZ_SHELL_SEARCH);
 /* SEARCH is on its keyboard: a push types. */
 const char *before=xz_shell_browse_query();size_t was=strlen(before);
 assert(click()==1&&out[0].kind==XZ_SHELL_ACT_BROWSE&&out[0].id==XZ_SHELL_BROWSE_SEARCH_QUERY&&strlen(xz_shell_browse_query())==was+1);
 /* A short press stays a short press, up to 329 ms. */
 start();assert(hw_tap(0x209,0,320)==0&&xz_shell_screen(&u)==XZ_SHELL_BROWSE);
 /* No owner tick while held (a busy GUI thread): the release finds the time passed and is the hold. */
 start();hw_key(0x209,0,0);clock_ms+=600;
 assert(hw_key(0x209,0,2)==0&&xz_shell_screen(&u)==XZ_SHELL_SEARCH);
 /* From every other screen, over a panel or the deck choice: SEARCH. */
 static const int from[]={XZ_SHELL_KEY_BROWSE,XZ_SHELL_KEY_TAG_LIST,XZ_SHELL_KEY_PLAYLIST,XZ_SHELL_KEY_SHORTCUT,XZ_SHELL_KEY_MENU_HOLD};
 for(size_t i=0;i<sizeof from/sizeof *from;i++){
  start();shell_key(from[i],-1);assert(xz_shell_screen(&u)!=XZ_SHELL_PLAY);
  hw_tap(0x209,0,400);assert(xz_shell_screen(&u)==XZ_SHELL_SEARCH);
 }
 start();hw_tap(0x209,0,100);click();shell_key(XZ_SHELL_KEY_INFO,-1);assert(u.panel.kind==XZ_SHELL_PANEL_INFO);
 hw_tap(0x209,0,400);assert(xz_shell_screen(&u)==XZ_SHELL_SEARCH&&u.panel.kind==XZ_SHELL_PANEL_NONE);
 start();hw_tap(0x209,0,100);click();turn(1);click();assert(u.load_choice.mode);
 hw_tap(0x209,0,400);assert(xz_shell_screen(&u)==XZ_SHELL_SEARCH&&!u.load_choice.mode);
 /* On SEARCH it keeps SEARCH (MODERN's design: idempotent). */
 hw_tap(0x209,0,400);assert(xz_shell_screen(&u)==XZ_SHELL_SEARCH);
 puts("PASS BROWSE held: SEARCH at 330 ms while down, from every screen and over panels; the release adds nothing");
}

static void holds_and_clicks(void) {
 /* A held push in a list toggles the jump mode once and never also enters or offers the decks. */
 start();hw_tap(0x209,0,100);click();
 assert(long_push()==0&&!u.load_choice.mode&&entry[2]==0);
 assert(turn(1)==0);                                   /* a jump step */
 assert(click()==0&&!u.load_choice.mode);              /* a push leaves the jump mode */
 assert(click()==0&&u.load_choice.mode);               /* and the next push offers the decks */
 /* Held MENU: UTILITY at 330 ms; a short MENU on a list: its MENU panel. */
 start();hw_tap(0x209,0,100);
 hw_tap(0x20b,0,100);assert(u.panel.kind==XZ_SHELL_PANEL_MENU);
 hw_tap(0x20b,0,100);assert(u.panel.kind==XZ_SHELL_PANEL_NONE);
 hw_tap(0x20b,0,500);assert(xz_shell_screen(&u)==XZ_SHELL_UTILITY);
 /* BACK held on a list's top level: home to PLAY. */
 start();hw_tap(0x209,0,100);click();
 hw_tap(0x4208,0,700);assert(xz_shell_screen(&u)==XZ_SHELL_PLAY);
 /* Touch is as it was after hardware use: a rail tap opens that tab, a row tap highlights. */
 start();hw_tap(0x209,0,100);click();
 shell_key(XZ_SHELL_KEY_BACK,-1);assert(sh_browse_at_root(&u));
 size_t n=tap(XZ_SHELL_BR_RAIL,0);
 assert(n<=1&&!sh_browse_at_root(&u));
 bx_tracks(&b,&lib,XZ_SHELL_BROWSE);b.media_generation=17;
 tap(XZ_SHELL_BR_ROW,2);assert(xz_shell_browse_touched()==view()->scroll+2);
 puts("PASS held push toggles jump mode only; MENU short panel / held UTILITY; BACK held home; touch unchanged");
}

int main(void) {
 setvbuf(stdout,NULL,_IONBF,0);
 fx_track_build(&track,7);bx_build(&lib);
 browse_then_push();
 push_on_another_tab();
 browse_held_opens_search();
 holds_and_clicks();
 puts("PASS browse encoder");
 return 0;
}
