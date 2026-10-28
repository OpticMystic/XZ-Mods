/* SPDX-License-Identifier: MIT */
#ifndef XZ_HUB_CAUSE_H
#define XZ_HUB_CAUSE_H
/* The player's E-8307 hub warning (stock_dialog.h xz_stock_usb_hub_text) and what MODS does with it.

   The loopback cable presents a hub, and the player raises E-8307 HUBS ARE NOT SUPPORTED when it is plugged in
   and keeps it up while the cable stays in. That is not an error for a DJ using four decks, so when the decks 3
   and 4 route proves the cable (or a music hub the four-deck helper admitted) is there, MODS shows no error:
   no dock strip, no play-screen notice, and CLASSIC paints a calm LOOPBACK CABLE CONNECTED card in the
   player's own window (ui/native_raw_skin.c).

   The cable is proven when the route is READY, or STARTING / WAITING_NATIVE_STOP after the connector has
   published a checked status (revision != 0): those states come only from a helper that found the cable or an
   admitted music hub (extra_route_status.h). NO_ENGINE, NOT_DETECTED, SCANNING, FAILED (an unsupported hub or
   port included) and LOST prove nothing, so the honest warning stays, worded so it does not blame the cable.

   The player raises the window the moment the hub appears, a little before the helper reports the cable, so an
   unexplained warning waits XZ_HUB_SETTLE_MS before MODS says anything. Header only; no state of its own. */
#include <stdint.h>

#define XZ_HUB_SETTLE_MS 3000u

/* state is enum xz_extra_route_state / enum xz_ui_route_state (numbered the same; test_hub_cause.c checks). */
enum { XZ_HUB_ROUTE_STARTING=3, XZ_HUB_ROUTE_WAITING_NATIVE_STOP=4, XZ_HUB_ROUTE_READY=5 };
static inline int xz_hub_route_cable(int state, uint32_t revision) {
 return state==XZ_HUB_ROUTE_READY||((state==XZ_HUB_ROUTE_STARTING||state==XZ_HUB_ROUTE_WAITING_NATIVE_STOP)&&revision!=0);
}

/* NONE: no warning. CABLE: the cable explains it, show no error. SETTLING: unexplained but too new to say
   anything. WARN: unexplained; show the honest warning. */
enum xz_hub_view { XZ_HUB_NONE, XZ_HUB_CABLE, XZ_HUB_SETTLING, XZ_HUB_WARN };
struct xz_hub_watch { const char *text; uint32_t since; };
/* One step per refresh. text is the live warning's firmware text (or NULL), cable xz_hub_route_cable. */
static inline enum xz_hub_view xz_hub_step(struct xz_hub_watch *w, const char *text, int cable, uint32_t now_ms) {
 if(!text||!text[0]){w->text=0;w->since=0;return XZ_HUB_NONE;}
 if(text!=w->text){w->text=text;w->since=now_ms;}
 if(cable)return XZ_HUB_CABLE;
 return now_ms-w->since<XZ_HUB_SETTLE_MS?XZ_HUB_SETTLING:XZ_HUB_WARN;
}
/* The play screen's one notice for an unexplained warning (MODERN). It names both causes, never just one. */
#define XZ_HUB_NOTICE "E-8307 HUBS ARE NOT SUPPORTED: CHECK THE TOP USB PORTS"
#endif
