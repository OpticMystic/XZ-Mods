/* SPDX-License-Identifier: MIT */
#ifndef XZ_MENU_KEY_H
#define XZ_MENU_KEY_H
/* What a short press of the physical MENU key does, in both interfaces (owner rule 2026-10-26): stock first.
   MENU keeps its stock job wherever the player's own firmware uses it, and opens or closes MODS only where a
   short MENU has no stock job. A held MENU always goes to the player's own UTILITY. One table, read by the
   MODERN shell (shell/shell.c) and the CLASSIC key hook (ui_runtime.c); each says which row it is on.
   Pure: no IO. */

/* Where the press lands: the screen or state, most specific first. */
enum xz_menu_place {
 XZ_MENU_AT_MODS,        /* the MODS menu is open */
 XZ_MENU_AT_GAME,        /* a game or Doom has the screen */
 XZ_MENU_AT_TAKEOVER,    /* the VJ.TOOLS view replaces the play screen */
 XZ_MENU_AT_STOCK_MENU,  /* a menu, popup, panel or dialog is open on top of a screen */
 XZ_MENU_AT_TRACK_LIST,  /* a track list: browse, playlists, search, history */
 XZ_MENU_AT_TAG_LIST,    /* TAG LIST */
 XZ_MENU_AT_GRID_ADJUST, /* the play screen in GRID ADJUST mode */
 XZ_MENU_AT_PLAY,        /* the play screen, nothing open over it */
 XZ_MENU_AT_SOURCE,      /* the media source screen */
 XZ_MENU_AT_INFO,        /* the track INFO screen */
 XZ_MENU_AT_SHORTCUT,    /* the SHORTCUT screen */
 XZ_MENU_AT_UTILITY,     /* the player's own UTILITY */
 XZ_MENU_AT_OTHER,       /* any screen this table does not name */
 XZ_MENU_PLACES
};
enum xz_menu_job {
 XZ_MENU_JOB_STOCK,      /* the press does the screen's own job (the stock firmware's, or MODERN's copy of it) */
 XZ_MENU_JOB_MODS,       /* the press opens MODS */
 XZ_MENU_JOB_CLOSE_MODS  /* the press closes MODS */
};
struct xz_menu_rule {
 enum xz_menu_job job;
 int unknown;            /* 1: the stock job here is not confirmed on the unit yet (owner to check) */
 const char *what;       /* the press's job here, one short phrase */
 const char *basis;      /* why: the XZ manual (DRI1625B) page or the owner's rule */
};
/* The row for place; any value outside the enum reads as XZ_MENU_AT_OTHER. */
const struct xz_menu_rule *xz_menu_rule(int place);
#endif
