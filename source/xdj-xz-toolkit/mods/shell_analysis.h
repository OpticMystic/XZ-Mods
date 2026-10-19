/* SPDX-License-Identifier: MIT */
#ifndef XZ_SHELL_ANALYSIS_H
#define XZ_SHELL_ANALYSIS_H
/* Track -> rekordbox analysis folder for one USB, for the FLAGSHIP shell's waveforms.

   ui/layered_wave_source.c keeps one thread-local index for the last root it saw and rebuilds it
   (opening every ANLZ0000.DAT on the stick) whenever the root changes or a lookup misses for 15 s.
   With deck 1 on USB1 and deck 2 on USB2 every wave load rebuilt it. This index is built once per
   mounted USB on the shell worker and kept per USB; the runtime rebuilds it only when the stick's
   analysis signature (xz_shana_signature) changes. It stores the PPTH path hash and the folder name
   relative to PIONEER/USBANLZ, and a hit is confirmed by reading that folder's DAT PPTH.
   Worker thread only; no locking. */
#include <stddef.h>
#include <stdint.h>
#include "ui/layered_wave_source.h"
#include "shell/shell.h"

struct xz_shana;
/* root: the folder that holds PIONEER (e.g. /media/usb1/sda2). Returns 0 and *out (possibly with no
   entries when the stick has no USBANLZ). *files: DAT/EXT files read. */
int xz_shana_build(const char *root, struct xz_shana **out, uint32_t *files);
void xz_shana_free(struct xz_shana *);
const char *xz_shana_root(const struct xz_shana *);
uint32_t xz_shana_count(const struct xz_shana *);
/* track: the path below root, starting "/Contents/". Writes the absolute analysis folder. 0 = found. */
int xz_shana_find(const struct xz_shana *, const char *track, char *dir, size_t capacity);
/* Reads PWV7 from dir/ANLZ0000.2EX, as xz_wave_load_three_band does after its lookup. */
int xz_shana_three_band(const char *dir, struct xz_wave_load *out);
/* Splits an absolute deck path at "/Contents/": root and track (pointing into source). */
int xz_shana_split(const char *source, char *root, size_t capacity, const char **track);

/* The stick's analysis signature: the USBANLZ folder's device and modification time, each group folder's
   modification time, and the stem index's time and size. A changed signature is the only reason to
   rebuild (a history write to export.pdb, or a lookup miss, is not). 0 when the stick has neither. */
uint64_t xz_shana_signature(const char *root);

/* Stems: track -> OverCue bundle folder, from root/CDJMODS/index.json parsed once per stick (the same
   index ui/layered_wave_source.c parses with xz_oc_bundle on every load). A track listed twice with
   two different bundles is left out, as xz_oc_bundle refuses it. */
struct xz_shana_stems;
int xz_shana_stems_build(const char *root, struct xz_shana_stems **out);
void xz_shana_stems_free(struct xz_shana_stems *);
uint32_t xz_shana_stems_count(const struct xz_shana_stems *);
int xz_shana_stems_find(const struct xz_shana_stems *, const char *track, char *folder, size_t capacity);
/* The three stem waveforms (drums, harmonics, vocal: each stem's three bands summed and capped, as
   xz_wave_load_stems does). Reads only each file's PWV7 section. read_ms: per stem file, or NULL. */
int xz_shana_stems_load(const char *folder, struct xz_wave_load *out, uint32_t read_ms[3]);

/* Memory cues and memory loops from dir/ANLZ0000.DAT: the PCOB list of type 0 (memory), each PCPT entry
   {hot_cue 0, type 1 cue / 2 loop, time ms, loop end ms}. Sorted by time, at most XZ_SHELL_MEMORY_CUES.
   Returns the count, or -1 when the file or its cue list cannot be read. */
int xz_shana_memory_cues(const char *dir, struct xz_shell_cue out[XZ_SHELL_MEMORY_CUES]);
#endif
