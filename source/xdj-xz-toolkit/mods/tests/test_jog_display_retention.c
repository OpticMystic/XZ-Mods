/* Actual own-code callback. Synthetic clock/assembler/route only. */
#define main original_latch_main
#include "test_jog_display_latch.c"
#undef main
static void dump(const char *tag){
 const unsigned char *p=(const unsigned char *)&xz_mods_jog_latch_v2;
 printf("LATCH_HEX %s ",tag);for(unsigned i=0;i<sizeof xz_mods_jog_latch_v2;i++)printf("%02x",p[i]);puts("");
}
static void steady(unsigned revision){for(unsigned i=0;i<100;i++){clock_ms++;publish(revision);tick(1);}}
static void unchanged(const struct xz_jdl_event *event,const unsigned char *saved){assert(!memcmp(event,saved,sizeof *event));}
int main(void){
 test_now_ms=now;
 for(unsigned i=0;i<2;i++){uint32_t channel=i+1;memcpy(objects[i]+0x64,&channel,4);}
 assert(!xz_jog_display_runtime_start()&&hook);
 struct xz_jdl_side *s=&xz_mods_jog_latch_v2.side[1];struct xz_jdl_event *normal=&s->event[14];
 unsigned saw_clear=0;
 for(unsigned i=0;i<100;i++){
  unsigned before=(unsigned)jd_state[1].phase;clock_ms++;publish(7);tick(1);
  if(before==XZ_JD_EXTRA_CLEAR&&jd_state[1].phase==XZ_JD_EXTRA){saw_clear++;assert(!GET(normal->valid));dump("clear-to-extra");}
  if(GET(normal->valid))assert(GET(normal->phase_before)==XZ_JD_EXTRA&&GET(normal->phase)==XZ_JD_EXTRA&&GET(normal->packet_command)==0x21);
 }
 assert(saw_clear&&GET(normal->valid)&&GET(normal->snapshot_reason)==XZ_JDP_VALID&&GET(normal->step_result)==XZ_JD_OK&&GET(normal->final_copied));
 unsigned char saved[sizeof *normal];memcpy(saved,normal,sizeof saved);dump("normal-loaded");
 steady(7);unchanged(normal,saved);dump("stable-cadence");
 unsigned captured_context=GET(normal->context);
 unsigned copied=GET(s->final[1]);route=0;controls=10;
 for(unsigned i=0;i<40;i++){clock_ms++;tick(1);}
 assert(GET(s->final[1])==copied+40);unchanged(normal,saved);dump("route-down-pc-return");
 puts("PASS normal loaded INFO survives route-down and 40 native callbacks; counters include all copies");
 route=3;controls=12;for(unsigned i=0;i<40;i++){clock_ms++;tick(1);}unchanged(normal,saved);dump("explicit-native");
 controls=14;steady(7);assert(GET(normal->context)!=captured_context);dump("extra-return");
 memcpy(saved,normal,sizeof saved);
 /* Source/load change replaces only when a new normal INFO copy qualifies. */
 clock_ms++;publish(8);tick(1);dump("load-first-callback");
 steady(8);assert(GET(normal->source_load_revision)==8&&GET(normal->source_media_lo)==24);dump("new-load-qualified");
 assert(memcmp(normal,saved,sizeof saved));memcpy(saved,normal,sizeof saved);
 struct xz_extra_deck d=deck(8);d.source=2;clock_ms++;d.timeline.at_ms=(uint32_t)clock_ms;
 xz_jog_display_runtime_publish_deck(1,&d,(uint32_t)clock_ms);tick(1);
 for(unsigned i=0;i<100;i++){clock_ms++;d.timeline.at_ms=(uint32_t)clock_ms;xz_jog_display_runtime_publish_deck(1,&d,(uint32_t)clock_ms);tick(1);}
 assert(GET(normal->source_device)==2&&memcmp(normal,saved,sizeof saved));dump("new-source-qualified");memcpy(saved,normal,sizeof saved);
 /* Both stop-only and immediate stop/start reject the old callback's final copy. */
 unsigned old_session=GET(normal->session);copied=GET(s->final[1]);unsigned skipped=GET(s->final[0]);
 action=1;clock_ms++;tick(1);assert(GET(s->final[1])==copied&&GET(s->final[0])==skipped+1);unchanged(normal,saved);dump("stop-guard-skipped");
 assert(!xz_jog_display_runtime_start());clock_ms++;tick(1);unchanged(normal,saved);dump("restart-no-publication");
 assert(GET(s->session)!=old_session&&!jd_side[1].cached);
 steady(9);assert(GET(normal->session)!=old_session&&GET(normal->source_session)==GET(normal->session));dump("new-session-qualified");memcpy(saved,normal,sizeof saved);
 old_session=GET(normal->session);copied=GET(s->final[1]);skipped=GET(s->final[0]);
 action=2;clock_ms++;tick(1);assert(GET(s->final[1])==copied&&GET(s->final[0])==skipped+1);unchanged(normal,saved);dump("restart-in-step-old-callback");
 clock_ms++;tick(1);unchanged(normal,saved);assert(GET(s->session)!=old_session&&!jd_side[1].cached);dump("new-session-after-old");
 steady(10);assert(GET(normal->session)!=old_session&&GET(normal->source_load_revision)==10);dump("final-qualified");
 puts("PASS control, load, source and session historical retention; old callbacks never mint accepted new-session copy");
 return 0;
}
