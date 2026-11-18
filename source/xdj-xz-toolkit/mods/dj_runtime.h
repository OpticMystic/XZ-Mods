/* SPDX-License-Identifier: MIT */
#ifndef XZ_DJ_RUNTIME_H
#define XZ_DJ_RUNTIME_H
#include "ui/ui.h"
#include <pthread.h>
/* DJ Profiles on the two USB ports, published into the MODS model.
 * One worker thread does all USB I/O. It rereads a stick when its DJPROFILE
 * folders, DJPROFILE.json or the old store change, and writes only below the
 * stick's DJPROFILE/ (DJ Profile 1.0): DJPROFILE.json when the DJ picks a
 * profile, a profile folder when the DJ saves one. Nothing is stored on the XZ.
 *
 * lock is the UI lock. view is written only while it is held, so the
 * renderer reads it under the same lock as the rest of the model. */
int xz_dj_runtime_start(pthread_mutex_t *lock, struct xz_ui_dj *view);
void xz_dj_runtime_stop(void);
/* Caller holds the UI lock. Reads the USBs again now. */
void xz_dj_runtime_rescan(void);
/* Caller holds the UI lock. Makes view entry `entry` its stick's active profile. */
void xz_dj_runtime_pick(int entry);
/* Caller holds the UI lock. Chooses the profile the header shows: the stick
 * the master deck's track came from, else a stick another deck plays from,
 * else the stick the browser shows, else the first stick with a profile.
 * Each argument is a USB slot 0-1 or -1. */
void xz_dj_runtime_current(int master_usb, int playing_usb, int browse_usb);
/* Caller holds the UI lock. Loads entry's full logo into view->detail*. */
void xz_dj_runtime_detail(int entry);
/* Caller holds the UI lock. Lists the PNG and JPEG files on a USB into view->images. */
void xz_dj_runtime_images(int usb);
/* Caller holds the UI lock. Renders listed image `image` into view->preview_tile. */
void xz_dj_runtime_preview(int image);
/* Caller holds the UI lock. Writes the editor's draft (edit_id, edit_name, edit_tagline,
   edit_logo, edit_usb) to its USB; a new profile becomes active there. */
void xz_dj_runtime_save(const struct xz_ui *edit);
/* USB slot of a mounted track path, or -1. */
int xz_dj_runtime_usb_of(const char *path);
#endif
