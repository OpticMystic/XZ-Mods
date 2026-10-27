/* SPDX-License-Identifier: MIT */
#ifndef XZ_KEY_VIEW_H
#define XZ_KEY_VIEW_H
#include "../extra_decks.h"
#include <stdio.h>
static inline const char *xz_extra_key_text(const struct xz_extra_deck *d){
 if(!(d->known&XZ_EXTRA_KNOWN(XZ_EXTRA_F_KEY_STATE)))return d->key;
 return (d->known&XZ_EXTRA_KNOWN(XZ_EXTRA_F_KEY_STATE))&&d->key_feedback&&d->key_state.playing_valid&&d->playing_key[0]?
  d->playing_key:d->original_display[0]?d->original_display:d->key;
}
static inline void xz_extra_key_label(const struct xz_extra_deck *d,char *out,size_t size){
 const char *key=xz_extra_key_text(d);
 if((d->known&XZ_EXTRA_KNOWN(XZ_EXTRA_F_KEY_STATE))&&d->key_feedback&&d->key_state.playing_valid&&d->key_state.playing.cents)
  snprintf(out,size,"%s %+dc",key,d->key_state.playing.cents);
 else snprintf(out,size,"%s",key[0]?key:"--");
}
static inline uint32_t xz_extra_key_ink(const struct xz_extra_deck *d,uint32_t plain){
 return (d->known&XZ_EXTRA_KNOWN(XZ_EXTRA_F_KEY_STATE))&&d->key_feedback&&d->key_state.playing_valid&&d->key_color?d->key_color:plain;
}
#endif
