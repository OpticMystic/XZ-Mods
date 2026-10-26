/* SPDX-License-Identifier: MIT */
#ifndef XZ_UI_PICTURE_DATA_H
#define XZ_UI_PICTURE_DATA_H
/* What the MODS menu's pictures show, as plain values. The CLASSIC panel fills them from its model and draws
   them itself; the MODERN menu carries them to the shell inside struct xz_shell_menu (no pointers), where
   shell_menu.c draws them with the same code (pictures.h). */
#include <stdint.h>

/* PLAY > BEAT JUMP PADS */
struct xz_pic_jump {
 int sizes[8], enabled, edit_page, focus_pair, adjusting;
 int deck_page[4];      /* the page each deck is on now; -1 unknown */
 int deck_live[4];      /* the deck exists now (decks 3 and 4 over the cable) */
};
/* STEMS > STEM PADS */
struct xz_pic_stem_pads {
 int mode, bank, order, lights, four, jump_wins;
 uint32_t rgb[4];       /* drums, harmonics, vocals, bass */
 int focus;             /* enum xz_pic_stem_focus, -1 none */
};
enum xz_pic_stem_focus { XZ_PIC_STEM_MODE, XZ_PIC_STEM_BANK, XZ_PIC_STEM_ORDER, XZ_PIC_STEM_LIGHTS };
/* HOME tile pictures */
struct xz_pic_home {
 uint32_t stem_rgb[3], accent;
 int modern;            /* the play screen is MODERN */
 int decks34;           /* decks 3 and 4 are live */
 char letter;           /* the active DJ profile's initial, 0 none */
};
/* STEMS > STEM COLOURS: each stem's nine choices (as they draw now) and the one picked */
struct xz_pic_colours {
 uint32_t rgb[3][9];
 int current[3];        /* drums, harmonics, vocals */
 uint32_t bass;         /* 4-stem bass: follows the theme */
};
/* 4 DECKS: the cable route and what works on each deck */
struct xz_pic_decks {
 int state, fail_part;  /* enum xz_ui_route_state, enum xz_ui_route_part */
 int view;              /* DECK VIEW 0..2 */
 unsigned caps[4];      /* xz_deck_caps per logical deck */
};
#endif
