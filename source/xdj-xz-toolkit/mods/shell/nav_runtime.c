/* Host-only adapter around the existing Linux runtime test fixture. Its main is never run.
 * No runtime start, writer thread, network client, or hardware hook is started. */
#define main unused_runtime_test_main
#include "../ui/test_runtime_controls.c"
#undef main
void xz_layered_wave_configure(int mode,const uint32_t rgb[3]){(void)mode;(void)rgb;}
static int nav_master_tempo(void *player,int on){(void)player;(void)on;return 0;}
void nav_runtime_init(void){
 native_master_tempo_on=nav_master_tempo;
 started=audio_available=requested_stems=key_available=1;
 fixture_loaded=3;model.enabled=XZ_UI_STEM;model.jump=xz_jump_defaults();
 for(int d=0;d<2;d++){
  fixture_key_valid[d]=key_valid[d]=1;
  fixture_keys[d]=key_snapshot[d]=(struct xz_key_snapshot){.loaded=1,.epoch=1,.original={d?0:9,1},.tempo_ratio=1,.master_tempo=1,.master=d==1};
 }
}
int nav_runtime_event(struct xz_ui_model *out,int code,int side,int release,int shift,int page){
 int previous_jumps=jump_calls;model=*out;
 if(code==30&&!release)rekordbox_run_locked(XZ_REKORDBOX_INTERFACE);
 else {
  struct xz_cue_event e={.deck=side,.pad=-1,.mode_button=-1,.operation=release?2:0,.shift=shift,.pad_page=page};
  if(code==31)e.sync=1;
  else if(code>=1&&code<=4)e.mode_button=code-1;
  else if(code>=10&&code<=17)e.pad=code-10;
  unsigned flags;xz_ui_runtime_pad(&e,&flags);
 }
 if(code==31&&!release&&model.deck[side].key_synced){fixture_key_apply(side);model.deck[side].key_semitones=fixture_key_cents[side]/100;xz_key_readout(&fixture_keys[side],fixture_key_applied[side],model.deck[side].key_readout,sizeof model.deck[side].key_readout);}
 *out=model;
 if(code==31&&!release&&!model.deck[side].key_synced)exit(10);
 return jump_calls!=previous_jumps?jump_beats:0;
}

static int rows_players=1;
int xz_read_memory(uint32_t address,void *out,size_t n){
    uint32_t value=0;
    if(!rows_players)return -1;
    if(n==4){
        if(address==0x1c8a308)value=0x200000;else if(address==0x200040)value=0x210000;else if(address==0x200048)value=2;
        else if(address==0x210000)value=0x220000;else if(address==0x210004)value=0x221000;
        else if(address==0x220138)value=0x230000;else if(address==0x221138)value=0x231000;else return -1;
        memcpy(out,&value,4);return 0;
    }
    if(n==1&&(address==0x230026||address==0x231026)){*(unsigned char *)out=address==0x230026?1:2;return 0;}
    return -1;
}


void nav_runtime_apply(struct xz_ui_model *out,struct xz_ui *state,struct xz_ui_action *action){
 model=*out;ui=*state;
 switch(action->kind){
 case XZ_UI_STEM_ORDER:case XZ_UI_STEM_COLOR:case XZ_UI_STEM_COLORS_RESET:
 case XZ_UI_BAND_COLOR:case XZ_UI_BAND_COLORS_RESET:case XZ_UI_PAD_FEEDBACK:
 case XZ_UI_KEY_FORMAT:case XZ_UI_KEY_FEEDBACK:case XZ_UI_BROWSE_TRAFFIC:case XZ_UI_BROWSE_KEY_PREFIX:
 case XZ_UI_REKORDBOX_ASSIGN:case XZ_UI_JUMP_SIZE:case XZ_UI_STEM_BANK:case XZ_UI_STEM_PAGE:
 case XZ_UI_SHELL_DISPLAY:case XZ_UI_SHELL_BROWSE_COLUMNS:case XZ_UI_SET_MODERN_THEME:case XZ_UI_WAVE_MODE:case XZ_UI_BROWSE_PREVIEW:case XZ_UI_STEMS_OVERLAY:
 case XZ_UI_ENABLE:case XZ_UI_SET_THEME:case XZ_UI_SET_INTERFACE:case XZ_UI_DECK_VIEW:
 apply(NULL,action,1);break;
 default:break;
 }
 *out=model;
}
