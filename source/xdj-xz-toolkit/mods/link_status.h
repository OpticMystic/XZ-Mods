/* SPDX-License-Identifier: MIT */
#ifndef XZ_LINK_STATUS_H
#define XZ_LINK_STATUS_H
/* What the interface shows about Ableton Link (link_runtime.c). */
#include <stdint.h>

enum xz_link_run {
 XZ_LINK_RUN_OFF,        /* the setting is off: no record, no helper */
 XZ_LINK_RUN_STARTING,   /* helper started, no heartbeat yet */
 XZ_LINK_RUN_ENABLED,    /* in the session; the session follows the unit's tempo master */
 XZ_LINK_RUN_NO_MASTER,  /* in the session, but the unit has no tempo master to send */
 XZ_LINK_RUN_NO_HELPER,  /* the helper binary is not staged in this build */
 XZ_LINK_RUN_FAILED      /* the record could not be created or the helper could not be started */
};

struct xz_link_status {
 int enabled;
 enum xz_link_run state;
 int helper_present, helper_alive, shm_ready, start_failed;
 int32_t helper_pid;
 uint32_t heartbeat_age_ms;     /* UINT32_MAX when the helper never wrote one */
 unsigned peers;                /* other Ableton Link apps in the session */
 double session_bpm;            /* 0 while unknown */
 int phase_known;
 int32_t phase_error_us;        /* session minus master at the last used observation */
 int source;                    /* enum xz_link_source of the master the policy last saw */
 int hold;                      /* enum xz_link_hold of the last policy pass */
 uint32_t tempo_commands, force_commands, helper_starts, soft_publishes, soft_skipped, services;
};

/* The part of the status a glance needs (MODERN PLAY's ABLETON LINK control): read from one word, so any thread
   reads it without a lock or a call into the network side. peers is meaningful in ENABLED and NO_MASTER. */
struct xz_link_glance {
 int enabled;
 enum xz_link_run state;
 unsigned peers;
};
#endif
