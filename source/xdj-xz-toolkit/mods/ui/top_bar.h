/* SPDX-License-Identifier: MIT */
#ifndef XZ_UI_TOP_BAR_H
#define XZ_UI_TOP_BAR_H
/* The mod's row across the top of every stock-drawn (CLASSIC) screen, left to right: VJ.TOOLS or EXIT VJ,
   DECKS (deck_view.h), the metrics readout and METRICS (metrics.h), STEMS and MODS.

   Paint: every control paints rows 0..XZ_TOP_BAR_H-1 only, the black band the stock play screen leaves
   above its own furniture. From y 18 down sit the scrolling waveform surface (x 131..666, wave_viewport.h),
   the BEAT FX header (x 690..799, y 18..45) and the DECK 1 tab (x 0..115, drawn from about y 22,
   classic_column.h). The band and the BEAT FX header were measured on the live capture behind the CLASSIC
   previews (E:/Codex/xz-four-deck-20261016/live-screen.png): between the old STEMS and MODS boxes, x 742..743
   is black to y 17 and BEAT FX grey from y 18. The header sprite (969, 120x31) spans y 15..45 (the FLAGSHIP
   art keeps its rows 0..8 black because the old 24-row buttons covered them), so its first three rows show
   black in the band; the themed header artwork keeps them plain too (native_main_theme.h, native_retro_theme.h).

   Touch: a control answers from y 0 down to a depth that depends on what lies under it.
   - VJ.TOOLS stops at y 17: stock registers DECK 1 select at {0,18,115,140} (NATIVE_WAVE_ABI.md).
   - STEMS and MODS reach y 39 on the play screen. Under them is black and the BEAT FX header, a readout
     with no stock touch rect (none registered there; XDJ-XZ manual elements 29-33 are displays). On other
     screens they keep y 0..23.
   - The metrics readout and METRICS keep y 0..23. The waveform under them carries the OUTER stem row, the
     four-deck lanes and the deck 3/4 panel's CLOSE key, which must keep their own touches.
   - DECKS reaches y 39 (deck_view.c). */
enum {
 XZ_TOP_BAR_H=18,          /* painted rows 0..17 */
 XZ_TOP_BAR_TOUCH_H=24,    /* default touch depth, the height the buttons used to paint */
 XZ_TOP_BAR_DEEP_H=40,     /* touch depth over passive stock furniture on the play screen, as DECKS */
 XZ_TOP_BAR_LABEL_Y=4,     /* atlas capitals (9 rows) on rows 4..12, as DECKS */
 XZ_TOP_BAR_PIXEL_Y=5,     /* the 5x7 pixel face on rows 5..11 */
 XZ_TOP_BAR_MARK_Y=14,     /* the STEMS layout marks, rows 14..15 under the label */
 XZ_TOP_VJ_X=0, XZ_TOP_VJ_W=112,
 XZ_TOP_STEMS_X=674, XZ_TOP_STEMS_W=68,
 XZ_TOP_MODS_X=744, XZ_TOP_MODS_W=56
};
#endif
