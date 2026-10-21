#ifdef NDEBUG
#error Control focus tests require assertions
#endif
#include "../shell_runtime.c"
#include "../shell/shell_internal.h"
#include <assert.h>
static struct xz_extra_decks sample;
int xz_extra_decks_snapshot(struct xz_extra_decks *out){*out=sample;return 1;}
int xz_native_four_mixer_order(void){return 0;}
void xz_log(const char *message){(void)message;}
int main(void){
 for(int focus=0;focus<4;focus++)for(unsigned sides=0;sides<4;sides++){
  memset(&model,0,sizeof model);memset(&shell,0,sizeof shell);memset(&sample,0,sizeof sample);
  extra_reset();model.deck_count=4;shell.deck=focus;sample.route_up=1;
  sample.left_on_3=!!(sides&1);sample.right_on_4=!!(sides&2);
  extra_refresh_locked(1000);
  assert(model.sides_extra==sides&&model.deck_count==4);
  assert(shell.deck==focus);
  sample.route_up=0;extra_refresh_locked(1001);
  assert(model.deck_count==2&&shell.deck==(focus&1));
 }
 puts("PASS actual runtime side changes preserve independent browse/grid focus");
 /* The real APPEARANCE and MY SETTINGS models expose the same shell-owned choice. */
 xz_shell_init(&shell);fill_look();
 int appearance=0,waveform=0;
 for(uint32_t i=0;i<look_model.count;i++)if(look_model.items[i].id==LOOK_WAVE_TITLES){
  assert(look_model.items[i].known&&look_model.items[i].writable&&look_model.items[i].current==0);appearance++;
 }
 for(uint32_t i=0;i<wave_model.count;i++)if(wave_model.items[i].id==LOOK_WAVE_TITLES)waveform++;
 assert(appearance==1&&waveform==1&&xz_shrt_shell_owned_setting(XZ_SHELL_ITEM_WAVE_TITLES));
 look_set_locked(LOOK_WAVE_TITLES,1,1002);
 assert(!sh_indicator_shown(&shell,XZ_SHELL_IND_WAVE_TITLES));
 xz_shell_init(&shell);restore_display();
 assert(!sh_indicator_shown(&shell,XZ_SHELL_IND_WAVE_TITLES));
 fill_look();
 for(uint32_t i=0;i<look_model.count;i++)if(look_model.items[i].id==LOOK_WAVE_TITLES)assert(look_model.items[i].current==1);
 look_set_locked(LOOK_WAVE_TITLES,0,1003);look_set_locked(LOOK_WAVE_TITLES,2,1004);
 assert(sh_indicator_shown(&shell,XZ_SHELL_IND_WAVE_TITLES)&&!ui_action_count);
 puts("PASS actual Appearance/MY SETTINGS title choice and RAM restore without native actions");
}
