/* SPDX-License-Identifier: MIT */
#include "wave_rider.h"
#include <math.h>
#include <string.h>

static float limit(float v,float lo,float hi){return v<lo?lo:v>hi?hi:v;}
static unsigned lower(unsigned i,unsigned radius){return i>radius?i-radius:0;}
static unsigned upper(unsigned i,unsigned radius,unsigned n){return i+radius+1<n?i+radius+1:n;}
static unsigned norm(const struct xz_wave_rider_snapshot *w){return w->normalization<64?64:w->normalization;}

/* Equal maxima pick the earliest sample, making plateaus one onset. */
static int peak(const uint16_t *rise,unsigned i,unsigned radius,unsigned n){
    for(unsigned j=lower(i,radius);j<upper(i,radius,n);j++)
        if(rise[j]>rise[i]||(j<i&&rise[j]==rise[i]))return 0;
    return 1;
}
void xz_rider_analyze(const struct xz_wave_rider_snapshot *w,struct xz_rider_course *c){
    memset(c,0,sizeof(*c));
    if(!w||!w->valid||!w->count||w->count>XZ_WAVE_RIDER_SAMPLES)return;
    uint32_t sum[XZ_WAVE_RIDER_SAMPLES+1]={0},harm[XZ_WAVE_RIDER_SAMPLES+1]={0};
    uint16_t kick[XZ_WAVE_RIDER_SAMPLES]={0},voice[XZ_WAVE_RIDER_SAMPLES]={0};
    unsigned n=w->count,scale=norm(w);
    for(unsigned i=0;i<n;i++){
        const uint8_t *b=w->bands+i*3;
        sum[i+1]=sum[i]+b[0]+b[1]+b[2];harm[i+1]=harm[i]+b[1];
        unsigned m0=b[0],m2=b[2];
        for(unsigned j=lower(i,8);j<i;j++){
            if(w->bands[j*3]<m0)m0=w->bands[j*3];
            if(w->bands[j*3+2]<m2)m2=w->bands[j*3+2];
        }
        kick[i]=(uint16_t)(b[0]-m0);voice[i]=(uint16_t)(b[2]-m2);
    }
    unsigned run=0;
    for(unsigned i=0;i<n;i++){
        unsigned lo=lower(i,12),hi=upper(i,12,n);
        float swell=(float)(sum[hi]-sum[lo])/(float)(hi-lo);
        c->swell[i]=(uint16_t)limit(swell*200.f/scale,0,200);
        lo=lower(i,20);hi=upper(i,20,n);
        float middle=(float)(harm[hi]-harm[lo])/(float)(hi-lo);
        int slip=middle>=.22f*scale&&middle>=swell*.4f;
        if(slip){c->slip[i]=1;run++;}
        if(!slip||i+1==n){
            unsigned end=slip?i+1:i;
            if(run<45)for(unsigned j=end-run;j<end;j++)c->slip[j]=0;
            run=0;
        }
    }
    for(unsigned i=0;i<n&&c->object_count<XZ_RIDER_OBJECTS;i++){
        /* The window provides 300 samples of history. Ignore its artificial
           edge; every playable and visible future event has its full context. */
        if((w->first&&i<40)||(w->first+n<w->total&&i+40>=n))continue;
        if(kick[i]>=10&&kick[i]>=.18f*scale&&peak(kick,i,30,n)){
            float height=limit(kick[i]*90.f/scale,24,76);
            c->objects[c->object_count++]=(struct xz_rider_object){w->first+i,c->swell[i]+height,XZ_RIDER_SPIKE};
            c->spikes++;
        }
        if(c->object_count==XZ_RIDER_OBJECTS)break;
        if(voice[i]>=10&&voice[i]>=.14f*scale&&peak(voice,i,24,n)){
            int drum=0;
            for(unsigned j=lower(i,10);j<upper(i,10,n);j++)
                if(kick[j]>=10&&kick[j]>=.18f*scale&&peak(kick,j,30,n)){drum=1;break;}
            if(!drum){
                float altitude=limit(c->swell[i]+30.f+w->bands[i*3+2]*90.f/scale,30,272);
                c->objects[c->object_count++]=(struct xz_rider_object){w->first+i,altitude,XZ_RIDER_GEM};
                c->gems++;
            }
        }
    }
}
float xz_rider_swell(const struct xz_rider *g,double at){
    if(!g->valid||at<g->wave.first||at>=g->wave.first+g->wave.count)return 0;
    return g->course.swell[(unsigned)(at-g->wave.first)];
}
static int slip_at(const struct xz_rider *g,double at){
    if(!g->valid||at<g->wave.first||at>=g->wave.first+g->wave.count)return 0;
    return g->course.slip[(unsigned)(at-g->wave.first)]!=0;
}
static void say(struct xz_rider *g,const char *text){g->feedback=text;g->feedback_until=g->position+100;}
static float height_at(const struct xz_rider *g,double at){
    double age=at-g->hop_at;
    /* A pulse jump lifts on the hit, then glides down over 360 ms of track.
       CUE on the drum onset is rewarded; no off-beat pre-jump is required. */
    float hop=age>=0&&age<54?92.f*(1.f-(float)(age*age/(54.*54.))):0;
    return limit(g->cruise+hop,12,290);
}
static void reward(struct xz_rider *g,unsigned points,const char *text){
    g->combo++;if(g->combo>g->best)g->best=g->combo;
    unsigned multiplier=1+g->combo/8;if(multiplier>8)multiplier=8;
    g->score+=points*multiplier*(g->slipped?2:1);say(g,text);
}
static void scrape(struct xz_rider *g){
    if(g->position<g->shield_until)return;
    g->scrapes++;g->combo=0;g->shield_until=g->position+45;say(g,"SCRAPE / KEEP RIDING");
}
static void judge_spike(struct xz_rider *g,double at,float altitude){
    double timing=at-g->hop_at;
    if(timing>=-10&&timing<=38){
        g->hops++;
        if(fabs(timing)<=10){g->perfects++;reward(g,100,"PERFECT HOP");}
        else reward(g,40,"HOP");
    }else if(height_at(g,at)<altitude+6)scrape(g);
    else say(g,"SAFE / HOP TO SCORE");
    g->judged++;
}
void xz_rider_init(struct xz_rider *g,unsigned deck,uint32_t now){
    memset(g,0,sizeof(*g));g->deck=deck<2?deck:0;g->initialized=1;
    g->phase=XZ_RIDER_SETUP;g->last_ms=now;g->hop_at=-1000;g->highwater=-1;
    g->cruise=g->target=g->altitude=110;g->feedback="YOUR TRACK IS THE LEVEL";
}
void xz_rider_begin(struct xz_rider *g,uint32_t now){
    if(!g->valid)return;
    if(g->wave.position>=g->wave.total-1&&g->wave.total>1){g->leave=1;return;}
    g->score=g->combo=g->best=g->perfects=g->hops=g->catches=g->scrapes=g->judged=0;
    g->spike_total=g->gem_total=0;g->pending_spike=0;g->hop_at=-1000;
    g->position=g->previous=g->highwater=g->wave.position;g->last_ms=now;
    g->cruise=g->target=limit(xz_rider_swell(g,g->position)+38,20,265);
    g->altitude=g->cruise;g->trail_count=g->trail_head=0;g->shield_until=g->position+90;
    g->phase=g->wave.playing?XZ_RIDER_RIDE:XZ_RIDER_HOLD;say(g,"JOG TO GLIDE / CUE ON THE HIT");
}
void xz_rider_feed(struct xz_rider *g,const struct xz_wave_rider_snapshot *w,uint32_t now){
    if(!w)return;
    int ok=w->valid&&w->count&&w->count<=XZ_WAVE_RIDER_SAMPLES&&
        w->first<=w->total&&w->count<=w->total-w->first&&isfinite(w->position)&&
        w->position>=0&&w->position<=w->total&&now-w->observed_ms<=XZ_WAVE_RIDER_FRESH_MS;
    int changed=g->course_generation&&ok&&(g->course_hash!=w->track_hash||g->course_generation!=w->generation||g->course_kind!=w->kind);
    g->wave=*w;g->valid=(unsigned)ok;g->feed_ms=w->observed_ms;
    if(!ok){
        if(g->phase!=XZ_RIDER_SETUP){g->phase=XZ_RIDER_SETUP;g->setup_step=2;g->pending_spike=0;}
        return;
    }
    xz_rider_analyze(w,&g->course);
    g->course_hash=w->track_hash;g->course_generation=w->generation;g->course_kind=w->kind;
    if(changed){
        g->phase=XZ_RIDER_TITLE;g->score=g->combo=g->best=g->judged=0;g->pending_spike=0;
        g->perfects=g->hops=g->catches=g->scrapes=g->spike_total=g->gem_total=0;
        g->hop_at=-1000;g->highwater=-1;g->trail_count=g->trail_head=0;say(g,"NEW TRACK / NEW COURSE");
    }
    double prior=g->position;g->previous=prior;g->position=w->position;
    if(g->phase==XZ_RIDER_SETUP||g->phase==XZ_RIDER_TITLE||g->phase==XZ_RIDER_RESULT)return;
    if(!w->playing){
        g->phase=w->total>1&&g->position>=w->total-1?XZ_RIDER_RESULT:XZ_RIDER_HOLD;
        if(g->phase==XZ_RIDER_RESULT&&g->pending_spike)judge_spike(g,g->pending_at,g->pending_altitude);
        g->pending_spike=0;g->altitude=height_at(g,g->position);return;
    }
    int resumed=g->phase==XZ_RIDER_HOLD;
    g->phase=XZ_RIDER_RIDE;
    double delta=g->position-prior;
    if(resumed||delta<0||delta>45){
        g->pending_spike=0;g->hop_at=-1000;g->trail_count=g->trail_head=0;
        if(g->position>g->highwater)g->highwater=g->position;
        g->shield_until=g->position+60;say(g,delta<0?"REPLAY / PRACTICE THIS SECTION":"FOLLOWING YOUR TRACK");return;
    }
    g->altitude=height_at(g,g->position);
    float ground=xz_rider_swell(g,g->position);
    g->slipped=slip_at(g,g->position)&&g->altitude>=ground&&g->altitude-ground<=26;
    if(g->pending_spike&&g->position>g->pending_at+10){
        judge_spike(g,g->pending_at,g->pending_altitude);g->pending_spike=0;
    }
    if(delta>0&&g->position>g->highwater){
        for(unsigned i=0;i<g->course.object_count;i++){
            const struct xz_rider_object *o=&g->course.objects[i];
            if(o->at<=prior||o->at<=g->highwater||o->at>g->position)continue;
            if(o->kind==XZ_RIDER_SPIKE){
                g->spike_total++;
                if(g->pending_spike)judge_spike(g,g->pending_at,g->pending_altitude);
                /* Ten samples of grace allow a real CUE press just after the
                   observed crossing; a frame boundary cannot steal a hit. */
                g->pending_spike=1;g->pending_at=o->at;g->pending_altitude=o->altitude;
            }else{
                g->gem_total++;g->judged++;
                if(fabsf(height_at(g,o->at)-o->altitude)<=18){g->catches++;reward(g,50,"GEM CAUGHT");}
            }
        }
        if(g->altitude+4<ground)scrape(g);
        g->highwater=g->position;
        unsigned t=g->trail_head++%48;g->trail_at[t]=g->position;g->trail_alt[t]=g->altitude;
        if(g->trail_count<48)g->trail_count++;
    }
    if(g->position>=w->total-1&&w->total>1){g->phase=XZ_RIDER_RESULT;g->pending_spike=0;}
}
void xz_rider_step(struct xz_rider *g,uint32_t now){
    uint32_t dt=now-g->last_ms;if(dt>UINT32_MAX/2)return;g->last_ms=now;
    if(g->valid&&now-g->feed_ms>XZ_WAVE_RIDER_FRESH_MS){
        g->valid=0;g->wave.valid=0;g->wave.error=XZ_WAVE_RIDER_POSITION_UNAVAILABLE;
        g->phase=XZ_RIDER_SETUP;g->setup_step=2;g->pending_spike=0;
    }
    if(dt>200)dt=200;
    g->cruise+=(g->target-g->cruise)*((float)dt/(55.f+(float)dt));
    g->altitude=height_at(g,g->position);
}
void xz_rider_jog(struct xz_rider *g,float pulses,uint32_t now){
    if(!isfinite(pulses))return;
    xz_rider_step(g,now);
    g->target=limit(g->target+limit(pulses,-90,90)*.42f,16,274);
    if(pulses!=0){g->jog_seen=1;g->jog_ms=now;g->jog_direction=pulses>0?1:-1;}
}
void xz_rider_key(struct xz_rider *g,enum xz_rider_key key,uint32_t now){
    xz_rider_step(g,now);
    if(key==XZ_RIDER_PAD_H){g->leave=1;return;}
    if(key==XZ_RIDER_PAD_G){g->quiet=!g->quiet;return;}
    if(key==XZ_RIDER_PAD_E){xz_rider_jog(g,-24,now);return;}
    if(key==XZ_RIDER_PAD_F){xz_rider_jog(g,24,now);return;}
    if(key>XZ_RIDER_PAD_D)return;
    g->cue_seen=1;
    if(g->phase==XZ_RIDER_TITLE||g->phase==XZ_RIDER_RESULT){xz_rider_begin(g,now);return;}
    if(g->phase==XZ_RIDER_SETUP){g->hop_at=g->position;g->altitude=height_at(g,g->position);return;}
    if(g->phase!=XZ_RIDER_RIDE)return;
    if(g->position-g->hop_at<24)return;
    g->hop_at=g->position;g->jumped=1;g->altitude=height_at(g,g->position);
    if(g->pending_spike&&fabs(g->position-g->pending_at)<=10){
        judge_spike(g,g->pending_at,g->pending_altitude);g->pending_spike=0;
    }
}
void xz_rider_touch(struct xz_rider *g,int x,int y,int down,uint32_t now){
    int press=down&&!g->touch_down;g->touch_down=!!down;xz_rider_step(g,now);
    if(!press||x<0||x>=800||y<0||y>=480)return;
    if(x>=690&&y<48){g->leave=1;return;}
    if(g->phase==XZ_RIDER_SETUP){
        if(g->setup_step==1&&y>=150&&y<224){
            if(x>=80&&x<380)g->deck=0;
            if(x>=420&&x<720)g->deck=1;
        }
        if(g->setup_step==2&&x>=24&&x<200&&y>=435){g->setup_step=1;return;}
        if(g->setup_step==3&&y>=434&&y<470&&x>=220&&x<580&&g->valid){xz_rider_begin(g,now);return;}
        if(x>=160&&x<640&&y>=(g->setup_step==3?350:330)&&y<(g->setup_step==3?418:410)){
            if(g->setup_step<2){g->setup_step++;return;}
            if(g->setup_step==2){if(g->valid)g->setup_step=3;else if(!g->wave.loading)g->leave=1;return;}
            if(g->valid&&g->jog_seen&&g->cue_seen)xz_rider_begin(g,now);
        }
    }else if(g->phase==XZ_RIDER_TITLE||g->phase==XZ_RIDER_RESULT){
        if(x>=160&&x<640&&y>=330&&y<410)xz_rider_begin(g,now);
    }else if(y>=48)xz_rider_key(g,XZ_RIDER_CUE,now);
}
