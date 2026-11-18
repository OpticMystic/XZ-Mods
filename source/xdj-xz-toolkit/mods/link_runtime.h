/* SPDX-License-Identifier: MIT */
#ifndef XZ_LINK_RUNTIME_H
#define XZ_LINK_RUNTIME_H
/* Ableton Link "send tempo": the player half of the xz-link-peer helper's shared-memory record
   (link/xz_link_shm.h). The helper, a separate GPL process, joins the Link session; the player decides what the
   session should do (link_policy.h) and writes it as commands. Decks are only read, never moved.

   Threads:
     publish  any thread inside xz_extra_decks_snapshot_held, including the real-time connector: a
              priority-inheriting trylock, one monotonic clock read and, only when DECK 3 or 4 is tempo master,
              that deck's facts (bounded memory work, djlink_facts.h). Skipped on contention.
     service  the network service worker (normal priority), every XZ_LINK_SERVICE_MS while the setting is on:
              record lifetime, helper start, restart and stop, the stock master clock copy, the policy, commands.
     status   the interface; never blocks.
     glance   any thread, the display thread included: one atomic load of the word the service stores at the end
              of every pass. No lock, no system call.

   With the setting off nothing exists: no record file, no helper process, no thread. */
#include <stdint.h>
#include "extra_decks.h"
#include "sync_master.h"
#include "sync_native.h"
#include "link_status.h"

#ifndef XZ_LINK_DIR
#define XZ_LINK_DIR "/dev/shm/xz-link"
#endif
#define XZ_LINK_HELPER XZ_LINK_DIR "/xz-link-peer"
#define XZ_LINK_SHM_PATH XZ_LINK_DIR "/state"
#define XZ_LINK_LOG XZ_LINK_DIR "/log"
#define XZ_LINK_HELPER_CPUS "0,2"
#define XZ_LINK_SERVICE_MS 40u                    /* stock samples arrive every 33-40 ms */
#define XZ_LINK_STALE_HEARTBEAT_US 5000000u       /* restart a helper whose heartbeat is older */
#define XZ_LINK_RESTART_US 30000000u              /* at most one start per 30 s */
#define XZ_LINK_STATE_FRESH_US 1000000u           /* Link state older than this is not used */

/* Start after the players, stop before them. Nothing happens until the setting is applied on. */
int xz_link_runtime_start(void);
void xz_link_runtime_stop(void);
/* The setting changed (0 off, 1 send). Takes effect on the next service pass. */
void xz_link_runtime_set_enabled(int mode);
void xz_link_runtime_publish(const struct xz_extra_decks *held);
/* One service pass. Returns the wake period it wants in ms (XZ_LINK_SERVICE_MS while on), 0 for the caller's own. */
unsigned xz_link_runtime_service(void);
/* 1 and a copy; 0 when the service is updating it this instant (keep the previous copy). */
int xz_link_runtime_status(struct xz_link_status *out);
/* State, setting and peer count as the last service pass published them. Like status, a setting changed since
   that pass reads as STARTING (on) or OFF (off) at once. Always fills out. */
void xz_link_runtime_glance(struct xz_link_glance *out);
enum xz_link_run xz_link_run_of(const struct xz_link_status *);

/* Host tests replace the process, clock and observation operations. NULL members keep the real ones. */
struct xz_link_test_ops {
 uint64_t (*now_us)(void);
 int32_t (*spawn)(void);                                   /* returns a pid, or -1 */
 int (*terminate)(int32_t pid);                            /* SIGTERM a verified helper; 1 when signalled */
 int (*reap)(int32_t pid);                                 /* 1 when the child has exited */
 int (*observe)(struct xz_master_observation *);           /* xz_sync_link_runtime_master_observe */
 int (*stock_clock)(struct xz_sync_native_snapshot *);     /* xz_extra_native_sync_clock */
};
void xz_link_runtime_test_install(const struct xz_link_test_ops *);
#endif
