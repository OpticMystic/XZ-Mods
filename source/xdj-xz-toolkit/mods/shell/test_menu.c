#ifdef NDEBUG
#error Shell acceptance requires active assertions
#endif
/* The MODS screen's MENU tabs over the MODS panel's real pages (ui/ui_menu.c): one tab per MODS section,
   the encoder never leaves MODS, BACK goes up exactly one level, every panel page is reachable, every row
   runs its own control, and every page draws inside the frame. A small bridge does what shell_runtime.c and
   ui_runtime.c do: it maps each MOD_MENU action to its target in the page the shell showed, runs it on the
   panel state (the shell's own look rows on a store of their own), publishes the page that results and
   moves the rail to that page's section. */
#include "fixtures.h"
#include "settings_fixtures.h"
#include "menu_fixtures.h"
#include "../ui/play_key.h"
#include "shell_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define STRIDE 804
#define GUARD 64
#define PIXELS (STRIDE*(XZ_SHELL_HEIGHT+2))
static uint16_t buffer[GUARD+PIXELS+GUARD];
static struct fx_track track;
static struct xz_ui ui;
static struct xz_ui_model um;
static struct xz_shell_menu menu;
static struct xz_shell_model sm;
static struct xz_shell_action out[XZ_SHELL_ACTIONS];
/* What the bridge saw: every shell action kind, and the panel actions the rows ran. */
static int others, refusals;
static struct xz_ui_action ran[64];
static size_t ran_count;

static void render_check(const struct xz_shell *s) {
 for(size_t i=0;i<GUARD+PIXELS+GUARD;i++)buffer[i]=0xa55a;
 assert(xz_shell_render(s,&sm,buffer+GUARD+STRIDE,STRIDE));
 for(size_t i=0;i<GUARD+STRIDE;i++)assert(buffer[i]==0xa55a);
 for(size_t i=GUARD+STRIDE+(size_t)STRIDE*XZ_SHELL_HEIGHT;i<GUARD+PIXELS+GUARD;i++)assert(buffer[i]==0xa55a);
 for(int y=0;y<XZ_SHELL_HEIGHT;y++)for(int x=XZ_SHELL_WIDTH;x<STRIDE;x++)assert(buffer[GUARD+STRIDE+y*STRIDE+x]==0xa55a);
}
static int look_store[XZ_SHELL_ITEM_ID_END+1];   /* the shell's own look settings, as shell_runtime.c keeps them */
static int look_sets;
static void publish(void) {
 assert(xz_ui_menu_build(&ui,&um,&menu));
 for(int i=0;i<menu.row_count;i++)if(menu.row[i].target.kind==XZ_UI_SHELL_LOOK){
  int item=menu.row[i].target.index;assert(item>=0&&item<=XZ_SHELL_ITEM_ID_END);
  menu.row[i].current=(int16_t)look_store[item];
 }
}
/* The runtime's own navigations after an action (ui_runtime.c apply()), for reachability. */
static void runtime_navigation(const struct xz_ui_action *a) {
 switch(a->kind){
 case XZ_UI_LOCAL_STEM_SETUP_OPEN:xz_ui_navigate(&ui,XZ_UI_LOCAL_STEM_SETUP);break;
 case XZ_UI_METRICS_SETUP_OPEN:xz_ui_navigate(&ui,XZ_UI_METRICS_SETUP);break;
 case XZ_UI_GAME_SELECT:xz_ui_navigate(&ui,a->index?XZ_UI_DOOM:XZ_UI_DOOM_FILES);break;
 default:break;
 }
}
static void run_target(const struct xz_shell_menu_target *t) {
 if(t->kind==XZ_UI_SHELL_LOOK){look_store[t->index]=t->value;look_sets++;return;}
 struct xz_ui_action a[XZ_UI_MENU_ACTIONS];size_t n=xz_ui_menu_run(&ui,&um,t,a);
 for(size_t i=0;i<n;i++){if(ran_count<64)ran[ran_count++]=a[i];runtime_navigation(&a[i]);}
}
static void bridge_action(const struct xz_shell_action *a) {
 /* nothing in MODS opens another menu or hands off */
 if(a->kind==XZ_SHELL_ACT_SET_MOD)return;
 if(a->kind!=XZ_SHELL_ACT_MOD_MENU){others++;fprintf(stderr,"other action kind %d id %d value %d page %d\n",(int)a->kind,a->id,(int)a->value,(int)ui.page);return;}
 struct xz_ui_action pad[XZ_UI_ACTIONS];size_t n;
 switch(a->id){
 case XZ_SHELL_MENU_OPEN:
  assert(xz_ui_page_is_top(a->value));
  xz_ui_menu_open(&ui,(enum xz_ui_page)a->value);break;
 case XZ_SHELL_MENU_BACK:
  n=xz_ui_menu_back(&ui,pad);
  for(size_t i=0;i<n;i++)assert(pad[i].kind!=XZ_UI_CLOSE);
  break;
 case XZ_SHELL_MENU_ACTIVATE:{
  if(a->value<0||a->value>=menu.row_count){refusals++;return;}
  const struct xz_shell_menu_row *r=&menu.row[a->value];
  if(a->deck>=r->choice_count){refusals++;return;}
  run_target(a->deck>=0?&menu.choice[r->first_choice+a->deck].target:&r->target);
  break;}
 case XZ_SHELL_MENU_TAP:{
  if(a->value<0||a->value>=menu.row_count||menu.row[a->value].kind!=XZ_SHELL_MENU_PART){refusals++;return;}
  struct xz_shell_menu_target t=menu.row[a->value].target;t.how=XZ_UI_MENU_TOUCH;run_target(&t);
  break;}
 case XZ_SHELL_MENU_TURN:
  if(!menu.adjusting||!a->value){refusals++;return;}
  n=xz_ui_menu_turn(&ui,&um,(int)a->value,pad);
  for(size_t i=0;i<n&&ran_count<64;i++)ran[ran_count++]=pad[i];
  break;
 default:{
  const struct xz_shell_menu_row *r=&menu.row[a->value];
  n=xz_ui_menu_pad(&ui,&um,&r->target,a->deck>>10,a->deck&1023,a->id!=XZ_SHELL_MENU_RELEASE,pad);
  for(size_t i=0;i<n&&ran_count<64;i++)ran[ran_count++]=pad[i];
  break;}
 }
}
static size_t send(struct xz_shell *s, struct xz_shell_event e) {
 memset(out,0,sizeof out);
 int root=menu.root;
 size_t n=xz_shell_input(s,&sm,&e,out);
 struct xz_shell_action copy[XZ_SHELL_ACTIONS];memcpy(copy,out,sizeof copy);
 int opened=0;
 for(size_t i=0;i<n;i++){bridge_action(&copy[i]);opened|=copy[i].kind==XZ_SHELL_ACT_MOD_MENU&&copy[i].id==XZ_SHELL_MENU_OPEN;}
 publish();
 /* the rail follows a link into another section, as ui_runtime.c asks the shell to */
 if(!opened&&menu.root!=root&&xz_shell_screen(s)==XZ_SHELL_UTILITY)xz_shell_mods_show(s,&sm,mfx_tab_of(menu.root));
 return n;
}
static size_t turn(struct xz_shell *s, int d) { return send(s,(struct xz_shell_event){XZ_SHELL_ENCODER_TURN,0,0,d,0,-1}); }
static size_t push(struct xz_shell *s) { return send(s,(struct xz_shell_event){XZ_SHELL_ENCODER_PUSH,0,0,0,0,-1}); }
static size_t hold(struct xz_shell *s) { return send(s,(struct xz_shell_event){XZ_SHELL_ENCODER_HOLD,0,0,0,0,-1}); }
static size_t key(struct xz_shell *s, enum xz_shell_key k) { return send(s,(struct xz_shell_event){XZ_SHELL_KEY,0,0,0,k,-1}); }
/* A touch and its release; out holds what both asked for. */
static size_t tap(struct xz_shell *s, int x, int y) {
 struct xz_shell_action both[XZ_SHELL_ACTIONS*2];
 size_t n=send(s,(struct xz_shell_event){XZ_SHELL_TOUCH_DOWN,x,y,0,0,-1});memcpy(both,out,sizeof out);
 size_t k=send(s,(struct xz_shell_event){XZ_SHELL_TOUCH_UP,x,y,0,0,-1});memcpy(both+n,out,k*sizeof *out);
 memcpy(out,both,sizeof out);
 return n+k;
}
static void fresh(struct xz_shell *s) {
 mfx_model(&um);xz_ui_init(&ui);publish();
 xz_shell_init(s);others=refusals=0;ran_count=0;
}
static int in_mods(const struct xz_shell *s) { return xz_shell_screen(s)==XZ_SHELL_UTILITY&&s->depth==2; }
static const struct xz_shell_menu_row *cursor_row(const struct xz_shell *s) {
 int r=xz_shell_menu_cursor(s,&sm);
 return r>=0?&menu.row[r]:NULL;
}
static int pane(const struct xz_shell *s) { return s->stack[s->depth-1].focus&3; }
/* The cursor on the row whose label starts with label; pushes nothing. */
static void cursor_to(struct xz_shell *s, const char *label) {
 for(int i=0;i<=menu.row_count;i++)turn(s,-8);
 for(int i=0;i<=menu.row_count;i++){
  const struct xz_shell_menu_row *r=cursor_row(s);
  if(r&&!strncmp(r->label,label,strlen(label)))return;
  turn(s,1);
 }
 fprintf(stderr,"no row %s on %s\n",label,menu.title);assert(0);
}

/* MENU, the MODS key and the header button open MODS on HOME and ask for its page; the rail asks for each
   section's page as it is reached. */
static void test_open(void) {
 struct xz_shell s;fresh(&s);
 xz_ui_navigate(&ui,XZ_UI_DOOM_FILES);   /* wherever the panel was */
 assert(key(&s,XZ_SHELL_KEY_MENU)==1&&out[0].kind==XZ_SHELL_ACT_MOD_MENU&&out[0].id==XZ_SHELL_MENU_OPEN&&out[0].value==XZ_UI_HOME);
 assert(in_mods(&s)&&s.mods_view.tab==1&&s.mods_view.on_tabs&&ui.page==XZ_UI_HOME&&!ui.history_count);
 assert(menu.root==XZ_UI_HOME&&menu.depth==0&&menu.layout==XZ_SHELL_MENU_HOME);
 render_check(&s);
 static const int expect[8]={-1,XZ_UI_HOME,XZ_UI_PLAY,XZ_UI_STEM_SECTION,XZ_UI_SCREEN,XZ_UI_FOUR_DECKS,XZ_UI_CONNECT,XZ_UI_EXTRAS};
 for(int t=2;t<=7;t++){
  size_t n=turn(&s,1);
  assert(s.mods_view.tab==t&&s.mods_view.on_tabs);
  assert(n==1&&out[0].value==expect[t]&&(int)ui.page==expect[t]);
  render_check(&s);
 }
 /* the seven sections fit the rail without scrolling, under UTILITY */
 for(int t=0;t<=7;t++){
  struct xz_shell_rect r=xz_shell_mods_tab_rect(&s,&sm,t);
  assert(r.w==88&&r.h>=44&&r.y+r.h<=422);
  if(t>=1)assert(r.y==96+46*(t-1));
 }
 assert(key(&s,XZ_SHELL_KEY_MENU)==0&&s.depth==1);
 assert(key(&s,XZ_SHELL_KEY_MODS)==1&&out[0].id==XZ_SHELL_MENU_OPEN&&in_mods(&s));
 assert(key(&s,XZ_SHELL_KEY_MODS)==0&&s.depth==1);
 assert(xz_shell_render_frame(&s,&sm,buffer+GUARD+STRIDE,STRIDE));
 assert(tap(&s,XZ_SHELL_MODS_X+20,10)==1&&out[0].id==XZ_SHELL_MENU_OPEN&&in_mods(&s));
 /* held MENU lands on UTILITY, which asks nothing */
 assert(key(&s,XZ_SHELL_KEY_MENU_HOLD)==0&&s.mods_view.tab==0);
 assert(!others&&!refusals);
 puts("PASS MENU tabs: MENU, the MODS key, the header and the rail ask for each of the seven sections; they fit the rail under UTILITY");
}

/* The owner's report: scrolling and clicking with the encoder must never leave MODS. Long seeded runs of
   turns, pushes and holds over every tab, every page they lead to, and every value pane. */
static void test_encoder_never_leaves(void) {
 uint32_t seed=0x2026u;
 int pages_seen[XZ_UI_PAGE_COUNT]={0};
 for(int run=0;run<24;run++){
  struct xz_shell s;fresh(&s);
  key(&s,XZ_SHELL_KEY_MENU);
  turn(&s,run%7);push(&s);   /* into the tab: BACK leaves its page or closes MODS */
  for(int step=0;step<1500;step++){
   seed=seed*1664525u+1013904223u;
   int r=(int)((seed>>16)%10);
   if(r<5)turn(&s,(int)((seed>>8)%7)-3);
   else if(r<9)push(&s);
   else hold(&s);
   /* a running game takes the screen in the runtime and gives it back to this page: nothing changes here */
   assert(in_mods(&s));
   assert(!others);
   pages_seen[ui.page]++;
   if(step%97==0)render_check(&s);
  }
 }
 int distinct=0;for(int p=0;p<XZ_UI_PAGE_COUNT;p++)distinct+=pages_seen[p]>0;
 assert(distinct>=12);
 assert(!refusals);
 printf("PASS encoder: 36000 seeded turns, pushes and holds over %d pages never leave MODS and ask only for MODS menu actions\n",distinct);
}

/* BACK: the values pane, then one page up onto its link, then top-level MODS closes. */
static void test_back(void) {
 struct xz_shell s;fresh(&s);
 key(&s,XZ_SHELL_KEY_MENU);turn(&s,2);   /* STEMS */
 assert(push(&s)==0&&!s.mods_view.on_tabs&&pane(&s)==1);
 cursor_to(&s,"STEMS IN DJ SOFTWARE");
 assert(push(&s)==1&&out[0].id==XZ_SHELL_MENU_ACTIVATE);
 assert(ui.page==XZ_UI_PC_STEMS&&menu.depth==1&&menu.root==XZ_UI_STEM_SECTION&&!strcmp(menu.path,"STEMS"));
 /* the new page starts on its first control: its setup step */
 assert(cursor_row(&s)&&!strcmp(cursor_row(&s)->label,"DETECT PC"));
 cursor_to(&s,"DECK TO SET UP");
 assert(push(&s)==1);   /* two decks: one push flips */
 assert(ui.deck==1);
 cursor_to(&s,"DETECT PC");
 assert(key(&s,XZ_SHELL_KEY_BACK)==1&&out[0].id==XZ_SHELL_MENU_BACK&&ui.page==XZ_UI_STEM_SECTION&&in_mods(&s));
 assert(cursor_row(&s)&&!strcmp(cursor_row(&s)->label,"STEMS IN DJ SOFTWARE")&&pane(&s)==1);
 /* BACK then push re-enters the page just left */
 assert(push(&s)==1&&ui.page==XZ_UI_PC_STEMS);
 key(&s,XZ_SHELL_KEY_BACK);
 assert(key(&s,XZ_SHELL_KEY_BACK)==0&&s.depth==1&&xz_shell_screen(&s)==XZ_SHELL_PLAY);
 /* values pane */
 key(&s,XZ_SHELL_KEY_MENU);turn(&s,3);push(&s);   /* SCREEN */
 cursor_to(&s,"KEY NOTATION");
 assert(push(&s)==0&&pane(&s)==2);
 assert(key(&s,XZ_SHELL_KEY_BACK)==0&&pane(&s)==1&&!s.mods_view.on_tabs&&ui.page==XZ_UI_SCREEN);
 /* three deep: EXTRAS / GAMES / DOOM, back one level at a time */
 key(&s,XZ_SHELL_KEY_BACK);key(&s,XZ_SHELL_KEY_MENU);turn(&s,6);assert(ui.page==XZ_UI_EXTRAS);push(&s);
 cursor_to(&s,"GAMES");push(&s);assert(ui.page==XZ_UI_GAMES&&menu.depth==1);
 cursor_to(&s,"CHEX QUEST");push(&s);assert(ui.page==XZ_UI_DOOM&&menu.depth==2&&!strcmp(menu.path,"EXTRAS / GAMES"));
 key(&s,XZ_SHELL_KEY_BACK);assert(ui.page==XZ_UI_GAMES&&cursor_row(&s)&&!strcmp(cursor_row(&s)->label,"CHEX QUEST"));
 key(&s,XZ_SHELL_KEY_BACK);assert(ui.page==XZ_UI_EXTRAS&&cursor_row(&s)&&!strcmp(cursor_row(&s)->label,"GAMES"));
 key(&s,XZ_SHELL_KEY_BACK);assert(xz_shell_screen(&s)==XZ_SHELL_PLAY);
 /* the title bar's back arrow is BACK too */
 key(&s,XZ_SHELL_KEY_MENU);turn(&s,6);push(&s);cursor_to(&s,"GAMES");push(&s);
 struct xz_shell_rect b=xz_shell_settings_rect(XZ_SHELL_SR_BACK_BUTTON);
 assert(send(&s,(struct xz_shell_event){XZ_SHELL_TOUCH_DOWN,b.x+b.w/2,b.y+b.h/2,0,0,-1})==1&&out[0].id==XZ_SHELL_MENU_BACK&&ui.page==XZ_UI_EXTRAS);
 send(&s,(struct xz_shell_event){XZ_SHELL_TOUCH_UP,b.x+b.w/2,b.y+b.h/2,0,0,-1});
 assert(!others&&!refusals);
 puts("PASS BACK: values pane, one page up onto its link, top-level MODS closes on one BACK");
}

/* HOME (DESIGN 3.3): six tiles with live state; turn moves tile to tile and into a tile's SET UP button;
   a tile opens its section on the rail; SET UP goes straight to the fix. */
static void test_home(void) {
 struct xz_shell s;fresh(&s);
 um.deck_view=2;um.route.state=XZ_UI_ROUTE_NOT_DETECTED;publish();
 key(&s,XZ_SHELL_KEY_MENU);push(&s);
 assert(menu.layout==XZ_SHELL_MENU_HOME&&menu.row_count==7);   /* six tiles and the 4 DECKS SET UP */
 static const char *const tiles[6]={"PLAY","STEMS","SCREEN","4 DECKS","CONNECT","EXTRAS"};
 int k=0;
 for(int i=0;i<menu.row_count;i++){
  const struct xz_shell_menu_row *r=&menu.row[i];
  assert(r->rect.w>=96&&r->rect.h>=44&&r->rect.x>=90&&r->rect.y>=42&&r->rect.x+r->rect.w<=800&&r->rect.y+r->rect.h<=350);
  if(r->kind==XZ_SHELL_MENU_LINK){assert(!strcmp(r->label,tiles[k])&&r->value[0]&&r->help[0]);k++;}
  else{assert(r->kind==XZ_SHELL_MENU_PART&&!strcmp(r->label,"SET UP")&&!strcmp(menu.row[i-1].label,"4 DECKS"));
   assert(menu.row[i-1].tone==XZ_SHELL_TONE_ALERT);}
 }
 assert(k==6);
 /* the encoder: PLAY first, then on through the tiles and into 4 DECKS's button */
 assert(cursor_row(&s)&&!strcmp(cursor_row(&s)->label,"PLAY"));
 for(int i=0;i<4;i++)turn(&s,1);
 assert(cursor_row(&s)&&!strcmp(cursor_row(&s)->label,"SET UP"));
 render_check(&s);
 /* SET UP opens 4 DECKS, and the rail follows to it */
 assert(push(&s)==1&&out[0].id==XZ_SHELL_MENU_ACTIVATE&&ui.page==XZ_UI_FOUR_DECKS&&s.mods_view.tab==mfx_tab_of(XZ_UI_FOUR_DECKS));
 /* a tap on a tile opens its section; a tap on the SET UP button inside a tile runs the button */
 fresh(&s);um.deck_view=2;um.route.state=XZ_UI_ROUTE_NOT_DETECTED;publish();key(&s,XZ_SHELL_KEY_MENU);
 struct xz_shell_rect t=menu.row[0].rect;
 assert(tap(&s,t.x+20,t.y+20)==1&&out[0].id==XZ_SHELL_MENU_ACTIVATE&&ui.page==XZ_UI_PLAY&&s.mods_view.tab==mfx_tab_of(XZ_UI_PLAY));
 key(&s,XZ_SHELL_KEY_BACK);key(&s,XZ_SHELL_KEY_MENU);
 for(int i=0;i<menu.row_count;i++)if(menu.row[i].kind==XZ_SHELL_MENU_PART){
  struct xz_shell_rect bb=menu.row[i].rect;
  assert(tap(&s,bb.x+bb.w/2,bb.y+bb.h/2)==1&&out[0].id==XZ_SHELL_MENU_TAP&&ui.page==XZ_UI_FOUR_DECKS);break;
 }
 /* no stem engine: the STEMS tile gets its own SET UP into the USB stems setup */
 fresh(&s);um.enabled=0;for(int d=0;d<2;d++){um.deck[d].ready&=~(uint32_t)XZ_UI_STEM;um.deck[d].usb_reason=XZ_UI_USB_WHY_ENGINE;}publish();
 key(&s,XZ_SHELL_KEY_MENU);push(&s);turn(&s,2);
 assert(cursor_row(&s)&&!strcmp(cursor_row(&s)->label,"SET UP"));
 /* a setup opened from HOME stays under HOME: BACK returns to the button that led there */
 push(&s);assert(ui.page==XZ_UI_LOCAL_STEM_SETUP&&s.mods_view.tab==mfx_tab_of(XZ_UI_HOME)&&menu.depth==1);
 key(&s,XZ_SHELL_KEY_BACK);assert(ui.page==XZ_UI_HOME&&cursor_row(&s)&&!strcmp(cursor_row(&s)->label,"SET UP"));
 assert(!others&&!refusals);
 puts("PASS HOME: six tiles with state, SET UP only where needed, encoder tile to tile and into the button, taps, the rail follows");
}

/* The picture pages (DESIGN 2): every part is a row at its rect; push runs the panel's encoder push, a tap
   the panel's touch, and while a pair adjusts the encoder turns its size. */
static void test_pictures(void) {
 struct xz_shell s;fresh(&s);um.jump.enabled=1;publish();
 key(&s,XZ_SHELL_KEY_MENU);turn(&s,1);push(&s);cursor_to(&s,"BEAT JUMP PADS");push(&s);
 assert(ui.page==XZ_UI_BEAT_JUMP&&menu.layout==XZ_SHELL_MENU_PICTURE&&menu.picture==XZ_SHELL_PIC_BEAT_JUMP);
 assert(menu.row_count==17);
 for(int i=0;i<menu.row_count;i++){
  const struct xz_shell_menu_row *r=&menu.row[i];
  assert(r->kind==XZ_SHELL_MENU_PART&&r->rect.h>=40&&r->rect.x>=96&&r->rect.x+r->rect.w<=800&&r->rect.y>=42&&r->rect.y+r->rect.h<=350);
  assert(r->chips&&(r->works_on&3u)==3u);
 }
 cursor_to(&s,"PADS E / F");
 ran_count=0;assert(push(&s)==1&&ui.adjusting&&menu.adjusting&&ui.jump_edit_pair==2);
 /* the turn goes to the panel: JUMP_STEP on that pair; push ends it */
 assert(turn(&s,1)==1&&out[0].id==XZ_SHELL_MENU_TURN&&ran_count&&ran[ran_count-1].kind==XZ_UI_JUMP_STEP);
 assert(cursor_row(&s)&&!strcmp(cursor_row(&s)->label,"PADS E / F"));
 push(&s);assert(!ui.adjusting&&!menu.adjusting);
 render_check(&s);
 /* a tap on a size chip sets the pair the panel edits */
 for(int i=0;i<menu.row_count;i++)if(menu.row[i].target.kind==XZ_UI_JUMP_SIZE&&menu.row[i].target.index==9){
  struct xz_shell_rect r=menu.row[i].rect;ran_count=0;
  assert(tap(&s,r.x+r.w/2,r.y+r.h/2)==1&&out[0].id==XZ_SHELL_MENU_TAP);
  assert(ran_count==1&&ran[0].kind==XZ_UI_JUMP_SIZE&&ran[0].index==2&&ran[0].value==9);
 }
 /* adjusting, BACK ends it and stays */
 cursor_to(&s,"PADS A / B");push(&s);assert(ui.adjusting);
 assert(key(&s,XZ_SHELL_KEY_BACK)==1&&out[0].id==XZ_SHELL_MENU_BACK&&!ui.adjusting&&ui.page==XZ_UI_BEAT_JUMP);
 /* STEM PADS: the mode buttons run STEM_PAGE */
 fresh(&s);key(&s,XZ_SHELL_KEY_MENU);turn(&s,2);push(&s);cursor_to(&s,"STEM PADS");push(&s);
 assert(ui.page==XZ_UI_STEM_PADS&&menu.picture==XZ_SHELL_PIC_STEM_PADS);
 ran_count=0;cursor_to(&s,"SLIP LOOP");push(&s);
 assert(ran_count&&ran[ran_count-1].kind==XZ_UI_STEM_PAGE&&ran[ran_count-1].index==2);
 render_check(&s);
 /* 4 DECKS: RESCAN and the DECK VIEW segments */
 fresh(&s);um.route.state=XZ_UI_ROUTE_NOT_DETECTED;publish();key(&s,XZ_SHELL_KEY_MENU);turn(&s,4);push(&s);
 assert(menu.picture==XZ_SHELL_PIC_FOUR_DECKS&&menu.line_count>=2);
 ran_count=0;cursor_to(&s,"RESCAN");push(&s);assert(ran_count&&ran[ran_count-1].kind==XZ_UI_ROUTE_RESCAN);
 for(int i=0;i<menu.row_count;i++)if(menu.row[i].target.kind==XZ_UI_DECK_VIEW&&menu.row[i].target.index==2){
  struct xz_shell_rect r=menu.row[i].rect;ran_count=0;
  assert(tap(&s,r.x+r.w/2,r.y+r.h/2)==1&&ran_count&&ran[ran_count-1].kind==XZ_UI_DECK_VIEW&&ran[ran_count-1].value==2);
 }
 render_check(&s);
 assert(!others&&!refusals);
 puts("PASS pictures: BEAT JUMP parts, push adjusts and the encoder turns the size, a tap on a chip, BACK ends adjusting; STEM PADS modes; 4 DECKS RESCAN and DECK VIEW");
}

/* The shell's own look settings live on SCREEN and STEMS as rows; a pick goes to the shell, not the panel. */
static void test_look_rows(void) {
 struct xz_shell s;fresh(&s);memset(look_store,0,sizeof look_store);publish();
 key(&s,XZ_SHELL_KEY_MENU);turn(&s,3);push(&s);
 static const char *const rows[5]={"WAVEFORM COLOURS","TITLES IN WAVEFORMS","TRACK COLOURS","MODE LAMPS","QUANTIZE LAMP"};
 for(int i=0;i<5;i++){
  cursor_to(&s,rows[i]);
  const struct xz_shell_menu_row *r=cursor_row(&s);
  assert(r->target.kind==XZ_UI_SHELL_LOOK&&r->current==0&&r->choice_count>=2);
  look_sets=0;ran_count=0;
  if(r->kind==XZ_SHELL_MENU_TOGGLE)push(&s);else{push(&s);turn(&s,1);push(&s);}
  assert(look_sets==1&&!ran_count&&cursor_row(&s)->current==1);
 }
 /* WAVEFORM COLOURS offers the shell's four (UTILITY's WAVEFORM COLOR is the same setting) */
 cursor_to(&s,"WAVEFORM COLOURS");assert(cursor_row(&s)->choice_count==4&&cursor_row(&s)->picture==XZ_SHELL_PIC_WAVE_COLOURS);
 fresh(&s);key(&s,XZ_SHELL_KEY_MENU);turn(&s,2);push(&s);cursor_to(&s,"BYPASS BUTTON");
 assert(cursor_row(&s)->target.kind==XZ_UI_SHELL_LOOK&&cursor_row(&s)->target.index==XZ_SHELL_ITEM_BYPASS_BUTTON);
 memset(look_store,0,sizeof look_store);
 assert(!others&&!refusals);
 puts("PASS look rows: WAVEFORM COLOURS (4), TITLES, TRACK COLOURS, MODE and QUANTIZE LAMPS, BYPASS BUTTON go to the shell");
}

/* Every page the panel has (but its classic theme list and the two pages the item tabs carry) is
   reachable from the four MENU tabs, and every row on it runs its own control. */
/* Synthetic inventory only. No service, OS backend or live adapter is used. */
static void detected_network(void) {
 um.network.actual.generation=7;um.network.actual.count=1;um.network.result=XZ_NET_OK;
 struct xz_net_adapter *a=&um.network.actual.adapter[0];
 strcpy(a->id.name,"fixture0");a->id.ifindex=7;a->id.identity_epoch=12;a->state_revision=20;
 a->addresses_known=a->link_up=a->gateway_known=XZ_NET_YES;a->address_count=1;
 a->address[0].ip=(struct xz_net_ipv4){{192,0,2,10}};a->address[0].prefix=24;
 a->caps=XZ_NET_CAP_STATIC_ADD|XZ_NET_CAP_GATEWAY;
}
static int link_like(const struct xz_shell_menu_row *r) { return r->kind==XZ_SHELL_MENU_LINK||r->kind==XZ_SHELL_MENU_ACTION||r->kind==XZ_SHELL_MENU_PART; }
static void test_reach_every_page(void) {
 int seen[XZ_UI_PAGE_COUNT]={0};
 static struct { struct xz_ui u; } queue[256];
 static const int roots[7]={XZ_UI_HOME,XZ_UI_PLAY,XZ_UI_STEM_SECTION,XZ_UI_SCREEN,XZ_UI_FOUR_DECKS,XZ_UI_CONNECT,XZ_UI_EXTRAS};
 int rows_checked=0;
 /* a loaded deck with everything working, then a loader without the stem engine: some setup pages
    open only while something is missing */
 for(int variant=0;variant<5;variant++){
 int head=0,tail=0,visits[XZ_UI_PAGE_COUNT]={0};
 mfx_model(&um);
 if(variant==1){um.enabled=0;for(int d=0;d<2;d++){um.deck[d].ready&=~(uint32_t)XZ_UI_STEM;um.deck[d].usb_reason=XZ_UI_USB_WHY_ENGINE;}}
 if(variant==2)detected_network();
 /* engines a loader may have: the sampler, grooves, hot cue preview; and decks 3 and 4 not yet connected */
 if(variant==3)for(int d=0;d<2;d++)um.deck[d].ready|=XZ_UI_SAMPLE|XZ_UI_GROOVE|XZ_UI_HOTCUE|XZ_UI_PREVIEW;
 if(variant==4){um.route.state=XZ_UI_ROUTE_NOT_DETECTED;um.deck_view=1;for(int d=0;d<2;d++)um.deck[d].ready&=~(uint32_t)(XZ_UI_GATE|XZ_UI_SMART);um.enabled=0;}
 for(int i=0;i<7;i++){xz_ui_init(&queue[tail].u);xz_ui_menu_open(&queue[tail].u,(enum xz_ui_page)roots[i]);tail++;}
 while(head<tail){
  ui=queue[head++].u;
  if(visits[ui.page]++>3)continue;
  seen[ui.page]++;
  publish();
  assert(menu.page==(int)ui.page&&menu.root>=0);
  assert(menu.row_count<=XZ_SHELL_MENU_ROWS&&menu.choice_count<=XZ_SHELL_MENU_CHOICES);
  struct xz_ui at=ui;
  for(int r=0;r<menu.row_count;r++){
   const struct xz_shell_menu_row *row=&menu.row[r];
   assert(row->label[0]);
   /* the retired switches are never offered */
   assert(row->target.kind!=XZ_UI_JUMP_SHIFT&&row->target.kind!=XZ_UI_SHIFT_PAGES&&row->target.kind!=XZ_UI_SHIFT_KEYSYNC&&
    row->target.kind!=XZ_UI_RAM_DEBUG&&row->target.kind!=XZ_UI_METRICS_TOGGLE&&row->target.kind!=XZ_UI_FOUR_WAVE_ORDER);
   assert(strcmp(row->label,"FOUR-DECK WAVE ORDER")&&!strstr(row->label,"REKORDBOX"));
   for(int k=r+1;k<menu.row_count;k++)assert(menu.row[k].key!=row->key);
   for(int c=0;c<row->choice_count;c++)assert(menu.choice[row->first_choice+c].label[0]);
   if(!row->available)assert(row->reason[0]);
   /* the row's own control is on the page: running it never reports a changed control */
   const struct xz_shell_menu_target *t=row->choice_count?&menu.choice[row->first_choice].target:&row->target;
   ui=at;ran_count=0;run_target(t);
   assert(strcmp(ui.notice,"THAT CONTROL HAS CHANGED: LOOK AGAIN"));
   rows_checked++;
   /* STATIC IP changes the setup in place; its newly visible EDIT rows
      must be traversed too. The default zero-adapter model cannot offer them. */
   int static_branch=t->kind==XZ_UI_NETWORK_MODE&&t->index==XZ_NET_STATIC&&
    at.network.initialized&&at.network.trial.policy.order[at.network.selected].mode!=XZ_NET_STATIC;
   if((link_like(row)&&ui.page!=at.page)||static_branch){assert(tail<256);queue[tail++].u=ui;}
  }
  ui=at;
 }
 }
 /* pages only the runtime opens: the setups, reached by their own rows above or the runtime itself */
 /* retired pages (their old ids open their new place), the CLASSIC theme grid, and the BUTTON NEXT TO LINK
    page (MODERN sets it in place on PLAY) are not MODERN pages */
 for(int p=0;p<XZ_UI_PAGE_COUNT;p++){
  if(p==XZ_UI_GENERAL||p==XZ_UI_CONTROLS||p==XZ_UI_SETTINGS||p==XZ_UI_THEMES||p==XZ_UI_THEME_PICK||p==XZ_UI_BROWSE_SETUP||p==XZ_UI_BUTTONS)continue;
  if(!seen[p])fprintf(stderr,"page %d (%s) not reached\n",p,xz_ui_page_title(p));
  assert(seen[p]);
 }
 /* The replacement is the PLAY readout and its existing key actions. */
 for(int deck=0;deck<2;deck++){
  struct xz_shell play;xz_shell_init(&play);struct xz_shell_model view;
  xz_shell_play_projection(&play,&sm,&view);
  assert(xz_shell_render_frame(&play,&sm,buffer+GUARD+STRIDE,STRIDE));
  struct xz_shell_rect r=xz_shell_play_rect(&view,XZ_SHELL_R_KEY,deck);
  assert(xz_shell_play_key_deck(&play,&sm,r.x+8,r.y+8)==deck);
  struct xz_play_key popup;xz_play_key_open(&popup,deck);struct xz_ui_action a;
  xz_play_key_touch(&popup,&um,0,0,0,&a);um.deck[deck].ready|=XZ_UI_KEY|XZ_UI_KEYSYNC;
  struct xz_play_key_view key_view;xz_play_key_view(&popup,&um,NULL,&key_view);
  xz_play_key_render(&popup,&key_view,buffer+GUARD+STRIDE,STRIDE);xz_play_key_published(&popup,&popup);
  for(int k=0;k<4;k++){struct xz_ui_widget w=xz_play_key_button(k);
   assert(!xz_play_key_touch(&popup,&um,w.x+8,w.y+8,1,&a));
   assert(xz_play_key_touch(&popup,&um,0,0,0,&a)&&a.deck==deck&&a.kind==(k==3?XZ_UI_KEY_SYNC:XZ_UI_KEY_SHIFT));
  }
 }
 printf("PASS reachable MODS pages from the seven sections (KEYS under PLAY), no retired switch offered, PLAY key actions; %d menu rows checked\n",rows_checked);
}

static void test_ipv4_encoder(void) {
 struct xz_shell s;fresh(&s);detected_network();publish();
 key(&s,XZ_SHELL_KEY_MENU);turn(&s,5);push(&s);cursor_to(&s,"NETWORK");push(&s);
 assert(ui.page==XZ_UI_NETWORK);cursor_to(&s,"CHOOSE ADAPTER / SETTINGS");push(&s);
 assert(ui.page==XZ_UI_NETWORK_SETUP&&ui.network.initialized);
 cursor_to(&s,"STATIC IP");push(&s);assert(ui.network.trial.policy.order[0].mode==XZ_NET_STATIC);
 cursor_to(&s,"EDIT IP");push(&s);assert(ui.page==XZ_UI_NETWORK_EDIT);
 cursor_to(&s,"CLEAR");assert(push(&s)==1&&!ui.network.edit[0]);
 cursor_to(&s,"1");assert(push(&s)==1&&!strcmp(ui.network.edit,"1"));
 hold(&s);assert(!strcmp(ui.network.edit,"1"));
 cursor_to(&s,"BACKSPACE");assert(push(&s)==1&&!ui.network.edit[0]);
 cursor_to(&s,"2");push(&s);cursor_to(&s,"DONE");push(&s);
 assert(ui.page==XZ_UI_NETWORK_SETUP&&!strcmp(ui.network.ip,"2"));
 cursor_to(&s,"EDIT MASK / PREFIX");push(&s);assert(ui.page==XZ_UI_NETWORK_EDIT);render_check(&s);
 key(&s,XZ_SHELL_KEY_BACK);assert(ui.page==XZ_UI_NETWORK_SETUP);
 cursor_to(&s,"EDIT GATEWAY");push(&s);assert(ui.page==XZ_UI_NETWORK_EDIT);
 cursor_to(&s,"CANCEL");push(&s);assert(ui.page==XZ_UI_NETWORK_SETUP);
 key(&s,XZ_SHELL_KEY_BACK);assert(ui.page==XZ_UI_NETWORK);
 key(&s,XZ_SHELL_KEY_BACK);assert(ui.page==XZ_UI_CONNECT);
 key(&s,XZ_SHELL_KEY_BACK);assert(xz_shell_screen(&s)==XZ_SHELL_PLAY);
 assert(!others&&!refusals);
 puts("PASS synthetic detected adapter: encoder STATIC / IP / MASK / GATEWAY / type / delete / done / cancel / nested BACK; no network execution");
}

/* Each page draws inside the frame, from the rail and from inside, at every row and in every values pane. */
static void test_render_every_page(void) {
 static const int pages[]={XZ_UI_HOME,XZ_UI_PLAY,XZ_UI_STEM_SECTION,XZ_UI_SCREEN,XZ_UI_FOUR_DECKS,XZ_UI_CONNECT,XZ_UI_EXTRAS,
  XZ_UI_NETWORK,XZ_UI_NETWORK_SETUP,XZ_UI_NETWORK_EDIT,XZ_UI_FEATURE_SETUP,XZ_UI_LOCAL_STEM_SETUP,XZ_UI_PC_STEMS,XZ_UI_STEM_COLORS,XZ_UI_XPAD,
  XZ_UI_METRICS_SETUP,XZ_UI_BEAT_JUMP,XZ_UI_STEM_PADS,XZ_UI_KEY_SETUP,XZ_UI_STEMS,XZ_UI_RAM_SETUP,
  XZ_UI_CONNECTION,XZ_UI_GAMES,XZ_UI_DOOM,XZ_UI_DOOM_SETUP,XZ_UI_DOOM_FILES,XZ_UI_MYHOUSE_SETUP,XZ_UI_UPDATES,
  XZ_UI_UPDATE_SETUP,XZ_UI_APPS,XZ_UI_APPS_SETUP,XZ_UI_PROFILE_SETUP,XZ_UI_PROFILE_EDIT};
 int frames=0;
 for(size_t i=0;i<sizeof pages/sizeof *pages;i++)for(int variant=0;variant<2;variant++){
  struct xz_shell s;fresh(&s);
  if(variant){um.enabled=0;um.dj=NULL;um.sdk.host=XZ_SDK_UI_ABSENT;um.connection.ready=0;
   for(int d=0;d<2;d++){um.deck[d].ready=0;um.deck[d].track=NULL;um.deck[d].usb_reason=XZ_UI_USB_WHY_ENGINE;}}
  int top=pages[i];while(!xz_ui_page_is_top(top))top=xz_ui_page_parent(top);
  if(variant){um.route.state=XZ_UI_ROUTE_FAILED;um.deck_view=2;}
  xz_ui_menu_open(&ui,(enum xz_ui_page)top);
  xz_ui_navigate(&ui,(enum xz_ui_page)pages[i]);
  if(pages[i]==XZ_UI_PROFILE_EDIT){ui.edit_step=variant?XZ_UI_EDIT_LOGO:XZ_UI_EDIT_NAME;ui.edit_entry=-1;}
  publish();
  int tab=mfx_tab_of(menu.root);assert(tab);
  xz_shell_mods_show(&s,&sm,tab);
  render_check(&s);frames++;
  for(int r=0;r<menu.row_count;r++){
   turn(&s,1);render_check(&s);frames++;
   const struct xz_shell_menu_row *row=cursor_row(&s);
   if(row&&row->kind==XZ_SHELL_MENU_CHOICE&&!menu.adjusting){
    struct xz_shell copy=s;
    struct xz_shell_action a[XZ_SHELL_ACTIONS];
    xz_shell_input(&copy,&sm,&(struct xz_shell_event){XZ_SHELL_ENCODER_PUSH,0,0,0,0,-1},a);
    render_check(&copy);frames++;
   }
   if((int)ui.page!=pages[i])break;   /* a row led on */
  }
 }
 printf("PASS %d frames of every MENU page (filled and empty models) stay inside the frame and its guards\n",frames);
}

/* Press parity with the old route (MENU, push into the page, the control): never more pushes. */
static int pushes;
static void count_push(struct xz_shell *s) { push(s);pushes++; }
static void test_press_parity(void) {
 struct xz_shell s;
 /* SMART CUE on PLAY: old 3 pushes after MENU, now 2 */
 fresh(&s);key(&s,XZ_SHELL_KEY_MENU);turn(&s,1);pushes=0;
 count_push(&s);cursor_to(&s,"SMART CUE");
 count_push(&s);
 assert(ran_count&&ran[ran_count-1].kind==XZ_UI_ENABLE&&ran[ran_count-1].index==XZ_UI_SMART&&ran[ran_count-1].value==1);
 assert(pushes==2);
 /* the stem pad mode: old 3 pushes (values pane), now 3 (STEMS, STEM PADS, the mode button) */
 fresh(&s);key(&s,XZ_SHELL_KEY_MENU);turn(&s,2);pushes=0;
 count_push(&s);cursor_to(&s,"STEM PADS");count_push(&s);cursor_to(&s,"SLIP LOOP");count_push(&s);
 assert(ran_count&&ran[ran_count-1].kind==XZ_UI_STEM_PAGE&&pushes<=3);
 /* a game from EXTRAS: old MENU, push, push, link, game = 4 pushes, now 3 */
 fresh(&s);key(&s,XZ_SHELL_KEY_MENU);turn(&s,6);pushes=0;
 count_push(&s);cursor_to(&s,"GAMES");count_push(&s);cursor_to(&s,"BEAT ARCADE");count_push(&s);
 assert(ran_count&&ran[ran_count-1].kind==XZ_UI_ARCADE_OPEN&&pushes==3);
 /* a beat jump size: old MENU, push, push, section, pair, push-turn-push = 5, now 4 (the turn sets it) */
 fresh(&s);um.jump.enabled=1;publish();key(&s,XZ_SHELL_KEY_MENU);turn(&s,1);pushes=0;
 count_push(&s);cursor_to(&s,"BEAT JUMP PADS");count_push(&s);cursor_to(&s,"PADS E / F");count_push(&s);turn(&s,1);count_push(&s);
 int stepped=0;for(size_t i=0;i<ran_count;i++)stepped|=ran[i].kind==XZ_UI_JUMP_STEP&&ran[i].index==1;
 assert(stepped&&pushes==4);
 /* DECK VIEW on 4 DECKS: MENU, turn, push into the page, the segment = 2 pushes */
 fresh(&s);um.route.state=XZ_UI_ROUTE_READY;publish();key(&s,XZ_SHELL_KEY_MENU);turn(&s,4);pushes=0;
 count_push(&s);cursor_to(&s,"4 | 3 1 2 4");count_push(&s);
 assert(ran_count&&ran[ran_count-1].kind==XZ_UI_DECK_VIEW&&ran[ran_count-1].value==2&&pushes==2);
 puts("PASS press parity: SMART CUE 2 pushes (was 3), pad mode 3 (3), a game 3 (4), a beat jump size 4 (5), DECK VIEW 2");
}

/* Touch: one tap runs a row or picks a value; dragging cancels; a pad is held while touched. */
static void test_touch(void) {
 struct xz_shell s;fresh(&s);key(&s,XZ_SHELL_KEY_MENU);
 struct xz_shell_rect r=xz_shell_menu_row_rect(&s,&sm,2);
 assert(r.w&&r.h>=36);
 size_t n=tap(&s,r.x+40,r.y+r.h/2);
 assert(n==1&&out[0].id==XZ_SHELL_MENU_ACTIVATE);
 /* the profile name: a key types on one tap */
 fresh(&s);
 xz_ui_menu_open(&ui,XZ_UI_CONNECT);xz_ui_navigate(&ui,XZ_UI_PROFILE_SETUP);
 publish();xz_shell_mods_show(&s,&sm,mfx_tab_of(menu.root));
 cursor_to(&s,"NEW PROFILE");push(&s);
 assert(ui.page==XZ_UI_PROFILE_EDIT&&menu.layout==XZ_SHELL_MENU_KEYBOARD&&!ui.edit_name[0]);
 int typed=0;
 for(int row=0;row<menu.row_count&&typed<3;row++){
  if(menu.row[row].kind!=XZ_SHELL_MENU_KEY||menu.row[row].label[1])continue;
  struct xz_shell_rect k=xz_shell_menu_row_rect(&s,&sm,row);
  assert(k.w>=44&&k.h>=36&&k.y+k.h<=xz_shell_settings_rect(XZ_SHELL_SR_STATUS).y);
  assert(tap(&s,k.x+k.w/2,k.y+k.h/2)==1);typed++;
 }
 assert(strlen(ui.edit_name)==3&&!strcmp(menu.field,ui.edit_name));
 render_check(&s);
 /* the X-PAD loop strip: held while touched, a release lets go */
 fresh(&s);for(int d=0;d<2;d++)um.deck[d].ready|=XZ_UI_SAMPLE;
 xz_ui_menu_open(&ui,XZ_UI_PLAY);xz_ui_navigate(&ui,XZ_UI_XPAD);publish();
 xz_shell_mods_show(&s,&sm,mfx_tab_of(XZ_UI_PLAY));cursor_to(&s,"LOOP STRIP");
 struct xz_shell_rect pad=xz_shell_menu_pad_rect();
 ran_count=0;
 assert(send(&s,(struct xz_shell_event){XZ_SHELL_TOUCH_DOWN,pad.x+pad.w-2,pad.y+2,0,0,-1})==1&&out[0].id==XZ_SHELL_MENU_PRESS);
 assert(ran_count==1&&ran[0].kind==XZ_UI_STRIP&&ran[0].phase==XZ_UI_PRESS&&ran[0].index==5);
 send(&s,(struct xz_shell_event){XZ_SHELL_TOUCH_MOVE,pad.x+2,pad.y+pad.h-2,0,0,-1});
 assert(ran_count==2&&ran[1].kind==XZ_UI_STRIP&&ran[1].phase==XZ_UI_MOVE&&ran[1].index==0);
 send(&s,(struct xz_shell_event){XZ_SHELL_TOUCH_UP,pad.x+2,pad.y+pad.h-2,0,0,-1});
 assert(ran_count==3&&ran[2].kind==XZ_UI_STRIP&&ran[2].phase==XZ_UI_RELEASE);
 assert(!others&&!refusals);
 puts("PASS touch: one-tap rows and keys, the loop strip is held while touched");
}

/* A page the runtime has not published for this tab, a hostile page and a page that changes under the
   cursor all draw and drive safely. */
static void test_wild(void) {
 struct xz_shell s;fresh(&s);
 key(&s,XZ_SHELL_KEY_MENU);
 sm.menu=NULL;render_check(&s);
 /* nothing to show: a push asks for the page again */
 assert(push(&s)==1&&out[0].id==XZ_SHELL_MENU_OPEN);
 sm.menu=&menu;
 static struct xz_shell_menu wild;
 memset(&wild,0x7f,sizeof wild);wild.root=XZ_UI_HOME;wild.page=XZ_UI_HOME;
 for(int i=0;i<XZ_SHELL_MENU_ROWS;i++){wild.row[i].label[sizeof wild.row[i].label-1]=0;wild.row[i].value[sizeof wild.row[i].value-1]=0;
  wild.row[i].help[sizeof wild.row[i].help-1]=0;wild.row[i].reason[sizeof wild.row[i].reason-1]=0;wild.row[i].excluded[sizeof wild.row[i].excluded-1]=0;}
 for(int i=0;i<XZ_SHELL_MENU_CHOICES;i++){wild.choice[i].label[sizeof wild.choice[i].label-1]=0;wild.choice[i].detail[sizeof wild.choice[i].detail-1]=0;}
 wild.save[sizeof wild.save-1]=0;
 for(int i=0;i<XZ_SHELL_MENU_LINES;i++)wild.line[i].text[sizeof wild.line[i].text-1]=0;
 for(int i=0;i<XZ_SHELL_MENU_SECTIONS;i++)wild.sections[i][sizeof wild.sections[i]-1]=0;
 for(int i=0;i<XZ_SHELL_MENU_STEPS;i++)wild.steps[i][sizeof wild.steps[i]-1]=0;
 wild.title[sizeof wild.title-1]=0;wild.path[sizeof wild.path-1]=0;wild.status[sizeof wild.status-1]=0;
 wild.field[sizeof wild.field-1]=0;wild.field_label[sizeof wild.field_label-1]=0;
 sm.menu=&wild;
 static const int layouts[4]={XZ_SHELL_MENU_LIST,XZ_SHELL_MENU_KEYBOARD,XZ_SHELL_MENU_HOME,XZ_SHELL_MENU_PICTURE};
 for(int li=0;li<4;li++)for(int picture=0;picture<=XZ_SHELL_PIC_MEDIA_BUTTON;picture+=(li==3?1:XZ_SHELL_PIC_MEDIA_BUTTON+1)){
  wild.layout=(uint8_t)layouts[li];wild.picture=(uint8_t)picture;
  struct xz_shell w;xz_shell_init(&w);xz_shell_mods_show(&w,&sm,1);
  for(int i=0;i<40;i++){
   struct xz_shell_action a[XZ_SHELL_ACTIONS];
   static const enum xz_shell_event_kind kinds[]={XZ_SHELL_ENCODER_TURN,XZ_SHELL_ENCODER_PUSH,XZ_SHELL_ENCODER_HOLD,XZ_SHELL_TOUCH_DOWN,XZ_SHELL_TOUCH_UP};
   xz_shell_input(&w,&sm,&(struct xz_shell_event){kinds[i%5],(i*97)%800,(i*53)%480,i%3-1,0,-1},a);
   render_check(&w);
  }
 }
 sm.menu=&menu;
 /* the page shrinks under the cursor */
 fresh(&s);key(&s,XZ_SHELL_KEY_MENU);push(&s);turn(&s,20);
 menu.row_count=1;render_check(&s);push(&s);turn(&s,3);render_check(&s);
 puts("PASS a missing, hostile or shrinking page draws and drives safely; a push asks for a missing page");
}

int main(void) {
 setvbuf(stdout,NULL,_IONBF,0);
 fx_track_build(&track,7);
 fx_model_full(&sm,&track);
 sm.settings_screen=fxs_reset();
 sm.mods=&mfx_mods;sm.menu=&menu;
 (void)fxs_item;
 test_open();
 test_encoder_never_leaves();
 test_back();
 test_home();
 test_pictures();
 test_look_rows();
 test_reach_every_page();
 test_ipv4_encoder();
 test_render_every_page();
 test_press_parity();
 test_touch();
 test_wild();
 puts("PASS menu");
 return 0;
}
