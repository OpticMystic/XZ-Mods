/* SPDX-License-Identifier: MIT */
#ifndef XZ_UI_NETWORK_H
#define XZ_UI_NETWORK_H
#include "ui_skin.h"
int xz_ui_network_page(int);
int xz_ui_network_kind(int);
size_t xz_ui_network_controls(const struct xz_ui *,const struct xz_ui_model *,struct xz_ui_widget *,size_t);
void xz_ui_network_place(const struct xz_ui *,const struct xz_ui_model *,struct xz_ui_widget *,size_t);
void xz_ui_network_render(const struct xz_ui_scene *);
int xz_ui_network_selected(const struct xz_ui *,const struct xz_ui_widget *);
size_t xz_ui_network_action(struct xz_ui *,const struct xz_ui_model *,int kind,int index,struct xz_ui_action *);
const char *xz_ui_network_help(int kind);
/* The status line under PRO DJ LINK DECKS 3/4 for one status copy. */
const char *xz_ui_djlink_line(const struct xz_djlink_status *,char *out,size_t size);
/* The status line beside ABLETON LINK for one status copy. */
const char *xz_ui_ablink_line(const struct xz_link_status *,char *out,size_t size);
/* NETWORK page bands, top to bottom (default view; SETUP and EDIT draw their own). test_network_ui.c checks that
   no band overlaps another or a control. Text bands are XZ_UI_NET_TEXT_H tall. */
#define XZ_UI_NET_ADAPTER_Y 110
#define XZ_UI_NET_ADAPTER_STEP 22
#define XZ_UI_NET_ADAPTER_LINES 4
#define XZ_UI_NET_NOTE_Y 198
#define XZ_UI_NET_NOTE_STEP 16
#define XZ_UI_NET_DJLINK_Y 234
#define XZ_UI_NET_ABLETON_Y 276
#define XZ_UI_NET_ROW_X 136
#define XZ_UI_NET_ROW_W 320
#define XZ_UI_NET_ROW_H 36
#define XZ_UI_NET_STATUS_X 472
#define XZ_UI_NET_STATUS_CHARS 52
#define XZ_UI_NET_TEXT_H 12
#define XZ_UI_NET_GRID_Y 320
const char *xz_ui_network_title(int page);
/* Pure UI publication: a review counts as seen only after the panel painted it (xz_ui_render). */
void xz_ui_network_invalidate(struct xz_ui *);
uint64_t xz_ui_network_review_token(struct xz_ui *,const struct xz_ui_model *);
void xz_ui_network_painted(struct xz_ui *,const struct xz_ui_model *,uint64_t);
int xz_ui_network_take_apply(struct xz_ui *,const struct xz_ui_model *,const struct xz_ui_action *,struct xz_net_trial *);
#endif
