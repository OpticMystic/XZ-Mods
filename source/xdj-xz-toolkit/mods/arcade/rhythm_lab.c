/* SPDX-License-Identifier: MIT */
#include "rhythm_lab.h"
#include <math.h>
#include <string.h>
#include <limits.h>

#define PI 3.14159265358979323846f
static float clamp(float x,float lo,float hi){return x<lo?lo:x>hi?hi:x;}
static float wrap(float x){return atan2f(sinf(x),cosf(x));}
static const struct xz_lab_description descriptions[XZ_LAB_CASES]={
 {"PULSE AND HOLD","ACTION ON DOTS. HOLD THROUGH LONG STRIPES.","DOES ONE BUTTON FEEL MUSICAL?","TAIKO / OSU / MELODY'S ESCAPE","ACCENTS / ENVELOPE HOLDS",1},
 {"WAVE LANES","TURN INTO THE LANE WHERE A DOT ARRIVES.","IS STEERING ALONE ENOUGH?","AUDIOSURF / MUSIC RACER","ENVELOPE LANES",1},
 {"WHEEL MATCH","TURN TO THE LANE. TAP ACTION ON ARRIVAL.","DOES ADDING ACTION HELP THE SAME ROUTE?","SPIN RHYTHM / GUITAR HERO","LANES + ATTACKS",1},
 {"SCRATCH CUTS","FLICK THE JOG WITH EACH ARROW.","DO SHORT REVERSALS FEEL LIKE PLAYING?","DJ HERO / IIDX / SOUND VOLTEX","ACCENTS / STUDY DIRECTIONS",0},
 {"RIBBON HOLD","HOLD ACTION. STEER INSIDE THE RIBBON.","FLOWING MOTION OR CONSTANT CORRECTION?","SOUND VOLTEX / OSU","SMOOTH ENVELOPE",0},
 {"CALL AND ANSWER","WATCH FOUR PULSES. TAP THE REPLY.","DO YOU ANTICIPATE THE PATTERN?","RHYTHM HEAVEN / RHYTHM DOCTOR","STUDY PATTERN / PULSE",0},
 {"BEAT STEPS","POINT A DIRECTION. TAP ACTION TO STEP.","CAN YOU CHOOSE WITHOUT LOSING RHYTHM?","CRYPT OF THE NECRODANCER","TACTICAL CHOICE / PULSE",0},
 {"BEAT BLASTER","JOG AIMS. ACTION FIRES. TIMING ADDS POINTS.","DO YOU CHOOSE TO SHOOT WITH THE MUSIC?","PISTOL WHIP / BEAT HAZARD","ACCENTS / FREE AIM",1},
 {"SWEEP PUZZLE","JOG PICKS A COLUMN. ACTION DROPS. B FLIPS.","DOES WAITING FOR THE SWEEP FEEL GOOD?","LUMINES","PUZZLE / FOUR-PULSE SWEEP",0},
 {"SPRING RELEASE","HOLD ACTION. TURN CCW. RELEASE ON DOT.","DOES LOADING A SMALL SWEEP FEEL GOOD?","STORED GESTURE / RELEASE STUDY","ACCENTS / STORED TRAVEL",0},
 {"STATIC SURFER","TAP TO JUMP. JOG TO TILT THROUGH GLYPHS.","CAN JUMPS AND SMALL TURNS BECOME A FLOW?","VJ OPTICMYSTIC / RHYTHM PLATFORMER","TRACK ACCENTS / GLYPH GATES",1}
};
const struct xz_lab_description *xz_lab_description(unsigned mode){return &descriptions[mode<XZ_LAB_CASES?mode:0];}
const char *xz_lab_action_label(const struct xz_rhythm_lab *g,unsigned deck){
    static const char *const names[]={"CUE / PAD A","PAD A ONLY","PAD C ONLY","PAD D ONLY","PAD E ONLY"};
    unsigned choice=deck<2?g->action_binding[deck]:0;return names[choice<5?choice:0];
}
int xz_lab_action_key(const struct xz_rhythm_lab *g,unsigned deck,int key){
    unsigned choice=deck<2?g->action_binding[deck]:0;
    int pad=choice<2?LAB_PAD_A:choice==2?LAB_PAD_C:choice==3?LAB_PAD_D:LAB_PAD_E;
    if(key==LAB_CUE)return choice?LAB_PLATTER:LAB_CUE;
    if(key==pad)return LAB_PAD_A;
    if(key==LAB_PAD_A||key==LAB_PAD_C||key==LAB_PAD_D||key==LAB_PAD_E)return LAB_PLATTER;
    return key;
}
static void open_controls(struct xz_rhythm_lab *g){
    g->controls_return=(unsigned)g->phase;g->phase=LAB_CONTROLS;g->calibrating=0;
    for(unsigned i=0;i<2;i++)g->player[i].held=0;
}
unsigned xz_lab_players(const struct xz_rhythm_lab *g){return g->duo&&descriptions[g->mode].duo?2:1;}
int xz_lab_player_for_deck(const struct xz_rhythm_lab *g,int deck){
    if(deck<0||deck>1)return -1;
    return xz_lab_players(g)==2?deck:deck==(int)g->control_deck?0:-1;
}
unsigned xz_lab_lane(const struct xz_lab_player *p){unsigned n=(unsigned)(clamp(p->cursor,0,1)*3);return n>2?2:n;}
float xz_lab_band(const struct xz_rhythm_lab *g,double at,unsigned band){
    if(!g->valid||!g->wave.count||band>2)return 0;
    float i=clamp((float)(at-g->wave.first),0,(float)(g->wave.count-1));
    unsigned a=(unsigned)i,b=a+1<g->wave.count?a+1:a;float f=i-a;
    return g->wave.bands[a*3+band]*(1-f)+g->wave.bands[b*3+band]*f;
}
static float mean(const struct xz_rhythm_lab *g,double at,unsigned band,float radius){
    float v=0;for(int i=-4;i<=4;i++)v+=xz_lab_band(g,at+i*radius/4,band);return v/9;
}
static float normalized(const struct xz_rhythm_lab *g,double at,unsigned band){
    float lo=255,hi=0;
    for(int i=-6;i<=6;i++){float v=mean(g,at+i*g->period*.5,band,g->period*.2f);if(v<lo)lo=v;if(v>hi)hi=v;}
    return hi-lo<3?.5f:clamp((mean(g,at,band,g->period*.2f)-lo)/(hi-lo),0,1);
}
float xz_lab_ribbon(const struct xz_rhythm_lab *g,double at){
    double beat=(at-g->origin)/g->period;
    if(g->study)return .5f+.32f*sinf((float)beat*.65f);
    double span=g->period*2,a=g->origin+floor(beat*.5)*span;
    float u=(float)((at-a)/span),q=u*u*(3-2*u),v[2];
    for(unsigned i=0;i<2;i++){
        float h=mean(g,a+i*span,1,g->period*.6f),voice=mean(g,a+i*span,2,g->period*.6f);
        v[i]=h/(h+voice+8);
    }
    return .12f+.76f*(v[0]+(v[1]-v[0])*q);
}
static void feedback(struct xz_rhythm_lab *g,struct xz_lab_player *p,const char *s){p->feedback=s;p->feedback_until=g->position+g->period*1.5;}
static int active(const struct xz_rhythm_lab *g){return g->phase==LAB_RUN&&g->valid&&g->playing&&g->position>=g->practice_until;}
static float window(const struct xz_rhythm_lab *g){return fminf(g->period*.32f,125*g->rate);}
static int ontime(const struct xz_rhythm_lab *g,double at){return fabs(g->position-at)<=window(g);}
static double beat_at(const struct xz_rhythm_lab *g){return g->origin+round((g->position-g->origin)/g->period)*g->period;}
static void result(struct xz_rhythm_lab *g,struct xz_lab_player *p,struct xz_lab_event *e,unsigned player,int good,double error,const char *why){
    p->last_error_ms=(float)(error/g->rate);
    if(good){
        int perfect=fabs(error)<=55*g->rate;p->hits++;p->score+=perfect?100:60;p->streak++;
        if(p->streak>p->best_streak)p->best_streak=p->streak;if(perfect)p->perfects++;
        if(e)e->grade[player]=(unsigned char)(perfect?LAB_PERFECT:LAB_GOOD);
        feedback(g,p,why?why:perfect?"IN TIME":error<0?"A LITTLE EARLY":"A LITTLE LATE");
    }else{p->misses++;p->streak=0;if(e)e->grade[player]=LAB_MISSED;feedback(g,p,why?why:"MISSED / NEXT ONE");}
}
static double accent(const struct xz_rhythm_lab *g,double nominal){
    if(g->study||g->manual_pulse)return nominal;
    int r=(int)fminf(g->period*.22f,25),start=(int)nominal;float best=0;int winner=start;
    for(int i=start-r;i<=start+r;i++){
        float f=xz_lab_band(g,i,0)-mean(g,i-5,0,3);
        if(f>best){best=f;winner=i;}
    }
    return best>=8?winner:nominal;
}
static void reset_chart(struct xz_rhythm_lab *g){
    g->event_count=0;g->next_event=(int64_t)floor((g->position-g->origin)/g->period)-1;
    g->last_sweep=(int64_t)floor((g->position-g->origin)/g->period*1.5);
}
static void generate(struct xz_rhythm_lab *g){
    unsigned kept=0;
    for(unsigned i=0;i<g->event_count;i++)if(g->events[i].end>=g->position-g->period*(g->mode==LAB_ECHO?8:2))g->events[kept++]=g->events[i];
    g->event_count=kept;
    int ahead=g->mode==LAB_ECHO?9:(int)clamp((float)((g->wave.first+g->wave.count-g->position)/g->period)-3,2,6);
    int64_t last=(int64_t)ceil((g->position-g->origin)/g->period)+ahead;
    for(;g->next_event<=last&&g->event_count<XZ_LAB_EVENTS;g->next_event++){
        int64_t n=g->next_event;unsigned k=(unsigned)((n%8+8)%8);int include=1;
        if(g->mode==LAB_PULSE_HOLD)include=k!=3&&k!=6&&k!=7;
        if(g->mode==LAB_SCRATCH)include=k!=3&&k!=7;
        if(g->mode==LAB_BLASTER)include=(k%2)==0;
        if(g->mode==LAB_SPRING)include=(k%4)==2;
        if(g->mode==LAB_GLYPH)include=(k%2)==0;
        if(g->mode==LAB_ECHO){k=(unsigned)(((n-g->echo_base)%8+8)%8);unsigned variant=(unsigned)(((n-g->echo_base)/8)%3+3)%3;include=k==4||k==7||k==(variant==1?6:5);}
        if(g->mode==LAB_RIBBON||g->mode==LAB_STEPS||g->mode==LAB_PUZZLE)include=0;
        if(!include)continue;
        double at=g->origin+n*g->period;if(g->mode!=LAB_ECHO)at=accent(g,at);
        if(!g->study&&!g->manual_pulse&&g->mode!=LAB_ECHO){
            float onset=xz_lab_band(g,at,0)-mean(g,at-6,0,3);
            if(onset<fmaxf(5,g->wave.normalization*.025f))continue;
        }
        struct xz_lab_event e={.id=n,.at=at,.end=at,.kind=LAB_TAP};
        float v=g->study?(float)((n%3+3)%3)/2:normalized(g,at,1)*.6f+normalized(g,at,2)*.4f;
        e.lane=(int)clamp(v*3,0,2);e.band=(unsigned)e.lane;e.energy=mean(g,at,0,12)/255;
        e.angle=(v-.5f)*4.8f;e.direction=(k==1||k==4||k==6)?-1:1;
        if(g->mode==LAB_PULSE_HOLD&&k==2){e.kind=LAB_HOLD;e.end=at+g->period*(g->study?1.25:.75+.75*fmaxf(normalized(g,at,1),normalized(g,at,2)));}
        if(g->mode==LAB_SCRATCH)e.kind=LAB_CUT;
        if(g->mode==LAB_BLASTER){e.kind=LAB_TARGET;e.end=at+g->period*2;}
        if(g->mode==LAB_SPRING)e.kind=LAB_RELEASE;
        if(g->mode==LAB_GLYPH){
            int64_t age=n-g->echo_base;e.end=at+g->period*.52;
            e.energy=clamp(e.energy,0,1);e.kind=age<8?LAB_GLYPH_SPIKE:age<16?LAB_GLYPH_GATE:k==6?LAB_GLYPH_GAP:k==2||k==4?LAB_GLYPH_GATE:LAB_GLYPH_SPIKE;
            e.angle=v<.38f?-.62f:v>.62f?.62f:0;
        }
        if(at<g->practice_until)continue;
        g->events[g->event_count++]=e;
    }
}
static void save_record(struct xz_rhythm_lab *g){
    struct xz_lab_record *r=&g->record[g->mode];r->played++;r->seconds=g->elapsed;
    for(unsigned i=0;i<2;i++){
        r->hits[i]=g->player[i].hits;r->misses[i]=g->player[i].misses;r->score[i]=g->player[i].score;
        r->travel[i]=g->player[i].travel;r->coverage[i]=g->player[i].coverage_total>0?g->player[i].coverage/g->player[i].coverage_total:0;
    }
}
void xz_lab_init(struct xz_rhythm_lab *g,uint32_t now){
    memset(g,0,sizeof(*g));g->initialized=1;g->phase=LAB_MENU;g->period=75;g->rate=.15f;
    g->duration=XZ_LAB_SECONDS;g->last_ms=g->now=now;g->touch_player=-1;
    for(unsigned i=0;i<2;i++){g->turn_pulses[i]=XZ_LAB_TURN_PULSES;g->player[i].cursor=.5f;}
}
void xz_lab_begin(struct xz_rhythm_lab *g,uint32_t now){
    xz_lab_step(g,now);
    if(!g->valid||(!g->manual_pulse&&g->wave.pulse_confidence<.25f)){g->phase=LAB_SETUP;g->setup_step=1;return;}
    g->elapsed=0;g->duration=g->mode==LAB_GLYPH?120:XZ_LAB_SECONDS;g->previous=g->position;
    g->start_position=g->position;g->practice_until=g->position+g->period*4;g->last_ms=g->now=now;
    g->echo_base=(int64_t)ceil((g->position-g->origin)/g->period);
    if(g->mode==LAB_ECHO)g->practice_until=g->origin+(g->echo_base+4)*g->period;
    memset(g->player,0,sizeof(g->player));
    for(unsigned i=0;i<2;i++){g->player[i].cursor=.5f;g->player[i].gx=2;g->player[i].gy=2;g->player[i].target_x=4;g->player[i].target_y=2;g->player[i].shot_at=-10000;g->player[i].launch_at=-10000;}
    xz_glyph_begin(g);reset_chart(g);g->phase=g->playing?LAB_RUN:LAB_PAUSE;
}
void xz_lab_feed(struct xz_rhythm_lab *g,const struct xz_wave_rider_snapshot *w,uint32_t now){
    if(!w||!w->valid||!w->count||w->count>XZ_WAVE_RIDER_SAMPLES||!w->total||!isfinite(w->position)||w->position<0||w->position>w->total||now-w->observed_ms>XZ_WAVE_RIDER_FRESH_MS){
        if(w)g->wave=*w;g->valid=0;g->playing=0;
        if(g->phase==LAB_RUN||g->phase==LAB_PAUSE){g->phase=LAB_SETUP;g->setup_step=1;}
        return;
    }
    int changed=g->wave.track_hash&&w->track_hash!=g->wave.track_hash;
    int seek=g->valid&&(w->position<g->source_position-8||w->position>g->source_position+75);
    uint32_t dt=w->observed_ms-g->feed_ms;
    if(g->valid&&dt>=10&&dt<=250&&!seek&&w->position>g->source_position)g->rate=clamp((float)((w->position-g->source_position)/dt),.04f,.6f);
    g->wave=*w;g->valid=1;g->playing=!!w->playing&&w->position<w->total-1;g->source_position=w->position;g->feed_ms=w->observed_ms;
    int pulse_valid=isfinite(w->pulse_period)&&w->pulse_period>=25&&w->pulse_period<=250&&isfinite(w->pulse_origin);
    if(!pulse_valid)g->wave.pulse_confidence=0;
    if(changed)g->manual_pulse=0;
    if(!g->manual_pulse){g->period=pulse_valid?w->pulse_period:75;g->origin=pulse_valid?w->pulse_origin+g->timing_ms*g->rate:0;}
    if(changed){g->manual_pulse=0;g->tap_count=0;if(g->phase==LAB_RUN||g->phase==LAB_PAUSE)g->phase=LAB_BRIEF;g->position=w->position;for(unsigned i=0;i<2;i++)g->player[i].held=0;reset_chart(g);}
    if(seek){g->position=w->position;g->previous=g->position;g->practice_until=g->position+g->period*2;reset_chart(g);if(g->mode==LAB_GLYPH)xz_glyph_begin(g);}
    if(g->phase!=LAB_RUN&&g->phase!=LAB_PAUSE)g->position=w->position;
    if(g->phase==LAB_RUN&&!g->playing)g->phase=LAB_PAUSE;
    else if(g->phase==LAB_PAUSE&&g->playing)g->phase=LAB_RUN;
}
uint32_t xz_lab_matches(const struct xz_lab_player *p){
    uint32_t mask=0;
    for(int y=0;y<3;y++)for(int x=0;x<5;x++){
        int a=y*6+x;unsigned v=p->grid[a];
        if(v&&p->grid[a+1]==v&&p->grid[a+6]==v&&p->grid[a+7]==v)mask|=(1u<<a)|(1u<<(a+1))|(1u<<(a+6))|(1u<<(a+7));
    }
    return mask;
}
static void settle_grid(struct xz_lab_player *p){
    for(int x=0;x<6;x++){int bottom=3;for(int y=3;y>=0;y--)if(p->grid[y*6+x]){unsigned v=p->grid[y*6+x];p->grid[y*6+x]=0;p->grid[bottom--*6+x]=(uint8_t)v;}}
}
static void sweep(struct xz_rhythm_lab *g,int column){
    struct xz_lab_player *p=&g->player[0];uint32_t mask=0;
    for(int y=0;y<3;y++){int a=y*6+column;if(column<5&&p->grid[a]&&p->grid[a]==p->grid[a+1]&&p->grid[a]==p->grid[a+6]&&p->grid[a]==p->grid[a+7])mask|=(1u<<a)|(1u<<(a+1))|(1u<<(a+6))|(1u<<(a+7));}
    if(mask){for(unsigned i=0;i<24;i++)if(mask&(1u<<i)){p->grid[i]=0;p->cleared++;p->score+=25;}p->hits++;settle_grid(p);feedback(g,p,"SWEEP / CLEARED");}
}
void xz_lab_step(struct xz_rhythm_lab *g,uint32_t now){
    uint32_t ms=now-g->last_ms;if(ms>UINT32_MAX/2)return;g->now=now;g->last_ms=now;
    if(g->valid&&now-g->feed_ms>XZ_WAVE_RIDER_FRESH_MS){g->valid=0;g->playing=0;g->wave.error=XZ_WAVE_RIDER_POSITION_UNAVAILABLE;if(g->phase==LAB_RUN||g->phase==LAB_PAUSE){g->phase=LAB_SETUP;g->setup_step=1;}return;}
    if(!g->valid)return;
    for(unsigned i=0;i<3;i++)g->music[i]=clamp(xz_lab_band(g,g->source_position,i)*3/fmaxf(64,g->wave.normalization),0,1);
    g->pulse=1-clamp((float)fabs(g->position-beat_at(g))/(g->period*.3f),0,1);
    if(g->phase!=LAB_RUN||!g->playing){
        if(g->playing&&g->phase!=LAB_PAUSE)g->position=fmin(g->wave.total,g->source_position+fminf((float)(now-g->feed_ms),60)*g->rate);
        return;
    }
    g->previous=g->position;g->position=g->source_position+fminf((float)(now-g->feed_ms),60)*g->rate;
    if(g->position>g->wave.total)g->position=g->wave.total;
    generate(g);
    if(g->position>=g->practice_until)g->elapsed+=fminf(ms,100)*.001f;
    float dt=(float)fmax(0,fmin(g->position-g->previous,30));
    if(g->mode==LAB_GLYPH)xz_glyph_step(g,dt);
    if(active(g)&&g->mode!=LAB_GLYPH)for(unsigned p=0;p<xz_lab_players(g);p++){
        struct xz_lab_player *a=&g->player[p];
        if(g->mode==LAB_RIBBON){a->coverage_total+=dt;if(a->held&&fabsf(a->cursor-xz_lab_ribbon(g,g->position))<=.13f){a->coverage+=dt;a->score=(unsigned)(a->coverage*2);}}
        for(unsigned i=0;i<g->event_count;i++){
            struct xz_lab_event *e=&g->events[i];unsigned grade=e->grade[p];
            if(grade==LAB_HOLDING){
                if(g->position<e->end){e->duration[p]+=dt;if(a->held)e->covered[p]+=dt;}
                if(g->position>e->end+window(g)){result(g,a,e,p,0,g->position-e->end,"RELEASE AT THE END");}
            }else if(grade==LAB_PENDING){
                if(g->mode==LAB_WAVE_LANES&&g->position>=e->at&&g->position<=e->at+window(g)&&xz_lab_lane(a)==(unsigned)e->lane){a->motion_hits++;result(g,a,e,p,1,0,"CAUGHT");}
                else if(g->position>e->end+window(g))result(g,a,e,p,0,g->position-e->at,g->mode==LAB_WAVE_LANES?"OTHER LANE / NEXT ONE":NULL);
            }
        }
    }
    if(active(g)&&g->mode==LAB_PUZZLE){int64_t current=(int64_t)floor((g->position-g->origin)/g->period*1.5);if(current-g->last_sweep<=6)for(int64_t n=g->last_sweep+1;n<=current;n++)sweep(g,(int)((n%6+6)%6));g->last_sweep=current;}
    if(g->elapsed>=g->duration){save_record(g);g->phase=LAB_RESULTS;for(unsigned p=0;p<2;p++)g->player[p].held=0;}
}
static struct xz_lab_event *nearest(struct xz_rhythm_lab *g,unsigned p){
    struct xz_lab_event *found=NULL;double best=window(g)+.001;
    for(unsigned i=0;i<g->event_count;i++){struct xz_lab_event *e=&g->events[i];double d=fabs(g->position-e->at);if(e->grade[p]==LAB_PENDING&&d<best){best=d;found=e;}}
    return found;
}
void xz_lab_jog(struct xz_rhythm_lab *g,int deck,float pulses,uint32_t now){
    if(deck<0||deck>1||!isfinite(pulses))return;xz_lab_step(g,now);
    if(g->phase==LAB_SETUP&&g->setup_step==3&&g->calibrating){g->cal_pulses[deck]+=clamp(pulses,-10000,10000);return;}
    int index=xz_lab_player_for_deck(g,deck);if(index<0)return;
    float radians=clamp(pulses,-4096,4096)*(2*PI/g->turn_pulses[deck]);struct xz_lab_player *p=&g->player[index];
    p->jog_events++;p->travel+=fabsf(radians);
    if(g->phase==LAB_MENU){p->stroke+=radians;if(fabsf(p->stroke)>.5f){g->menu_selected=(g->menu_selected+(p->stroke>0?1:9))%10;p->stroke=0;}return;}
    p->cursor=clamp(p->cursor+radians/PI,0,1);p->angle=g->mode==LAB_GLYPH?clamp(p->angle+radians,-1.05f,1.05f):wrap(p->angle+radians);
    if(!active(g))return;
    if(g->mode==LAB_SPRING&&p->held)p->charge=clamp(p->charge-radians/(PI*.5f),0,1);
    if(g->mode==LAB_SCRATCH){
        int direction=radians<0?-1:1;
        if(now-p->jog_ms>120||direction!=p->stroke_direction){p->stroke=0;p->stroke_used=0;}
        p->stroke_direction=direction;p->jog_ms=now;p->stroke+=fabsf(radians);
        if(!p->stroke_used&&p->stroke>=.18f){
            p->stroke_used=1;struct xz_lab_event *e=nearest(g,(unsigned)index);
            if(e){p->timing_hits++;int good=e->direction==direction;if(good)p->motion_hits++;result(g,p,e,(unsigned)index,good,g->position-e->at,good?"CLEAN CUT":"OTHER DIRECTION");}
            else feedback(g,p,"WAIT FOR THE ARROW");
        }
    }
}
static void drop(struct xz_rhythm_lab *g,struct xz_lab_player *p){
    int col=(int)clamp(p->cursor*5,0,4);
    if(p->grid[col]||p->grid[col+1]){memmove(p->grid+6,p->grid,18);memset(p->grid,0,6);feedback(g,p,"SPACE RESET / KEEP PLAYING");}
    unsigned colors[2]={(p->attacks/2)%2+1,(p->attacks/3)%2+1};if(p->pair_flip){unsigned t=colors[0];colors[0]=colors[1];colors[1]=t;}
    for(int k=0;k<2;k++)for(int y=3;y>=0;y--)if(!p->grid[y*6+col+k]){p->grid[y*6+col+k]=(uint8_t)colors[k];break;}
    feedback(g,p,"PLACED / WAIT FOR THE SWEEP");
}
static void attack(struct xz_rhythm_lab *g,unsigned index){
    if(g->mode==LAB_GLYPH){xz_glyph_jump(g,index);return;}
    struct xz_lab_player *p=&g->player[index];p->attacks++;
    if(g->mode==LAB_RIBBON||g->mode==LAB_WAVE_LANES||g->mode==LAB_SCRATCH)return;
    if(g->mode==LAB_SPRING){p->charge=0;feedback(g,p,"TURN CCW A LITTLE");return;}
    if(g->mode==LAB_PUZZLE){drop(g,p);return;}
    if(g->mode==LAB_STEPS){
        p->last_error_ms=(float)((g->position-beat_at(g))/g->rate);
        int dir=(int)floorf((wrap(p->angle)+PI/4+2*PI)/(PI/2))%4;
        static const int dx[4]={1,0,-1,0},dy[4]={0,1,0,-1};p->gx=(int)clamp((float)(p->gx+dx[dir]),0,4);p->gy=(int)clamp((float)(p->gy+dy[dir]),0,4);
        int timed=ontime(g,beat_at(g));if(timed){p->hits++;p->timing_hits++;p->score+=30;}else p->misses++;
        if(p->gx==p->target_x&&p->gy==p->target_y){p->collections++;p->score+=100;p->target_x=(p->collections*3+1)%5;p->target_y=(p->collections*2+3)%5;}
        feedback(g,p,timed?"IN TIME / STEP":"STEP / OFF PULSE");return;
    }
    if(g->mode==LAB_BLASTER){
        p->last_error_ms=(float)((g->position-beat_at(g))/g->rate);
        p->shots++;p->shot_at=g->position;p->shot_angle=p->angle;int timed=ontime(g,beat_at(g));if(timed)p->timing_hits++;
        struct xz_lab_event *best=NULL;float distance=.3f;
        for(unsigned i=0;i<g->event_count;i++){struct xz_lab_event *e=&g->events[i];float d=fabsf(wrap(p->angle-e->angle));if(e->grade[index]==LAB_PENDING&&g->position>=e->at-g->period*.6&&g->position<=e->end&&d<distance){best=e;distance=d;}}
        if(best){p->motion_hits++;p->hits++;p->score+=timed?100:50;best->grade[index]=timed?LAB_PERFECT:LAB_GOOD;if(timed)p->perfects++;feedback(g,p,timed?"HIT / IN TIME":"HIT / FREE SHOT");}
        else feedback(g,p,timed?"IN TIME / AIM CLOSER":"SHOT / AIM CLOSER");return;
    }
    struct xz_lab_event *e=nearest(g,index);
    if(!e){
        int watching=g->mode==LAB_ECHO&&(((int64_t)floor((g->position-g->origin)/g->period)-g->echo_base)%8+8)%8<4;
        if(!watching){p->misses++;p->streak=0;}
        feedback(g,p,watching?"WATCH FIRST":g->mode==LAB_ECHO?"REST HERE":"OFF THE DOT");return;
    }
    p->timing_hits++;
    if(g->mode==LAB_WHEEL_MATCH&&xz_lab_lane(p)!=(unsigned)e->lane){result(g,p,e,index,0,g->position-e->at,"WRONG LANE / TIMING OK");return;}
    p->motion_hits++;
    if(e->kind==LAB_HOLD){e->grade[index]=LAB_HOLDING;feedback(g,p,"HOLD THE STRIPE");}
    else result(g,p,e,index,1,g->position-e->at,NULL);
}
static void release(struct xz_rhythm_lab *g,unsigned index){
    struct xz_lab_player *p=&g->player[index];
    if(g->mode==LAB_SPRING){
        p->launch_at=g->position;p->launch_charge=p->charge;struct xz_lab_event *e=nearest(g,index);
        p->launch_grade=e&&p->charge>=.25f;
        if(e){int enough=p->charge>=.25f;if(enough)p->motion_hits++;p->timing_hits++;result(g,p,e,index,enough,g->position-e->at,enough?"RELEASE / LIFT":"PULL A LITTLE MORE");}
        else feedback(g,p,"RELEASED / OFF PULSE");p->charge=0;
    }
    if(g->mode==LAB_PULSE_HOLD)for(unsigned i=0;i<g->event_count;i++){
        struct xz_lab_event *e=&g->events[i];if(e->grade[index]!=LAB_HOLDING)continue;
        int good=ontime(g,e->end)&&e->covered[index]>=(e->end-e->at)*.7;
        result(g,p,e,index,good,g->position-e->end,good?"HOLD COMPLETE":g->position<e->end?"RELEASED EARLY":"RELEASED LATE");
    }
}
static void tap_pulse(struct xz_rhythm_lab *g){
    if(!g->valid||!g->playing)return;
    if(g->tap_count&&g->position-g->taps[(g->tap_count-1)%8]>350)g->tap_count=0;
    g->taps[g->tap_count++%8]=g->position;
    if(g->tap_count>=4){unsigned n=g->tap_count>8?8:g->tap_count;double sum=0;unsigned good=0;
        for(unsigned i=g->tap_count-n+1;i<g->tap_count;i++){double d=g->taps[i%8]-g->taps[(i-1)%8];if(d>=25&&d<=250){sum+=d;good++;}}
        if(good>=3){g->period=(float)(sum/good);g->origin=g->position;g->manual_pulse=1;reset_chart(g);}
    }
}
void xz_lab_button(struct xz_rhythm_lab *g,unsigned index,enum xz_lab_key key,int down,uint32_t now){
    if(index>1||key==LAB_PLATTER)return;xz_lab_step(g,now);
    if(g->phase==LAB_SETUP&&down&&(key==LAB_CUE||key==LAB_PAD_A)){
        if(g->setup_step==2)tap_pulse(g);
        if(g->setup_step==3&&g->calibrating){float v=fabsf(g->cal_pulses[g->cal_deck]);if(v>=256&&v<=50000){g->turn_pulses[g->cal_deck]=v;g->calibrating=0;}}
        return;
    }
    if(down&&key==LAB_PAD_H){if(g->phase==LAB_MENU)g->leave=1;else{g->phase=LAB_MENU;for(unsigned p=0;p<2;p++)g->player[p].held=0;}return;}
    if(down&&key==LAB_PAD_G){xz_lab_begin(g,now);return;}
    if(down&&key==LAB_PAD_F&&(g->phase==LAB_RUN||g->phase==LAB_PAUSE)){save_record(g);g->phase=LAB_RESULTS;for(unsigned p=0;p<2;p++)g->player[p].held=0;return;}
    if(down&&g->phase==LAB_MENU&&(key==LAB_CUE||key==LAB_PAD_A)){g->mode=(enum xz_lab_mode)g->menu_selected;g->phase=LAB_BRIEF;return;}
    if(down&&g->phase==LAB_BRIEF&&(key==LAB_CUE||key==LAB_PAD_A)){
        xz_lab_begin(g,now);
        if(g->phase==LAB_RUN||g->phase==LAB_PAUSE){g->player[index].held|=1u<<key;g->player[index].cue_edges++;}
        return;
    }
    if(key==LAB_PAD_B&&down&&active(g)&&g->mode==LAB_PUZZLE){g->player[index].pair_flip=!g->player[index].pair_flip;return;}
    if(key!=LAB_CUE&&key!=LAB_PAD_A&&key!=LAB_TOUCH)return;
    struct xz_lab_player *p=&g->player[index];uint32_t bit=1u<<key;int before=!!p->held;
    if(down)p->held|=bit;else p->held&=~bit;
    if(before!=!!p->held)p->cue_edges++;
    if(!active(g)&&!(g->mode==LAB_GLYPH&&g->phase==LAB_RUN&&g->valid&&g->playing))return;
    if(!before&&p->held)attack(g,index);else if(before&&!p->held)release(g,index);
}
void xz_lab_back(struct xz_rhythm_lab *g,uint32_t now){
    xz_lab_step(g,now);
    switch(g->phase){
    case LAB_MENU:g->leave=1;break;
    case LAB_CONTROLS:g->phase=(enum xz_lab_phase)g->controls_return;break;
    case LAB_SETUP:g->calibrating=0;if(g->setup_step)g->setup_step--;else g->phase=LAB_MENU;break;
    /* Glyph Run opens on its brief, so BACK there leaves like DECK does. */
    case LAB_BRIEF:if(g->mode==LAB_GLYPH)g->leave=1;else g->phase=LAB_MENU;break;
    case LAB_RATINGS:case LAB_RESULTS:g->phase=LAB_MENU;break;
    default:break;
    }
}
void xz_lab_touch(struct xz_rhythm_lab *g,int x,int y,int down,uint32_t now){
    int pressed=down&&!g->touch_down,released=!down&&g->touch_down;g->touch_down=!!down;xz_lab_step(g,now);
    if(released&&g->touch_player>=0){xz_lab_button(g,(unsigned)g->touch_player,LAB_TOUCH,0,now);g->touch_player=-1;}
    if(!pressed||x<0||x>=800||y<0||y>=480)return;
    if(x>=704&&y<48){g->leave=1;return;}
    if(g->phase==LAB_PAUSE&&g->wave.position>=g->wave.total-1&&x>=220&&x<580&&y>=204&&y<246){g->leave=1;return;}
    if(g->phase==LAB_MENU){
        if(y>=96&&y<396){unsigned row=(unsigned)(y-96)/60,col=x>=400;unsigned n=row+col*5;if(n<10){g->menu_selected=n;g->mode=(enum xz_lab_mode)n;g->phase=LAB_BRIEF;}}
        else if(y>=424){if(x<160){g->phase=LAB_SETUP;g->setup_step=0;}else if(x<320)open_controls(g);else if(x<480)g->study=!g->study;else if(x<640)g->duo=!g->duo;else g->phase=LAB_RATINGS;}
        return;
    }
    if(g->phase==LAB_CONTROLS){
        if(y>=430){g->phase=(enum xz_lab_phase)g->controls_return;return;}
        if(y>=120&&y<402){unsigned row=(unsigned)(y-120)/58,deck=x>=400;if(row<5&&(y-120)%58<50){g->action_binding[deck]=row;for(unsigned i=0;i<2;i++)g->player[i].held=0;}}
        return;
    }
    if(g->phase==LAB_RATINGS){if(y>=424)g->phase=LAB_MENU;return;}
    if(g->phase==LAB_SETUP){
        if(y>=430&&x>=180&&x<600){open_controls(g);return;}
        if(y>=430&&x<180){g->calibrating=0;if(g->setup_step)g->setup_step--;else g->phase=LAB_MENU;return;}
        if(y>=430&&x>=600){g->leave=1;return;}
        if(g->setup_step==0){if(y>=150&&y<230){g->deck=x>=400;g->manual_pulse=0;g->tap_count=0;}if(y>=330&&y<410)g->setup_step=1;}
        else if(g->setup_step==1){
            if(y>=270&&y<315){g->setup_step=2;g->tap_count=0;}
            if(y>=330&&y<410){if(g->valid&&(g->manual_pulse||g->wave.pulse_confidence>=.25f))g->setup_step=3;else if(g->valid){g->setup_step=2;g->tap_count=0;}else g->leave=1;}
        }else if(g->setup_step==2){
            if(y>=165&&y<270)tap_pulse(g);
            if(y>=285&&y<325){float change=x<400?-20:20;g->timing_ms+=change;g->origin+=change*g->rate;}
            if(y>=345&&y<410&&(g->manual_pulse||g->wave.pulse_confidence>=.25f))g->setup_step=3;
        }else{
            if(y>=140&&y<210){g->cal_deck=x>=400;g->control_deck=g->cal_deck;}
            if(y>=240&&y<295){g->cal_pulses[0]=g->cal_pulses[1]=0;g->calibrating=1;}
            if(y>=330&&y<410){g->calibrating=0;g->phase=LAB_BRIEF;}
        }
        return;
    }
    if(g->phase==LAB_BRIEF){
        if(g->mode==LAB_GLYPH&&y>=430){if(x<160)g->phase=LAB_MENU;else if(x<320)open_controls(g);else if(x<600){g->phase=LAB_SETUP;g->setup_step=0;}else g->duo=!g->duo;return;}
        if(y>=330&&y<410){xz_lab_begin(g,now);return;}
        if(y>=430){if(x<220)g->phase=LAB_MENU;else{g->phase=LAB_SETUP;g->setup_step=0;}}return;
    }
    if(g->phase==LAB_RESULTS){
        if(y>=164&&y<332&&x>=230&&x<670){unsigned row=(unsigned)(y-164)/56,rating=(unsigned)(x-230)/88+1;struct xz_lab_record *r=&g->record[g->mode];if(row==0)r->fun=rating;else if(row==1)r->clarity=rating;else r->effort=rating;}
        if(y>=350&&y<408){if(x<266){g->record[g->mode].wants_replay=1;xz_lab_begin(g,now);}else if(x<534){g->duration=g->elapsed+90;g->phase=g->playing?LAB_RUN:LAB_PAUSE;}else if(g->mode==LAB_GLYPH){g->mode=0;g->menu_selected=0;g->phase=LAB_MENU;}else{g->mode=(enum xz_lab_mode)((g->mode+1)%10);g->menu_selected=g->mode;g->phase=LAB_BRIEF;}}
        if(y>=430)g->phase=LAB_MENU;return;
    }
    if(y>=430){if(x<160)xz_lab_begin(g,now);else if(x<320)open_controls(g);else if(x>=640)g->phase=LAB_MENU;else if(x>=520){save_record(g);g->phase=LAB_RESULTS;for(unsigned p=0;p<2;p++)g->player[p].held=0;}else if(g->phase==LAB_RUN){g->touch_player=0;xz_lab_button(g,0,LAB_TOUCH,1,now);}return;}
}
