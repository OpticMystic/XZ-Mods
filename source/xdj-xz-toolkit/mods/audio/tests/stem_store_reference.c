/* SPDX-License-Identifier: MIT */
/* The whole-track store as it was before segments, for comparison: verify_stem_pool.py extracts overcue_flat.h of
 * the reference commit into a folder of its own and puts that folder on this file's include path. */
#include "overcue_flat.h"
#include "stem_store_reference.h"

int reference_store(uint32_t deck_rate, uint32_t stem_rate, uint64_t stem_frames, double offset, reference_reader read,
                    void *context, int16_t *pcm[3], uint64_t *frames) {
    struct xz_oc_flat *f = xz_oc_flat_create(deck_rate, stem_rate, stem_frames, offset, 3, read, context);
    if (!f) return -1;
    int step;
    while ((step = xz_oc_flat_step(f, 0)) == 1) {}
    if (step < 0 || !xz_oc_flat_complete(f)) { xz_oc_flat_destroy(f); return -1; }
    *frames = f->frames;
    for (unsigned r = 0; r < 3; r++) {
        pcm[r] = malloc((size_t)f->frames * 2 * sizeof(int16_t));
        if (!pcm[r]) { xz_oc_flat_destroy(f); return -1; }
        memcpy(pcm[r], f->pcm[r], (size_t)f->frames * 2 * sizeof(int16_t));
    }
    xz_oc_flat_destroy(f);
    return 0;
}
