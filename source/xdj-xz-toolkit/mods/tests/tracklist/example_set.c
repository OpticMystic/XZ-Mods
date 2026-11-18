/* SPDX-License-Identifier: MIT */
/* Runs the core over a simulated one-hour, four-deck set with invented track names and prints the files the unit
   would write. Used for the example in REPORT.md. Build: gcc -std=c11 -o out/example tests/example_set.c
   proposal/mods/tracklist.c && out/example */
#include "../../tracklist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct xz_tl *t;static struct xz_tl_input in;static uint32_t now=50000,rec0;
static void tick_to(uint32_t s){
 uint32_t until=50000+s*1000u;
 while(now<until){
  now+=500;in.now_ms=now;
  in.rec.elapsed_known=in.rec.recording;in.rec.elapsed_ms=in.rec.recording?now-rec0:0;
  xz_tl_step(t,&in);
  struct xz_tl_op op;
  while(xz_tl_take(t,&op)){
   static const char *kinds[]={"OPEN","RESUME","APPEND","CLOSE","WHOLE"};
   fprintf(stderr,"[%s %s]\n",kinds[op.kind],strrchr(op.path,'/')+1);
   fwrite(op.text,1,op.length,stdout);xz_tl_op_free(&op);
  }
 }
}
static void load(int d,uint32_t id,const char *artist,const char *title){
 struct xz_tl_deck_in *x=&in.deck[d];x->loaded=1;x->track_key=(1ull<<32)|id;x->load_id++;x->text_known=1;
 snprintf(x->artist,XZ_TL_TEXT,"%s",artist);snprintf(x->title,XZ_TL_TEXT,"%s",title);x->playing=0;x->on_air=0;
}
static void play(int d,int on){in.deck[d].playing=on;}
static void air(int d,int on){in.deck[d].on_air=on;}
/* A mix: the incoming deck plays in the headphones for cue_s, comes on air, the outgoing one leaves after
   overlap_s. Times in seconds of the recording. */
static void mix(int from,int to,uint32_t at,uint32_t cue_s,uint32_t overlap_s){
 tick_to(at-cue_s);play(to,1);tick_to(at);air(to,1);tick_to(at+overlap_s);air(from,0);tick_to(at+overlap_s+5);play(from,0);
}
int main(void){
 t=malloc(xz_tl_size());xz_tl_init(t,NULL);memset(&in,0,sizeof in);in.rec.known=1;
 for(int i=0;i<4;i++){in.deck[i].known=1;in.deck[i].playing=0;in.deck[i].on_air=0;}
 /* Before the recording: the opener is already playing. */
 load(0,1,"Harbour Lights","Slow Morning (Extended Mix)");play(0,1);air(0,1);
 tick_to(40);in.rec.recording=1;in.rec.generation=1;
 snprintf(in.rec.file,sizeof in.rec.file,"/mnt/usb2/PIONEER REC/REC014.WAV");rec0=now;
 /* recording time = sim time - 40 */
 load(1,2,"Kestrel","Paper Planes");                   mix(0,1,40+251,60,45);
 load(0,3,"Mira Solenne","Cobalt");                     mix(1,0,40+532,50,60);
 load(2,4,"Ondine & The Tide","Lumière");               mix(0,2,40+790,40,30);
 /* A track cued in the headphones and never brought in. */
 load(3,5,"Test Pattern","Unused Idea");play(3,1);tick_to(40+900);play(3,0);
 load(3,6,"Fieldwork","Night Bus");                     mix(2,3,40+1047,45,50);
 load(1,7,"","untitled_master_v3");                     mix(3,1,40+1310,30,20);
 load(0,8,"Kestrel","Glasshouse (Dub)");                mix(1,0,40+1586,60,90);
 /* Doubles: the same track on DECK 3 brought in over DECK 1 for a long blend. */
 load(2,8,"Kestrel","Glasshouse (Dub)");                mix(0,2,40+1750,20,40);
 load(3,9,"Arvo Lane","Still Water");                   mix(2,3,40+2020,45,30);
 load(1,10,"Mira Solenne","Static Bloom");              mix(3,1,40+2301,40,35);
 /* A five-second spinback teaser of the next track, then the real mix a minute later. */
 load(0,11,"Harbour Lights","Afterglow");play(0,1);tick_to(40+2540);air(0,1);tick_to(40+2545);air(0,0);
 tick_to(40+2560);                                       mix(1,0,40+2600,0,45);
 load(2,12,"Delta Nine","Overpass");                    mix(0,2,40+2890,50,40);
 load(3,13,"Fieldwork","Last Train Home");              mix(2,3,40+3170,45,60);
 tick_to(40+3600+42);in.rec.recording=0;tick_to(40+3600+44);
 free(t);return 0;
}
