/* Offline fixtures only. No device, USB or native calls. */
#include "classic_mixer.h"
#include "extra_detail.h"
#include "extra_lane.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint16_t pixels[800*480],overview[2][300*34];
static struct xz_extra_column wave[4096];
int main(int argc,char **argv){
 if(argc!=6)return 2;
 int layout=!strcmp(argv[5],"four")?XZ_LAYOUT_FOUR:XZ_LAYOUT_MIXER;
 int order=layout==XZ_LAYOUT_MIXER?XZ_FOUR_MIXER:XZ_FOUR_STACKED;
 FILE *f=fopen(argv[1],"rb");if(!f)return 3;size_t n=fread(pixels,2,800*480,f);fclose(f);if(n!=800*480)return 4;
 /* Retained stock footer's overview rectangles only, never its data readouts. */
 for(int d=0;d<2;d++)for(int y=0;y<34;y++)for(int x=0;x<300;x++)overview[d][y*300+x]=pixels[(423+y)*800+10+d*400+x];
 struct xz_shell_deck native[2];struct xz_extra_decks extra;memset(native,0,sizeof native);memset(&extra,0,sizeof extra);extra.route_up=1;
 const char *titles[4]={"Good Times Roll","DJ Turn It Up","Night Drive Extended Mix","Midnight Motion"};
 for(int i=0;i<4096;i++){wave[i].low=(uint8_t)(50+(i*17)%170);wave[i].mid=(uint8_t)(20+(i*23)%120);wave[i].high=(uint8_t)(10+(i*7)%90);}
 for(int i=0;i<2;i++){
  struct xz_shell_deck *d=&native[i];d->known=UINT64_MAX;d->loaded=1;d->title=titles[i];d->key=i?"4A":"2A";d->source=XZ_SHELL_SRC_USB2;d->time_mode=XZ_SHELL_TIME_REMAIN;d->elapsed_ms=24414+i*30000;d->total_ms=259900;d->bpm_x100=11750+i*3130;d->tempo_x100=i?-1450:1750;d->tempo_range=100;d->master=i==0;d->auto_cue=1;d->quantize_on=1;d->master_tempo=1;
  struct xz_extra_deck *e=&extra.deck[i];e->known=UINT32_MAX;e->loaded=1;snprintf(e->title,sizeof e->title,"%s",titles[i+2]);strcpy(e->key,"8A");e->source=1;e->remain=1;e->auto_cue=1;e->quantize=1;e->master_tempo=1;e->total_ms=300000;e->elapsed_ms=42000+i*25000;e->bpm_x100=13120+i*4070;e->tempo_x100=250;e->tempo_range=10;e->wave=(struct xz_extra_wave){wave,4096,300000,650};e->pad_mode=XZ_EXTRA_PAD_HOT_CUE;e->active_pad=-1;e->sync.requested=1;e->sync.locked=1;
 }
 /* The retained DECK 2 counter is at END, including text over its overview. */
 native[1].elapsed_ms=native[1].total_ms;
 int scenario=atoi(argv[3]);
 if(scenario==1){native[0].known=0;extra.deck[0].known=0;native[1].loaded=0;extra.deck[1].loaded=0;}
 if(scenario==2){memset(extra.deck[0].title,'W',95);extra.deck[0].title[95]=0;native[0].title="A very long title that must never cross its channel boundary";extra.deck[0].master=1;extra.deck[0].master_scope=12;extra.deck[1].reverse=1;extra.deck[1].slip=1;extra.deck[1].pad_mode=XZ_EXTRA_PAD_CALL;}
 /* Reconstruct native lanes from retained capture, then use parent's real frame composer. */
 static uint16_t surface[XZ_WAVE_WIDTH*XZ_WAVE_HEIGHT];
 f=fopen(argv[4],"rb");if(!f)return 8;n=fread(surface,2,XZ_WAVE_WIDTH*XZ_WAVE_HEIGHT,f);fclose(f);if(n!=XZ_WAVE_WIDTH*XZ_WAVE_HEIGHT)return 9;
 xz_four_layout_pixels(surface,XZ_WAVE_WIDTH*XZ_WAVE_HEIGHT,XZ_WAVE_WIDTH,order);
 struct xz_four_frame frame={&extra,order,1000,XZ_EXTRA_LANE_COLUMN_US,1,-1,0};
 if(!xz_four_frame_draw(surface,XZ_WAVE_WIDTH*XZ_WAVE_HEIGHT,&frame,NULL))return 10;
 for(int y=0;y<XZ_WAVE_HEIGHT;y++)for(int x=0;x<XZ_WAVE_WIDTH;x++){
  int sx=131+x,sy=18+y;if(sy<24&&sx>=282&&sx<672)continue;
  pixels[sy*800+sx]=surface[y*XZ_WAVE_WIDTH+x];
 }
 struct xz_classic_mixer_view v={native,&extra,1000,0,layout,{overview[0],overview[1]}};
 if(!xz_classic_mixer_draw(pixels,800,800*480,&v))return 5;
 f=fopen(argv[2],"wb");if(!f)return 6;n=fwrite(pixels,2,800*480,f);fclose(f);return n==800*480?0:7;
}
