/* SPDX-License-Identifier: MIT */
#ifndef XZ_KEY_DISPLAY_H
#define XZ_KEY_DISPLAY_H
#include <stdint.h>
#include <stddef.h>
struct xz_ui_model;
struct xz_ui;
void xz_key_display_panel(uint16_t *, size_t stride, const struct xz_ui *, const struct xz_ui_model *);
#endif
