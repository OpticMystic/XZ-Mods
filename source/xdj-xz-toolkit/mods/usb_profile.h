/* SPDX-License-Identifier: MIT */
#ifndef XZ_USB_PROFILE_H
#define XZ_USB_PROFILE_H
#include "settings.h"
#include <stdint.h>
struct xz_usb_profile {
    char root[1024];
    unsigned long mount_id;
    uint64_t generation;
};
/* Worker-only. A changed volume always receives its own settings or defaults. */
/* *present (optional) receives the record's field count; see xz_settings_overlay. */
int xz_usb_profile_load(struct xz_usb_profile *, const char *root,
    unsigned long mount_id, struct xz_settings *, unsigned *present);
int xz_usb_profile_save(const struct xz_usb_profile *, uint64_t generation,
    const struct xz_settings *);
void xz_usb_profile_removed(struct xz_usb_profile *);
#endif
