/* SPDX-License-Identifier: MIT */
#ifndef XZ_STEM_STORE_REFERENCE_H
#define XZ_STEM_STORE_REFERENCE_H
#include <stddef.h>
#include <stdint.h>
typedef int (*reference_reader)(void *context, unsigned single, int64_t first, size_t frames, int16_t *stereo);
/* A whole copy of three singles built by the pre-segment store: pcm[r][frame * 2 + channel], malloc'ed. 0 on
 * success. */
int reference_store(uint32_t deck_rate, uint32_t stem_rate, uint64_t stem_frames, double offset, reference_reader read,
                    void *context, int16_t *pcm[3], uint64_t *frames);
#endif
