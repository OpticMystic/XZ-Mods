/* Native C game preview host. No controller/audio hooks; generated local fixture. */
#include "wave_rider.h"
#include "rhythm_lab.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static struct xz_rider wr;
static struct xz_rhythm_lab lab;
static struct xz_wave_rider_snapshot wave;
static uint8_t samples[60000*3];
static uint16_t pixels[800*480];
static float stats[26];
static int which,ready,duo;
static uint32_t now;
static double time_now;
__attribute__((visibility("default"))) uint16_t *preview_pixels(void){return pixels;}
__attribute__((visibility("default"))) float *preview_stats(void){return stats;}
__attribute__((visibility("default"))) void preview_start(int game,int players){
 which=game?1:0;duo=players;ready=1;time_now=0;now=1000;
 for(unsigned i=0;i<60000;i++){samples[i*3]=(uint8_t)(i%75<12?230-i%75*17:6);samples[i*3+1]=(uint8_t)(105+85*sinf(i*.006f));samples[i*3+2]=(uint8_t)(100+80*sinf(i*.014f));}
 memset(&wave,0,sizeof(wave));wave.valid=wave.playing=1;wave.kind=1;wave.normalization=500;wave.generation=1;wave.track_hash=49;wave.pulse_period=75;wave.pulse_confidence=1;wave.observed_ms=now;strcpy(wave.title,"PREVIEW GROOVE / 120 BPM");xz_wave_rider_window(&wave,samples,60000,300);
 if(!which){xz_rider_init(&wr,0,now);wr.mode=players?XZ_RIDER_DUEL:XZ_RIDER_SOLO;xz_rider_feed(&wr,&wave,now);wr.setup_step=3;}
 else{xz_lab_init(&lab,now);lab.mode=LAB_GLYPH;lab.duo=players;lab.study=0;xz_lab_feed(&lab,&wave,now);xz_lab_begin(&lab,now);}
}
__attribute__((visibility("default"))) void preview_tick(double seconds,int playing){
 if(!ready)preview_start(0,0);if(seconds<time_now)seconds=time_now;time_now=seconds;now=1000+(uint32_t)(seconds*1000);
 double at=300+seconds*150;if(at>=60000)at=59999;xz_wave_rider_window(&wave,samples,60000,at);wave.observed_ms=now;wave.playing=playing;
 if(!which){wave.deck=wr.deck;xz_rider_feed(&wr,&wave,now);xz_rider_step(&wr,now);xz_rider_render(&wr,pixels,800*480,800);stats[0]=(float)wr.phase;stats[1]=(float)wr.player[0].score;stats[2]=(float)wr.player[0].streak;stats[3]=wr.player[0].pitch;stats[4]=wr.player[0].vx;stats[5]=wr.player[0].vy;stats[6]=wr.player[0].altitude;stats[7]=wr.player[0].lift;stats[8]=wr.player[0].downforce;stats[9]=(float)wr.player[0].grounded;stats[10]=(float)wr.leave;}
 else{wave.deck=(int)lab.deck;xz_lab_feed(&lab,&wave,now);xz_lab_step(&lab,now);xz_lab_render(&lab,pixels,800*480,800);stats[0]=(float)lab.phase;stats[1]=(float)lab.player[0].score;stats[2]=(float)lab.player[0].streak;stats[3]=lab.player[0].angle;stats[4]=120;stats[5]=lab.glyph[0].velocity;stats[6]=lab.glyph[0].height;stats[7]=0;stats[8]=0;stats[9]=lab.glyph[0].height<1;stats[10]=(float)lab.leave;}
 if(!which){stats[14]=(float)wr.player[0].air_tricks;stats[15]=(float)wr.player[0].air_crests;stats[16]=(float)wr.player[0].air_points;stats[17]=(float)wr.player[0].combo_points;stats[18]=(float)wr.player[0].combo_failed;stats[19]=(float)wr.player[0].catches;stats[20]=(float)wr.player[0].best_combo;stats[21]=(float)wr.player[0].tricks_landed;stats[22]=(float)wr.player[0].power;stats[23]=(float)wr.player[0].power_pickups;}
 stats[11]=(float)seconds;stats[12]=(float)which;stats[13]=(float)duo;
 stats[24]=which?0:(float)wr.difficulty;stats[25]=which?0:(float)wr.player[0].landing_points;
}
__attribute__((visibility("default"))) void preview_jog(int player,float pulses){if(player<0||player>1)return;if(!which)xz_rider_jog(&wr,(unsigned)player,pulses,now);else xz_lab_jog(&lab,player,pulses,now);}
__attribute__((visibility("default"))) void preview_action(int player,int down){if(player<0||player>1)return;if(!which)xz_rider_button(&wr,(unsigned)player,XZ_RIDER_PAD_A,down,now);else xz_lab_button(&lab,(unsigned)player,LAB_PAD_A,down,now);}
__attribute__((visibility("default"))) void preview_pad(int player,int pad,int down){if(player<0||player>1||pad<0||pad>7)return;if(!which)xz_rider_button(&wr,(unsigned)player,(enum xz_rider_key)(XZ_RIDER_PAD_A+pad),down,now);else xz_lab_button(&lab,(unsigned)player,(enum xz_lab_key)(LAB_PAD_A+pad),down,now);}
__attribute__((visibility("default"))) void preview_touch(int x,int y,int down){if(!which)xz_rider_touch(&wr,x,y,down,now);else xz_lab_touch(&lab,x,y,down,now);}
__attribute__((visibility("default"))) void preview_retry(void){if(!which)xz_rider_begin(&wr,now);else xz_lab_begin(&lab,now);}
