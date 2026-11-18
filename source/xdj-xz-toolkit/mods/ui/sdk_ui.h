/* SPDX-License-Identifier: MIT */
#ifndef XZ_SDK_UI_H
#define XZ_SDK_UI_H
/* MODS > EXTRAS > APPLICATIONS and the SET UP APPS wizard for the XZ SDK app
 * manager. Layout, touch, rendering and planning read the snapshot that the
 * runtime copied under ui_mutex. Nothing here performs I/O, takes a lock or
 * calls the manager; the runtime submits the planned command itself. */
#include <stddef.h>
#include <stdint.h>
#include "../sdk/xz_sdk_host.h"

/* Mount-relative folders the manager searches. A host header that exports its
 * own defaults wins, so the wizard always names the folders the worker scans. */
#ifdef XZ_SDK_DEFAULT_APPLICATIONS_FOLDER
#define XZ_SDK_UI_APPLICATIONS_FOLDER XZ_SDK_DEFAULT_APPLICATIONS_FOLDER
#else
#define XZ_SDK_UI_APPLICATIONS_FOLDER "VJ.Tools/XZ-SDK/Applications"
#endif
#ifdef XZ_SDK_DEFAULT_AUTOLOAD_FOLDER
#define XZ_SDK_UI_AUTOLOAD_FOLDER XZ_SDK_DEFAULT_AUTOLOAD_FOLDER
#else
#define XZ_SDK_UI_AUTOLOAD_FOLDER "VJ.Tools/XZ-SDK/Autoload"
#endif
#define XZ_SDK_UI_STAGE_ROOT "/dev/shm/xz-sdk"
/* The live test prefers the SDK example (examples/test-card), else the first application. */
#define XZ_SDK_UI_TEST_ID "io.vjtools.test-card"
#define XZ_SDK_UI_ROWS 6

enum xz_sdk_ui_host { XZ_SDK_UI_ABSENT, XZ_SDK_UI_RUNNING };
enum xz_sdk_ui_step { XZ_SDK_UI_DETECT, XZ_SDK_UI_FOLDERS, XZ_SDK_UI_INSTALL, XZ_SDK_UI_VERIFY, XZ_SDK_UI_STEPS };
/* Copied by the runtime on refresh. Rendering and touch never change it. */
struct xz_sdk_ui_model {
 int host;         /* enum xz_sdk_ui_host: xz_sdk_host_start succeeded */
 int fresh;        /* the last trylock read returned this snapshot */
 int foreground;   /* snapshot.foreground: an application child is running */
 int display_busy; /* a game owns the display, so RUN waits for it */
 unsigned roots;   /* mount roots the manager searches */
 int extra_root;   /* the loader USB (XZ_MODS_USB) is searched as well */
 struct xz_sdk_snapshot snapshot;
};

struct xz_ui; struct xz_ui_model; struct xz_ui_widget; struct xz_ui_action; struct xz_ui_scene;
int xz_sdk_ui_page(int page);
int xz_sdk_ui_kind(int kind);
/* Game launches refused while a foreground app owns the display. */
int xz_sdk_ui_game_launch(int kind);
/* Appends this page's controls; xz_sdk_ui_place assigns their geometry. */
size_t xz_sdk_ui_controls(const struct xz_ui *, const struct xz_ui_model *, struct xz_ui_widget *, size_t n);
/* Runs after xz_ui_place for SDK controls and the EXTRAS tile beside GAMES. */
void xz_sdk_ui_place(const struct xz_ui *, const struct xz_ui_model *, struct xz_ui_widget *, size_t n);
int xz_sdk_ui_selected(const struct xz_ui *, const struct xz_ui_model *, const struct xz_ui_widget *);
/* The page's one accented action, or 0 when the page has none. */
int xz_sdk_ui_primary(const struct xz_ui *, const struct xz_ui_model *, int *kind, int *index);
const char *xz_sdk_ui_footer(const struct xz_ui *, const struct xz_ui_model *);
void xz_sdk_ui_render(const struct xz_ui_scene *);
/* A press on an SDK control: page navigation first, then exactly one action. */
size_t xz_sdk_ui_touch(struct xz_ui *, const struct xz_ui_model *, const struct xz_ui_widget *, struct xz_ui_action *out);

/* What the runtime does with an SDK action. A stale generation never submits. */
struct xz_sdk_ui_plan { int submit; struct xz_sdk_command command; char notice[96]; };
void xz_sdk_ui_plan(const struct xz_sdk_ui_model *, const struct xz_ui_action *, struct xz_sdk_ui_plan *);
/* fresh is NULL when the trylock read failed; the previous snapshot and foreground stay. */
void xz_sdk_ui_model_read(struct xz_sdk_ui_model *, const struct xz_sdk_snapshot *fresh, int display_busy);

/* Display takeover for one foreground session: captured once, restored once. */
struct xz_sdk_ui_session { int active, previous; };
struct xz_sdk_ui_change { int set_takeover, takeover, close_panel; };
struct xz_sdk_ui_change xz_sdk_ui_session_step(struct xz_sdk_ui_session *, int foreground, int takeover);
/* The takeover value to save: the user's own, never the forced session value. */
int xz_sdk_ui_saved_takeover(const struct xz_sdk_ui_session *, int takeover);
/* A profile applied during a session sets the value restored when it ends. */
int xz_sdk_ui_session_profile(struct xz_sdk_ui_session *, int profile_takeover);
/* /media/usb1 and /media/usb2, plus XZ_MODS_USB (the loader USB) when valid and not
 * an exact duplicate. The manager opens every root as a USB root. */
unsigned xz_sdk_ui_roots(const char *usb, const char *roots[XZ_SDK_ROOTS], char *storage, size_t size, int *extra);
#endif
