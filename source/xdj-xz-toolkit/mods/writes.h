/* SPDX-License-Identifier: MIT */
#ifndef XZ_MODS_WRITES_H
#define XZ_MODS_WRITES_H
#include <stdlib.h>
#include <string.h>
/* Whether this runtime may save: XZ Mods settings to USB and the XZ, stock UTILITY changes, deck 3/4 cue edits
   and the USB diary. XZ_MODS_SETTINGS_READONLY=1 alone means a RAM trial, which writes nothing. The USB loader
   also sets it, because the four-deck bridge requires it before attaching, so a loader launch is recognized by
   XZ_MODS_BOOT_RUNTIME, which only the loader's orchestrator sets. Observer launches never write. */
static inline int xz_mods_env_is(const char *name, const char *value) {
    const char *v = getenv(name);
    return v && !strcmp(v, value);
}
static inline int xz_mods_may_write(void) {
    if (xz_mods_env_is("XZ_MODS_OBSERVER", "1")) return 0;
    if (!xz_mods_env_is("XZ_MODS_SETTINGS_READONLY", "1")) return 1;
    const char *boot = getenv("XZ_MODS_BOOT_RUNTIME");
    return boot && boot[0] && !xz_mods_env_is("XZ_MODS_OBSERVER", "1");
}
#endif
