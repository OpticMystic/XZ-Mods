/* SPDX-License-Identifier: MIT */
#ifndef XZ_KEY_NATIVE_H
#define XZ_KEY_NATIVE_H
#include "music.h"
typedef int (*xz_key_memory_read)(void *, uint32_t, void *, size_t);
int xz_key_native_valid(xz_key_memory_read, void *);
int xz_key_native_read(xz_key_memory_read, void *, unsigned deck, struct xz_key_snapshot *);
int xz_key_native_snapshot(unsigned deck, struct xz_key_snapshot *);
#endif
