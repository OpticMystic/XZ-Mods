/* DECK 3/4 jog feel at the hardware count estimate (issue #5): a released backspin follows the
   platter's whole free spin, and a side nudge bends the speed by platter speed, not by encoder
   resolution. Synthetic packets through the production decoder; physical feel stays unverified. */
#define main upstream_player_main
#include "../test_extra_player.c"
#undef main
#include "../deck_select.h"

static unsigned checks,failures,epoch=1900;
static void check(const char *name,int pass){
 checks++;if(!pass){failures++;fprintf(stderr,"FAIL %s\n",name);}
}
static void tick(unsigned ms){while(ms--){clock_ms++;consume(0,44.1);consume(1,44.1);step();}}

/* 30 s of 44.1 kHz sine: room for a long backspin. */
#define LONG_ID 77u
static void write_long(void){
 char path[1100];snprintf(path,sizeof path,"%s/long44100.wav",fixtures);
 FILE *out=fopen(path,"wb");assert(out);
 uint32_t frames=30u*44100u,bytes=frames*4u,riff=36u+bytes,fmt=16u,rate=44100u,byte_rate=rate*4u;
 uint16_t pcm=1,channels=2,align=4,bits=16;
 fwrite("RIFF",1,4,out);fwrite(&riff,4,1,out);fwrite("WAVEfmt ",1,8,out);fwrite(&fmt,4,1,out);
 fwrite(&pcm,2,1,out);fwrite(&channels,2,1,out);fwrite(&rate,4,1,out);fwrite(&byte_rate,4,1,out);
 fwrite(&align,2,1,out);fwrite(&bits,2,1,out);fwrite("data",1,4,out);fwrite(&bytes,4,1,out);
 for(uint32_t n=0;n<frames;n++){int16_t v=(int16_t)lrint(8000*sin(6.283185307179586*440*n/44100.));fwrite(&v,2,1,out);fwrite(&v,2,1,out);}
 fclose(out);
}
static int feel_resolve(int source,uint32_t id,struct xz_extra_track *out){
 if(id!=LONG_ID)return resolve(source,id,out);
 memset(out,0,sizeof *out);out->source=source;out->track_id=id;
 snprintf(out->path,sizeof out->path,"%s/long44100.wav",fixtures);
 snprintf(out->title,sizeof out->title,"Long");return 0;
}

static struct xz_deck_select selector;
/* One hardware JOG packet (code 0x4305, op 4) through the production decoder and selector. */
static void packet(int side,int32_t delta,float speed){
 unsigned char raw[24]={0};uint16_t code=0x4305;
 memcpy(raw+8,&code,2);raw[10]=(unsigned char)(side+1);raw[11]=4;
 memcpy(raw+12,&delta,4);memcpy(raw+16,&speed,4);
 struct xz_ds_key key;assert(xz_ds_key_decode_global(raw,&key));
 struct xz_ds_outcome result=xz_deck_select_global_player(&selector,&key,clock_ms);
 assert(result.verdict==XZ_DS_DELIVER&&result.event.jog.delta==delta);
 assert(input(result.event));
}
static void start_deck(int side,int vinyl){
 struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,feel_resolve,master_bpm,NULL,NULL,0,0};
 p=xz_xp_create(&env);assert(p);clock_ms=1000;fake_route(++epoch);step();
 load(side,LONG_ID);seek(side,20000);
 if(vinyl)press(side,XZ_EXTRA_IN_VINYL);
 press(side,XZ_EXTRA_IN_PLAY);tick(200);
 xz_deck_select_init(&selector);xz_deck_select_route(&selector,1);
 struct xz_ds_outcome selected;xz_deck_select_toggle(&selector,side,&selected);assert(selected.toggled);
}

/* Hand on the platter top pushes it back to peak rev/s over push_ms, lets go, and the platter spins
   freely down to rest over coast_ms. Packets every cadence ms carry the counts since the last one;
   the worker runs every worker ms, as a busy or slow pass would. The speed field is unmeasured on
   hardware, so samples vary: SPEED_DECAY reports the decaying speed and a final zero, SPEED_GAPS
   also sends zero-speed samples between counts, SPEED_SILENT sends no sample after the last count. */
enum speed_samples { SPEED_DECAY, SPEED_GAPS, SPEED_SILENT };
static const char *const speed_name[]={"decay","gaps","silent"};
static double completed_ms(int side){return (double)completed_tag(side)*1000./debug(side).source_rate;}
static double source_ms(int side){return (double)debug(side).position*1000./debug(side).source_rate;}
static void backspin(int side,double peak,unsigned push_ms,unsigned coast_ms,unsigned cadence,unsigned worker,
                     enum speed_samples samples){
 start_deck(side,1);
 hold(side,XZ_EXTRA_IN_JOG_TOUCH,1);tick(100);
 assert(debug(side).state==XZ_XP_SCRATCHING);
 uint32_t notices=view.notice_seq;
 double before=source_ms(side),sent=0;uint32_t resumed_at=0,rest_at=0;
 const double counts=XZ_XP_JOG_TICKS_PER_REV;
 int released=0;unsigned total=push_ms+coast_ms,end=total+cadence;
 for(unsigned t=1;t<=end+(samples==SPEED_SILENT?0:8*cadence);t++){
  clock_ms++;consume(0,44.1);consume(1,44.1);
  if(!(t%cadence)){
   double at=t>total?total:t;
   /* revolutions moved by time at: linear ramp up, then linear spin-down to rest */
   double rev=at<=push_ms?.5*peak*at*at/(1000.*push_ms):
    .5*peak*push_ms/1000.+peak*(at-push_ms)/1000.-.5*peak*(at-push_ms)*(at-push_ms)/(1000.*coast_ms);
   double now_speed=at<=push_ms?peak*at/push_ms:peak*(1.-(at-push_ms)/coast_ms);
   int32_t delta=(int32_t)lrint(-rev*counts-sent);sent+=delta;
   if(delta||samples!=SPEED_SILENT)packet(side,delta,(float)(delta||now_speed>.001?-now_speed:0.));
   if(delta)rest_at=clock_ms;
   if(samples==SPEED_GAPS&&t<total)packet(side,0,0.f);
   if(!released&&t>=push_ms){assert(input(event(side,XZ_EXTRA_IN_JOG_TOUCH,0)));released=1;}
  }
  if(!(t%worker)){step();if(!resumed_at&&debug(side).state==XZ_XP_PLAYING)resumed_at=clock_ms;}
 }
 for(unsigned t=0;t<300&&!resumed_at;t++){tick(1);if(debug(side).state==XZ_XP_PLAYING)resumed_at=clock_ms;}
 int resumed=resumed_at!=0;
 /* Play resumes where the platter came to rest: later the heard position is that much past it. */
 if(resumed&&clock_ms-resumed_at<200)tick(200-(clock_ms-resumed_at));
 double moved=completed_ms(side)-(double)(clock_ms-resumed_at)-before,expect=sent/counts*XZ_XP_SEARCH_MS_PER_REV;
 printf("BACKSPIN side=%d peak=%.1frev/s push=%ums coast=%ums cadence=%ums worker=%ums speed=%s platter=%.0fms heard=%.0fms resumed=%d after_rest=%dms notice=%s\n",
        side,peak,push_ms,coast_ms,cadence,worker,speed_name[samples],expect,moved,resumed,(int)(resumed_at-rest_at),view.notice_seq!=notices?view.notice:"-");
 char name[200];
 snprintf(name,sizeof name,"backspin %.1frev/s %ums coast, %ums packets, %ums worker, %s speed rewinds the platter's travel",peak,coast_ms,cadence,worker,speed_name[samples]);
 check(name,fabs(moved-expect)<=5.+(cadence>worker?cadence:worker));
 snprintf(name,sizeof name,"backspin %.1frev/s %ums coast, %ums packets, %ums worker, %s speed resumes play",peak,coast_ms,cadence,worker,speed_name[samples]);
 check(name,resumed&&(uint32_t)(resumed_at-rest_at)<=XZ_XP_COAST_QUIET_MS+70u+worker);
 xz_xp_destroy(p);p=NULL;
}

/* Playing, platter top untouched: turn the side at rev_per_s for hold_ms and read the speed. */
static double nudge(int side,double rev_per_s,unsigned hold_ms,unsigned cadence){
 start_deck(side,0);
 double sent=0;double peak=1.;
 for(unsigned t=cadence;t<=hold_ms;t+=cadence){
  tick(cadence);
  int32_t delta=(int32_t)lrint(rev_per_s*XZ_XP_JOG_TICKS_PER_REV*t/1000.-sent);sent+=delta;
  packet(side,delta,(float)rev_per_s);step();
  double s=debug(side).speed;if(fabs(s-1.)>fabs(peak-1.))peak=s;
 }
 tick(XZ_XP_BEND_DECAY_MS+cadence);
 check("nudge ends after the decay",fabs(debug(side).speed-1.)<1e-9);
 xz_xp_destroy(p);p=NULL;
 return peak-1.;
}

int main(int argc,char **argv){
 assert(argc==2);assert(!pthread_mutex_init(&f.lock,NULL));
 snprintf(fixtures,sizeof fixtures,"%s",argv[1]);
 snprintf(anlz_dir,sizeof anlz_dir,"%s/anlz",argv[1]);
 write_long();
 for(int side=0;side<2;side++){
  const unsigned cadences[]={1,4,8},workers[]={1,3,10,40};
  for(unsigned c=0;c<3;c++)for(unsigned w=0;w<4;w++){
   backspin(side,2.,80,600,cadences[c],workers[w],SPEED_DECAY);
   backspin(side,4.,80,1500,cadences[c],workers[w],SPEED_DECAY);
   backspin(side,6.,60,2500,cadences[c],workers[w],SPEED_DECAY);
  }
  for(unsigned c=0;c<3;c++){
   backspin(side,4.,80,1500,cadences[c],3,SPEED_GAPS);
   backspin(side,4.,80,1500,cadences[c],3,SPEED_SILENT);
  }
 }
 /* Side nudge: the bend is set by platter speed. Expected steady bend per rev/s is the configured
    gain; it must not depend on how many counts the encoder reports per turn. */
 for(int side=0;side<2;side++){
  const double speeds[]={.1,.25,.5,1.,2.};
  for(unsigned i=0;i<5;i++){
   double bend=nudge(side,speeds[i],400,4),reverse=nudge(side,-speeds[i],400,4);
   double expect=speeds[i]*XZ_XP_BEND_PER_REV_PER_S;if(expect>XZ_XP_BEND_MAX)expect=XZ_XP_BEND_MAX;
   printf("NUDGE side=%d rev/s=%.2f bend=%+.4f reverse=%+.4f expect=%.4f\n",side,speeds[i],bend,reverse,expect);
   char name[120];snprintf(name,sizeof name,"nudge %.2f rev/s bends by the configured gain",speeds[i]);
   check(name,fabs(bend-expect)<=.1*expect+.002&&fabs(reverse+expect)<=.1*expect+.002);
  }
 }
 printf("RESULT jog-feel counts=%d checks=%u failures=%u physical_feel=unverified\n",XZ_XP_JOG_TICKS_PER_REV,checks,failures);
 return failures?1:0;
}
