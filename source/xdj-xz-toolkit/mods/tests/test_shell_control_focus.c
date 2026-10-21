#ifdef NDEBUG
#error Control focus tests require assertions
#endif
#include "../shell_runtime.c"
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
}
