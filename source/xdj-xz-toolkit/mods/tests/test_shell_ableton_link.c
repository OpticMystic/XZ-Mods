/* SPDX-License-Identifier: MIT */
/* PLAY's ABLETON LINK control through the real shell runtime: the Link runtime's glance becomes the model, a touch
   on the drawn control queues the NETWORK row's own action (XZ_UI_ABLETON_LINK, the setting's other value) for
   ui_runtime.c's apply(), and a full queue or a bad value is refused with a notice. Host only. */
#ifdef NDEBUG
#error ABLETON LINK control acceptance requires active assertions
#endif
#include "../shell_runtime.c"
#include "../shell/fixtures.h"
#include <assert.h>

/* The Link runtime, as this test plays it: the word xz_link_runtime_glance would read. */
void xz_log(const char *message) { (void)message; }
static struct xz_link_glance published;
static unsigned glance_reads;
void xz_link_runtime_glance(struct xz_link_glance *out) { glance_reads++; *out = published; }

static struct fx_track track;
static struct xz_shell_action out[XZ_SHELL_ACTIONS];

static size_t touch(int x, int y) {
 size_t n = xz_shell_input(&shell, &model, &(struct xz_shell_event){XZ_SHELL_TOUCH_DOWN, x, y, 0, 0, -1}, out);
 xz_shell_input(&shell, &model, &(struct xz_shell_event){XZ_SHELL_TOUCH_UP, x, y, 0, 0, -1}, out + n);
 return n;
}
static void publish(int enabled, enum xz_link_run state, unsigned peers) {
 published = (struct xz_link_glance){enabled, state, peers};
 model.ableton = ableton_glance();
}

static void test_mapping(void) {
 struct xz_shell_ableton a = xz_shrt_ableton(NULL);
 assert(!a.known && !a.enabled && a.state == XZ_SHELL_ABLETON_OFF && !a.peers);
 static const struct { enum xz_link_run run; enum xz_shell_ableton_state shown; int peers; } map[] = {
  {XZ_LINK_RUN_OFF, XZ_SHELL_ABLETON_OFF, 0}, {XZ_LINK_RUN_STARTING, XZ_SHELL_ABLETON_STARTING, 0},
  {XZ_LINK_RUN_ENABLED, XZ_SHELL_ABLETON_ON, 1}, {XZ_LINK_RUN_NO_MASTER, XZ_SHELL_ABLETON_ON, 1},
  {XZ_LINK_RUN_NO_HELPER, XZ_SHELL_ABLETON_FAILED, 0}, {XZ_LINK_RUN_FAILED, XZ_SHELL_ABLETON_FAILED, 0}};
 for (unsigned i = 0; i < sizeof map / sizeof *map; i++) {
  struct xz_link_glance g = {map[i].run != XZ_LINK_RUN_OFF, map[i].run, 3};
  a = xz_shrt_ableton(&g);
  assert(a.known && a.enabled == (map[i].run != XZ_LINK_RUN_OFF) && a.state == map[i].shown);
  /* The count is shown only in the session; elsewhere a stale count never reaches the screen. */
  assert(a.peers == (map[i].peers ? 3u : 0u));
 }
 struct xz_link_glance odd = {1, (enum xz_link_run)9, 2};
 a = xz_shrt_ableton(&odd);
 assert(!a.known);
}

static void test_touch_queues_the_network_action(void) {
 fx_track_build(&track, 7);
 xz_shell_init(&shell);
 fx_model_full(&model, &track);
 __atomic_store_n(&enabled, 1, __ATOMIC_RELEASE);
 ui_action_count = 0;
 ui_requests = 0;
 for (int on = 0; on < 2; on++) {
  publish(on, on ? XZ_LINK_RUN_ENABLED : XZ_LINK_RUN_OFF, on ? 2 : 0);
  struct xz_shell_rect r = xz_shell_ableton_rect(&shell, &model);
  /* In the top bar between A. CUE and the BEAT FX strip, one rectangle in every state. */
  assert(r.x == 187 && r.y == 1 && r.w == XZ_SHELL_ABLETON_W && r.h == XZ_SHELL_ABLETON_H && r.w == 58 && r.h == 24);
  /* The middle of the drawing, its bottom-left pixel, and the touch target's top-left and bottom-right corners. */
  const int px[4][2] = {{r.x + r.w / 2, r.y + r.h / 2}, {r.x, r.y + r.h - 1}, {r.x, 0},
                        {r.x + r.w - 1, XZ_SHELL_ABLETON_HIT_H - 1}};
  for (int p = 0; p < 4; p++) {
   size_t n = touch(px[p][0], px[p][1]);
   assert(n == 1 && out[0].kind == XZ_SHELL_ACT_ABLETON_LINK && out[0].deck == -1 && out[0].value == !on);
   ui_action_count = 0;
   ui_requests = 0;
   ableton_action_locked(out[0].value, 1000); /* execute_locked's case for this action */
   assert(ui_action_count == 1 && ui_actions[0].kind == XZ_UI_ABLETON_LINK && ui_actions[0].deck == -1 &&
          ui_actions[0].index == 0 && ui_actions[0].value == (float)!on);
   assert(ui_requests & XZ_SHELL_UI_ACTION);
   assert(ableton_queued_locked());
   /* ui_runtime.c takes it as it takes every queued panel action, and runs apply() on it. */
   int kind, deck, index;
   float value;
   assert(xz_shell_runtime_take_ui_action(&kind, &deck, &index, &value) == 1);
   assert(kind == XZ_UI_ABLETON_LINK && deck == -1 && index == 0 && value == (float)!on);
   assert(!xz_shell_runtime_take_ui_action(&kind, &deck, &index, &value) && !ableton_queued_locked());
  }
  /* Just outside the target: one pixel left and right of it (dead columns: nothing at all), and one pixel under
     the header band (the lane: no Link). */
  assert(touch(r.x - 1, r.y + r.h / 2) == 0 && touch(r.x + r.w, r.y + r.h / 2) == 0);
  size_t n = touch(r.x + r.w / 2, XZ_SHELL_ABLETON_HIT_H);
  for (size_t i = 0; i < n; i++) assert(out[i].kind != XZ_SHELL_ACT_ABLETON_LINK);
 }
 /* STARTING and FAILED: the setting is on, so the touch turns it off, as the NETWORK row would. */
 publish(1, XZ_LINK_RUN_STARTING, 0);
 struct xz_shell_rect r = xz_shell_ableton_rect(&shell, &model);
 assert(touch(r.x + 4, r.y + 4) == 1 && out[0].value == 0);
 publish(1, XZ_LINK_RUN_NO_HELPER, 0);
 r = xz_shell_ableton_rect(&shell, &model);
 assert(touch(r.x + 4, r.y + 4) == 1 && out[0].value == 0);
 /* No Link runtime: no control and no target. */
 model.ableton = xz_shrt_ableton(NULL);
 assert(!xz_shell_ableton_rect(&shell, &model).w);
 size_t n = touch(r.x + 4, r.y + 4);
 for (size_t i = 0; i < n; i++) assert(out[i].kind != XZ_SHELL_ACT_ABLETON_LINK);
}

static void test_refusals(void) {
 uint32_t refused = xz_mods_shell_proof_v1.commands_refused;
 ui_action_count = 8; /* the panel queue is full */
 notice_text[0] = 0;
 ableton_action_locked(1, 2000);
 assert(ui_action_count == 8 && xz_mods_shell_proof_v1.commands_refused == refused + 1);
 assert(!strcmp(notice_text, "ABLETON LINK DID NOT CHANGE: TRY AGAIN") && notice_pending);
 ui_action_count = 0;
 /* not a value of the setting */
 notice_text[0] = 0;
 ableton_action_locked(2, 2000);
 assert(ui_action_count == 0 && xz_mods_shell_proof_v1.commands_refused == refused + 2);
 assert(!strcmp(notice_text, "ABLETON LINK DID NOT CHANGE: TRY AGAIN"));
}

int main(void) {
 test_mapping();
 test_touch_queues_the_network_action();
 test_refusals();
 assert(glance_reads > 0);
 puts("PASS ABLETON LINK control: glance mapping, touch on every corner queues the NETWORK row's action with the "
      "setting's other value, nothing outside the target, no runtime no control, full queue and bad value refused");
 return 0;
}
