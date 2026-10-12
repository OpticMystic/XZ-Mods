/* SPDX-License-Identifier: MIT */
#ifndef XZ_KEY_CATALOG_H
#define XZ_KEY_CATALOG_H
#include "music.h"
struct xz_key_entry {unsigned char title[32];struct xz_music_key key;uint32_t id,artist_id,tempo;};
struct xz_artist_entry {uint32_t id;uint16_t name[64];};
struct xz_key_catalog {struct xz_key_entry *entries;size_t count,capacity;struct xz_artist_entry *artists;size_t artist_count,artist_capacity;};
int xz_key_catalog_load(const char *path,struct xz_key_catalog *);
void xz_key_catalog_free(struct xz_key_catalog *);
/* 1 known key, 0 no match, -1 unknown/ambiguous title. */
int xz_key_catalog_lookup(const struct xz_key_catalog *,const uint16_t *,size_t,struct xz_music_key *);
int xz_key_catalog_track(const struct xz_key_catalog *,const uint16_t *,size_t,struct xz_key_entry *);
int xz_key_catalog_artist(const struct xz_key_catalog *,uint32_t,uint16_t out[64]);
#endif
