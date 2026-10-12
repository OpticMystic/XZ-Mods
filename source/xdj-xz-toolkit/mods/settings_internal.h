/* SPDX-License-Identifier: MIT */
#ifndef XZ_SETTINGS_INTERNAL_H
#define XZ_SETTINGS_INTERNAL_H
#include "settings.h"

enum xz_internal_settings_result {
    XZ_INTERNAL_SETTINGS_OK = 0,
    XZ_INTERNAL_SETTINGS_MISSING = 1,
    XZ_INTERNAL_SETTINGS_UNAVAILABLE = -1,
    XZ_INTERNAL_SETTINGS_CORRUPT = -2,
    XZ_INTERNAL_SETTINGS_LOW_SPACE = -3,
    XZ_INTERNAL_SETTINGS_IO = -4
};

/* Worker-only I/O. Production writes only below the mounted internal settings
 * volume, never the stock XdjSettings.dat file or a raw flash device. */
/* *present (optional) receives the record's field count; see xz_settings_overlay. */
int xz_settings_internal_load(struct xz_settings *out, unsigned *present);
int xz_settings_internal_save(const struct xz_settings *settings);

#ifdef XZ_SETTINGS_INTERNAL_TEST
void xz_settings_internal_test_root(const char *root);
void xz_settings_internal_test_low_space(int enabled);
#endif
#endif
