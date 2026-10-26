/* SPDX-License-Identifier: GPL-2.0-only */
/* DECK 3 / DECK 4 in the runtime: the players (extra_player.c) and the route connector that owns the
   CH3/CH4 USB transport (audio/usb_transport). The connector polls for the route node about once a
   second, opens it, and reports the route up only once the transport runs; a lost route closes and
   retries. Adapted from the route worker of VJToolsLibrary tools/xz-four-deck-poc/player.c (dae68bec).
   Without the node nothing changes: the route stays down and every interface keeps two decks. */
#define _GNU_SOURCE
#include "sync_observation.h"
#include "extra_decks.h"
#include "extra_player.h"
#include "runtime.h"
#include "extra_thread_placement.h"
#include "clock_compat.h"
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
#ifdef XZ_JOG_DISPLAY_PROOF
#include "jog_display_proof.h"
#endif

#pragma weak xz_extra_resolve_track_checked
#pragma weak xz_extra_resolve_track
#pragma weak xz_extra_native_master_bpm
#pragma weak xz_extra_native_sync_clock
extern void xz_xp_native_master_selected(struct xz_xp_players *,unsigned) __attribute__((weak));
extern void xz_xp_set_quantize_grid(struct xz_xp_players *,unsigned,int) __attribute__((weak));
extern void xz_xp_set_master_observer(struct xz_xp_players *,xz_master_observer) __attribute__((weak));
extern void xz_jog_display_runtime_publish(const struct xz_extra_decks *decks,uint32_t now_ms) __attribute__((weak));
extern void xz_jog_display_runtime_service_assets(void) __attribute__((weak));
extern void xz_native_led_service_feedback(void) __attribute__((weak));
extern void xz_sync_link_runtime_poll(const struct xz_extra_decks *decks) __attribute__((weak));
extern int xz_sync_link_runtime_master_committed(int deck_index) __attribute__((weak));
extern int xz_sync_link_runtime_master_observe(struct xz_master_observation *) __attribute__((weak));

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
int xz_usb_clock_gettime(clockid_t clock, struct timespec *t) { return xz_clock_gettime_compat(clock,t); }
static uint32_t now_ms(void) {
 struct timespec t;
 if (xz_usb_clock_gettime(CLOCK_MONOTONIC, &t)) return 0;
 return (uint32_t)((uint64_t)t.tv_sec * 1000u + (uint32_t)t.tv_nsec / 1000000u);
}

enum { CONTROL_LEFT=1u,CONTROL_RIGHT=2u,CONTROL_ROUTE=4u,CONTROL_AVAILABLE=8u };
static unsigned control_state;
static void control_update(unsigned mask,unsigned value){
 unsigned before=__atomic_load_n(&control_state,__ATOMIC_ACQUIRE),after;
 do{after=(before&~mask)|(value&mask);}while(!__atomic_compare_exchange_n(&control_state,&before,after,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE));
}
uint32_t xz_extra_decks_control_revision(void){return __atomic_load_n(&control_state,__ATOMIC_ACQUIRE);}
static uint32_t link_epoch;                /* nonzero while the transport runs */
static struct xz_xp_players *players;
static struct {
 pthread_rwlock_t lock;                    /* write: the connector replaces or closes usb; read: deck workers */
 xz_usb_transport *usb;
 int closing;                              /* close timed out: the handle is kept only to retry the close */
 char node[256];
 pthread_t thread;
 int quit, started, published;
} route = {.lock = PTHREAD_RWLOCK_INITIALIZER};

void xz_extra_decks_publish_sides(int left_on_3, int right_on_4) {
 control_update(CONTROL_LEFT|CONTROL_RIGHT,(left_on_3?CONTROL_LEFT:0)|(right_on_4?CONTROL_RIGHT:0));
#ifdef XZ_JOG_DISPLAY_PROOF
 JDP_INC(xz_mods_jog_proof_v1.selections);
 JDP_SET(xz_mods_jog_proof_v1.selection_control,xz_extra_decks_control_revision());
#endif
}

struct xz_extra_route xz_extra_decks_route(void) {
 unsigned c=xz_extra_decks_control_revision();
 struct xz_extra_route r = {!!(c&CONTROL_AVAILABLE),!!(c&CONTROL_ROUTE)};
 return r;
}

#if XZ_SYNC_OBSERVATION
static void syo_software(const struct xz_extra_decks *s){
 for(unsigned i=0;i<2;i++){
  const struct xz_extra_deck *d=&s->deck[i];
  /* v1 has counters only for reasons 0..13. Keep newer reasons in the record,
     without misclassifying them as OFF or changing the 2260-byte ABI. */
  unsigned reason = (unsigned)d->sync.reason;
  if (reason < 14u) xz_syo_inc((i?SYO_C_SOFT4_REASON_0:SYO_C_SOFT3_REASON_0)+reason);
  uint32_t v[]={d->sync.requested,d->sync.locked,d->sync.reason,d->sync.master_deck,d->sync.master_scope,
   d->master,d->playing,d->loaded,d->timeline.flags,d->timeline.route_epoch,d->timeline.load_revision,
   d->timeline.transport_generation,SYO_LO(d->timeline.media_generation),SYO_HI(d->timeline.media_generation),xz_syo_request_seq(i),
   d->sync.phase_error_us,d->sync.correction_ppm,d->tempo_x100,d->bpm_x100,d->timeline.at_ms,s->snapshot_revision,s->control_revision};
  xz_syo_record(i?SYO_R_SOFT4:SYO_R_SOFT3,15,v,sizeof v/sizeof *v,0);
 }
}
#define SYO_SOFTWARE(s) syo_software(s)
#else
#define SYO_SOFTWARE(s) ((void)0)
#endif
static void project_master(struct xz_extra_decks *out,uint32_t now){
 struct xz_master_observation a={.deck=XZ_MASTER_UNKNOWN};
 int valid=xz_sync_link_runtime_master_observe(&a)&&a.revision&&a.operator_epoch&&
  a.deck>=XZ_MASTER_NONE&&a.deck<=XZ_MASTER_DECK4&&
  (uint32_t)(now-a.observed_ms)<=XZ_MASTER_MAX_AGE_MS;
 if(valid&&a.deck>=XZ_MASTER_DECK3){
  const struct xz_extra_deck *d=&out->deck[a.deck-XZ_EXTRA_FIRST_DECK];
  valid=out->route_up&&d->loaded&&a.request_epoch&&a.lease_generation&&
   a.route_epoch&&a.load_revision&&a.transport_generation&&
   a.route_epoch==d->timeline.route_epoch&&a.load_revision==d->timeline.load_revision&&
   a.transport_generation==d->timeline.transport_generation&&
   (out->master_intent.deck!=a.deck||out->master_intent.epoch==a.request_epoch);
 }
 if(!valid)a=(struct xz_master_observation){.deck=XZ_MASTER_UNKNOWN};
 out->master_actual=a;
 for(int i=0;i<XZ_EXTRA_DECKS;i++){
  struct xz_extra_deck *d=&out->deck[i];
  d->master=valid&&a.deck==i+XZ_EXTRA_FIRST_DECK;
  d->master_scope=d->master?(XZ_EXTRA_MASTER_SCOPE_SOFTWARE|XZ_EXTRA_MASTER_SCOPE_NATIVE):0;
  if(valid)d->known|=XZ_EXTRA_KNOWN(XZ_EXTRA_F_MASTER);
  else d->known&=~XZ_EXTRA_KNOWN(XZ_EXTRA_F_MASTER);
 }
}
int xz_extra_decks_snapshot_held(struct xz_extra_decks *out,struct xz_extra_analysis_hold *hold) {
 SYO_INC(SNAPSHOT);
#ifdef XZ_JOG_DISPLAY_PROOF
 JDP_INC(xz_mods_jog_proof_v1.snapshot_calls);
#endif
 if (!out||!hold) return 0;
 memset(out, 0, sizeof *out);
 out->master_actual.deck=XZ_MASTER_UNKNOWN;
 struct xz_xp_players *p = __atomic_load_n(&players, __ATOMIC_ACQUIRE);
 if (!p) {
  SYO_INC(SNAPSHOT_EMPTY);
#ifdef XZ_JOG_DISPLAY_PROOF
  JDP_INC(xz_mods_jog_proof_v1.snapshot_no_players);
#endif
  out->control_revision=xz_extra_decks_control_revision();return 0;
 }
 if(!xz_xp_read_held(p,out,hold))return 0;
 unsigned c=xz_extra_decks_control_revision();out->control_revision=c;
 out->route_up=!!(c&CONTROL_ROUTE);out->left_on_3=out->route_up&&!!(c&CONTROL_LEFT);
 out->right_on_4=out->route_up&&!!(c&CONTROL_RIGHT);
 if(xz_sync_link_runtime_poll)xz_sync_link_runtime_poll(out);
 if(xz_sync_link_runtime_master_observe&&out->master_intent.epoch)project_master(out,now_ms());
 else if(xz_sync_link_runtime_master_committed){
  for(int i=0;i<XZ_EXTRA_DECKS;i++){
   if(out->deck[i].master&&xz_sync_link_runtime_master_committed(i)){
    out->deck[i].master_scope|=XZ_EXTRA_MASTER_SCOPE_NATIVE;
    for(int j=0;j<XZ_EXTRA_DECKS;j++)
     if(out->deck[j].sync.master_deck==i+XZ_EXTRA_FIRST_DECK)out->deck[j].sync.master_scope|=XZ_EXTRA_MASTER_SCOPE_NATIVE;
   }
  }
 }
 if(xz_jog_display_runtime_publish)xz_jog_display_runtime_publish(out,now_ms());
 SYO_SOFTWARE(out);
 return out->route_up;
}
void xz_extra_decks_release_analysis(struct xz_extra_decks *out,struct xz_extra_analysis_hold *hold){
 xz_xp_release_analysis(out,hold);
}
int xz_extra_decks_snapshot(struct xz_extra_decks *out){
 struct xz_extra_analysis_hold hold={0};
 int result=xz_extra_decks_snapshot_held(out,&hold);
 /* poll's synchronous anchor computation consumes the pinned grid above.
    Public ordinary snapshots carry scalar values/descriptors only. */
 xz_extra_decks_release_analysis(out,&hold);return result;
}

enum xz_extra_wave_copy_result xz_extra_decks_copy_wave(int deck,
 const struct xz_extra_wave_key *expected,uint32_t first,struct xz_extra_wave_chunk *out) {
 /* Only the connector calls this, after successful startup publication.
    stop joins it before stopping players. Startup failure has no published p. */
 struct xz_xp_players *p=__atomic_load_n(&players,__ATOMIC_ACQUIRE);
 return p?xz_xp_copy_wave(p,deck,expected,first,out):XZ_EXTRA_WAVE_COPY_ABSENT;
}
uint32_t xz_extra_decks_jog_content_epoch(int deck){return xz_xp_jog_content_epoch(deck);}

int xz_extra_deck_input(const struct xz_extra_deck_event *event) {
 struct xz_xp_players *p = __atomic_load_n(&players, __ATOMIC_ACQUIRE);
 int accepted=p ? xz_xp_input(p, event) : 0;
#if XZ_SYNC_OBSERVATION
 if(event&&(unsigned)event->deck<2&&(event->kind==XZ_EXTRA_IN_SYNC||event->kind==XZ_EXTRA_IN_MASTER)){
  SYO_INC(REQUEST);if(accepted)SYO_INC(REQUEST_ACCEPTED);else SYO_INC(REQUEST_REFUSED);
  uint32_t v[]={event->kind,event->deck,event->pressed,event->shift,event->held,accepted};
  xz_syo_record(event->deck?SYO_R_REQUEST4:SYO_R_REQUEST3,6,v,6,1);
 }
#endif
 return accepted;
}
uint32_t xz_extra_decks_revision(void){
 return xz_xp_revision(__atomic_load_n(&players,__ATOMIC_ACQUIRE));
}
int xz_extra_decks_needs_native_sync(void){
 if(!(xz_extra_decks_control_revision()&CONTROL_AVAILABLE))return 0;
 return xz_xp_needs_native_sync(__atomic_load_n(&players,__ATOMIC_ACQUIRE));
}
int xz_extra_decks_use_native_master(void){
 return xz_xp_use_native_master(__atomic_load_n(&players,__ATOMIC_ACQUIRE));
}
void xz_extra_decks_native_master_selected(unsigned native_deck){
 struct xz_xp_players *p=__atomic_load_n(&players,__ATOMIC_ACQUIRE);
 if(p&&native_deck<2&&xz_xp_native_master_selected)xz_xp_native_master_selected(p,native_deck);
}
void xz_extra_decks_set_performance_settings(const struct xz_performance_settings *settings){
 struct xz_xp_players *p=__atomic_load_n(&players,__ATOMIC_ACQUIRE);
 if(p)xz_xp_set_performance_settings(p,settings);
}
int xz_extra_decks_jump_snapshot(int deck,struct xz_jump_snapshot *out){
 return xz_xp_jump_snapshot(__atomic_load_n(&players,__ATOMIC_ACQUIRE),deck,out);
}
void xz_extra_decks_set_quantize_grid(unsigned denominator,int known){
 struct xz_xp_players *p=__atomic_load_n(&players,__ATOMIC_ACQUIRE);
 if(p&&xz_xp_set_quantize_grid)xz_xp_set_quantize_grid(p,denominator,known);
}

/* ---- the sink the players submit into; never waits on the connector ---- */
static xz_usb_deck usb_deck(int deck) { return deck ? XZ_USB_DECK_4 : XZ_USB_DECK_3; }
uint32_t xz_extra_decks_epoch(void) { return __atomic_load_n(&link_epoch, __ATOMIC_ACQUIRE); }
static uint32_t sink_link(void *ctx) { (void)ctx; return __atomic_load_n(&link_epoch, __ATOMIC_ACQUIRE); }
static xz_usb_result sink_submit(void *ctx, uint32_t epoch, int deck, uint32_t generation, const float *stereo, uint32_t frames, uint64_t after) {
 (void)ctx;
 if (pthread_rwlock_tryrdlock(&route.lock)) return XZ_USB_BUSY;
 xz_usb_result r = epoch && epoch == sink_link(NULL) && route.usb && !route.closing ? xz_usb_transport_submit(route.usb, usb_deck(deck), generation, stereo, frames, after)
                                                : XZ_USB_STOPPED;
 pthread_rwlock_unlock(&route.lock);
 return r;
}
static xz_usb_result sink_flush(void *ctx, uint32_t epoch, int deck, uint32_t *generation) {
 (void)ctx;
 if (pthread_rwlock_tryrdlock(&route.lock)) return XZ_USB_BUSY;
 xz_usb_result r = epoch && epoch == sink_link(NULL) && route.usb && !route.closing ? xz_usb_transport_flush(route.usb, usb_deck(deck), generation) : XZ_USB_STOPPED;
 pthread_rwlock_unlock(&route.lock);
 return r;
}
static xz_usb_result sink_transition(void *ctx, uint32_t epoch, int deck, uint32_t expected_generation,
                                           const float *stereo, uint32_t frames, uint64_t after,
                                           uint32_t *generation) {
 (void)ctx;
 if (pthread_rwlock_tryrdlock(&route.lock)) return XZ_USB_BUSY;
 xz_usb_result r = epoch && epoch == sink_link(NULL) && route.usb && !route.closing
  ? xz_usb_transport_flush_and_submit(route.usb, usb_deck(deck), expected_generation, stereo, frames, after, generation)
  : XZ_USB_STOPPED;
 pthread_rwlock_unlock(&route.lock);
 return r;
}
static xz_usb_result sink_position(void *ctx, uint32_t epoch, xz_usb_position *out) {
 (void)ctx;
 if (pthread_rwlock_tryrdlock(&route.lock)) return XZ_USB_AGAIN;
 xz_usb_result r = epoch && epoch == sink_link(NULL) && route.usb && !route.closing ? xz_usb_transport_position(route.usb, out) : XZ_USB_STOPPED;
 pthread_rwlock_unlock(&route.lock);
 return r;
}

#ifdef XZ_XP_TEST_RENDER_HOOK
const struct xz_xp_sink *xz_extra_decks_test_sink(void) {
 static const struct xz_xp_sink sink = {NULL, sink_link, sink_submit, sink_flush, sink_position, sink_transition};
 return &sink;
}
#endif

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
 pthread_rwlock_wrlock(&route.lock);
 __atomic_store_n(&link_epoch, epoch, __ATOMIC_RELEASE);
 control_update(CONTROL_ROUTE,epoch?CONTROL_ROUTE:0);
 pthread_rwlock_unlock(&route.lock);
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
 xz_extra_thread_place("connector");
 /* Creation can fail. Keep the new player private until it succeeds, and let
    this worker enter only after the public pointer and availability agree. */
 while(!__atomic_load_n(&route.published,__ATOMIC_ACQUIRE)&&!__atomic_load_n(&route.quit,__ATOMIC_ACQUIRE))rest(1);
 if(__atomic_load_n(&route.quit,__ATOMIC_ACQUIRE))return NULL;
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
  if (p.state == XZ_USB_RUNNING) {
   struct xz_extra_decks snap;
   (void)xz_extra_decks_snapshot(&snap);
   if(xz_jog_display_runtime_service_assets)xz_jog_display_runtime_service_assets();
   if(xz_native_led_service_feedback)xz_native_led_service_feedback();
  }
  rest(POLL_UP_MS);
 }
 set_link(0);
 for (int tries = 0; (route.usb || route.closing) && tries < 3; ++tries) if (!close_route()) break;
 return NULL;
}

static void deck_worker_enter(int deck) { xz_extra_thread_place(deck ? "deck4" : "deck3"); }
static void cue_store_notice(const char *message){
 struct xz_xp_players *p=__atomic_load_n(&players,__ATOMIC_ACQUIRE);if(p)xz_xp_notice(p,message);
}

int xz_extra_decks_start(void) {
 const char *enabled = getenv("XZ_MODS_FOUR_DECK");
 if (route.started || (enabled && !strcmp(enabled, "0"))) return 0;
 struct xz_xp_env env = {{NULL, sink_link, sink_submit, sink_flush, sink_position, sink_transition}, now_ms,
                         xz_extra_resolve_track, xz_extra_native_master_bpm, deck_worker_enter, xz_extra_resolve_track_checked,
                         1, 1}; /* VINYL mode and QUANTIZE on at start, as the stock decks */
 struct xz_xp_players *p = xz_xp_create(&env);
 if (!p) return -1;
 (void)xz_cues_worker_start(xz_extra_resolve_track_checked,cue_store_notice,"/media/usb1","/media/usb2");
 xz_xp_set_cue_store(p,xz_cues_restore,xz_cues_enqueue);
 if(xz_extra_native_sync_clock)xz_xp_set_native_sync_clock(p,xz_extra_native_sync_clock);
 if(xz_xp_set_master_observer&&xz_sync_link_runtime_master_observe)
  xz_xp_set_master_observer(p,xz_sync_link_runtime_master_observe);
 if (xz_xp_start(p)) { xz_cues_worker_stop();xz_xp_destroy(p); return -1; }
 __atomic_store_n(&route.quit, 0, __ATOMIC_RELEASE);
 __atomic_store_n(&route.published, 0, __ATOMIC_RELEASE);
 if (pthread_create(&route.thread, NULL, connector, NULL)) {
  xz_cues_worker_stop();
  xz_xp_destroy(p);
  return -1;
 }
 __atomic_store_n(&players, p, __ATOMIC_RELEASE);
 route.started = 1;
 control_update(CONTROL_AVAILABLE,CONTROL_AVAILABLE);
 __atomic_store_n(&route.published, 1, __ATOMIC_RELEASE);
 return 0;
}

/* The players stay allocated: a draw thread may be inside a snapshot, and the library never unloads. */
void xz_extra_decks_stop(void) {
 if (!route.started) return;
 control_update(CONTROL_AVAILABLE,0);
 __atomic_store_n(&route.quit, 1, __ATOMIC_RELEASE);
 pthread_join(route.thread, NULL);
 xz_xp_stop(players);
 xz_cues_worker_stop();
 route.started = 0;
}
