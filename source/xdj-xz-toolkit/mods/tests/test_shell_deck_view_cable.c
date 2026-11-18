/* SPDX-License-Identifier: MIT */
/* DECK VIEW follows the loopback cable on MODERN, through the real shell runtime: cable_tick_locked and cable_sides
   as the owner tick runs them (source facts in verify_deck_view_cable.py), with the route, the clock and deck
   select played by this file, and frames rendered by the real shell so the picker's paint, timeout and touch are
   the ones the unit runs. Host only. */
#ifdef NDEBUG
#error DECK VIEW cable acceptance requires active assertions
#endif
#include "../shell_runtime.c"
#include "../shell/four_deck_fixtures.h"
#include <assert.h>

void xz_log(const char *message) { (void)message; }

/* The published route and the deck-select path, as this test plays them. */
static int route_up;
struct xz_extra_route xz_extra_decks_route(void) { return (struct xz_extra_route){1, route_up}; }
static int side_calls[2], side_extra[2] = {1, 1};
int xz_runtime_deck_select_target(int side, int extra) {
 assert(side == 0 || side == 1);
 side_calls[side]++;
 side_extra[side] = extra;
 return 1;
}

static struct fx_track track;
static uint16_t pixels[XZ_SHELL_WIDTH * XZ_SHELL_HEIGHT];
static uint32_t clock_now = 100000;
static struct xz_shell_action out[XZ_SHELL_ACTIONS];

/* One owner tick: the step under the lock, the sides after it. */
static void tick(void) {
 int back = cable_tick_locked(clock_now);
 cable_sides(back);
}
/* One frame as the runtime draws it: the model refreshed from the route and DECK VIEW, then the real render, which
   publishes the overlay a touch must match and closes a picker whose time is up. */
static void frame(void) {
 if (route_up) fx_model_four(&model, &track);
 else fx_model_full(&model, &track);
 model.clock_ms = clock_now;
 model.layout_preference = xz_deck_layout_get() + 1;
 model.wave_order = xz_deck_layout_get() == XZ_LAYOUT_MIXER;
 assert(xz_shell_render_frame(&shell, &model, pixels, XZ_SHELL_WIDTH));
}
/* ms of owner ticks and frames, 50 ms apart. */
static void run(uint32_t ms) {
 for (uint32_t t = 0; t < ms; t += 50) { clock_now += 50; tick(); frame(); }
}
static size_t touch(int x, int y) {
 size_t n = xz_shell_input(&shell, &model, &(struct xz_shell_event){XZ_SHELL_TOUCH_DOWN, x, y, 0, 0, -1}, out);
 xz_shell_input(&shell, &model, &(struct xz_shell_event){XZ_SHELL_TOUCH_UP, x, y, 0, 0, -1}, out + n);
 return n;
}
static int cable_picker(void) { return shell.deck_view.open && shell.deck_view.cable; }
static void reset(int view, int up) {
 xz_shell_init(&shell);
 memset(&cable, 0, sizeof cable);
 xz_deck_layout_set(view);
 route_up = up;
 owns_flag = 1;
 cable_stock_window = 0;
 last_input.key_controls_open = 0;
 __atomic_store_n(&interface_modern, 1, __ATOMIC_RELEASE);
 memset(side_calls, 0, sizeof side_calls);
 frame();
 tick(); /* the first step only takes the route as it is */
 frame();
}

/* Cable in while 2 DECKS shows: 4 DECKS in mixer order, set as the setting itself, and the picker with its line,
   once; it closes by itself after XZ_DECK_VIEW_CABLE_TIMEOUT_MS; nothing more for the same connection. */
static void test_connect_two_decks(void) {
 reset(XZ_LAYOUT_TWO, 0);
 assert(xz_deck_layout_get() == XZ_LAYOUT_TWO && !shell.deck_view.open);
 run(3000);
 assert(xz_deck_layout_get() == XZ_LAYOUT_TWO && !shell.deck_view.open);
 unsigned revision = xz_deck_layout_revision();
 dirty_flag = 0;
 route_up = 1;
 clock_now += 50;
 tick();
 assert(xz_deck_layout_get() == XZ_LAYOUT_MIXER && xz_deck_layout_revision() == revision + 1);
 assert(model.layout_preference == XZ_LAYOUT_MIXER + 1 && model.wave_order == 1 && dirty_flag);
 assert(cable_picker() && shell.deck_view.tile == XZ_LAYOUT_MIXER && shell.deck_view.focus == XZ_SHELL_FOCUS_NONE);
 assert(!side_calls[0] && !side_calls[1]);
 frame();
 /* The picker on screen carries the cable's line: the same frame with the ordinary picker differs only there. */
 static uint16_t ordinary[XZ_SHELL_WIDTH * XZ_SHELL_HEIGHT];
 memcpy(ordinary, pixels, sizeof pixels);
 struct xz_shell plain = shell;
 plain.deck_view.cable = 0;
 assert(xz_shell_render(&plain, &model, ordinary, XZ_SHELL_WIDTH));
 const struct xz_dv_rect p = xz_deck_view_modern.picker;
 int differ = 0, outside = 0;
 for (int y = 0; y < XZ_SHELL_HEIGHT; y++)
  for (int x = 0; x < XZ_SHELL_WIDTH; x++) {
   int d = pixels[y * XZ_SHELL_WIDTH + x] != ordinary[y * XZ_SHELL_WIDTH + x];
   if (d && x >= p.x && x < p.x + p.w && y >= p.y + 4 && y < p.y + 30) differ++;
   else if (d) outside++;
  }
 assert(differ > 100 && !outside);
 /* Stays XZ_DECK_VIEW_CABLE_TIMEOUT_MS, then closes by itself; DECK VIEW stays 4 DECKS. */
 run(XZ_DECK_VIEW_CABLE_TIMEOUT_MS - 200);
 assert(cable_picker());
 run(300);
 assert(!shell.deck_view.open && xz_deck_layout_get() == XZ_LAYOUT_MIXER);
 /* The same connection: nothing again, also after the DJ goes back to 2 DECKS. */
 run(5000);
 assert(!shell.deck_view.open && xz_deck_layout_get() == XZ_LAYOUT_MIXER);
 xz_deck_layout_set(XZ_LAYOUT_TWO);
 run(5000);
 assert(!shell.deck_view.open && xz_deck_layout_get() == XZ_LAYOUT_TWO);
 assert(!side_calls[0] && !side_calls[1]);
 puts("PASS cable in on 2 DECKS: 4 DECKS mixer order and the picker with its line, once; closes after 8 s");
}

/* Already in a four-deck view (either order): nothing changes and nothing opens; the DJ's order is theirs. */
static void test_connect_four_decks(void) {
 for (int view = XZ_LAYOUT_FOUR; view <= XZ_LAYOUT_MIXER; view++) {
  reset(view, 0);
  unsigned revision = xz_deck_layout_revision();
  route_up = 1;
  run(3000);
  assert(xz_deck_layout_get() == view && xz_deck_layout_revision() == revision && !shell.deck_view.open);
 }
 puts("PASS cable in on a four-deck view: no change, no picker");
}

/* It waits for the play screen at rest and on screen; a touch, the encoder, a panel, another screen, the MODS panel or
   a hand-off, the key overlay and a stock window all hold it. A playing deck does not. */
static void test_waits_for_rest(void) {
 reset(XZ_LAYOUT_TWO, 0);
 /* A touch held on a lane (activates the deck, no overlay). */
 struct xz_shell_model view;
 xz_shell_play_projection(&shell, &model, &view);
 struct xz_shell_rect lane = xz_shell_play_rect(&view, XZ_SHELL_R_LANE, 0);
 xz_shell_input(&shell, &model, &(struct xz_shell_event){XZ_SHELL_TOUCH_DOWN, lane.x + 200, lane.y + 40, 0, 0, -1}, out);
 assert(shell.down);
 route_up = 1;
 run(3000);
 assert(xz_deck_layout_get() == XZ_LAYOUT_TWO && !shell.deck_view.open);
 xz_shell_input(&shell, &model, &(struct xz_shell_event){XZ_SHELL_TOUCH_UP, lane.x + 200, lane.y + 40, 0, 0, -1}, out);
 /* Released: it still waits XZ_DECK_VIEW_CABLE_QUIET_MS from that input. */
 clock_now += 50; model.clock_ms = clock_now;
 xz_shell_input(&shell, &model, &(struct xz_shell_event){XZ_SHELL_ENCODER_TURN, 0, 0, 1, 0, -1}, out);
 uint32_t input = clock_now;
 while (clock_now - input < XZ_DECK_VIEW_CABLE_QUIET_MS - 100) {
  clock_now += 50; tick(); frame();
  assert(xz_deck_layout_get() == XZ_LAYOUT_TWO && !shell.deck_view.open);
 }
 run(200);
 assert(xz_deck_layout_get() == XZ_LAYOUT_MIXER && cable_picker());
 /* Each hold in turn: connect, wait, release, then it acts. */
 for (int hold = 0; hold < 7; hold++) {
  reset(XZ_LAYOUT_TWO, 0);
  switch (hold) {
  case 0: shell.panel.kind = XZ_SHELL_PANEL_INFO; break;
  case 1: shell.popup.kind = XZ_SHELL_POPUP_RECENT; break;
  case 2: shell.stack[shell.depth++] = (struct xz_shell_view){.screen = XZ_SHELL_BROWSE}; break;
  case 3: owns_flag = 0; break; /* MODS panel, a game, or a stock screen in front */
  case 4: last_input.key_controls_open = 1; break;
  case 5: cable_stock_window = 1; break;
  case 6: shell.deck_view.open = 1; shell.deck_view.input_at = clock_now; break; /* the DJ's own picker */
  }
  route_up = 1;
  run(hold == 6 ? 4000 : 3000);
  assert(xz_deck_layout_get() == XZ_LAYOUT_TWO && !cable_picker());
  switch (hold) {
  case 0: shell.panel.kind = XZ_SHELL_PANEL_NONE; break;
  case 1: shell.popup.kind = XZ_SHELL_POPUP_NONE; break;
  case 2: shell.depth--; break;
  case 3: owns_flag = 1; break;
  case 4: last_input.key_controls_open = 0; break;
  case 5: cable_stock_window = 0; break;
  case 6: break; /* its own six seconds end it */
  }
  run(hold == 6 ? 4000 : 1200);
  assert(xz_deck_layout_get() == XZ_LAYOUT_MIXER && cable_picker());
 }
 /* Decks playing: it still switches (plugging the cable in is deliberate; decks 1 and 2 keep their lanes). */
 reset(XZ_LAYOUT_TWO, 0);
 route_up = 1;
 clock_now += 50;
 fx_model_four(&model, &track);
 assert(model.deck[0].playing || model.deck[1].playing);
 tick();
 assert(xz_deck_layout_get() == XZ_LAYOUT_MIXER && cable_picker());
 puts("PASS waits for rest: touch, encoder (1 s), panel, popup, other screen, not on screen, key overlay, stock window, "
      "the DJ's picker; a playing deck does not hold it");
}

/* A view the DJ chooses while the connection waits wins: the pending switch is dropped. */
static void test_dj_choice_cancels(void) {
 reset(XZ_LAYOUT_TWO, 0);
 owns_flag = 0;
 route_up = 1;
 run(1000);
 xz_deck_layout_set(XZ_LAYOUT_TWO); /* MODS DECK VIEW: 2 DECKS, chosen again */
 run(1000);
 owns_flag = 1;
 run(3000);
 assert(xz_deck_layout_get() == XZ_LAYOUT_TWO && !shell.deck_view.open);
 puts("PASS a DECK VIEW choice while it waits drops the switch");
}

/* Cable out: after XZ_SHRT_CABLE_DROP_MS down, 2 DECKS and both sides through deck select to DECK 1 / DECK 2, once.
   A shorter drop is the same connection: nothing either way. After a settled drop a cable in is a new connection. */
static void test_pull(void) {
 reset(XZ_LAYOUT_TWO, 0);
 route_up = 1;
 run(200);
 assert(xz_deck_layout_get() == XZ_LAYOUT_MIXER && cable_picker());
 /* A flaky cable: down 1.4 s, back up. The picker and the view stay, nothing opens again. */
 route_up = 0;
 run(1400);
 assert(xz_deck_layout_get() == XZ_LAYOUT_MIXER && cable_picker() && !side_calls[0] && !side_calls[1]);
 route_up = 1;
 run(500);
 assert(xz_deck_layout_get() == XZ_LAYOUT_MIXER && !side_calls[0] && !side_calls[1]);
 /* Pulled: nothing before 1.5 s, then 2 DECKS, the cable's picker closed, both sides asked for their stock deck. */
 unsigned revision = xz_deck_layout_revision();
 route_up = 0;
 clock_now += 50; tick(); frame();
 uint32_t dropped = clock_now;
 while (clock_now - dropped < XZ_SHRT_CABLE_DROP_MS - 50) {
  clock_now += 50; tick(); frame();
  assert(xz_deck_layout_get() == XZ_LAYOUT_MIXER && !side_calls[0] && !side_calls[1]);
 }
 clock_now += 50; tick(); frame();
 assert(xz_deck_layout_get() == XZ_LAYOUT_TWO && xz_deck_layout_revision() == revision + 1);
 assert(!shell.deck_view.open && model.layout_preference == XZ_LAYOUT_TWO + 1);
 assert(side_calls[0] == 1 && side_calls[1] == 1 && side_extra[0] == 0 && side_extra[1] == 0);
 run(5000);
 assert(side_calls[0] == 1 && side_calls[1] == 1 && xz_deck_layout_get() == XZ_LAYOUT_TWO);
 /* Back in after the settled drop: a new connection. */
 route_up = 1;
 run(200);
 assert(xz_deck_layout_get() == XZ_LAYOUT_MIXER && cable_picker());
 /* The DJ's own four-deck view, chosen before any cable event: out means back to 2 DECKS and decks 1 and 2 too. */
 reset(XZ_LAYOUT_FOUR, 1);
 assert(xz_deck_layout_get() == XZ_LAYOUT_FOUR && !shell.deck_view.open);
 route_up = 0;
 run(XZ_SHRT_CABLE_DROP_MS + 100);
 assert(xz_deck_layout_get() == XZ_LAYOUT_TWO && side_calls[0] == 1 && side_calls[1] == 1);
 /* Out on 2 DECKS: the view is left alone (no save), the sides still go back. */
 reset(XZ_LAYOUT_TWO, 1);
 revision = xz_deck_layout_revision();
 route_up = 0;
 run(XZ_SHRT_CABLE_DROP_MS + 100);
 assert(xz_deck_layout_get() == XZ_LAYOUT_TWO && xz_deck_layout_revision() == revision);
 assert(side_calls[0] == 1 && side_calls[1] == 1);
 /* The DJ's own picker is not closed by a pull. */
 reset(XZ_LAYOUT_MIXER, 1);
 frame();
 shell.deck_view.open = 1; shell.deck_view.input_at = clock_now;
 route_up = 0;
 for (int i = 0; i < 32; i++) { clock_now += 50; tick(); }
 assert(shell.deck_view.open && !shell.deck_view.cable && xz_deck_layout_get() == XZ_LAYOUT_TWO);
 puts("PASS cable out: 1.5 s, then 2 DECKS and both sides to DECK 1 / 2 once; a 1.4 s drop changes nothing; "
      "back in after a settled drop is a new connection");
}

/* Interface: CLASSIC never runs it; switching into MODERN with the cable in is no connection. */
static void test_classic_and_first_sight(void) {
 reset(XZ_LAYOUT_TWO, 0);
 __atomic_store_n(&interface_modern, 0, __ATOMIC_RELEASE);
 route_up = 1;
 run(3000);
 route_up = 0;
 run(3000);
 route_up = 1;
 run(3000);
 assert(xz_deck_layout_get() == XZ_LAYOUT_TWO && !shell.deck_view.open && !side_calls[0] && !side_calls[1]);
 __atomic_store_n(&interface_modern, 1, __ATOMIC_RELEASE);
 run(3000);
 assert(xz_deck_layout_get() == XZ_LAYOUT_TWO && !shell.deck_view.open);
 puts("PASS CLASSIC: nothing; into MODERN with the cable already in: nothing");
}

/* The picker the cable opened, by touch, encoder and key: outside goes on to what is under it, a tile chooses, BACK
   closes, the encoder makes it an ordinary picker. */
static void test_picker_input(void) {
 const struct xz_dv_rect p = xz_deck_view_modern.picker;
 /* A tile: 2 DECKS chosen at once, the one DECK VIEW action. */
 reset(XZ_LAYOUT_TWO, 0);
 route_up = 1;
 run(100);
 assert(cable_picker());
 struct xz_dv_rect two = xz_deck_view_tile(&xz_deck_view_modern, XZ_LAYOUT_TWO);
 size_t n = touch(two.x + 20, two.y + 20);
 assert(n == 1 && out[0].kind == XZ_SHELL_ACT_DECK_LAYOUT && out[0].value == XZ_LAYOUT_TWO && !shell.deck_view.open);
 /* Outside, on deck 1's lane: closes and that touch acts (here: the lane makes its deck the active one). */
 reset(XZ_LAYOUT_TWO, 0);
 route_up = 1;
 run(100);
 assert(cable_picker());
 shell.deck = 1;
 struct xz_shell_model view;
 xz_shell_play_projection(&shell, &model, &view);
 struct xz_shell_rect lane = xz_shell_play_rect(&view, XZ_SHELL_R_LANE, 0);
 assert(lane.x + 10 < p.x);
 n = touch(lane.x + 10, lane.y + lane.h / 2);
 assert(!shell.deck_view.open && shell.deck == 0);
 for (size_t i = 0; i < n; i++) assert(out[i].kind != XZ_SHELL_ACT_DECK_LAYOUT);
 /* BACK closes it and does nothing else. */
 reset(XZ_LAYOUT_TWO, 0);
 route_up = 1;
 run(100);
 assert(cable_picker());
 n = xz_shell_input(&shell, &model, &(struct xz_shell_event){XZ_SHELL_KEY, 0, 0, 0, XZ_SHELL_KEY_BACK, -1}, out);
 assert(!n && !shell.deck_view.open && xz_deck_layout_get() == XZ_LAYOUT_MIXER);
 /* The encoder: an ordinary picker from then on (its six seconds from that input). */
 reset(XZ_LAYOUT_TWO, 0);
 route_up = 1;
 run(100);
 xz_shell_input(&shell, &model, &(struct xz_shell_event){XZ_SHELL_ENCODER_TURN, 0, 0, -1, 0, -1}, out);
 assert(shell.deck_view.open && !shell.deck_view.cable && shell.deck_view.tile == XZ_LAYOUT_FOUR);
 run(XZ_DECK_VIEW_PICKER_TIMEOUT_MS + 100);
 assert(!shell.deck_view.open);
 puts("PASS cable picker input: tile chooses, outside closes and acts, BACK closes, the encoder makes it ordinary");
}

/* The route-up notice of 2 DECKS: MODERN takes the latch without the notice (the picker says it); CLASSIC's
   browser over the shell keeps it. */
static void test_ready_notice(void) {
 struct xz_extra_route_status status = {0};
 status.state = XZ_EXTRA_ROUTE_READY;
 xz_deck_layout_set(XZ_LAYOUT_TWO);
 model.deck_count = 4;
 memset(&card_seen, 0, sizeof card_seen);
 notice_text[0] = 0;
 status.ready_revision = 7001;
 __atomic_store_n(&interface_modern, 1, __ATOMIC_RELEASE);
 deck_view_route_locked(1000, &status);
 assert(strcmp(notice_text, XZ_DECK_VIEW_READY_NOTICE) && !xz_deck_view_ready_notice_take(7001));
 status.ready_revision = 7002;
 __atomic_store_n(&interface_modern, 0, __ATOMIC_RELEASE);
 deck_view_route_locked(2000, &status);
 assert(!strcmp(notice_text, XZ_DECK_VIEW_READY_NOTICE));
 __atomic_store_n(&interface_modern, 1, __ATOMIC_RELEASE);
 puts("PASS route-up notice: MODERN takes the latch silently, CLASSIC's browser still says it");
}

int main(void) {
 fx_track_build(&track, 7);
 xz_shell_init(&shell);
 __atomic_store_n(&enabled, 1, __ATOMIC_RELEASE);
 test_connect_two_decks();
 test_connect_four_decks();
 test_waits_for_rest();
 test_dj_choice_cancels();
 test_pull();
 test_classic_and_first_sight();
 test_picker_input();
 test_ready_notice();
 return 0;
}
