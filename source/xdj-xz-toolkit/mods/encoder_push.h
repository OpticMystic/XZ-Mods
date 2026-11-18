/* SPDX-License-Identifier: MIT */
/* The rotary selector's push as stock's browse key table carries it (firmware 1.26).

   The selector is key 0x4207. BrowseUiIfDpl::onKey (0x2a9d48) turns it into browse key kind 8, and
   UiKey_KeyPush (0xe14c0) keeps one entry per kind in the table at 0x22518e0, 16 bytes each:
   {flags, 0, state, handler}. For kind 8 the handler is UiKey_EncoderPush (0xdec08), which the mods hook.
     press   (op 0): flags = 1; state 0 -> 2, a handler pass. A press while state is not 0 changes nothing
                     and starts no pass (0xe14f4..0xe1500).
     hold    (op 1, 500 ms, KeyInput::getLongPressTime 0x2ee4c0): flags = 1; state != 0 -> 3, a pass.
     release (op 2): flags = 0; state left as it is; a pass while state != 0.
   Ui_EventTask (0x197cd0) runs BrowseKeyProcessing, which calls the handler of every entry whose state is
   not 0. Stock's own handler zeroes the state when it is done (0xdec50..0xdec5c, or all 74 entries), and a
   turn pass zeroes it too (BrowseEncoderRotate 0xe17b0). A hook that takes the push and leaves the state set
   makes the next press start no pass: that push is lost until something turns the selector.

   So the mods do what stock's handler does: when they take a pass, they write the state back. They also
   mark the contact as theirs with state values stock never writes (1: pressed, 4: held). Stock reads the
   state only as zero, at most 2, or above 2, so 1 and 4 keep stock's meaning, and a pass with a marked
   state belongs to the mods even if the screen changed owner in the middle of the contact. */
#ifndef XZ_ENCODER_PUSH_H
#define XZ_ENCODER_PUSH_H
#include <stdint.h>

enum xz_push_phase {
 XZ_PUSH_NONE,       /* a repeated pass of a contact already read, or nothing pressed */
 XZ_PUSH_DOWN,       /* the press */
 XZ_PUSH_HOLD,       /* held 500 ms, still down */
 XZ_PUSH_UP,         /* released before the hold */
 XZ_PUSH_UP_HELD     /* released after the hold */
};
enum { XZ_PUSH_MARK_DOWN=1u, XZ_PUSH_MARK_HELD=4u };
struct xz_push_step { enum xz_push_phase phase; uint32_t state; /* write this back when the pass is taken */ };

/* 1: this contact's press was already taken by the mods. */
static inline int xz_push_marked(uint32_t state) { return state==XZ_PUSH_MARK_DOWN||state==XZ_PUSH_MARK_HELD; }

static inline struct xz_push_step xz_push_read(uint32_t flags, uint32_t state) {
 struct xz_push_step s={XZ_PUSH_NONE,state};
 if(flags==1u){
  if(state==2u){s.phase=XZ_PUSH_DOWN;s.state=XZ_PUSH_MARK_DOWN;}
  else if(state==3u){s.phase=XZ_PUSH_HOLD;s.state=XZ_PUSH_MARK_HELD;}
  /* Stock reaches the handler only through BrowseKeyProcessing, which skips state 0. A caller that is not
     stock (a host test) gets a press, and nothing is marked: a mark would block stock's next press. */
  else if(state==0u)s.phase=XZ_PUSH_DOWN;
  return s;   /* 1 or 4: the same contact passed again; other values: nothing to read */
 }
 if(flags==0u&&state!=0u){
  s.phase=state==3u||state==XZ_PUSH_MARK_HELD?XZ_PUSH_UP_HELD:XZ_PUSH_UP;
  s.state=0;
 }
 return s;
}
#endif
