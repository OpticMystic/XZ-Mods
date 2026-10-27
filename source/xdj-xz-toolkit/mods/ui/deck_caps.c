/* SPDX-License-Identifier: MIT */
/* What works on each logical deck. Decks 1 and 2 are the player's own decks, 3 and 4 the software players
   reached over the loopback cable. The MODS menu's WORKS ON chips and the 4 DECKS grid are built from
   xz_deck_caps only, so the menu never claims a feature works on a deck where it does not. A bit says the
   feature works on that deck whenever the deck plays: whether decks 3 and 4 are connected right now is the
   4 DECKS page's to say, not every setting's. Pure: reads the UI model, no device calls. Each bit's evidence
   is in merged-evidence/REPORT.md (capability table). */
#include "ui.h"

/* What the software players on decks 3 and 4 run beyond the display: the shared beat jump resolver with the
   saved sizes and both pages, and the Gate Cue and Smart Cue policies from the same settings snapshot
   (extra_player.c, ui/beat_jump.c; parity-evidence/REPORT.md fixes 1 and 3). */
#define XZ_DECK34_PARITY (XZ_DECK_CAN_BEAT_JUMP|XZ_DECK_CAN_GATE_CUE|XZ_DECK_CAN_SMART_CUE)

static const unsigned deck34_now=XZ_DECK_CAN_DISPLAY|XZ_DECK_CAN_PLAYING_KEY|XZ_DECK_CAN_KEY_SYNC;

/* Engines this runtime runs. A ready bit only lights a feature the runtime can also carry out: the menu audit
   (audit/MENU-AUDIT.md, highest-impact defect 5) found no hot cue preview, X-PAD sampler or groove pad engine on
   any deck. refresh_local never sets PREVIEW, SAMPLE, GROOVE or HOTCUE, and the dispatcher has no handler for
   their actions. Add a feature's bit here only with the runtime handler that makes it work. */
#define XZ_DECK_ENGINES_RUN 0u /* of XZ_DECK_CAN_CUE_PREVIEW | XZ_DECK_CAN_XPAD | XZ_DECK_CAN_GROOVE */

static unsigned engine(const struct xz_ui_model *m, int deck, uint32_t ready_bit, unsigned feature) {
 return (m->deck[deck].ready&ready_bit)&&(XZ_DECK_ENGINES_RUN&feature)?feature:0u;
}
unsigned xz_deck_caps(const struct xz_ui_model *m, int deck) {
 if(!m||deck<0||deck>3)return 0;
 /* Decks 3 and 4 have these whenever they play, connected now or not. */
 if(deck>=2)return deck34_now|XZ_DECK34_PARITY;
 /* Native beat jump interception and the display settings are part of the runtime itself. */
 unsigned caps=XZ_DECK_CAN_BEAT_JUMP|XZ_DECK_CAN_PLAYING_KEY|XZ_DECK_CAN_DISPLAY;
 /* The rest light only once the runtime reports them (refresh_local's ready bits and engine states). */
 if(m->deck[deck].ready&XZ_UI_GATE)caps|=XZ_DECK_CAN_GATE_CUE;
 if(m->deck[deck].ready&XZ_UI_SMART)caps|=XZ_DECK_CAN_SMART_CUE;
 if((m->deck[deck].ready&(XZ_UI_KEY|XZ_UI_KEYSYNC)))caps|=XZ_DECK_CAN_KEY_SYNC;
 if(xz_ui_usb_engine(m))caps|=XZ_DECK_CAN_STEMS;
 caps|=engine(m,deck,XZ_UI_PREVIEW,XZ_DECK_CAN_CUE_PREVIEW)|engine(m,deck,XZ_UI_SAMPLE,XZ_DECK_CAN_XPAD)|
  engine(m,deck,XZ_UI_GROOVE,XZ_DECK_CAN_GROOVE);
 if(m->deck[0].ram.state!=XZ_UI_RAM_UNAVAILABLE||m->deck[1].ram.state!=XZ_UI_RAM_UNAVAILABLE)caps|=XZ_DECK_CAN_RAM;
 return caps;
}
/* 1 when no deck can have feature in this build, whatever the decks report: say so once, hide its controls. */
int xz_deck_cap_absent(unsigned feature) {
 return (feature&(XZ_DECK_CAN_CUE_PREVIEW|XZ_DECK_CAN_XPAD|XZ_DECK_CAN_GROOVE)&~XZ_DECK_ENGINES_RUN)!=0;
}
/* One short sentence (it fits one line of the CLASSIC dock): why feature is not on logical_deck now. */
const char *xz_deck_cap_reason(const struct xz_ui_model *m, int deck, unsigned feature) {
 if(!m||deck<0||deck>3||(xz_deck_caps(m,deck)&feature))return NULL;
 if(xz_deck_cap_absent(feature))return "Not in this version, on any deck.";
 if(deck>=2){
  switch(feature){
  case XZ_DECK_CAN_STEMS:return "Decks 3 and 4 have no stems.";
  case XZ_DECK_CAN_RAM:return "Decks 3 and 4 always read the USB.";
  default:return "Not on decks 3 and 4 in this version.";
  }
 }
 switch(feature){
 case XZ_DECK_CAN_GATE_CUE:case XZ_DECK_CAN_SMART_CUE:return "The cue engine has not reported on this deck.";
 case XZ_DECK_CAN_STEMS:return "This USB loader has no stem engine.";
 case XZ_DECK_CAN_KEY_SYNC:return "The key engine has not reported yet.";
 case XZ_DECK_CAN_RAM:return "The RAM engine has not reported yet.";
 default:return "Not on this deck in this version.";
 }
}
