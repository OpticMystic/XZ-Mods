/* SPDX-License-Identifier: MIT */
#ifndef XZ_DJLINK_RUNTIME_H
#define XZ_DJLINK_RUNTIME_H
/* Pro DJ Link presence for DECK 3 and DECK 4: the player half of the xz-djlink helper's shared-memory bus
   (djlink/djl_bus.h). The helper, a separate process, does all network work; the player writes deck facts,
   supervises the helper and acts on the commands it queues.

   Threads:
     publish  any thread inside xz_extra_decks_snapshot_held, including the real-time connector. Memory
              writes, a priority-inheriting trylock and one monotonic clock read; skipped on contention.
     service  the network service worker's 100 ms wake (normal priority): bus lifetime, helper start,
              restart and stop, command intake, and a facts refresh when no snapshot has run for 1 s.
     status   the interface; never blocks.

   Each deck is announced while the setting is on and either the DECK 3/4 route is up or that deck holds a
   track: an empty deck stays on the network while four decks are in use, and a loaded deck keeps its
   number through a short route loss. With the setting off nothing exists: no bus file, no helper. */
#include <stdint.h>
#include "extra_decks.h"
#include "djlink_status.h"

#ifndef XZ_DJLINK_DIR
#define XZ_DJLINK_DIR "/dev/shm/xz-djlink"
#endif
#define XZ_DJLINK_HELPER XZ_DJLINK_DIR "/xz-djlink"
#define XZ_DJLINK_BUS XZ_DJLINK_DIR "/bus"
#define XZ_DJLINK_LOG XZ_DJLINK_DIR "/log"
#define XZ_DJLINK_STALE_HEARTBEAT_US 5000000u /* restart a helper whose heartbeat is older */
#define XZ_DJLINK_RESTART_US 30000000u        /* at most one start per 30 s */
#define XZ_DJLINK_REFRESH_US 1000000u         /* the service publishes itself after 1 s without a snapshot */

/* Start after the players, stop before them. Nothing happens until the setting is applied on. */
int xz_djlink_runtime_start(void);
void xz_djlink_runtime_stop(void);
/* The setting changed. Takes effect on the next service pass, at most 100 ms later. */
void xz_djlink_runtime_set_enabled(int enabled);
void xz_djlink_runtime_publish(const struct xz_extra_decks *held);
void xz_djlink_runtime_service(void);
/* 1 and a copy; 0 when the service is updating it this instant (keep the previous copy). */
int xz_djlink_runtime_status(struct xz_djlink_status *out);
/* The state the interface shows for a status copy. */
enum xz_djlink_state xz_djlink_state_of(const struct xz_djlink_status *);

/* Host tests replace the process and clock operations. NULL members keep the real ones. */
struct xz_djlink_test_ops {
 uint64_t (*now_us)(void);
 int32_t (*spawn)(void);                    /* returns a pid, or -1 */
 int (*terminate)(int32_t pid);             /* SIGTERM a verified helper; 1 when signalled */
 int (*reap)(int32_t pid);                  /* 1 when the child has exited */
};
void xz_djlink_runtime_test_install(const struct xz_djlink_test_ops *);
#endif
