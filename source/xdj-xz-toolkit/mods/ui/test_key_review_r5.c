/* Independent key-only boundary cases. All actions use frozen owning C. */
#define XZ_PLAY_KEY_RUNTIME_NO_MAIN
#include "test_play_key_runtime_r1.c"
static uint16_t screen[800*480],saved[800*480];
static int paint(void){
 if(!model.ui_interface)stock_key_fixture(screen);
 return xz_mods_render_v1(screen,800,480,800,NULL);
}

static void native_entry(int interface,int deck){
 reset(interface);key_model.deck_count=2;key_shell.play_decks=2;
 assert(paint());
 if(interface){struct xz_shell_model v;xz_shell_play_projection(&key_shell,&key_model,&v);struct xz_shell_rect r=xz_shell_play_rect(&v,XZ_SHELL_R_KEY,deck);tap_xy(r.x+8,r.y+8);}
 else tap_xy(20,130+143*deck);
 assert(play_key.open&&play_key.deck==deck);assert(paint());
}

static int missing_extra(void){
 int failures=0;
 for(int interface=0;interface<2;interface++)for(int deck=0;deck<2;deck++){
  reset(interface);fixture_extra_ok=0;key_model.deck_count=2;key_shell.play_decks=2;assert(paint());
  int before=fixture_key_requests;
  if(interface){struct xz_shell_model v;xz_shell_play_projection(&key_shell,&key_model,&v);struct xz_shell_rect r=xz_shell_play_rect(&v,XZ_SHELL_R_KEY,deck);tap_xy(r.x+8,r.y+8);}
  else tap_xy(20,130+143*deck);
  int opened=play_key.open;
  if(opened){assert(paint());press_button(2);}
  int sent=fixture_key_requests-before;
  printf("RESULT missing-extra interface=%s native_deck=%d snapshot=0 ready=%u popup_open=%d native_requests=%d expected=1,1\n",interface?"MODERN":"CLASSIC",deck+1,model.deck[deck].ready,opened,sent);
  failures+=!opened||sent!=1;
 }
 return failures?1:0;
}

static int setup_publication(void){
 int failures=0;
 for(int interface=0;interface<2;interface++){
  native_entry(interface,0);fixture_key_valid[0]=fixture_key_valid[1]=0;model.deck[0].ready=0;assert(paint());
  press_button(4);assert(play_key.setup&&play_key.step==0);assert(paint());
  memcpy(saved,screen,sizeof screen);
  press_button(4);assert(play_key.step==1);
  uint64_t revision=play_key.revision;
  assert(play_key.painted_revision!=revision);
  unsigned checks=fixture_key_snapshot_calls[0];
  /* The displayed button still says DETECT. USE THIS DECK has never painted. */
  press_button(4);
  printf("RESULT setup-publication interface=%s painted_step=0 first_next_step=1 repeated_unpainted_step=%d revision_before=%llu revision_after=%llu screen_unchanged=%d unseen_key_check_delta=%u expected_step=1\n",interface?"MODERN":"CLASSIC",play_key.step,(unsigned long long)revision,(unsigned long long)play_key.revision,!memcmp(saved,screen,sizeof screen),fixture_key_snapshot_calls[0]-checks);
  failures+=play_key.step!=1;
 }
 return failures?1:0;
}

static int contact_boundaries(void){
 /* A DOWN rejected before paint cannot revive via MOVE or a later UP. */
 for(int interface=0;interface<2;interface++){
  reset(interface);key_shell.play_decks=2;assert(paint());
  if(interface){struct xz_shell_model v;xz_shell_play_projection(&key_shell,&key_model,&v);struct xz_shell_rect r=xz_shell_play_rect(&v,XZ_SHELL_R_KEY,0);tap_xy(r.x+8,r.y+8);}
  else tap_xy(20,130);
  int before=fixture_key_requests;struct xz_ui_widget plus=xz_play_key_button(2);
  contact(solver,plus.x+8,plus.y+8,1);assert(paint());contact(solver,plus.x+9,plus.y+9,1);contact(solver,0,0,0);
  assert(fixture_key_requests==before&&!stock_calls);press_button(2);assert(fixture_key_requests==before+1);
 }
 /* A semantic projection change during a held key press cancels through UP. */
 native_entry(1,0);struct xz_ui_widget plus=xz_play_key_button(2);int before=fixture_key_requests;
 contact(solver,plus.x+8,plus.y+8,1);key_model.deck_count=4;key_shell.play_decks=4;assert(paint());
 assert(!play_key.open);contact(solver,0,0,0);assert(!play_key.down&&!play_key_raw_down&&!stock_calls&&fixture_key_requests==before);
 /* Existing CLASSIC wave and stem contacts keep release ownership. */
 int cases=0;
 for(int layout=1;layout<XZ_STEM_LAYOUT_COUNT;layout++)for(int deck=0;deck<2;deck++)for(int wave=0;wave<2;wave++){
  reset(0);inline_contact=inline_wave_contact=four_contact=0;audio_available=requested_stems=1;model.enabled=XZ_UI_STEM;model.stems_overlay=layout;
  assert(paint());struct xz_wave_layout g;assert(xz_wave_layout_get(layout,&g));
  int y=18+(wave?g.lane_y[deck]+10:g.control_y[deck]+8);
  contact(solver,200,y,1);assert(wave?inline_wave_contact:inline_contact);
  contact(solver,20,130+143*deck,1);assert(!play_key.open);
  contact(solver,0,0,0);assert(!inline_contact&&!inline_wave_contact&&!inline_ui.down&&!((unsigned char *)solver)[4]);cases++;
 }
 /* Four-lane native and software waveform contacts have the same invariant. */
 for(int order=0;order<2;order++)for(int deck=0;deck<4;deck++){
  reset(0);inline_contact=inline_wave_contact=four_contact=0;native_four=order;assert(paint());
  struct xz_four_layout g;assert(xz_four_layout_get(order,&g));
  contact(solver,200,18+g.lane_y[deck]+10,1);assert(four_contact);
  contact(solver,20,130,1);assert(!play_key.open);contact(solver,0,0,0);
  assert(!four_contact&&!inline_wave_contact&&!((unsigned char *)solver)[4]);cases++;
 }
 printf("PASS contact boundaries: native DOWN before paint, no MOVE/UP revival, held popup projection cancellation, %d CLASSIC owner/release cases\n",cases);
 return 0;
}

static int semantic_publication(void){
 reset(1);key_shell.play_decks=4;assert(paint());
 uint32_t original=xz_shell_play_key_frame(&key_shell,&key_model);assert(original);
 struct xz_shell_model v;xz_shell_play_projection(&key_shell,&key_model,&v);struct xz_shell_rect r=xz_shell_play_rect(&v,XZ_SHELL_R_KEY,0);
 key_shell.play_decks=2;assert(!xz_shell_play_key_frame(&key_shell,&key_model));
 key_shell.play_decks=4;assert(xz_shell_play_key_frame(&key_shell,&key_model)==original);
 assert(xz_shell_play_key_deck(&key_shell,&key_model,r.x,r.y)==0);
 assert(xz_shell_play_key_deck(&key_shell,&key_model,r.x+r.w-1,r.y+r.h-1)==0);
 assert(xz_shell_play_key_deck(&key_shell,&key_model,r.x+r.w,r.y)==-1);
 assert(xz_shell_play_key_deck(&key_shell,&key_model,r.x,r.y+r.h)==-1);
 struct xz_shell_key_frame recorded=key_shell.key_frame;
 struct xz_shell copy=key_shell;copy.play_decks=2;assert(xz_shell_render_frame(&copy,&key_model,screen,800));
 assert(!memcmp(&recorded,&key_shell.key_frame,sizeof recorded));
 key_model.wave_order=1;assert(!xz_shell_play_key_frame(&key_shell,&key_model));
 assert(!xz_shell_render_frame(&key_shell,&key_model,NULL,800));assert(xz_shell_play_key_deck(&key_shell,&key_model,r.x+1,r.y+1)==-2);
 puts("PASS semantic publication: still-displayed A/B/A, rectangle edge bounds, independent value copy, failed render retains stale rejection");return 0;
}

int main(int argc,char **argv){
 (void)actions;
 (void)render_classic_key_fixture;
 if(argc!=2)return 2;
 if(!strcmp(argv[1],"missing-extra"))return missing_extra();
 if(!strcmp(argv[1],"setup-publication"))return setup_publication();
 if(!strcmp(argv[1],"contact-boundaries"))return contact_boundaries();
 if(!strcmp(argv[1],"semantic-publication"))return semantic_publication();
 return 2;
}
