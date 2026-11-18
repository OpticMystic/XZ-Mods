/* SPDX-License-Identifier: MIT */
#ifndef XZ_RECORDER_STATE_STUB_H
#define XZ_RECORDER_STATE_STUB_H
#include "recorder_state.h"
/* The four states the previews and tests drive: not recording, recording 0:03, recording 1:12:40
   (past the one-hour mark) and a build that cannot tell. */
enum xz_recorder_stub_case { XZ_RECORDER_STUB_UNKNOWN, XZ_RECORDER_STUB_IDLE, XZ_RECORDER_STUB_JUST_STARTED, XZ_RECORDER_STUB_LONG };
void xz_recorder_state_stub_set(const struct xz_recorder_state *);
void xz_recorder_state_stub_case(enum xz_recorder_stub_case);
#endif
