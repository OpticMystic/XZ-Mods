/* SPDX-License-Identifier: MIT */
#include "wave_rider.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846f
#define RADIUS 7.f
#define BASE_VX 300.f
static float clampf(float v,float a,float b){return v<a?a:v>b?b:v;}
static float wrapped(float a){while(a>PI)a-=2*PI;while(a< -PI)a+=2*PI;return a;}
static unsigned scale_of(const struct xz_wave_rider_snapshot *w){return w->normalization<64?64:w->normalization;}
struct envelope {const struct xz_wave_rider_snapshot *wave;uint32_t sum[XZ_WAVE_RIDER_SAMPLES+1],mid[XZ_WAVE_RIDER_SAMPLES+1];};
static float average(const struct envelope *e,double a,double b,int middle){
    int lo=(int)floor(a)-(int)e->wave->first,hi=(int)ceil(b)-(int)e->wave->first;
    if(lo<0)lo=0;if(hi>(int)e->wave->count)hi=(int)e->wave->count;
    if(hi<=lo)return 0;
    const uint32_t *s=middle?e->mid:e->sum;
    return (float)(s[hi]-s[lo])/(hi-lo);
}
static unsigned flux(const struct xz_wave_rider_snapshot *w,int at,unsigned band){
    int i=at-(int)w->first;if(i<0||i>=(int)w->count)return 0;
    unsigned low=w->bands[i*3+band];
    for(int j=i>6?i-6:0;j<i;j++)if(w->bands[j*3+band]<low)low=w->bands[j*3+band];
    return w->bands[i*3+band]-low;
}
static double valley(const struct xz_wave_rider_snapshot *w,double nominal){
    int radius=(int)(w->pulse_period*.49f),at=(int)floor(nominal+.5);
    unsigned best=0;int winner=at;
    for(int j=at-radius;j<=at+radius;j++){
        unsigned f=flux(w,j,0);
        if(f>best||(f==best&&abs(j-at)<abs(winner-at))){best=f;winner=j;}
    }
    return best>=10?winner:nominal;
}
static void curve(float a,float b,float t,float length,float *height,float *slope,float *curvature){
    t=clampf(t,0,1);float u=1-t,d=b-a;
    float q=t*t*t*(10+t*(-15+6*t));
    *height=a+d*q;
    *slope=d*30*t*t*u*u/(length*XZ_RIDER_WORLD_SCALE);
    *curvature=d*60*t*u*(1-2*t)/(length*length*XZ_RIDER_WORLD_SCALE*XZ_RIDER_WORLD_SCALE);
}
static void musical_terrain(const struct envelope *e,struct xz_rider_course *c){
    const struct xz_wave_rider_snapshot *w=e->wave;
    double span=w->pulse_period*2,origin=w->pulse_origin;
    int first=(int)floor((w->first-origin)/span)-2;
    int last=(int)ceil((w->first+w->count-origin)/span)+2;
    float norm=(float)scale_of(w);
    for(int hill=first;hill<last;hill++){
        double a=valley(w,origin+hill*span),b=valley(w,origin+(hill+1)*span);
        if(b-a<20)continue;
        float energy=average(e,a,b,0),harm=average(e,a,b,1);
        float activity=clampf(energy/norm*1.7f,0,1);
        float low=clampf(average(e,a-span*.35,a+span*.35,0)/norm*28,0,32);
        float high=clampf(average(e,b-span*.35,b+span*.35,0)/norm*28,0,32);
        float amp=clampf((float)(b-a)*XZ_RIDER_WORLD_SCALE*.40f,0,175)*activity;
        float crest=fmaxf(low,high)+amp;
        double top=a+(b-a)*(.48+.10*(energy>0?harm/energy:0));
        int lo=(int)floor(a)-(int)w->first,hi=(int)ceil(b)-(int)w->first;
        if(lo<0)lo=0;if(hi>(int)w->count)hi=(int)w->count;
        for(int i=lo;i<hi;i++){
            double at=w->first+i;
            if(at<top)curve(low,crest,(float)((at-a)/(top-a)),(float)(top-a),&c->height[i],&c->slope[i],&c->curvature[i]);
            else curve(crest,high,(float)((at-top)/(b-top)),(float)(b-top),&c->height[i],&c->slope[i],&c->curvature[i]);
        }
    }
}
static float tangent(float a,float b){return a*b>0?2*a*b/(a+b):0;}
static void envelope_terrain(const struct envelope *e,struct xz_rider_course *c){
    const struct xz_wave_rider_snapshot *w=e->wave;const int gap=36;
    int start=(int)(w->first/gap)-1,end=(int)((w->first+w->count)/gap)+1;
    float inv=170.f/scale_of(w);
    for(int k=start;k<=end;k++){
        float v[4];
        for(int j=0;j<4;j++){double at=(k+j-1)*gap;v[j]=clampf(average(e,at-24,at+25,0)*inv,0,180);}
        float m0=tangent(v[1]-v[0],v[2]-v[1]),m1=tangent(v[2]-v[1],v[3]-v[2]);
        float a=2*v[1]-2*v[2]+m0+m1,b=-3*v[1]+3*v[2]-2*m0-m1;
        for(int j=0;j<gap;j++){
            int i=k*gap+j-(int)w->first;if(i<0||i>=(int)w->count)continue;
            float t=(float)j/gap;
            c->height[i]=((a*t+b)*t+m0)*t+v[1];
            c->slope[i]=((3*a*t+2*b)*t+m0)/(gap*XZ_RIDER_WORLD_SCALE);
            c->curvature[i]=(6*a*t+2*b)/(gap*gap*XZ_RIDER_WORLD_SCALE*XZ_RIDER_WORLD_SCALE);
        }
    }
}
void xz_rider_analyze(const struct xz_wave_rider_snapshot *w,struct xz_rider_course *c){
    memset(c,0,sizeof(*c));
    if(!w||!w->valid||!w->count||w->count>XZ_WAVE_RIDER_SAMPLES)return;
    struct envelope e={.wave=w};unsigned norm=scale_of(w);
    for(unsigned i=0;i<w->count;i++){
        const uint8_t *b=w->bands+i*3;
        e.sum[i+1]=e.sum[i]+b[0]+b[1]+b[2];e.mid[i+1]=e.mid[i]+b[1];
    }
    if(w->pulse_confidence>=.25f&&w->pulse_period>=40&&w->pulse_period<=150&&isfinite(w->pulse_origin))musical_terrain(&e,c);
    else envelope_terrain(&e,c);
    /* Half-beat beads sit inside the sliding rider's collection radius. */
    double spacing=w->pulse_confidence>=.25f?clampf(w->pulse_period*.5f,30,60):40;
    double origin=isfinite(w->pulse_origin)?w->pulse_origin:0;
    int first=(int)ceil((w->first+32-origin)/spacing);
    for(int slot=first;c->object_count<XZ_RIDER_OBJECTS;slot++){
        int at=(int)floor(origin+slot*spacing+.5),i=at-(int)w->first;
        if(i+32>=(int)w->count)break;
        if(i<0||average(&e,at-12,at+13,0)<norm*.06f)continue;
        unsigned phase=(unsigned)(slot<0?-slot:slot)%48u;
        enum xz_rider_pickup kind=phase==16u?XZ_RIDER_PICKUP_AURORA:
            phase==40u?XZ_RIDER_PICKUP_COMET:XZ_RIDER_PICKUP_GEM;
        c->objects[c->object_count++]=(struct xz_rider_object){(uint32_t)at,c->height[i]+17,kind};
    }
    for(unsigned i=0;i<w->count;i++){
        double at=w->first+i;float total=average(&e,at-20,at+21,0),mid=average(&e,at-20,at+21,1);
        c->thermal[i]=total>0&&mid>=total*.4f?clampf(mid/norm,0,1):0;
        unsigned onset=flux(w,(int)at,2);
        if(onset<10||onset<norm*.14f||c->object_count>=XZ_RIDER_OBJECTS)continue;
        if((w->first&&i<32)||(w->first+w->count<w->total&&i+32>=w->count))continue;
        int peak=1;
        for(int j=(int)at-24;j<=(int)at+24;j++){
            unsigned other=flux(w,j,2);
            if(other>onset||(other==onset&&j<(int)at)){peak=0;break;}
        }
        if(peak)for(int j=-1;j<=1&&c->object_count<XZ_RIDER_OBJECTS;j++){
            int index=(int)i+j*16;
            if(index<32||index+32>=(int)w->count)continue;
            float height=fmaxf(c->height[index]+46,c->height[i]+100-j*j*24);
            c->objects[c->object_count++]=(struct xz_rider_object){w->first+(unsigned)index,height,XZ_RIDER_PICKUP_GEM};
        }
    }
}
struct xz_rider_surface xz_rider_surface_at(const struct xz_rider *g,double at){
    struct xz_rider_surface s={0};
    if(!g->valid||!g->wave.count)return s;
    float f=clampf((float)(at-g->wave.first),0,(float)(g->wave.count-1));
    unsigned i=(unsigned)f,j=i+1<g->wave.count?i+1:i;f-=i;
    s.height=g->course.height[i]+(g->course.height[j]-g->course.height[i])*f;
    s.slope=g->course.slope[i]+(g->course.slope[j]-g->course.slope[i])*f;
    s.curvature=g->course.curvature[i]+(g->course.curvature[j]-g->course.curvature[i])*f;
    s.thermal=g->course.thermal[i]+(g->course.thermal[j]-g->course.thermal[i])*f;
    s.height+=xz_rider_base_at(at);s.slope-=XZ_RIDER_GRADE;
    return s;
}
unsigned xz_rider_players(const struct xz_rider *g){return g->mode==XZ_RIDER_SOLO?1u:2u;}
int xz_rider_player_for_deck(const struct xz_rider *g,int deck){
    if(deck<0||deck>1)return -1;
    return g->mode==XZ_RIDER_SOLO?(deck==(int)g->deck?0:-1):deck;
}
static void feedback(struct xz_rider *g,struct xz_rider_player *p,const char *s){
    p->feedback=s;p->feedback_until=g->sim_at+135;
}
static void place(struct xz_rider *g,struct xz_rider_player *p,int reset){
    if(reset){memset(p,0,sizeof(*p));p->highwater=-1;p->last_good_landing=-10000;p->last_landing=-10000;}
    memset(&p->effects,0,sizeof(p->effects));p->contact_time=-10000;p->lift=p->downforce=p->landing_quality=0;
    p->at=g->position;p->altitude=xz_rider_surface_at(g,p->at).height+RADIUS;
    p->vx=BASE_VX;p->vy=0;p->grounded=1;p->held=0;p->tuck=0;
    p->pitch=p->pitch_target=atanf(xz_rider_surface_at(g,p->at).slope);
    p->trail_count=p->trail_head=0;p->flight_peak=0;p->launch_at=g->position;
    p->flick_pulses=p->flick_strength=0;p->flick_until=0;p->flick_launch_at=-10000;
    p->air_rotation=p->spin_velocity=p->landing_boost=0;p->air_tricks=p->last_tricks=0;
    p->angular_velocity=p->ground_lean=0;
    p->perfect_chain=0;p->rocket_until=0;p->launch_boost=0;
    p->trick_at=p->pickup_at=-10000;p->boost_until=0;p->contact_bad=0;
    p->air_crests=p->air_points=p->combo_points=p->combo_failed=p->landing_points=0;
    p->combo_until=p->air_boost_until=0;p->last_air_crest=-10000;
    if(p->highwater<p->at)p->highwater=p->at;
}
void xz_rider_init(struct xz_rider *g,unsigned deck,uint32_t now){
    memset(g,0,sizeof(*g));g->deck=deck<2?deck:0;g->initialized=1;g->zoom=1;
    g->spin_lift=1;g->difficulty=XZ_RIDER_EASY;
    g->phase=XZ_RIDER_SETUP;g->last_ms=now;g->source_rate=.15f;g->team_pair_at=-10000;
    g->touch_player=-1;for(unsigned i=0;i<2;i++)place(g,&g->player[i],1);
}
void xz_rider_begin(struct xz_rider *g,uint32_t now){
    if(!g->valid)return;
    if(g->wave.total>1&&g->wave.position>=g->wave.total-1){g->leave=1;return;}
    g->position=g->sim_at=g->source_position=g->wave.position;g->last_ms=now;g->zoom=1;g->camera_alt=xz_rider_base_at(g->position);
    g->team_score=g->harmonies=g->team_chain=0;g->team_pair_at=-10000;
    for(unsigned i=0;i<2;i++){
        unsigned jog=g->player[i].jog_seen,hold=g->player[i].hold_seen;
        place(g,&g->player[i],1);g->player[i].jog_seen=jog;g->player[i].hold_seen=hold;
        feedback(g,&g->player[i],"ANGLE DOWN TO GAIN SPEED / UP TO LAUNCH");
    }
    g->phase=g->wave.playing?XZ_RIDER_RIDE:XZ_RIDER_HOLD;
}
static void reset_position(struct xz_rider *g,double at){
    g->position=g->sim_at=at;g->camera_alt=xz_rider_base_at(at);g->zoom=1;
    for(unsigned i=0;i<2;i++){place(g,&g->player[i],0);feedback(g,&g->player[i],"BACK ON THE WAVE");}
}
void xz_rider_feed(struct xz_rider *g,const struct xz_wave_rider_snapshot *w,uint32_t now){
    if(!w)return;
    int ok=w->valid&&w->count&&w->count<=XZ_WAVE_RIDER_SAMPLES&&w->total<=1000000&&
        w->first<=w->total&&w->count<=w->total-w->first&&isfinite(w->position)&&
        w->position>=0&&w->position<=w->total&&now-w->observed_ms<=XZ_WAVE_RIDER_FRESH_MS;
    int changed=g->course_generation&&ok&&(g->course_hash!=w->track_hash||g->course_generation!=w->generation||g->course_kind!=w->kind);
    uint32_t elapsed=w->observed_ms-g->feed_ms;double moved=w->position-g->source_position;
    int jump=g->valid&&ok&&(moved< -8||moved>75||(elapsed>0&&elapsed<500&&fabs(moved-g->source_rate*elapsed)>45));
    int analyze=!g->valid||changed||w->first!=g->wave.first||w->count!=g->wave.count||w->observed_ms!=g->feed_ms;
    g->wave=*w;g->valid=(unsigned)ok;g->feed_ms=w->observed_ms;g->source_position=w->position;
    if(!ok){
        if(g->phase!=XZ_RIDER_SETUP){g->phase=XZ_RIDER_SETUP;g->setup_step=2;}
        for(unsigned p=0;p<2;p++){g->player[p].held=0;g->player[p].tuck=0;}
        return;
    }
    if(analyze)xz_rider_analyze(w,&g->course);
    g->course_hash=w->track_hash;g->course_generation=w->generation;g->course_kind=w->kind;
    if(changed){
        if(g->phase!=XZ_RIDER_SETUP)g->phase=XZ_RIDER_TITLE;
        g->position=g->sim_at=w->position;g->camera_alt=xz_rider_base_at(w->position);g->zoom=1;
        g->team_score=g->harmonies=g->team_chain=0;
        for(unsigned p=0;p<2;p++)place(g,&g->player[p],1);
    }
    if(!jump&&elapsed>0&&elapsed<500&&moved>0&&moved/elapsed<=.5)
        g->source_rate=(float)(moved/elapsed);
    if(g->phase==XZ_RIDER_SETUP||g->phase==XZ_RIDER_TITLE||g->phase==XZ_RIDER_RESULT){
        g->position=g->sim_at=w->position;
        if(g->phase!=XZ_RIDER_RESULT)for(unsigned p=0;p<2;p++){
            g->player[p].at=w->position;g->player[p].altitude=xz_rider_surface_at(g,w->position).height+RADIUS;
            g->player[p].pitch=g->player[p].pitch_target=atanf(xz_rider_surface_at(g,w->position).slope)+g->player[p].ground_lean;
        }
        return;
    }
    if(jump)reset_position(g,w->position);
    if(w->total>1&&w->position>=w->total-1){g->phase=XZ_RIDER_RESULT;return;}
    g->phase=w->playing?XZ_RIDER_RIDE:XZ_RIDER_HOLD;
}
static void score(struct xz_rider *g,struct xz_rider_player *p,unsigned points){
    unsigned mult=1+p->streak/3;if(mult>5)mult=5;
    p->score+=points*mult;g->team_score+=points*mult;
}
static void landing(struct xz_rider *g,struct xz_rider_player *p,struct xz_rider_surface s){
    int easy=g->difficulty==XZ_RIDER_EASY,hard=g->difficulty==XZ_RIDER_HARD;
    float angle=atanf(s.slope),impact=fabsf(p->vy-p->vx*s.slope)/sqrtf(1+s.slope*s.slope);
    float error=fabsf(wrapped(p->pitch-angle));
    int meaningful=g->sim_at-p->launch_at>=10&&p->flight_peak>=10;
    int fresh=p->at>p->highwater&&p->at-p->last_landing>24;
    float tangent_speed=(p->vx+p->vy*s.slope)/(1+s.slope*s.slope);
    float smooth_angle=easy?1.25f:hard?.50f:.72f;
    int smooth=error<smooth_angle&&tangent_speed>(easy?40:hard?100:80);
    int onbeat=1;
    if(g->wave.pulse_confidence>=.25f){
        double nearest=g->wave.pulse_origin+round((g->sim_at-g->wave.pulse_origin)/g->wave.pulse_period)*g->wave.pulse_period;
        unsigned strength=0;int radius=(int)(g->wave.pulse_period*.35f);
        for(int at=(int)g->sim_at-radius;at<=(int)g->sim_at+radius;at++){
            unsigned value=flux(&g->wave,at,0);
            if(value>strength&&value>=scale_of(&g->wave)*.08f){strength=value;nearest=at;}
        }
        onbeat=fabs(g->sim_at-nearest)<=g->wave.pulse_period*.22;
    }
    if(p->power==XZ_RIDER_POWER_AURORA){smooth=error<fmaxf(smooth_angle,1.0f)&&tangent_speed>(easy?40:60);impact*=.6f;}
    int perfect=smooth&&impact<(easy?600:hard?380:480)&&error<(easy?.45f:hard?.20f:.28f);
    p->landing_points=0;
    p->last_tricks=0;p->landing_boost=0;p->boost_until=0;p->contact_bad=(unsigned)!smooth;
    p->contact_time=g->sim_at;p->contact_at=p->at;p->contact_impact=clampf(impact/500.f,0,1);p->contact_angle=angle;
    p->landing_quality=clampf(1-error/(easy?1.4f:hard?.6f:.8f),0,1);
    p->grounded=1;p->altitude=s.height+RADIUS;p->vy=0;
    if(meaningful){
        p->landings++;
        if(p->air_points){
            p->combo_points=p->air_points;p->combo_failed=(unsigned)(!smooth||!fresh);
            p->combo_until=g->sim_at+150;
            if(!p->combo_failed){
                p->score+=p->air_points;g->team_score+=p->air_points;
                if(p->air_points>p->best_combo)p->best_combo=p->air_points;
            }
        }
        if(smooth){
            float before_speed=p->vx;
            p->vx=clampf(tangent_speed*(.90f+.22f*p->landing_quality+(onbeat?.04f:0))+35+45*p->landing_quality+fminf(p->streak,5)*16,190,780);
            if(fresh){
                p->streak++;if(p->streak>p->best_streak)p->best_streak=p->streak;
                p->last_good_landing=g->sim_at;p->last_good_position=p->at;
                if(perfect)p->perfects++;
                p->perfect_chain=perfect?p->perfect_chain+1:0;
                if(p->perfect_chain>=3){
                    if(p->rocket_until<=g->sim_at)p->rocket_ignitions++;
                    p->rocket_until=g->sim_at+900;
                }
                p->charge+=perfect?2:1;if(p->charge>9)p->charge=9;
                unsigned before_score=p->score;
                score(g,p,(perfect?150:70)+(onbeat?40:0)+(unsigned)fminf(p->flight_peak*.8f,500));
                p->landing_points=p->score-before_score;p->combo_until=g->sim_at+150;
                if(p->streak>=3)p->fever_until=g->sim_at+600;
                feedback(g,p,perfect?"PERFECT LANDING":"SMOOTH LANDING");
                if(p->perfect_chain==3)feedback(g,p,"ROCKET / THREE PERFECTS");
                if(p->air_tricks){
                    p->last_tricks=p->air_tricks;p->tricks_landed+=p->air_tricks;
                    feedback(g,p,p->air_tricks>1?"MULTI FLIP / LANDED":"360 / LANDED");
                }
                p->landing_boost=fmaxf(0,p->vx-before_speed);p->boost_until=g->sim_at+75;
            }
        }else{
            p->vx=clampf(tangent_speed*(easy?.84f:hard?.45f:.58f),easy?220:120,easy?500:410);
            p->perfect_chain=0;p->rocket_until=0;
            if(fresh){
                p->hard_landings++;p->streak=easy&&p->streak?p->streak-1:0;
                p->fever_until=0;g->team_chain=0;feedback(g,p,easy?"KEEP RIDING":"MATCH THE SLOPE");
            }
        }
        p->last_landing=p->at;
        xz_game_emit(&p->effects,perfect?XZ_IMPACT_PERFECT_LAND:XZ_IMPACT_LAND,g->sim_at,p->at,s.height,clampf(.25f+impact/600.f,0,1),angle);
    }
    p->flight_peak=0;
    p->air_tricks=p->air_crests=p->air_points=0;p->air_rotation=p->spin_velocity=0;p->air_boost_until=0;
    p->pitch=wrapped(p->pitch);p->ground_lean=easy&&!smooth?0:clampf(wrapped(p->pitch-angle),-.40f,.42f);
    p->angular_velocity*=.15f;p->pitch_target=angle+p->ground_lean;
    p->flick_pulses=p->flick_strength=0;p->flick_until=0;
}
static void physics(struct xz_rider *g,struct xz_rider_player *p){
    const float dt=1.f/XZ_WAVE_RIDER_HZ;
    struct xz_rider_surface s=xz_rider_surface_at(g,p->at);
    double before=p->at;float before_y=p->altitude;
    p->tuck=p->held?1:0;
    if(g->sim_at>=p->flick_until){p->flick_pulses=p->flick_strength=0;}
    if(p->power!=XZ_RIDER_POWER_NONE&&g->sim_at>=p->power_until)p->power=XZ_RIDER_POWER_NONE;
    float surface_angle=atanf(s.slope),old_pitch=p->pitch;
    if(p->grounded){
        p->pitch_target=surface_angle+p->ground_lean;
        p->angular_velocity+=((p->pitch_target-p->pitch)*90-p->angular_velocity*16)*dt;
        p->angular_velocity=clampf(p->angular_velocity,-3,3);
        p->pitch=clampf(p->pitch+p->angular_velocity*dt,surface_angle-.45f,surface_angle+.47f);
    }else{
        /* Easy leaves pitch to the rider. Gravity still draws the flight arc. */
        float flight_angle=atan2f(p->vy,p->vx),error=wrapped(flight_angle-p->pitch);
        float torque=g->difficulty==XZ_RIDER_EASY?0:g->difficulty==XZ_RIDER_HARD?6.5f:5.f;
        p->angular_velocity+=(clampf(error,-.8f,.8f)*torque-p->angular_velocity*2.8f)*dt;
        p->pitch+=p->angular_velocity*dt;p->pitch_target=p->pitch+p->angular_velocity*.10f;
    }
    float turn=p->pitch-old_pitch;p->spin_velocity=p->angular_velocity;
    if(!p->grounded){
        p->air_rotation+=turn;
        if(fabsf(p->air_rotation)>=2*PI-.015f){
            p->air_rotation-=copysignf(2*PI,p->air_rotation);
            if(p->air_tricks<8){
                p->air_tricks++;p->air_points+=125;p->trick_at=g->sim_at;
                p->vx=clampf(p->vx+70,120,780);p->air_boost_until=g->sim_at+60;
                feedback(g,p,"360 BOOST / LAND TO BANK");
            }
        }
        float angle=wrapped(p->pitch),offset=p->pitch-angle;
        p->pitch=angle;p->pitch_target-=offset;
    }
    float lead=(float)(p->at-g->sim_at);
    int rocket=p->rocket_until>g->sim_at;
    float speed_cap=rocket?980:780;
    float spring=rocket?-(lead-75)*5.f-(p->vx-BASE_VX)*.22f+220:
        -lead*11.f-(p->vx-BASE_VX)*.60f;
    float gravity=430.f*(1+3*p->tuck);
    if(p->fever_until>g->sim_at&&!p->tuck)gravity*=.82f;
    if(p->power==XZ_RIDER_POWER_AURORA&&!p->tuck)gravity*=.72f;
    if(p->grounded){
        float nose_down=clampf(-sinf(p->pitch),0,1);
        float heavy=480+980*nose_down+1320*p->tuck;
        p->downforce=(heavy-480)/2310.f;p->lift=0;
        float align=fabsf(wrapped(p->pitch-atanf(s.slope)));
        p->vx+=(-heavy*s.slope/(1+s.slope*s.slope)+spring-8-align*28-align*align*160)*dt;
        p->vx=clampf(p->vx,120,speed_cap);
        float nose_up=clampf(sinf(p->pitch),0,1);
        int flick_ready=p->flick_strength>=1&&g->sim_at<p->flick_until;
        float launch_cap=270+fminf(p->streak,6)*55+(flick_ready?80:0);
        if(p->power==XZ_RIDER_POWER_COMET||flick_ready)launch_cap=650;
        float takeoff_vx=p->vx*(flick_ready?1.05f:1)*(p->power==XZ_RIDER_POWER_COMET?1.10f:1);
        if(align<.50f&&p->vx>260)takeoff_vx+=25+45*(1-align/.50f);
        int crest_launch=p->ground_lean>=-.02f&&p->pitch>=0&&s.slope>.06f&&-p->vx*p->vx*s.curvature>gravity*(.90f-.35f*nose_up);
        if(!p->tuck&&p->vx>190&&takeoff_vx*s.slope<launch_cap&&(flick_ready||crest_launch)){
            float launch_vx=p->vx;
            p->grounded=0;p->vy=p->vx*(s.slope+nose_up*.52f)*(1+fminf(p->streak,6)*.17f);
            p->launch_at=g->sim_at;p->launch_position=p->at;p->flight_peak=0;
            p->air_rotation=0;p->air_tricks=p->air_crests=p->air_points=0;p->last_air_crest=-10000;
            if(flick_ready){
                p->vy=fmaxf(p->vy*1.28f,230+p->vx*fmaxf(s.slope,0));p->vx*=1.05f;
                p->angular_velocity=-p->flick_direction*3.8f;
                p->flick_launches++;p->flick_launch_at=g->sim_at;
                p->flick_pulses=p->flick_strength=0;p->flick_until=0;
                feedback(g,p,"SPIN LIFT");
            }
            if(p->power==XZ_RIDER_POWER_COMET){p->vy*=1.38f;p->vx*=1.10f;p->power=XZ_RIDER_POWER_NONE;p->comet_until=g->sim_at+225;launch_cap=650;feedback(g,p,"COMET LAUNCH");}
            if(g->mode==XZ_RIDER_COOP&&g->sim_at-g->team_pair_at<450)p->vy*=1.10f;
            if(align<.50f&&launch_vx>260){
                p->vx=clampf(p->vx+25+45*(1-align/.50f),120,speed_cap);
                p->air_boost_until=g->sim_at+60;
            }
            p->launch_boost=fmaxf(0,p->vx-launch_vx);
            p->vy=clampf(p->vy,0,launch_cap);p->launches++;p->launch_strength=clampf(p->vy/500.f,0,1);
            xz_game_emit(&p->effects,p->launch_boost>20?XZ_IMPACT_BOOST:XZ_IMPACT_LAUNCH,g->sim_at,p->at,s.height,p->launch_strength,p->pitch);
        }else p->vy=p->vx*s.slope;
    }else{
        float flight_angle=atan2f(p->vy,p->vx),attack=wrapped(p->pitch-flight_angle);
        float speed=p->vx/BASE_VX;
        float lift=!p->tuck?clampf(attack,-.85f,.65f)*180*clampf(speed*speed,.3f,2.2f):0;
        gravity*=1-s.thermal*.18f;
        lift=fminf(lift,gravity*.9f);
        p->lift=clampf(lift/240.f,0,1);p->downforce=clampf((-lift+gravity*p->tuck)/700.f,0,1);
        p->vy+=(lift-gravity)*dt;
        float drag=fabsf(attack)*30+fmaxf(0,fabsf(attack)-.7f)*80;
        if(g->difficulty==XZ_RIDER_EASY)drag*=.45f;
        p->vx+=(spring-drag)*dt;
        p->vx=clampf(p->vx,120,speed_cap);
    }
    p->at+=p->vx*dt/XZ_RIDER_WORLD_SCALE;
    if(p->at<g->sim_at-75)p->at=g->sim_at-75;
    /* The normal spring sheds a boost's lead without teleporting the rider back. */
    if(p->at>g->sim_at+280)p->at=g->sim_at+280;
    if(p->at>g->wave.total)p->at=g->wave.total;
    struct xz_rider_surface next=xz_rider_surface_at(g,p->at);
    if(p->grounded){p->altitude=next.height+RADIUS;p->vy=p->vx*next.slope;}
    else{
        p->altitude+=p->vy*dt;
        float air=fmaxf(0,p->altitude-next.height-RADIUS);
        if(air>=120&&p->flight_peak<120)feedback(g,p,"SUPER FLIGHT");
        if(air>p->flight_peak)p->flight_peak=air;
        if(air>p->best_height)p->best_height=air;
        if(p->altitude>p->peak)p->peak=p->altitude;
        if(s.slope>0&&next.slope<=0&&next.height-xz_rider_base_at(p->at)>10&&air>=10&&
           p->at>p->highwater&&p->at-p->last_air_crest>24){
            p->air_crests++;p->air_points+=100;p->last_air_crest=p->at;
        }
        if(p->altitude<=next.height+RADIUS&&p->vy<p->vx*next.slope)landing(g,p,next);
    }
    if(p->at>p->highwater){
        for(unsigned i=0;i<g->course.object_count;i++){
            struct xz_rider_object o=g->course.objects[i];
            if(o.at<=before||o.at<=p->highwater||o.at>p->at)continue;
            float t=(float)((o.at-before)/(p->at-before));
            float altitude=o.altitude+xz_rider_base_at(o.at);
            if(fabsf(before_y+(p->altitude-before_y)*t-altitude)<(g->difficulty==XZ_RIDER_EASY?30:g->difficulty==XZ_RIDER_HARD?14:20)){
                p->catches++;p->pickup_at=g->sim_at;p->last_pickup=o.kind;
                xz_game_emit(&p->effects,XZ_IMPACT_SHATTER,g->sim_at,o.at,altitude,.55f,p->pitch);
                if(p->charge<9)p->charge++;score(g,p,40);
                if(o.kind==XZ_RIDER_PICKUP_GEM){
                    if(g->sim_at-p->contact_time>90&&g->sim_at-p->trick_at>90&&g->sim_at-p->launch_at>35)
                        feedback(g,p,"LIGHT SHARD +1");
                }
                else{
                    p->power_pickups++;p->powers_used++;
                    p->power=o.kind==XZ_RIDER_PICKUP_AURORA?XZ_RIDER_POWER_AURORA:XZ_RIDER_POWER_COMET;
                    p->power_until=g->sim_at+600;
                    feedback(g,p,o.kind==XZ_RIDER_PICKUP_AURORA?"AURORA / SOFT TOUCH":"COMET / NEXT CREST");
                }
            }
        }
        p->highwater=p->at;
    }
    if(((uint32_t)g->sim_at%3)==0){
        unsigned t=p->trail_head++%XZ_RIDER_TRAIL;p->trail_at[t]=p->at;p->trail_alt[t]=p->altitude;
        if(p->trail_count<XZ_RIDER_TRAIL)p->trail_count++;
    }
}
void xz_rider_step(struct xz_rider *g,uint32_t now){
    uint32_t elapsed=now-g->last_ms;if(elapsed>UINT32_MAX/2)return;g->last_ms=now;
    if(g->valid&&now-g->feed_ms>XZ_WAVE_RIDER_FRESH_MS){
        g->valid=0;g->wave.valid=0;g->wave.error=XZ_WAVE_RIDER_POSITION_UNAVAILABLE;
        g->phase=XZ_RIDER_SETUP;g->setup_step=2;return;
    }
    if(g->valid){
        unsigned i=(unsigned)clampf((float)(g->source_position-g->wave.first),0,(float)(g->wave.count-1));
        float scale=1.8f/scale_of(&g->wave);
        for(unsigned k=0;k<3;k++)g->music[k]=clampf(g->wave.bands[i*3+k]*scale,0,1);
        float kick=flux(&g->wave,(int)g->source_position,0)*4.f/scale_of(&g->wave);
        g->pulse=fmaxf(clampf(kick,0,1),g->pulse-fminf(elapsed,100)*.004f);
    }
    if(g->phase!=XZ_RIDER_RIDE)return;
    double target=g->source_position+fmin(now-g->feed_ms,60)*g->source_rate;
    if(target>g->wave.total)target=g->wave.total;
    if(elapsed>500||target-g->sim_at>75||target<g->sim_at-8){reset_position(g,target);return;}
    g->position=target;
    for(unsigned count=0;g->sim_at+1<=target&&count<75;count++){
        g->sim_at+=1;
        for(unsigned p=0;p<xz_rider_players(g);p++)physics(g,&g->player[p]);
        if(g->mode==XZ_RIDER_COOP){
            double a=g->player[0].last_good_landing,b=g->player[1].last_good_landing,earlier=fmin(a,b);
            if(earlier>g->team_pair_at&&fabs(a-b)<=45){
                g->team_pair_at=earlier;g->harmonies++;g->team_chain++;
                g->team_score+=200*(g->team_chain>8?8:g->team_chain);
                g->team_feedback_until=g->sim_at+180;
                for(unsigned p=0;p<2;p++)feedback(g,&g->player[p],"HARMONY / TOGETHER");
            }
        }
    }
    float datum=xz_rider_base_at(g->position),top=datum,low=1000000;
    unsigned players=xz_rider_players(g);
    for(unsigned p=0;p<players;p++){
        float ahead=g->player[p].altitude+fmaxf(0,g->player[p].vy)*.22f;
        if(ahead>top)top=ahead;
        if(g->player[p].altitude<low)low=g->player[p].altitude;
    }
    float zoom=players>1?clampf(220.f/(top-low+90),.1f,1):clampf(268.f/(top-datum+45),.4f,1);
    g->zoom+=(zoom-g->zoom)*fminf(elapsed*(zoom<g->zoom?.008f:.002f),1);
    float camera=players>1?fmaxf(datum,low-60):fmaxf(datum,top-216.f/g->zoom);
    g->camera_alt+=(camera-g->camera_alt)*fminf(elapsed*(camera>g->camera_alt?.012f:.004f),1);
}
void xz_rider_jog(struct xz_rider *g,unsigned player,float pulses,uint32_t now){
    if(player>=xz_rider_players(g)||!isfinite(pulses))return;
    xz_rider_step(g,now);struct xz_rider_player *p=&g->player[player];
    if(g->phase==XZ_RIDER_RIDE&&!p->grounded){
        float impulse=-clampf(pulses,-4096,4096)*XZ_RIDER_AIR_IMPULSE;
        if(fabsf(p->angular_velocity)>.05f&&p->angular_velocity*impulse<0){
            /* Countersteering cancels momentum quickly; only unused input starts the opposite spin. */
            float brake=fminf(fabsf(p->angular_velocity),fabsf(impulse)*XZ_RIDER_AIR_BRAKE);
            p->angular_velocity+=copysignf(brake,impulse);
            impulse=copysignf(fmaxf(0,fabsf(impulse)-brake/XZ_RIDER_AIR_BRAKE),impulse);
        }
        p->angular_velocity=clampf(p->angular_velocity+impulse,-14,14);
        p->pitch_target=p->pitch+p->angular_velocity*.10f;
    }else{
        p->ground_lean=clampf(p->ground_lean-clampf(pulses,-4096,4096)*XZ_RIDER_JOG_GAIN,-.40f,.42f);
        p->pitch_target=atanf(xz_rider_surface_at(g,p->at).slope)+p->ground_lean;
    }
    if(pulses){p->jog_seen=1;p->jog_ms=now;p->jog_direction=pulses>0?1:-1;}
    if(g->spin_lift&&g->phase==XZ_RIDER_RIDE&&p->grounded&&pulses){
        {
            int direction=pulses>0?1:-1;
            float window=clampf(g->wave.pulse_period*.45f,15,38);
            if(g->sim_at>=p->flick_until||direction!=p->flick_direction){
                p->flick_pulses=p->flick_strength=0;p->flick_started=g->sim_at;
                p->flick_until=g->sim_at+window;p->flick_direction=direction;
            }
            float previous=p->flick_strength;
            p->flick_pulses=fminf(1200,p->flick_pulses+fabsf(pulses));
            p->flick_strength=p->flick_pulses/1200;
            if(previous<1&&p->flick_strength>=1){
                p->flick_until=g->sim_at+window;
                feedback(g,p,p->held?"SPIN READY / RELEASE CUE":"SPIN READY");
            }
        }
    }
    if(g->phase!=XZ_RIDER_RIDE){p->pitch=p->pitch_target;}
}
void xz_rider_button(struct xz_rider *g,unsigned player,enum xz_rider_key key,int down,uint32_t now){
    if(player>=xz_rider_players(g))return;
    /* Platter contact is captured by the native adapter to protect playback.
     * Resting a hand on the jog must not hold the dive action down. */
    if(key==XZ_RIDER_PLATTER)return;
    xz_rider_step(g,now);struct xz_rider_player *p=&g->player[player];
    int weight=key<=XZ_RIDER_PAD_D;
    if(weight){
        unsigned bit=1u<<(unsigned)key;
        if(down){p->held|=bit;p->hold_seen=1;}else p->held&=~bit;
        p->tuck=p->held?1:0;
        if(down&&(g->phase==XZ_RIDER_TITLE||g->phase==XZ_RIDER_RESULT)){
            xz_rider_begin(g,now);p->held|=bit;p->tuck=1;p->hold_seen=1;
        }
        return;
    }
    if(!down)return;
    if(key==XZ_RIDER_PAD_H){g->leave=1;return;}
    if(key==XZ_RIDER_PAD_G){g->quiet=!g->quiet;return;}
    if(g->phase!=XZ_RIDER_RIDE)return;
    if(key==XZ_RIDER_PAD_E||key==XZ_RIDER_PAD_F){
        unsigned cost=key==XZ_RIDER_PAD_E?3:5;
        if(p->charge<cost){feedback(g,p,"LAND SMOOTHLY TO CHARGE");return;}
        p->charge-=cost;p->powers_used++;
        p->power=key==XZ_RIDER_PAD_E?XZ_RIDER_POWER_AURORA:XZ_RIDER_POWER_COMET;
        p->power_until=g->sim_at+900;feedback(g,p,p->power==XZ_RIDER_POWER_AURORA?"AURORA / SOFT TOUCH":"COMET / NEXT CREST");
    }
}
void xz_rider_back(struct xz_rider *g,uint32_t now){
    xz_rider_step(g,now);
    if(g->phase==XZ_RIDER_SETUP&&g->setup_step>0){g->setup_step--;return;}
    /* A ride in progress ignores BACK; DECK or the menu key leaves it. */
    if(g->phase==XZ_RIDER_SETUP||g->phase==XZ_RIDER_TITLE||g->phase==XZ_RIDER_RESULT)g->leave=1;
}
void xz_rider_touch(struct xz_rider *g,int x,int y,int down,uint32_t now){
    int press=down&&!g->touch_down,release=!down&&g->touch_down;
    g->touch_down=!!down;xz_rider_step(g,now);
    if(release&&g->touch_player>=0){xz_rider_button(g,(unsigned)g->touch_player,XZ_RIDER_CUE,0,now);g->touch_player=-1;}
    if(!press||x<0||x>=800||y<0||y>=480)return;
    if(x>=690&&y<48){g->leave=1;return;}
    if(g->phase==XZ_RIDER_SETUP){
        if(g->setup_step==0&&y>=220&&y<288){
            if(x>=40&&x<268)g->mode=XZ_RIDER_SOLO;
            if(x>=286&&x<514)g->mode=XZ_RIDER_COOP;
            if(x>=532&&x<760)g->mode=XZ_RIDER_DUEL;
        }
        if(g->setup_step==1&&y>=150&&y<224){if(x>=80&&x<380)g->deck=0;if(x>=420&&x<720)g->deck=1;}
        if(g->setup_step==2&&x>=24&&x<200&&y>=435){g->setup_step=1;return;}
        if(g->setup_step==3&&x>=160&&x<640&&y>=194&&y<250){
            g->difficulty=(enum xz_rider_difficulty)((x-160)/160);return;
        }
        if(g->setup_step==3&&x>=260&&x<540&&y>=294&&y<320){g->spin_lift=!g->spin_lift;return;}
        if(g->setup_step==3&&y>=434&&y<470&&x>=220&&x<580&&g->valid){
            g->difficulty=XZ_RIDER_EASY;g->spin_lift=1;xz_rider_begin(g,now);return;
        }
        if(x>=160&&x<640&&y>=(g->setup_step==3?350:330)&&y<(g->setup_step==3?418:410)){
            if(g->setup_step<2){g->setup_step++;return;}
            if(g->setup_step==2){if(g->valid)g->setup_step=3;else if(!g->wave.loading)g->leave=1;return;}
            unsigned ready=0;for(unsigned p=0;p<xz_rider_players(g);p++)ready+=g->player[p].jog_seen;
            if(g->valid&&ready==xz_rider_players(g))xz_rider_begin(g,now);
        }
    }else if(g->phase==XZ_RIDER_TITLE||g->phase==XZ_RIDER_RESULT){
        if(x>=160&&x<640&&y>=330&&y<410)xz_rider_begin(g,now);
    }else if(y>=48){
        g->touch_player=g->mode==XZ_RIDER_SOLO?0:x>=400;
        xz_rider_button(g,(unsigned)g->touch_player,XZ_RIDER_CUE,1,now);
    }
}
