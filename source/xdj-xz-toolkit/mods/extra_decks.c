/* SPDX-License-Identifier: GPL-2.0-only */
/* DECK 3 / DECK 4 in the runtime: the players (extra_player.c) and the route connector that owns the
   CH3/CH4 USB transport (audio/usb_transport). The connector polls for the route node about once a
   second, opens it, and reports the route up only once the transport runs; a lost route closes and
   retries. Adapted from the route worker of VJToolsLibrary tools/xz-four-deck-poc/player.c (dae68bec).
   Without the node nothing changes: the route stays down and every interface keeps two decks. */
#define _GNU_SOURCE
#include "extra_decks.h"
#include "extra_player.h"
#include "runtime.h"
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <unistd.h>

#pragma weak xz_extra_resolve_track
#pragma weak xz_extra_native_master_bpm

/* Overridable so the host test can use a node it may stat and poll fast. */
#ifndef USB_MAJOR
#define USB_MAJOR 189
#endif
#ifndef POLL_DOWN_MS
#define POLL_DOWN_MS 1000
#endif
#ifndef POLL_UP_MS
#define POLL_UP_MS 50
#endif

/* glibc 2.13 has clock_gettime only in librt; the transport is built with -Dclock_gettime=this. */
int xz_usb_clock_gettime(clockid_t clock, struct timespec *t) { return (int)syscall(SYS_clock_gettime, clock, t); }
static uint32_t now_ms(void) {
 struct timespec t;
 if (xz_usb_clock_gettime(CLOCK_MONOTONIC, &t)) return 0;
 return (uint32_t)((uint64_t)t.tv_sec * 1000u + (uint32_t)t.tv_nsec / 1000000u);
}

static int left_side, right_side;
static int available, route_up;
static uint32_t link_epoch;                /* nonzero while the transport runs */
static struct xz_xp_players *players;
static struct {
 pthread_rwlock_t lock;                    /* write: the connector replaces or closes usb; read: deck workers */
 xz_usb_transport *usb;
 int closing;                              /* close timed out: the handle is kept only to retry the close */
 char node[256];
 pthread_t thread;
 int quit, started;
} route = {.lock = PTHREAD_RWLOCK_INITIALIZER};

void xz_extra_decks_publish_sides(int left_on_3, int right_on_4) {
 __atomic_store_n(&left_side, !!left_on_3, __ATOMIC_RELEASE);
 __atomic_store_n(&right_side, !!right_on_4, __ATOMIC_RELEASE);
}

struct xz_extra_route xz_extra_decks_route(void) {
 struct xz_extra_route r = {__atomic_load_n(&available, __ATOMIC_ACQUIRE), __atomic_load_n(&route_up, __ATOMIC_ACQUIRE)};
 return r;
}

int xz_extra_decks_snapshot(struct xz_extra_decks *out) {
 if (!out) return 0;
 memset(out, 0, sizeof *out);
 struct xz_xp_players *p = __atomic_load_n(&players, __ATOMIC_ACQUIRE);
 if (!p) return 0;
 xz_xp_read(p, out);
 out->route_up = __atomic_load_n(&route_up, __ATOMIC_ACQUIRE);
 out->left_on_3 = out->route_up && __atomic_load_n(&left_side, __ATOMIC_ACQUIRE);
 out->right_on_4 = out->route_up && __atomic_load_n(&right_side, __ATOMIC_ACQUIRE);
 return out->route_up;
}

int xz_extra_deck_input(const struct xz_extra_deck_event *event) {
 struct xz_xp_players *p = __atomic_load_n(&players, __ATOMIC_ACQUIRE);
 return p ? xz_xp_input(p, event) : 0;
}

/* ---- the sink the players submit into; never waits on the connector ---- */
static xz_usb_deck usb_deck(int deck) { return deck ? XZ_USB_DECK_4 : XZ_USB_DECK_3; }
static uint32_t sink_link(void *ctx) { (void)ctx; return __atomic_load_n(&link_epoch, __ATOMIC_ACQUIRE); }
static xz_usb_result sink_submit(void *ctx, int deck, uint32_t generation, const float *stereo, uint32_t frames, uint64_t after) {
 (void)ctx;
 if (pthread_rwlock_tryrdlock(&route.lock)) return XZ_USB_BUSY;
 xz_usb_result r = route.usb && !route.closing ? xz_usb_transport_submit(route.usb, usb_deck(deck), generation, stereo, frames, after)
                                                : XZ_USB_STOPPED;
 pthread_rwlock_unlock(&route.lock);
 return r;
}
static xz_usb_result sink_flush(void *ctx, int deck, uint32_t *generation) {
 (void)ctx;
 if (pthread_rwlock_tryrdlock(&route.lock)) return XZ_USB_BUSY;
 xz_usb_result r = route.usb && !route.closing ? xz_usb_transport_flush(route.usb, usb_deck(deck), generation) : XZ_USB_STOPPED;
 pthread_rwlock_unlock(&route.lock);
 return r;
}
static xz_usb_result sink_position(void *ctx, xz_usb_position *out) {
 (void)ctx;
 if (pthread_rwlock_tryrdlock(&route.lock)) return XZ_USB_AGAIN;
 xz_usb_result r = route.usb && !route.closing ? xz_usb_transport_position(route.usb, out) : XZ_USB_STOPPED;
 pthread_rwlock_unlock(&route.lock);
 return r;
}

/* ---- connector ---- */
static int usb_node(const char *path) {
 struct stat s;
 return !stat(path, &s) && S_ISCHR(s.st_mode) && major(s.st_rdev) == USB_MAJOR;
}
/* XZ_MODS_EXTRA_USB_NODE, else the default node, else the first /dev/shm/xz-poc-NAME/audio-usb. */
static int find_node(char *out, size_t cap) {
 const char *forced = getenv("XZ_MODS_EXTRA_USB_NODE");
 if (forced && forced[0]) return snprintf(out, cap, "%s", forced) < (int)cap && usb_node(out);
 if (usb_node(XZ_EXTRA_USB_NODE_DEFAULT)) return snprintf(out, cap, "%s", XZ_EXTRA_USB_NODE_DEFAULT) < (int)cap;
 DIR *dir = opendir("/dev/shm");
 if (!dir) return 0;
 int found = 0;
 for (struct dirent *e; !found && (e = readdir(dir));)
  found = !strncmp(e->d_name, "xz-poc-", 7) && snprintf(out, cap, "/dev/shm/%s/audio-usb", e->d_name) < (int)cap && usb_node(out);
 closedir(dir);
 return found;
}

static void say(const char *text) {
 xz_log(text);
 xz_xp_notice(players, text);
}
static void set_link(uint32_t epoch) {
 __atomic_store_n(&link_epoch, epoch, __ATOMIC_RELEASE);
 __atomic_store_n(&route_up, epoch != 0, __ATOMIC_RELEASE);
}
/* 0 when closed. On TIMEOUT the transport keeps its handle and its worker; retry later, never free it. */
static int close_route(void) {
 pthread_rwlock_wrlock(&route.lock);
 xz_usb_result r = xz_usb_transport_close(&route.usb);
 route.closing = r == XZ_USB_TIMEOUT;
 pthread_rwlock_unlock(&route.lock);
 if (r == XZ_USB_TIMEOUT) xz_log("DECK 3/4 route: USB worker did not stop; close retried");
 return route.closing;
}
static void rest(unsigned ms) {
 for (unsigned t = 0; t < ms && !__atomic_load_n(&route.quit, __ATOMIC_ACQUIRE); t += 10) {
  struct timespec d = {0, 10000000L}; nanosleep(&d, NULL);
 }
}
static void *connector(void *unused) {
 (void)unused;
 uint32_t epoch = 0;
 xz_usb_result refused = XZ_USB_OK;
 while (!__atomic_load_n(&route.quit, __ATOMIC_ACQUIRE)) {
  if (route.closing) { close_route(); rest(POLL_DOWN_MS); continue; }
  if (!route.usb) {
   char node[sizeof route.node];
   if (find_node(node, sizeof node)) {
    xz_usb_transport *usb = NULL;
    xz_usb_result r = xz_usb_transport_open(node, &usb);
    if (r == XZ_USB_OK) {
     pthread_rwlock_wrlock(&route.lock);
     route.usb = usb; memcpy(route.node, node, sizeof node);
     pthread_rwlock_unlock(&route.lock);
     refused = XZ_USB_OK;
     continue;
    }
    if ((r == XZ_USB_BUSY || r == XZ_USB_WRONG_DEVICE) && r != refused)
     say(r == XZ_USB_BUSY ? "DECK 3/4 ROUTE IS IN USE" : "DECK 3/4 ROUTE IS NOT THE MIXER");
    refused = r;
   }
   rest(POLL_DOWN_MS);
   continue;
  }
  xz_usb_position p;
  (void)xz_usb_transport_position(route.usb, &p);   /* state is valid on AGAIN too */
  int lost = p.state == XZ_USB_FAULTED || p.state == XZ_USB_CLOSED || !usb_node(route.node);
  if (lost) {
   int was_up = __atomic_load_n(&link_epoch, __ATOMIC_ACQUIRE) != 0;
   set_link(0);
   if (was_up) say("DECK 3/4 ROUTE LOST");
   close_route();
   rest(POLL_DOWN_MS);
   continue;
  }
  if (p.state == XZ_USB_RUNNING && !__atomic_load_n(&link_epoch, __ATOMIC_ACQUIRE)) {
   if (!++epoch) epoch = 1;
   set_link(epoch);
   xz_log("DECK 3/4 route up");
  }
  rest(POLL_UP_MS);
 }
 set_link(0);
 for (int tries = 0; (route.usb || route.closing) && tries < 3; ++tries) if (!close_route()) break;
 return NULL;
}

int xz_extra_decks_start(void) {
 const char *enabled = getenv("XZ_MODS_FOUR_DECK");
 if (route.started || (enabled && !strcmp(enabled, "0"))) return 0;
 struct xz_xp_env env = {{NULL, sink_link, sink_submit, sink_flush, sink_position}, now_ms,
                         xz_extra_resolve_track, xz_extra_native_master_bpm};
 struct xz_xp_players *p = xz_xp_create(&env);
 if (!p) return -1;
 if (xz_xp_start(p)) { xz_xp_destroy(p); return -1; }
 __atomic_store_n(&players, p, __ATOMIC_RELEASE);
 __atomic_store_n(&route.quit, 0, __ATOMIC_RELEASE);
 if (pthread_create(&route.thread, NULL, connector, NULL)) {
  __atomic_store_n(&players, NULL, __ATOMIC_RELEASE);
  xz_xp_destroy(p);
  return -1;
 }
 route.started = 1;
 __atomic_store_n(&available, 1, __ATOMIC_RELEASE);
 return 0;
}

/* The players stay allocated: a draw thread may be inside a snapshot, and the library never unloads. */
void xz_extra_decks_stop(void) {
 if (!route.started) return;
 __atomic_store_n(&available, 0, __ATOMIC_RELEASE);
 __atomic_store_n(&route.quit, 1, __ATOMIC_RELEASE);
 pthread_join(route.thread, NULL);
 xz_xp_stop(players);
 route.started = 0;
}
