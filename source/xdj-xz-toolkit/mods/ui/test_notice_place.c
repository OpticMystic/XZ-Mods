#ifdef NDEBUG
#error Notice acceptance requires assertions
#endif
/* Owner 2026-11-17: no notices over the waveforms.
   1. notice_policy.h: every notice the mods raise is either quiet (it repeats what the screen shows: a new MASTER,
      the loop's words, a side switching deck) or shown; refusals and faults are always shown.
   2. play_notice.h: which sentence CLASSIC shows (route-up first, quiet ones skipped, 3.5 s), and that its paint
      touches only the top bar's METRICS columns (x 280..591, or ..671 for a long sentence; rows 0..17), never the
      waveform surface at (131,18), in every theme; and puts back what it covered unless fresh pixels came in. */
#include "ui_skin.h"
#include "play_notice.h"
#include "deck_view.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint16_t before[800*480],after[800*480];

static void test_policy(void){
 static const char *const quiet[]={
  "MASTER: DECK 3","MASTER: DECK 4","MASTER: NATIVE DECK","SOFTWARE CLOCK MASTER",
  "LOOP ON","LOOP EXIT","RELOOP","4 BEAT LOOP","LOOP IN SET","LOOP IN ADJUST","LOOP OUT ADJUST","LOOP ADJUST DONE",
  "LOOP HALF","LOOP DOUBLE","QUANTIZE ON","QUANTIZE OFF","MEMORY POINT SET","MEMORY POINT DELETED",
  "KEY REQUEST QUEUED / PLAY TO VERIFY","KEY RESET QUEUED","SCRATCH RELEASE TRUNCATED",
  "LEFT: DECK 3","RIGHT: DECK 2","LEFT: DECK 1  RIGHT: DECK 4","DECK VIEW: 4 DECKS (3 1 2 4)",
  "D1 KEY +100c","D4 KEY +0c / MASTER CAPTURED",XZ_DECK_VIEW_SUCCESS_NOTICE,"",NULL};
 static const char *const shown[]={
  /* refusals and faults */
  "DECK 1 DID NOT LOAD THE TRACK","USB STARTING / DECK 2 LOADS WHEN READY","LOAD LOCK: DECK 3 IS PLAYING",
  "USB SOURCE CHANGED: SELECT TRACK AGAIN","OPEN A TRACK LIST TO LOAD","SELECT A TRACK, THEN PRESS LOAD",
  "DECK 3/4 ROUTE LOST","DECK 3/4 ROUTE IS NOT THE MIXER","DECK 3/4 ROUTE IS IN USE",
  "DECK 3 AND 4 NEED THE CH3/CH4 ROUTE","DECK 3 AND 4 ARE NOT RUNNING","LOAD DECK 3/4 FROM THE FLAGSHIP SCREEN",
  "TRACK CANNOT BE READ","NEXT TRACK CANNOT BE READ","TRACK NOT FOUND","ROUTE CHANGED / LOAD REFUSED",
  "SOURCE CHANGED / LOAD REFUSED","ROUTE CHANGED / NEXT REFUSED","SOURCE CHANGED / NEXT REFUSED",
  "RAM HANDOFF FAILED / LOAD ANOTHER TRACK","EMERGENCY LOOP / LOAD NEXT TRACK","MASTER REQUEST NOT READY",
  "MASTER CHANGED / RETRY KEY SYNC","TRACK CHANGED / RETRY KEY CONTROL","KEY SHIFT REFUSED: AUDIO LOAD TOO HIGH",
  "SET UP KEYS / KEY TARGET NOT CAPTURED","SELECT ONE LOCAL MASTER / SET UP KEYS","THIS DECK IS MASTER",
  "JUMP UNAVAILABLE: NO BEAT GRID OR BPM","LOOP OUT MUST FOLLOW LOOP IN","LOOP OVER 8 SECONDS","SET A LOOP FIRST",
  "NO STORED POINTS","NO LATER POINT","NO EARLIER POINT","NO MEMORY POINT AT CUE","MEMORY POINTS FULL",
  "LOAD OTHER SOFTWARE DECK FIRST","DOUBLES SOURCE CHANGED","NO SAVED TRACK ORDER / NEXT UNAVAILABLE",
  "PLAYER BUSY / NEXT REFUSED","END OF TRACK LIST","SCRATCH BUFFER FULL / RETOUCH","SAVED CUES INVALID / USING EXPORT",
  "ABLETON LINK DID NOT CHANGE: TRY AGAIN","MODS DID NOT OPEN: PUSH AGAIN","LINK DID NOT OPEN","VJ.TOOLS IS NOT CONNECTED",
  "STEM ROWS SHOW ONCE STEMS ARE ON IN MODS","DECK 1 CANNOT SEARCH NOW","PLAYLIST CHANGE NOT APPLIED",
  "E-8304: USB ACCESS ERROR",
  /* worth knowing, nothing on screen says it */
  "CUES KEPT FOR THIS SESSION","LOOP KEPT FOR THIS SESSION","AUTO CUE ON","AUTO CUE OFF","TEMPO RESET ON",
  "TEMPO RESET OFF",XZ_DECK_VIEW_READY_NOTICE,
  /* look-alikes of quiet ones */
  "MASTER","DECK 1 KEY","D5 KEY +1c","LOOP","DECKS 3 AND 4 ARE READY."};
 for(unsigned i=0;i<sizeof quiet/sizeof *quiet;i++)assert(xz_notice_quiet(quiet[i]));
 for(unsigned i=0;i<sizeof shown/sizeof *shown;i++){
  if(xz_notice_quiet(shown[i])){fprintf(stderr,"quiet: %s\n",shown[i]);assert(0);}
 }
 printf("PASS notice policy: %u quiet (the screen shows them), %u shown (refusals, faults, unshown state)\n",
  (unsigned)(sizeof quiet/sizeof *quiet),(unsigned)(sizeof shown/sizeof *shown));
}

static void test_pick(void){
 struct xz_extra_decks s;memset(&s,0,sizeof s);char out[64];
 assert(!xz_play_notice_pick(NULL,&s,100,out,sizeof out)&&!out[0]);
 snprintf(s.notice,sizeof s.notice,"MASTER: DECK 3");s.notice_at_ms=100;s.notice_seq=1;
 assert(!xz_play_notice_pick(NULL,&s,100,out,sizeof out));                    /* quiet */
 snprintf(s.notice,sizeof s.notice,"DECK 3/4 ROUTE LOST");s.notice_seq=2;
 assert(xz_play_notice_pick(NULL,&s,100,out,sizeof out)==out&&!strcmp(out,"DECK 3/4 ROUTE LOST"));
 assert(xz_play_notice_pick(NULL,&s,3599,out,sizeof out));
 assert(!xz_play_notice_pick(NULL,&s,3600,out,sizeof out));                   /* 3.5 s */
 assert(!xz_play_notice_pick(NULL,NULL,100,out,sizeof out));
 /* The route-up notice wins; the four-deck view's READY repeats the card and is quiet, so the player's shows. */
 assert(!strcmp(xz_play_notice_pick(XZ_DECK_VIEW_READY_NOTICE,&s,100,out,sizeof out),XZ_DECK_VIEW_READY_NOTICE));
 assert(!strcmp(xz_play_notice_pick(XZ_DECK_VIEW_SUCCESS_NOTICE,&s,100,out,sizeof out),"DECK 3/4 ROUTE LOST"));
 memset(s.notice,'X',sizeof s.notice);                                            /* unterminated */
 assert(xz_play_notice_pick(NULL,&s,100,out,sizeof out)&&strlen(out)==sizeof s.notice-1);
 puts("PASS CLASSIC picks the route-up notice, else the players' fresh notice, never a quiet one");
}

static void fill(uint16_t *p){for(unsigned i=0;i<800*480;i++)p[i]=(uint16_t)(0x4567+i*7);}
static void test_paint(void){
 static const char *const texts[]={"DECK 3/4 ROUTE LOST","RAM HANDOFF FAILED / LOAD ANOTHER TRACK",
  XZ_DECK_VIEW_READY_NOTICE,"THE TRACK ON USB 2 COULD NOT BE READ BECAUSE THE DEVICE WAS REMOVED WHILE IT WAS LOADING"};
 struct xz_play_notice_pixels saved;memset(&saved,0,sizeof saved);
 for(int theme=0;theme<XZ_THEME_COUNT;theme++)for(unsigned t=0;t<sizeof texts/sizeof *texts;t++){
  fill(before);memcpy(after,before,sizeof before);
  assert(xz_play_notice_draw(after,800,theme,texts[t],&saved));
  int w=xz_play_notice_width(theme,texts[t]);
  assert(w==XZ_PLAY_NOTICE_W||w==XZ_PLAY_NOTICE_WIDE_W);
  if(t<2)assert(w==XZ_PLAY_NOTICE_W);
  unsigned changed=0;
  for(unsigned y=0;y<480;y++)for(unsigned x=0;x<800;x++)if(before[y*800+x]!=after[y*800+x]){
   /* Top bar only (rows 0..17): the waveform surface starts at y 18 (top_bar.h); x 280..671, STEMS from 674. */
   if(!(y<XZ_TOP_BAR_H&&x>=XZ_PLAY_NOTICE_X&&x<(unsigned)(XZ_PLAY_NOTICE_X+w))){
    fprintf(stderr,"theme %d text %u: pixel %u,%u\n",theme,t,x,y);assert(0);
   }
   changed++;
  }
  assert(changed>(unsigned)w*10);
  assert(XZ_PLAY_NOTICE_X+w<=XZ_TOP_STEMS_X);
  /* The next frame puts back what it covered. */
  xz_play_notice_restore(&saved,after,800);
  assert(!memcmp(before,after,sizeof before)&&!saved.valid);
 }
 /* Fresh pixels from the stock (or another painter) win over the restore. */
 fill(before);memcpy(after,before,sizeof before);
 assert(xz_play_notice_draw(after,800,0,texts[0],&saved));
 after[5*800+300]^=1;memcpy(before,after,sizeof before);
 xz_play_notice_restore(&saved,after,800);
 assert(!memcmp(before,after,sizeof before)&&!saved.valid);
 /* Nothing to say draws nothing. */
 fill(before);memcpy(after,before,sizeof before);
 assert(!xz_play_notice_draw(after,800,0,NULL,&saved)&&!xz_play_notice_draw(after,800,0,"",&saved));
 assert(!memcmp(before,after,sizeof before)&&!saved.valid);
 printf("PASS CLASSIC notice paints only top-bar rows 0..%d at x %d..%d (long: ..%d) in %d themes, and restores\n",
  XZ_TOP_BAR_H-1,XZ_PLAY_NOTICE_X,XZ_PLAY_NOTICE_X+XZ_PLAY_NOTICE_W-1,XZ_PLAY_NOTICE_X+XZ_PLAY_NOTICE_WIDE_W-1,XZ_THEME_COUNT);
}

int main(void){
 test_policy();
 test_pick();
 test_paint();
 return 0;
}
