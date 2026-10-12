/* SPDX-License-Identifier: MIT */
#ifndef XZ_PC_STEMS_H
#define XZ_PC_STEMS_H
#include <stddef.h>
#include <stdint.h>

enum xz_pc_stem_role {XZ_PC_DRUMS, XZ_PC_INST, XZ_PC_BASS, XZ_PC_VOCAL, XZ_PC_ROLES};
#define XZ_PC_DECKS 4
enum xz_pc_stem_cap {XZ_PC_TOGGLE=1, XZ_PC_FEEDBACK=2};
/* Deliberately no absolute mute/level or bypass capability until live proof. */
struct xz_pc_stem_state {
 uint64_t generation;
 uint32_t known,active,pending;
 unsigned caps;
 int connected,verified;
 uint64_t sent_at[4];
};
struct xz_pc_stem_sink {
 void *context;
 int (*note)(void *,unsigned channel,unsigned note,int pressed);
};
struct xz_pc_stems {struct xz_pc_stem_state deck[XZ_PC_DECKS];struct xz_pc_stem_sink sink;};

void xz_pc_stems_init(struct xz_pc_stems *,struct xz_pc_stem_sink);
/* Called by the owner after a source/session or loaded-track change. No replay. */
void xz_pc_stems_session(struct xz_pc_stems *,unsigned deck,int connected,uint64_t generation);
/* Explicit successful live test, or a future separately qualified detector. */
void xz_pc_stems_verify(struct xz_pc_stems *,unsigned deck);
int xz_pc_stems_toggle(struct xz_pc_stems *,unsigned deck,unsigned role,uint64_t now_ms);
int xz_pc_stems_feedback(struct xz_pc_stems *,const uint8_t *message,size_t size);
void xz_pc_stems_tick(struct xz_pc_stems *,uint64_t now_ms);

/* Hash/ABI guarded native sender. It reuses firmware MidiTransfer and its lock.
 * Never opens a USB/MIDI reader and never changes local audio or ASIO state. */
int xz_pc_stems_native_ready(void);
int xz_pc_stems_native_redraw(void);
struct xz_pc_stem_sink xz_pc_stems_native_sink(void);
/* Read-only decode of a stock JUCE MidiMessage: buffer +8, byte count +12.
 * Caller keeps stock callback delivery intact. Installing its hook is separate. */
int xz_pc_stems_native_feedback(struct xz_pc_stems *,const void *juce_message);
struct xz_pc_stem_source {int pc,slot,linked,rekordbox,loaded;uint32_t epoch;};
int xz_pc_stems_native_source(unsigned physical,struct xz_pc_stem_source *);
int xz_pc_stems_native_observe(void);
struct xz_pc_wave_stamp {uint32_t epoch,serial,count;int complete,busy;};
int xz_pc_wave_native_stamp(unsigned slot,struct xz_pc_wave_stamp *);
#endif
