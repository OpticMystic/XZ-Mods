/* SPDX-License-Identifier: MIT */
#ifndef XZ_DIARY_RUNTIME_H
#define XZ_DIARY_RUNTIME_H
#include "diary.h"
#include "../settings.h"
struct xz_diary_view {
 struct xz_settings settings;
 unsigned notice, usb_error, error[4], ram_state[4], ram_reason[4];
 unsigned deck_state[4],sync_state[4];
 int have_settings,have_decks;
};
/* Worker calls these try-lock snapshots. They copy no media strings or identities. */
int xz_ui_diary_snapshot(struct xz_diary_view *);
int xz_shell_diary_snapshot(struct xz_diary_view *);
#endif
