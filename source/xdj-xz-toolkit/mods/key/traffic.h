/* SPDX-License-Identifier: MIT */
#ifndef XZ_KEY_TRAFFIC_H
#define XZ_KEY_TRAFFIC_H
#include "music.h"
/* UI-only stock traffic-light compatible-key set. No track metadata is changed. */
int xz_key_traffic_ids(const struct xz_playing_key *,uint32_t ids[4]);
void xz_key_traffic_master(const struct xz_playing_key *);
int xz_key_traffic_start(void);
#endif
