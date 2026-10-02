/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error Arcade acceptance requires active assertions
#endif
#include "arcade.h"
#include "input.h"
#include "clock.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint32_t now;
static void advance(struct xz_arcade *g,unsigned ms){now+=ms;xz_arcade_step(g,now);}
static void launch(struct xz_arcade *g,enum xz_arcade_mode mode){now=1000;xz_arcade_init(g,now);xz_arcade_start(g,mode,now);for(int i=0;i<250;i++)advance(g,10);assert(g->phase==XZ_ARCADE_PLAY);}
static void follow(struct xz_arcade *g){
    if(g->flight.contact!=XZ_ARCADE_PADDLE)return;
    if(g->mode>=XZ_ARCADE_BREAK_SOLO){float x=g->flight.x1;int deck=g->mode==XZ_ARCADE_BREAK_COOP&&x>400?1:0;xz_arcade_move(g,deck,x-g->player[deck].position,now);}
    else {int deck=g->flight.index;if(g->mode==XZ_ARCADE_PONG_SOLO&&deck==1)return;xz_arcade_move(g,deck,g->flight.y1-g->player[deck].position,now);}
}
static void screenshot(struct xz_arcade *g,const char *prefix,const char *name){
    static uint16_t pixels[800*480+4];for(int i=0;i<4;i++)pixels[800*480+i]=0xabcd;
    assert(xz_arcade_render(g,pixels,800*480,800));
    assert(!xz_arcade_render(g,pixels,800*479,800));
    for(int i=0;i<4;i++)assert(pixels[800*480+i]==0xabcd);
    if(!prefix)return;
    char file[1024];snprintf(file,sizeof(file),"%s/%s.ppm",prefix,name);FILE *f=fopen(file,"wb");assert(f);
    fprintf(f,"P6\n800 480\n255\n");
    for(int i=0;i<800*480;i++){uint16_t c=pixels[i];unsigned char p[3]={(unsigned char)(((c>>11)&31)*255/31),(unsigned char)(((c>>5)&63)*255/63),(unsigned char)((c&31)*255/31)};fwrite(p,1,3,f);}fclose(f);
}
static void packet(unsigned char p[96]){memset(p,0,96);memcpy(p,"Qspt1WmJOL",10);p[10]=0x28;p[31]=1;p[33]=1;p[35]=0x3c;p[85]=0x10;p[90]=0x2e;p[91]=0xe0;p[92]=1;p[95]=1;}
int main(int argc,char **argv){
    const char *prefix=argc>1?argv[1]:NULL;struct xz_arcade g;
    launch(&g,XZ_ARCADE_PONG_DUEL);
    unsigned previous=0;
    for(int i=0;i<45000;i++){
        follow(&g);advance(&g,8);
        assert(isfinite(g.x)&&isfinite(g.y)&&g.x>=22&&g.x<=778&&g.y>=68&&g.y<=415);
        assert(g.flight.end>g.flight.start);
        if(g.rally>previous){assert(fabs(g.flight.start*4-round(g.flight.start*4))<1e-4);previous=g.rally;}
    }
    assert(g.rally>100&&g.player[0].score==0&&g.player[1].score==0);screenshot(&g,prefix,"duel");
    printf("PASS six-minute duel: %u beat-grid returns, no goal or nonfinite position\n",g.rally);
    float x=g.x,y=g.y;double beat=g.beat;
    xz_arcade_key(&g,0,XZ_ARCADE_PAUSE_KEY,now);assert(g.phase==XZ_ARCADE_MUSIC);
    for(int i=0;i<500;i++)advance(&g,20);
    assert(g.x==x&&g.y==y&&g.beat==beat);
    xz_arcade_key(&g,0,XZ_ARCADE_CUE_KEY,now);assert(g.phase==XZ_ARCADE_MUSIC);
    xz_arcade_key(&g,1,XZ_ARCADE_CUE_KEY,now);assert(g.phase==XZ_ARCADE_COUNT_IN);
    for(int i=0;i<199;i++)advance(&g,10);
    assert(g.x==x&&g.y==y);while(g.phase==XZ_ARCADE_COUNT_IN)advance(&g,1);assert(g.phase==XZ_ARCADE_PLAY);assert(g.x==x&&g.y==y);
    printf("PASS pause freezes rally and powers; both players confirm; four beats resume\n");
    launch(&g,XZ_ARCADE_BREAK_COOP);
    for(int i=0;i<45000;i++){
        follow(&g);advance(&g,8);
        assert(isfinite(g.x)&&isfinite(g.y)&&g.x>=22&&g.x<=778&&g.y>=68&&g.y<=415);
    }
    assert(g.team_score>500&&g.lives==5);screenshot(&g,prefix,"coop");
    printf("PASS six-minute coop: %u points, stage %u, five shared lives retained\n",g.team_score,g.stage);
    launch(&g,XZ_ARCADE_BREAK_SOLO);for(int i=0;i<2400;i++){follow(&g);advance(&g,8);}screenshot(&g,prefix,"breaks");assert(g.lives==5&&g.team_score>0);
    launch(&g,XZ_ARCADE_PONG_SOLO);for(int i=0;i<3600;i++){follow(&g);advance(&g,8);}screenshot(&g,prefix,"solo");
    launch(&g,XZ_ARCADE_PONG_DUEL);x=g.x;y=g.y;
    xz_arcade_sync(&g,160,3,now);assert(g.x==x&&g.y==y&&g.bpm==160&&g.live);
    xz_arcade_sync(&g,NAN,1,now);assert(g.bpm==160);
    xz_arcade_sync(&g,120,0,now);assert(g.bpm==160);
    xz_arcade_sync(&g,120,1,now-30);assert(g.phase==XZ_ARCADE_PLAY);
    advance(&g,501);assert(g.phase==XZ_ARCADE_MUSIC);
    printf("PASS tempo switch preserves position; invalid clocks rejected; stalled display pauses\n");
    unsigned char event[24]={0};struct xz_arcade_input in={{0}};uint16_t key;
    launch(&g,XZ_ARCADE_PONG_DUEL);
    for(key=0x4101;key<=0x4113;key++)if(key!=0x4102){
        memcpy(event+8,&key,2);
        for(unsigned op=0;op<4;op++){event[11]=(unsigned char)op;assert(!xz_arcade_input_event(&in,&g,1,0,event,now));}
    }
    key=0x4102;memcpy(event+8,&key,2);event[11]=0;
    assert(xz_arcade_input_event(&in,&g,1,0,event,now));assert(g.phase==XZ_ARCADE_PLAY);
    xz_arcade_key(&g,0,XZ_ARCADE_PAUSE_KEY,now);
    event[11]=2;assert(xz_arcade_input_event(&in,&g,0,0,event,now));assert(!xz_arcade_input_event(&in,&g,0,0,event,now));
    launch(&g,XZ_ARCADE_PONG_DUEL);key=0x4305;memcpy(event+8,&key,2);event[11]=4;int32_t delta=10;memcpy(event+12,&delta,4);float before=g.player[0].position;
    assert(xz_arcade_input_event(&in,&g,1,0,event,now));assert(fabsf(g.player[0].position-before-5.5f)<.01f);
    delta=-10;memcpy(event+12,&delta,4);xz_arcade_input_event(&in,&g,1,0,event,now);assert(fabsf(g.player[0].position-before)<.01f);
    assert(!xz_arcade_input_event(&in,&g,0,0,event,now));
    key=0x4306;memcpy(event+8,&key,2);event[11]=0;assert(xz_arcade_input_event(&in,&g,1,0,event,now));
    event[11]=2;assert(xz_arcade_input_event(&in,&g,0,0,event,now));assert(!xz_arcade_input_event(&in,&g,0,0,event,now));
    printf("PASS music PLAY and skip edges pass through; CUE fires; jog gain 5.5px/10 pulses; platter touch/release captured\n");
    unsigned char p[96];packet(p);struct xz_arcade_beat sample;
    assert(xz_arcade_decode_beat(p,96,&sample)&&sample.bpm==120&&sample.deck==0&&sample.bar==1);
    assert(!xz_arcade_decode_beat(p,95,&sample));p[95]=2;assert(!xz_arcade_decode_beat(p,96,&sample));packet(p);p[92]=0;assert(!xz_arcade_decode_beat(p,96,&sample));
    printf("PASS qualified PRO DJ LINK beat layout and invalid packet rejection\n");
    launch(&g,XZ_ARCADE_PONG_DUEL);g.player[0].energy=9;xz_arcade_key(&g,0,XZ_ARCADE_PAD_B,now);assert(g.player[0].energy==6&&xz_arcade_paddle_size(&g,0)==172);
    xz_arcade_key(&g,0,XZ_ARCADE_PAD_C,now);assert(g.player[0].energy==2&&g.player[0].shield);
    xz_arcade_key(&g,0,XZ_ARCADE_PAD_D,now);assert(g.player[0].energy==2&&g.player[0].drop_until==0);
    g.player[0].energy=9;xz_arcade_key(&g,0,XZ_ARCADE_PAD_D,now);assert(g.player[0].energy==3&&g.drop_pending&&g.drop_at>g.clock);
    screenshot(&g,prefix,"drop");
    xz_arcade_init(&g,now);screenshot(&g,prefix,"menu");g.phase=XZ_ARCADE_CLOCK;screenshot(&g,prefix,"clock");
    launch(&g,XZ_ARCADE_PONG_DUEL);xz_arcade_key(&g,0,XZ_ARCADE_PAUSE_KEY,now);screenshot(&g,prefix,"paused");g.phase=XZ_ARCADE_RESULT;g.winner=1;screenshot(&g,prefix,"result");
    launch(&g,XZ_ARCADE_BREAK_SOLO);
    int last_note=-1;
    for(int i=0;i<2500;i++){
        follow(&g);advance(&g,8);
        int note=(int)floor(g.clock+.00001);
        if(g.phase==XZ_ARCADE_PLAY&&note!=last_note){
            last_note=note;xz_arcade_key(&g,0,XZ_ARCADE_CUE_KEY,now);unsigned points=g.team_score;
            xz_arcade_key(&g,0,XZ_ARCADE_CUE_KEY,now);assert(g.team_score==points);
        }
    }
    assert(g.player[0].notes>=24&&g.player[0].best_combo>=8&&g.drops>=1);
    screenshot(&g,prefix,"groove");
    for(unsigned style=0;style<3;style++){g.style=style;char name[24];snprintf(name,sizeof(name),"scene-%u",style);screenshot(&g,prefix,name);}
    printf("PASS CUE groove chain: %u notes, %u best chain, %u drops; same-beat mashing earns nothing\n",g.player[0].notes,g.player[0].best_combo,g.drops);
    launch(&g,XZ_ARCADE_PONG_DUEL);x=g.x;y=g.y;
    xz_arcade_music(&g,128,1.2f,1,100,0,now);assert(g.phase==XZ_ARCADE_COUNT_IN&&g.live&&g.clock_source==1);
    xz_arcade_music(&g,128,1.2f,0,100,0,now);assert(g.phase==XZ_ARCADE_WAIT_TRACK);
    for(int i=0;i<100;i++)advance(&g,20);assert(g.x==x&&g.y==y);
    xz_arcade_music(&g,128,1.2f,1,100,0,now);assert(g.phase==XZ_ARCADE_COUNT_IN);
    while(g.phase==XZ_ARCADE_COUNT_IN)advance(&g,8);
    x=g.x;y=g.y;xz_arcade_music(&g,126,.2f,1,200,1,now);assert(g.phase==XZ_ARCADE_COUNT_IN&&g.track_id==200&&g.x==x&&g.y==y);
    xz_arcade_music_missing(&g);assert(g.phase==XZ_ARCADE_WAIT_TRACK);
    screenshot(&g,prefix,"track-paused");
    xz_arcade_tempo(&g,126,now);assert(g.clock_source==3&&!g.phase_aligned);
    xz_arcade_key(&g,0,XZ_ARCADE_PAD_H,now);assert(g.phase_aligned&&g.bpm==126&&g.phase==XZ_ARCADE_COUNT_IN);
    printf("PASS track pause/restart/skip preserve ball and score; mixer tempo takes a phase-align tap\n");
    puts("PASS powers spend earned charge; native RGB565 rendering bounds and all screens");
    static uint16_t bench[800*480];launch(&g,XZ_ARCADE_BREAK_COOP);
    clock_t begin=clock();for(int i=0;i<120;i++)xz_arcade_render(&g,bench,800*480,800);
    double ms=(double)(clock()-begin)*1000/CLOCKS_PER_SEC;
    printf("RENDER_BENCH frames=120 cpu_ms=%.2f mean_ms=%.3f\n",ms,ms/120);
    return 0;
}
