/* SPDX-License-Identifier: MIT */
#ifndef XZ_L10N_H
#define XZ_L10N_H
struct xz_canvas;
void xz_l10n_frame(unsigned locale);
const char *xz_l10n_authored(const char *authored);
const char *xz_l10n_label(unsigned locale,const char *authored);
const char *xz_l10n_language(unsigned locale);
int xz_l10n_height(const char *label,int scale,int fallback);
int xz_l10n_wrap(struct xz_canvas c,int x,int y,int width,int pitch,int lines,const char *s,unsigned color);
/* -1 ordinary text; -2 height overflow; otherwise required box height. */
int xz_l10n_box_height(const char *s,int width,int pitch);
#endif
