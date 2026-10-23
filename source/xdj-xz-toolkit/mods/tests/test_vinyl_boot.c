#define main retained_selector_main
#include "../test_deck_select.c"
#undef main
int main(void){unsigned checks=0,failed=0;
 for(int route=0;route<2;route++)for(int side=0;side<2;side++)for(int global=0;global<2;global++)for(int value=0;value<3;value++){
  reset(route);struct xz_ds_key k={.side=side,.code=0x4123,.op=XZ_DS_OP_FADER,.value=value*.5f};
  struct xz_ds_outcome o=global?xz_deck_select_global_player(&d,&k,now):xz_deck_select_key(&d,&k,now);
  checks++;if(!(o.verdict==XZ_DS_DELIVER&&o.also_stock&&o.event.kind==XZ_EXTRA_IN_VINYL_SPEED&&o.event.tempo_position==(value-1)*XZ_EXTRA_TEMPO_SPAN)){
   failed++;printf("RED route=%d side=%d hook=%d value=%d verdict=%d stock=%d\n",route,side,global,value,o.verdict,o.also_stock);
  }
 }
 printf("RESULT physical knob capture checks=%u failures=%u\n",checks,failed);return !!failed;
}
