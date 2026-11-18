/* SPDX-License-Identifier: GPL-2.0-only */
/* DECK 3 / DECK 4 in the runtime: the players (extra_player.c) and the route connector that owns the
   CH3/CH4 USB transport (audio/usb_transport). The connector watches for the route node, opens it,
   and reports the route up only once the transport runs; a lost route closes and
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
#include "xz_mutex.h"
#include "xz_wake.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#ifdef XZ_JOG_DISPLAY_PROOF
#include "jog_display_proof.h"
#endif

#pragma weak xz_extra_source_generation
#pragma weak xz_core_use_report
#pragma weak xz_ram_extra_create
#pragma weak xz_ram_extra_destroy
#pragma weak xz_ram_extra_player_service
#pragma weak xz_ram_extra_status
#pragma weak xz_ram_extra_command
#pragma weak xz_xp_set_ram_service
#pragma weak xz_xp_set_source_generation
#pragma weak xz_xp_set_release_answer
#pragma weak xz_xp_release_source
#pragma weak xz_ram_extra_source_gone
extern int xz_usb_release_holder(const char *,unsigned (*)(void *,int,uint32_t),void *) __attribute__((weak));
extern void xz_usb_release_answer(int,int,uint32_t) __attribute__((weak));
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
extern void xz_djlink_runtime_publish(const struct xz_extra_decks *decks) __attribute__((weak));
extern void xz_link_runtime_publish(const struct xz_extra_decks *decks) __attribute__((weak));
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
#ifndef XZ_EXTRA_SHM_DIR
#define XZ_EXTRA_SHM_DIR "/dev/shm"
#endif
#ifndef XZ_EXTRA_PROC_DIR
#define XZ_EXTRA_PROC_DIR "/proc"
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
static struct xz_ram_extra *ram;
static pthread_mutex_t ram_lock = XZ_MUTEX_INITIALIZER;
static uint64_t ram_generation(void *unused,unsigned source){
 (void)unused;return xz_extra_source_generation?xz_extra_source_generation((int)source):0;
}
static uint64_t cue_generation(unsigned source){return ram_generation(NULL,source);}
static uint32_t ram_route(void *unused){
 (void)unused;
 return xz_extra_decks_route().route_up?__atomic_load_n(&link_epoch,__ATOMIC_ACQUIRE):0;
}
void xz_extra_decks_ram_status(unsigned deck,struct xz_ram_status *out){
 pthread_mutex_lock(&ram_lock);
 memset(out,0,sizeof(*out));
 if(ram&&xz_ram_extra_status)xz_ram_extra_status(ram,deck,out);
 pthread_mutex_unlock(&ram_lock);
}
int xz_extra_decks_ram_command(unsigned deck,int command,const struct xz_ram_track_key *expected){
 pthread_mutex_lock(&ram_lock);
 int result=ram&&xz_ram_extra_command?xz_ram_extra_command(ram,deck,(enum xz_ram_command)command,expected):XZ_RAM_REFUSED;
 pthread_mutex_unlock(&ram_lock);return result;
}
static void ram_stop(void){
 pthread_mutex_lock(&ram_lock);struct xz_ram_extra *old=ram;ram=NULL;pthread_mutex_unlock(&ram_lock);
 if(old&&xz_ram_extra_destroy)xz_ram_extra_destroy(old);
}
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
 int same=out->master_actual.deck==a.deck&&out->master_actual.revision==a.revision&&
  out->master_actual.operator_epoch==a.operator_epoch&&out->master_actual.request_epoch==a.request_epoch&&
  out->master_actual.lease_generation==a.lease_generation&&out->master_actual.route_epoch==a.route_epoch&&
  out->master_actual.load_revision==a.load_revision&&out->master_actual.transport_generation==a.transport_generation;
 out->master_actual=a;
 for(int i=0;i<XZ_EXTRA_DECKS;i++){
  struct xz_extra_deck *d=&out->deck[i];
  d->sync.quantum_unknown=d->quantize&&!d->quantize_den;
  if(d->sync.requested&&(!same||!valid||a.deck<0)){
   d->sync.tempo_matched=0;d->sync.locked=0;d->sync.correction_ppm=0;d->sync.master_paused=0;
   if(d->sync.source!=XZ_EXTRA_SYNC_SOURCE_EMERGENCY)d->sync.source=a.deck==XZ_MASTER_NONE?XZ_EXTRA_SYNC_SOURCE_NONE:XZ_EXTRA_SYNC_SOURCE_CLOCK_LOST;
  }
  d->master=valid&&a.deck==i+XZ_EXTRA_FIRST_DECK;
  d->master_scope=d->master?(XZ_EXTRA_MASTER_SCOPE_SOFTWARE|XZ_EXTRA_MASTER_SCOPE_NATIVE):0;
  if(valid)d->known|=XZ_EXTRA_KNOWN(XZ_EXTRA_F_MASTER);
  else d->known&=~XZ_EXTRA_KNOWN(XZ_EXTRA_F_MASTER);
 }
}
extern void xz_key_project_extra(struct xz_extra_decks *) __attribute__((weak));
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
 if(xz_djlink_runtime_publish)xz_djlink_runtime_publish(out);
 if(xz_link_runtime_publish)xz_link_runtime_publish(out);
 if(xz_jog_display_runtime_publish)xz_jog_display_runtime_publish(out,now_ms());
 if(xz_key_project_extra)xz_key_project_extra(out);
 SYO_SOFTWARE(out);
 return out->route_up;
}
void xz_extra_decks_release_analysis(struct xz_extra_decks *out,struct xz_extra_analysis_hold *hold){
 xz_xp_release_analysis(out,hold);
}
int xz_extra_decks_worker_load(struct xz_worker_load out[2]) {
 return xz_xp_worker_load(__atomic_load_n(&players,__ATOMIC_ACQUIRE),out);
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

extern int xz_key_control_request(int,enum xz_key_command_kind,int) __attribute__((weak));
extern uint32_t xz_audio_output_epoch(unsigned) __attribute__((weak));
extern void xz_xp_set_key_owner_reader(struct xz_xp_players *,int (*)(int,struct xz_key_owner *)) __attribute__((weak));
int xz_key_owner_current(int deck,struct xz_key_owner *out){
 if(!out||(unsigned)deck>=4)return 0;
 *out=(struct xz_key_owner){0};
 if(deck<2){out->native_epoch=xz_audio_output_epoch?xz_audio_output_epoch((unsigned)deck):0;return out->native_epoch!=0;}
 struct xz_extra_decks s={0};
 struct xz_xp_players *p=__atomic_load_n(&players,__ATOMIC_ACQUIRE);
 if(!p||!xz_extra_decks_route().route_up)return 0;
 xz_xp_read(p,&s);
 const struct xz_extra_deck *d=&s.deck[deck-2];
 if(!d->loaded||!(d->known&XZ_EXTRA_KNOWN(XZ_EXTRA_F_KEY_STATE)))return 0;
 *out=d->key_state.owner;return out->route_epoch&&out->load_revision;
}
int xz_extra_deck_input(const struct xz_extra_deck_event *event) {
 if(event&&(unsigned)event->deck<2&&event->kind==XZ_EXTRA_IN_SYNC&&event->shift){
  if(!event->pressed||event->held)return 1;
  return xz_key_control_request?xz_key_control_request(event->deck+2,XZ_KEY_SYNC_TOGGLE,0):0;
 }
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
 if (usb_node(XZ_EXTRA_SHM_DIR "/xz-four-deck/usb-audio")) return snprintf(out, cap, "%s", XZ_EXTRA_SHM_DIR "/xz-four-deck/usb-audio") < (int)cap;
 DIR *dir = opendir(XZ_EXTRA_SHM_DIR);
 if (!dir) return 0;
 int found = 0;
 for (struct dirent *e; !found && (e = readdir(dir));)
  found = !strncmp(e->d_name, "xz-poc-", 7) && snprintf(out, cap, XZ_EXTRA_SHM_DIR "/%s/audio-usb", e->d_name) < (int)cap && usb_node(out);
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
/* Wake/request mutex is independent of the transport and both UI mutexes.
   Only the connector writes the three-slot mailbox. Readers pin an immutable
   slot with atomics; the connector skips occupied slots instead of waiting. */
#include "extra_route_status.h"
static pthread_mutex_t route_wake_lock=XZ_MUTEX_INITIALIZER;
/* Wakes the connector (RR 60) from interface threads at lower priority: never a condition variable, whose
   internal lock a preempted signaller would keep, see xz_wake.h. */
static struct xz_wake route_wake;
static void route_signal_locked(void);
static unsigned route_scan; /* 0 idle, 1 requested, 2 attempt done / minimum display */
static uint32_t route_scan_at;
static unsigned route_status_token; /* revision in high bits, slot in low two bits */
static struct { unsigned users;struct xz_extra_route_status status; } route_status_slots[3];
static struct xz_extra_route_status route_last;
static int route_ever_up,route_last_up;
static unsigned route_faults;
static void route_status_publish(struct xz_extra_route_status s,uint32_t now,int up) {
 s.checked_at_ms=now;s.revision=route_last.revision+1u;if(!s.revision)s.revision=1;
 s.ready_revision=route_last.ready_revision;
 if(up&&!route_last_up)s.ready_revision=s.revision;
 route_last_up=up;if(up)route_ever_up=1;
 route_last=s;
 unsigned current=__atomic_load_n(&route_status_token,__ATOMIC_ACQUIRE)&3u;
 for(unsigned i=0;i<3u;i++)if(i!=current) {
  unsigned empty=0;
  if(!__atomic_compare_exchange_n(&route_status_slots[i].users,&empty,UINT_MAX,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))continue;
  route_status_slots[i].status=s;
  __atomic_store_n(&route_status_slots[i].users,0,__ATOMIC_RELEASE);
  __atomic_store_n(&route_status_token,(s.revision<<2)|i,__ATOMIC_RELEASE);
  return;
 }
 /* Readers still own both spare slots. Keep the observation/edge in route_last;
    the next connector publication carries it. Never wait for a renderer. */
}

struct xz_extra_route_status xz_extra_decks_route_status(void) {
 struct xz_extra_route_status s;
 for(;;) {
  unsigned token=__atomic_load_n(&route_status_token,__ATOMIC_ACQUIRE),slot=token&3u;
  unsigned users=__atomic_load_n(&route_status_slots[slot].users,__ATOMIC_RELAXED);
  if(users==UINT_MAX||!__atomic_compare_exchange_n(&route_status_slots[slot].users,&users,users+1u,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))continue;
  int current=token==__atomic_load_n(&route_status_token,__ATOMIC_ACQUIRE);
  if(current)s=route_status_slots[slot].status;
  __atomic_sub_fetch(&route_status_slots[slot].users,1,__ATOMIC_RELEASE);
  if(current)break;
 }

 unsigned control=xz_extra_decks_control_revision();
 if(!(control&CONTROL_AVAILABLE)) {
  struct xz_extra_route_status no=route_message(XZ_EXTRA_ROUTE_NO_ENGINE,XZ_EXTRA_ROUTE_PART_NONE,"The software players are unavailable or have not been started.");
  no.revision=s.revision;no.ready_revision=s.ready_revision;no.checked_at_ms=s.checked_at_ms;s=no;
 } else if(__atomic_load_n(&route_scan,__ATOMIC_ACQUIRE)) {
  s.state=XZ_EXTRA_ROUTE_SCANNING;s.fail_part=XZ_EXTRA_ROUTE_PART_NONE;
  memcpy(s.reason,"Checking the loopback cable and audio route now.",sizeof "Checking the loopback cable and audio route now.");
 } else if(s.state==XZ_EXTRA_ROUTE_READY&&!(control&CONTROL_ROUTE)) {
  s.state=XZ_EXTRA_ROUTE_LOST;s.fail_part=XZ_EXTRA_ROUTE_PART_CABLE;
  memcpy(s.reason,"The audio route was disconnected; check the loopback cable.",sizeof "The audio route was disconnected; check the loopback cable.");
 } else if(!s.revision) s=route_message(XZ_EXTRA_ROUTE_STARTING,XZ_EXTRA_ROUTE_PART_NONE,"The software players are starting; the audio route has not been checked yet.");
 return s;
}
int xz_extra_decks_route_up_since(uint32_t revision) {
 uint32_t ready=xz_extra_decks_route_status().ready_revision;
 return ready&&(int32_t)(ready-revision)>0;
}
void xz_extra_decks_rescan(void) {
 if(!(xz_extra_decks_control_revision()&CONTROL_AVAILABLE))return;
 pthread_mutex_lock(&route_wake_lock);
 if(!__atomic_load_n(&route_scan,__ATOMIC_ACQUIRE)) {
  route_scan_at=now_ms();__atomic_store_n(&route_scan,1,__ATOMIC_RELEASE);
  route_signal_locked();
 }
 pthread_mutex_unlock(&route_wake_lock);
}
static void route_scan_finish(uint32_t now,unsigned attempted) {
 pthread_mutex_lock(&route_wake_lock);
 if(attempted&&__atomic_load_n(&route_scan,__ATOMIC_ACQUIRE))
  __atomic_store_n(&route_scan,(uint32_t)(now-route_scan_at)>=600u?0:2,__ATOMIC_RELEASE);
 pthread_mutex_unlock(&route_wake_lock);
}
static void route_check_status(int node,xz_usb_result refused,unsigned attempted) {
 int up=!!__atomic_load_n(&link_epoch,__ATOMIC_ACQUIRE);
 struct route_bridge b={0};
 if(!up)b=route_bridge_read(XZ_EXTRA_SHM_DIR "/xz-four-deck",XZ_EXTRA_PROC_DIR);
 uint32_t now=now_ms();
 route_status_publish(route_observed(&b,node,up,route_ever_up,route_faults,refused),now,up);
 route_scan_finish(now,attempted);
}
/* RETRY. A running four-deck helper takes the marker as "check and bind again now"; a helper that has stopped is
   started again with the loader's own launch script (detach + run.sh, staged by the orchestrator for this rbp).
   Connector thread only. The new helper repeats every check of a first start, and a second one refuses itself. */
#ifndef XZ_EXTRA_AUTO_DIR
#define XZ_EXTRA_AUTO_DIR XZ_EXTRA_SHM_DIR "/xz-auto-4bb9db00a2ab"
#endif
static int route_root_file(const char *path) {
 struct stat st;
 return !lstat(path,&st)&&S_ISREG(st.st_mode)&&!st.st_uid&&!(st.st_mode&022);
}
static void route_bridge_retry(void) {
 int fd=open(XZ_EXTRA_SHM_DIR "/xz-four-deck/retry",O_WRONLY|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
 if(fd>=0)close(fd);
 if(route_bridge_read(XZ_EXTRA_SHM_DIR "/xz-four-deck",XZ_EXTRA_PROC_DIR).valid)return;
 if(!access(XZ_EXTRA_SHM_DIR "/xz-four-deck/stop",F_OK))return;
 static const char detach[]=XZ_EXTRA_AUTO_DIR "/detach",script[]=XZ_EXTRA_AUTO_DIR "/run.sh";
 if(!route_root_file(detach)||!route_root_file(script))return;
 char *const argv[]={(char *)detach,(char *)script,NULL};
 char *const envp[]={(char *)"PATH=/sbin:/usr/sbin:/bin:/usr/bin",NULL};
 /* detach does not close descriptors and the helper lives for the whole power-on: give it none of the
    player's (a track or export.pdb on a stick would stay open and USB STOP could not unmount it). */
 long open_max=sysconf(_SC_OPEN_MAX);int last=open_max>3&&open_max<65536?(int)open_max:1024;
 pid_t child=vfork();
 if(!child){for(int fd=3;fd<last;fd++)close(fd);execve(detach,argv,envp);_exit(127);}
 int status=0;
 if(child>0)while(waitpid(child,&status,0)<0&&errno==EINTR){}
 xz_log(child>0&&WIFEXITED(status)&&!WEXITSTATUS(status)?"DECK 3/4 route: RETRY started the four-deck helper again":
        "DECK 3/4 route: RETRY could not start the four-deck helper");
}
static void rest(unsigned ms) {
 pthread_mutex_lock(&route_wake_lock);
 unsigned scanning=__atomic_load_n(&route_scan,__ATOMIC_ACQUIRE);
 if(scanning==2) {
  uint32_t elapsed=(uint32_t)(now_ms()-route_scan_at);
  if(elapsed<600u&&ms>600u-elapsed)ms=600u-elapsed;
 }
 if(!__atomic_load_n(&route.quit,__ATOMIC_ACQUIRE)&&scanning!=1)
  xz_wake_wait_locked(&route_wake,&route_wake_lock,ms);
 pthread_mutex_unlock(&route_wake_lock);
}
#include "extra_route_events.h"
int xz_extra_decks_usb_capture(int action,uint32_t ms,uint32_t out[3]) {
 static uint8_t *buffer;static uint32_t wanted;   /* allocated on first use, kept: the USB worker may hold it */
 enum { MOST_FRAMES=4u*XZ_USB_RATE };
 out[0]=out[1]=out[2]=0;
 if(pthread_rwlock_tryrdlock(&route.lock))return -EAGAIN;
 int result=-ENODEV;
 if(route.usb&&!route.closing){
  int done=0;uint32_t filled=xz_usb_transport_capture_filled(route.usb,&done);
  if(action==1){
   if(ms<50||ms>4000)result=-EINVAL;
   else{
    if(!buffer)buffer=malloc((size_t)MOST_FRAMES*XZ_USB_CAPTURE_BYTES);
    wanted=(uint32_t)((uint64_t)ms*XZ_USB_RATE/1000u);
    xz_usb_result r=buffer?xz_usb_transport_capture(route.usb,buffer,wanted):XZ_USB_IO;
    result=r==XZ_USB_OK?0:r==XZ_USB_BUSY?-EBUSY:-EIO;filled=0;done=0;
   }
  }else if(action==2)result=buffer&&wanted?0:-ENOENT;
  else if(action==3){
   if(!buffer||!wanted||!done||!filled)result=-EAGAIN;
   else{
    int fd=open(XZ_EXTRA_SHM_DIR "/xz-usb-capture.bin",O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC,0600);
    uint32_t header[4]={0x43555a58u,1u,filled,XZ_USB_CAPTURE_BYTES};   /* 'XZUC' */
    size_t bytes=(size_t)filled*XZ_USB_CAPTURE_BYTES;
    result=fd>=0&&write(fd,header,sizeof header)==(ssize_t)sizeof header&&write(fd,buffer,bytes)==(ssize_t)bytes?0:-EIO;
    if(fd>=0)close(fd);
   }
  }else result=-EINVAL;
  out[0]=filled;out[1]=wanted;out[2]=(uint32_t)done;
 }
 pthread_rwlock_unlock(&route.lock);
 return result;
}
/* The players call this around a track load (extra_player.c load()). */
extern void xz_ram_track_load_begin(void) __attribute__((weak));
void xz_extra_thread_load_priority(int begin,int saved[2]) {
 /* A DECK 3/4 track is loading: background stem copies leave the USB alone for a moment (ram/budget.h). */
 if(begin&&xz_ram_track_load_begin)xz_ram_track_load_begin();
#if defined(__linux__) && defined(__arm__) && !defined(XZ_PLACEMENT_CONNECTOR_TEST)
 if(begin){
  struct sched_param now={0};
  saved[0]=sched_getscheduler(0);saved[1]=sched_getparam(0,&now)?0:now.sched_priority;
  if(saved[0]==SCHED_RR||saved[0]==SCHED_FIFO)sched_setscheduler(0,SCHED_OTHER,&(struct sched_param){0});
 }else if((saved[0]==SCHED_RR||saved[0]==SCHED_FIFO)&&saved[1]>0){
  struct sched_param back={.sched_priority=saved[1]};
  sched_setscheduler(0,saved[0],&back);
 }
#else
 (void)begin;(void)saved;
#endif
}
/* SYNC trail. One log line whenever SYNC on DECK 3 or 4 changes state, so a session nobody watched can be read
   afterwards: why a deck was not beat-locked, and what broke a lock. Connector thread, about 20 looks a second;
   at most 40 lines a minute. */
static void sync_trail(const struct xz_extra_decks *s,const uint32_t beats[XZ_EXTRA_DECKS]) {
 static const char *const why[]={"OFF","WAIT_MASTER","WAIT_ANCHOR","NO_BEAT_GRID","MANUAL","DISCONTINUITY","LOOP_UNALIGNED",
  "OUT_OF_RANGE","ACQUIRING","LOCKED","STOCK_PHASE_UNAVAILABLE","MASTER_CLOCK","STOCK_MASTER_UNSUPPORTED","NO_SOURCE_BPM","TEMPO_ONLY"};
 static struct { int valid,requested,reason,master,quantize;unsigned den;uint32_t load,beats;uint64_t underrun; } seen[XZ_EXTRA_DECKS];
 static uint32_t window_at,lines;
 uint32_t now=now_ms();
 if((uint32_t)(now-window_at)>=60000u){window_at=now;lines=0;}
 for(int i=0;i<XZ_EXTRA_DECKS;i++){
  const struct xz_extra_deck *d=&s->deck[i];
  int reason=d->sync.requested?(int)d->sync.reason:0;
  if(seen[i].valid&&seen[i].requested==d->sync.requested&&seen[i].reason==reason&&seen[i].master==d->sync.master_deck&&
     seen[i].quantize==d->quantize&&seen[i].den==d->quantize_den&&seen[i].load==d->timeline.load_revision&&seen[i].beats==beats[i])continue;
  int was_off=seen[i].valid&&!seen[i].requested&&!d->sync.requested&&seen[i].load==d->timeline.load_revision&&seen[i].beats==beats[i];
  seen[i].valid=1;seen[i].requested=d->sync.requested;seen[i].reason=reason;seen[i].master=d->sync.master_deck;
  seen[i].quantize=d->quantize;seen[i].den=d->quantize_den;seen[i].load=d->timeline.load_revision;seen[i].beats=beats[i];
  if(was_off||!d->loaded)continue;      /* SYNC off and nothing new about the track: nothing to say */
  if(lines>=40u){if(lines==40u){lines++;xz_log("SYNC trail: more changes this minute are not listed");}continue;}
  lines++;
  char line[220];
  snprintf(line,sizeof line,"DECK %d SYNC %s %s master=%d phase=%+.1fms trim=%+dppm tempo=%+.2f%% bpm=%.2f grid=%u beats quantize=%s 1/%u load=%u",
   i+XZ_EXTRA_FIRST_DECK+1,d->sync.requested?"on":"off",reason>=0&&reason<(int)(sizeof why/sizeof *why)?why[reason]:"?",
   d->sync.master_deck<0?0:d->sync.master_deck+1,d->sync.phase_error_us/1000.,(int)d->sync.correction_ppm,d->tempo_x100/100.,
   d->bpm_x100/100.,(unsigned)beats[i],d->quantize?"on":"off",d->quantize_den,(unsigned)d->timeline.load_revision);
  xz_log(line);
 }
}
/* Private diagnostic. The transport counts, per deck, the frames of each USB packet the deck did not fill.
   Within one output generation of a playing deck that is audio the mixer did not get. */
#include "late_trail.h"
#include "core_use.h"
extern struct xz_late_trail xz_extra_late_trail[XZ_EXTRA_DECKS];
extern struct xz_late_trail xz_usb_late_trail;
extern struct xz_turn_trail xz_extra_turn_trail[XZ_EXTRA_DECKS];
extern struct xz_underrun_note xz_usb_underrun_note[2];
#pragma weak xz_extra_late_trail
#pragma weak xz_usb_late_trail
#pragma weak xz_extra_turn_trail
#pragma weak xz_usb_underrun_note
/* The deck worker's last 36 turns, oldest first: blocks submitted, the queue's lead in blocks (x unknown),
   flags in hex (late_trail.h); a turn that submitted nothing below a full lead adds the deck state (s), how far
   the render got (r), the transport's answer (u), the rate in hundredths (v) and the state bits (b).
   And the transport's note of its latest unfilled packet group for this deck. */
static void gap_turns(int deck,const struct xz_turn_trail *trail,const struct xz_underrun_note *note){
 char line[720];int at;
 if(note){
  struct xz_underrun_note copy=*note;
  snprintf(line,sizeof line,"DECK %d GAP TRANSPORT: %u frames missing in the last unfilled group, %u blocks queued, %u.%u ms since a completion, "
           "longest pause between completions %u.%u ms, unfilled groups so far %u",deck,(unsigned)copy.missing_frames,(unsigned)copy.queued_blocks,
           (unsigned)(copy.since_completion_us/1000u),(unsigned)(copy.since_completion_us%1000u/100u),
           (unsigned)(copy.pause_us/1000u),(unsigned)(copy.pause_us%1000u/100u),(unsigned)copy.count);
  xz_log(line);
 }
 if(!trail)return;
 uint32_t count=__atomic_load_n(&trail->count,__ATOMIC_ACQUIRE),held=count<36u?count:36u;
 at=snprintf(line,sizeof line,"DECK %d GAP TURNS:",deck);
 for(uint32_t k=0;k<held&&at>0&&at<(int)sizeof line-40;k++){
  struct xz_turn turn=trail->turn[(count-held+k)%XZ_TURN_SLOTS];
  unsigned blocks=(turn.frames+255u)/256u,lead=turn.lead==0xffffu?99u:turn.lead/256u;
  at+=snprintf(line+at,sizeof line-(size_t)at," %u%c%02x",blocks>9u?9u:blocks,lead==99u?'x':(char)('0'+(lead>9u?9u:lead)),(unsigned)turn.flags);
  if(!blocks&&lead<8u&&at>0&&at<(int)sizeof line-32)
   at+=snprintf(line+at,sizeof line-(size_t)at,"s%ur%uu%dv%db%02x",(unsigned)turn.state,(unsigned)turn.render,(int)turn.submit,(int)turn.rate_x100,(unsigned)turn.bits);
 }
 xz_log(line);
}
/* The worst late turn a loop kept in the last 400 ms, as one log line under the dropout line. */
static void gap_cause(int deck,const char *who,const struct xz_late_trail *trail){
 struct xz_late_turn turn[XZ_LATE_SLOTS];char line[224];
 unsigned count=trail?xz_late_recent(trail,400000u,turn):0,worst=0;uint32_t most=0;
 if(!count){snprintf(line,sizeof line,"DECK %d GAP CAUSE: %s kept no late turn in the last 400 ms",deck,who);xz_log(line);return;}
 for(unsigned k=0;k<count;k++){
  uint32_t over=turn[k].waited_us>turn[k].asked_us?turn[k].waited_us-turn[k].asked_us:0;
  if(turn[k].worked_us>over)over=turn[k].worked_us;
  if(over>=most){most=over;worst=k;}
 }
 const struct xz_late_turn *t=&turn[worst];
 snprintf(line,sizeof line,"DECK %d GAP CAUSE: %s waited %u.%u ms (asked %u.%u), worked %u.%u ms, kernel %u.%u ms, faults %u minor %u major, "
          "switches %u yielded %u preempted, late turns %u",deck,who,
          (unsigned)(t->waited_us/1000u),(unsigned)(t->waited_us%1000u/100u),(unsigned)(t->asked_us/1000u),(unsigned)(t->asked_us%1000u/100u),
          (unsigned)(t->worked_us/1000u),(unsigned)(t->worked_us%1000u/100u),(unsigned)(t->system_us/1000u),(unsigned)(t->system_us%1000u/100u),
          (unsigned)t->minor_faults,(unsigned)t->major_faults,(unsigned)t->yielded,(unsigned)t->preempted,count);
 xz_log(line);
}
/* The deck's playhead is at the end of its track (turn bit 32): its queue runs out because the track is over. */
static int deck_at_end(int i){
 if(!xz_extra_turn_trail)return 0;
 const struct xz_turn_trail *t=&xz_extra_turn_trail[i];
 uint32_t count=__atomic_load_n(&t->count,__ATOMIC_ACQUIRE);
 return count&&(t->turn[(count-1u)%XZ_TURN_SLOTS].bits&32u)!=0;
}
/* The connector's own turns (late_trail.h): its loop outranked the DECK 3/4 players until r58 and shares a core
   with them, so a long turn of its housekeeping is a stall of theirs. Parts: route state, snapshot and sync
   trail, dropout and stall trails, Jog LCD waveform service, lamps. */
static struct xz_late_trail connector_late_trail;
#define CONNECTOR_PART(n) do{uint64_t part_now=xz_late_us();connector_late_trail.part_us[n]=(uint32_t)(part_now-part_at);part_at=part_now;}while(0)
/* The connector's rank. It needs RR 60 only while it creates the USB pump, which inherits it. Its loop runs
   below the stock track readers (RR 58): status, log lines and lamps can wait a few milliseconds, audio cannot.
   XZ_EXTRA_SHM_DIR "/xz-connector-priority" holds another rank (1 to 60) for a test; it is read once a second. */
#define XZ_CONNECTOR_RANK 57
#define XZ_CONNECTOR_PUMP_RANK 60
#if defined(__linux__)
#include <sched.h>
static void connector_priority(int priority){
 struct sched_param now;
 if(sched_getscheduler(0)!=SCHED_RR||sched_getparam(0,&now)||now.sched_priority==priority)return;
 struct sched_param wanted={.sched_priority=priority};
 (void)sched_setscheduler(0,SCHED_RR,&wanted);
}
static int connector_rank_read(void){
 char text[8];int fd=open(XZ_EXTRA_SHM_DIR "/xz-connector-priority",O_RDONLY|O_CLOEXEC);
 if(fd<0)return XZ_CONNECTOR_RANK;
 ssize_t n=read(fd,text,sizeof text-1);close(fd);
 if(n<=0)return XZ_CONNECTOR_RANK;
 text[n]=0;int rank=atoi(text);
 return rank>=1&&rank<=XZ_CONNECTOR_PUMP_RANK?rank:XZ_CONNECTOR_RANK;
}
#else
static void connector_priority(int priority){(void)priority;}
static int connector_rank_read(void){return XZ_CONNECTOR_RANK;}
#endif
/* A loop on the DECK 3/4 core that was held up for 15 ms or more, logged whether or not audio was lost, with
   the time since boot so it can be set beside the kernel log. A second line follows once the core use trail
   (core_use.h) has a sample from after the stall: who used the core meanwhile. */
static void stall_trail(void){
 static uint64_t reported[4];static uint32_t window_at,lines;
 static struct{uint64_t from_us,to_us;uint32_t asked_ms;}owed[8];static unsigned owing;
 for(unsigned k=0;k<owing;){
  char line[520];
  int at=snprintf(line,sizeof line,"DECK 3/4 CORE USE for the stall at %u.%03u s: ",(unsigned)(owed[k].from_us/1000000u),(unsigned)(owed[k].from_us%1000000u/1000u));
  int found=xz_core_use_report?xz_core_use_report(owed[k].from_us,owed[k].to_us,line+at,sizeof line-(size_t)at):-1;
  if(!found&&(uint32_t)(now_ms()-owed[k].asked_ms)<1000u){k++;continue;}
  if(found>0)xz_log(line);
  owed[k]=owed[--owing];
 }
 static const char *const who[4]={"deck 3 worker","deck 4 worker","USB transport","connector"};
 const struct xz_late_trail *trail[4]={xz_extra_late_trail?&xz_extra_late_trail[0]:NULL,xz_extra_late_trail?&xz_extra_late_trail[1]:NULL,&xz_usb_late_trail,&connector_late_trail};
 uint32_t now=now_ms();
 if((uint32_t)(now-window_at)>=60000u){window_at=now;lines=0;}
 for(unsigned n=0;n<4;n++){
  struct xz_late_turn turn[XZ_LATE_SLOTS];
  unsigned count=trail[n]?xz_late_recent(trail[n],2000000u,turn):0;
  /* The connector rests 50 ms by its own choice and, below the audio threads, is preempted at will: only its
     own processor time in one turn says it did something long (a system call that had to find memory). */
  uint32_t limit=n==3u?8000u:15000u;
  for(unsigned k=0;k<count;k++){
   const struct xz_late_turn *t=&turn[k];
   uint32_t over=n==3u?0u:t->waited_us>t->asked_us?t->waited_us-t->asked_us:0;
   uint32_t busy=n==3u?t->user_us+t->system_us:t->worked_us;
   if(t->at_us<=reported[n])continue;
   reported[n]=t->at_us;
   if((over<15000u&&busy<limit)||lines>=20u)continue;
   char line[440];
   int at=snprintf(line,sizeof line,"DECK 3/4 CORE STALL: %s waited %u.%u ms (asked %u.%u), worked %u.%u ms, user %u.%u ms, kernel %u.%u ms, "
            "switches %u yielded %u preempted, page faults %u (%u from disk), at %u.%03u s since boot",who[n],
            (unsigned)(t->waited_us/1000u),(unsigned)(t->waited_us%1000u/100u),(unsigned)(t->asked_us/1000u),(unsigned)(t->asked_us%1000u/100u),
            (unsigned)(t->worked_us/1000u),(unsigned)(t->worked_us%1000u/100u),(unsigned)(t->user_us/1000u),(unsigned)(t->user_us%1000u/100u),
            (unsigned)(t->system_us/1000u),(unsigned)(t->system_us%1000u/100u),
            (unsigned)t->yielded,(unsigned)t->preempted,(unsigned)(t->minor_faults+t->major_faults),(unsigned)t->major_faults,
            (unsigned)(t->at_us/1000000u),(unsigned)(t->at_us%1000000u/1000u));
   if(n!=2u&&at>0&&at<(int)sizeof line-130)
    snprintf(line+at,sizeof line-(size_t)at,n==3u?"; loop parts ms: route %u.%u, snapshot+sync %u.%u, trails %u.%u, jog waveform %u.%u, lamps %u.%u"
             :"; step parts ms: intake+RAM %u.%u, controls %u.%u, handoff %u.%u, stream %u.%u, publish %u.%u",
             (unsigned)(t->part_us[0]/1000u),(unsigned)(t->part_us[0]%1000u/100u),(unsigned)(t->part_us[1]/1000u),(unsigned)(t->part_us[1]%1000u/100u),
             (unsigned)(t->part_us[2]/1000u),(unsigned)(t->part_us[2]%1000u/100u),(unsigned)(t->part_us[3]/1000u),(unsigned)(t->part_us[3]%1000u/100u),
             (unsigned)(t->part_us[4]/1000u),(unsigned)(t->part_us[4]%1000u/100u));
   xz_log(line);lines++;
   /* The stalled span: from when the loop should have woken to the end of its work. */
   if(owing<8u){owed[owing].from_us=t->at_us-over;owed[owing].to_us=t->at_us+t->worked_us;owed[owing].asked_ms=now;owing++;}
  }
 }
}
static void output_gap_trail(const xz_usb_position *p,const struct xz_extra_decks *s) {
 static struct { int valid,playing;uint32_t generation,steady_at;uint64_t missing; } seen[XZ_EXTRA_DECKS];
 static uint32_t window_at,lines;
 uint32_t now=now_ms();
 if((uint32_t)(now-window_at)>=60000u){window_at=now;lines=0;}
 for(int i=0;i<XZ_EXTRA_DECKS;i++){
  const xz_usb_deck_position *q=&p->deck[i];
  int playing=s->deck[i].playing!=0;
  /* A deck that has just started or jumped fills its first packets late. That is a start, not a dropout in
     running audio: it gets its own line, and OUTPUT GAP means the deck had played on for half a second. */
  if(!seen[i].valid||seen[i].generation!=q->applied_generation||playing!=seen[i].playing)seen[i].steady_at=now;
  int steady=(uint32_t)(now-seen[i].steady_at)>=500u;
  if(seen[i].valid&&seen[i].generation==q->applied_generation&&q->underrun_frames>seen[i].missing&&playing&&lines<30u){
   unsigned frames=(unsigned)(q->underrun_frames-seen[i].missing);
   char line[96];
   int ended=steady&&deck_at_end(i);
   snprintf(line,sizeof line,ended?"DECK %d END OF TRACK: queue ran out, %u ms (%u frames)":
            steady?"DECK %d OUTPUT GAP %u ms (%u frames) while playing":"DECK %d START GAP %u ms (%u frames)",
            i+XZ_EXTRA_FIRST_DECK+1,(unsigned)((uint64_t)frames*1000u/XZ_USB_RATE),frames);
   xz_log(line);lines++;
   if(steady&&!ended){
    gap_cause(i+XZ_EXTRA_FIRST_DECK+1,"deck worker",xz_extra_late_trail?&xz_extra_late_trail[i]:NULL);
    gap_cause(i+XZ_EXTRA_FIRST_DECK+1,"USB transport",&xz_usb_late_trail);
    gap_turns(i+XZ_EXTRA_FIRST_DECK+1,xz_extra_turn_trail?&xz_extra_turn_trail[i]:NULL,xz_usb_underrun_note?&xz_usb_underrun_note[i]:NULL);
   }
  }
  seen[i].valid=1;seen[i].playing=playing;seen[i].generation=q->applied_generation;seen[i].missing=q->underrun_frames;
 }
}
static void *connector(void *unused) {
 (void)unused;
 xz_extra_thread_place("connector");
 /* Creation can fail. Keep the new player private until it succeeds, and let
    this worker enter only after the public pointer and availability agree. */
 while(!__atomic_load_n(&route.published,__ATOMIC_ACQUIRE)&&!__atomic_load_n(&route.quit,__ATOMIC_ACQUIRE))rest(1);
 if(__atomic_load_n(&route.quit,__ATOMIC_ACQUIRE))return NULL;
 route_ever_up=route_last_up=0;route_faults=0;
 uint32_t epoch = 0;
 xz_usb_result refused = XZ_USB_OK;
 unsigned rank_tick=0;int rank=XZ_CONNECTOR_RANK;
 while (!__atomic_load_n(&route.quit, __ATOMIC_ACQUIRE)) {
  xz_late_woke(&connector_late_trail,0);uint64_t part_at=xz_late_us();
  /* Arm before discovery, so a publication during the attempt stays queued. */
  if(!route.usb)route_events_refresh();
  unsigned attempted=__atomic_load_n(&route_scan,__ATOMIC_ACQUIRE);
  if (route.closing) { close_route(); route_check_status(0,XZ_USB_OK,attempted); rest(POLL_DOWN_MS); continue; }
  if (!route.usb) {
   if(attempted==1)route_bridge_retry();
   char node[sizeof route.node];
   int node_found=find_node(node, sizeof node);
   if (node_found) {
    xz_usb_transport *usb = NULL;
    connector_priority(XZ_CONNECTOR_PUMP_RANK); /* the pump is created here and inherits this rank */
    xz_usb_result r = xz_usb_transport_open(node, &usb);
    if (r == XZ_USB_OK) {
     pthread_rwlock_wrlock(&route.lock);
     route.usb = usb; memcpy(route.node, node, sizeof node);
     pthread_rwlock_unlock(&route.lock);
     refused = XZ_USB_OK;
     route_check_status(1,refused,attempted);
     continue;
    }
    if ((r == XZ_USB_BUSY || r == XZ_USB_WRONG_DEVICE) && r != refused)
     say(r == XZ_USB_BUSY ? "DECK 3/4 ROUTE IS IN USE" : "DECK 3/4 ROUTE IS NOT THE MIXER");
    refused = r;
    if(r!=XZ_USB_BUSY&&r!=XZ_USB_WRONG_DEVICE&&route_faults<3u)route_faults++;
   } else refused=XZ_USB_OK;
   route_check_status(node_found,refused,attempted);
   route_events_wait();
   continue;
  }
  xz_usb_position p;
  (void)xz_usb_transport_position(route.usb, &p);   /* state is valid on AGAIN too */
  int lost = p.state == XZ_USB_FAULTED || p.state == XZ_USB_CLOSED || !usb_node(route.node);
  if (lost) {
   int was_up = __atomic_load_n(&link_epoch, __ATOMIC_ACQUIRE) != 0;
   set_link(0);
   if (was_up) say("DECK 3/4 ROUTE LOST");
   if(p.state==XZ_USB_FAULTED&&route_faults<3u)route_faults++;
   close_route();
   route_check_status(0,XZ_USB_OK,attempted);
   if(route.closing)rest(POLL_DOWN_MS);else route_events_wait();
   continue;
  }
  if (p.state == XZ_USB_RUNNING && !__atomic_load_n(&link_epoch, __ATOMIC_ACQUIRE)) {
   if (!++epoch) epoch = 1;
   set_link(epoch);
   xz_log("DECK 3/4 route up");
   route_events_unwatch();
  }
  if(p.state==XZ_USB_RUNNING)route_faults=0;
  route_check_status(1,XZ_USB_OK,attempted);
  if (p.state == XZ_USB_RUNNING) {
   if(rank_tick++%20u==0u)rank=connector_rank_read();
   connector_priority(rank);
   CONNECTOR_PART(0);
   struct xz_extra_decks snap;struct xz_extra_analysis_hold hold={0};
   (void)xz_extra_decks_snapshot_held(&snap,&hold);
   uint32_t beats[XZ_EXTRA_DECKS];
   for(int i=0;i<XZ_EXTRA_DECKS;i++)beats[i]=snap.deck[i].grid_count;
   xz_extra_decks_release_analysis(&snap,&hold);
   sync_trail(&snap,beats);
   CONNECTOR_PART(1);
   output_gap_trail(&p,&snap);
   stall_trail();
   CONNECTOR_PART(2);
   if(xz_jog_display_runtime_service_assets)xz_jog_display_runtime_service_assets();
   CONNECTOR_PART(3);
   if(xz_native_led_service_feedback)xz_native_led_service_feedback();
   CONNECTOR_PART(4);
  }
  xz_late_worked(&connector_late_trail,0x40000000u,4000u);
  rest(POLL_UP_MS);
 }
 set_link(0);
 for (int tries = 0; (route.usb || route.closing) && tries < 3; ++tries) if (!close_route()) break;
 return NULL;
}

/* USB STOP or a pulled stick (usb_release.h): DECK 3 and DECK 4 let go of it on their own workers. */
static int release_holder=-1;
static unsigned decks_let_go(void *unused,int slot,uint32_t stop){
 (void)unused;
 struct xz_xp_players *p=__atomic_load_n(&players,__ATOMIC_ACQUIRE);
 if(!p||!route.started||!xz_xp_release_source)return 0;
 /* First the copies: one being handed back to the stick right now must stay in RAM. */
 pthread_mutex_lock(&ram_lock);if(ram&&xz_ram_extra_source_gone)xz_ram_extra_source_gone(ram,(unsigned)slot);pthread_mutex_unlock(&ram_lock);
 return xz_xp_release_source(p,slot,stop);
}
static void deck_let_go(void *unused,int deck,int slot,uint32_t stop,enum xz_xp_release what){
 (void)unused;
 char line[200];
 snprintf(line,sizeof line,"USB_RELEASE: DECK %d, USB %d: %s",deck+3,slot,
          what==XZ_XP_RELEASE_KEPT?"its track plays on from its RAM copy; every file it had on the stick is closed":
          what==XZ_XP_RELEASE_UNLOADED?"its track was on the stick and not in RAM: unloaded, its file closed":
          "nothing loaded from this stick; kept as it is");
 xz_log(line);
 if(xz_usb_release_answer)xz_usb_release_answer(release_holder,slot,stop);
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
 if(xz_ram_extra_create&&xz_ram_extra_player_service&&xz_xp_set_ram_service){
  struct xz_ram_extra *service=xz_ram_extra_create(NULL,ram_route,ram_generation);
  if(service){
   struct xz_ram_player_service callbacks=xz_ram_extra_player_service(service);
   xz_xp_set_ram_service(p,&callbacks);
   pthread_mutex_lock(&ram_lock);ram=service;pthread_mutex_unlock(&ram_lock);
  }
 }
 if(xz_xp_set_source_generation&&xz_extra_source_generation)xz_xp_set_source_generation(p,cue_generation);
 (void)xz_cues_worker_start(xz_extra_resolve_track_checked,cue_store_notice,"/media/usb1","/media/usb2");
 xz_xp_set_cue_store(p,xz_cues_restore,xz_cues_enqueue);
 if(xz_extra_native_sync_clock)xz_xp_set_native_sync_clock(p,xz_extra_native_sync_clock);
 if(xz_xp_set_key_owner_reader)xz_xp_set_key_owner_reader(p,xz_key_owner_current);
 if(xz_xp_set_master_observer&&xz_sync_link_runtime_master_observe)
  xz_xp_set_master_observer(p,xz_sync_link_runtime_master_observe);
 if(xz_xp_set_release_answer)xz_xp_set_release_answer(p,deck_let_go,NULL);
 if (xz_xp_start(p)) { xz_cues_worker_stop();xz_xp_destroy(p); ram_stop(); return -1; }
 __atomic_store_n(&route.quit, 0, __ATOMIC_RELEASE);
 __atomic_store_n(&route.published, 0, __ATOMIC_RELEASE);
 route_events_init();
 if (pthread_create(&route.thread, NULL, connector, NULL)) {
  route_events_close();
  xz_cues_worker_stop();
  xz_xp_destroy(p); ram_stop();
  return -1;
 }
 __atomic_store_n(&players, p, __ATOMIC_RELEASE);
 route.started = 1;
 if(release_holder<0&&xz_usb_release_holder)release_holder=xz_usb_release_holder("DECK 3/4",decks_let_go,NULL);
 control_update(CONTROL_AVAILABLE,CONTROL_AVAILABLE);
 __atomic_store_n(&route.published, 1, __ATOMIC_RELEASE);
 return 0;
}

/* The players stay allocated: a draw thread may be inside a snapshot, and the library never unloads. */
void xz_extra_decks_stop(void) {
 if (!route.started) return;
 control_update(CONTROL_AVAILABLE,0);
 __atomic_store_n(&route.quit, 1, __ATOMIC_RELEASE);
 pthread_mutex_lock(&route_wake_lock);route_signal_locked();pthread_mutex_unlock(&route_wake_lock);
 pthread_join(route.thread, NULL);
 pthread_mutex_lock(&route_wake_lock);route_events_close();pthread_mutex_unlock(&route_wake_lock);
 xz_xp_stop(players);
 ram_stop();
 xz_cues_worker_stop();
 route.started = 0;
 __atomic_store_n(&route_scan,0,__ATOMIC_RELEASE);
}
