#ifndef XZ_CLASSIC_LOAD_H
#define XZ_CLASSIC_LOAD_H
/* LOAD from the classic browser into DECK 3 / DECK 4.

   The stock browser never builds a music_id for LOAD. UiKey_LoadCmn -> BrowseLoadItemEnter posts 0x711
   {row, deck} to the Total task, which commits the native deck (load state 7, wave clear, current track
   stopped) before it asks the DB client for the row's track (0x3fc). The DB answers 0x13a1 with the
   music_id. So the divert takes the 0x711 for an armed side before stock commits, asks the DB the same
   question stock would have asked, and takes the 0x13a1 answer: the native deck never sees either. */
#include <stdint.h>
#include "extra_decks.h"

#define XZ_CLASSIC_LOAD_MS 1000u
#define XZ_CL_MSG_LOAD_ROW 0x711u     /* UI -> Total: LOAD row of the native list */
#define XZ_CL_MSG_ROW_TRACK 0x13a1u   /* DB client -> Total: that row's music_id */

enum xz_cl_phase { XZ_CL_IDLE, XZ_CL_ARMED, XZ_CL_ASKED };
struct xz_classic_load { struct { enum xz_cl_phase phase; uint32_t until_ms; } side[2]; };

/* One Total-task message as the hook read it. 0x711 fills row and title_flag; 0x13a1 fills id_hi
   (byte 0 node, byte 1 device kind, byte 2 browse kind) and id_lo (the track id). */
struct xz_cl_message { uint32_t id; int deck; uint32_t row, title_flag, id_hi, id_lo; };

enum xz_cl_verdict {
 XZ_CL_PASS,     /* stock handles the message */
 XZ_CL_ASK,      /* stock must not see it; ask the DB for the row on behalf of the side */
 XZ_CL_DELIVER,  /* stock must not see it; *load is the DECK 3/4 LOAD */
 XZ_CL_REFUSE    /* stock must not see it; the track is not on a local USB stick */
};

void xz_classic_load_arm(struct xz_classic_load *, int side, uint32_t now_ms);
void xz_classic_load_cancel(struct xz_classic_load *, int side);
enum xz_cl_verdict xz_classic_load_message(struct xz_classic_load *, const struct xz_cl_message *,
                                           uint32_t now_ms, struct xz_extra_deck_event *load);
/* Device kind 2 (USB1) -> source 1, 3 (USB2) -> source 2, a remote node or anything else -> 0. */
int xz_classic_load_source(uint32_t id_hi);

/* Installs the Total-task hook after checking its guard bytes. 0 on success. */
int xz_classic_load_start(void);
/* Unit B calls this when LOAD is pressed on a switched side in the classic interface. 1 when the next
   browser LOAD for native deck side (0 left, 1 right) within XZ_CLASSIC_LOAD_MS goes to DECK 3 / 4. */
int xz_classic_load_divert_arm(int side);
#endif
