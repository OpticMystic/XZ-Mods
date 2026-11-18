#ifdef NDEBUG
#error Shell acceptance requires active assertions
#endif
/* DECK VIEW and the loopback cable on PLAY (shell.h xz_shell_deck_view_quiet / cable_open / cable_close): what
   counts as rest, the picker the cable opens (its line in the header, its time, the touch that goes past it) and
   that none of it changes DECK VIEW. The runtime's side is tests/test_shell_deck_view_cable.c. */
#include "four_deck_fixtures.h"
#include "shell_internal.h"
#include "../ui/deck_view.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct fx_track track;
static uint16_t pixels[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT],plain[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT];

static void test_line_fits(void){
 /* The line takes the grey caption's place: right-aligned in the header, clear of DECK VIEW with room to spare. */
 struct xz_canvas c={pixels,XZ_SHELL_WIDTH,XZ_SHELL_WIDTH,XZ_SHELL_HEIGHT,xz_deck_view_modern.theme};
 const struct xz_dv_rect p=xz_deck_view_modern.picker;
 int title=xz_text_width(c,"DECK VIEW",1,99),line=xz_text_width(c,XZ_DECK_VIEW_CABLE_LINE,1,99);
 assert(line>0&&12+title+24+line+12<=p.w);
 struct xz_canvas classic={pixels,XZ_SHELL_WIDTH,XZ_SHELL_WIDTH,XZ_SHELL_HEIGHT,xz_deck_view_classic.theme};
 assert(12+xz_text_width(classic,"DECK VIEW",1,99)+24+xz_text_width(classic,XZ_DECK_VIEW_CABLE_LINE,1,99)+12<=xz_deck_view_classic.picker.w);
 /* NULL draws the ordinary picker, pixel for pixel. */
 memset(pixels,0,sizeof pixels);memset(plain,0,sizeof plain);
 xz_deck_view_picker_draw((struct xz_canvas){pixels,XZ_SHELL_WIDTH,XZ_SHELL_WIDTH,XZ_SHELL_HEIGHT,0},&xz_deck_view_modern,2,1,2);
 xz_deck_view_picker_draw_line((struct xz_canvas){plain,XZ_SHELL_WIDTH,XZ_SHELL_WIDTH,XZ_SHELL_HEIGHT,0},&xz_deck_view_modern,2,1,2,NULL);
 assert(!memcmp(pixels,plain,sizeof pixels));
 /* The line writes only inside the panel's header band. */
 memset(plain,0,sizeof plain);
 xz_deck_view_picker_draw_line((struct xz_canvas){plain,XZ_SHELL_WIDTH,XZ_SHELL_WIDTH,XZ_SHELL_HEIGHT,0},&xz_deck_view_modern,2,1,2,XZ_DECK_VIEW_CABLE_LINE);
 int changed=0;
 for(int y=0;y<XZ_SHELL_HEIGHT;y++)for(int x=0;x<XZ_SHELL_WIDTH;x++)if(pixels[y*XZ_SHELL_WIDTH+x]!=plain[y*XZ_SHELL_WIDTH+x]){
  assert(x>=p.x+12+title+24&&x<p.x+p.w-12+1&&y>=p.y+4&&y<p.y+30);changed++;
 }
 assert(changed>100);
 puts("PASS cable line: fits both pickers' headers after DECK VIEW, draws only there, NULL is the ordinary picker");
}

static void test_quiet(void){
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_action out[XZ_SHELL_ACTIONS];
 xz_shell_init(&u);fx_model_full(&m,&track);
 assert(xz_shell_deck_view_quiet(&u,&m)&&!xz_shell_deck_view_quiet(NULL,&m)&&!xz_shell_deck_view_quiet(&u,NULL));
 /* An input on PLAY: rest again XZ_DECK_VIEW_CABLE_QUIET_MS later. */
 xz_shell_input(&u,&m,&(struct xz_shell_event){XZ_SHELL_ENCODER_TURN,0,0,1,0,-1},out);
 assert(!xz_shell_deck_view_quiet(&u,&m));
 m.clock_ms+=XZ_DECK_VIEW_CABLE_QUIET_MS-1;assert(!xz_shell_deck_view_quiet(&u,&m));
 m.clock_ms+=1;assert(xz_shell_deck_view_quiet(&u,&m));
 /* A touch held, a panel, a popup, a LOAD menu, another screen, the open picker, the encoder's PLAY focus. */
 struct xz_shell s=u;s.down=1;assert(!xz_shell_deck_view_quiet(&s,&m));
 s=u;s.deck_view.contact=1;assert(!xz_shell_deck_view_quiet(&s,&m));
 s=u;s.panel.kind=XZ_SHELL_PANEL_MENU;assert(!xz_shell_deck_view_quiet(&s,&m));
 s=u;s.popup.kind=XZ_SHELL_POPUP_RECENT;assert(!xz_shell_deck_view_quiet(&s,&m));
 s=u;s.load_choice.mode=1;assert(!xz_shell_deck_view_quiet(&s,&m));
 s=u;s.stack[s.depth++]=(struct xz_shell_view){.screen=XZ_SHELL_BROWSE};assert(!xz_shell_deck_view_quiet(&s,&m));
 s=u;s.deck_view.open=1;s.deck_view.input_at=m.clock_ms-XZ_DECK_VIEW_CABLE_QUIET_MS;assert(!xz_shell_deck_view_quiet(&s,&m));
 s=u;s.deck_view.focus=XZ_SHELL_FOCUS_DECK_VIEW;s.deck_view.input_at=m.clock_ms-XZ_DECK_VIEW_CABLE_QUIET_MS;assert(!xz_shell_deck_view_quiet(&s,&m));
 /* A deck playing is no reason to wait. */
 assert(m.deck[0].playing||m.deck[1].playing);
 puts("PASS cable rest: no input for 1 s, no touch, panel, popup, LOAD menu, other screen, picker or PLAY focus");
}

static void test_cable_picker(void){
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_action out[XZ_SHELL_ACTIONS];
 xz_shell_init(&u);fx_model_four(&m,&track);m.layout_preference=XZ_LAYOUT_MIXER+1;
 struct xz_shell_model before=m;
 xz_shell_deck_view_cable_open(&u,&m,XZ_LAYOUT_MIXER);
 assert(u.deck_view.open&&u.deck_view.cable&&u.deck_view.tile==XZ_LAYOUT_MIXER&&!memcmp(&m,&before,sizeof m));
 assert(xz_shell_render_frame(&u,&m,pixels,XZ_SHELL_WIDTH));
 /* 8 s untouched, against the ordinary picker's 6 s. */
 m.clock_ms+=XZ_DECK_VIEW_PICKER_TIMEOUT_MS+500;assert(xz_shell_render_frame(&u,&m,pixels,XZ_SHELL_WIDTH)&&u.deck_view.open);
 m.clock_ms+=XZ_DECK_VIEW_CABLE_TIMEOUT_MS-XZ_DECK_VIEW_PICKER_TIMEOUT_MS-500;
 assert(xz_shell_render_frame(&u,&m,pixels,XZ_SHELL_WIDTH)&&!u.deck_view.open&&!u.deck_view.cable);
 /* cable_close closes only the cable's picker. */
 xz_shell_deck_view_cable_open(&u,&m,XZ_LAYOUT_MIXER);assert(xz_shell_deck_view_cable_close(&u)&&!u.deck_view.open);
 assert(!xz_shell_deck_view_cable_close(&u));
 u.deck_view.open=1;u.deck_view.cable=0;u.deck_view.input_at=m.clock_ms;assert(!xz_shell_deck_view_cable_close(&u)&&u.deck_view.open);
 /* The DECKS button while it is open closes it, as the ordinary picker does, and nothing goes past. */
 xz_shell_init(&u);xz_shell_deck_view_cable_open(&u,&m,XZ_LAYOUT_MIXER);assert(xz_shell_render_frame(&u,&m,pixels,XZ_SHELL_WIDTH));
 const struct xz_dv_rect b=xz_deck_view_modern.button;
 size_t n=xz_shell_input(&u,&m,&(struct xz_shell_event){XZ_SHELL_TOUCH_DOWN,b.x+b.w/2,10,0,0,-1},out);
 xz_shell_input(&u,&m,&(struct xz_shell_event){XZ_SHELL_TOUCH_UP,b.x+b.w/2,10,0,0,-1},out+n);
 assert(!n&&!u.deck_view.open);
 /* The panel's own empty parts keep the touch, as before. */
 xz_shell_init(&u);xz_shell_deck_view_cable_open(&u,&m,XZ_LAYOUT_MIXER);assert(xz_shell_render_frame(&u,&m,pixels,XZ_SHELL_WIDTH));
 const struct xz_dv_rect p=xz_deck_view_modern.picker;
 n=xz_shell_input(&u,&m,&(struct xz_shell_event){XZ_SHELL_TOUCH_DOWN,p.x+6,p.y+8,0,0,-1},out);
 assert(!n&&u.deck_view.open);
 puts("PASS cable picker: opens on the given tile without touching the model, 8 s, closes only itself, DECKS button closes, panel keeps its touch");
}

int main(void){fx_track_build(&track,7);test_line_fits();test_quiet();test_cable_picker();return 0;}
