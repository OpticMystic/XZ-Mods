/* SPDX-License-Identifier: MIT */
#ifndef XZ_KEY_BROWSER_H
#define XZ_KEY_BROWSER_H
#include "music.h"
int xz_key_browser_start(void);
void xz_key_browser_stop(void);
void xz_key_browser_master(const struct xz_playing_key *,int format);
int xz_key_browser_repaint(void);
void xz_key_browser_scope(int enter,int primary,int format,int usb);
size_t xz_key_browser_prefix(const uint16_t *title,size_t length,uint16_t out[264],uint16_t prefix[8],uint32_t *color);
const char *xz_key_browser_status(void);
/* Browse context for the MODS badge. The stock list hook counts each local USB
   track-list draw; no extra firmware hook and no I/O. */
void xz_key_browser_list_drawn(void);
uint32_t xz_key_browser_list_generation(void);
/* Browse customization. Values match enum xz_browse_column in settings.h. */
enum xz_key_browser_column {XZ_KEY_COLUMN_TITLE,XZ_KEY_COLUMN_ARTIST,XZ_KEY_COLUMN_KEY,XZ_KEY_COLUMN_BPM,XZ_KEY_COLUMN_COUNT};
/* A native track row has two cells: list column 0 (the title cell) and list column 1
   (the stock right cell, chosen by firmware dsqlGetRightTrackKey from the XZ browse
   menu). The three mod columns are drawn in order inside the title cell, in the stock
   font and clip, from the USB export only: title text from firmware, key/artist/BPM
   from the matching export.pdb track. A field the export lacks is left out, never
   guessed. Match colours need the key to lead the row, where its glyphs start at the
   cell origin. */
struct xz_key_browser_fields {
    const uint16_t *title; size_t title_length;
    const char *key;        /* NULL when the export has no key */
    const uint16_t *artist; /* NULL or "" when the export has no artist */
    uint32_t tempo;         /* BPM x 100 as exported; 0 = none */
};
/* Field order: a leading key when key_first, then the columns in slot order. */
static inline int xz_key_browser_order(const int columns[3],int key_first,int order[4]){
    int n=0;if(key_first)order[n++]=XZ_KEY_COLUMN_KEY;
    for(int i=0;i<3;i++)if(!(key_first&&columns[i]==XZ_KEY_COLUMN_KEY))order[n++]=columns[i];
    return n;
}
/* Returns the row length, or 0 when the row would only repeat the stock title.
   *key_lead is the key length when the key starts the row, else 0. */
size_t xz_key_browser_compose(const int columns[3],int key_first,const struct xz_key_browser_fields *,
    uint16_t out[264],size_t *key_lead);
int xz_key_browser_columns_valid(const int columns[3]);
/* Puts value in slot; a slot already holding it takes the old value instead. */
int xz_key_browser_column_choose(int columns[3],int slot,int value);
/* key_format: 0 stock browser setting, 1 classic, 2 Camelot. Invalid columns keep the last valid set. */
void xz_key_browser_configure(const int columns[3],int key_prefix,int traffic,int key_format);
struct xz_key_browser_proof {uint32_t version,scoped,prefixes,unknown,last_length,column,format,local,source_player,own_player,source_slot,tracks[2];};
extern struct xz_key_browser_proof xz_mods_key_browser_proof_v1;
/* Play-screen key line, drawn by the same native wstring hook as browser rows. */
/* Stock play-screen key line records (getUiPlayKeyLinePointer, XZ 1.26 0xd9324):
   0x224e19c + deck*0x224. Returns the owning deck 0/1 for text inside a record, else -1. */
int xz_key_line_deck(uintptr_t text);
/* Runtime publishes the effective-key text shown after the stock key, or "" for none. */
void xz_key_line_publish(int deck, const char *adjacent);
/* Stock key text + " " + published text, for one native wstring draw in the stock
   window, font and clip. Returns 0 when there is nothing to add. */
size_t xz_key_line_adjacent(int deck, const uint16_t *text, size_t length, uint16_t out[96]);
struct xz_key_line_proof { uint32_t version, matched[2], drawn[2]; };
extern struct xz_key_line_proof xz_mods_key_line_proof_v1;
#endif
