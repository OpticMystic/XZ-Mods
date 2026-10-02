/* SPDX-License-Identifier: MIT */
#include "arcade.h"
#include <stdio.h>
#include <math.h>
static struct xz_arcade game;
static uint16_t pixels[800*480];
static unsigned demo_enabled,demo_playing,demo_last,demo_track=1;
static double demo_beat;
static float demo_bpm=124;
__declspec(dllexport) void preview_init(unsigned now){xz_arcade_init(&game,now);demo_last=now;}
__declspec(dllexport) void preview_step(unsigned now){
    unsigned elapsed=now-demo_last;demo_last=now;
    if(demo_enabled){
        if(demo_playing&&elapsed<500)demo_beat+=elapsed*demo_bpm/60000.;
        xz_arcade_music(&game,demo_bpm,(float)fmod(demo_beat,4.),demo_playing,demo_track,0,now);
        if(game.clock_source==1)game.clock_source=5;
    }
    xz_arcade_step(&game,now);
}
__declspec(dllexport) void preview_music(int command,float value,unsigned now){
    preview_step(now);game.clock_deck=2;
    if(command==0){demo_playing=demo_enabled?!demo_playing:1;demo_enabled=1;}
    else if(command==1){demo_beat=0;demo_track++;demo_enabled=demo_playing=1;}
    else if(command==2&&value>=40&&value<=240)demo_bpm=value;
    else if(command==3&&value>=0&&value<3)game.style=(unsigned)value;
    preview_step(now);
}
__declspec(dllexport) void preview_key(int deck,int key,unsigned now){if(key>=0&&key<=XZ_ARCADE_PAUSE_KEY)xz_arcade_key(&game,deck,(enum xz_arcade_key)key,now);}
__declspec(dllexport) void preview_move(int deck,float delta,unsigned now){xz_arcade_move(&game,deck,delta*xz_arcade_jog_gain(&game),now);}
__declspec(dllexport) void preview_touch(int x,int y,int down,unsigned now){xz_arcade_touch(&game,x,y,down,now);}
__declspec(dllexport) const uint16_t *preview_frame(void){xz_arcade_render(&game,pixels,800*480,800);return pixels;}
__declspec(dllexport) const char *preview_status(void){
    static char s[768];snprintf(s,sizeof(s),"{\"phase\":%d,\"mode\":%d,\"bpm\":%.2f,\"clock\":%.4f,\"ball\":[%.2f,%.2f],\"rally\":%u,\"lives\":%u,\"score\":[%u,%u],\"points\":%u,\"energy\":[%u,%u],\"ready\":[%u,%u],\"stage\":%u,\"combo\":[%u,%u],\"groove\":%u,\"drops\":%u,\"music\":%u}",
        game.phase,game.mode,(double)game.bpm,game.clock,(double)game.x,(double)game.y,game.rally,game.lives,game.player[0].score,game.player[1].score,game.team_score,game.player[0].energy,game.player[1].energy,game.player[0].ready,game.player[1].ready,game.stage,game.player[0].combo,game.player[1].combo,game.groove,game.drops,demo_enabled&&demo_playing);return s;
}
