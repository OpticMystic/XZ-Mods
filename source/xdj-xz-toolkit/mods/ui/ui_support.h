/* SPDX-License-Identifier: MIT */
#ifndef XZ_UI_SUPPORT_H
#define XZ_UI_SUPPORT_H
#include "ui_skin.h"

/* REPORT AN ISSUE (XZ-REPORTS.md section 2): what it is about, what happened (optional), send, the result.
   Topic and SEND are three presses: a topic, SKIP, SEND. From the dock's error strip REPORT THIS starts at
   WHAT HAPPENED with SOMETHING ISN'T WORKING chosen, so SKIP and SEND finish it. */
enum xz_ui_support_step {
    XZ_UI_REPORT_TOPIC = 0,
    XZ_UI_REPORT_TEXT = 1,
    XZ_UI_REPORT_SEND = 2,
    XZ_UI_REPORT_RESULT = 3,
    XZ_UI_REPORT_STEPS = 4
};

int xz_ui_support_page(int page);
int xz_ui_support_kind(int kind);
size_t xz_ui_support_controls(const struct xz_ui *u, const struct xz_ui_model *m, struct xz_ui_widget *w, size_t n);
void xz_ui_support_place(const struct xz_ui *u, const struct xz_ui_model *m, struct xz_ui_widget *w, size_t n);
int xz_ui_support_selected(const struct xz_ui *u, const struct xz_ui_model *m, const struct xz_ui_widget *w);
int xz_ui_support_ready(const struct xz_ui *u, const struct xz_ui_model *m, const struct xz_ui_widget *w);
void xz_ui_support_render(const struct xz_ui_scene *s);
void xz_ui_support_render_qr_box(struct xz_canvas c, int x, int y, int box_side, const char *url);
size_t xz_ui_support_action(struct xz_ui *u, const struct xz_ui_model *m, int kind, int index, struct xz_ui_action *out);
/* The dock's text for a control; *title when the label is not the title. */
const char *xz_ui_support_describe(const struct xz_ui *u, const struct xz_ui_model *m, const struct xz_ui_widget *w, const char **title);
const char *xz_ui_support_title(int page);
const char *xz_ui_support_footer(const struct xz_ui *u, const struct xz_ui_model *m);
/* The page just opened from `from` (EXTRAS, UPDATES): a finished report gives way to a new one. */
void xz_ui_support_opened(struct xz_ui *u, const struct xz_ui_model *m, enum xz_ui_page from);
/* BACK: 1 when it went back a step instead of leaving the page. */
int xz_ui_support_back(struct xz_ui *u);
int xz_ui_support_take_request(struct xz_ui *u, const struct xz_ui_model *m, const struct xz_ui_action *a, struct xz_support_request_args *out);
/* The topic's name as the XZ shows it ("SOMETHING ISN'T WORKING"), NOT SURE for -1. */
const char *xz_ui_support_topic_name(int topic);
#endif
