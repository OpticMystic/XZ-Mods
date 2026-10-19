#ifndef XZ_SHELL_FIXTURES_H
#define XZ_SHELL_FIXTURES_H
/* Example decks for the host test and the previews. The waveform is
   generated to read like a club track: kicks on the grid, hats between,
   a breakdown without lows, then the drop again. Never device data. */
#include "shell.h"
#include <string.h>

#define FX_BPM_X100 12800
#define FX_BEAT_MS 46875                      /* 1/100 ms at 128 BPM */
#define FX_TOTAL_MS 372000
#define FX_LANE_COLUMNS (FX_TOTAL_MS*3/20)    /* 150 columns per second */
#define FX_OVERVIEW_COLUMNS 400
#define FX_GRID_BEATS (FX_TOTAL_MS*100/FX_BEAT_MS+1)

struct fx_track {
 struct xz_shell_column lane[FX_LANE_COLUMNS];
 struct xz_shell_column overview[FX_OVERVIEW_COLUMNS];
 struct xz_shell_column stems[FX_LANE_COLUMNS];   /* low = drums, mid = harmonics, high = vocal */
 struct xz_shell_beat grid[FX_GRID_BEATS];
 uint32_t grid_count;
};

static int sh_fx_max(int a,int b){return a>b?a:b;}
static uint32_t fx_noise(uint32_t *state) { *state=*state*1664525u+1013904223u;return *state>>8; }
/* Section energy by position in the track: intro, build, drop, break, build, drop, outro.
   Builds ramp within their eight bars; everything else holds. 0..255 each. */
static void fx_section(int32_t ms, int *lows, int *mids, int *highs) {
 int bar=(int)(ms/(FX_BEAT_MS*4/100)),section=(bar/8)%7,ramp=(bar%8)*32/8;
 switch(section){
 case 0:*lows=60;*mids=110;*highs=150;break;
 case 1:*lows=90+ramp*3;*mids=140+ramp*2;*highs=150+ramp*3;break;
 case 2:*lows=255;*mids=210;*highs=215;break;
 case 3:*lows=0;*mids=180;*highs=90+ramp*4;break;
 case 4:*lows=120+ramp*4;*mids=170+ramp*2;*highs=170+ramp*2;break;
 case 5:*lows=255;*mids=230;*highs=225;break;
 default:*lows=110;*mids=90;*highs=70;break;
 }
}
static int fx_decay(int frac, int length) { /* (1 - frac/length)^2 in 0..255, 0 past length */
 if(frac>=length)return 0;
 int r=255-frac*255/length;return r*r/255;
}
static struct xz_shell_column fx_column(int32_t ms, uint32_t seed) {
 int lows,mids,highs;fx_section(ms,&lows,&mids,&highs);
 int64_t beat_index=(int64_t)ms*100/FX_BEAT_MS;
 int beat=(int)(beat_index%4),bar=(int)(beat_index/4);
 int frac=(int)(((int64_t)ms*100%FX_BEAT_MS)*256/FX_BEAT_MS);  /* 0..255 into the beat */
 uint32_t state=(uint32_t)bar*2654435761u+seed;
 int bar_gain=200+(int)(fx_noise(&state)%56);                    /* per-bar variation, 0.78..1.0 */
 uint32_t fine=(uint32_t)ms*747796405u+seed;
 int shimmer=230+(int)(fx_noise(&fine)%50);
 int kick=fx_decay(frac,210),snare=(beat==1||beat==3)?fx_decay(frac,110):0;
 int eighth=frac>=128?frac-128:frac,hat=fx_decay(eighth,70);
 int open_hat=bar%4==3&&beat==3?fx_decay(frac,255):0;
 int bass_note=((int)(beat_index*2+(frac>=128))%3)?225:165;        /* a walking 8th-note bass line */
 int low=sh_fx_max(kick,frac<225?bass_note*200/255:0)*lows/255;
 int mid=(mids*80/255+snare*mids*120/255/255+kick*mids*45/255/255+bass_note*mids*30/255/255);
 int high=(highs*45/255+hat*highs*135/255/255+open_hat*highs*100/255/255+(frac<16?highs*50/255:0));
 low=low*bar_gain/256*shimmer/256;mid=mid*bar_gain/256*shimmer/256;high=high*bar_gain/256*shimmer/256;
 struct xz_shell_column q={(uint8_t)(low>255?255:low),(uint8_t)(mid>255?255:mid),(uint8_t)(high>255?255:high)};
 return q;
}
/* Stems from the same arrangement: drums follow the kick and hats, harmonics the bass and pads,
   and a vocal sings three bars in four through the break and the second drop. */
static struct xz_shell_column fx_stem_column(int32_t ms, struct xz_shell_column band) {
 int bar=(int)(ms/(FX_BEAT_MS*4/100)),section=(bar/8)%7;
 int drums=band.low*3/4+band.high/3,harmonics=band.mid*9/10;
 int vocal=0;
 if((section==3||section==5)&&bar%4!=3){
  int frac=(int)(((int64_t)ms*100%(FX_BEAT_MS*2))*256/(FX_BEAT_MS*2));    /* a two-beat phrase */
  vocal=frac<200?150+frac/4:60;
 }
 struct xz_shell_column q={(uint8_t)(drums>255?255:drums),(uint8_t)(harmonics>255?255:harmonics),(uint8_t)vocal};
 return q;
}
static void fx_track_build(struct fx_track *t, uint32_t seed) {
 for(int i=0;i<FX_LANE_COLUMNS;i++){
  int32_t ms=(int32_t)((int64_t)i*FX_TOTAL_MS/FX_LANE_COLUMNS);
  t->lane[i]=fx_column(ms,seed);t->stems[i]=fx_stem_column(ms,t->lane[i]);
 }
 for(int i=0;i<FX_OVERVIEW_COLUMNS;i++){
  int from=i*FX_LANE_COLUMNS/FX_OVERVIEW_COLUMNS,to=(i+1)*FX_LANE_COLUMNS/FX_OVERVIEW_COLUMNS;
  struct xz_shell_column q={0,0,0};
  for(int j=from;j<to;j++){
   if(t->lane[j].low>q.low)q.low=t->lane[j].low;
   if(t->lane[j].mid>q.mid)q.mid=t->lane[j].mid;
   if(t->lane[j].high>q.high)q.high=t->lane[j].high;
  }
  t->overview[i]=q;
 }
 t->grid_count=0;
 for(int64_t beat=0;beat*FX_BEAT_MS/100<FX_TOTAL_MS&&t->grid_count<FX_GRID_BEATS;beat++){
  t->grid[t->grid_count].ms=(int32_t)(beat*FX_BEAT_MS/100);
  t->grid[t->grid_count].beat_in_bar=(uint8_t)(beat%4+1);
  t->grid_count++;
 }
}
static int32_t fx_beat_ms(int beat) { return (int32_t)((int64_t)beat*FX_BEAT_MS/100); }
static uint64_t fx_all_known(void) { return (XZ_SHELL_KNOWN(XZ_SHELL_FIELD_COUNT)-1)&~(XZ_SHELL_KNOWN(XZ_SHELL_F_ARTWORK)|XZ_SHELL_KNOWN(XZ_SHELL_F_SOURCE_LOOK)); }

static void fx_deck_one(struct xz_shell_deck *d, const struct fx_track *t) {
 memset(d,0,sizeof *d);
 d->known=fx_all_known();
 d->loaded=1;d->title="Alone - Mixed";d->artist="Example Artist";d->source=XZ_SHELL_SRC_USB1;d->track_no=1;
 d->bpm_x100=12966;d->track_bpm_x100=FX_BPM_X100;d->tempo_x100=130;d->tempo_range=10;
 d->master_tempo=1;d->master=1;d->sync=0;d->playing=1;d->cue_standby=0;
 d->elapsed_ms=82523;d->total_ms=FX_TOTAL_MS;d->time_mode=XZ_SHELL_TIME_REMAIN;
 d->key="Fm";d->key_matches_master=1;d->key_shift=0;
 d->quantize_on=1;d->quantize_num=1;d->quantize_den=1;d->beat_jump_num=16;d->beat_jump_den=1;
 d->loop.active=0;
 static const uint32_t colors[XZ_SHELL_HOT_CUES]={0xe02020,0x20c8ff,0xff8c1a,0x28e028,0x3c3cff,0x9b3cff,0xffe020,0xff50c8};
 static const int beats[XZ_SHELL_HOT_CUES]={0,64,128,-1,192,256,-1,320};
 for(int i=0;i<XZ_SHELL_HOT_CUES;i++)if(beats[i]>=0)d->hot_cue[i]=(struct xz_shell_cue){1,fx_beat_ms(beats[i]),colors[i],i==2};
 d->memory_count=3;
 d->memory_cue[0]=(struct xz_shell_cue){1,fx_beat_ms(32),0,0};
 d->memory_cue[1]=(struct xz_shell_cue){1,fx_beat_ms(224),0,0};
 d->memory_cue[2]=(struct xz_shell_cue){1,fx_beat_ms(448),0,0};
 d->cue_ms=fx_beat_ms(64);
 d->grid=t->grid;d->grid_count=t->grid_count;d->bar_phase=(int)((d->elapsed_ms*100/FX_BEAT_MS)%4)+1;
 d->wave=(struct xz_shell_wave){t->lane,FX_LANE_COLUMNS,FX_TOTAL_MS};
 d->overview=(struct xz_shell_wave){t->overview,FX_OVERVIEW_COLUMNS,FX_TOTAL_MS};
 d->stems=(struct xz_shell_wave){t->stems,FX_LANE_COLUMNS,FX_TOTAL_MS};
}
static void fx_deck_two(struct xz_shell_deck *d, const struct fx_track *t) {
 memset(d,0,sizeof *d);
 d->known=fx_all_known();
 d->loaded=1;d->title="Night Drive (feat. Example Vocalist) [Club Remix] - Extended Mix";d->artist="Another Artist";
 d->source=XZ_SHELL_SRC_USB1;d->track_no=12;
 d->bpm_x100=13082;d->track_bpm_x100=FX_BPM_X100;d->tempo_x100=220;d->tempo_range=10;
 d->master_tempo=0;d->master=0;d->sync=1;d->playing=1;d->cue_standby=0;
 d->elapsed_ms=41088;d->total_ms=FX_TOTAL_MS;d->time_mode=XZ_SHELL_TIME_ELAPSED;
 d->key="Am";d->key_matches_master=0;d->key_shift=-1;
 d->quantize_on=0;d->quantize_num=1;d->quantize_den=2;d->beat_jump_num=4;d->beat_jump_den=1;
 d->loop=(struct xz_shell_loop){1,4,1,fx_beat_ms(84),fx_beat_ms(88)};
 d->hot_cue[0]=(struct xz_shell_cue){1,fx_beat_ms(16),0x28e028,0};
 d->hot_cue[1]=(struct xz_shell_cue){1,fx_beat_ms(96),0x28e028,0};
 d->hot_cue[3]=(struct xz_shell_cue){1,fx_beat_ms(384),0xff8c1a,1};
 d->memory_count=1;d->memory_cue[0]=(struct xz_shell_cue){1,fx_beat_ms(192),0,0};
 d->cue_ms=fx_beat_ms(16);
 d->grid=t->grid;d->grid_count=t->grid_count;d->bar_phase=(int)((d->elapsed_ms*100/FX_BEAT_MS)%4)+1;
 d->wave=(struct xz_shell_wave){t->lane,FX_LANE_COLUMNS,FX_TOTAL_MS};
 d->overview=(struct xz_shell_wave){t->overview,FX_OVERVIEW_COLUMNS,FX_TOTAL_MS};
 d->stems=(struct xz_shell_wave){t->stems,FX_LANE_COLUMNS,FX_TOTAL_MS};
}
/* Beat FX as the runtime reads it: no channel or level. */
static inline void fx_beat_fx(struct xz_shell_fx *f, int on, const char *name, int num, int den, int32_t time_ms) {
 memset(f,0,sizeof *f);
 f->known=(1u<<XZ_SHELL_FX_F_ON)|(1u<<XZ_SHELL_FX_F_NAME)|(1u<<XZ_SHELL_FX_F_BPM)|(1u<<XZ_SHELL_FX_F_TAP);
 f->on=on;f->name=name;f->bpm_x10=1297;f->tap=0;
 if(num>0){f->known|=1u<<XZ_SHELL_FX_F_BEAT;f->beat_num=num;f->beat_den=den;}
 if(time_ms>=0){f->known|=1u<<XZ_SHELL_FX_F_TIME;f->time_ms=time_ms;}
}
static void fx_model_full(struct xz_shell_model *m, const struct fx_track *t) {
 memset(m,0,sizeof *m);
 fx_deck_one(&m->deck[0],t);fx_deck_two(&m->deck[1],t);
 m->capabilities=XZ_SHELL_CAP_SEEK|XZ_SHELL_CAP_SET_SETTING|XZ_SHELL_CAP_GRID;
 m->settings.known=(1u<<XZ_SHELL_SETTING_COUNT)-1;
 m->settings.writable=((uint64_t)1<<XZ_SHELL_SETTING_COUNT)-1;
 m->settings.time_mode=XZ_SHELL_TIME_REMAIN;
 m->settings.play_mode=XZ_SHELL_PLAY_SINGLE;
 m->settings.auto_cue=1;m->settings.hot_cue_auto_load=1;
 m->link_known=1;m->link_connected=0;
 m->clock_ms=123456;
}
#endif
