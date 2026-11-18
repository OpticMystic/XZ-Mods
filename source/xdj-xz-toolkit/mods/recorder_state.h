/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifndef XZ_RECORDER_STATE_H
#define XZ_RECORDER_STATE_H
#include <stddef.h>
#include <stdint.h>

/* The stock MASTER REC recorder as seen from inside the player (firmware 1.26 only).
 * See recorder/SEAM.md for the pointer chain, the evidence and what each field means. */
struct xz_recorder_state {
    int known;            /* 0: this build cannot tell (other firmware, a guard failed, no recorder object) */
    int recording;        /* 1 while the stock recorder is writing a file (WavWriter +0x9c) */
    uint32_t elapsed_ms;  /* position of the audio captured now in the current WAV file; 0 when not recording */
    uint32_t generation;  /* changes for every new recording file (start, TRACK MARK, 3-hour split) */
    char file[256];       /* final path "<mount>/PIONEER REC/RECnnn.WAV" while recording, "" otherwise or when
                             not known. Stock writes RECTMP.WAV and renames it to this name when the file closes. */
};

/* UI or worker thread only, never the audio thread (it makes syscalls). Cheap: about ten guarded reads; the
 * path text is re-read only when it changes. Returns 0 when out->known is 1, -1 otherwise. Never writes to the
 * player, calls no firmware function, takes only a priority-inheriting mutex of its own. */
int xz_recorder_state(struct xz_recorder_state *out);

/* Called from the existing notify_rec_complete hook (mods/ram/removal.c, UI thread) with the stock result code
 * (3 stopped, 4 split, negative = error; 2 "started" never reaches that hook, see SEAM.md). Kept for display and
 * diagnostics, never needed for correctness. */
void xz_recorder_note_result(int code);
int xz_recorder_last_result(void);   /* 0 when none since start */

/* Backlog = frames the audio thread put into the stock ring that the writer thread has not written yet; the stock
 * recorder silently loses audio once it reaches 1,000,000 frames (22.7 s). max is the largest value seen by polls
 * since the current file began. -1 (both 0) when not recording, not known, or the ring could not be read. */
int xz_recorder_backlog(uint32_t *now_frames, uint32_t *max_frames);

/* The recorder as one JSON object for the runtime's status reply (TCP 50010, ui_runtime.c). Polls once. No track
 * names; the file is given as its base name RECnnn.WAV only. Returns the length, 0 when it does not fit. */
size_t xz_recorder_status_json(char *out, size_t capacity);

/* ---- Pure core, host-testable: everything above is a thin wrapper over these. ---- */
typedef int (*xz126_recorder_read_fn)(void *context, uint32_t address, void *out, size_t n);

enum xz126_recorder_reason {
    XZ126_REC_OK = 0,
    XZ126_REC_NO_READER,
    XZ126_REC_BAD_ABI,       /* code guard mismatch: not firmware 1.26 */
    XZ126_REC_NO_ENGINE,     /* PlayEngine singleton not created yet */
    XZ126_REC_NO_INPUT,      /* RecInput not created */
    XZ126_REC_BAD_WRITER,    /* WavWriter pointer or vtables wrong */
    XZ126_REC_UNSTABLE,      /* two snapshots kept disagreeing, or the chain moved while it was read */
};

/* The writer changes several fields one after another (WavWriter::run, fileRenew, eventRecExit). A poll that falls
 * between them is held for this long and then accepted (recorder/SEAM.md, "Windows inside the writer"). */
#define XZ126_REC_SETTLE_MS 200u

/* Mod-side memory between polls: the generation counter and the cached path text. Zero-initialise. */
struct xz126_recorder_tracker {
    uint32_t now_ms;          /* set by the caller before each poll: a monotonic clock in ms */
    int abi_checked;          /* code guards passed once (code is immutable) */
    int abi_refused;          /* code guards were read and differ: another firmware, for good */
    int seen;                 /* a known sample was taken before */
    int was_recording;
    int32_t file_no;          /* file number at the last accepted sample (recording or idle) */
    int32_t written;          /* frames written to the current file at the last accepted sample */
    uint32_t elapsed_ms;      /* elapsed_ms reported at the last accepted sample */
    int pending;              /* a writer transition seen in the middle (1 start, 2 file closing), since pending_ms */
    uint32_t pending_ms;
    uint32_t generation;
    uint32_t path_ptr;        /* cache key for path */
    int32_t path_no;
    char path[256];
    int last_reason;
    int backlog_known;        /* the ring indices were read at the last sample */
    uint32_t backlog;         /* ring frames captured but not yet written, at the last poll */
    uint32_t backlog_max;     /* largest backlog seen by polls in the current file */
};

/* One poll. Fills *out (always, known=0 on failure) and returns the reason. */
int xz126_recorder_poll(xz126_recorder_read_fn read, void *context,
                        struct xz126_recorder_tracker *tracker, struct xz_recorder_state *out);

/* Constants shared with the host test. */
#define XZ126_REC_PLAY_ENGINE_CELL UINT32_C(0x010e9688)
#define XZ126_REC_ENGINE_INPUT     0x18u
#define XZ126_REC_INPUT_WRITER     0x08u
#define XZ126_REC_WRITER_SIZE      0x140u
#define XZ126_REC_WRITER_VTABLE    UINT32_C(0x003d2fa0)
#define XZ126_REC_WRITER_VTABLE2   UINT32_C(0x003d2fb4)
#define XZ126_REC_SAMPLE_RATE      44100u
#define XZ126_REC_RING_FRAMES      1000000
#define XZ126_REC_MAX_FILE_FRAMES  476280000   /* 44100 * 10800: 3-hour split */
#endif
