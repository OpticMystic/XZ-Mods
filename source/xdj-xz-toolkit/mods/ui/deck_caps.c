/* SPDX-License-Identifier: MIT */
/* What works on each logical deck. Decks 1 and 2 are the player's own decks, 3 and 4 the software players
   reached over the loopback cable. The MODS menu's WORKS ON chips and the 4 DECKS grid are built from
   xz_deck_caps only, so the menu never claims a feature works on a deck where it does not. Pure: reads the
   UI model, no device calls. */
#include "ui.h"

/* MERGE SWITCH. What the software players on decks 3 and 4 do in this build beyond what is listed in
   deck34_now. The parity work (branch claude/xz-parity-fixes-20261026) brings custom beat jump sizes, GATE CUE
   and SMART CUE to decks 3 and 4; when it merges, set this to
   XZ_DECK_CAN_BEAT_JUMP|XZ_DECK_CAN_GATE_CUE|XZ_DECK_CAN_SMART_CUE and the grid, the chips and the reasons follow. */
#define XZ_DECK34_PARITY 0u

/* Decks 3 and 4 today: the playing key and every display setting. */
static const unsigned deck34_now=XZ_DECK_CAN_PLAYING_KEY|XZ_DECK_CAN_DISPLAY;

static int native_ready(const struct xz_ui_model *m, uint32_t bit) {
 return ((m->deck[0].ready|m->deck[1].ready)&bit)!=0;
}
unsigned xz_deck_caps(const struct xz_ui_model *m, int deck) {
 if(!m||deck<0||deck>3)return 0;
 if(deck>=2){
  /* A loader without the four-deck connector has no decks 3 and 4 at all. */
  if(m->route.state==XZ_UI_ROUTE_NO_ENGINE)return 0;
  return deck34_now|XZ_DECK34_PARITY;
 }
 unsigned caps=XZ_DECK_CAN_BEAT_JUMP|XZ_DECK_CAN_GATE_CUE|XZ_DECK_CAN_SMART_CUE|XZ_DECK_CAN_PLAYING_KEY|XZ_DECK_CAN_DISPLAY;
 /* Engines this loader may lack: each lights only once the runtime reports it. */
 if(native_ready(m,XZ_UI_KEY|XZ_UI_KEYSYNC))caps|=XZ_DECK_CAN_KEY_SYNC;
 if(xz_ui_usb_engine(m))caps|=XZ_DECK_CAN_STEMS;
 if(m->deck[deck].ready&XZ_UI_PREVIEW)caps|=XZ_DECK_CAN_CUE_PREVIEW;
 if(m->deck[deck].ready&XZ_UI_SAMPLE)caps|=XZ_DECK_CAN_XPAD;
 if(m->deck[deck].ready&XZ_UI_GROOVE)caps|=XZ_DECK_CAN_GROOVE;
 if(m->deck[0].ram.state!=XZ_UI_RAM_UNAVAILABLE||m->deck[1].ram.state!=XZ_UI_RAM_UNAVAILABLE)caps|=XZ_DECK_CAN_RAM;
 return caps;
}
/* One short sentence (it fits one line of the CLASSIC dock): why feature is not on logical_deck now. */
const char *xz_deck_cap_reason(const struct xz_ui_model *m, int deck, unsigned feature) {
 if(!m||deck<0||deck>3||(xz_deck_caps(m,deck)&feature))return NULL;
 if(deck>=2){
  if(m->route.state==XZ_UI_ROUTE_NO_ENGINE)return "Decks 3 and 4 need a newer USB loader.";
  switch(feature){
  case XZ_DECK_CAN_BEAT_JUMP:return "Decks 3 and 4 jump 1, 2, 4, 8 beats only.";
  case XZ_DECK_CAN_KEY_SYNC:return "Decks 3 and 4 cannot shift key yet.";
  case XZ_DECK_CAN_STEMS:case XZ_DECK_CAN_GROOVE:return "Decks 3 and 4: no stems in this version.";
  case XZ_DECK_CAN_XPAD:return "The sampler plays on decks 1 and 2.";
  case XZ_DECK_CAN_RAM:return "Decks 3 and 4 always read the USB.";
  default:return "Not on decks 3 and 4 in this version.";
  }
 }
 switch(feature){
 case XZ_DECK_CAN_STEMS:return "This USB loader has no stem engine.";
 case XZ_DECK_CAN_KEY_SYNC:return "The key engine has not reported yet.";
 case XZ_DECK_CAN_CUE_PREVIEW:case XZ_DECK_CAN_XPAD:case XZ_DECK_CAN_GROOVE:return "Not in this version of XZ Mods.";
 case XZ_DECK_CAN_RAM:return "The RAM engine has not reported yet.";
 default:return "Not on this deck in this version.";
 }
}
