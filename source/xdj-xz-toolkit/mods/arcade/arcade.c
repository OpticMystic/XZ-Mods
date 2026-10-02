/* SPDX-License-Identifier: MIT */
#include "arcade.h"
#include <math.h>
#include <string.h>

static float clamp(float v,float a,float b){return v<a?a:v>b?b:v;}
static int breaks(const struct xz_arcade *g){return g->mode>=XZ_ARCADE_BREAK_SOLO;}
static int two(const struct xz_arcade *g){return g->mode==XZ_ARCADE_PONG_DUEL||g->mode==XZ_ARCADE_BREAK_COOP;}
float xz_arcade_jog_gain(const struct xz_arcade *g){static const float gains[3]={.35f,.55f,.85f};return gains[g->sensitivity%3];}
float xz_arcade_paddle_size(const struct xz_arcade *g,int p){
    static const float pong[3]={132,112,92},brick[3]={160,144,120};
    return (breaks(g)?brick[g->difficulty%3]:pong[g->difficulty%3])+(g->player[p].wide_until>g->beat?40.f:0.f);
}
static void message(struct xz_arcade *g,const char *s,int p){g->feedback=s;g->feedback_player=p;g->feedback_until=g->beat+1.5;}
static void normalize(struct xz_arcade *g){float n=sqrtf(g->dx*g->dx+g->dy*g->dy);g->dx/=n;g->dy/=n;}
static void brick_rect(int i,float *x,float *y){*x=88.f+(i%8)*78.f;*y=104.f+(i/8)*29.f;}
static void board(struct xz_arcade *g){
    unsigned rows=g->stage<3?4:5;g->remaining=rows*8;
    for(unsigned i=0;i<48;i++)g->bricks[i]=i<rows*8?(uint8_t)(1+(g->stage>1&&(i/8+g->stage)%3==0)):0;
}
/* Nearest geometric contact first; time is chosen afterwards. The immutable
 * segment keeps moving paddles and tempo corrections from bending the ball. */
static void candidate(struct xz_arcade *g,float t,float nx,float ny,enum xz_arcade_contact c,int index,float *best){
    if(t>.05f&&t<*best){*best=t;g->flight.nx=nx;g->flight.ny=ny;g->flight.contact=c;g->flight.index=index;}
}
static void plan(struct xz_arcade *g){
    float t=100000.f;
    if(g->dy<-.001f)candidate(g,(76-g->y)/g->dy,0,1,XZ_ARCADE_WALL,0,&t);
    if(g->dy>.001f)candidate(g,((breaks(g)?389.f:407.f)-g->y)/g->dy,0,-1,breaks(g)?XZ_ARCADE_PADDLE:XZ_ARCADE_WALL,breaks(g)?2:0,&t);
    if(g->dx<-.001f)candidate(g,((breaks(g)?30.f:63.f)-g->x)/g->dx,1,0,breaks(g)?XZ_ARCADE_WALL:XZ_ARCADE_PADDLE,0,&t);
    if(g->dx>.001f)candidate(g,((breaks(g)?770.f:737.f)-g->x)/g->dx,-1,0,breaks(g)?XZ_ARCADE_WALL:XZ_ARCADE_PADDLE,1,&t);
    if(breaks(g))for(int i=0;i<48;i++)if(g->bricks[i]){
        float x,y;brick_rect(i,&x,&y);
        float left=x-7,right=x+76,top=y-7,bottom=y+28;
        float tx0=-100000,tx1=100000,ty0=-100000,ty1=100000;
        if(fabsf(g->dx)<.001f){if(g->x<left||g->x>right)continue;}
        else {tx0=(left-g->x)/g->dx;tx1=(right-g->x)/g->dx;if(tx0>tx1){float swap=tx0;tx0=tx1;tx1=swap;}}
        if(fabsf(g->dy)<.001f){if(g->y<top||g->y>bottom)continue;}
        else {ty0=(top-g->y)/g->dy;ty1=(bottom-g->y)/g->dy;if(ty0>ty1){float swap=ty0;ty0=ty1;ty1=swap;}}
        float near=tx0>ty0?tx0:ty0,far=tx1<ty1?tx1:ty1;
        if(near<=far&&far>.05f)candidate(g,near,tx0>ty0?(g->dx>0?-1.f:1.f):0,ty0>=tx0?(g->dy>0?-1.f:1.f):0,XZ_ARCADE_BRICK,i,&t);
    }
    static const float pong_speed[3]={280,340,400},brick_speed[3]={180,220,270};
    float speed=breaks(g)?brick_speed[g->difficulty%3]:pong_speed[g->difficulty%3];
    speed+=clamp((float)g->rally*3,0,g->difficulty==0?40:90);
    if(g->last_player>=0&&g->player[g->last_player].drop_until>g->beat)speed*=1.10f;
    double duration=t/speed*g->bpm/60.;
    double division=g->flight.contact==XZ_ARCADE_PADDLE?1.:2.;
    double end=ceil((g->clock+duration)*division-1e-7)/division;
    if(end-g->clock<.22)end=ceil((g->clock+.22)*division)/division;
    g->flight.x0=g->x;g->flight.y0=g->y;
    g->flight.x1=g->x+g->dx*t;g->flight.y1=g->y+g->dy*t;
    g->flight.start=g->beat;g->flight.end=g->beat+end-g->clock;
}
static void serve(struct xz_arcade *g,int target){
    g->x=400;g->y=breaks(g)?330.f:242.f;
    g->dx=breaks(g)?.45f:target?1.f:-1.f;g->dy=breaks(g)?-.9f:.23f;
    normalize(g);g->rally=0;g->last_player=-1;g->trail_count=0;
    g->phase=g->music_known&&!g->music_playing?XZ_ARCADE_WAIT_TRACK:XZ_ARCADE_COUNT_IN;
    g->count_until=ceil(g->clock-1e-6)+4;
}
void xz_arcade_init(struct xz_arcade *g,uint32_t now){
    memset(g,0,sizeof(*g));g->initialized=1;g->last_ms=now;g->bpm=120;g->lives=5;g->stage=1;g->winner=-1;
    g->clock_deck=2;g->sensitivity=1;
    g->last_player=-1;g->feedback_player=-1;g->feedback="JOG TO MOVE / CUE TO THE BEAT";
}
void xz_arcade_start(struct xz_arcade *g,enum xz_arcade_mode mode,uint32_t now){
    struct xz_arcade previous=*g;
    xz_arcade_init(g,now);g->mode=mode;g->bpm=previous.bpm;g->clock=previous.clock;g->live=previous.live;g->live_ms=previous.live_ms;
    g->clock_deck=previous.clock_deck;g->style=previous.style;g->quiet=previous.quiet;g->difficulty=previous.difficulty;g->sensitivity=previous.sensitivity;
    g->clock_selected=previous.clock_selected;g->clock_source=previous.clock_source;g->music_known=previous.music_known;
    g->music_playing=previous.music_playing;g->music_at=previous.music_at;g->track_id=previous.track_id;g->phase_aligned=previous.phase_aligned;
    g->lives=g->difficulty==0?5:3;
    g->round_active=1;
    g->player[0].position=breaks(g)?(two(g)?210.f:400.f):242.f;
    g->player[1].position=breaks(g)?590.f:242.f;
    for(int p=0;p<2;p++){g->player[p].strike=-100;g->player[p].contact=-100;g->player[p].energy=3;g->player[p].contact_graded=1;g->player[p].last_note=-100;g->player[p].note_at=-100;}
    if(breaks(g))board(g);
    serve(g,1);message(g,"GET READY / FOUR BEATS",-1);
}
static void perfect(struct xz_arcade *g,int p){
    struct xz_arcade_player *d=&g->player[p];
    if(d->contact_graded)return;
    d->contact_graded=1;d->energy=d->energy+1>9?9:d->energy+1;
    message(g,"ON-BEAT RETURN",p);g->team_score+=25;
}
static void miss(struct xz_arcade *g,int p){
    if(g->player[p].shield){g->player[p].shield=0;g->dx=-g->dx;g->dy=-fabsf(g->dy);message(g,"SHIELD SAVE",p);plan(g);return;}
    g->rally=0;
    if(breaks(g)){
        if(--g->lives==0){g->phase=XZ_ARCADE_RESULT;message(g,"SET COMPLETE",-1);return;}
    }else {
        unsigned winner=(unsigned)(1-p);
        if(++g->player[winner].score>=7){g->winner=(int)winner;g->phase=XZ_ARCADE_RESULT;message(g,"MATCH POINT",(int)winner);return;}
    }
    serve(g,p);message(g,"NEXT RALLY",-1);
}
static void hit(struct xz_arcade *g){
    struct xz_arcade_flight f=g->flight;
    g->impact_x=g->x;g->impact_y=g->y;g->impact_until=g->beat+.55;
    if(f.contact==XZ_ARCADE_PADDLE){
        int p=f.index;
        if(breaks(g)){
            float a=fabsf(g->x-g->player[0].position),b=fabsf(g->x-g->player[1].position);
            p=two(g)&&b<a?1:0;
            if(a>xz_arcade_paddle_size(g,0)/2+13&&(!two(g)||b>xz_arcade_paddle_size(g,1)/2+13)){miss(g,p);return;}
        }else if(fabsf(g->y-g->player[p].position)>xz_arcade_paddle_size(g,p)/2+13){miss(g,p);return;}
        struct xz_arcade_player *d=&g->player[p];
        float offset=((breaks(g)?g->x:g->y)-d->position)/(xz_arcade_paddle_size(g,p)/2);
        offset=clamp(offset+d->aim,-.85f,.85f);
        if(breaks(g)){
            float direction=g->dx<0?-1.f:1.f;
            g->dx=offset*.86f;
            if(fabsf(g->dx)<.18f)g->dx=.23f*direction;
            g->dy=-sqrtf(1-g->dx*g->dx);
        }
        else {g->dy=offset*.86f;g->dx=(p?-1.f:1.f)*sqrtf(1-g->dy*g->dy);}
        d->aim=0;d->contact=g->beat;d->contact_graded=0;
        d->energy=d->energy<9?d->energy+1:9;
        g->rally++;if(g->rally>g->best)g->best=g->rally;
        g->team_score+=10+(g->last_player>=0&&g->last_player!=p?10:0);
        g->last_player=p;
        if(fabs(g->beat-d->strike)*60000./g->bpm<=115)perfect(g,p);
        else if(g->note_flash<g->clock)message(g,"NICE RETURN",p);
    }else if(f.contact==XZ_ARCADE_BRICK){
        int i=f.index;
        int drop=g->last_player>=0&&g->player[g->last_player].drop_until>g->beat;
        if(drop||!--g->bricks[i]){g->bricks[i]=0;g->remaining--;g->team_score+=50;}
        else g->team_score+=10;
        if(!g->remaining){g->stage++;board(g);serve(g,1);g->drop_visual_until=g->beat+4;message(g,"NEXT PATTERN",-1);return;}
        if(!drop){if(f.nx)g->dx=-g->dx;if(f.ny)g->dy=-g->dy;}
        message(g,drop?"DROP / BRICK SMASH":"BREAK",g->last_player);
    }else {if(f.nx)g->dx=-g->dx;if(f.ny)g->dy=-g->dy;}
    g->x+=g->dx*.1f;g->y+=g->dy*.1f;plan(g);
}
void xz_arcade_step(struct xz_arcade *g,uint32_t now){
    uint32_t elapsed=now-g->last_ms;
    if(elapsed>UINT32_MAX/2)return;
    g->last_ms=now;
    /* A suspended renderer must not simulate an unseen series of losses. */
    if(elapsed>500&&g->phase==XZ_ARCADE_PLAY){g->phase=XZ_ARCADE_MUSIC;message(g,"PAUSED / DISPLAY RESUMED",-1);}
    double delta=(elapsed>500?0:elapsed)*g->bpm/60000.;
    if(!g->music_known||g->music_playing)g->clock+=delta;
    if(g->live&&now-g->live_ms>4000)g->live=0;
    if(g->phase==XZ_ARCADE_COUNT_IN){
        if(g->clock+1e-6>=g->count_until){
            g->phase=XZ_ARCADE_PLAY;
            if(g->drop_pending)g->drop_at=(floor(g->clock/4)+1)*4;
            plan(g);message(g,"CUE / BUILD THE GROOVE",-1);
        }
        return;
    }
    if(g->phase!=XZ_ARCADE_PLAY)return;
    if(g->drop_pending&&g->clock>=g->drop_at){
        g->drop_pending=0;g->drops++;g->drop_visual_until=g->beat+8;
        for(int p=0;p<2;p++)g->player[p].drop_until=g->beat+8;
        message(g,"THE DROP / 8 BEATS",-1);
    }
    if(g->mode==XZ_ARCADE_PONG_SOLO){
        float target=g->dx>0?g->flight.y1:242.f;
        /* Human-speed pursuit with deterministic error; never reads the player's input. */
        target+=sinf((float)g->beat*.73f)*24.f;
        float limit=(float)elapsed*(g->difficulty==0?.13f:.18f);
        float half=xz_arcade_paddle_size(g,1)/2;
        g->player[1].position=clamp(g->player[1].position+clamp(target-g->player[1].position,-limit,limit),76+half,407-half);
    }
    double until=g->beat+delta;
    for(int events=0;g->phase==XZ_ARCADE_PLAY&&g->flight.end<=until+1e-7&&events<16;events++){
        double dt=g->flight.end-g->beat;g->beat=g->flight.end;
        /* Reconstruct the music phase at contact, not the later render timestamp. */
        g->clock-=until-g->beat;
        g->x=g->flight.x1;g->y=g->flight.y1;hit(g);
        g->clock+=until-g->beat;
        if(dt<0)break;
    }
    if(g->phase==XZ_ARCADE_PLAY){
        g->beat=until;
        double t=(g->beat-g->flight.start)/(g->flight.end-g->flight.start);
        t=t<0?0:t>1?1:t;
        g->x=g->flight.x0+(g->flight.x1-g->flight.x0)*(float)t;
        g->y=g->flight.y0+(g->flight.y1-g->flight.y0)*(float)t;
        unsigned i=g->trail_head++%12;g->trail_x[i]=g->x;g->trail_y[i]=g->y;if(g->trail_count<12)g->trail_count++;
    }
}
void xz_arcade_move(struct xz_arcade *g,int deck,float pixels,uint32_t now){
    if(deck<0||deck>1||!isfinite(pixels))return;
    xz_arcade_step(g,now);
    if(g->phase==XZ_ARCADE_MENU){
        g->menu_jog+=pixels;
        if(fabsf(g->menu_jog)>=18){g->mode=(enum xz_arcade_mode)(((int)g->mode+(g->menu_jog>0?1:3))%4);g->menu_jog=0;}
        return;
    }
    if(g->phase!=XZ_ARCADE_PLAY&&g->phase!=XZ_ARCADE_COUNT_IN&&g->phase!=XZ_ARCADE_WAIT_TRACK)return;
    if(!two(g)&&deck)return;
    struct xz_arcade_player *p=&g->player[deck];
    float half=xz_arcade_paddle_size(g,deck)/2;
    float lo=breaks(g)?30+half:76+half,hi=breaks(g)?770-half:407-half;
    if(g->mode==XZ_ARCADE_BREAK_COOP){lo=deck?400-half:30+half;hi=deck?770-half:400+half;}
    p->velocity=clamp(pixels,-30,30);p->position=clamp(p->position+clamp(pixels,-36,36),lo,hi);
}
static void align_clock(struct xz_arcade *g,double target){
    double error=target-g->clock;
    if(g->phase==XZ_ARCADE_PLAY){
        double remaining=g->flight.end-g->beat;
        double progress=(g->beat-g->flight.start)/(g->flight.end-g->flight.start);
        if(remaining>.16&&progress>=0&&progress<.99){
            double correction=error<-.10?-.10:error>.10?.10:error;
            double next=remaining-correction;
            g->flight.start=g->beat-progress*next/(1-progress);
            g->flight.end=g->beat+next;
        }
    }
    g->clock=target;
}
static void groove_note(struct xz_arcade *g,int deck){
    struct xz_arcade_player *p=&g->player[deck];
    if(g->clock_source==3&&!g->phase_aligned){align_clock(g,round(g->clock));g->phase_aligned=1;}
    double note=round(g->clock);
    if(note==p->last_note)return;
    p->timing_ms=(float)((g->clock-note)*60000./g->bpm);
    g->note_flash=g->clock+.6;
    if(fabsf(p->timing_ms)>115){p->combo=0;message(g,p->timing_ms<0?"EARLY / FOLLOW THE PULSE":"LATE / FOLLOW THE PULSE",deck);return;}
    p->last_note=note;
    p->combo=g->clock-p->note_at>1.6?1:p->combo+1;p->note_at=g->clock;p->notes++;
    if(p->combo>p->best_combo)p->best_combo=p->combo;
    unsigned perfect_hit=fabsf(p->timing_ms)<=48;
    p->perfects+=perfect_hit;p->energy+=perfect_hit?2:1;if(p->energy>9)p->energy=9;
    g->team_score+=perfect_hit?50:25;g->groove++;
    if(two(g)&&g->player[1-deck].last_note==note&&fabsf(g->player[1-deck].timing_ms)<=115){g->team_score+=25;message(g,"DUET / TOGETHER",deck);}
    else message(g,perfect_hit?"PERFECT":"IN THE GROOVE",deck);
    if(p->combo==8&&!p->shield){p->shield=1;message(g,"8 BEATS / FREE SHIELD",deck);}
    if(g->groove>=16&&!g->drop_pending){g->groove=0;g->drop_pending=1;g->drop_at=(floor(g->clock/4)+1)*4;message(g,"DROP ON THE NEXT ONE",-1);}
    p->strike=g->beat;
    if(g->beat-p->contact>=0&&(g->beat-p->contact)*60000./g->bpm<=115)perfect(g,deck);
}
void xz_arcade_key(struct xz_arcade *g,int deck,enum xz_arcade_key key,uint32_t now){
    if(deck<0||deck>1||key==XZ_ARCADE_PLAY_KEY)return;
    xz_arcade_step(g,now);
    if(key==XZ_ARCADE_PAUSE_KEY){g->phase=XZ_ARCADE_MUSIC;g->player[0].ready=g->player[1].ready=0;return;}
    if(key==XZ_ARCADE_PAD_H){
        uint32_t dt=now-g->tap_ms;
        int mixer=g->clock_source==3&&(uint32_t)(now-g->music_at)<2000;
        if(!mixer&&g->tap_ms&&dt>=250&&dt<=1500){
            g->tap_intervals[g->taps%4]=dt;g->taps++;unsigned n=g->taps<4?g->taps:4;uint32_t sum=0;
            for(unsigned i=0;i<n;i++)sum+=g->tap_intervals[i];g->bpm=clamp(60000.f*n/sum,40,240);
        }else if(!mixer)g->taps=0;
        g->tap_ms=now;align_clock(g,round(g->clock));g->phase_aligned=1;g->live=0;g->music_known=0;
        if(!mixer){g->clock_deck=3;g->clock_source=4;}
        if(g->phase==XZ_ARCADE_WAIT_TRACK){g->phase=XZ_ARCADE_COUNT_IN;g->count_until=ceil(g->clock)+4;}
        message(g,mixer?"MIXER BEAT ALIGNED":"TAP CLOCK ALIGNED",deck);return;
    }
    int fire=key==XZ_ARCADE_CUE_KEY||key==XZ_ARCADE_PAD_A;
    if(g->phase==XZ_ARCADE_MUSIC){
        if(fire){
            if(!g->round_active){g->phase=XZ_ARCADE_MENU;return;}
            if(g->winner>=0||!g->lives){g->phase=XZ_ARCADE_RESULT;return;}
            g->player[deck].ready=1;
            if(g->player[0].ready&&(!two(g)||g->player[1].ready)){
                g->phase=g->music_known&&!g->music_playing?XZ_ARCADE_WAIT_TRACK:XZ_ARCADE_COUNT_IN;g->count_until=ceil(g->clock)+4;
            }
        }
        return;
    }
    if(g->phase==XZ_ARCADE_RESULT){if(fire)xz_arcade_start(g,g->mode,now);return;}
    if(g->phase==XZ_ARCADE_MENU){if(fire)xz_arcade_start(g,g->mode,now);return;}
    if(g->phase!=XZ_ARCADE_PLAY&&g->phase!=XZ_ARCADE_COUNT_IN)return;
    if(!two(g)&&deck)return;
    struct xz_arcade_player *p=&g->player[deck];
    if(fire){if(g->phase==XZ_ARCADE_PLAY)groove_note(g,deck);}
    else if(key==XZ_ARCADE_PAD_B||key==XZ_ARCADE_PAD_C||key==XZ_ARCADE_PAD_D){
        unsigned cost=key==XZ_ARCADE_PAD_B?3:key==XZ_ARCADE_PAD_C?4:6;
        if(p->energy<cost){message(g,"CUE ON BEAT BUILDS CHARGE",deck);return;}
        if(key==XZ_ARCADE_PAD_C&&p->shield){message(g,"SHIELD ALREADY ARMED",deck);return;}
        p->energy-=cost;
        if(key==XZ_ARCADE_PAD_B){p->wide_until=g->beat+16;message(g,"WIDE / 16 BEATS",deck);}
        if(key==XZ_ARCADE_PAD_C){p->shield=1;message(g,"SHIELD / ONE SAVE",deck);}
        if(key==XZ_ARCADE_PAD_D){
            g->drop_pending=1;g->drop_at=(floor(g->clock/4)+1)*4;message(g,"DROP ON THE NEXT ONE",deck);
        }
    }else if(key==XZ_ARCADE_PAD_E)p->aim=-.45f;
    else if(key==XZ_ARCADE_PAD_F)p->aim=.45f;
    else if(key==XZ_ARCADE_PAD_G)g->style=(g->style+1)%3;
}
void xz_arcade_sync(struct xz_arcade *g,float bpm,unsigned bar,uint32_t at){
    if(!isfinite(bpm)||bpm<40||bpm>240||bar<1||bar>4||g->clock_deck>=3)return;
    xz_arcade_step(g,at);g->bpm=bpm;g->live=1;g->live_ms=at;g->clock_source=2;g->phase_aligned=1;g->music_known=0;
    double target=round((g->clock-(bar-1))/4.)*4.+bar-1;
    if(g->phase==XZ_ARCADE_WAIT_TRACK){g->phase=XZ_ARCADE_COUNT_IN;g->count_until=ceil(target)+4;}
    align_clock(g,target);
}
void xz_arcade_music(struct xz_arcade *g,float bpm,float phase,unsigned playing,uint32_t track,unsigned deck,uint32_t at){
    if(!isfinite(bpm)||bpm<40||bpm>240||!isfinite(phase)||phase<0||phase>=4||deck>1||g->clock_deck>=3)return;
    xz_arcade_step(g,at);
    double target=round((g->clock-phase)/4.)*4.+phase;
    int changed=g->music_known&&(g->track_id!=track||fabs(target-g->clock)>.25);
    int acquired=!g->music_known;
    g->bpm=bpm;g->music_known=1;g->music_playing=!!playing;g->music_at=at;g->track_id=track;g->clock_selected=deck;
    g->live=1;g->live_ms=at;g->clock_source=1;g->phase_aligned=1;
    if(!playing&&(g->phase==XZ_ARCADE_PLAY||g->phase==XZ_ARCADE_COUNT_IN)){g->phase=XZ_ARCADE_WAIT_TRACK;g->trail_count=0;}
    if(changed)for(int p=0;p<2;p++){g->player[p].last_note=-100;g->player[p].note_at=-100;g->player[p].combo=0;}
    if(playing&&(g->phase==XZ_ARCADE_WAIT_TRACK||((changed||acquired)&&(g->phase==XZ_ARCADE_PLAY||g->phase==XZ_ARCADE_COUNT_IN)))){
        g->phase=XZ_ARCADE_COUNT_IN;g->count_until=ceil(target)+4;g->trail_count=0;message(g,"CATCH THE NEW GROOVE",-1);
    }
    align_clock(g,target);
}
void xz_arcade_music_missing(struct xz_arcade *g){
    if(g->clock_deck>=3)return;
    g->live=0;
    if(g->music_known){g->music_playing=0;if(g->phase==XZ_ARCADE_PLAY||g->phase==XZ_ARCADE_COUNT_IN)g->phase=XZ_ARCADE_WAIT_TRACK;}
}
void xz_arcade_tempo(struct xz_arcade *g,float bpm,uint32_t at){
    if(!isfinite(bpm)||bpm<40||bpm>240)return;
    xz_arcade_step(g,at);
    if(g->clock_source!=3)g->phase_aligned=0;
    g->clock_source=3;g->bpm=bpm;g->music_known=0;g->live=0;g->music_at=at;
}
void xz_arcade_touch(struct xz_arcade *g,int x,int y,int down,uint32_t now){
    int press=down&&!g->touch_down;g->touch_down=!!down;xz_arcade_step(g,now);
    if(!press)return;
    if(x>=674&&y<54){xz_arcade_key(g,0,XZ_ARCADE_PAUSE_KEY,now);return;}
    if(x>=230&&x<574&&y<42&&g->phase!=XZ_ARCADE_CLOCK){g->clock_return=g->round_active?(unsigned)g->phase:0;g->phase=XZ_ARCADE_CLOCK;return;}
    if(x>=578&&x<670&&y<54){g->quiet=!g->quiet;return;}
    if(g->phase==XZ_ARCADE_MENU){
        if(y>=118&&y<298){int col=x>=400,row=y>=208;if(x>=40&&x<760)xz_arcade_start(g,(enum xz_arcade_mode)(row*2+col),now);}
        if(y>=322&&y<378){g->clock_return=0;g->phase=XZ_ARCADE_CLOCK;}
        if(y>=392&&y<440){if(x<400)g->sensitivity=(g->sensitivity+1)%3;else g->difficulty=(g->difficulty+1)%3;}
    }else if(g->phase==XZ_ARCADE_CLOCK){
        if(y>=148&&y<207)g->clock_deck=x<280?2:x<520?0:1;
        if(y>=216&&y<268){g->clock_deck=x<400?4:3;g->music_known=0;if(g->clock_deck==3)g->clock_source=4;}
        if(y>=280&&y<346)xz_arcade_key(g,0,XZ_ARCADE_PAD_H,now);
        if(y>=366&&y<432){
            g->phase=g->clock_return==XZ_ARCADE_MUSIC?XZ_ARCADE_MUSIC:g->clock_return?(g->music_known&&!g->music_playing?XZ_ARCADE_WAIT_TRACK:XZ_ARCADE_COUNT_IN):XZ_ARCADE_MENU;
            g->count_until=ceil(g->clock)+4;
        }
    }else if(g->phase==XZ_ARCADE_MUSIC){
        if(y>=188&&y<280){xz_arcade_key(g,x<400?0:1,XZ_ARCADE_CUE_KEY,now);}
        if(y>=316&&y<382)g->phase=XZ_ARCADE_MENU;
    }else if(g->phase==XZ_ARCADE_RESULT){if(y>=190&&y<280)xz_arcade_start(g,g->mode,now);if(y>=316&&y<382)g->phase=XZ_ARCADE_MENU;}
    else if(y>=435&&x>=12&&x<796){int pad=(x-12)/98;if(pad<8)xz_arcade_key(g,g->last_player>=0?g->last_player:0,(enum xz_arcade_key)(XZ_ARCADE_PAD_A+pad),now);}
}
