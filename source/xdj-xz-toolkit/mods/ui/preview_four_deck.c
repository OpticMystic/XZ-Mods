/* SPDX-License-Identifier: MIT */
/* Offline four-deck classic wave surface: a native two-deck 536x268 RGB565 surface in, the four-deck
   surface out for each fixture scenario, plus a host draw-cost bench. Driven by preview_four_deck.py. */
#define _POSIX_C_SOURCE 200809L
#include "extra_lane.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PIXELS (XZ_WAVE_WIDTH*XZ_WAVE_HEIGHT)
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
 for(uint32_t i=0;i<t->beats;i++)t->grid[i]=(struct xz_extra_beat){(int32_t)(120+i*beat),(uint8_t)(i%4+1)};
}
static void fill_deck(struct xz_extra_deck *d,const struct track *t,const char *title,int32_t elapsed,int32_t bpm,int32_t tempo,int playing){
 memset(d,0,sizeof(*d));
 d->known=~0u&((1u<<XZ_EXTRA_FIELD_COUNT)-1);d->loaded=1;
 snprintf(d->title,sizeof(d->title),"%s",title);snprintf(d->artist,sizeof(d->artist),"Fixture");
 d->source=1;d->elapsed_ms=elapsed;d->total_ms=(int32_t)t->span_ms;d->elapsed_at_ms=1000;d->playing=playing;
 d->bpm_x100=bpm*(10000+tempo)/10000;d->track_bpm_x100=bpm;d->tempo_x100=tempo;
 d->tempo_range=10;d->master_tempo=1;d->vinyl=1;
 snprintf(d->key,sizeof(d->key),"8A");
 d->cue_ms=elapsed-900;
 static const uint32_t colors[3]={0x30d050,0xe03040,0x3080ff};
 for(int i=0;i<3;i++)d->hot_cue[i]=(struct xz_extra_cue){1,elapsed-1400+i*1100,colors[i],0};
 d->grid=t->grid;d->grid_count=t->beats;
 d->wave=(struct xz_extra_wave){t->columns,t->count,t->span_ms,t->normalization};
}

static void render(const struct xz_extra_decks *decks,int order){
 memcpy(surface,native,sizeof(surface));
 xz_four_layout_pixels(surface,PIXELS,XZ_WAVE_WIDTH,order);
 struct xz_four_layout g;xz_four_layout_get(order,&g);
 for(int i=0;i<2;i++){
  struct xz_extra_lane_view view={1000,XZ_EXTRA_LANE_COLUMN_US,1,i?decks->right_on_4:decks->left_on_3};
  xz_extra_lane_draw(surface,XZ_WAVE_WIDTH,PIXELS,g.lane_y[2+i],3+(unsigned)i,&decks->deck[i],&view,&marks[i]);
 }
 struct xz_four_marks m={0};
 for(int i=0;i<2;i++){m.look[2+i]=marks[i].look;m.col[2+i]=marks[i].col;}
 xz_four_protect(&g,&m,surface,keep);
 xz_four_restore(&g,&m,surface,keep);
 xz_extra_notice_draw(surface,XZ_WAVE_WIDTH,PIXELS,decks,1000);
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
 fill_deck(&full.deck[0],&a,"Night Drive (Extended Mix)",61500,12800,250,1);
 fill_deck(&full.deck[1],&b,"Liquid Steps",120200,17400,-120,0);
 full.deck[0].master=1;
 if(argc>3){
  /* Best of five rounds per step, so a busy host does not inflate one number. */
  int frames=atoi(argv[3]);double best[5]={1e9,1e9,1e9,1e9,1e9};
  struct xz_four_layout g;xz_four_layout_get(XZ_FOUR_ORDER,&g);
  struct xz_four_marks two={0},four={0};
  for(int round=0;round<5;round++){
   double t0=seconds();
   for(int i=0;i<frames;i++)memcpy(surface,native,sizeof(surface));
   double copy=seconds()-t0;t0=seconds();
   for(int i=0;i<frames;i++){memcpy(surface,native,sizeof(surface));xz_four_layout_pixels(surface,PIXELS,XZ_WAVE_WIDTH,XZ_FOUR_ORDER);}
   double step[5]={copy,seconds()-t0-copy,0,0,0};t0=seconds();
   for(int i=0;i<frames;i++)for(int k=0;k<2;k++){
    struct xz_extra_lane_view view={1000+(uint32_t)i*16,XZ_EXTRA_LANE_COLUMN_US,1,0};
    xz_extra_lane_draw(surface,XZ_WAVE_WIDTH,PIXELS,g.lane_y[2+k],3+(unsigned)k,&full.deck[k],&view,&marks[k]);
   }
   step[2]=seconds()-t0;
   for(int i=0;i<2;i++){two.look[2+i]=four.look[2+i]=four.look[i]=marks[i].look;two.col[2+i]=four.col[2+i]=four.col[i]=marks[i].col;}
   t0=seconds();
   for(int i=0;i<frames;i++){xz_four_protect(&g,&two,surface,keep);xz_four_restore(&g,&two,surface,keep);}
   step[3]=seconds()-t0;t0=seconds();
   for(int i=0;i<frames;i++){xz_four_protect(&g,&four,surface,keep);xz_four_restore(&g,&four,surface,keep);}
   step[4]=seconds()-t0;
   for(int s=0;s<5;s++)if(step[s]<best[s])best[s]=step[s];
  }
  printf("host us/frame, best of 5 x %d frames: surface copy %.1f, four layout %.1f, two extra lanes %.1f, "
   "theme protect+restore DECK 3/4 %.1f, all four lanes layered %.1f\n",frames,
   best[0]*1e6/frames,best[1]*1e6/frames,best[2]*1e6/frames,best[3]*1e6/frames,best[4]*1e6/frames);
  return 0;
 }
 render(&full,XZ_FOUR_INTERLEAVED);save(argv[2],"01-full-interleaved");
 render(&full,XZ_FOUR_STACKED);save(argv[2],"02-full-stacked");
 struct xz_extra_decks empty={.route_up=1};
 render(&empty,XZ_FOUR_INTERLEAVED);save(argv[2],"03-unloaded-interleaved");
 struct xz_extra_decks loop=full;
 loop.deck[0].master=0;loop.deck[1].master=1;loop.deck[1].playing=1;
 loop.deck[1].loop.active=1;loop.deck[1].loop.in_ms=119500;loop.deck[1].loop.out_ms=119500+4*60000/174;
 render(&loop,XZ_FOUR_INTERLEAVED);save(argv[2],"04-deck4-master-loop");
 struct xz_extra_decks titles=full;
 snprintf(titles.deck[0].title,sizeof(titles.deck[0].title),"An Extraordinarily Long Title That Never Seems To End (Twelve Inch Club Remix)");
 snprintf(titles.deck[1].title,sizeof(titles.deck[1].title),"Ünïcödé Tïtlé Wïth Äccents And Plenty More Words");
 render(&titles,XZ_FOUR_INTERLEAVED);save(argv[2],"05-long-titles");
 struct xz_extra_decks lit=full;lit.left_on_3=1;
 render(&lit,XZ_FOUR_INTERLEAVED);save(argv[2],"06-left-on-3-lit");
 struct xz_extra_decks unknown=full;
 unknown.deck[0].known=XZ_EXTRA_KNOWN(XZ_EXTRA_F_TITLE);unknown.deck[1].known=XZ_EXTRA_KNOWN(XZ_EXTRA_F_ELAPSED);
 render(&unknown,XZ_FOUR_INTERLEAVED);save(argv[2],"07-unknown-fields");
 struct xz_extra_decks bothlit=loop;bothlit.left_on_3=bothlit.right_on_4=1;
 render(&bothlit,XZ_FOUR_STACKED);save(argv[2],"08-stacked-master-loop-lit");
 struct xz_extra_decks notice=lit;notice.notice_seq=1;notice.notice_at_ms=1000;
 snprintf(notice.notice,sizeof notice.notice,"LOOP IN ADJUST");
 render(&notice,XZ_FOUR_INTERLEAVED);save(argv[2],"09-loop-adjust-notice");
 memcpy(surface,native,sizeof(surface));
 snprintf(notice.notice,sizeof notice.notice,"DECK 3/4 ROUTE LOST");notice.route_up=0;
 xz_extra_notice_draw(surface,XZ_WAVE_WIDTH,PIXELS,&notice,1000);
 save(argv[2],"10-route-lost-two-native-decks");
 puts("wrote 10 scenarios");
 return 0;
}
