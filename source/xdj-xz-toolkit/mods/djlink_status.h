/* SPDX-License-Identifier: MIT */
#ifndef XZ_DJLINK_STATUS_H
#define XZ_DJLINK_STATUS_H
/* What the interface shows about Pro DJ Link presence for DECK 3 and DECK 4 (djlink_runtime.c). */
#include <stdint.h>

enum xz_djlink_state {
 XZ_DJLINK_OFF,           /* the setting is off: no bus, no helper */
 XZ_DJLINK_STARTING,      /* helper started, not yet heard or not yet through its number claim */
 XZ_DJLINK_ONLINE,        /* at least one of the decks holds its player number on the network */
 XZ_DJLINK_IDLE,          /* helper running, nothing to announce: no DECK 3/4 route and no track */
 XZ_DJLINK_NO_HELPER,     /* the helper binary is not staged in this build */
 XZ_DJLINK_NO_LINK,       /* the stock player is not announcing: no cable or link down */
 XZ_DJLINK_NUMBER_IN_USE, /* another device defended player 3 or 4 */
 XZ_DJLINK_FAILED         /* the bus could not be created or the helper could not be started */
};

struct xz_djlink_status {
 int enabled;
 enum xz_djlink_state state;
 int helper_present, helper_alive, bus_ready, start_failed;
 int32_t helper_pid;
 uint32_t heartbeat_age_ms;       /* UINT32_MAX when the helper never wrote one */
 uint32_t helper_up_ms;           /* since the heartbeat became fresh; 0 while not alive */
 uint8_t phase[2];                /* enum djl_phase of DECK 3 and DECK 4 */
 uint8_t announce[2];
 uint8_t unit_ip[4];
 int identity_ok;
 unsigned peers;
 int external_master_device;      /* last MASTER_GRANTED (3/4) or YIELD_MASTER target; 0 none. Recorded only. */
 uint8_t on_air[2];               /* last ON_AIR command per deck, 0xff none. Recorded only. */
 uint32_t helper_starts, publishes, publish_skipped, commands, loads, loads_rejected, loads_refused,
          sync_commands, master_commands, ignored, ring_dropped, sync_dropped;
};
#endif
