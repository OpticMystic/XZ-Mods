/* SPDX-License-Identifier: MIT */
#include "glyph_run.h"
#include "rhythm_lab.h"
#include <math.h>
#include <string.h>
static void say(struct xz_rhythm_lab *g,unsigned i,const char *message){g->player[i].feedback=message;g->player[i].feedback_until=g->position+g->period*1.4;}
void xz_glyph_begin(struct xz_rhythm_lab *g){memset(g->glyph,0,sizeof(g->glyph));for(unsigned i=0;i<2;i++){g->glyph[i].jump_at=-10000;g->glyph[i].landing_at=-10000;g->glyph[i].party_at=-10000;}}
void xz_glyph_jump(struct xz_rhythm_lab *g,unsigned i){
    if(i>=xz_lab_players(g))return;struct xz_glyph_player *s=&g->glyph[i];
    if(s->height>1){s->buffer=.14f;return;}
    double error=1e9;
    for(unsigned n=0;n<g->event_count;n++){double d=g->position-g->events[n].at;if(fabs(d)<fabs(error))error=d;}
    float boost=fmaxf(0,1-(float)fabs(error)/(g->period*.16f));
    s->velocity=XZ_GLYPH_JUMP_SPEED*(1+.08f*boost);s->height=.01f;s->jump_at=g->position;s->jumps++;s->buffer=0;s->launch_strength=boost;
    g->player[i].last_error_ms=fabs(error)<100000?(float)(error/g->rate):0;
    xz_game_emit(&s->effects,boost>.5f?XZ_IMPACT_BOOST:XZ_IMPACT_LAUNCH,g->position,g->position,0,.5f+.5f*boost,g->player[i].angle);
    if(fabs(error)<g->period*.22) xz_game_emit(&s->effects,XZ_IMPACT_ARROW,g->position,g->position-error,0,.5f+.5f*boost,0);
    say(g,i,fabs(error)<g->period*.14?"ON THE ACCENT":"JUMP");
}
void xz_glyph_step(struct xz_rhythm_lab *g,float source_dt){
    float dt=source_dt/g->period;if(dt<0)dt=0;if(dt>.25f)dt=.25f;
    for(unsigned i=0;i<xz_lab_players(g);i++){
        struct xz_glyph_player *s=&g->glyph[i];struct xz_lab_player *p=&g->player[i];
        s->flash=fmaxf(0,s->flash-dt*3);s->buffer=fmaxf(0,s->buffer-dt);
        if(s->height>0||s->velocity>0){
            s->height+=s->velocity*dt-.5f*XZ_GLYPH_GRAVITY*dt*dt;s->velocity-=XZ_GLYPH_GRAVITY*dt;
            if(s->height<=0){s->landing_impact=fminf(1,fabsf(s->velocity)/450.f);s->height=s->velocity=0;s->landings++;s->landing_at=g->position;
                double phase=(g->position-g->origin)/g->period;
                s->landing_quality=fmaxf(0,1-(float)fabs(phase-round(phase))/.18f);
                xz_game_emit(&s->effects,s->landing_quality>.5f?XZ_IMPACT_PERFECT_LAND:XZ_IMPACT_LAND,g->position,g->position,0,s->landing_impact,0);
                if(g->position>s->safe_until&&fabs(phase-round(phase))<.18){p->score+=25;say(g,i,"CLEAN LANDING");}
                if(s->buffer>0)xz_glyph_jump(g,i);
            }
        }
        for(unsigned n=0;n<g->event_count;n++){
            struct xz_lab_event *e=&g->events[n];if(e->grade[i]!=LAB_PENDING||g->position<e->end)continue;
            int angle=e->kind!=LAB_GLYPH_GATE||fabsf(p->angle-e->angle)<.29f;
            int height=e->kind==LAB_GLYPH_GATE?s->height>=36&&s->height<=130:e->kind==LAB_GLYPH_GAP?s->height>25:s->height>30+e->energy*28;
            int good=height&&angle&&g->position>=s->safe_until;
            double timing=s->jump_at-e->at;p->last_error_ms=(float)(timing/g->rate);
            if(good){
                int perfect=fabs(timing)<g->period*.14;e->grade[i]=perfect?LAB_PERFECT:LAB_GOOD;
                p->hits++;p->motion_hits++;if(perfect){p->perfects++;p->timing_hits++;}
                p->streak++;if(p->streak%4==0)s->party_at=g->position;if(p->streak>p->best_streak)p->best_streak=p->streak;
                p->score+=(perfect?150:100)+(p->streak>12?12:p->streak)*10;
                if(e->kind==LAB_GLYPH_GATE){s->gates++;xz_game_emit(&s->effects,XZ_IMPACT_SHATTER,g->position,e->end,111,.7f+.3f*perfect,e->angle);}
                say(g,i,e->kind==LAB_GLYPH_GATE?"CRYSTAL BREAK":perfect?"ACCENT / CLEAR":"CLEAR");
            }else{
                xz_game_emit(&s->effects,XZ_IMPACT_GLANCE,g->position,e->end,s->height+28,.8f,p->angle);
                e->grade[i]=LAB_MISSED;p->misses++;p->streak=0;s->collisions++;s->flash=1;
                s->safe_until=g->position+g->period*.7;s->height=0;s->velocity=0;s->buffer=0;
                say(g,i,!height?"JUMP FROM THE GLOWING MARK":!angle?"MATCH THE GATE TILT":"NEXT ACCENT");
            }
        }
    }
}
