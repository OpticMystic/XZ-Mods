// SPDX-License-Identifier: GPL-2.0-only
/* The route connector of extra_decks.c against a fake transport. Linux host only. Built with
   -DUSB_MAJOR=1 so /dev/null passes the node check: the connector only stats the node, and the fake
   transport never opens anything. */
#define _GNU_SOURCE
#ifdef NDEBUG
#error "Extra decks acceptance requires active assertions"
#endif
#include "extra_decks.h"
#include "audio/usb_transport/usb_transport.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void xz_log(const char *message) { (void)message; }
int xz_oc_bundle(const char *source, char *folder, size_t capacity) { (void)source; (void)folder; (void)capacity; return -1; }

struct xz_usb_transport { int unused; };
static struct xz_usb_transport handle;
static struct { xz_usb_result open_result; int opens, closes, close_timeouts, open_handles; xz_usb_state state, open_state; uint32_t generation[2]; } t;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
xz_usb_result xz_usb_transport_open(const char *node, xz_usb_transport **out) {
 assert(!strcmp(node, "/dev/null"));
 pthread_mutex_lock(&lock);
 t.opens++;
 xz_usb_result r = t.open_result;
 if (r == XZ_USB_OK) {
  assert(t.open_handles == 0);
  t.open_handles++; t.state = t.open_state; *out = &handle;
  t.generation[0] = t.generation[1] = XZ_USB_INITIAL_GENERATION;
 }
 pthread_mutex_unlock(&lock);
 return r;
}
xz_usb_result xz_usb_transport_submit(xz_usb_transport *u, xz_usb_deck d, uint32_t g, const float *pcm, uint32_t n, uint64_t after) {
 (void)d; (void)g; (void)pcm; (void)n; (void)after;
 assert(u == &handle && t.open_handles == 1);
 return XZ_USB_OK;
}
xz_usb_result xz_usb_transport_flush(xz_usb_transport *u, xz_usb_deck d, uint32_t *g) {
 assert(u == &handle && t.open_handles == 1);
 *g = ++t.generation[d == XZ_USB_DECK_4];
 return XZ_USB_OK;
}
xz_usb_result xz_usb_transport_position(xz_usb_transport *u, xz_usb_position *out) {
 assert(u == &handle);
 pthread_mutex_lock(&lock);
 memset(out, 0, sizeof *out);
 out->state = t.state;
 out->deck[0].applied_generation = t.generation[0]; out->deck[1].applied_generation = t.generation[1];
 pthread_mutex_unlock(&lock);
 return XZ_USB_OK;
}
xz_usb_result xz_usb_transport_close(xz_usb_transport **u) {
 pthread_mutex_lock(&lock);
 xz_usb_result r = XZ_USB_OK;
 if (t.close_timeouts) { t.close_timeouts--; r = XZ_USB_TIMEOUT; }
 else { t.closes++; t.open_handles--; *u = NULL; }
 pthread_mutex_unlock(&lock);
 return r;
}

static void set(xz_usb_result open_result, xz_usb_state state) {
 pthread_mutex_lock(&lock); t.open_result = open_result; t.state = t.open_state = state; pthread_mutex_unlock(&lock);
}
static int read_int(const int *v) { pthread_mutex_lock(&lock); int n = *v; pthread_mutex_unlock(&lock); return n; }
static void wait_ms(unsigned ms) { struct timespec d = {0, (long)ms * 1000000L}; nanosleep(&d, NULL); }
static int until_route(int up) {
 for (int i = 0; i < 400; ++i) { if (xz_extra_decks_route().route_up == up) return 1; wait_ms(5); }
 return 0;
}
static struct xz_extra_decks view;
static void expect_notice(const char *text, uint32_t seq) {
 xz_extra_decks_snapshot(&view);
 assert(view.notice_seq == seq && !strcmp(view.notice, text));
}
static int tempo(void) {
 struct xz_extra_deck_event e; memset(&e, 0, sizeof e);
 e.kind = XZ_EXTRA_IN_TEMPO; e.tempo_position = 100;
 return xz_extra_deck_input(&e);
}

int main(void) {
 setenv("XZ_MODS_EXTRA_USB_NODE", "/dev/null", 1);
 setenv("XZ_MODS_FOUR_DECK", "0", 1);
 assert(!xz_extra_decks_start());
 assert(!xz_extra_decks_route().available && !xz_extra_decks_snapshot(&view) && !tempo());
 unsetenv("XZ_MODS_FOUR_DECK");

 set(XZ_USB_BUSY, XZ_USB_CLOSED);
 assert(!xz_extra_decks_start());
 wait_ms(200);
 assert(xz_extra_decks_route().available && !xz_extra_decks_route().route_up && read_int(&t.opens) >= 3);
 expect_notice("DECK 3/4 ROUTE IS IN USE", 1);
 assert(!tempo());

 set(XZ_USB_OK, XZ_USB_STARTING);
 for (int i = 0; i < 200 && !read_int(&t.open_handles); ++i) wait_ms(5);
 wait_ms(50);
 assert(read_int(&t.open_handles) == 1 && !xz_extra_decks_route().route_up);   /* STARTING is not up */
 set(XZ_USB_OK, XZ_USB_RUNNING);
 assert(until_route(1) && tempo());
 xz_extra_decks_publish_sides(1, 0);
 assert(xz_extra_decks_snapshot(&view) && view.left_on_3 && !view.right_on_4);

 set(XZ_USB_BUSY, XZ_USB_FAULTED);
 assert(until_route(0));
 wait_ms(20);
 expect_notice("DECK 3/4 ROUTE LOST", 2);
 assert(!view.left_on_3 && !tempo() && read_int(&t.closes) == 1);
 set(XZ_USB_OK, XZ_USB_RUNNING);
 assert(until_route(1) && read_int(&t.opens) >= 5 && tempo());

 pthread_mutex_lock(&lock); t.close_timeouts = 2; pthread_mutex_unlock(&lock);
 int opens = read_int(&t.opens);
 set(XZ_USB_BUSY, XZ_USB_FAULTED);
 assert(until_route(0));
 wait_ms(25);
 /* A close that timed out keeps the handle: no new open until it really closed. */
 assert(read_int(&t.open_handles) == 1 && read_int(&t.opens) == opens);
 set(XZ_USB_OK, XZ_USB_RUNNING);
 assert(until_route(1) && read_int(&t.closes) == 2 && read_int(&t.opens) > opens);

 xz_extra_decks_stop();
 assert(!xz_extra_decks_route().available && !xz_extra_decks_route().route_up && read_int(&t.open_handles) == 0);
 puts("PASS route connector: disabled, busy notice once, STARTING not up, running up, lost notice and close, reconnect, close timeout retained and retried, stop");
 return 0;
}
