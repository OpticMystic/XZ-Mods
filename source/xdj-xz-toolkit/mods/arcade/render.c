/* SPDX-License-Identifier: MIT */
#include "arcade.h"
#include "../ui/ui_skin.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const uint32_t cyan=0x55efff,amber=0xffbd69,ink=0xf1f7ff,dim=0x9ba9c7,pink=0xff71cb;
static const char *modes[4]={"SOLO PONG","PONG DUEL","SOLO BREAKS","BACK2BACK"};
static const char *levels[3]={"CHILL","FLOW","CLUB"},*jogs[3]={"FINE","SMOOTH","FAST"};
static uint32_t color(int p){return p==1?amber:cyan;}
static uint16_t pixel(uint32_t c){return (uint16_t)(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31));}
static void text(struct xz_canvas c,int x,int y,const char *s,int n,uint32_t col){xz_text(c,x,y,s,n,100,col);}
static void center(struct xz_canvas c,int x,int y,const char *s,int n,uint32_t col){text(c,x-xz_text_width(c,s,n,100)/2,y,s,n,col);}
/* Bounded wireframe strokes avoid scanning the interior of large rings on ARM. */
static void line(struct xz_canvas c,int x0,int y0,int x1,int y1,uint32_t col){
    int dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,err=dx+dy;uint16_t value=pixel(col);
    for(int limit=0;limit<2000;limit++){
        if(x0>=24&&x0<776&&y0>=76&&y0<414)c.p[(size_t)y0*c.stride+(size_t)x0]=value;
        if(x0==x1&&y0==y1)break;
        int e=2*err;if(e>=dy){err+=dy;x0+=sx;}if(e<=dx){err+=dx;y0+=sy;}
    }
}
static void polygon(struct xz_canvas c,int cx,int cy,int rx,int ry,int rotation,int sides,uint32_t col){
    int px=cx+xz_cos1024(rotation)*rx/1024,py=cy+xz_sin1024(rotation)*ry/1024;
    for(int i=1;i<=sides;i++){int a=rotation+i*360/sides,x=cx+xz_cos1024(a)*rx/1024,y=cy+xz_sin1024(a)*ry/1024;line(c,px,py,x,y,col);px=x;py=y;}
}
static void backdrop(struct xz_canvas c,const struct xz_arcade *g){
    double phase=g->clock-floor(g->clock),bar=fmod(g->clock,4.)/4.;
    int drop=g->drop_visual_until>g->beat;
    for(int y=0;y<480;y++){
        unsigned glow=(unsigned)(20+12*xz_sin1024(y/2)/1024);
        xz_rect(c,0,y,800,1,xz_blend(0x040615,0x182347,glow));
    }
    if(g->quiet)return;
    /* Small lookup field, expanded straight into RGB565. No per-pixel trig,
       assets, frame allocation or GPU dependency on the XZ. */
    int horizontal[200],diagonal[284];uint16_t palette[256];int motion=(int)(g->clock*(drop?28:14));
    for(int i=0;i<200;i++)horizontal[i]=xz_sin1024(i*3+motion);
    for(int i=0;i<284;i++)diagonal[i]=xz_cos1024(i*2-motion);
    for(unsigned i=0;i<256;i++){
        uint32_t col=i<128?xz_blend(0x050918,0x112b3c,i*2):xz_blend(0x112b3c,drop?0x481932:0x271334,(i-128)*2);
        palette[i]=pixel(col);
    }
    for(int row=0;row<84;row++){
        int vertical=xz_cos1024(row*5-motion);
        for(int col=6;col<194;col++){
            unsigned index=(unsigned)(horizontal[col]+vertical+diagonal[col+row]+3072)*255/6144;
            uint16_t value=palette[index];
            for(int yy=0;yy<4;yy++){uint16_t *dst=c.p+(size_t)(78+row*4+yy)*c.stride+(size_t)col*4;dst[0]=dst[1]=dst[2]=dst[3]=value;}
        }
    }
    uint32_t deep=drop?0x302044:0x18233d,accent=drop?0x825282:0x285369;
    if(g->style==0){
        int rotate=12+(int)(bar*12)+(drop?(int)(g->clock*30):0),cx=400+(xz_sin1024((int)(g->clock*8))*24/1024),cy=238;
        for(int i=0;i<9;i++){
            double z=(i+phase)/9.;int rx=24+(int)(z*z*470),ry=14+(int)(z*z*208);
            polygon(c,cx,cy,rx,ry,rotate,8,xz_blend(deep,accent,(unsigned)(z*90)));
        }
        for(int i=0;i<8;i++){int a=rotate+i*45;line(c,cx+xz_cos1024(a)*25/1024,cy+xz_sin1024(a)*14/1024,cx+xz_cos1024(a)*500/1024,cy+xz_sin1024(a)*225/1024,deep);}
    }else if(g->style==1){
        for(int x=-640;x<=1440;x+=100)line(c,400+(x-400)/9,200,x,414,deep);
        for(int i=0;i<10;i++){double z=(i+phase)/10.;int y=204+(int)(z*z*210);line(c,24,y,775,y,xz_blend(deep,accent,(unsigned)(z*80)));}
        for(int i=0;i<16;i++){
            int h=18+abs(xz_sin1024(i*37+(int)(g->clock*90)))*56/1024;
            xz_rect(c,36+i*47,212-h,17,h,xz_blend(0x070b1b,i%2?pink:cyan,26));
        }
    }else{
        for(int i=0;i<5;i++){
            int a=(int)(g->clock*12)+i*36;polygon(c,400,243,65+i*50,30+i*24,a,24,deep);
        }
        for(int i=0;i<18;i++){
            int a=i*20+(int)(g->clock*14),r=90+(i%4)*38;
            int x=400+xz_cos1024(a)*r/1024,y=243+xz_sin1024(a)*r*3/5/1024;
            xz_rect(c,x,y,2,2,accent);
        }
    }
    double wave=phase;
    polygon(c,400,243,28+(int)(wave*420),16+(int)(wave*192),0,32,
            xz_blend(deep,((int)floor(g->clock)&3)?cyan:pink,(unsigned)((1-wave)*60)));
    if(drop)polygon(c,400,243,70+(int)(bar*430),30+(int)(bar*205),(int)(bar*40),6,0x653956);
}
static void card(struct xz_canvas c,int x,int y,int w,int h,uint32_t col,const char *title,const char *sub,int selected){
    xz_round_rect(c,x,y,w,h,12,15,selected?0x152b42:0x10192d);
    xz_rect(c,x+13,y+14,3,h-28,col);
    if(selected){xz_rect(c,x+12,y+h-3,w-24,2,col);xz_rect(c,x+12,y+1,w-24,1,xz_blend(0x10192d,col,95));}
    text(c,x+28,y+12,title,2,col);if(sub)text(c,x+28,y+45,sub,1,dim);
}
static void clock_label(const struct xz_arcade *g,char *s,size_t n){
    if(g->clock_deck==4&&g->clock_source!=3){snprintf(s,n,"MIXER NOT READY / TAP H");return;}
    const char *source=g->clock_source==1?"USB":g->clock_source==2?"LINK":g->clock_source==3?"MIXER":g->clock_source==4?"TAP":g->clock_source==5?"DEMO":"FREE";
    if(g->clock_source==1||g->clock_source==2)snprintf(s,n,"%s %u   %.1f BPM   %s",source,g->clock_selected+1,(double)g->bpm,g->music_known&&!g->music_playing?"PAUSED":g->live?"SYNC":"HOLD");
    else snprintf(s,n,"%s   %.1f BPM   %s",source,(double)g->bpm,g->phase_aligned?"ALIGNED":g->clock_source==3?"TAP CUE":"SYNC >");
}
static void beat_lane(struct xz_canvas c,const struct xz_arcade *g){
    double phase=g->clock-floor(g->clock);int beat=(int)floor(g->clock);
    xz_rect(c,213,44,376,29,0x0b1122);xz_rect(c,223,59,356,1,0x2c3c59);
    for(int i=-2;i<=2;i++){
        int x=400+(int)((i-phase)*80);if(x<228||x>573)continue;
        uint32_t col=((beat+i)&3)==0?pink:cyan;
        xz_disc(c,x,59,4,xz_blend(0x172137,col,140));
    }
    uint32_t col=(beat&3)?cyan:pink;
    xz_round_rect(c,378,41,44,34,10,15,xz_blend(0x14223a,col,(unsigned)((1-phase)*120)));
    char s[8];snprintf(s,sizeof(s),"%d",(beat&3)+1);center(c,400,47,s,2,ink);
    text(c,429,51,"CUE",1,g->note_flash>g->clock?ink:dim);
    for(unsigned i=0;i<16;i++)xz_rect(c,272+(int)i*16,76,12,3,i<g->groove?pink:0x27304b);
}
static void charge(struct xz_canvas c,int x,int y,const struct xz_arcade_player *p,int deck){
    for(unsigned i=0;i<9;i++)xz_rect(c,x+(int)i*9,y,6,4,i<p->energy?color(deck):0x293247);
}
int xz_arcade_render(const struct xz_arcade *g,uint16_t *pixels,size_t count,size_t stride){
    if(!g||!pixels||stride<800||stride>count/480)return 0;
    struct xz_canvas c={pixels,stride,800,480,0};char s[128];backdrop(c,g);
    text(c,24,13,"BEAT / ARCADE",2,ink);
    xz_round_rect(c,232,8,337,30,8,15,0x121d32);clock_label(g,s,sizeof(s));center(c,400,16,s,1,g->live?cyan:dim);
    xz_round_rect(c,582,8,84,35,8,15,0x17243a);center(c,624,18,g->quiet?"CALM":"NEON",1,dim);
    xz_round_rect(c,680,8,108,35,8,15,0x24334e);center(c,734,18,"DJ VIEW",1,ink);
    if(g->phase==XZ_ARCADE_MENU){
        beat_lane(c,g);text(c,40,84,"YOUR MUSIC. YOUR MOVES.",2,ink);
        const char *sub[4]={"JOG TO CATCH / CUE TO THE BEAT","TWO DECKS / ONE COURT / FIRST TO 7","FIVE LIVES / FIND YOUR GROOVE","SHARED BREAKOUT / BUILD A DROP"};
        for(int i=0;i<4;i++)card(c,40+(i%2)*370,118+(i/2)*90,350,76,i%2?amber:cyan,modes[i],sub[i],(int)g->mode==i);
        text(c,40,299,"PLAY + TRACK SKIP KEEP CONTROLLING YOUR MUSIC",1,dim);
        card(c,40,322,720,56,cyan,"BEAT SOURCE / SETUP",NULL,0);clock_label(g,s,sizeof(s));text(c,334,343,s,1,dim);
        snprintf(s,sizeof(s),"JOG: %s",jogs[g->sensitivity%3]);card(c,40,392,350,48,cyan,s,NULL,0);
        snprintf(s,sizeof(s),"PACE: %s",levels[g->difficulty%3]);card(c,410,392,350,48,amber,s,NULL,0);
        text(c,40,454,"TURN JOG TO CHOOSE / CUE TO START / OR TOUCH A GAME",1,dim);return 1;
    }
    if(g->phase==XZ_ARCADE_CLOCK){
        text(c,40,72,"ONE GROOVE, ANY SOURCE",2,ink);
        text(c,40,110,"USB grid / Pro DJ Link / computer or external audio through the mixer",1,dim);
        card(c,40,148,228,58,cyan,"AUTO",NULL,g->clock_deck==2);
        card(c,280,148,228,58,cyan,"USB / LINK 1",NULL,g->clock_deck==0);
        card(c,520,148,240,58,amber,"USB / LINK 2",NULL,g->clock_deck==1);
        card(c,40,216,350,52,pink,"MIXER / ANY AUDIO",NULL,g->clock_deck==4);
        card(c,410,216,350,52,cyan,"TAP TEMPO",NULL,g->clock_deck==3);
        card(c,40,280,720,66,cyan,"TAP HERE ON THE BEAT",g->clock_source==3?"MIXER TEMPO: ONE TAP ALIGNS THE PULSE / FX CHANNEL FOLLOWS YOUR MIX":"TAP FOUR BEATS FOR ANY TRACK / PAD H ALSO WORKS",0);
        for(int i=0;i<4;i++){int active=((int)floor(g->clock)&3)==i;xz_disc(c,310+i*60,361,6,active?cyan:0x24334e);}
        card(c,40,382,720,50,cyan,"FOLLOW THE PULSE / DONE",NULL,0);
        text(c,40,449,"For computer or external audio: choose MIXER, set FX channel to MASTER, tap to align.",1,dim);return 1;
    }
    if(g->phase==XZ_ARCADE_MUSIC){
        text(c,74,92,"YOUR ROUND IS SAVED",2,ink);
        text(c,74,130,"PLAY and track skip control music. CUE gets you back into the game.",1,dim);
        card(c,40,188,350,92,cyan,g->player[0].ready?"PLAYER 1 READY":"PLAYER 1: CUE","OR TOUCH HERE TO GET READY",g->player[0].ready);
        card(c,410,188,350,92,amber,g->mode==XZ_ARCADE_PONG_DUEL||g->mode==XZ_ARCADE_BREAK_COOP?(g->player[1].ready?"PLAYER 2 READY":"PLAYER 2: CUE"):"SOLO ROUND","FOUR BEATS BEFORE THE BALL MOVES",g->player[1].ready);
        card(c,40,316,720,66,dim,"CHOOSE ANOTHER GAME",NULL,0);
        text(c,40,418,"DJ VIEW returns every control to the music / no score lost",1,dim);return 1;
    }
    if(g->phase==XZ_ARCADE_RESULT){
        if(g->winner>=0)snprintf(s,sizeof(s),"PLAYER %d TAKES THE SET",g->winner+1);else snprintf(s,sizeof(s),"NICE SET / %u POINTS",g->team_score);
        center(c,400,105,s,2,g->winner>=0?color(g->winner):ink);
        snprintf(s,sizeof(s),"BEST RALLY %u / PERFECTS %u / DROPS %u",g->best,g->player[0].perfects+g->player[1].perfects,g->drops);center(c,400,148,s,1,dim);
        card(c,40,190,720,90,cyan,"ONE MORE? / CUE TO REMATCH","YOUR MUSIC KEEPS PLAYING",1);
        card(c,40,316,720,66,dim,"CHOOSE ANOTHER GAME",NULL,0);return 1;
    }
    int is_break=g->mode>=XZ_ARCADE_BREAK_SOLO,is_two=g->mode==XZ_ARCADE_PONG_DUEL||g->mode==XZ_ARCADE_BREAK_COOP;
    beat_lane(c,g);
    snprintf(s,sizeof(s),is_break?"P1  %ux":"P1  %u",is_break?g->player[0].combo:g->player[0].score);text(c,24,47,s,2,cyan);charge(c,115,57,&g->player[0],0);
    snprintf(s,sizeof(s),g->mode==XZ_ARCADE_PONG_SOLO?"BOT  %u":is_break?"P2  %ux":"P2  %u",is_break?g->player[1].combo:g->player[1].score);text(c,617,47,s,2,amber);charge(c,694,57,&g->player[1],1);
    xz_border(c,23,80,754,335,0x2e3d58);
    xz_rect(c,24,81,116,2,cyan);xz_rect(c,660,81,115,2,amber);
    if(is_break){
        for(int i=0;i<48;i++)if(g->bricks[i]){
            int x=88+i%8*78,y=104+i/8*29;uint32_t col=i/8%3==0?pink:i/8%3==1?amber:cyan;
            unsigned pulse=(unsigned)(20*(1-(g->clock-floor(g->clock))));
            xz_round_rect(c,x,y,69,21,4,15,xz_blend(0x162035,col,115+pulse));xz_rect(c,x+4,y+2,61,2,col);
            if(g->bricks[i]>1)xz_rect(c,x+29,y+11,11,3,ink);
        }
    }else for(int y=90;y<405;y+=22)xz_rect(c,398,y,2,7,0x314059);
    for(int p=0;p<(is_break&&!is_two?1:2);p++){
        int width=(int)xz_arcade_paddle_size(g,p),pos=(int)g->player[p].position;
        uint32_t glow=xz_blend(0x0b1122,color(p),55);
        if(is_break){
            xz_round_rect(c,pos-width/2-7,392,width+14,18,8,15,glow);xz_round_rect(c,pos-width/2,396,width,9,4,15,color(p));xz_rect(c,pos-width/2+9,397,width-18,2,ink);
            if(g->player[p].shield)xz_rect(c,p?400:30,413,p?370:is_two?370:740,2,color(p));
        }else {
            xz_round_rect(c,p?739:44,pos-width/2-7,18,width+14,8,15,glow);xz_round_rect(c,p?744:49,pos-width/2,8,width,4,15,color(p));xz_rect(c,p?745:51,pos-width/2+10,2,width-20,ink);
            if(g->player[p].shield)xz_rect(c,p?767:30,86,2,320,color(p));
        }
    }
    if(g->phase==XZ_ARCADE_PLAY){
        uint32_t ball=g->drop_visual_until>g->beat?pink:g->last_player>=0?color(g->last_player):ink;
        if(!g->quiet&&g->impact_until>g->beat){
            float age=(float)(.55-(g->impact_until-g->beat));uint32_t col=xz_blend(0x10192b,ball,(unsigned)(190*(1-age/.55f)));
            polygon(c,(int)g->impact_x,(int)g->impact_y,12+(int)(age*74),12+(int)(age*74),(int)(age*30),16,col);
            for(int i=0;i<12;i++){int distance=12+(int)(age*(70+(i%3)*24));int x=(int)g->impact_x+xz_cos1024(i*30)*distance/1024,y=(int)g->impact_y+xz_sin1024(i*30)*distance/1024;if(y>82&&y<414&&x>24&&x<777)xz_rect(c,x,y,3,3,col);}
        }
        if(!g->quiet)for(unsigned j=1;j<g->trail_count;j++){
            unsigned a=(g->trail_head+12-j)%12,b=(g->trail_head+11-j)%12;
            uint32_t col=xz_blend(0x10192b,ball,150-j*10);
            line(c,(int)g->trail_x[a],(int)g->trail_y[a],(int)g->trail_x[b],(int)g->trail_y[b],col);
            line(c,(int)g->trail_x[a],(int)g->trail_y[a]+1,(int)g->trail_x[b],(int)g->trail_y[b]+1,col);
        }
        if(g->flight.contact==XZ_ARCADE_PADDLE){
            float remaining=(float)(g->flight.end-g->beat);int radius=12+(int)(fminf(remaining,2.f)*13);
            uint32_t col=remaining<.25?ink:color(is_break?(g->flight.x1>400&&is_two):g->flight.index);
            polygon(c,(int)g->flight.x1,(int)g->flight.y1,radius,radius,45,24,xz_blend(0x18233d,col,180));
            xz_disc(c,(int)g->flight.x1,(int)g->flight.y1,2,ink);
        }
        if(g->feedback&&g->feedback_until>g->beat){
            int w=xz_text_width(c,g->feedback,2,90);xz_round_rect(c,400-w/2-18,294,w+36,32,10,15,0x111b31);
            center(c,400,301,g->feedback,2,g->feedback_player<0?pink:color(g->feedback_player));
        }
        if(g->drop_pending){snprintf(s,sizeof(s),"DROP IN %u",(unsigned)ceil(g->drop_at-g->clock));center(c,400,270,s,2,pink);}
        xz_disc(c,(int)g->x,(int)g->y,13,xz_blend(0x10192b,ball,65));xz_disc(c,(int)g->x,(int)g->y,10,ball);xz_disc(c,(int)g->x-1,(int)g->y-1,6,ink);
    }else if(g->phase==XZ_ARCADE_WAIT_TRACK){
        xz_round_rect(c,169,256,462,104,14,15,0x101a30);center(c,400,273,"WAITING FOR MUSIC / ROUND SAVED",2,ink);center(c,400,310,"PRESS PLAY ON THE DECK",2,cyan);
        center(c,400,339,"Track restart or skip keeps your score",1,dim);
    }else {
        int left=(int)ceil(g->count_until-g->clock);if(left<1)left=1;if(left>4)left=4;
        xz_round_rect(c,258,262,284,106,14,15,0x101a30);snprintf(s,sizeof(s),"%d",left);xz_pixel_text(c,382,275,s,6,2,cyan);center(c,400,335,"CUE TO THE BEAT",2,ink);
    }
    if(is_break)snprintf(s,sizeof(s),"%s   %u LIVES   %u PTS   %s",modes[g->mode],g->lives,g->team_score,levels[g->difficulty%3]);
    else snprintf(s,sizeof(s),"%s   RALLY %u   %s",modes[g->mode],g->rally,levels[g->difficulty%3]);
    text(c,24,419,s,1,dim);text(c,503,419,"PLAY + SKIP = YOUR MUSIC",1,dim);
    const char *labels[8]={"A / CUE","B WIDE 3","C SAVE 4","D DROP 6","E AIM -","F AIM +","G SCENE","H SYNC"};
    for(int i=0;i<8;i++){uint32_t col=i==0?cyan:i==3?pink:i==7?amber:dim;xz_round_rect(c,12+i*98,438,90,32,7,15,0x15213a);center(c,57+i*98,447,labels[i],1,col);}
    return 1;
}
