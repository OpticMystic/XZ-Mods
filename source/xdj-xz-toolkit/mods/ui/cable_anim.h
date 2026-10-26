/* SPDX-License-Identifier: MIT */
#ifndef XZ_CABLE_ANIM_H
#define XZ_CABLE_ANIM_H
/* The loopback-cable explainer: one unit in true perspective, the camera going once round it per loop.
   It opens and closes on the main shot (behind and to the right, above: USB 1 on top, the rear USB-B
   socket and the cable over the rear edge in one view), goes down to the rear socket where the plug is
   pulled out and pushed home, up to USB 1 where the plug is lifted out and dropped back in, out to the
   front overview, and back. The cable stays connected and glowing throughout.

   Drawn natively each frame from a compact bake (cable_anim_data.h, built by build_cable_anim.py from the
   Blender scene in cable-anim/): the unit's feature lines in 3D, which parts of each are visible in every
   60 fps frame (hidden lines removed in Blender by ray casting), the filled faces, and the camera and plug
   poses. The renderer projects the lines itself, so the same picture fits every box and any frame rate;
   poses interpolate between baked frames. Lines are anti-aliased by coverage (one layer per pixel, the
   strongest wins), then tinted per layer.

   Pure: no IO, no allocation, no clock of its own. It writes only the picture memory it is given. */
#include "ui_skin.h"

/* USB 1 is where the picture plugs the cable (most DJs use it); USB 2 works as well and carries a softer
   accent of its own. */
enum xz_cable_layer { XZ_CABLE_DEVICE, XZ_CABLE_USB1, XZ_CABLE_SOCKET, XZ_CABLE_SELECTOR, XZ_CABLE_CABLE, XZ_CABLE_USB2, XZ_CABLE_LAYERS };
enum xz_cable_mark { XZ_CABLE_MARK_NONE, XZ_CABLE_MARK_USB1, XZ_CABLE_MARK_SOCKET, XZ_CABLE_MARK_CABLE, XZ_CABLE_MARK_SELECTOR };

/* How a placement colours the picture: the fills, one ink per layer (the route state's tint), the
   labels' ink (0: no labels) in the canvas theme's face, and the failure mark. */
struct xz_cable_look {
 uint32_t bg, body, hole;
 uint32_t ink[XZ_CABLE_LAYERS];
 uint32_t label;
 int theme;
 int mark;                      /* enum xz_cable_mark */
 uint32_t mark_fill, mark_ink;
};
/* The picture, owned by the caller: w x h RGB565 pixels and a w x h coverage plane (scratch). */
struct xz_cable_picture { uint16_t *px; uint8_t *cov; int w, h; };

/* The loop's length, and the still a resting card shows: the main shot, or the overview when the
   failing part is the CH3/CH4 input selectors (they are out of the main shot). */
uint32_t xz_cable_loop_ms(void);
uint32_t xz_cable_rest_ms(int mark);
/* Draws the frame at t_ms (wrapped to the loop) into the picture. signal: XZ_CABLE_SIGNAL_LOOP the loop's
   own travelling glow, XZ_CABLE_SIGNAL_NONE none, else its head in thousandths along the cable (USB 1 = 0,
   rear socket = 1000; it runs on to 1160 so its tail leaves).
   budget: the most pixel operations the frame may spend, 0 = no limit. A frame that would cost more is
   not drawn: the picture keeps what it had and the call returns 0. Otherwise returns the operations spent
   (at least 1). */
uint32_t xz_cable_draw(const struct xz_cable_picture *, uint32_t t_ms, const struct xz_cable_look *, int signal, uint32_t budget);
/* What a route state shows (state and part: enum xz_extra_route_state / _part; the menus' enum
   xz_ui_route_state / _part have the same values): nothing (no engine), the still, the explainer loop
   (not detected, failed, lost: the DJ has a cable to fix), or the still with the signal running along the
   cable (scanning, starting, waiting). */
enum xz_cable_motion { XZ_CABLE_HIDDEN, XZ_CABLE_STILL, XZ_CABLE_LOOP, XZ_CABLE_SIGNAL };
enum xz_cable_motion xz_cable_motion_of(int state);
/* One interface's colours for the picture; xz_cable_look_of tints it for a state: amber not detected,
   blue scanning, white starting and waiting, green ready, red for what failed (with its mark; the other
   accents dim), red cable when lost. */
struct xz_cable_palette { uint32_t bg, body, hole, edge, amber, blue, white, green, red, dim, label, mark_ink; int theme; };
void xz_cable_look_of(struct xz_cable_look *, const struct xz_cable_palette *, int state, int part);
/* The frame a placement shows at now_ms (its presentation clock): moving = the state moves and has not
   rested. step_ms: the frame step (0 = 16 ms, 60 fps; callers pass more while native decks play or frames
   run late). Fills the loop time and signal to draw and returns a key that changes exactly when the
   picture does, so a caller redraws on a change and copies its picture otherwise. */
enum { XZ_CABLE_SIGNAL_LOOP=-1, XZ_CABLE_SIGNAL_NONE=-2 };
uint32_t xz_cable_frame_of(int state, int part, uint32_t now_ms, int moving, uint32_t step_ms, uint32_t *t_ms, int *signal);
/* Copies the picture into the canvas with its top left at (x, y), clipped. */
void xz_cable_blit(struct xz_canvas c, int x, int y, const struct xz_cable_picture *);
/* Where a mark's part is in the picture at t_ms (pixels), 0 when it is behind the camera or outside. */
int xz_cable_anchor(int w, int h, uint32_t t_ms, int mark, int *x, int *y);
#endif
