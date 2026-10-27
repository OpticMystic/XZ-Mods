/* SPDX-License-Identifier: MIT */
#ifndef XZ_UI_SUPPORT_H
#define XZ_UI_SUPPORT_H
#include "ui_skin.h"

enum xz_support_step {
    XZ_SUPPORT_STEP_PREPARE = 0,
    XZ_SUPPORT_STEP_PREVIEW = 1,
    XZ_SUPPORT_STEP_RECEIPT = 2,
    XZ_SUPPORT_STEPS = 3
};

int xz_ui_support_page(int page);
int xz_ui_support_kind(int kind);
int xz_ui_support_consented(const struct xz_ui *u, const struct xz_ui_model *m);
size_t xz_ui_support_controls(const struct xz_ui *u, const struct xz_ui_model *m, struct xz_ui_widget *w, size_t n);
void xz_ui_support_place(const struct xz_ui *u, const struct xz_ui_model *m, struct xz_ui_widget *w, size_t n);
int xz_ui_support_selected(const struct xz_ui *u, const struct xz_ui_model *m, const struct xz_ui_widget *w);
void xz_ui_support_render(const struct xz_ui_scene *s);
void xz_ui_support_render_qr_box(struct xz_canvas c, int x, int y, int box_side, const char *url);
size_t xz_ui_support_action(struct xz_ui *u, const struct xz_ui_model *m, int kind, int index, struct xz_ui_action *out);
const char *xz_ui_support_help(int kind);
const char *xz_ui_support_title(int page);
int xz_ui_support_take_request(struct xz_ui *u, const struct xz_ui_model *m, const struct xz_ui_action *a, struct xz_support_request_args *out);
#endif
