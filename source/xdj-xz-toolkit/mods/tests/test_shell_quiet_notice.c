/* SPDX-License-Identifier: MIT */
/* MODERN's notices through the real shell runtime (owner 2026-11-17: no notices over the waveforms): a notice that
   repeats what the screen shows (ui/notice_policy.h; a new MASTER, the loop's words, a side switching deck, a key
   request taken, the DECK VIEW shortcut) neither shows nor replaces the sentence on screen, and still reaches the
   diary; a refusal or fault shows as before. The drawing is shell/test_notice_place.c. Host only. */
#ifdef NDEBUG
#error Quiet notice acceptance requires active assertions
#endif
#include "../shell_runtime.c"
#include <assert.h>

void xz_log(const char *message) { (void)message; }
void xz_link_runtime_glance(struct xz_link_glance *out) { memset(out, 0, sizeof *out); }
static unsigned diary_calls;
void xz_diary_notice(unsigned component, unsigned code) { (void)component; (void)code; diary_calls++; }
unsigned xz_diary_notice_code(const char *text) { return text && text[0] ? 1u : 0u; }

static void shown_now(const char *expect) {
 publish_notice_locked();
 if (expect) assert(model.notice && !strcmp(model.notice, expect));
 else assert(!model.notice);
}

int main(void) {
 model.clock_ms = 1000;
 __atomic_store_n(&enabled, 1, __ATOMIC_RELEASE);
 notice_text[0] = 0; notice_pending = 0;
 static const char *const quiet[] = {"MASTER: DECK 3", "MASTER: NATIVE DECK", "LOOP IN ADJUST", "LOOP EXIT",
  "LEFT: DECK 3", "LEFT: DECK 1  RIGHT: DECK 4", "DECK VIEW: 4 DECKS (3 1 2 4)", "D1 KEY +100c",
  XZ_DECK_VIEW_SUCCESS_NOTICE, "QUANTIZE ON", "MEMORY POINT SET"};
 /* Nothing shown: a quiet notice raises nothing. */
 for (unsigned i = 0; i < sizeof quiet / sizeof *quiet; i++) {
  unsigned diary = diary_calls;
  notice_locked(0, quiet[i]);
  assert(!notice_pending && !notice_text[0] && diary_calls == diary + 1);
 }
 shown_now(NULL);
 /* A refusal shows; a quiet notice after it leaves it on screen, unchanged and not restamped. */
 notice_locked(0, "DECK 1 DID NOT LOAD THE TRACK");
 assert(notice_pending);
 shown_now("DECK 1 DID NOT LOAD THE TRACK");
 uint32_t at = model.notice_at_ms;
 model.clock_ms = 1500;
 for (unsigned i = 0; i < sizeof quiet / sizeof *quiet; i++) {
  notice_locked(0, quiet[i]);
  xz_shell_runtime_notice(quiet[i]);
 }
 assert(!notice_pending);
 shown_now("DECK 1 DID NOT LOAD THE TRACK");
 assert(model.notice_at_ms == at);
 /* Through the public entry other modules use, a fault still shows. */
 xz_shell_runtime_notice("DECK 3/4 ROUTE LOST");
 shown_now("DECK 3/4 ROUTE LOST");
 assert(model.notice_at_ms == 1500);
 notice_locked(0, NULL); notice_locked(0, "");
 shown_now("DECK 3/4 ROUTE LOST");
 puts("PASS MODERN quiet notices: logged, never shown, never replace a refusal; refusals and faults still show");
 return 0;
}
