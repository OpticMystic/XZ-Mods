#define main retained_assets_main
#include "test_jog_assets_runtime.c"
#undef main
static uint64_t hash=1469598103934665603ULL;
static void observed(unsigned side){tick(side);for(unsigned i=0;i<148;i++)hash=(hash^frame(side)[i])*1099511628211ULL;}
int main(void){
 for(unsigned side=0;side<2;side++){
#if XZ_JOG_WAVE_DIAGNOSTIC
  assert(xz_mods_jog_wave_diagnostic_v1.version==1&&xz_mods_jog_wave_diagnostic_v1.size==96);
  unsigned before=atomic_load(&xz_mods_jog_wave_diagnostic_v1.count[side][JW_COMMITTED]);
#endif
  reset(1025);complete();for(unsigned n=0;n<180;n++)observed(side);
#if XZ_JOG_WAVE_DIAGNOSTIC
  unsigned after=atomic_load(&xz_mods_jog_wave_diagnostic_v1.count[side][JW_COMMITTED]);assert(after-before==73);
#endif
  selection.deck[side]=side+1;selection.epoch[side]++;for(unsigned n=0;n<30;n++)observed(side);
#if XZ_JOG_WAVE_DIAGNOSTIC
  assert(atomic_load(&xz_mods_jog_wave_diagnostic_v1.count[side][JW_COMMITTED])==after);
#endif
 }
 printf("PASS owned wave counters and native exclusion; packet hash=%016llx\n",(unsigned long long)hash);return 0;
}
