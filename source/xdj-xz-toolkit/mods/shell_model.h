/* SPDX-License-Identifier: MIT */
#ifndef XZ_SHELL_MODEL_H
#define XZ_SHELL_MODEL_H
/* Fills the FLAGSHIP shell model from the deck's own state, using only the read contracts proven in
   VJToolsLibrary docs/research/2026-10-18-xz-shell-abi.md section 7 (R3 stat frames, R4 settings,
   R5 title text, R6 cautions) plus what the runtime already reads (key snapshot, native clock grid,
   PWV7 waves). A field that cannot be read this tick keeps its known bit clear. */
#include <stddef.h>
#include <stdint.h>
#include "shell/shell.h"

/* Everything the model points into lives here, so the model never points at native memory. */
struct xz_shell_model_storage;
size_t xz_shell_model_storage_size(void);

/* Reads native memory through this function only (the runtime passes xz_read_memory; host tests pass a
   fake image). Returns 0 on success; a failed read leaves the destination untouched. */
typedef int (*xz_shell_read_fn)(uintptr_t address, void *destination, size_t length);

/* Refreshes model and storage. Safe on the Flip thread: reads only, double-reads mutable cells, never
   calls native functions. now_ms is a monotonic clock. Returns a bitmask of decks whose loaded track
   changed (bit 0 deck 1, bit 1 deck 2) so the caller can start a waveform load. */
unsigned xz_shell_model_refresh(struct xz_shell_model *model, struct xz_shell_model_storage *storage,
                                xz_shell_read_fn read, uint32_t now_ms);

/* Hands decoded waveform data for one deck to the model (called by the runtime's worker after it loads
   the deck's PWV7 analysis; NULL clears it). Columns are copied into storage. */
void xz_shell_model_set_wave(struct xz_shell_model_storage *storage, int deck,
                             const struct xz_shell_column *scroll, size_t scroll_count, uint32_t scroll_span_ms,
                             const struct xz_shell_column *overview, size_t overview_count, uint32_t overview_span_ms);

/* The stock caution currently showing (0 = none); while non-zero the runtime gives the screen to stock. */
uint32_t xz_shell_model_caution(const struct xz_shell_model_storage *storage);

/* Values the runtime already reads elsewhere (key snapshot, native clock grid and bar phase, loaded and
   playing, source device kind 2/3). Copied on the next refresh; call it on the same serialized path as
   refresh and render. */
void xz_shell_model_set_input(struct xz_shell_model_storage *storage, int deck,
                              const struct xz_shell_deck *input, unsigned device_kind);

/* Guard failures counted per read family: [0] R3 stat frames, [1] R4 settings, [2] R5 title, [3] R6 caution. */
void xz_shell_model_diagnostics(const struct xz_shell_model_storage *storage, uint32_t out[4]);

/* Hot cue OUT time (loop hot cues); returns 0 when not known. */
int xz_shell_model_hot_cue_out(const struct xz_shell_model_storage *storage, int deck, int cue, int32_t *out_ms);
#endif
