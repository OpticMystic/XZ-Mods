/* SPDX-License-Identifier: MIT */
#include "../writes.h"
#include <assert.h>
#include <stdio.h>
#ifdef NDEBUG
#error "write policy acceptance requires assertions; compile with -UNDEBUG"
#endif
#ifdef _WIN32
static void put(const char *name, const char *value) { _putenv_s(name, value ? value : ""); }
#else
static void put(const char *name, const char *value) { if (value) setenv(name, value, 1); else unsetenv(name); }
#endif
static int with(const char *readonly, const char *boot, const char *observer) {
    put("XZ_MODS_SETTINGS_READONLY", readonly);
    put("XZ_MODS_BOOT_RUNTIME", boot);
    put("XZ_MODS_OBSERVER", observer);
    return xz_mods_may_write();
}
int main(void) {
    assert(with(NULL, NULL, NULL));                       /* development launch */
    assert(with("0", NULL, NULL));                        /* explicit durable launch */
    assert(!with("1", NULL, NULL));                       /* RAM trial writes nothing */
    assert(!with("1", "", NULL));                         /* an empty boot marker is not a loader */
    assert(with("1", "5eb042be:ui", "0"));                /* USB loader: bridge needs READONLY=1, saving still works */
    assert(!with("1", "5eb042be:observer", "1"));         /* observer launch never writes */
    puts("PASS write policy: development, RAM trial, USB loader and observer launches");
    return 0;
}
