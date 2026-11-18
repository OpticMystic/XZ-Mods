/* SPDX-License-Identifier: MIT */
#ifndef XZ_DIARY_H
#define XZ_DIARY_H
#include <stddef.h>
#include <stdint.h>

#define XZ_DIARY_SEGMENTS 8u
#define XZ_DIARY_SEGMENT_BYTES 524288u
#define XZ_DIARY_BATCH_BYTES 8192u
#define XZ_DIARY_MIN_FREE (64u * 1024u * 1024u)
#define XZ_DIARY_INTERVAL_MS 30000u
#define XZ_DIARY_LINE_BYTES 1536u
#define XZ_DIARY_SESSION_MS 604800000u

enum xz_diary_component { XZ_DIARY_RUNTIME, XZ_DIARY_USB, XZ_DIARY_SETTINGS,
 XZ_DIARY_DECK, XZ_DIARY_SYNC, XZ_DIARY_RAM, XZ_DIARY_UI, XZ_DIARY_COMPONENTS };
enum xz_diary_state { XZ_DIARY_UNKNOWN, XZ_DIARY_STARTED, XZ_DIARY_PRESENT,
 XZ_DIARY_REMOVED, XZ_DIARY_CHANGED, XZ_DIARY_EMPTY, XZ_DIARY_LOADING,
 XZ_DIARY_READY, XZ_DIARY_PLAYING, XZ_DIARY_PAUSED, XZ_DIARY_ERROR,
 XZ_DIARY_OFF, XZ_DIARY_FOLLOWING, XZ_DIARY_LOCKED, XZ_DIARY_SHOWN, XZ_DIARY_STATES };
enum xz_diary_reason { XZ_DIARY_NONE, XZ_DIARY_OBSERVED, XZ_DIARY_TRACK_LOAD,
 XZ_DIARY_RAM_COPY, XZ_DIARY_READ_BUSY, XZ_DIARY_SPACE_LOW, XZ_DIARY_READ_ONLY,
 XZ_DIARY_IO_ERROR, XZ_DIARY_QUEUE_FULL, XZ_DIARY_SOURCE_CHANGED,
 XZ_DIARY_NO_GRID, XZ_DIARY_MANUAL, XZ_DIARY_CLOCK_LOST, XZ_DIARY_RANGE_LIMIT,
 XZ_DIARY_NO_MASTER, XZ_DIARY_PHASE_UNAVAILABLE, XZ_DIARY_DISCONTINUITY,
 XZ_DIARY_LOOP, XZ_DIARY_ALIGNING, XZ_DIARY_EXTERNAL_MASTER, XZ_DIARY_NO_BPM,
 XZ_DIARY_TEMPO_ONLY, XZ_DIARY_REASONS };
enum xz_diary_notice { XZ_DIARY_NOTICE_NONE, XZ_DIARY_NOTICE_OTHER,
 XZ_DIARY_NOTICE_TRACK_MISSING, XZ_DIARY_NOTICE_TRACK_READ,
 XZ_DIARY_NOTICE_SOURCE_CHANGED, XZ_DIARY_NOTICE_ROUTE_CHANGED,
 XZ_DIARY_NOTICE_KEY_BUSY, XZ_DIARY_NOTICE_KEY_PENDING,
 XZ_DIARY_NOTICE_JUMP_LIMIT, XZ_DIARY_NOTICE_CUES_SESSION,
 XZ_DIARY_NOTICE_CUES_INVALID, XZ_DIARY_NOTICE_SCRATCH_TRUNCATED,
 XZ_DIARY_NOTICES };

struct xz_diary_session {
 unsigned char id[16], settings_hash[32];
 int have_settings, modern, deck_view, firmware_verified;
 unsigned slots;
 uint32_t epoch;
 const char *version; /* Build constant only; formatter still validates grammar. */
};
struct xz_diary_event {
 unsigned component, state, reason, notice, deck, slot, value;
};
/* -1 means unavailable, except phase where INT32_MIN means unavailable. */
struct xz_diary_metrics {
 int cpu_percent, free_ram_kib, usb_read_latency_us;
 int32_t phase_ms[4], key_max_us[4], underrun_frames[4];
 unsigned dropped;
};
int xz_diary_header(char *,size_t,const struct xz_diary_session *,unsigned generation);
int xz_diary_event_line(char *,size_t,unsigned sequence,uint32_t ms,const struct xz_diary_event *);
int xz_diary_metrics_line(char *,size_t,unsigned sequence,uint32_t ms,const struct xz_diary_metrics *);
unsigned xz_diary_notice_code(const char *);
unsigned xz_diary_error_code(const char *);
void xz_diary_metrics_unknown(struct xz_diary_metrics *);

/* These hooks do only bounded atomics, no allocation, locks or filesystem calls. */
void xz_diary_busy_begin(unsigned slot_mask);
void xz_diary_busy_end(unsigned slot_mask);
void xz_diary_audio(unsigned deck,uint64_t underrun_frames,unsigned key_max_us,int key_valid);
void xz_diary_notice(unsigned component,unsigned notice);
int xz_diary_start(void);
void xz_diary_stop(void);
#endif
