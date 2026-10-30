/* Portable acceptance for the production settings parser and jump handler. */
#include "settings.h"
#include "ui/stem_pads.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
 struct xz_settings old,loaded,current;char text[1024];unsigned present;
 xz_settings_default(&old);old.jump.enabled=0;
 old.jump.shift_page=old.ram_debug=old.metrics_enabled=old.shift_pages=old.shift_keysync=0;
 assert(xz_settings_format(&old,text,sizeof(text))>0);
 assert(strstr(text,"shift_pages=0\n")&&strstr(text,"shift_keysync=0\n"));
 assert(strstr(text,"jump_shift_page=0\n")&&strstr(text,"ram_debug=0\n")&&strstr(text,"metrics_enabled=0\n"));
 assert(!xz_settings_parse_present(text,&loaded,&present)&&present==58); /* every field of the current record (58 with controller_locale) */
 assert(loaded.shift_pages&&loaded.shift_keysync&&loaded.jump.shift_page&&loaded.ram_debug&&loaded.metrics_enabled&&!loaded.jump.enabled);
 current=old;assert(!xz_settings_overlay(&current,&old,43));
 assert(current.shift_pages&&current.shift_keysync&&current.jump.shift_page&&current.ram_debug&&current.metrics_enabled&&!current.jump.enabled);
 /* Partial older records cannot disable an additive feature through overlay. */
 for(unsigned n=0;n<=43;n++){
  current=old;assert(!xz_settings_overlay(&current,&old,n));
  assert(current.shift_pages&&current.shift_keysync&&current.jump.shift_page&&current.ram_debug&&current.metrics_enabled);
 }
 /* Every shifted mode-button action ignores the retired Off flag. Runtime
    reserves mode 3 for page 2; direct handler coverage retains its mapping. */
 for(int deck=0;deck<2;deck++)for(int mode=0;mode<4;mode++){
  struct xz_stem_pads pads={0};int toggle=-1;
  struct xz_cue_event e={.deck=deck,.pad=-1,.mode_button=mode,.shift=1};
  assert(xz_stem_control_event(&pads,&e,1,0,0,&toggle));
  assert(toggle==(mode<3?2-mode:XZ_STEM_PAD_BYPASS));
  e.operation=2;e.shift=0;assert(xz_stem_control_event(&pads,&e,0,0,0,&toggle));
 }
 for(int deck=0;deck<2;deck++){
  struct xz_jump_state state={0};int beats=0;
  struct xz_cue_event e={.deck=deck,.pad=-1,.pad_page=3,.mode_button=3,.shift=1};
  assert(!xz_jump_event(&state,&old.jump,&e,&beats)&&state.page[deck]==1);
  e.operation=2;assert(!xz_jump_event(&state,&old.jump,&e,&beats));
  e=(struct xz_cue_event){.deck=deck,.pad=7,.pad_page=3,.mode_button=-1};
  assert(xz_jump_event(&state,&old.jump,&e,&beats)&&beats==128);
  e.operation=2;assert(xz_jump_event(&state,&old.jump,&e,&beats));
  e=(struct xz_cue_event){.deck=deck,.pad=-1,.pad_page=3,.mode_button=3};
  assert(!xz_jump_event(&state,&old.jump,&e,&beats)&&state.page[deck]==0);
  e=(struct xz_cue_event){.deck=deck,.pad=0,.pad_page=3,.mode_button=-1};
  assert(!xz_jump_event(&state,&old.jump,&e,&beats)&&!beats);
 }
 puts("PASS retired Off values migrate, partial overlays normalize, page 2 works with both old switches Off, page 1 remains stock");
}
