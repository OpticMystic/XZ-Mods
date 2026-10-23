#ifndef XZ_MOD_UI_MENU_H
#define XZ_MOD_UI_MENU_H
/* The MODS panel's open page as a flat menu, for the MODERN interface's MODS screen
   (shell/shell_settings.h struct xz_shell_menu). The panel keeps its pages, state and actions: this only
   describes the page the panel has open, and runs a row the way the panel's own encoder push runs a
   control (a press and release at that control), so the two interfaces cannot disagree. */
#include "ui.h"
#include "../shell/shell_settings.h"

/* How a menu target runs (struct xz_shell_menu_target.how). */
enum xz_ui_menu_how {
 XZ_UI_MENU_TOUCH,   /* press and release at the control {kind, index, deck} of the current layout */
 XZ_UI_MENU_DIRECT,  /* the action {kind, index, value} at once, phase PRESS */
 XZ_UI_MENU_SLIDE,   /* the same, phase MOVE (a slider) */
 XZ_UI_MENU_CYCLE,   /* the control's own step, repeated until its value is index */
 XZ_UI_MENU_PAGE     /* open page index below this one */
};
#define XZ_UI_MENU_ACTIONS 8
/* The top page the open page lives under, and how many pages below it. */
int xz_ui_menu_root(const struct xz_ui *, int *depth);
/* Opens a top page with no parent page; BACK returns CLOSE to leave MODS. */
void xz_ui_menu_open(struct xz_ui *, enum xz_ui_page top);
/* Describes the open page into out. Returns 0 when u or m is NULL. */
int xz_ui_menu_build(const struct xz_ui *, const struct xz_ui_model *, struct xz_shell_menu *out);
/* Runs a row or choice target; returns the panel actions the runtime applies, as after a touch. */
size_t xz_ui_menu_run(struct xz_ui *, const struct xz_ui_model *, const struct xz_shell_menu_target *, struct xz_ui_action out[XZ_UI_MENU_ACTIONS]);
/* A held pad: down 1 presses (or moves) at x, y (0..1000 across the control), 0 releases. */
size_t xz_ui_menu_pad(struct xz_ui *, const struct xz_ui_model *, const struct xz_shell_menu_target *, int x, int y, int down, struct xz_ui_action out[XZ_UI_ACTIONS]);
/* One page up, as the panel's BACK; a top page returns CLOSE. */
size_t xz_ui_menu_back(struct xz_ui *, struct xz_ui_action out[XZ_UI_ACTIONS]);
#endif
