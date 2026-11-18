/* SPDX-License-Identifier: MIT */
/* Host tests for the tracklist core: simulated sets drive xz_tl_step every 500 ms, the queued file operations are
   applied to an in-memory folder, and the files are compared with the expected text. No real track data. */
#include "../../tracklist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;
static int at_end;          /* the suite runs twice: live lines, then the whole file at the end */
#define CHECK(c) do{checks++;if(!(c)){failures++;fprintf(stderr,"%s:%d: CHECK failed: %s\n",__FILE__,__LINE__,#c);}}while(0)

/* ---- in-memory folder ---- */
#define FILES 16
static struct { char path[300]; char *text; size_t n; int ops; } files[FILES];
static unsigned file_count;
static void fs_reset(void){for(unsigned i=0;i<file_count;i++)free(files[i].text);memset(files,0,sizeof files);file_count=0;}
static int fs_find(const char *path,int make){
 for(unsigned i=0;i<file_count;i++)if(!strcmp(files[i].path,path))return (int)i;
 if(!make||file_count>=FILES)return -1;
 snprintf(files[file_count].path,sizeof files[file_count].path,"%s",path);return (int)file_count++;
}
static void fs_apply(struct xz_tl_op *op){
 if(at_end&&op->kind!=XZ_TL_OP_WHOLE&&op->kind!=XZ_TL_OP_RESUME){failures++;fprintf(stderr,"at_end mode queued a live operation\n");}
 int i=fs_find(op->path,1);if(i<0)return;
 if(op->kind==XZ_TL_OP_OPEN||op->kind==XZ_TL_OP_WHOLE){free(files[i].text);files[i].text=NULL;files[i].n=0;}
 files[i].text=realloc(files[i].text,files[i].n+op->length+1);
 memcpy(files[i].text+files[i].n,op->text,op->length);files[i].n+=op->length;files[i].text[files[i].n]=0;
 files[i].ops++;
}
static const char *fs_text(const char *path){int i=fs_find(path,0);return i<0?NULL:files[i].text;}

/* ---- the simulated unit ---- */
struct sim {
 struct xz_tl *t; struct xz_tl_input in; uint32_t now; uint32_t rec_start; int elapsed_known;
};
static int keep_files;       /* a player restart: the new runtime finds the files the old one wrote */
static void sim_init(struct sim *s,const struct xz_tl_config *c){
 if(!keep_files)fs_reset();
 memset(s,0,sizeof *s);s->t=malloc(xz_tl_size());
 struct xz_tl_config d;if(!c){xz_tl_default_config(&d);d.at_end=at_end;c=&d;}
 xz_tl_init(s->t,c);
 s->now=100000u;s->in.rec.known=1;s->elapsed_known=1;
 for(int i=0;i<XZ_TL_DECKS;i++){s->in.deck[i].known=1;s->in.deck[i].on_air=0;s->in.deck[i].playing=0;}
}
static void sim_free(struct sim *s){free(s->t);s->t=NULL;}
static uint32_t T(uint32_t seconds){return 100000u+seconds*1000u;}   /* sim time from 0 s */
static void sim_tick(struct sim *s){
 s->in.now_ms=s->now;
 s->in.rec.elapsed_known=s->in.rec.recording&&s->elapsed_known;
 s->in.rec.elapsed_ms=s->in.rec.recording?s->now-s->rec_start:0;
 xz_tl_step(s->t,&s->in);
 struct xz_tl_op op;while(xz_tl_take(s->t,&op)){fs_apply(&op);xz_tl_op_free(&op);}
}
/* Steps every 500 ms up to and including time `until` (sim seconds, may be fractional via ms). */
static void run_ms(struct sim *s,uint32_t until_abs){while(s->now<until_abs){s->now+=500u;sim_tick(s);}}
static void run_to(struct sim *s,uint32_t seconds){run_ms(s,T(seconds));}
static void load(struct sim *s,int d,uint32_t source,uint32_t id,const char *artist,const char *title){
 struct xz_tl_deck_in *x=&s->in.deck[d];x->loaded=1;x->track_key=(uint64_t)source<<32|id;
 if(d>=2)x->load_id++;
 snprintf(x->artist,sizeof x->artist,"%s",artist);snprintf(x->title,sizeof x->title,"%s",title);x->text_known=1;
}
static void unload(struct sim *s,int d){struct xz_tl_deck_in *x=&s->in.deck[d];x->loaded=0;x->playing=0;x->track_key=0;x->text_known=0;}
static void play(struct sim *s,int d,int on){s->in.deck[d].playing=on;}
static void air(struct sim *s,int d,int on){s->in.deck[d].on_air=on;}
static void rec_start(struct sim *s,uint32_t generation,const char *file){
 s->in.rec.recording=1;s->in.rec.generation=generation;snprintf(s->in.rec.file,sizeof s->in.rec.file,"%s",file);s->rec_start=s->now;
}
static void rec_split(struct sim *s,uint32_t generation,const char *file){rec_start(s,generation,file);}
static void rec_stop(struct sim *s){s->in.rec.recording=0;}

static int same(const char *what,const char *got,const char *want){
 if(got&&want&&!strcmp(got,want))return 1;
 fprintf(stderr,"---- %s: expected ----\n%s\n---- got ----\n%s\n----\n",what,want?want:"(none)",got?got:"(none)");
 return 0;
}
#define DIR "/media/usb2/PIONEER REC/"
#define UNI_ARTIST "\xc3\x9c" "n" "\xc3\xaf" "c" "\xc3\xb8" "d" "\xc3\xa9" " " "\xc3\x84" "rtist"
#define UNI_TITLE "T" "\xc3\xaf" "tle \"quoted\""
#define HEADER(name) name " tracklist (XZ Mods)\r\n" \
 "Times are from the start of " name ". A track is listed once it has been on air for 30 seconds.\r\n\r\n"

/* A short set: a track running before the recording, one cued in the headphones and brought in, a unicode
   track with quotes, an untitled one, a 5 s blip that is not listed, doubles on the other deck, a track
   brought in 10 s before the end, then a second recording that starts while two tracks are on air. */
static void test_set(void){
 struct sim s;sim_init(&s,NULL);
 load(&s,0,1,101,"Artist A","Track A");play(&s,0,1);air(&s,0,1);
 run_to(&s,10);
 rec_start(&s,1,DIR "REC001.WAV");
 run_to(&s,60);
 load(&s,1,1,202,"Artist B","Track B");play(&s,1,1);          /* headphones: fader down */
 run_to(&s,190);air(&s,1,1);                                  /* rec 3:00 */
 run_to(&s,250);air(&s,0,0);run_to(&s,255);play(&s,0,0);
 run_to(&s,300);load(&s,2,1,303,UNI_ARTIST,UNI_TITLE);play(&s,2,1);
 run_to(&s,360);air(&s,2,1);                                  /* rec 5:50 */
 run_to(&s,400);load(&s,3,1,404,"Artist D","Track D");play(&s,3,1);air(&s,3,1);
 run_to(&s,405);air(&s,3,0);                                  /* a 5 s blip */
 run_to(&s,420);load(&s,0,1,505,"Artist E","");play(&s,0,1);
 run_to(&s,450);air(&s,0,1);                                  /* rec 7:20 */
 run_to(&s,500);play(&s,1,0);air(&s,1,0);
 run_to(&s,520);load(&s,1,1,303,UNI_ARTIST,UNI_TITLE);
 play(&s,1,1);air(&s,1,1);                                    /* doubles of the deck 3 track */
 run_to(&s,590);air(&s,3,1);                                  /* rec 9:40, heard 10 s at the stop */
 run_to(&s,600);rec_stop(&s);run_ms(&s,T(600)+500u);
 const char *want1=HEADER("REC001.WAV")
  "00:00:00  Artist A - Track A  (continued)\r\n"
  "00:03:00  Artist B - Track B\r\n"
  "00:05:50  " UNI_ARTIST " - " UNI_TITLE "\r\n"
  "00:07:20  Artist E - (untitled)\r\n"
  "00:09:40  Artist D - Track D\r\n"
  "\r\nEnd of recording at 00:09:50.\r\n";
 CHECK(same("REC001.TXT",fs_text(DIR "REC001.TXT"),want1));
 const char *cue1="\xef\xbb\xbfREM COMMENT \"XZ Mods tracklist\"\r\nTITLE \"REC001\"\r\nFILE \"REC001.WAV\" WAVE\r\n"
  "  TRACK 01 AUDIO\r\n    TITLE \"Track A\"\r\n    PERFORMER \"Artist A\"\r\n    INDEX 01 00:00:00\r\n"
  "  TRACK 02 AUDIO\r\n    TITLE \"Track B\"\r\n    PERFORMER \"Artist B\"\r\n    INDEX 01 03:00:37\r\n"
  "  TRACK 03 AUDIO\r\n    TITLE \"T" "\xc3\xaf" "tle 'quoted'\"\r\n    PERFORMER \"" UNI_ARTIST "\"\r\n    INDEX 01 05:50:37\r\n"
  "  TRACK 04 AUDIO\r\n    TITLE \"(untitled)\"\r\n    PERFORMER \"Artist E\"\r\n    INDEX 01 07:20:37\r\n"
  "  TRACK 05 AUDIO\r\n    TITLE \"Track D\"\r\n    PERFORMER \"Artist D\"\r\n    INDEX 01 09:40:37\r\n";
 CHECK(same("REC001.CUE",fs_text(DIR "REC001.CUE"),cue1));

 /* The second recording starts while DECK 1, DECK 3 (with its doubles on DECK 2) and DECK 4 are on air. */
 run_to(&s,700);rec_start(&s,2,DIR "REC002.WAV");
 run_to(&s,800);rec_stop(&s);run_ms(&s,T(800)+500u);
 const char *want2=HEADER("REC002.WAV")
  "00:00:00  Artist E - (untitled)  (continued)\r\n"
  "00:00:00  " UNI_ARTIST " - " UNI_TITLE "  (continued)\r\n"
  "00:00:00  Artist D - Track D  (continued)\r\n"
  "\r\nEnd of recording at 00:01:40.\r\n";
 CHECK(same("REC002.TXT",fs_text(DIR "REC002.TXT"),want2));
 struct xz_tl_status st;xz_tl_status(s.t,&st);
 CHECK(st.files==2&&st.entries==8&&!st.ops_dropped&&!st.entries_dropped&&!st.file_open);
 sim_free(&s);
}

/* TRACK MARK: the old file lists a track brought in 15 s before the split and says where the recording goes on;
   the new file carries both tracks at 00:00:00. */
static void test_split(void){
 struct sim s;sim_init(&s,NULL);
 rec_start(&s,5,DIR "REC010.WAV");
 run_to(&s,10);load(&s,0,2,11,"A","One");play(&s,0,1);air(&s,0,1);
 run_to(&s,100);load(&s,1,2,12,"B","Two");play(&s,1,1);air(&s,1,1);
 run_to(&s,115);rec_split(&s,6,DIR "REC011.WAV");
 run_to(&s,200);rec_stop(&s);run_ms(&s,T(200)+500u);
 CHECK(same("REC010.TXT",fs_text(DIR "REC010.TXT"),HEADER("REC010.WAV")
  "00:00:10  A - One\r\n00:01:40  B - Two\r\n\r\nRecording continues in REC011.WAV (split at 00:01:55).\r\n"));
 CHECK(same("REC011.TXT",fs_text(DIR "REC011.TXT"),HEADER("REC011.WAV")
  "00:00:00  A - One  (continued)\r\n00:00:00  B - Two  (continued)\r\n\r\nEnd of recording at 00:01:25.\r\n"));
 CHECK(fs_text(DIR "REC010.CUE")&&strstr(fs_text(DIR "REC010.CUE"),"INDEX 01 01:40:"));
 sim_free(&s);
}

/* Order: a track faded in and out (heard 30 s only at 0:54) is written before one brought in later that reached
   30 s first. Nothing is written while the earlier one is still pending. */
static void test_order(void){
 struct sim s;sim_init(&s,NULL);
 rec_start(&s,1,DIR "REC020.WAV");
 load(&s,0,1,1,"X","Early");play(&s,0,1);load(&s,1,1,2,"Y","Late");play(&s,1,1);
 run_to(&s,10);air(&s,0,1);run_to(&s,15);air(&s,1,1);
 run_to(&s,20);air(&s,0,0);run_to(&s,27);air(&s,0,1);run_to(&s,34);air(&s,0,0);run_to(&s,41);air(&s,0,1);
 run_to(&s,50);
 if(at_end)CHECK(!fs_text(DIR "REC020.TXT"));
 else CHECK(same("REC020 at 0:50",fs_text(DIR "REC020.TXT"),HEADER("REC020.WAV")));
 run_to(&s,60);
 if(at_end)CHECK(!fs_text(DIR "REC020.TXT"));
 else CHECK(same("REC020 at 1:00",fs_text(DIR "REC020.TXT"),HEADER("REC020.WAV")"00:00:10  X - Early\r\n00:00:15  Y - Late\r\n"));
 rec_stop(&s);run_ms(&s,T(60)+500u);
 CHECK(same("REC020 final",fs_text(DIR "REC020.TXT"),HEADER("REC020.WAV")"00:00:10  X - Early\r\n00:00:15  Y - Late\r\n\r\nEnd of recording at 00:01:00.\r\n"));
 sim_free(&s);
 /* hold_max: a run that never settles does not hold the list for more than two minutes. */
 sim_init(&s,NULL);rec_start(&s,1,DIR "REC022.WAV");
 load(&s,0,1,1,"X","Scratchy");play(&s,0,1);load(&s,1,1,2,"Y","Steady");play(&s,1,1);
 air(&s,1,0);
 uint32_t t=10;for(;t<15;t+=6){run_to(&s,t);air(&s,0,1);run_to(&s,t+1);air(&s,0,0);}
 air(&s,1,1);                                               /* Y on air from 0:11, listed at 0:41, held */
 for(;t<200;t+=6){run_to(&s,t);air(&s,0,1);run_to(&s,t+1);air(&s,0,0);}
 /* Y waits for X (pending since 0:10) at most 120 s, so it is written at 2:41, before X lists at about 3:10. */
 rec_stop(&s);run_to(&s,t+1);
 const char *got=fs_text(DIR "REC022.TXT");
 CHECK(got&&strstr(got,"00:00:11  Y - Steady")&&strstr(got,"00:00:10  X - Scratchy")&&
  strstr(got,"00:00:11  Y - Steady")<strstr(got,"00:00:10  X - Scratchy"));
 sim_free(&s);
}

/* The mixer's ON AIR flags cannot be read: listed by play time, 60 s, and the header says so. */
static void test_no_air(void){
 struct sim s;sim_init(&s,NULL);
 for(int i=0;i<XZ_TL_DECKS;i++)air(&s,i,-1);
 rec_start(&s,1,DIR "REC030.WAV");
 run_to(&s,5);load(&s,0,1,1,"A","Long");play(&s,0,1);
 run_to(&s,70);load(&s,1,1,2,"B","Cued only");play(&s,1,1);
 run_to(&s,140);play(&s,1,0);                 /* 70 s in the headphones: listed, the documented limit */
 run_to(&s,150);rec_stop(&s);run_ms(&s,T(150)+500u);
 CHECK(same("REC030.TXT",fs_text(DIR "REC030.TXT"),"REC030.WAV tracklist (XZ Mods)\r\n"
  "Times are from the start of REC030.WAV. The mixer's ON AIR state could not be read, so a track is listed\r\n"
  "once it has played for 60 seconds, even if its channel fader was down.\r\n\r\n"
  "00:00:05  A - Long\r\n00:01:10  B - Cued only\r\n\r\nEnd of recording at 00:02:30.\r\n"));
 sim_free(&s);
 /* A single failed on-air read does not change the mode or end a run. */
 sim_init(&s,NULL);rec_start(&s,1,DIR "REC031.WAV");
 load(&s,0,1,1,"A","Steady");play(&s,0,1);air(&s,0,1);
 run_to(&s,10);for(int i=0;i<XZ_TL_DECKS;i++)air(&s,i,-1);run_to(&s,12);air(&s,0,1);for(int i=1;i<4;i++)air(&s,i,0);
 run_to(&s,40);rec_stop(&s);run_ms(&s,T(40)+500u);
 CHECK(same("REC031.TXT",fs_text(DIR "REC031.TXT"),HEADER("REC031.WAV")"00:00:00  A - Steady\r\n\r\nEnd of recording at 00:00:40.\r\n"));
 sim_free(&s);
 /* On-air reads that fail 3 s in every 4 keep the last value: a track playing only in the headphones is never
    counted as heard, and the header keeps the on-air wording. */
 sim_init(&s,NULL);rec_start(&s,1,DIR "REC032.WAV");
 load(&s,1,1,2,"B","Headphones");play(&s,1,1);
 for(uint32_t t=1;t<200;t+=4){
  for(int i=0;i<XZ_TL_DECKS;i++)air(&s,i,0);
  run_to(&s,t);
  for(int i=0;i<XZ_TL_DECKS;i++)air(&s,i,-1);
  run_to(&s,t+3);
 }
 for(int i=0;i<XZ_TL_DECKS;i++)air(&s,i,0);
 run_to(&s,200);rec_stop(&s);run_ms(&s,T(200)+500u);
 CHECK(same("REC032.TXT",fs_text(DIR "REC032.TXT"),HEADER("REC032.WAV")"\r\nEnd of recording at 00:03:20.\r\n"));
 sim_free(&s);
}

/* The mods restart in the middle of a recording; the recorder's clock is known, then not known. */
static void test_resume(void){
 struct sim s;sim_init(&s,NULL);
 s.in.rec.recording=1;s.in.rec.generation=3;snprintf(s.in.rec.file,sizeof s.in.rec.file,DIR "REC003.WAV");
 s.rec_start=s.now-600000u;                                  /* ten minutes in */
 load(&s,0,1,1,"A","After");play(&s,0,1);
 run_to(&s,5);air(&s,0,1);run_to(&s,40);rec_stop(&s);run_ms(&s,T(40)+500u);
 char want[512];
 snprintf(want,sizeof want,"\r\nREC003.WAV tracklist (XZ Mods), continued after XZ Mods restarted at 00:10:00.\r\n"
  "%s\r\n\r\n00:10:05  A - After\r\n\r\nEnd of recording at 00:10:40.\r\n",
  at_end?"Tracks heard before that are not in this list.":"Tracks heard just before that may be missing.");
 CHECK(same("REC003.TXT",fs_text(DIR "REC003.TXT"),want));
 CHECK(!fs_text(DIR "REC003.CUE"));                         /* a resumed list is incomplete: no CUE */
 sim_free(&s);
 sim_init(&s,NULL);s.elapsed_known=0;
 s.in.rec.recording=1;s.in.rec.generation=4;snprintf(s.in.rec.file,sizeof s.in.rec.file,DIR "REC004.WAV");
 load(&s,0,1,1,"A","After");play(&s,0,1);air(&s,0,1);
 run_to(&s,60);rec_stop(&s);run_ms(&s,T(60)+500u);
 CHECK(same("REC004.TXT",fs_text(DIR "REC004.TXT"),"\r\nREC004.WAV tracklist (XZ Mods): XZ Mods restarted during this recording and could not read\r\n"
  "the time into it, so no further tracks are listed.\r\n\r\nEnd of recording.\r\n"));
 sim_free(&s);
}

/* The recorder seam: path late, elapsed not given (our own clock from the start edge), recorder lost, bad path. */
static void test_recorder_seam(void){
 struct sim s;sim_init(&s,NULL);s.elapsed_known=0;
 load(&s,0,1,1,"A","Edge");play(&s,0,1);air(&s,0,1);
 run_to(&s,10);rec_start(&s,1,"");run_to(&s,12);snprintf(s.in.rec.file,sizeof s.in.rec.file,DIR "REC040.WAV");
 run_to(&s,50);s.in.rec.known=0;run_to(&s,52);
 CHECK(!fs_text(DIR "REC040.TXT")||!strstr(fs_text(DIR "REC040.TXT"),"could no longer"));   /* 2 s: still open */
 run_to(&s,57);
 CHECK(same("REC040.TXT",fs_text(DIR "REC040.TXT"),HEADER("REC040.WAV")"00:00:00  A - Edge  (continued)\r\n"
  "\r\nThe recorder could no longer be read at about 00:00:40; the list ends here.\r\n"));
 sim_free(&s);
 sim_init(&s,NULL);
 load(&s,0,1,1,"A","Edge");play(&s,0,1);air(&s,0,1);
 rec_start(&s,1,"/media/usb2/PIONEER REC/REC041.MP3");run_to(&s,60);rec_stop(&s);run_to(&s,61);
 CHECK(file_count==0);
 sim_free(&s);
 /* A start that fails at once (stick read-only, under 32 MB): recording shows for one sample, no file appears. */
 sim_init(&s,NULL);load(&s,0,1,1,"A","Edge");play(&s,0,1);air(&s,0,1);
 run_to(&s,10);rec_start(&s,1,DIR "REC043.WAV");s.now+=500u;sim_tick(&s);rec_stop(&s);run_to(&s,60);
 CHECK(file_count==0);
 /* A failed start, then the real one a moment later: times count from the real start (our own clock here). */
 s.elapsed_known=0;rec_start(&s,2,DIR "REC043.WAV");s.now+=500u;sim_tick(&s);
 rec_start(&s,3,DIR "REC044.WAV");run_to(&s,63);air(&s,0,0);load(&s,1,1,2,"B","Real");play(&s,1,1);air(&s,1,1);
 run_to(&s,100);rec_stop(&s);run_ms(&s,T(100)+500u);
 CHECK(!fs_text(DIR "REC043.TXT")&&fs_text(DIR "REC044.TXT")&&strstr(fs_text(DIR "REC044.TXT"),"00:00:02  B - Real"));
 sim_free(&s);
 /* The mods stop while recording (a player restart): the list ends with a note. */
 sim_init(&s,NULL);rec_start(&s,1,DIR "REC045.WAV");load(&s,0,1,1,"A","Edge");play(&s,0,1);air(&s,0,1);
 run_to(&s,45);xz_tl_finish(s.t,s.now);{struct xz_tl_op op;while(xz_tl_take(s.t,&op)){fs_apply(&op);xz_tl_op_free(&op);}}
 CHECK(same("REC045.TXT",fs_text(DIR "REC045.TXT"),HEADER("REC045.WAV")"00:00:00  A - Edge\r\n\r\nXZ Mods stopped at about 00:00:45; the list ends here.\r\n"));
 CHECK(!fs_text(DIR "REC045.CUE"));
 sim_free(&s);
 /* The recorder cannot be read at all: nothing is written. */
 sim_init(&s,NULL);s.in.rec.known=0;load(&s,0,1,1,"A","Edge");play(&s,0,1);air(&s,0,1);
 rec_start(&s,1,DIR "REC042.WAV");s.in.rec.known=0;run_to(&s,60);
 CHECK(file_count==0);
 sim_free(&s);
}

/* A short gap in the recorder reads (the seam saw the writer move for a moment) changes nothing: the file is the
   one a run without the gap writes. */
static void test_recorder_blip(void){
 const char *want=HEADER("REC046.WAV")"00:00:00  A - Edge  (continued)\r\n00:01:00  B - Blip\r\n\r\nEnd of recording at 00:02:00.\r\n";
 for(int blip=0;blip<2;blip++){
  struct sim s;sim_init(&s,NULL);
  load(&s,0,1,1,"A","Edge");play(&s,0,1);air(&s,0,1);run_to(&s,10);rec_start(&s,1,DIR "REC046.WAV");
  run_to(&s,70);load(&s,1,1,2,"B","Blip");play(&s,1,1);air(&s,1,1);
  run_to(&s,80);if(blip)s.in.rec.known=0;run_to(&s,82);s.in.rec.known=1;
  run_to(&s,130);rec_stop(&s);run_ms(&s,T(130)+500u);
  CHECK(same(blip?"REC046.TXT with a blip":"REC046.TXT",fs_text(DIR "REC046.TXT"),want));
  CHECK(fs_text(DIR "REC046.CUE")!=NULL);
  sim_free(&s);
 }
}

/* RECORDING TRACKLIST turned off while recording ends the list with a note and no CUE; turned on during a
   recording, that recording gets no list, and the file after a TRACK MARK does. */
static void test_switch(void){
 struct sim s;sim_init(&s,NULL);
 load(&s,0,1,1,"A","Edge");play(&s,0,1);air(&s,0,1);rec_start(&s,1,DIR "REC047.WAV");
 run_to(&s,45);xz_tl_turn_off(s.t,s.now);{struct xz_tl_op op;while(xz_tl_take(s.t,&op)){fs_apply(&op);xz_tl_op_free(&op);}}
 CHECK(same("REC047.TXT",fs_text(DIR "REC047.TXT"),HEADER("REC047.WAV")"00:00:00  A - Edge\r\n"
  "\r\nRECORDING TRACKLIST was turned off at about 00:00:45; the list ends here.\r\n"));
 CHECK(!fs_text(DIR "REC047.CUE"));
 sim_free(&s);
 sim_init(&s,NULL);
 load(&s,0,1,1,"A","Edge");play(&s,0,1);air(&s,0,1);rec_start(&s,1,DIR "REC048.WAV");
 xz_tl_skip_running(s.t);
 run_to(&s,60);
 CHECK(!fs_text(DIR "REC048.TXT"));
 rec_split(&s,2,DIR "REC049.WAV");run_to(&s,100);rec_stop(&s);run_ms(&s,T(100)+500u);
 CHECK(!fs_text(DIR "REC048.TXT"));
 CHECK(same("REC049.TXT",fs_text(DIR "REC049.TXT"),HEADER("REC049.WAV")"00:00:00  A - Edge  (continued)\r\n"
  "\r\nEnd of recording at 00:00:40.\r\n"));
 sim_free(&s);
}

/* USB STOP of the stick that holds the recording: the list ends at once with every entry (a track heard 15 s comes in
   by the tail rule), the CUE sheet and a footer naming the stop. A stop of the other stick changes nothing. The
   recorder going idle afterwards adds no second footer and no second write; the next recording gets its own list. */
static void drain(struct sim *s){struct xz_tl_op op;while(xz_tl_take(s->t,&op)){fs_apply(&op);xz_tl_op_free(&op);}}
static void test_stick_stop(void){
 struct sim s;sim_init(&s,NULL);
 CHECK(xz_tl_stick_stopped(s.t,"/media/usb2/",s.now)==0&&!xz_tl_open_wav(s.t)[0]);   /* nothing open */
 load(&s,0,1,1,"A","Opener");play(&s,0,1);air(&s,0,1);rec_start(&s,1,DIR "REC060.WAV");
 run_to(&s,100);load(&s,1,1,2,"B","Second");play(&s,1,1);air(&s,1,1);
 run_to(&s,115);
 CHECK(xz_tl_stick_stopped(s.t,"/media/usb1/",s.now)==0);
 CHECK(!strcmp(xz_tl_open_wav(s.t),DIR "REC060.WAV"));
 CHECK(xz_tl_stick_stopped(s.t,"/media/usb2/",s.now+200u)==1);
 CHECK(!xz_tl_open_wav(s.t)[0]);
 drain(&s);
 const char *want=HEADER("REC060.WAV")"00:00:00  A - Opener\r\n00:01:40  B - Second\r\n"
  "\r\nThe recording ended at about 00:01:55 because its USB stick was stopped (USB STOP).\r\n";
 CHECK(same("REC060.TXT at the stop",fs_text(DIR "REC060.TXT"),want));
 const char *cue=fs_text(DIR "REC060.CUE");
 CHECK(cue&&strstr(cue,"FILE \"REC060.WAV\" WAVE")&&strstr(cue,"TRACK 02 AUDIO\r\n    TITLE \"Second\"")&&!strstr(cue,"TRACK 03"));
 int txt=fs_find(DIR "REC060.TXT",0),ops=txt<0?0:files[txt].ops;
 /* Stock stops the recorder a moment later. */
 run_to(&s,117);rec_stop(&s);run_to(&s,125);
 CHECK(same("REC060.TXT after the recorder stopped",fs_text(DIR "REC060.TXT"),want));
 CHECK(txt>=0&&files[txt].ops==ops);
 /* The stick comes back and a new recording starts on it. */
 rec_start(&s,2,DIR "REC061.WAV");run_to(&s,160);rec_stop(&s);run_ms(&s,T(160)+500u);
 const char *next=fs_text(DIR "REC061.TXT");
 CHECK(next&&!strncmp(next,"REC061.WAV tracklist (XZ Mods)\r\n",32)&&strstr(next,"\r\nEnd of recording at 00:00:35.\r\n")&&!strstr(next,"USB STOP"));
 CHECK(fs_text(DIR "REC061.CUE")!=NULL);
 sim_free(&s);
}

/* The same track again: a reload within a minute is one entry, a replay 20 minutes later is a second one. */
static void test_repeat(void){
 struct sim s;sim_init(&s,NULL);rec_start(&s,1,DIR "REC050.WAV");
 load(&s,0,1,7,"A","Anthem");play(&s,0,1);air(&s,0,1);
 run_to(&s,100);unload(&s,0);run_to(&s,105);load(&s,0,1,7,"A","Anthem");play(&s,0,1);   /* reload */
 run_to(&s,200);load(&s,0,1,8,"B","Other");play(&s,0,1);
 run_to(&s,1300);load(&s,0,1,7,"A","Anthem");play(&s,0,1);                                 /* replay */
 run_to(&s,1400);rec_stop(&s);run_ms(&s,T(1400)+500u);
 CHECK(same("REC050.TXT",fs_text(DIR "REC050.TXT"),HEADER("REC050.WAV")
  "00:00:00  A - Anthem\r\n00:03:20  B - Other\r\n00:21:40  A - Anthem\r\n\r\nEnd of recording at 00:23:20.\r\n"));
 sim_free(&s);
}

/* A deck that cannot be read for a few seconds keeps its run; text arriving after the load is used. */
static void test_unreadable_deck(void){
 struct sim s;sim_init(&s,NULL);rec_start(&s,1,DIR "REC060.WAV");
 load(&s,2,1,9,"","");s.in.deck[2].text_known=0;play(&s,2,1);air(&s,2,1);
 run_to(&s,10);s.in.deck[2].known=0;run_to(&s,14);s.in.deck[2].known=1;
 run_to(&s,20);snprintf(s.in.deck[2].title,XZ_TL_TEXT,"Late title");snprintf(s.in.deck[2].artist,XZ_TL_TEXT,"Late artist");s.in.deck[2].text_known=1;
 run_to(&s,50);load(&s,3,1,10,"","");play(&s,3,1);air(&s,3,1);
 run_to(&s,90);rec_stop(&s);run_ms(&s,T(90)+500u);
 CHECK(same("REC060.TXT",fs_text(DIR "REC060.TXT"),HEADER("REC060.WAV")
  "00:00:00  Late artist - Late title\r\n00:00:50  Unknown track (DECK 4)\r\n\r\nEnd of recording at 00:01:30.\r\n"));
 sim_free(&s);
}

/* The sampler stalls for 20 s while a track is on air: the stall adds at most one second of heard time, so the
   track lists 20 s later than it would if the stall counted. */
static void test_stall(void){
 struct sim s;sim_init(&s,NULL);rec_start(&s,1,DIR "REC070.WAV");
 load(&s,0,1,1,"A","Stalled");play(&s,0,1);air(&s,0,1);
 run_to(&s,5);s.now+=20000u;sim_tick(&s);                  /* one sample after a 20 s gap */
 run_to(&s,40);
 if(!at_end)CHECK(same("REC070 at 0:40",fs_text(DIR "REC070.TXT"),HEADER("REC070.WAV")));
 run_to(&s,60);
 if(!at_end)CHECK(same("REC070 at 1:00",fs_text(DIR "REC070.TXT"),HEADER("REC070.WAV")"00:00:00  A - Stalled\r\n"));
 rec_stop(&s);run_ms(&s,T(60)+500u);
 CHECK(same("REC070 final",fs_text(DIR "REC070.TXT"),HEADER("REC070.WAV")"00:00:00  A - Stalled\r\n\r\nEnd of recording at 00:01:00.\r\n"));
 sim_free(&s);
}

/* The mods stop during a recording and start again while it goes on: the list keeps both parts, in both modes. */
static void test_restart(void){
 struct sim s;sim_init(&s,NULL);rec_start(&s,1,DIR "REC080.WAV");
 load(&s,0,1,1,"A","Before");play(&s,0,1);air(&s,0,1);
 run_to(&s,45);xz_tl_finish(s.t,s.now);{struct xz_tl_op op;while(xz_tl_take(s.t,&op)){fs_apply(&op);xz_tl_op_free(&op);}}
 uint32_t started=s.rec_start,now=s.now;sim_free(&s);
 keep_files=1;sim_init(&s,NULL);keep_files=0;
 s.now=now+5000u;s.rec_start=started;s.in.rec.recording=1;s.in.rec.generation=1;
 snprintf(s.in.rec.file,sizeof s.in.rec.file,DIR "REC080.WAV");
 load(&s,1,1,2,"B","After");play(&s,1,1);air(&s,1,1);
 run_to(&s,100);rec_stop(&s);run_ms(&s,T(100)+500u);
 char want[1024];
 snprintf(want,sizeof want,HEADER("REC080.WAV")"00:00:00  A - Before\r\n\r\nXZ Mods stopped at about 00:00:45; the list ends here.\r\n"
  "\r\nREC080.WAV tracklist (XZ Mods), continued after XZ Mods restarted at 00:00:50.\r\n%s\r\n\r\n"
  "00:00:50  B - After\r\n\r\nEnd of recording at 00:01:40.\r\n",
  at_end?"Tracks heard before that are not in this list.":"Tracks heard just before that may be missing.");
 CHECK(same("REC080.TXT",fs_text(DIR "REC080.TXT"),want));
 sim_free(&s);
}

static void test_text(void){
 char out[64];
 xz_tl_name(out,sizeof out,"  Art\tist\n","Ti\x01tle  ",0);CHECK(!strcmp(out,"Art ist - Ti tle"));
 xz_tl_name(out,sizeof out,"","\xff\xfe" "ok",1);CHECK(!strcmp(out,"??ok"));
 xz_tl_name(out,sizeof out,"","\xc0\xaf",1);CHECK(!strcmp(out,"??"));                 /* overlong '/' */
 xz_tl_name(out,sizeof out,"","\xed\xa0\x80x",1);CHECK(!strcmp(out,"???x"));          /* surrogate */
 xz_tl_name(out,sizeof out,"","",2);CHECK(!strcmp(out,"Unknown track (DECK 3)"));
 char small[8];xz_tl_name(small,sizeof small,"","\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9",0);CHECK(!strcmp(small,"\xc3\xa9\xc3\xa9\xc3\xa9"));
 char longer[400];memset(longer,'a',sizeof longer-1);longer[sizeof longer-1]=0;
 char line[512];size_t n=xz_tl_name(line,sizeof line,longer,longer,0);CHECK(n==2*(XZ_TL_TEXT-1)+3&&n==strlen(line));
 char clock[16];xz_tl_clock(clock,0);CHECK(!strcmp(clock,"00:00:00"));
 xz_tl_clock(clock,3723999u);CHECK(!strcmp(clock,"01:02:03"));
 xz_tl_clock(clock,360000000u);CHECK(!strcmp(clock,"100:00:00"));
 char path[300];
 CHECK(!xz_tl_sibling(path,sizeof path,"/m/PIONEER REC/REC007.WAV","TXT")&&!strcmp(path,"/m/PIONEER REC/REC007.TXT"));
 CHECK(!xz_tl_sibling(path,sizeof path,"/m/rec.wav","CUE")&&!strcmp(path,"/m/rec.cue"));
 CHECK(xz_tl_sibling(path,sizeof path,"/m/rec.mp3","TXT")==-1);
 CHECK(xz_tl_sibling(path,sizeof path,"/m/.wav","TXT")==-1);
 CHECK(xz_tl_sibling(path,sizeof path,"","TXT")==-1);
 CHECK(xz_tl_sibling(path,8,"/m/REC007.WAV","TXT")==-1);
}

/* A stalled writer: ops pile up to the queue limit and the overflow is counted, never blocking the step. */
static void test_queue_full(void){
 struct xz_tl *t=malloc(xz_tl_size());xz_tl_init(t,NULL);
 struct xz_tl_input in;memset(&in,0,sizeof in);in.rec.known=1;
 for(int i=0;i<XZ_TL_DECKS;i++){in.deck[i].known=1;in.deck[i].on_air=1;}
 uint32_t now=1000;
 for(uint32_t g=1;g<=80;g++){
  in.now_ms=now;in.rec.recording=1;in.rec.generation=g;in.rec.elapsed_known=1;in.rec.elapsed_ms=2000;
  snprintf(in.rec.file,sizeof in.rec.file,"/m/REC%03u.WAV",g);xz_tl_step(t,&in);now+=500;
 }
 struct xz_tl_status st;xz_tl_status(t,&st);
 CHECK(st.ops_dropped>0);
 struct xz_tl_op op;unsigned n=0;while(xz_tl_take(t,&op)){n++;xz_tl_op_free(&op);}
 CHECK(n==XZ_TL_OPS);
 free(t);
}

int main(void){
 for(at_end=0;at_end<2;at_end++){
  test_set();test_split();test_order();test_no_air();test_resume();test_recorder_seam();test_repeat();
  test_unreadable_deck();test_stall();test_restart();test_recorder_blip();test_switch();test_stick_stop();
 }
 at_end=0;test_text();test_queue_full();
 fs_reset();
 printf("%s: %d checks, %d failures\n",failures?"FAIL":"PASS",checks,failures);
 return failures?1:0;
}
