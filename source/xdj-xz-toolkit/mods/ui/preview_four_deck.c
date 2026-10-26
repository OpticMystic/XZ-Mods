/* SPDX-License-Identifier: MIT */
/* Offline four-deck classic wave surface: a native two-deck 536x268 RGB565 surface in, the four-deck
   surface out for each fixture scenario, plus a host draw-cost bench. Driven by preview_four_deck.py.
   Every frame goes through xz_four_frame_draw, the composition the runtime's unlock hook draws. */
#define _POSIX_C_SOURCE 200809L
#include "extra_lane.h"
#include "extra_detail.h"
#include "route_card.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PIXELS (XZ_WAVE_WIDTH*XZ_WAVE_HEIGHT)
#define NOW 1000u
static uint16_t native[PIXELS],surface[PIXELS];
static uint8_t keep[XZ_FOUR_KEEP_BYTES];
static struct xz_extra_lane_marks marks[2];

struct track { struct xz_extra_column *columns; uint32_t count,span_ms,normalization; struct xz_extra_beat *grid; uint32_t beats; };
static uint32_t lcg(uint32_t *s){*s=*s*1664525u+1013904223u;return *s>>8;}
static void make_track(struct track *t,double bpm,uint32_t span_ms,uint32_t seed){
 t->span_ms=span_ms;t->count=span_ms*150/1000;
 t->columns=calloc(t->count,sizeof(*t->columns));
 double beat=60000.0/bpm;unsigned histogram[766]={0};
 for(uint32_t i=0;i<t->count;i++){
  double ms=i*1000.0/150,ph=fmod(ms,beat)/beat;unsigned n=(unsigned)(ms/beat);
  int drop=(n/32)%4==2,build=(n/16)%2;
  double low=drop?8:150*exp(-ph*9)+12,mid=(build?38:22)+14*sin(ms/2300.0)+((n%2)?55*exp(-ph*12):0);
  double high=10+45*exp(-fabs(ph-0.5)*24)+(drop?25:0);
  low+=lcg(&seed)%18;mid+=lcg(&seed)%16;high+=lcg(&seed)%14;
  t->columns[i]=(struct xz_extra_column){(uint8_t)(low>255?255:low),(uint8_t)(mid>255?255:mid),(uint8_t)(high>255?255:high)};
  histogram[t->columns[i].low+t->columns[i].mid+t->columns[i].high]++;
 }
 uint32_t below=0,target=t->count-t->count/200;t->normalization=765;
 for(unsigned v=0;v<766;v++){below+=histogram[v];if(below>=target){t->normalization=v?v:1;break;}}
 t->beats=(uint32_t)(span_ms/beat);t->grid=calloc(t->beats,sizeof(*t->grid));
 for(uint32_t i=0;i<t->beats;i++)t->grid[i]=(struct xz_extra_beat){(int32_t)(120+i*beat),(uint8_t)(i%4+1),0};
}
/* A loaded deck as the players publish one: every known bit they set, stock start modes (VINYL,
   QUANTIZE, REMAIN), HOT CUE pads and three coloured hot cues around the playhead. */
static void fill_deck(struct xz_extra_deck *d,const struct track *t,const char *title,const char *artist,
 int32_t elapsed,int32_t bpm,int32_t tempo,int playing){
 memset(d,0,sizeof(*d));
 d->known=~0u&((1u<<XZ_EXTRA_FIELD_COUNT)-1);d->loaded=1;
 snprintf(d->title,sizeof(d->title),"%s",title);snprintf(d->artist,sizeof(d->artist),"%s",artist);
 d->source=1;d->elapsed_ms=elapsed;d->total_ms=(int32_t)t->span_ms;d->elapsed_at_ms=NOW;d->playing=playing;
 d->bpm_x100=bpm*(10000+tempo)/10000;d->track_bpm_x100=bpm;d->tempo_x100=tempo;
 d->tempo_range=10;d->master_tempo=1;d->vinyl=1;d->quantize=1;d->remain=1;d->auto_cue=1;
 d->pad_mode=XZ_EXTRA_PAD_HOT_CUE;d->active_pad=-1;
 snprintf(d->key,sizeof(d->key),"8A");
 d->cue_ms=elapsed-900;
 static const uint32_t colors[3]={0x30d050,0xe03040,0x3080ff};
 for(int i=0;i<3;i++)d->hot_cue[i]=(struct xz_extra_cue){1,elapsed-1400+i*1100,colors[i],0};
 d->hot_cue[5]=(struct xz_extra_cue){1,elapsed+12000,0xffa020,elapsed+16000};
 d->grid=t->grid;d->grid_count=t->beats;
 d->wave=(struct xz_extra_wave){t->columns,t->count,t->span_ms,t->normalization};
}
static void master_of_software(struct xz_extra_deck *d,int index){
 d->master=1;d->master_scope=XZ_EXTRA_MASTER_SCOPE_SOFTWARE;
 d->sync=(struct xz_extra_sync){0,0,index+XZ_EXTRA_FIRST_DECK,XZ_EXTRA_SYNC_OFF,0,0,XZ_EXTRA_MASTER_SCOPE_SOFTWARE};
}
static void synced(struct xz_extra_deck *d,int master_deck,enum xz_extra_sync_reason reason,uint32_t scope){
 d->sync=(struct xz_extra_sync){1,reason==XZ_EXTRA_SYNC_LOCKED,master_deck,reason,0,0,scope};
}

static void render(const struct xz_extra_decks *decks,int order,int detail,int notice){
 memcpy(surface,native,sizeof(surface));
 xz_four_layout_pixels(surface,PIXELS,XZ_WAVE_WIDTH,order);
 struct xz_four_frame frame={decks,order,NOW,XZ_EXTRA_LANE_COLUMN_US,1,detail,notice,0};
 xz_four_frame_draw(surface,PIXELS,&frame,marks);
 struct xz_four_layout g;xz_four_layout_get(order,&g);
 struct xz_four_marks m={0};
 for(int i=0;i<2;i++){m.look[2+i]=marks[i].look;m.col[2+i]=marks[i].col;}
 xz_four_protect(&g,&m,surface,keep);
 xz_four_restore(&g,&m,surface,keep);
 if(detail<0)xz_extra_notice_draw(surface,XZ_WAVE_WIDTH,PIXELS,decks,NOW);
}
static void save(const char *folder,const char *name){
 char path[512];snprintf(path,sizeof(path),"%s/%s.raw",folder,name);
 FILE *f=fopen(path,"wb");if(!f||fwrite(surface,2,PIXELS,f)!=PIXELS){perror(path);exit(1);}fclose(f);
}
static double seconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}

int main(int argc,char **argv){
 if(argc<3){fprintf(stderr,"usage: %s native.raw out-folder [bench-frames]\n",argv[0]);return 2;}
 FILE *f=fopen(argv[1],"rb");if(!f||fread(native,2,PIXELS,f)!=PIXELS){perror(argv[1]);return 1;}fclose(f);
 struct track a,b;make_track(&a,128.0,312000,7);make_track(&b,174.0,268000,11);
 struct xz_extra_decks full={.route_up=1};
 fill_deck(&full.deck[0],&a,"Night Drive (Extended Mix)","Aurora Lane",61500,12800,250,1);
 fill_deck(&full.deck[1],&b,"Liquid Steps","Mira Vale",120200,17400,-120,1);
 master_of_software(&full.deck[0],0);
 synced(&full.deck[1],2,XZ_EXTRA_SYNC_LOCKED,XZ_EXTRA_MASTER_SCOPE_SOFTWARE);
 if(argc>3){
  /* Best of five rounds per step, so a busy host does not inflate one number. */
  int frames=atoi(argv[3]);double best[9]={1e9,1e9,1e9,1e9,1e9,1e9,1e9,1e9,1e9};
  static struct xz_route_card_cache card_cache;
  struct xz_four_layout g;xz_four_layout_get(XZ_FOUR_ORDER,&g);
  struct xz_four_marks two={0},four={0};
  for(int round=0;round<5;round++){
   double t0=seconds();
   for(int i=0;i<frames;i++)memcpy(surface,native,sizeof(surface));
   double copy=seconds()-t0;t0=seconds();
   for(int i=0;i<frames;i++){memcpy(surface,native,sizeof(surface));xz_four_layout_pixels(surface,PIXELS,XZ_WAVE_WIDTH,XZ_FOUR_ORDER);}
   double step[9]={copy,seconds()-t0-copy,0,0,0,0,0,0,0};t0=seconds();
   for(int i=0;i<frames;i++){
    struct xz_four_frame frame={&full,XZ_FOUR_ORDER,NOW+(uint32_t)i*16,XZ_EXTRA_LANE_COLUMN_US,1,-1,0,0};
    xz_four_frame_draw(surface,PIXELS,&frame,marks);
   }
   step[2]=seconds()-t0;t0=seconds();
   for(int i=0;i<frames;i++){
    struct xz_four_frame frame={&full,XZ_FOUR_ORDER,NOW+(uint32_t)i*16,XZ_EXTRA_LANE_COLUMN_US,1,0,0,0};
    xz_four_frame_draw(surface,PIXELS,&frame,marks);
   }
   step[3]=seconds()-t0;
   for(int i=0;i<2;i++){two.look[2+i]=four.look[2+i]=four.look[i]=marks[i].look;two.col[2+i]=four.col[2+i]=four.col[i]=marks[i].col;}
   t0=seconds();
   for(int i=0;i<frames;i++){xz_four_protect(&g,&two,surface,keep);xz_four_restore(&g,&two,surface,keep);}
   step[4]=seconds()-t0;t0=seconds();
   for(int i=0;i<frames;i++){xz_four_protect(&g,&four,surface,keep);xz_four_restore(&g,&four,surface,keep);}
   step[5]=seconds()-t0;
   /* DECK VIEW 4 DECKS with decks 3 and 4 down: native tags and the lane tint, then the card (route_card.h)
      after the theme, its pulse moving: copied from its cached still, and drawn whole every frame. */
   t0=seconds();
   for(int i=0;i<frames;i++){
    struct xz_four_frame frame={NULL,XZ_FOUR_ORDER,NOW+(uint32_t)i*16,XZ_EXTRA_LANE_COLUMN_US,1,-1,0,1};
    xz_four_frame_draw(surface,PIXELS,&frame,NULL);
   }
   step[6]=seconds()-t0;t0=seconds();
   for(int i=0;i<frames;i++){
    struct xz_route_card_input in={XZ_EXTRA_ROUTE_NOT_DETECTED,XZ_EXTRA_ROUTE_PART_CABLE,"The loopback audio route has not been detected.",NOW+(uint32_t)i*50,NOW,0,-1,0};
    xz_route_card_draw((struct xz_canvas){surface,XZ_WAVE_WIDTH,XZ_WAVE_WIDTH,XZ_WAVE_HEIGHT,0},&xz_route_card_classic,(int)g.lane_y[2],(int)g.lane_y[3],&in,&card_cache);
   }
   step[7]=seconds()-t0;t0=seconds();
   for(int i=0;i<frames;i++){
    struct xz_route_card_input in={XZ_EXTRA_ROUTE_NOT_DETECTED,XZ_EXTRA_ROUTE_PART_CABLE,"The loopback audio route has not been detected.",NOW+(uint32_t)i*50,NOW,0,-1,0};
    xz_route_card_draw((struct xz_canvas){surface,XZ_WAVE_WIDTH,XZ_WAVE_WIDTH,XZ_WAVE_HEIGHT,0},&xz_route_card_classic,(int)g.lane_y[2],(int)g.lane_y[3],&in,NULL);
   }
   step[8]=seconds()-t0;
   for(int s=0;s<9;s++)if(step[s]<best[s])best[s]=step[s];
  }
  printf("host us/frame, best of 5 x %d frames: surface copy %.1f, four layout %.1f, two extra lanes + tags %.1f, "
   "same with the detail panel open %.1f, theme protect+restore DECK 3/4 %.1f, all four lanes layered %.1f\n",frames,
   best[0]*1e6/frames,best[1]*1e6/frames,best[2]*1e6/frames,best[3]*1e6/frames,best[4]*1e6/frames,best[5]*1e6/frames);
  printf("host us/frame, DECK VIEW 4 DECKS with decks 3/4 down: native tags + lane tint %.1f, card from its cached still %.1f, "
   "card drawn whole %.1f\n",best[6]*1e6/frames,best[7]*1e6/frames,best[8]*1e6/frames);
  return 0;
 }
 /* Plates: DECK 3 the 3/4 clock master, DECK 4 locked to it; stock modes (REMAIN, A.CUE, Q, VINYL, MT). */
 render(&full,XZ_FOUR_STACKED,-1,0);save(argv[2],"01-stacked-master-and-locked");
 /* MIXER order 3 1 2 4. DECK 3 shows elapsed TIME; DECK 4 follows the native DECK 1 tempo only (QUANTIZE off). */
 struct xz_extra_decks modes=full;
 modes.deck[0].master=0;modes.deck[0].master_scope=0;modes.deck[0].remain=0;modes.deck[0].auto_cue=0;
 synced(&modes.deck[0],0,XZ_EXTRA_SYNC_WAIT_ANCHOR,XZ_EXTRA_MASTER_SCOPE_NATIVE);modes.deck[0].playing=0;
 modes.deck[1].quantize=0;modes.deck[1].tempo_range=100;modes.deck[1].master_tempo=0;
 synced(&modes.deck[1],0,XZ_EXTRA_SYNC_TEMPO_FOLLOW,XZ_EXTRA_MASTER_SCOPE_NATIVE);
 render(&modes,XZ_FOUR_MIXER,-1,0);save(argv[2],"02-mixer-time-mode-wait-vs-tempo-only");
 /* Pads and transport modes: DECK 3 BEAT LOOP pad 1/2 running under SLIP; DECK 4 REV, SLIP LOOP pads, TEMPO RESET. */
 struct xz_extra_decks pads=full;
 pads.deck[0].pad_mode=XZ_EXTRA_PAD_BEAT_LOOP;pads.deck[0].active_pad=1;pads.deck[0].slip=1;pads.deck[0].slip_active=1;
 pads.deck[0].loop.active=pads.deck[0].loop.stored=1;pads.deck[0].loop.in_ms=61300;pads.deck[0].loop.out_ms=61300+60000/256;
 pads.deck[1].reverse=1;pads.deck[1].pad_mode=XZ_EXTRA_PAD_SLIP_LOOP;pads.deck[1].slip=1;pads.deck[1].tempo_reset=1;
 pads.deck[1].tempo_x100=0;pads.deck[1].bpm_x100=17400;
 synced(&pads.deck[1],2,XZ_EXTRA_SYNC_NO_GRID,0);
 render(&pads,XZ_FOUR_STACKED,-1,0);save(argv[2],"03-pads-rev-slip-reset-no-grid");
 /* Deck assignment: the left side drives DECK 3 (DECK 1 tag unlit), then both sides switched. */
 struct xz_extra_decks left=full;left.left_on_3=1;
 render(&left,XZ_FOUR_STACKED,-1,0);save(argv[2],"04-left-controls-deck3");
 struct xz_extra_decks both=full;both.left_on_3=both.right_on_4=1;
 render(&both,XZ_FOUR_MIXER,-1,0);save(argv[2],"05-both-sides-switched-mixer");
 /* Loops: DECK 3 adjusting LOOP IN with BEAT JUMP pads; DECK 4 a stored loop RELOOP returns to, CALL pads. */
 struct xz_extra_decks loops=full;
 loops.deck[0].pad_mode=XZ_EXTRA_PAD_BEAT_JUMP;loops.deck[0].loop.active=loops.deck[0].loop.stored=1;loops.deck[0].loop.edit=1;
 loops.deck[0].loop.in_ms=61200;loops.deck[0].loop.out_ms=61200+4*60000/128;
 loops.deck[1].pad_mode=XZ_EXTRA_PAD_CALL;loops.deck[1].loop.stored=1;loops.deck[1].loop.in_ms=120700;loops.deck[1].loop.out_ms=120700+8*60000/174;
 render(&loops,XZ_FOUR_STACKED,-1,0);save(argv[2],"06-loop-in-adjust-and-stored-loop");
 /* No track on DECK 3; DECK 4 with every readout unknown except the title. */
 struct xz_extra_decks unknown=full;memset(&unknown.deck[0],0,sizeof unknown.deck[0]);
 unknown.deck[1].known=XZ_EXTRA_KNOWN(XZ_EXTRA_F_TITLE);
 render(&unknown,XZ_FOUR_STACKED,-1,0);save(argv[2],"07-no-track-and-unknown-fields");
 /* Long and accented titles clip inside the plate. */
 struct xz_extra_decks titles=full;
 snprintf(titles.deck[0].title,sizeof(titles.deck[0].title),"An Extraordinarily Long Title That Never Seems To End (Twelve Inch Club Remix)");
 snprintf(titles.deck[1].title,sizeof(titles.deck[1].title),"Ünïcödé Tïtlé Wïth Äccents And Plenty More Words");
 render(&titles,XZ_FOUR_STACKED,-1,0);save(argv[2],"08-long-titles");
 /* Detail panel: DECK 3, paused so the strip can search, left controls on DECK 3. */
 struct xz_extra_decks panel=full;panel.left_on_3=1;panel.deck[0].playing=0;
 render(&panel,XZ_FOUR_STACKED,0,XZ_EXTRA_DETAIL_NOTICE_NONE);save(argv[2],"09-detail-deck3-master-paused");
 /* DECK 4 detail in MIXER order: locked to DECK 3, BEAT LOOP 1/2 running. */
 struct xz_extra_decks panel4=full;
 panel4.deck[1].pad_mode=XZ_EXTRA_PAD_BEAT_LOOP;panel4.deck[1].active_pad=1;
 panel4.deck[1].loop.active=panel4.deck[1].loop.stored=1;panel4.deck[1].loop.in_ms=120100;panel4.deck[1].loop.out_ms=120100+30000/174;
 render(&panel4,XZ_FOUR_MIXER,1,XZ_EXTRA_DETAIL_NOTICE_NONE);save(argv[2],"10-detail-deck4-locked-mixer");
 /* A needle search refused while DECK 3 plays: NEEDLE LOCK in the panel's status row. */
 render(&full,XZ_FOUR_STACKED,0,XZ_EXTRA_DETAIL_NOTICE_NEEDLE_PLAYING);save(argv[2],"11-detail-needle-lock");
 /* DECK 4 empty: how to load it from the classic browser, the right side's DECK SELECT. */
 struct xz_extra_decks empty=full;memset(&empty.deck[1],0,sizeof empty.deck[1]);
 empty.deck[1].known=XZ_EXTRA_KNOWN(XZ_EXTRA_F_VINYL)|XZ_EXTRA_KNOWN(XZ_EXTRA_F_MASTER_TEMPO)|XZ_EXTRA_KNOWN(XZ_EXTRA_F_QUANTIZE)|
  XZ_EXTRA_KNOWN(XZ_EXTRA_F_TIME_MODE);
 empty.deck[1].vinyl=empty.deck[1].quantize=empty.deck[1].remain=1;
 render(&empty,XZ_FOUR_STACKED,1,XZ_EXTRA_DETAIL_NOTICE_NONE);save(argv[2],"12-detail-deck4-empty-load-help");
 /* Following a native master: tempo only, the native beat unavailable; then an external master. */
 struct xz_extra_decks native_tempo=full;native_tempo.deck[0].master=0;native_tempo.deck[0].master_scope=0;
 synced(&native_tempo.deck[0],1,XZ_EXTRA_SYNC_NATIVE_PHASE_UNAVAILABLE,XZ_EXTRA_MASTER_SCOPE_NATIVE);
 synced(&native_tempo.deck[1],-1,XZ_EXTRA_SYNC_NATIVE_MASTER_UNSUPPORTED,0);
 render(&native_tempo,XZ_FOUR_STACKED,0,XZ_EXTRA_DETAIL_NOTICE_NONE);save(argv[2],"13-detail-native-tempo-only");
 render(&native_tempo,XZ_FOUR_STACKED,1,XZ_EXTRA_DETAIL_NOTICE_NONE);save(argv[2],"14-detail-external-master");
 /* Player notice: the banner with the panel closed, the panel's status row while open. */
 struct xz_extra_decks notice=left;notice.notice_seq=1;notice.notice_at_ms=NOW;
 snprintf(notice.notice,sizeof notice.notice,"LOOP IN ADJUST");
 render(&notice,XZ_FOUR_STACKED,-1,0);save(argv[2],"15-player-notice-banner");
 render(&notice,XZ_FOUR_STACKED,0,0);save(argv[2],"16-player-notice-in-panel");
 /* Route lost: two native decks again, the notice still shown. */
 memcpy(surface,native,sizeof(surface));
 snprintf(notice.notice,sizeof notice.notice,"DECK 3/4 ROUTE LOST");notice.route_up=0;
 xz_extra_notice_draw(surface,XZ_WAVE_WIDTH,PIXELS,&notice,NOW);
 save(argv[2],"17-route-lost-two-native-decks");
 render(&loops,XZ_FOUR_STACKED,1,0);save(argv[2],"18-detail-hot-cue-call");
 puts("wrote 18 scenarios");
 return 0;
}
