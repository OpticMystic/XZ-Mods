/* SPDX-License-Identifier: MIT */
/* Host stand-in for xz_recorder_state (recorder/mods/recorder_state.c on the unit): previews and tests set
   the state the runtime would read. Never linked into the player. */
#include "recorder_state_stub.h"
#include <string.h>

static struct xz_recorder_state stub;

void xz_recorder_state_stub_set(const struct xz_recorder_state *s) {
    if (s) stub = *s; else memset(&stub, 0, sizeof stub);
}
void xz_recorder_state_stub_case(enum xz_recorder_stub_case which) {
    struct xz_recorder_state s = {0};
    switch (which) {
    case XZ_RECORDER_STUB_UNKNOWN: break;
    case XZ_RECORDER_STUB_IDLE: s.known = 1; break;
    case XZ_RECORDER_STUB_JUST_STARTED:
        s.known = 1; s.recording = 1; s.elapsed_ms = 3000; s.generation = 7;
        strcpy(s.file, "/PIONEER REC/REC007.WAV");
        break;
    case XZ_RECORDER_STUB_LONG:
        s.known = 1; s.recording = 1; s.elapsed_ms = ((1u * 60u + 12u) * 60u + 40u) * 1000u; s.generation = 7;
        strcpy(s.file, "/PIONEER REC/REC007.WAV");
        break;
    }
    stub = s;
}
/* As the seam (recorder/SEAM.md): 0 when out->known is 1, -1 otherwise. */
int xz_recorder_state(struct xz_recorder_state *out) {
    if (!out) return -1;
    *out = stub;
    return stub.known ? 0 : -1;
}
