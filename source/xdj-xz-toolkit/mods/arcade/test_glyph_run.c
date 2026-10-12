#include "rhythm_lab.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static struct xz_rhythm_lab g;
static struct xz_wave_rider_snapshot w;
static uint8_t music[60000*3];
static void feed(unsigned ms){assert(!xz_wave_rider_window(&w,music,60000,600+ms*.15));w.observed_ms=1000+ms;xz_lab_feed(&g,&w,1000+ms);xz_lab_step(&g,1000+ms);}
int main(void){
 for(unsigned n=0;n<60000;n++){music[n*3]=(uint8_t)(n%75<10?230-n%75*20:4);music[n*3+1]=(uint8_t)(100+90*sinf(n*.007f));music[n*3+2]=(uint8_t)(100+85*sinf(n*.013f));}
 xz_lab_init(&g,1000);g.mode=LAB_GLYPH;g.duo=1;w.valid=w.playing=1;w.normalization=500;w.pulse_period=75;w.pulse_confidence=1;w.track_hash=73;w.kind=1;feed(0);xz_lab_begin(&g,1000);assert(g.duration==120);
 unsigned kinds=0;float maxheight=0;
 for(unsigned ms=20;ms<124000;ms+=20){
  feed(ms);
  for(unsigned n=0;n<g.event_count;n++){
   struct xz_lab_event *e=&g.events[n];kinds|=1u<<(e->kind-LAB_GLYPH_SPIKE);
   if(e->grade[0]!=LAB_PENDING)continue;
   if(e->at>=g.position-g.period*.05&&e->at<g.position+g.period*.05&&fabs(g.glyph[0].jump_at-e->at)>g.period){
    float pulses=(e->angle-g.player[0].angle)*g.turn_pulses[0]/6.2831853f;xz_lab_jog(&g,0,pulses,g.now);
    xz_lab_button(&g,0,LAB_PAD_A,1,g.now);xz_lab_button(&g,0,LAB_PAD_A,0,g.now);
   }
  }
  assert(isfinite(g.glyph[0].height)&&g.glyph[0].height>=0&&g.glyph[0].height<140);
  if(g.glyph[0].height>maxheight)maxheight=g.glyph[0].height;
  if(g.phase==LAB_RESULTS)break;
 }
 printf("GLYPH run %.2fs hits=%u misses=%u no_input_misses=%u gates=%u landings=%u height=%.1f kinds=%u\n",g.elapsed,g.player[0].hits,g.player[0].misses,g.player[1].misses,g.glyph[0].gates,g.glyph[0].landings,maxheight,kinds);
 assert(g.phase==LAB_RESULTS&&g.elapsed>=120&&g.elapsed<120.1&&g.player[0].hits>75&&g.player[0].misses<8&&g.player[1].misses>50&&g.glyph[0].gates>20&&kinds==7);
 xz_lab_jog(&g,0,100000,g.now);assert(g.player[0].angle<=1.05f);xz_lab_jog(&g,0,-100000,g.now);assert(g.player[0].angle>=-1.05f);
 puts("PASS music-derived traversal, reachable jumps/gates/gaps, bounded rotation, two independent players and complete120-second run");
 return 0;
}
