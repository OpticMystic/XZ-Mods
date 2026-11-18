#include "wave_rider.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static void fixture(struct xz_rider *g,float pitch,int air){
 xz_rider_init(g,0,1000);g->difficulty=XZ_RIDER_NORMAL;g->valid=1;g->phase=XZ_RIDER_RIDE;g->spin_lift=0;g->wave.valid=g->wave.playing=1;g->wave.count=2400;g->wave.total=60000;g->wave.pulse_period=75;g->wave.observed_ms=g->feed_ms=1000;g->source_rate=.15f;g->position=g->sim_at=g->source_position=500;
 for(unsigned i=0;i<2400;i++){float x=(float)i-515,h=80*expf(-x*x/2500.f);g->course.height[i]=h;g->course.slope[i]=-x*h/2500;g->course.curvature[i]=h*(x*x/6250000.f-1.f/5000);}
 struct xz_rider_player *p=&g->player[0];p->at=500;p->vx=420;p->vy=0;p->grounded=!air;p->pitch=p->pitch_target=pitch;p->ground_lean=fmaxf(-.4f,fminf(.42f,pitch-atanf(xz_rider_surface_at(g,p->at).slope)));p->jog_seen=1;p->altitude=xz_rider_surface_at(g,500).height+7+(air?160:0);p->launch_at=470;p->flight_peak=40;p->highwater=499;
}
static void advance(struct xz_rider *g,unsigned steps){uint32_t now=g->last_ms+(unsigned)(steps*1000/150.f);g->source_position=g->sim_at+steps;g->feed_ms=now;xz_rider_step(g,now);}
int main(void){
 struct xz_rider down,up,level;fixture(&down,-.65f,0);fixture(&up,.65f,0);advance(&down,20);advance(&up,20);
 printf("CREST down_launches=%u up_launches=%u down_grounded=%u\n",down.player[0].launches,up.player[0].launches,down.player[0].grounded);fflush(stdout);
 assert(!down.player[0].launches&&down.player[0].grounded&&up.player[0].launches);
 fixture(&level,.65f,0);for(unsigned i=0;i<2400;i++){level.course.height[i]=400+((float)i-500)*2;level.course.slope[i]=1;level.course.curvature[i]=-.02f;}level.player[0].altitude=407;advance(&level,1);assert(level.player[0].grounded&&!level.player[0].launches);
 fixture(&down,-.7f,1);fixture(&level,0,1);fixture(&up,.7f,1);advance(&down,25);advance(&level,25);advance(&up,25);
 printf("FLIGHT down_vy=%.1f level_vy=%.1f up_vy=%.1f\n",down.player[0].vy,level.player[0].vy,up.player[0].vy);assert(down.player[0].vy<level.player[0].vy-10&&up.player[0].vy>level.player[0].vy+10);
 fixture(&down,.7f,1);fixture(&up,.7f,1);down.player[0].vx=150;up.player[0].vx=650;advance(&down,25);advance(&up,25);assert(up.player[0].vy>down.player[0].vy+10);
 fixture(&down,-.7f,1);down.player[0].held=1;advance(&down,25);assert(down.player[0].vy<level.player[0].vy-150);
 fixture(&down,-.65f,0);for(int i=0;i<30;i++)advance(&down,10);assert(down.player[0].ground_lean==-.4f);
 fixture(&down,.7f,1);fixture(&up,atanf(-.45f),1);
 for(unsigned i=0;i<2400;i++){down.course.height[i]=up.course.height[i]=400-((float)i-500)*.9f;down.course.slope[i]=up.course.slope[i]=-.45f+XZ_RIDER_GRADE;down.course.curvature[i]=up.course.curvature[i]=0;}
 down.player[0].altitude=up.player[0].altitude=xz_rider_surface_at(&down,500).height+7.2f;down.player[0].vx=up.player[0].vx=380;down.player[0].vy=up.player[0].vy=-240;advance(&down,3);advance(&up,3);
 printf("LANDING aligned_vx=%.1f bad_vx=%.1f streaks=%u/%u\n",up.player[0].vx,down.player[0].vx,up.player[0].streak,down.player[0].streak);assert(up.player[0].landings&&up.player[0].vx>down.player[0].vx*1.2f&&up.player[0].streak>down.player[0].streak);
 puts("PASS angle controls takeoff, signed airborne lift, optional dive, sustained heading and landing momentum");return 0;
}
