/* R29 independent boundaries. Public selector/input/worker/reader/sink only. */
#define main retained_reference_main
#include "test_extra_player.c"
#undef main
#include "deck_select.h"
#include "shell_runtime.h"
static unsigned checks,failures,cases;
static const char *label;
static int side;
static FILE *pcm_file;
static uint64_t captured_after[2];
static unsigned runtime_delivered,runtime_refused;
void xz_extra_decks_publish_sides(int left,int right){(void)left;(void)right;}
static void review_notice(const char *text){(void)text;}
static void (*review_notice_callback)(const char *)=review_notice;
static void xz_log(const char *text){(void)text;}
int xz_extra_deck_input(const struct xz_extra_deck_event *e){runtime_delivered++;int r=input(*e);if(!r)runtime_refused++;return r;}
#define xz_shell_runtime_notice review_notice_callback
#include "runtime-outcome.inc"
#undef xz_shell_runtime_notice
static xz_usb_result review_submit(void *ctx,uint32_t epoch,int d,uint32_t gen,const float *v,uint32_t n,uint64_t after){int capture=f.capture[d];xz_usb_result r=fake_submit(ctx,epoch,d,gen,v,n,after);if(capture&&r==XZ_USB_OK)captured_after[d]=after;return r;}
static xz_usb_result review_transition(void *ctx,uint32_t epoch,int d,uint32_t expected,const float *v,uint32_t n,uint64_t after,uint32_t *gen){int capture=f.capture[d];xz_usb_result r=fake_transition(ctx,epoch,d,expected,v,n,after,gen);if(capture&&r==XZ_USB_OK)captured_after[d]=after;return r;}
#define CK(x) do{checks++;if(!(x)){failures++;printf("RED R29 case=%u side=%d label=%s line=%d check=%s\n",cases,side,label,__LINE__,#x);}}while(0)
static int review_resolve(int source,uint32_t id,struct xz_extra_track *out){
 if(id!=5)return resolve(source,id,out);memset(out,0,sizeof *out);out->source=source;out->track_id=id;
 snprintf(out->path,sizeof out->path,"%s/short44100.wav",fixtures);snprintf(out->anlz_dir,sizeof out->anlz_dir,"%s",anlz_dir);snprintf(out->title,sizeof out->title,"Short");out->track_bpm_x100=12000;return 0;
}
static void begin(int d,int playing,int vinyl,int quantize,const char *name){
 cases++;side=d;label=name;clock_ms=1000;wire_horizon=0;position_interval=position_noise=position_blocked=threaded_clock=hold_flush=0;
 struct xz_xp_env e={{NULL,fake_link,review_submit,fake_flush,fake_position,review_transition},now_ms,review_resolve,master_bpm,NULL,order_resolve,vinyl,quantize};
 p=xz_xp_create(&e);assert(p);fake_route(29000+cases);step();load(d,d?2:1);load(1-d,1);seek(d,700);seek(1-d,1000);if(playing)press(d,XZ_EXTRA_IN_PLAY);
}
static void end(void){xz_xp_destroy(p);p=NULL;}
static struct xz_extra_deck_event search(int d,int direction,int down){struct xz_extra_deck_event e=event(d,XZ_EXTRA_IN_SEARCH,down);e.hot_cue=direction;return e;}
static void fill(int d){for(unsigned i=0;i<64;i++)assert(input(event(d,XZ_EXTRA_IN_JOG_TURN,1)));}
static void pcm(int d,uint64_t origin,int direction,double rate){
 CK(f.captured_frames[d]>=32);double err=0;double sr=debug(d).source_rate;
 for(unsigned n=0;n<f.captured_frames[d];n++)for(unsigned ch=0;ch<2;ch++){
  double frame=origin+direction*rate*n*sr/XZ_USB_RATE;
  err=fmax(err,fabs(f.captured[d][2*n+ch]-.5*sin(6.283185307179586*440*frame/sr)));
 }
 CK(err<.001);double after=origin+direction*rate*f.captured_frames[d]*sr/XZ_USB_RATE;
 CK(fabs((double)captured_after[d]-after)<=1);
 assert(fwrite(f.captured[d],sizeof(float),2*f.captured_frames[d],pcm_file)==2*f.captured_frames[d]);
 printf("PCM R29 case=%u side=%d label=%s origin=%llu frames=%u after=%llu rate=%.1f direction=%d error=%.9f\n",cases,d,label,(unsigned long long)origin,f.captured_frames[d],(unsigned long long)captured_after[d],rate,direction,err);
}
static struct xz_ds_outcome g(struct xz_deck_select *s,int d,uint16_t code,unsigned op){return xz_deck_select_global_key(s,d+1,code,op,clock_ms);}
static void delivery(struct xz_ds_outcome o){unsigned n=runtime_delivered,r=runtime_refused;int consumed=deck_outcome(&o,0,0,1,0);CK(consumed==(o.verdict!=XZ_DS_PASS&&!o.also_stock));CK(runtime_delivered==n+(o.verdict==XZ_DS_DELIVER));CK(runtime_refused==r);}
static void selection(struct xz_deck_select *s,int d,int away,int shift){xz_deck_select_init(s);xz_deck_select_route(s,1);if(away)xz_deck_select_toggle(s,d,NULL);if(shift)g(s,d,0x4103,0);}
static void search_overlap(void){
 for(int d=0;d<2;d++)for(int first=0;first<2;first++){
  begin(d,1,0,0,"search-overlap-old-UP");
  assert(input(search(d,first,1)));step();assert(input(search(d,1-first,1)));step();
  unsigned gen=debug(d).generation;double rate=debug(d).speed;unsigned other=debug(1-d).generation;
  assert(input(search(d,first,0)));step();
  CK(debug(d).speed==rate&&rate==4.0);CK(debug(d).generation==gen);CK(debug(1-d).generation==other);
  assert(input(search(d,1-first,0)));step();CK(debug(d).speed==1.0);end();
  begin(d,1,0,0,"search-overlap-reserved-UP-full64");
  assert(input(search(d,first,1)));step();assert(input(search(d,1-first,1)));step();
  fill(d);CK(input(search(d,first,0)));int accepted=input(search(d,1-first,0));CK(accepted);step();
  CK(debug(d).speed==1.0);end();
 }
}
static void release_capacity(void){
 for(int d=0;d<2;d++)for(int rotation=0;rotation<14;rotation++){
  begin(d,0,0,0,"all-distinct-release-reservations-full64");struct xz_extra_deck_event e[14];
  e[0]=event(d,XZ_EXTRA_IN_CUE,1);e[1]=event(d,XZ_EXTRA_IN_JOG_TOUCH,1);e[2]=event(d,XZ_EXTRA_IN_LOOP_IN,1);e[3]=event(d,XZ_EXTRA_IN_REVERSE,1);
  for(int n=0;n<8;n++){e[n+4]=event(d,XZ_EXTRA_IN_HOT_CUE,1);e[n+4].hot_cue=n;}
  e[12]=search(d,0,1);e[13]=search(d,1,1);for(int n=0;n<14;n++){assert(input(e[n]));step();}
  fill(d);CK(!input(event(d,XZ_EXTRA_IN_JOG_TURN,1)));for(int n=0;n<14;n++){int i=(n+rotation)%14;e[i].pressed=0;CK(input(e[i]));}
  step();step();CK(input(event(d,XZ_EXTRA_IN_JOG_TURN,1)));end();
 }
}
static void search_lifecycle(void){
 for(int d=0;d<2;d++)for(int direction=0;direction<2;direction++){
  begin(d,1,0,0,"search-route-cancel");assert(input(search(d,direction,1)));step();fake_route(40000+cases);step();seek(d,700);press(d,XZ_EXTRA_IN_PLAY);CK(debug(d).speed==1.0);end();
  begin(d,1,0,0,"search-CUE-cancel-realPCM");assert(input(search(d,direction,1)));step();press(d,XZ_EXTRA_IN_CUE);seek(d,700);f.capture[d]=1;press(d,XZ_EXTRA_IN_PLAY);CK(debug(d).speed==1.0);pcm(d,ms_frames(d,700),1,1);end();
  begin(d,1,0,0,"search-jog-takeover-old-UP");press(d,XZ_EXTRA_IN_VINYL);assert(input(search(d,direction,1)));step();hold(d,XZ_EXTRA_IN_JOG_TOUCH,1);jog(d,5);step();hold(d,XZ_EXTRA_IN_JOG_TOUCH,0);struct xz_extra_deck_event z=event(d,XZ_EXTRA_IN_JOG_TURN,1);z.jog.speed_known=1;assert(input(z));step();advance(100);CK(debug(d).state==XZ_XP_PLAYING&&debug(d).speed==1.0);end();
  begin(d,1,0,0,"search-load-cancel");assert(input(search(d,direction,1)));step();load(d,d?2:1);seek(d,700);press(d,XZ_EXTRA_IN_PLAY);CK(debug(d).speed==1.0);end();
  begin(d,1,0,0,"search-held-duplicate-END");assert(input(search(d,direction,1)));step();struct xz_extra_deck_event h=search(d,direction,1);h.held=1;unsigned gen=debug(d).generation;assert(input(h));step();CK(debug(d).generation==gen);assert(input(search(d,direction,0)));step();gen=debug(d).generation;assert(input(search(d,direction,0)));step();CK(debug(d).generation==gen);CK(debug(d).speed==1);end();
 }
}
static void search_routes(void){
 for(int d=0;d<2;d++)for(int direction=0;direction<2;direction++)for(unsigned up=2;up<=3;up++){
  begin(d,1,0,0,"search-decoded-owner-switch-full64");struct xz_deck_select s;selection(&s,d,1,0);
  for(int edge=0;edge<2;edge++){
   unsigned char bytes[24]={0};uint16_t code=(uint16_t)(0x4121+direction);memcpy(bytes+8,&code,2);bytes[10]=(unsigned char)(d+1);bytes[11]=(unsigned char)(edge?up:0);
   struct xz_ds_key k;assert(xz_ds_key_decode_global(bytes,&k));struct xz_ds_outcome o=xz_deck_select_global_player(&s,&k,clock_ms);
   CK(o.verdict==XZ_DS_DELIVER&&o.event.deck==d&&o.event.hot_cue==direction);delivery(o);step();
   if(!edge){xz_deck_select_toggle(&s,d,NULL);fill(d);}
  }
  CK(debug(d).speed==1.0);end();
 }
 for(int d=0;d<2;d++)for(int direction=0;direction<2;direction++)for(int reversed_mode=0;reversed_mode<2;reversed_mode++){
  begin(d,1,0,0,"search-direction-realPCM");if(reversed_mode)press(d,XZ_EXTRA_IN_REVERSE);seek(d,700);f.capture[d]=1;
  assert(input(search(d,direction,1)));step();pcm(d,ms_frames(d,700),(direction^reversed_mode)?-1:1,4);CK(debug(d).speed==4);assert(input(search(d,direction,0)));step();CK(debug(d).speed==1);end();
 }
}
static void new_owners(void){
 for(int d=0;d<2;d++)for(int target=0;target<2;target++){
  begin(d,0,0,0,"new-CALL-or-PREV-retires-held-REV");load(d,1);seek(d,700);press(d,XZ_EXTRA_IN_PLAY);press(d,XZ_EXTRA_IN_SLIP);hold(d,XZ_EXTRA_IN_REVERSE,1);
  press(d,target?XZ_EXTRA_IN_TRACK_PREV:XZ_EXTRA_IN_LOOP_HALF);unsigned gen=debug(d).generation;uint64_t at=debug(d).position;
  CK(!view.deck[d].reverse&&!view.deck[d].slip_active);hold(d,XZ_EXTRA_IN_REVERSE,0);CK(debug(d).generation==gen);CK(debug(d).position==at);end();
 }
 for(int d=0;d<2;d++)for(int dir=0;dir<2;dir++){
  begin(d,1,0,0,"new-pad-retires-SEARCH");mode(d,XZ_EXTRA_PAD_SLIP_LOOP,0);assert(input(search(d,dir,1)));step();pad_key(d,0,1);
  CK(debug(d).speed==1);unsigned gen=debug(d).generation;assert(input(search(d,dir,0)));step();CK(debug(d).generation==gen);pad_key(d,0,0);end();
 }
 for(int d=0;d<2;d++)for(int gap=200;gap<=600;gap+=400)for(int parity=0;parity<2;parity++){
  begin(d,0,0,0,"previous-window-uses-arrival");uint32_t ids[]={1,2};uint8_t kinds[]={1,1};struct xz_extra_order *o=xz_extra_order_create(1,42,ids,kinds,2,1);assert(o);
  struct xz_extra_deck_event e=event(d,XZ_EXTRA_IN_LOAD,1);e.load.source=1;e.load.track_id=2;e.load.media_generation=42;e.load.route_epoch=f.link;e.load.order=o;e.load.order_index=1;assert(input(e));xz_extra_order_release(o);step();
  struct xz_extra_deck_event prev=event(d,XZ_EXTRA_IN_TRACK_PREV,1);assert(input(prev));prev.pressed=0;assert(input(prev));clock_ms+=(unsigned)gap;prev.pressed=1;assert(input(prev));clock_ms+=100u+(unsigned)parity;step();CK(!strcmp(view.deck[d].title,gap<500?"Track A":"Track B"));end();
 }
}
static void stock_boundaries(void){
 for(int d=0;d<2;d++){
  begin(d,0,0,1,"quantized-nearest-floor-offset-grid");load(d,1);seek(d,840);press(d,XZ_EXTRA_IN_CUE);CK(debug(d).cue==ms_frames(d,600));seek(d,860);press(d,XZ_EXTRA_IN_CUE);CK(debug(d).cue==ms_frames(d,1100));seek(d,840);mode(d,XZ_EXTRA_PAD_BEAT_LOOP,0);pad_key(d,0,1);pad_key(d,0,0);CK(debug(d).loop_in==ms_frames(d,600));CK(debug(d).loop_out>debug(d).loop_in&&debug(d).loop_out<=ms_frames(d,2000));end();
  begin(d,0,0,1,"quantized-short-track-before-first-gridpoint");load(d,5);seek(d,25);press(d,XZ_EXTRA_IN_CUE);CK(debug(d).cue<ms_frames(d,50));mode(d,XZ_EXTRA_PAD_BEAT_LOOP,0);pad_key(d,0,1);pad_key(d,0,0);CK(debug(d).loop_active&&debug(d).loop_in<debug(d).loop_out&&debug(d).loop_out<=ms_frames(d,50));end();
  begin(d,1,1,0,"instant-PLAY-default-vinyl-pause-brake");CK(view.deck[d].vinyl&&debug(d).speed==1.0);advance(20);uint64_t at=completed_tag(d);press(d,XZ_EXTRA_IN_PLAY);advance(100);uint64_t delta=completed_tag(d)-at;CK(debug(d).state==XZ_XP_PLAYING&&delta>ms_frames(d,1)&&delta<ms_frames(d,100));press(d,XZ_EXTRA_IN_PLAY);CK(debug(d).state==XZ_XP_PLAYING&&debug(d).speed==1);end();
  cases++;side=d;label="empty-mode-truth";struct xz_xp_env e={{NULL,fake_link,review_submit,fake_flush,fake_position,review_transition},now_ms,resolve,master_bpm,NULL,NULL,1,1};p=xz_xp_create(&e);assert(p);fake_route(50000+cases);step();CK(!view.deck[d].loaded&&view.deck[d].vinyl&&view.deck[d].quantize&&view.deck[d].auto_cue);CK(view.deck[d].known==(XZ_EXTRA_KNOWN(XZ_EXTRA_F_VINYL)|XZ_EXTRA_KNOWN(XZ_EXTRA_F_MASTER_TEMPO)|XZ_EXTRA_KNOWN(XZ_EXTRA_F_QUANTIZE)|XZ_EXTRA_KNOWN(XZ_EXTRA_F_TIME_MODE)));end();
 }
}
static void modes(void){
 for(int d=0;d<2;d++){
  begin(d,0,0,0,"global-mode-stale-route-does-not-change-next-target");
  struct xz_extra_deck_event e=event(d,XZ_EXTRA_IN_QUANTIZE,1);assert(input(e));
  fake_route(60000+cases);step();CK(!view.deck[0].quantize&&!view.deck[1].quantize);
  assert(input(e));step();CK(view.deck[0].quantize&&view.deck[1].quantize);end();
 }
 for(int d=0;d<2;d++)for(int mode=0;mode<3;mode++){
  begin(d,0,0,0,"global-mode-opposite-queue-full");fill(1-d);enum xz_extra_input kind=mode==0?XZ_EXTRA_IN_QUANTIZE:mode==1?XZ_EXTRA_IN_TIME_MODE:XZ_EXTRA_IN_AUTO_CUE;
  struct xz_extra_deck_event e=event(d,kind,1);CK(input(e));stepping_side=d;xz_xp_step(p,d);stepping_side=1-d;xz_xp_step(p,1-d);step();
  CK(mode==0?view.deck[0].quantize==view.deck[1].quantize:mode==1?view.deck[0].remain==view.deck[1].remain:view.deck[0].auto_cue==view.deck[1].auto_cue);end();
 }
 for(int d=0;d<2;d++){
  begin(d,0,0,0,"quantize-duplicate-DOWN");struct xz_deck_select s;selection(&s,d,1,0);delivery(g(&s,d,0x430c,0));step();step();CK(view.deck[0].quantize&&view.deck[1].quantize);delivery(g(&s,d,0x430c,0));step();step();CK(view.deck[0].quantize&&view.deck[1].quantize);g(&s,d,0x430c,2);end();
  for(int away=0;away<2;away++)for(int longhold=0;longhold<2;longhold++){
   begin(d,0,0,0,"time-SHIFT-switch-before-UP");selection(&s,d,away,1);struct xz_ds_outcome o=g(&s,d,0x430b,0);CK((o.verdict!=XZ_DS_PASS)==away);xz_deck_select_toggle(&s,d,NULL);
   o=g(&s,d,0x430b,longhold?1:2);if(away){CK(o.verdict==XZ_DS_DELIVER&&!o.also_stock&&o.event.shift);delivery(o);step();CK(longhold?!view.deck[d].auto_cue:view.deck[d].remain);CK(longhold?view.deck[1-d].auto_cue:!view.deck[1-d].remain);}else{CK(o.verdict==XZ_DS_PASS);}
   if(longhold){o=g(&s,d,0x430b,2);CK(away?o.verdict==XZ_DS_SWALLOW:o.verdict==XZ_DS_PASS);}end();
  }
 }
}
static void route_matrix(void){
 for(int d=0;d<2;d++)for(int away=0;away<2;away++)for(int shift=0;shift<2;shift++)for(int which=0;which<3;which++){
  begin(d,0,0,0,"modes-normal-contact-paths");struct xz_deck_select s;selection(&s,d,away,shift);
  uint16_t code=which?0x430b:0x430c;int software=!shift||away;unsigned deliveries=0;
  for(unsigned op=0;op<4;op++){
   if(op==1&&which!=2)continue;
   struct xz_ds_outcome o=g(&s,d,code,op);int consumed=o.verdict!=XZ_DS_PASS&&!o.also_stock;
   if(op!=3)CK(consumed==(shift&&away));if(o.verdict==XZ_DS_DELIVER){deliveries++;delivery(o);}
   struct xz_ds_key k={.side=d,.code=code,.op=op,.shift=shift};
   struct xz_ds_outcome early=xz_deck_select_global_player(&s,&k,clock_ms);CK(early.verdict==XZ_DS_PASS);
   if(!consumed){struct xz_ds_outcome fallback=xz_deck_select_key(&s,&k,clock_ms);CK(fallback.verdict!=XZ_DS_DELIVER);}
   step();step();
  }
  CK(deliveries==(unsigned)software);
  if(software){CK(which==0?view.deck[d].quantize:which==1?view.deck[d].remain:!view.deck[d].auto_cue);CK(shift?(which==0?!view.deck[1-d].quantize:which==1?!view.deck[1-d].remain:view.deck[1-d].auto_cue):(which==0?view.deck[1-d].quantize:which==1?view.deck[1-d].remain:!view.deck[1-d].auto_cue));}
  end();
 }
}
static int needle(int d){
 struct xz_shell_model m={0};struct xz_shrt_extra_parts parts[2]={{0},{0}};view.route_up=1;
 xz_shrt_extra_map(&m,&view,parts,clock_ms,clock_ms);xz_shrt_extra_caps(&m,0,0,1,1);
 struct xz_shell_action a={.kind=XZ_SHELL_ACT_SEEK,.deck=d+XZ_EXTRA_FIRST_DECK,.value=900};struct xz_extra_deck_event e;
 return xz_shrt_extra_action(&m,&a,0,&e);
}
static void needle_cases(void){
 for(int d=0;d<2;d++){
  begin(d,1,1,0,"needle-held-versus-coast");CK(needle(d)==XZ_SHRT_EXTRA_REFUSE);hold(d,XZ_EXTRA_IN_JOG_TOUCH,1);CK(needle(d)==XZ_SHRT_EXTRA_INPUT);
  struct xz_extra_deck_event e=event(d,XZ_EXTRA_IN_JOG_TURN,1);e.jog.delta=5;e.jog.speed=.5;e.jog.speed_known=1;assert(input(e));step();hold(d,XZ_EXTRA_IN_JOG_TOUCH,0);CK(debug(d).state==XZ_XP_SCRATCHING);CK(needle(d)==XZ_SHRT_EXTRA_REFUSE);
  e.jog.delta=0;e.jog.speed=0;assert(input(e));step();press(d,XZ_EXTRA_IN_CUE);CK(needle(d)==XZ_SHRT_EXTRA_INPUT);end();
 }
}
int main(int argc,char **argv){
 assert(argc==3);assert(!pthread_mutex_init(&f.lock,NULL));snprintf(fixtures,sizeof fixtures,"%s",argv[1]);snprintf(anlz_dir,sizeof anlz_dir,"%s/anlz",argv[1]);build_analysis();pcm_file=fopen("pcm.f32le","wb");assert(pcm_file);
 const char *group=argv[2];
 if(!strcmp(group,"all")||!strcmp(group,"p1")){search_overlap();release_capacity();}
 if(!strcmp(group,"search")||!strcmp(group,"p2")||!strcmp(group,"all"))search_lifecycle();
 if(!strcmp(group,"all")||!strcmp(group,"p2")||!strcmp(group,"routes"))search_routes();
 if(!strcmp(group,"all")||!strcmp(group,"p2")||!strcmp(group,"owners"))new_owners();
 if(!strcmp(group,"all")||!strcmp(group,"p2")||!strcmp(group,"stock"))stock_boundaries();
 if(!strcmp(group,"all")||!strcmp(group,"p2")||!strcmp(group,"modes")){modes();route_matrix();}
 if(!strcmp(group,"all")||!strcmp(group,"p2")||!strcmp(group,"needle"))needle_cases();
 assert(!fclose(pcm_file));printf("RESULT R29 independent group=%s cases=%u checks=%u failures=%u profile=%d\n",group,cases,checks,failures,XZ_XP_JOG_TICKS_PER_REV);return failures?1:0;
}
