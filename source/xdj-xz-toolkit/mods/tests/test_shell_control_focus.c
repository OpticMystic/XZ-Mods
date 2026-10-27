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
 /* SCREEN's TITLES IN WAVEFORMS row (ui/ui_menu.c) and MY SETTINGS expose the same shell-owned choice. */
 xz_shell_init(&shell);fill_look();
 int waveform=0;
 for(uint32_t i=0;i<wave_model.count;i++)if(wave_model.items[i].id==LOOK_WAVE_TITLES)waveform++;
 assert(waveform==1&&xz_shrt_shell_owned_setting(XZ_SHELL_ITEM_WAVE_TITLES));
 /* a published SCREEN page with the shell's look rows, as ui_menu.c builds them */
 static struct xz_shell_menu page;memset(&page,0,sizeof page);
 page.root=page.page=XZ_UI_SCREEN;page.row_count=2;page.choice_count=6;
 page.row[0]=(struct xz_shell_menu_row){.kind=XZ_SHELL_MENU_TOGGLE,.choice_count=2,.first_choice=0,.current=-1,.available=1,
  .target={XZ_UI_SHELL_LOOK,XZ_SHELL_ITEM_WAVE_TITLES,-1,XZ_UI_MENU_DIRECT,0}};
 page.row[1]=(struct xz_shell_menu_row){.kind=XZ_SHELL_MENU_CHOICE,.choice_count=4,.first_choice=2,.current=-1,.available=1,
  .target={XZ_UI_SHELL_LOOK,XZ_SHELL_SET_WAVE_COLOR,-1,XZ_UI_MENU_DIRECT,0}};
 for(int i=0;i<2;i++)page.choice[i].target=(struct xz_shell_menu_target){XZ_UI_SHELL_LOOK,XZ_SHELL_ITEM_WAVE_TITLES,-1,XZ_UI_MENU_DIRECT,(int16_t)i};
 for(int i=0;i<4;i++)page.choice[2+i].target=(struct xz_shell_menu_target){XZ_UI_SHELL_LOOK,XZ_SHELL_SET_WAVE_COLOR,-1,XZ_UI_MENU_DIRECT,(int16_t)i};
 menu_raw=page;menu_page=page;menu_valid=1;look_patch_locked();
 /* the rows show the shell's own values, not the panel's */
 assert(menu_page.row[0].current==0&&menu_page.row[1].current==(int)shell.display.wave_color);
 /* HIDE through the MENU command path: applied here, kept for the next start, nothing queued for the panel */
 ui_action_count=0;menu_count=0;
 menu_command_locked(&(struct xz_shell_action){.kind=XZ_SHELL_ACT_MOD_MENU,.deck=1,.value=0,.id=XZ_SHELL_MENU_ACTIVATE},1002);
 assert(!sh_indicator_shown(&shell,XZ_SHELL_IND_WAVE_TITLES)&&menu_page.row[0].current==1&&!ui_action_count&&!menu_count);
 xz_shell_init(&shell);restore_display();
 assert(!sh_indicator_shown(&shell,XZ_SHELL_IND_WAVE_TITLES));
 /* MY SETTINGS sets the same choice back; the row follows */
 look_set_locked(LOOK_WAVE_TITLES,0,1003);look_set_locked(LOOK_WAVE_TITLES,2,1004);
 look_patch_locked();
 assert(sh_indicator_shown(&shell,XZ_SHELL_IND_WAVE_TITLES)&&menu_page.row[0].current==0&&!ui_action_count);
 /* WAVEFORM COLOURS is UTILITY's WAVEFORM COLOR: RGB applies in the shell and tells the panel STOCK */
 menu_command_locked(&(struct xz_shell_action){.kind=XZ_SHELL_ACT_MOD_MENU,.deck=1,.value=1,.id=XZ_SHELL_MENU_ACTIVATE},1005);
 assert(shell.display.wave_color==XZ_SHELL_WAVE_RGB&&menu_page.row[1].current==XZ_SHELL_WAVE_RGB&&!menu_count);
 assert(ui_action_count==1&&ui_actions[0].kind==XZ_UI_WAVE_MODE&&ui_actions[0].index==0);
 /* a pick past the choices is refused; a row-level push on a look row is refused */
 ui_action_count=0;
 menu_command_locked(&(struct xz_shell_action){.kind=XZ_SHELL_ACT_MOD_MENU,.deck=7,.value=1,.id=XZ_SHELL_MENU_ACTIVATE},1006);
 menu_command_locked(&(struct xz_shell_action){.kind=XZ_SHELL_ACT_MOD_MENU,.deck=-1,.value=0,.id=XZ_SHELL_MENU_ACTIVATE},1007);
 assert(shell.display.wave_color==XZ_SHELL_WAVE_RGB&&!ui_action_count&&!menu_count);
 puts("PASS actual SCREEN look rows: shell values shown, MENU picks applied in the shell and kept, MY SETTINGS shares them, WAVEFORM COLOURS is UTILITY's");
}
