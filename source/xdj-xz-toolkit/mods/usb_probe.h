/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifndef XZ_USB_PROBE_H
#define XZ_USB_PROBE_H
/* Answers that need the file system, for code on the drawing path.
 *
 * The music stick is mounted through a user-space file-system driver. Any call on it, even access(), waits for
 * that driver. While a track loads the wait reached hundreds of milliseconds, and while a stick is being
 * removed it has no bound, so the display thread froze with the interface lock held. xz_probe_cached() returns
 * the last result of a check that a background worker repeats about every two seconds. It never blocks and
 * returns 0 until the first evaluation. Up to eight distinct checks.
 * Weak so a unit test that links one source file evaluates the check directly. */
typedef int (*xz_probe_check)(void);
int xz_probe_cached(xz_probe_check check) __attribute__((weak));
#define XZ_PROBE_CACHED(check) (xz_probe_cached ? xz_probe_cached(check) : (check)())
#endif
