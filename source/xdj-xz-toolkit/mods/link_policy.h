/* SPDX-License-Identifier: MIT */
#ifndef XZ_LINK_POLICY_H
#define XZ_LINK_POLICY_H
/* Ableton Link "send tempo": makes the Link session follow the unit's tempo master (link_clock.h), tempo and
   bar phase at quantum 4. Pure: no clock, I/O, allocation or locks; the caller passes now.

   Each call takes the latest master observation and Link state and returns at most one command.
   - Tempo: the session tempo is the master's effective BPM times (1 + r - p). r is the ratio between the
     master's audio clock and CLOCK_MONOTONIC (tens of ppm on this unit), estimated from the master's own bar phase
     against its time stamps, independent of the Link session; p is a proportional pull on the filtered phase
     error. In steady state p is a few ppm, so the session tempo stays within 0.005 BPM of the master's tempo as
     measured on the monotonic clock.
   - Phase: an error over XZ_LINK_STEP_US on XZ_LINK_STEP_COUNT consecutive fresh observations is a step (first
     lock, seek, loop jump, master change, a peer forcing the session) and gets one force-beat command, at most
     one per XZ_LINK_FORCE_GAP_US. Smaller errors are left to the trim.
   - A master that says it plays but does not move by its own tempo between two observations (the stock deck at
     the end of its track) is held like a paused one: nothing is sent; a step is forced only once the master has
     moved consistently again (rule 4 in link_policy.c).
   - Only observations under XZ_LINK_FRESH_US old with uncertainty under XZ_LINK_UNCERTAIN_US are used. With no
     valid master, a paused master or a stale Link state nothing is sent and the session keeps going as it is.
     A master that is being scratched or jog-bent (manual) is held, not chased.
   - A tempo change by the DJ goes out on the next fresh observation, without a force.
   - Tempos are sent in Link's own resolution (whole microseconds per beat). Once the clock ratio is learnt
     (XZ_LINK_LEARN_S of usable observations after the helper starts) three rules hold (link_policy.c):
     1. the ratio belongs to a clock: it is kept across seeks, loops, pitch changes, pauses and master changes
        between this unit's decks (one audio crystal), follows the clock only slowly (XZ_LINK_RATIO_TAU_S), and is
        dropped for a master on another device (EXTERNAL), whose clock is learnt anew; this unit's ratio is used
        again when the master returns;
     2. phase errors under 2 ms are absorbed slowly (at most 10 ppm), so observation noise, gaps and Link
        re-measuring its clock offset do not swing the tempo; the phase a DJ tempo move leaves behind and errors
        over 2 ms are pulled back faster, steps are forced;
     3. the tempo peers show to 0.01 BPM is held: the commanded tempo stays inside the shown value unless that
        costs more than 1 ms of phase, and a shown value reached that way is kept at least 60 s. A tempo change
        by the DJ or a new master shows at once. A phase error beyond 2 ms is recovered first, the shown value
        following it, and the hold comes back once the error is under 1 ms.
   - While a command is unacknowledged (state.applied_serial) nothing else is sent, for up to XZ_LINK_ACK_US. */
#include <stdint.h>
#include "link_clock.h"

#define XZ_LINK_FRESH_US 150000u
#define XZ_LINK_AHEAD_US 50000u      /* an observation may be stamped this far ahead of now (output latency) */
#define XZ_LINK_UNCERTAIN_US 2000u
#define XZ_LINK_STEP_US 10000.0
#define XZ_LINK_STEP_COUNT 3u
#define XZ_LINK_FORCE_GAP_US 1000000u
#define XZ_LINK_ACK_US 500000u
#define XZ_LINK_STATE_STALE_US 1000000u
#define XZ_LINK_RATIO_MAX 1e-4        /* ratio bound: the two crystals together stay well inside 100 ppm */
#define XZ_LINK_PULL_MAX 5e-4         /* proportional bound, reached only by errors of several ms */
#define XZ_LINK_LEARN_S 90.0          /* seconds of usable observations before the clock ratio counts as learnt */
#define XZ_LINK_RATIO_TAU_S 120.0     /* the ratio estimate weighs observations older than this less and less */

enum xz_link_clock { XZ_LINK_CLOCK_UNIT, XZ_LINK_CLOCK_EXTERNAL }; /* whose clock the ratio describes */

/* The clock ratio of one clock: the slope of the master's phase residual (its bar phase minus where its own BPM
   would have put it, in microseconds) against CLOCK_MONOTONIC (seconds), fitted by least squares with each unbroken
   run of observations (a segment) free to sit at its own offset, and weighted down over XZ_LINK_RATIO_TAU_S. The
   open segment's sums are kept relative to its newest point; closed segments keep only their centred sums. */
struct xz_link_ratio {
 int any;
 uint64_t last_us;         /* time of the newest point */
 double s0, su, suu, sr, sur;  /* open segment */
 double puu, pur;          /* closed segments, centred */
 double ratio;             /* slope, relative */
 double seconds;           /* observation time inside segments that went into the fit */
};

/* What the helper last published (xz_link_shm.h struct xz_link_state), in policy terms. */
struct xz_link_view {
 int valid, enabled;
 uint32_t peers, applied_serial;
 double tempo_bpm;
 double beat;              /* phase-encoded at quantum 4 */
 uint64_t at_us;           /* CLOCK_MONOTONIC at which beat holds */
};

enum xz_link_command_kind { XZ_LINK_CMD_NONE, XZ_LINK_CMD_TEMPO, XZ_LINK_CMD_FORCE };
struct xz_link_command {
 int kind;
 uint32_t serial;          /* to write into cmd.serial */
 double tempo_bpm;         /* TEMPO and FORCE */
 double beat;              /* FORCE: the session beat at at_us */
 uint64_t at_us;           /* FORCE: CLOCK_MONOTONIC; TEMPO applies at the helper's now */
};

enum xz_link_hold {
 XZ_LINK_HOLD_NONE, XZ_LINK_HOLD_NO_LINK, XZ_LINK_HOLD_PENDING, XZ_LINK_HOLD_NO_MASTER, XZ_LINK_HOLD_PAUSED,
 XZ_LINK_HOLD_MANUAL, XZ_LINK_HOLD_STALE, XZ_LINK_HOLD_UNCERTAIN, XZ_LINK_HOLD_REPEAT,
 XZ_LINK_HOLD_NOT_RUNNING   /* the master did not move by its own tempo since the last observation */
};

struct xz_link_policy {
 uint32_t serial;          /* last serial sent */
 int pending;
 uint64_t sent_us;
 int have;                 /* last_* hold a used observation of the current source and epoch */
 int source;
 uint32_t epoch;
 uint64_t last_at_us;
 double last_bpm, last_phase, slope;   /* master BPM and its latest rate of change, BPM per second */
 struct xz_link_ratio fit;  /* for the clock in `clock` */
 struct xz_link_ratio unit; /* this unit's, kept while an external master is followed */
 int clock;                /* enum xz_link_clock */
 uint32_t clock_epoch;     /* epoch of the EXTERNAL master whose clock is being learnt */
 double ratio;             /* r in use: fit.ratio once there is enough of it, else 0 */
 double error_f_us;        /* filtered phase error */
 int filtered;
 unsigned steps;           /* consecutive fresh observations beyond XZ_LINK_STEP_US */
 double run_us[3];         /* rule 4: the latest intervals' phase residuals against the master's own tempo, newest
                              first, microseconds (0 for an interval that jumped) */
 uint64_t last_force_us;
 int forced_once;
 double sent_master_bpm;   /* master BPM when the last tempo command went out */
 long shown;               /* tempo in 0.01 BPM the trim keeps peers showing; 0 when not held */
 double shown_bpm;         /* master BPM when `shown` was chosen */
 int crossed;              /* `shown` was reached for phase, at crossed_us: kept at least 60 s */
 uint64_t crossed_us;
 uint32_t crossings;       /* shown-value changes made for phase */
 int released;             /* the hold let go for a phase error beyond 2 ms */
 int recovering;           /* pulling back the phase a DJ tempo move left behind, since recover_us */
 int move_dir;             /* +1 after a faster DJ move, -1 after a slower one, until the shown value is chosen */
 uint64_t recover_us;
 double learnt_s;          /* fit.seconds */
 uint64_t tempo_sent_us;
 /* For status and tests. */
 int hold;                 /* enum xz_link_hold of the last call */
 double error_us;          /* last raw phase error, session minus master */
 uint32_t tempo_commands, force_commands, used, stale, uncertain, not_running;
};

void xz_link_policy_init(struct xz_link_policy *);
/* Returns 1 and fills *out when a command is due. now_us is CLOCK_MONOTONIC. */
int xz_link_policy_step(struct xz_link_policy *, const struct xz_link_master *, const struct xz_link_view *,
                        uint64_t now_us, struct xz_link_command *out);
#endif
