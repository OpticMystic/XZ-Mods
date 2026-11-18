/* SPDX-License-Identifier: MIT */
#ifndef XZ_TRACKLIST_H
#define XZ_TRACKLIST_H
/* Per-recording tracklist: the pure part. No threads, no clock, no files.

   The runtime (tracklist_runtime.c) samples the four decks, the mixer's on-air flags and the recorder about
   twice a second and hands each sample to xz_tl_step. The core decides which tracks were heard and when, and
   queues file operations (open with a header, append lines, close with a footer, write a CUE sheet). The
   runtime's writer thread carries them out. Host tests drive the core with a simulated set and compare the
   text it produces.

   Rules (REPORT.md, "What counts as played"):
   - A deck is audible while it is loaded, playing and its mixer channel is on air (decks 3 and 4 also need
     the CH3/CH4 route up; the caller folds that into on_air).
   - Audible moments of one loaded track form a run; gaps shorter than gap_ms are bridged.
   - A run that has been audible for heard_ms in total is listed, at the recording time of its first audible
     moment. One entry per load. A track that was already heard less than repeat_ms ago (doubles on the
     other deck, a reload) is not listed again.
   - When the mixer's on-air flags cannot be read, audible means playing and the threshold is
     heard_ms_no_air; the header says so.
   - A run that started before the file (before the recording, or before a TRACK MARK or 3-hour split) is listed at
     00:00:00 and marked "(continued)".
   - When a file ends, a run still waiting that was audible for tail_ms inside the file is listed too.
   - Lines go out in time order: an entry waits while a still-pending run on another deck started earlier,
     at most hold_max_ms. */
#include <stddef.h>
#include <stdint.h>

#define XZ_TL_DECKS 4
#define XZ_TL_TEXT 128          /* UTF-8 bytes per title or artist, including the terminator */
#define XZ_TL_PATH 256          /* as struct xz_recorder_state.file */
#define XZ_TL_ENTRIES 400       /* per file; a six-hour set at 1 min a track fits. Later tracks are dropped */
#define XZ_TL_OPS 64            /* queued file operations; one per track in normal use */
#define XZ_TL_LINE 512

struct xz_tl_config {
 uint32_t heard_ms;             /* audible time before a track is listed (30 s) */
 uint32_t heard_ms_no_air;      /* the same when on-air cannot be read: play time (60 s, as stock HISTORY) */
 uint32_t gap_ms;               /* silence bridged inside one run (8 s) */
 uint32_t repeat_ms;            /* the same track again within this is not a new entry (60 s) */
 uint32_t tail_ms;              /* a pending run heard this long when its file ends is listed (10 s) */
 uint32_t hold_max_ms;          /* the longest an entry waits for an earlier pending run (120 s) */
 uint32_t air_hold_ms;          /* a failed on-air read keeps the last value this long (5 s) */
 uint32_t step_max_ms;          /* the most one sample may add to heard time (1 s) */
 uint32_t rec_hold_ms;          /* the recorder unreadable this long ends an open file (5 s); shorter gaps change
                                   nothing (the seam reports "not known" for a poll that keeps seeing it move) */
 int cue_sheet;                 /* also write NAME.CUE when a file ends cleanly */
 int at_end;                    /* 1 (default): keep the list in RAM and write the whole file when it ends, so the
                                   stick sees no tracklist write while it records; a pulled stick or power cut
                                   then loses the list. 0: live, header at the start and one line about 30 s after
                                   each track comes in, each fsynced, so the file survives a pull up to its last line */
};
void xz_tl_default_config(struct xz_tl_config *);

/* What the runtime read for one deck this tick. */
struct xz_tl_deck_in {
 int known;                     /* the deck could be read this tick; 0: no audible time is added */
 int loaded;
 uint64_t track_key;            /* (source << 32) | rekordbox id: the same track on any deck has the same key.
                                   0 when not known; identity then falls back to load_id and the text */
 uint32_t load_id;              /* changes with every LOAD when the deck can tell (deck 3/4 jog epoch), else 0 */
 int text_known;                /* title (and artist when non-empty) belong to this load */
 char title[XZ_TL_TEXT], artist[XZ_TL_TEXT];
 int playing;                   /* 1, 0, or -1 unknown */
 int on_air;                    /* 1, 0, or -1 unknown */
};
/* The recorder, as mods/recorder_state.h gives it (recorder/SEAM.md). */
struct xz_tl_recorder {
 int known, recording;
 int elapsed_known;             /* elapsed_ms is the time into the current file */
 uint32_t elapsed_ms;
 uint32_t generation;           /* changes for every new file, TRACK MARK splits included */
 char file[XZ_TL_PATH];         /* the WAV's full path, "" when not known */
};
struct xz_tl_input {
 uint32_t now_ms;               /* monotonic */
 struct xz_tl_recorder rec;
 struct xz_tl_deck_in deck[XZ_TL_DECKS];
};

enum xz_tl_op_kind {
 XZ_TL_OP_OPEN,                 /* create path (truncate a stale file), write text */
 XZ_TL_OP_RESUME,               /* append to path (create when missing), write text: the mods restarted */
 XZ_TL_OP_APPEND,               /* append text */
 XZ_TL_OP_CLOSE,                /* append text (the footer); the file is complete */
 XZ_TL_OP_WHOLE                 /* write text as the whole file (the CUE sheet, or the TXT in at_end mode) */
};
struct xz_tl_op {
 enum xz_tl_op_kind kind;
 uint32_t file_seq;             /* which recording file; the writer stops a file's ops after its first failure */
 char path[XZ_TL_PATH + 8];     /* the tracklist file (NAME.TXT or NAME.CUE) */
 char wav[XZ_TL_PATH];          /* the recording beside it: the writer only writes while it exists */
 size_t length;
 char *text;                    /* heap, owned by the op: xz_tl_op_free */
};
void xz_tl_op_free(struct xz_tl_op *);

struct xz_tl_status {
 int file_open;                 /* a tracklist file is being written */
 int on_air_mode;               /* 1 listed by on-air, 0 by play time (on-air unreadable) */
 uint32_t entries, files, ops_dropped, entries_dropped;
};

struct xz_tl;                   /* about 120 KiB: allocate once */
size_t xz_tl_size(void);
void xz_tl_init(struct xz_tl *, const struct xz_tl_config *);
/* One sample. Queues file operations; never blocks, never allocates more than the op texts. */
void xz_tl_step(struct xz_tl *, const struct xz_tl_input *);
/* The mods are stopping: end an open file with a note ("XZ Mods stopped at ..."). */
void xz_tl_finish(struct xz_tl *, uint32_t now_ms);
/* USB STOP of the stick mounted at mount ("/media/usb1/", with the slash): when the open list's recording lies
   under it, the list ends now as at the end of the recording (tail rule, CUE sheet) with a footer saying the stick
   was stopped. 1 when it ended a list, 0 when none was open there. The recording is not listed again afterwards. */
int xz_tl_stick_stopped(struct xz_tl *, const char *mount, uint32_t now_ms);
/* The open list's recording (its WAV path); "" when no list is open. */
const char *xz_tl_open_wav(const struct xz_tl *);
/* The DJ turned RECORDING TRACKLIST off: end an open file with a note saying so. */
void xz_tl_turn_off(struct xz_tl *, uint32_t now_ms);
/* The DJ turned it on (again): a recording already running at the next known sample gets no list; the next file
   (TRACK MARK, a 3-hour split or a new recording) does. */
void xz_tl_skip_running(struct xz_tl *);
/* Oldest queued operation into *out (ownership moves to the caller); 0 when none. */
int xz_tl_take(struct xz_tl *, struct xz_tl_op *out);
void xz_tl_status(const struct xz_tl *, struct xz_tl_status *);

/* Pure formatting, exported for tests and for a PC converter. */
void xz_tl_clock(char out[16], uint32_t ms);                 /* "HH:MM:SS", hours past 99 keep growing */
/* "Artist - Title", the title alone, or the fallback when both are empty. Control characters become spaces,
   malformed UTF-8 becomes '?', text is cut at a character boundary. */
size_t xz_tl_name(char *out, size_t cap, const char *artist, const char *title, int deck);
/* NAME.WAV -> NAME.TXT (or .CUE): same folder, same base name, extension case follows the WAV's.
   0 on success, -1 when the path does not end in .wav (any case) or does not fit. */
int xz_tl_sibling(char *out, size_t cap, const char *wav, const char *ext_upper);
#endif
