/* Private, opt-in benchmark adapter. Included after the shell's command helpers.
   Catalog identity, media generation and native load refusals remain authoritative. */
extern int xz_runtime_bench_transport(int,int,int32_t) __attribute__((weak));
extern int xz_ui_runtime_bench_link_option(int,int,int[2]) __attribute__((weak));
#include "bench_keys.h"
#pragma weak xz_bench_keys_apply
#include "bench_lights.h"
#pragma weak xz_bench_lights_apply
static void bench_apply(const struct xz_bench_request *q,struct xz_bench_result *r,void *opaque){
 (void)opaque;r->status=-EINVAL;
 if(q->deck>4)return;
 int d=(int)q->deck;
 if(pthread_mutex_trylock(&mutex)){r->status=-EAGAIN;return;}
 struct loaded current=d<2?read_loaded(d):(struct loaded){0};
 if(q->op==9){
  if(d==4){r->args[0]=shell.display.wave_color;r->args[1]=shell.zoom;r->args[2]=xz_deck_layout_get();r->args[3]=owns_flag;r->status=0;}
  else if(d<2){r->args[0]=(int32_t)current.id;r->args[1]=(int32_t)current.state;r->args[2]=current.device_kind;r->args[3]=model.deck[d].elapsed_ms;r->status=current.known?0:-EAGAIN;}
  else{struct xz_extra_decks s;if(xz_extra_decks_snapshot(&s)){r->args[0]=(int32_t)s.deck[d-2].timeline.load_revision;r->args[1]=s.deck[d-2].loaded;r->args[2]=s.route_up;r->args[3]=s.deck[d-2].elapsed_ms;r->status=0;}else r->status=-EAGAIN;}
 }else if(q->op==2){
  int slot=q->args[1]-1;
  if(d<2&&q->args[0]>0&&slot>=0&&slot<2&&q->args[2]>=0&&current.known&&current.id==(uint32_t)q->args[2]&&
     has(G_LOAD)&&load_enabled&&usb[slot].lib&&usb[slot].generation&&xz_shlib_track(usb[slot].lib,(uint32_t)q->args[0])){
   struct xz_shrt_command c={XZ_SHRT_CMD_LOAD,d,0,(uint32_t)q->args[0],(uint8_t)(slot+2),now_ms(),usb[slot].generation};
   if(xz_shrt_load_generation_current(&c,(uint32_t)xz_extra_source_generation(slot+1))){r->status=xz_shrt_queue_push(&queue,&c)?0:-EBUSY;}else r->status=-ESTALE;
  }else if(d==2||d==3){
   struct xz_extra_decks s;
   if(q->args[0]>0&&slot>=0&&slot<2&&usb[slot].lib&&usb[slot].generation&&xz_shlib_track(usb[slot].lib,(uint32_t)q->args[0])&&
      xz_extra_decks_snapshot(&s)&&s.route_up&&s.deck[d-2].timeline.load_revision==(uint32_t)q->args[2]){
    struct xz_extra_deck_event e={.deck=d-2,.kind=XZ_EXTRA_IN_LOAD,.pressed=1};
    e.load.source=slot+1;e.load.track_id=(uint32_t)q->args[0];e.load.media_generation=usb[slot].generation;e.load.route_epoch=xz_extra_decks_epoch();
    r->status=xz_extra_deck_input(&e)?0:-EAGAIN;
   }
  }
 }else if(q->op==10&&(d==2||d==3)&&q->args[0]>=0&&q->args[0]<=2){
  /* Private sync audit control for DECK 3/4: args[0] 0 = SYNC press, 1 = MASTER request,
     2 = tempo fader position args[1]. Buttons get their release, as every producer must send. */
  struct xz_extra_deck_event e={.deck=d-2,.kind=q->args[0]==0?XZ_EXTRA_IN_SYNC:q->args[0]==1?XZ_EXTRA_IN_MASTER:XZ_EXTRA_IN_TEMPO,.pressed=1};
  if(q->args[0]==2)e.tempo_position=q->args[1];
  r->status=xz_extra_deck_input(&e)?0:-EAGAIN;
  if(!r->status&&q->args[0]!=2){e.pressed=0;(void)xz_extra_deck_input(&e);}
 }else if(q->op==11&&(d==2||d==3)){
  /* Private sync audit read-out for DECK 3/4, live from the same snapshot the screens use:
     [0] requested | locked<<1 | playing<<2 | master<<3 | tempo_matched<<4 | reason<<8 | (master_deck+1)<<16,
     [1] phase error in microseconds, [2] correction in ppm, [3] BPM x100. */
  struct xz_extra_decks s;
  if(q->args[0]==1){
   /* Second page, what a beat lock needs:
      [0] quantize | quantize_den<<4 | quantum_unknown<<8 | sync source<<12 | kept offset<<16 | master_paused<<17 | loaded<<18,
      [1] beats in this track's own grid, [2] the track's analysed BPM x100, [3] tempo in 0.01 %. */
   struct xz_extra_analysis_hold hold={0};
   if(xz_extra_decks_snapshot_held(&s,&hold)){
    const struct xz_extra_deck *k=&s.deck[d-2];
    r->args[0]=(int32_t)((k->quantize?1u:0u)|(k->quantize_den&15u)<<4|(k->sync.quantum_unknown?256u:0u)|((unsigned)k->sync.source&15u)<<12|
      (k->sync.offset?1u<<16:0u)|(k->sync.master_paused?1u<<17:0u)|(k->loaded?1u<<18:0u));
    r->args[1]=(int32_t)k->grid_count;r->args[2]=k->track_bpm_x100;r->args[3]=k->tempo_x100;r->status=0;
   }else r->status=-EAGAIN;
   xz_extra_decks_release_analysis(&s,&hold);
  }else if(xz_extra_decks_snapshot(&s)){
   const struct xz_extra_deck *k=&s.deck[d-2];
   r->args[0]=(int32_t)((k->sync.requested?1u:0u)|(k->sync.locked?2u:0u)|(k->playing?4u:0u)|(k->master?8u:0u)|(k->sync.tempo_matched?16u:0u)|
     ((unsigned)k->sync.reason&255u)<<8|((unsigned)(k->sync.master_deck+1)&255u)<<16);
   r->args[1]=k->sync.phase_error_us;r->args[2]=k->sync.correction_ppm;r->args[3]=(int32_t)k->bpm_x100;r->status=0;
  }else r->status=-EAGAIN;
 }else if(q->op==12&&d==2&&q->args[0]>=1&&q->args[0]<=3){
  /* Private diagnostic: capture what the mixer sends back over the loopback USB link (extra_decks.h).
     args[0] 1 start (args[1] ms), 2 progress, 3 write the file. Reply: frames filled, frames wanted, done. */
  uint32_t out[3];int rc=xz_extra_decks_usb_capture(q->args[0],(uint32_t)q->args[1],out);
  r->args[0]=(int32_t)out[0];r->args[1]=(int32_t)out[1];r->args[2]=(int32_t)out[2];r->status=rc;
 }else if(q->op==4&&d==4&&q->args[0]>=0&&q->args[0]<=XZ_SHELL_WAVE_STEMS){
  xz_shell_restore_setting(&shell,XZ_SHELL_SET_WAVE_COLOR,q->args[0]);wave_color_explicit=1;dirty_flag=1;r->status=0;
 }else if(q->op==5&&d==4&&q->args[0]>=0&&q->args[0]<XZ_LAYOUT_COUNT){
  xz_deck_layout_load(q->args[0]);dirty_flag=1;r->status=0;
 }else if(q->op==8&&d==4&&q->args[0]>=0&&q->args[0]<=XZ_SHELL_ZOOM_MAX){
  shell.zoom=q->args[0];dirty_flag=1;r->status=0;
 }else if(q->op==13&&d==4&&q->args[0]>=0&&q->args[0]<=3&&q->args[1]>=0&&q->args[1]<=1){
  /* Private diagnostic: PRO DJ LINK DECKS 3/4 (0) or ABLETON LINK (1) as the NETWORK page sets it, unsaved; 2 reads. */
  pthread_mutex_unlock(&mutex);
  int now[2]={0,0};
  r->status=xz_ui_runtime_bench_link_option?xz_ui_runtime_bench_link_option(q->args[0],q->args[1],now):-ENOSYS;
  r->args[0]=now[0];r->args[1]=now[1];return;
 }else if(q->op>=XZ_BL_OP_FIRST&&q->op<=XZ_BL_OP_LAST){
  /* bench_lights.h: MASTER / BEAT SYNC lights and the master state behind them; MASTER, SYNC, deck-select
     presses. Nothing of the shell's is needed, and the keys come back through hooks that take the lock. */
  pthread_mutex_unlock(&mutex);
  if(xz_bench_lights_apply)xz_bench_lights_apply(q,r);else r->status=-ENOSYS;
  return;
 }else if((q->op>=XZ_BK_OP_FIRST&&q->op<=XZ_BK_OP_LAST)||q->op==XZ_BK_OP_USB_STOP){
  /* bench_keys.h: loops, beat jumps, hot cues, MASTER REC / TRACK MARK. The shell's identity of a stock deck is
     copied under the lock, which is then released: the keys come back through hooks that take it. */
  struct xz_bk_native_deck native={0,0,0};
  if(d<2){native.settled=settled(&current);native.id=current.id;native.contact=(int)__atomic_load_n(&native_contact_down[d],__ATOMIC_ACQUIRE);}
  pthread_mutex_unlock(&mutex);
  if(xz_bench_keys_apply)xz_bench_keys_apply(q,r,&native);else r->status=-ENOSYS;
  return;
 }else if(q->op==6&&d==4&&q->args[0]>=0&&q->args[0]<=1){
  pthread_mutex_unlock(&mutex);xz_audio_set_enabled(q->args[0]);r->status=0;return;
 }else if(q->op==7&&d<2&&q->args[0]>0&&settled(&current)&&current.id==(uint32_t)q->args[0]&&
          q->args[1]>=0&&q->args[1]<=100&&q->args[2]>=0&&q->args[2]<=100&&q->args[3]>=0&&q->args[3]<=100){
  pthread_mutex_unlock(&mutex);
  struct xz_audio_status status;
  if(xz_audio_get_status(d,&status)){r->status=-EAGAIN;return;}
  float gains[4]={q->args[1]/100.f,q->args[2]/100.f,q->args[3]/100.f,1.f};
  r->status=xz_audio_set_levels4_for_generation(d,status.generation,gains)?-ESTALE:0;return;
 }else if((q->op==1||q->op==3)&&d<2){
  if(q->args[0]>0&&settled(&current)&&current.id==(uint32_t)q->args[0]&&xz_runtime_bench_transport&&
     ((q->op==1&&q->args[1]>=0&&q->args[1]<=1)||(q->op==3&&q->args[1]>=0&&(model.deck[d].known&K(TOTAL))&&q->args[1]<=model.deck[d].total_ms))){
   if(__atomic_load_n(&native_contact_down[d],__ATOMIC_ACQUIRE)){r->status=-EBUSY;}
   else{pthread_mutex_unlock(&mutex);r->status=xz_runtime_bench_transport(d,(int)q->op,q->args[1]);return;}
  }else r->status=-ESTALE;
 }else if((q->op==1||q->op==3)&&(d==2||d==3)){
  struct xz_extra_decks s;
  if(xz_extra_decks_snapshot(&s)&&s.route_up&&s.deck[d-2].loaded&&s.deck[d-2].timeline.load_revision==(uint32_t)q->args[0]){
   const struct xz_extra_deck *deck=&s.deck[d-2];
   if(deck->platter_contact||deck->held_playing)r->status=-EBUSY;
   else if(q->op==1&&q->args[1]>=0&&q->args[1]<=1){
    if(!!deck->playing==q->args[1])r->status=0;
    else{struct xz_extra_deck_event e={.deck=d-2,.kind=XZ_EXTRA_IN_PLAY,.pressed=1};r->status=xz_extra_deck_input(&e)?0:-EAGAIN;}
   }else if(q->op==3&&q->args[1]>=0&&q->args[1]<=deck->total_ms){
    struct xz_extra_deck_event e={.deck=d-2,.kind=XZ_EXTRA_IN_SEEK,.pressed=1};e.seek_ms=q->args[1];r->status=xz_extra_deck_input(&e)?0:-EAGAIN;
   }
  }else r->status=-ESTALE;
 }
 pthread_mutex_unlock(&mutex);
}
