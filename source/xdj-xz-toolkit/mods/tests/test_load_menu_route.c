/* The MODERN browser's LOAD menu through the real runtime: physical events in (the selector push and turn, the
   touch screen, each LOAD key as runtime.c / ui_runtime.c hand it over), and out the decks that load (native LOAD
   commands queued for the owner tick, DECK 3/4 inputs) plus what the stock player would see (the swallow verdict).
   One selection loads exactly the deck named on the chosen row, in every DECK VIEW, and nothing reaches stock. */
#ifdef NDEBUG
#error LOAD menu route tests require assertions
#endif
#include "../shell_runtime.c"
#include "../shell/shell_internal.h"
#include "../shell/browse_fixtures.h"
#include <assert.h>

static struct bx_library library;
static struct fx_track fixture_track;
static struct xz_shell_browse_model browse;
static struct { int count, deck[8]; uint32_t track[8]; } extra_in;
int xz_extra_deck_input(const struct xz_extra_deck_event *e){
 assert(e->kind==XZ_EXTRA_IN_LOAD&&extra_in.count<8);
 extra_in.deck[extra_in.count]=XZ_EXTRA_FIRST_DECK+e->deck;extra_in.track[extra_in.count]=e->load.track_id;extra_in.count++;
 return 1;
}
int xz_extra_order_matches(const struct xz_extra_order *o,int source,uint64_t generation,uint32_t row,uint32_t id){
 (void)o;(void)source;(void)generation;(void)row;(void)id;return 0;
}
int xz_native_four_mixer_order(void){return 0;}
unsigned xz_native_browse_mode(void){return 0;}
int xz_read_memory(uint32_t address,void *out,size_t length){(void)address;(void)out;(void)length;return -1;}
void xz_native_skin_request_repaint(void){}
uint32_t xz_extra_decks_epoch(void){return 1;}
void xz_extra_order_release(struct xz_extra_order *order){(void)order;}
void xz_log(const char *message){(void)message;}

static const int theme=XZ_THEME_FLAGSHIP;
/* Every deck that took a load since the last call, in the order they took it; -1 ends the list. */
struct loads { int n, deck[8]; uint32_t track[8]; };
static struct loads take_loads(void){
 struct loads l={0};struct xz_shrt_command c;
 while(xz_shrt_queue_pop(&queue,&c)){assert(c.kind==XZ_SHRT_CMD_LOAD&&l.n<8);l.deck[l.n]=c.deck;l.track[l.n]=c.track_id;l.n++;}
 for(int i=0;i<extra_in.count;i++){assert(l.n<8);l.deck[l.n]=extra_in.deck[i];l.track[l.n]=extra_in.track[i];l.n++;}
 extra_in.count=0;
 return l;
}
/* Four decks up, all loadable, the browser on a track list with the cursor on row 0. */
static void start(int mixer,int focus,int decks){
 memset(&model,0,sizeof model);memset(&queue,0,sizeof queue);extra_in.count=0;
 __atomic_store_n(&enabled,1,__ATOMIC_RELEASE);__atomic_store_n(&interface_modern,1,__ATOMIC_RELEASE);
 refreshed=1;memset(&handoff,0,sizeof handoff);memset(&contacts,0,sizeof contacts);memset(&touch_state,0,sizeof touch_state);
 memset(&push_state,0,sizeof push_state);
 fx_model_full(&model,&fixture_track);
 bx_tracks(&browse,&library,XZ_SHELL_BROWSE);browse.media_generation=7;browse.source_name="USB1";
 model.browse=&browse;model.deck_count=decks;model.wave_order=mixer;model.clock_ms=1000;
 model.capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_LOAD;
 for(int d=0;d<4;d++){model.deck[d].caps_off=0;model.deck[d].playing=1;}
 browse_source=0;usb[0].generation=5;media_published[0]=5;usb[0].lib=(struct xz_shlib *)&library;
 xz_shell_init(&shell);xz_shell_browse_reset();
 deliver_key_locked(XZ_SHELL_KEY_BROWSE,-1,1000);
 assert(xz_shell_screen(&shell)==XZ_SHELL_BROWSE);
 shell.stack[shell.depth-1].focus=1;shell.deck=focus;
 (void)take_loads();
}
static int push(void){
 int a=xz_shell_runtime_selector_push(1,theme,XZ_PUSH_DOWN),b=xz_shell_runtime_selector_push(1,theme,XZ_PUSH_UP);
 assert(a==b);return a;
}
static int load_key(int channel){
 int a=xz_shell_runtime_key(1,theme,0x430f,(unsigned)channel,0),b=xz_shell_runtime_key(1,theme,0x430f,(unsigned)channel,2);
 assert(a==b);return a;
}
static int touch_row(int row){
 struct xz_shell_rect r=sh_browse_load_choice_rect(row);
 int a=xz_shell_runtime_touch(1,theme,r.x+r.w/2,r.y+r.h/2,1,0),b=xz_shell_runtime_touch(1,theme,r.x+r.w/2,r.y+r.h/2,0,0);
 assert(b==XZ_SHELL_TOUCH_TAKEN);return a;
}
/* The deck the menu names on screen row i. */
static int named(int mixer,int decks,int row){
 static const int numeric[4]={0,1,2,3},order[4]={2,0,1,3};
 return decks==4&&mixer?order[row]:numeric[row];
}
/* Failures by kind, so a run before a fix says what is wrong and how often. */
static int bad_order,bad_load,stock_saw,checks,highlight;
static int one_load(struct loads l,int deck,uint32_t track){ return l.n==1&&l.deck[0]==deck&&l.track[0]==track; }
static void note_load(struct loads l,int deck,uint32_t track,const char *how,int decks,int mixer,int focus,int row){
 checks++;
 if(one_load(l,deck,track))return;
 if(bad_load<6){
  fprintf(stderr,"WRONG LOAD %s: %d decks, %s order, focus DECK %d, highlighted row %d names DECK %d; loaded:",how,decks,mixer?"mixer":"number",focus+1,row,highlight+1);
  for(int i=0;i<l.n;i++)fprintf(stderr," DECK %d",l.deck[i]+1);
  fprintf(stderr,l.n?"\n":" nothing\n");
 }
 bad_load++;
}
int main(void){
 (void)&bx_folders;bx_build(&library);fx_track_build(&fixture_track,1);
 for(int decks=2;decks<=4;decks+=2)for(int mixer=0;mixer<=1;mixer++)for(int focus=0;focus<decks;focus++){
  for(int row=0;row<decks;row++){
   uint32_t id=library.tracks[0].id;
   /* Encoder: push opens the menu with the cursor on the deck in focus, turn to the row, push loads the deck
      that row names. The rows follow the lanes (named()). */
   start(mixer,focus,decks);
   assert(push()==1&&sh_browse_load_pending(&shell));
   if(sh_browse_load_choice_deck(&shell,shell.load_choice.cursor)!=focus)bad_order++;
   if(sh_browse_load_choice_deck(&shell,row)!=named(mixer,decks,row))bad_order++;
   int shown=sh_browse_load_choice_deck(&shell,row);  /* what the row's label says */
   highlight=shown;
   int at=shell.load_choice.cursor;
   if(row!=at&&xz_shell_runtime_encoder_turn(1,theme,row-at)!=1)stock_saw++;
   assert(shell.load_choice.cursor==row);
   if(push()!=1)stock_saw++;
   note_load(take_loads(),shown,id,"encoder",decks,mixer,focus,row);
   /* Touch on the row. */
   start(mixer,focus,decks);
   if(push()!=1)stock_saw++;
   if(touch_row(row)!=XZ_SHELL_TOUCH_TAKEN)stock_saw++;
   note_load(take_loads(),shown,id,"touch",decks,mixer,focus,row);
   /* A LOAD key while the menu is open: the stock side keys (channel 1 = DECK 1 side, 2 = DECK 2 side) and the
      keys of a side switched to DECK 3/4 (runtime.c hands those to xz_shell_runtime_extra_load_key). The key
      confirms the highlighted row when that row names the key's own deck; otherwise it moves the highlight to
      its deck and loads nothing, and the next press loads it. No press loads a deck the highlight did not name. */
   for(int key=0;key<decks;key++){
    start(mixer,focus,decks);
    if(push()!=1)stock_saw++;
    int at2=shell.load_choice.cursor;
    if(row!=at2)xz_shell_runtime_encoder_turn(1,theme,row-at2);
    if((key<2?load_key(key+1):xz_shell_runtime_extra_load_key(1,theme,key-2))!=1)stock_saw++;
    struct loads l=take_loads();
    if(shown==key){note_load(l,key,id,"LOAD key",decks,mixer,focus,row);continue;}
    if(l.n){note_load(l,-1,0,"LOAD key off the highlight",decks,mixer,focus,row);continue;}
    checks++;
    if(!sh_browse_load_pending(&shell)||sh_browse_load_choice_deck(&shell,shell.load_choice.cursor)!=key)bad_order++;
    if((key<2?load_key(key+1):xz_shell_runtime_extra_load_key(1,theme,key-2))!=1)stock_saw++;
    note_load(take_loads(),key,id,"LOAD key twice",decks,mixer,focus,row);
   }
  }
 }
 /* LOAD LOCK on: a playing deck takes no track by any input and says why; a paused deck still loads. */
 int locked_refused=0,locked_loaded=0,unlocked_loaded=0,lock_cases=0;
 for(int mixer=0;mixer<=1;mixer++)for(int deck=0;deck<4;deck++)for(int input=0;input<4;input++)for(int playing=0;playing<=1;playing++){
  start(mixer,deck,4);model.settings.load_lock=1;for(int d=0;d<4;d++)model.deck[d].playing=d==deck?playing:1;
  uint32_t id=library.tracks[0].id;int row=-1;lock_cases++;
  if(input<3){
   if(push()!=1)stock_saw++;
   for(int r=0;r<4;r++)if(sh_browse_load_choice_deck(&shell,r)==deck)row=r;
   int at=shell.load_choice.cursor;if(row!=at)xz_shell_runtime_encoder_turn(1,theme,row-at);
   if(input==0&&push()!=1)stock_saw++;
   if(input==1&&touch_row(row)!=XZ_SHELL_TOUCH_TAKEN)stock_saw++;
   if(input==2&&(deck<2?load_key(deck+1):xz_shell_runtime_extra_load_key(1,theme,deck-2))!=1)stock_saw++;
  }else if((deck<2?load_key(deck+1):xz_shell_runtime_extra_load_key(1,theme,deck-2))!=1)stock_saw++; /* one-press LOAD from the list */
  struct loads l=take_loads();
  if(playing){
   if(l.n)locked_loaded++;
   else if(shell.notice&&!strcmp(shell.notice,sh_load_lock_notice(deck))&&!sh_browse_load_pending(&shell))locked_refused++;
  }else if(one_load(l,deck,id))unlocked_loaded++;
 }
 int lock_ok=locked_refused==lock_cases/2&&!locked_loaded&&unlocked_loaded==lock_cases/2;
 printf("%s LOAD LOCK: %d cases (4 decks, both orders, encoder, touch, LOAD key in the menu, one-press LOAD): "
        "playing decks refused with the notice %d/%d (loaded %d), paused decks loaded %d/%d\n",lock_ok?"PASS":"FAIL",
        lock_cases,locked_refused,lock_cases/2,locked_loaded,unlocked_loaded,lock_cases/2);
 if(!lock_ok)return 1;
 printf("%s LOAD menu through the runtime: %d selections (encoder, touch, every LOAD key; 2 and 4 decks; number and mixer "
        "order; every focus and row): %d loaded other than the highlighted row names, %d rows out of lane order, %d presses seen by stock\n",
        bad_load||bad_order||stock_saw?"FAIL":"PASS",checks,bad_load,bad_order,stock_saw);
 return bad_load||bad_order||stock_saw;
}
