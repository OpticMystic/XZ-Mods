/* SPDX-License-Identifier: MIT */
#ifndef XZ_UI_NETWORK_H
#define XZ_UI_NETWORK_H
#include "ui_skin.h"
#include "ui_menu.h"
int xz_ui_network_page(int);
int xz_ui_network_kind(int);
size_t xz_ui_network_controls(const struct xz_ui *,const struct xz_ui_model *,struct xz_ui_widget *,size_t);
void xz_ui_network_place(const struct xz_ui *,const struct xz_ui_model *,struct xz_ui_widget *,size_t);
void xz_ui_network_render(const struct xz_ui_scene *);
int xz_ui_network_selected(const struct xz_ui *,const struct xz_ui_widget *);
size_t xz_ui_network_action(struct xz_ui *,const struct xz_ui_model *,int kind,int index,struct xz_ui_action *);
int xz_ui_network_menu(const struct xz_ui *,const struct xz_ui_model *,struct xz_shell_menu *);
const char *xz_ui_network_help(int kind);
const char *xz_ui_network_title(int page);
#endif
