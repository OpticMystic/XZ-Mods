#ifdef NDEBUG
#error classic LOAD acceptance requires assertions
#endif
#include "classic_load.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct xz_classic_load state;
static unsigned stock_calls, asks, loads, refusals;
static struct xz_extra_deck_event last;

static int fake_stock(uint32_t id) { (void)id; return ++stock_calls; }

/* The hook's mapping from verdict to effect, with the firmware calls faked. */
static void message(uint32_t id, int deck, uint32_t hi, uint32_t lo, uint32_t now) {
 struct xz_cl_message m = { .id = id, .deck = deck, .row = 7, .title_flag = 0, .id_hi = hi, .id_lo = lo };
 struct xz_extra_deck_event load;
 switch(xz_classic_load_message(&state, &m, now, &load)) {
 case XZ_CL_PASS: fake_stock(id); break;
 case XZ_CL_ASK: asks++; break;
 case XZ_CL_DELIVER: loads++; last = load; break;
 case XZ_CL_REFUSE: refusals++; break;
 }
}
#define USB1 0x00010200u   /* node 0, device kind 2, browse kind 1 */
#define USB2 0x00010300u

int main(void) {
 message(XZ_CL_MSG_LOAD_ROW, 0, 0, 0, 100);
 message(XZ_CL_MSG_ROW_TRACK, 0, USB1, 42, 110);
 assert(stock_calls == 2 && !asks && !loads);

 xz_classic_load_arm(&state, 0, 1000);
 message(XZ_CL_MSG_LOAD_ROW, 1, 0, 0, 1010);
 assert(stock_calls == 3 && !asks);
 message(0x40e, 0, 0, 0, 1020);
 assert(stock_calls == 4 && !asks);
 message(XZ_CL_MSG_ROW_TRACK, 0, USB1, 42, 1030);
 assert(stock_calls == 5 && !loads);
 message(XZ_CL_MSG_LOAD_ROW, 0, 0, 0, 1040);
 assert(asks == 1 && stock_calls == 5);
 message(XZ_CL_MSG_ROW_TRACK, 1, USB1, 42, 1050);
 assert(stock_calls == 6 && !loads);
 message(XZ_CL_MSG_ROW_TRACK, 0, USB1, 42, 1060);
 assert(loads == 1 && stock_calls == 6);
 assert(last.deck == 0 && last.kind == XZ_EXTRA_IN_LOAD && last.pressed == 1);
 assert(last.load.source == 1 && last.load.track_id == 42);
 message(XZ_CL_MSG_LOAD_ROW, 0, 0, 0, 1070);
 message(XZ_CL_MSG_ROW_TRACK, 0, USB1, 43, 1080);
 assert(stock_calls == 8 && asks == 1 && loads == 1);

 xz_classic_load_arm(&state, 1, 2000);
 message(XZ_CL_MSG_LOAD_ROW, 1, 0, 0, 2000 + XZ_CLASSIC_LOAD_MS);
 assert(stock_calls == 8 && asks == 1 && refusals == 1);
 message(XZ_CL_MSG_LOAD_ROW, 1, 0, 0, 2000 + XZ_CLASSIC_LOAD_MS + 1);
 assert(stock_calls == 9);

 xz_classic_load_arm(&state, 1, 3000);
 message(XZ_CL_MSG_LOAD_ROW, 1, 0, 0, 3500);
 message(XZ_CL_MSG_ROW_TRACK, 1, USB2, 9, 3500 + XZ_CLASSIC_LOAD_MS);
 assert(asks == 2 && loads == 1 && stock_calls == 10);

 xz_classic_load_arm(&state, 1, 0xfffffe00u);
 message(XZ_CL_MSG_LOAD_ROW, 1, 0, 0, 0x100u);
 message(XZ_CL_MSG_ROW_TRACK, 1, USB2, 9, 0x200u);
 assert(asks == 3 && loads == 2 && last.deck == 1 && last.load.source == 2 && last.load.track_id == 9);

 const uint32_t refused[][2] = {{0x00010100u, 5}, {0x00010400u, 5}, {0x00010201u, 5}, {USB1, 0}, {USB1, UINT32_MAX}};
 for(unsigned i = 0; i < sizeof refused / sizeof refused[0]; i++) {
  xz_classic_load_arm(&state, 0, 5000);
  message(XZ_CL_MSG_LOAD_ROW, 0, 0, 0, 5001);
  message(XZ_CL_MSG_ROW_TRACK, 0, refused[i][0], refused[i][1], 5002);
  assert(refusals == i + 2 && loads == 2);
 }
 unsigned before = stock_calls;
 message(XZ_CL_MSG_LOAD_ROW, 0, 0, 0, 5003);
 assert(stock_calls == before + 1);

 xz_classic_load_arm(&state, 0, 6000);
 xz_classic_load_cancel(&state, 0);
 message(XZ_CL_MSG_LOAD_ROW, 0, 0, 0, 6001);
 assert(stock_calls == before + 2);
 xz_classic_load_arm(&state, 2, 6000); xz_classic_load_arm(&state, -1, 6000);
 message(XZ_CL_MSG_LOAD_ROW, 2, 0, 0, 6001);
 assert(stock_calls == before + 3);
 struct xz_extra_deck_event unused;
 assert(xz_classic_load_message(NULL, NULL, 0, &unused) == XZ_CL_PASS);

 assert(xz_classic_load_source(USB1) == 1 && xz_classic_load_source(USB2) == 2);
 assert(!xz_classic_load_source(0x00010101u) && !xz_classic_load_source(0));
 puts("PASS classic LOAD divert: arm, side match, expiry, wrap, non-USB refusal, stock pass-through");
 return 0;
}
