/* SPDX-License-Identifier: MIT */
#ifndef XZ_TRACKLIST_RUNTIME_H
#define XZ_TRACKLIST_RUNTIME_H
#include <stddef.h>
#include <stdint.h>
/* The tracklist on the unit: one sampler thread (memory reads only, never the stick) and one writer thread (the
   only one that touches the stick, at nice 19 and idle I/O priority).

   On and off: the MODS setting RECORDING TRACKLIST (settings recording_tracklist), which writes the whole list
   when the recording file closes and nothing while the stick records. The environment variable
   XZ_MODS_TRACKLIST overrides the setting, for tests only: "1" the same mode, "live" a line per track while
   recording, "0" off. Nothing runs unless this runtime may write (writes.h).

   xz_tracklist_start: runtime.c's init path, after the deck 3/4 players. It arms the feature and starts the
   threads at once when the variable or the setting already asks for it. xz_tracklist_set_enabled: the setting,
   from the UI (any time, also before start); the threads start on the first ON after start and then stay, an
   OFF only stops listing. xz_tracklist_stop: runtime.c's destructor. */
int xz_tracklist_start(void);
void xz_tracklist_set_enabled(int on);
void xz_tracklist_stop(void);
/* The tracklist's state as one JSON object for the runtime's status reply (TCP 50010, ui_runtime.c). Counters and
   modes only: no titles, artists or paths. Returns the length, 0 when it does not fit. */
size_t xz_tracklist_status_json(char *out, size_t capacity);

/* Read by the coordinating session's tools from the running player (plain memory read). */
struct xz_tracklist_proof {
 uint32_t version, started, samples, sample_us_max;
 uint32_t mode, enabled;                                     /* mode 0 none, 1 at the end, 2 live; enabled now */
 uint32_t air_guard, air_reads, air_failures, air_raw;      /* air_raw: the mixer word, CH1 in the top byte */
 uint32_t rec_known, recording, generation, elapsed_ms;
 uint32_t file_open, on_air_mode, entries, files, ops_dropped, entries_dropped;
 uint32_t ops, writes, syncs, failures, skipped, last_errno;
 uint32_t write_us_max, idle_waits, idle_timeouts, io_idle;
 uint32_t text_lookups, text_found;
 /* USB STOP (tracklist_runtime.c): asks that said an answer follows, lists ended by a stop, answers given, files
    written and operations failed or skipped at a stop, the time from the ask to the answer (last and largest, ms),
    whether the writer got nice 0 for it. */
 uint32_t stop_asks, stop_lists, stop_answers, stop_files, stop_failures, stop_ms_last, stop_ms_max, stop_nice_ok;
};
#endif
