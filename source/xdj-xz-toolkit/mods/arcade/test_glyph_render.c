#define _GNU_SOURCE
#include "rhythm_lab.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <sys/syscall.h>
#include <unistd.h>
static struct xz_rhythm_lab g;
static uint16_t frame[800*480+2];
static double millis(void){struct timespec t;assert(!syscall(SYS_clock_gettime,CLOCK_MONOTONIC,&t));return t.tv_sec*1000.+t.tv_nsec*.000001;}
static double cpu_millis(void){struct timespec t;assert(!syscall(SYS_clock_gettime,CLOCK_PROCESS_CPUTIME_ID,&t));return t.tv_sec*1000.+t.tv_nsec*.000001;}
static void save(const char *dir,const char *name){if(!dir)return;char path[1000];snprintf(path,sizeof(path),"%s/%s.ppm",dir,name);FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n800 480\n255\n");for(unsigned n=1;n<=800*480;n++){unsigned v=frame[n];unsigned char rgb[3]={(v>>11)*255/31,((v>>5)&63)*255/63,(v&31)*255/31};fwrite(rgb,1,3,f);}fclose(f);}
static void draw(void){frame[0]=0x1234;frame[800*480+1]=0xabcd;assert(xz_glyph_render(&g,frame+1,800*480,800));assert(frame[0]==0x1234&&frame[800*480+1]==0xabcd);}
int main(int argc,char **argv){
 const char *out=argc>1&&strcmp(argv[1],"--render-only")?argv[1]:NULL;xz_lab_init(&g,1000);g.mode=LAB_GLYPH;g.valid=g.playing=g.wave.valid=1;g.wave.count=2400;g.wave.total=60000;g.wave.normalization=500;g.wave.pulse_confidence=.9;g.wave.kind=1;g.position=800;g.origin=0;g.period=75;g.rate=.15;g.duration=120;g.practice_until=300;g.source_position=g.position;
 strcpy(g.wave.title,"STATIC SURFER / WAVEFORM FIXTURE");for(unsigned n=0;n<2400;n++){g.wave.bands[n*3]=n%75<10?220:8;g.wave.bands[n*3+1]=(uint8_t)(120+100*sinf(n*.01));g.wave.bands[n*3+2]=(uint8_t)(120+100*cosf(n*.015));}
 g.phase=LAB_BRIEF;g.origin=g.position+100;draw();g.origin=0;draw();save(out,"static-surfer-title");g.phase=LAB_RUN;
 double total=0,worst=0,cpu_total=0,cpu_worst=0;unsigned frames=0;
 for(unsigned duo=0;duo<2;duo++)for(unsigned world=0;world<3;world++){
  g.duo=duo;g.elapsed=world*40+12;g.music[0]=.7;g.music[1]=.45;g.music[2]=.6;
  g.event_count=3;for(unsigned i=0;i<3;i++){g.events[i]=(struct xz_lab_event){.at=g.position+(i*1.8f+.6f)*g.period,.end=g.position+(i*1.8f+1.12f)*g.period,.energy=.3f+i*.2f,.angle=i==0?-.62f:.62f,.kind=LAB_GLYPH_SPIKE+i};}
  for(unsigned p=0;p<2;p++){g.glyph[p].party_at=g.position-g.period*.75;g.glyph[p].height=73-p*18;g.glyph[p].velocity=100;g.glyph[p].jump_at=g.position-g.period*.4;g.player[p].angle=p?.62f:-.62f;g.player[p].streak=8+world*4;g.player[p].score=1300+world*2100;g.player[p].feedback="THREADED THE GLYPH";g.player[p].feedback_until=g.position+500;}
  for(unsigned f=0;f<8;f++){g.position+=2;g.pulse=(8-f)*.125f;double cpu_before=cpu_millis(),before=millis();draw();double cost=millis()-before,cpu_cost=cpu_millis()-cpu_before;cpu_total+=cpu_cost;if(cpu_cost>cpu_worst)cpu_worst=cpu_cost;total+=cost;if(cost>worst)worst=cost;frames++;}
  char name[80];snprintf(name,sizeof(name),"static-surfer-%s-%u",duo?"duo":"solo",world);save(out,name);
 }
 g.phase=LAB_PAUSE;g.wave.position=g.wave.total-1;draw();save(out,"static-surfer-ended");
 printf("STATIC_SURFER_RENDER frames=%u mean_ms=%.3f worst_ms=%.3f cpu_mean_ms=%.3f cpu_worst_ms=%.3f\n",frames,total/frames,worst,cpu_total/frames,cpu_worst);fflush(stdout);return 0;
}
