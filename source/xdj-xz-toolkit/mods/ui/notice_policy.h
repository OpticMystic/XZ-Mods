/* SPDX-License-Identifier: MIT */
#ifndef XZ_UI_NOTICE_POLICY_H
#define XZ_UI_NOTICE_POLICY_H
#include <string.h>
/* Which notices the play screens draw at all. Owner 2026-11-17: "I dont really like the blocking notices ...
   like the ones that block the waveforms to inform of a new deck becoming master". A notice that only repeats
   what the screen already shows is quiet: the diary and the log still get it, nothing is drawn. Every other
   notice (a refusal, a fault, something the screen does not show) is drawn, in a place that covers no waveform:
   MODERN's top-bar BEAT FX strip (shell_draw.c), CLASSIC's top-bar METRICS readout (ui/play_notice.h).

   Quiet, and what already shows it:
   - "MASTER: DECK 3/4", "MASTER: NATIVE DECK", "SOFTWARE CLOCK MASTER": the MASTER mark on the deck.
   - the loop's own words (LOOP ON / EXIT / HALF / DOUBLE, RELOOP, 4 BEAT LOOP, LOOP IN SET, LOOP IN / OUT
     ADJUST, LOOP ADJUST DONE): the loop band, its length and the IN / OUT adjust marker in the lane. DECK 1 and 2
     say none of these either.
   - QUANTIZE ON / OFF: the QUANTIZE lamp. MEMORY POINT SET / DELETED: the memory marks on the overview.
   - "Dn KEY ...", KEY REQUEST QUEUED / PLAY TO VERIFY, KEY RESET QUEUED: the KEY readout and its shift.
   - "LEFT: DECK n", "RIGHT: DECK n" (a side switched deck), "DECK VIEW: ...": the lanes' deck tags and the
     DECK VIEW button. "DECKS 3 AND 4 ARE READY" (four-deck view): the route card says READY in the lanes.
   - SCRATCH RELEASE TRUNCATED: a diagnostic; nothing for the DJ to do.
   Shown although a deck 3/4 confirmation: AUTO CUE ON / OFF and TEMPO RESET ON / OFF (no lamp on PLAY carries
   a software deck's own setting), CUES / LOOP KEPT FOR THIS SESSION (the stick did not take the change). */
static inline int xz_notice_quiet(const char *text){
 static const char *const exact[]={
  "LOOP ON","LOOP EXIT","RELOOP","4 BEAT LOOP","LOOP IN SET","LOOP IN ADJUST","LOOP OUT ADJUST","LOOP ADJUST DONE",
  "LOOP HALF","LOOP DOUBLE","QUANTIZE ON","QUANTIZE OFF","MEMORY POINT SET","MEMORY POINT DELETED",
  "SOFTWARE CLOCK MASTER","KEY REQUEST QUEUED / PLAY TO VERIFY","KEY RESET QUEUED","SCRATCH RELEASE TRUNCATED",
  "DECKS 3 AND 4 ARE READY"};
 static const char *const prefix[]={"MASTER: ","LEFT: ","RIGHT: ","DECK VIEW: "};
 if(!text||!text[0])return 1;
 for(unsigned i=0;i<sizeof exact/sizeof exact[0];i++)if(!strcmp(text,exact[i]))return 1;
 for(unsigned i=0;i<sizeof prefix/sizeof prefix[0];i++)if(!strncmp(text,prefix[i],strlen(prefix[i])))return 1;
 /* A key request the deck took: "D1 KEY +100c", "D2 KEY +0c / MASTER CAPTURED" (ui_runtime.c). */
 return text[0]=='D'&&text[1]>='1'&&text[1]<='4'&&!strncmp(text+2," KEY ",5);
}
#endif
