#ifdef NDEBUG
#error Control focus tests require assertions
#endif
#include "../shell_runtime.c"
#include "../shell/shell_internal.h"
#include <assert.h>
static struct xz_extra_decks sample;
int xz_extra_decks_snapshot(struct xz_extra_decks *out){*out=sample;return 1;}
int xz_extra_decks_snapshot_held(struct xz_extra_decks *out,struct xz_extra_analysis_hold *h){(void)h;return xz_extra_decks_snapshot(out);}
void xz_extra_decks_release_analysis(struct xz_extra_decks *out,struct xz_extra_analysis_hold *h){(void)h;for(unsigned i=0;i<2;i++){out->deck[i].wave.columns=NULL;out->deck[i].grid=NULL;}}
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
 /* The browse tile uses the existing shell-to-UI action queue, once per tap. */
 __atomic_store_n(&enabled,1,__ATOMIC_RELEASE);ui_action_count=0;ui_requests=0;
 for(int shown=1;shown>=0;shown--){
  ui_action_locked(XZ_UI_BROWSE_PREVIEW,-1,0,(float)shown,1000);
  assert(xz_shell_runtime_take_ui_requests()==XZ_SHELL_UI_ACTION);
  int kind,deck,index;float value;
  assert(xz_shell_runtime_take_ui_action(&kind,&deck,&index,&value));
  assert(kind==XZ_UI_BROWSE_PREVIEW&&deck==-1&&index==0&&value==(float)shown);
  assert(!xz_shell_runtime_take_ui_action(&kind,&deck,&index,&value)&&!xz_shell_runtime_take_ui_requests());
 }
 puts("PASS actual browse preview runtime queue: shown/hidden forwarded once to the UI settings action");
 /* MODS's TITLES IN WAVEFORMS (the panel's XZ_UI_SHELL_LOOK) and MY SETTINGS expose the same shell-owned choice. */
 xz_shell_init(&shell);fill_look();
 int waveform=0;
 for(uint32_t i=0;i<wave_model.count;i++)if(wave_model.items[i].id==LOOK_WAVE_TITLES)waveform++;
 assert(waveform==1&&xz_shrt_shell_owned_setting(XZ_SHELL_ITEM_WAVE_TITLES));
 /* the panel reads the shell's own values */
 assert(look_current(XZ_SHELL_ITEM_WAVE_TITLES)==0&&look_current(XZ_SHELL_SET_WAVE_COLOR)==(int)shell.display.wave_color);
 /* HIDE through the panel's path: applied here, kept for the next start, nothing queued back for the panel */
 ui_action_count=0;
 assert(look_apply_locked(XZ_SHELL_ITEM_WAVE_TITLES,1));
 assert(!sh_indicator_shown(&shell,XZ_SHELL_IND_WAVE_TITLES)&&look_current(XZ_SHELL_ITEM_WAVE_TITLES)==1&&!ui_action_count);
 xz_shell_init(&shell);restore_display();
 assert(!sh_indicator_shown(&shell,XZ_SHELL_IND_WAVE_TITLES));
 /* MY SETTINGS sets the same choice back; the row follows */
 look_set_locked(LOOK_WAVE_TITLES,0,1003);look_set_locked(LOOK_WAVE_TITLES,2,1004);
 assert(sh_indicator_shown(&shell,XZ_SHELL_IND_WAVE_TITLES)&&look_current(XZ_SHELL_ITEM_WAVE_TITLES)==0&&!ui_action_count);
 /* WAVEFORM COLOURS is UTILITY's WAVEFORM COLOR: RGB applies in the shell (the panel sets its own twin itself) */
 assert(look_apply_locked(XZ_SHELL_SET_WAVE_COLOR,XZ_SHELL_WAVE_RGB));
 assert(shell.display.wave_color==XZ_SHELL_WAVE_RGB&&look_current(XZ_SHELL_SET_WAVE_COLOR)==XZ_SHELL_WAVE_RGB&&!ui_action_count);
 /* a pick past the choices is refused */
 assert(!look_apply_locked(XZ_SHELL_SET_WAVE_COLOR,7)&&!look_apply_locked(XZ_SHELL_ITEM_MODE_LAMPS,2)&&!look_apply_locked(-1,0));
 assert(shell.display.wave_color==XZ_SHELL_WAVE_RGB&&!ui_action_count);
 puts("PASS actual MODS look rows: shell values shown, picks applied in the shell and kept, MY SETTINGS shares them, WAVEFORM COLOURS is UTILITY's");
}
