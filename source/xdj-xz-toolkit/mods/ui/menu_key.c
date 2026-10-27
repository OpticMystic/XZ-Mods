/* SPDX-License-Identifier: MIT */
/* The short MENU table (menu_key.h). Pages are the XZ manual DRI1625B's. */
#include "menu_key.h"

static const struct xz_menu_rule rules[XZ_MENU_PLACES]={
 [XZ_MENU_AT_MODS]={XZ_MENU_JOB_CLOSE_MODS,0,"closes MODS","owner 2026-10-26: a short MENU closes MODS"},
 [XZ_MENU_AT_GAME]={XZ_MENU_JOB_STOCK,0,"the game's own key","games and Doom keep their keys"},
 [XZ_MENU_AT_TAKEOVER]={XZ_MENU_JOB_STOCK,0,"as before","the VJ.TOOLS view keeps its rules"},
 [XZ_MENU_AT_STOCK_MENU]={XZ_MENU_JOB_STOCK,0,"works or closes the open menu","manual: any open menu or dialog"},
 [XZ_MENU_AT_TRACK_LIST]={XZ_MENU_JOB_STOCK,0,"the sort menu or the track menu","manual: sort menu, track menu p.81, history colour"},
 [XZ_MENU_AT_TAG_LIST]={XZ_MENU_JOB_STOCK,0,"LIST MENU","manual: TAG LIST menu"},
 [XZ_MENU_AT_GRID_ADJUST]={XZ_MENU_JOB_STOCK,0,"the beat grid menu","manual: GRID ADJUST mode"},
 [XZ_MENU_AT_PLAY]={XZ_MENU_JOB_MODS,1,"opens MODS","manual says only 'displays the menu screen': owner to confirm on the unit"},
 [XZ_MENU_AT_SOURCE]={XZ_MENU_JOB_MODS,1,"opens MODS","no stock MENU job known here"},
 [XZ_MENU_AT_INFO]={XZ_MENU_JOB_MODS,1,"opens MODS","no stock MENU job known here"},
 [XZ_MENU_AT_SHORTCUT]={XZ_MENU_JOB_MODS,1,"opens MODS","no stock MENU job known here"},
 [XZ_MENU_AT_UTILITY]={XZ_MENU_JOB_STOCK,0,"closes UTILITY","the player's own menu"},
 [XZ_MENU_AT_OTHER]={XZ_MENU_JOB_STOCK,0,"the screen's own job","stock first on every screen not named here"},
};
const struct xz_menu_rule *xz_menu_rule(int place) {
 return &rules[place>=0&&place<XZ_MENU_PLACES?place:XZ_MENU_AT_OTHER];
}
