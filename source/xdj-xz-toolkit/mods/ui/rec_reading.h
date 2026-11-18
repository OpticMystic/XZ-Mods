/* SPDX-License-Identifier: MIT */
#ifndef XZ_UI_REC_READING_H
#define XZ_UI_REC_READING_H
/* One reading of the unit's recorder (recorder_state.h), as the runtime hands it to the renderers
   (rec_mark.h). Plain data, so the shell and MODS models can carry it without the drawing headers.
   read_at_ms is in the renderer's clock domain (shell model clock_ms, MODS model clock_ms). A zeroed
   reading is "not known" and draws nothing. elapsed_known 0: no clock is shown (the seam gave no
   elapsed time and the mods did not see the recording start). */
#include <stdint.h>
struct xz_rec_reading {
 uint8_t known, recording, elapsed_known;
 uint32_t elapsed_ms, read_at_ms, generation;
};
#endif
