#define main retained_assets_main
#include "test_jog_assets_runtime.c"
#undef main
int main(void){
 const int delta[]={0,6,26,-26,600,-600,601,-601};unsigned checks=0,failed=0;
 for(unsigned side=0;side<2;side++)for(unsigned k=0;k<sizeof delta/sizeof delta[0];k++){
  reset(27000);values.deck[side].total_ms=180000+delta[k];publish();complete();
  for(unsigned n=0;n<180;n++)tick(side);
  int expected=delta[k]>=-600&&delta[k]<=600;
  checks++;if(jd_side[side].have_wave!=expected){failed++;printf("RED side=%u delta_ms=%d overview_bin_ms=600 wave=%d expected=%d\n",side,delta[k],jd_side[side].have_wave,expected);}
  assert(jd_side[side].cached&&jd_state[side].last_info.length_ms==(unsigned)values.deck[side].total_ms);
 }
 printf("RESULT Jog waveform duration checks=%u failures=%u\n",checks,failed);return !!failed;
}
