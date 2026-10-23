#ifdef NDEBUG
#error Shell acceptance requires active assertions
#endif
/* The MODS screen's MENU tabs over the MODS panel's real pages (ui/ui_menu.c): the encoder never leaves
   MODS, BACK goes up exactly one level, every panel page is reachable, every row runs its own control,
   and every page draws inside the frame. A small bridge does what shell_runtime.c and ui_runtime.c do:
   it maps each MOD_MENU action to its target in the page the shell showed, runs it on the panel state and
   publishes the page that results. */
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
static void publish(void) { assert(xz_ui_menu_build(&ui,&um,&menu)); }
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
 struct xz_ui_action a[XZ_UI_MENU_ACTIONS];size_t n=xz_ui_menu_run(&ui,&um,t,a);
 for(size_t i=0;i<n;i++){if(ran_count<64)ran[ran_count++]=a[i];runtime_navigation(&a[i]);}
}
static void bridge_action(const struct xz_shell_action *a) {
 /* the item tabs (BUTTONS, APPEARANCE) set their items; nothing in MODS opens another menu or hands off */
 if(a->kind==XZ_SHELL_ACT_SET_MOD)return;
 if(a->kind!=XZ_SHELL_ACT_MOD_MENU){others++;fprintf(stderr,"other action kind %d id %d value %d page %d\n",(int)a->kind,a->id,(int)a->value,(int)ui.page);return;}
 struct xz_ui_action pad[XZ_UI_ACTIONS];size_t n;
 switch(a->id){
 case XZ_SHELL_MENU_OPEN:
  assert(a->value==XZ_UI_GENERAL||a->value==XZ_UI_CONTROLS||a->value==XZ_UI_SETTINGS||a->value==XZ_UI_EXTRAS);
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
 default:{
  const struct xz_shell_menu_row *r=&menu.row[a->value];
  n=xz_ui_menu_pad(&ui,&um,&r->target,a->deck>>10,a->deck&1023,a->id!=XZ_SHELL_MENU_RELEASE,pad);
  for(size_t i=0;i<n&&ran_count<64;i++)ran[ran_count++]=pad[i];
  break;}
 }
}
static size_t send(struct xz_shell *s, struct xz_shell_event e) {
 memset(out,0,sizeof out);
 size_t n=xz_shell_input(s,&sm,&e,out);
 struct xz_shell_action copy[XZ_SHELL_ACTIONS];memcpy(copy,out,sizeof copy);
 for(size_t i=0;i<n;i++)bridge_action(&copy[i]);
 publish();
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
 turn(s,-99);
 for(int i=0;i<=menu.row_count;i++){
  const struct xz_shell_menu_row *r=cursor_row(s);
  if(r&&!strncmp(r->label,label,strlen(label)))return;
  turn(s,1);
 }
 fprintf(stderr,"no row %s on %s\n",label,menu.title);assert(0);
}

/* MENU, the MODS key and the header button open MODS on GENERAL and ask for its page; the rail asks for
   each MENU tab's page as it is reached; the item tabs ask nothing. */
static void test_open(void) {
 struct xz_shell s;fresh(&s);
 xz_ui_navigate(&ui,XZ_UI_DOOM_FILES);   /* wherever the panel was */
 assert(key(&s,XZ_SHELL_KEY_MENU)==1&&out[0].kind==XZ_SHELL_ACT_MOD_MENU&&out[0].id==XZ_SHELL_MENU_OPEN&&out[0].value==XZ_UI_GENERAL);
 assert(in_mods(&s)&&s.mods_view.tab==1&&s.mods_view.on_tabs&&ui.page==XZ_UI_GENERAL&&!ui.history_count);
 assert(menu.root==XZ_UI_GENERAL&&menu.depth==0);
 render_check(&s);
 static const int expect[7]={-1,XZ_UI_GENERAL,XZ_UI_CONTROLS,-1,-1,XZ_UI_SETTINGS,XZ_UI_EXTRAS};
 for(int t=2;t<=6;t++){
  size_t n=turn(&s,1);
  assert(s.mods_view.tab==t&&s.mods_view.on_tabs);
  if(expect[t]<0)assert(n==0);
  else{assert(n==1&&out[0].value==expect[t]&&(int)ui.page==expect[t]);}
  render_check(&s);
 }
 assert(key(&s,XZ_SHELL_KEY_MENU)==0&&s.depth==1);
 assert(key(&s,XZ_SHELL_KEY_MODS)==1&&out[0].id==XZ_SHELL_MENU_OPEN&&in_mods(&s));
 assert(key(&s,XZ_SHELL_KEY_MODS)==0&&s.depth==1);
 assert(tap(&s,XZ_SHELL_MODS_X+20,10)==1&&out[0].id==XZ_SHELL_MENU_OPEN&&in_mods(&s));
 /* held MENU lands on UTILITY, which asks nothing */
 assert(key(&s,XZ_SHELL_KEY_MENU_HOLD)==0&&s.mods_view.tab==0);
 assert(!others&&!refusals);
 puts("PASS MENU tabs: MENU, the MODS key, the header and the rail ask for the tab's page; item tabs ask nothing");
}

/* The owner's report: scrolling and clicking with the encoder must never leave MODS. Long seeded runs of
   turns, pushes and holds over every tab, every page they lead to, and every value pane. */
static void test_encoder_never_leaves(void) {
 uint32_t seed=0x2026u;
 int pages_seen[XZ_UI_PAGE_COUNT]={0};
 for(int run=0;run<24;run++){
  struct xz_shell s;fresh(&s);
  key(&s,XZ_SHELL_KEY_MENU);
  turn(&s,run%6);push(&s);   /* into the tab: from here only BACK reaches the rail */
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

/* BACK: the values pane, then one page up (the cursor back on the link that led away), then the rail,
   then MODS closes. */
static void test_back(void) {
 struct xz_shell s;fresh(&s);
 key(&s,XZ_SHELL_KEY_MENU);
 assert(push(&s)==0&&!s.mods_view.on_tabs&&pane(&s)==1);
 cursor_to(&s,"PC STEMS SETUP");
 assert(push(&s)==1&&out[0].id==XZ_SHELL_MENU_ACTIVATE);
 assert(ui.page==XZ_UI_PC_STEMS&&menu.depth==1&&menu.root==XZ_UI_GENERAL);
 /* the new page starts on its first control: its setup step */
 assert(cursor_row(&s)&&!strcmp(cursor_row(&s)->label,"DETECT PC"));
 /* a choice row: push opens its values, BACK closes them and asks nothing */
 cursor_to(&s,"DECK TO SET UP");
 assert(push(&s)==1);   /* two decks: one push flips */
 assert(ui.deck==1);
 cursor_to(&s,"DETECT PC");
 assert(key(&s,XZ_SHELL_KEY_BACK)==1&&out[0].id==XZ_SHELL_MENU_BACK&&ui.page==XZ_UI_GENERAL&&in_mods(&s));
 assert(cursor_row(&s)&&!strcmp(cursor_row(&s)->label,"PC STEMS SETUP")&&pane(&s)==1);
 /* BACK then push re-enters the page just left */
 assert(push(&s)==1&&ui.page==XZ_UI_PC_STEMS);
 key(&s,XZ_SHELL_KEY_BACK);
 assert(key(&s,XZ_SHELL_KEY_BACK)==0&&s.mods_view.on_tabs&&in_mods(&s));
 assert(key(&s,XZ_SHELL_KEY_BACK)==0&&s.depth==1&&xz_shell_screen(&s)==XZ_SHELL_PLAY);
 /* values pane */
 key(&s,XZ_SHELL_KEY_MENU);turn(&s,1);push(&s);   /* CONTROLS */
 cursor_to(&s,"STEM ORDER");
 assert(push(&s)==0&&pane(&s)==2);
 assert(key(&s,XZ_SHELL_KEY_BACK)==0&&pane(&s)==1&&!s.mods_view.on_tabs&&ui.page==XZ_UI_CONTROLS);
 /* three deep: EXTRAS / GAMES / DOOM, back one level at a time */
 key(&s,XZ_SHELL_KEY_BACK);turn(&s,4);assert(ui.page==XZ_UI_EXTRAS);push(&s);
 cursor_to(&s,"GAMES");push(&s);assert(ui.page==XZ_UI_GAMES&&menu.depth==1);
 cursor_to(&s,"CHEX QUEST");push(&s);assert(ui.page==XZ_UI_DOOM&&menu.depth==2&&!strcmp(menu.path,"EXTRAS / GAMES"));
 key(&s,XZ_SHELL_KEY_BACK);assert(ui.page==XZ_UI_GAMES&&cursor_row(&s)&&!strcmp(cursor_row(&s)->label,"CHEX QUEST"));
 key(&s,XZ_SHELL_KEY_BACK);assert(ui.page==XZ_UI_EXTRAS&&cursor_row(&s)&&!strcmp(cursor_row(&s)->label,"GAMES"));
 key(&s,XZ_SHELL_KEY_BACK);assert(s.mods_view.on_tabs);
 /* the title bar's back arrow is BACK too */
 push(&s);cursor_to(&s,"GAMES");push(&s);
 struct xz_shell_rect b=xz_shell_settings_rect(XZ_SHELL_SR_BACK_BUTTON);
 assert(send(&s,(struct xz_shell_event){XZ_SHELL_TOUCH_DOWN,b.x+b.w/2,b.y+b.h/2,0,0,-1})==1&&out[0].id==XZ_SHELL_MENU_BACK&&ui.page==XZ_UI_EXTRAS);
 send(&s,(struct xz_shell_event){XZ_SHELL_TOUCH_UP,b.x+b.w/2,b.y+b.h/2,0,0,-1});
 assert(!others&&!refusals);
 puts("PASS BACK: values pane, one page up onto the link that led away, the rail, then closed");
}

/* Every page the panel has (but its classic theme list and the two pages the item tabs carry) is
   reachable from the four MENU tabs, and every row on it runs its own control. */
static int link_like(const struct xz_shell_menu_row *r) { return r->kind==XZ_SHELL_MENU_LINK||r->kind==XZ_SHELL_MENU_ACTION; }
static void test_reach_every_page(void) {
 int seen[XZ_UI_PAGE_COUNT]={0};
 static struct { struct xz_ui u; } queue[256];
 static const int roots[4]={XZ_UI_GENERAL,XZ_UI_CONTROLS,XZ_UI_SETTINGS,XZ_UI_EXTRAS};
 int rows_checked=0;
 /* a loaded deck with everything working, then a loader without the stem engine: some setup pages
    open only while something is missing */
 for(int variant=0;variant<2;variant++){
 int head=0,tail=0;
 mfx_model(&um);
 if(variant){um.enabled=0;for(int d=0;d<2;d++){um.deck[d].ready&=~(uint32_t)XZ_UI_STEM;um.deck[d].usb_reason=XZ_UI_USB_WHY_ENGINE;}}
 for(int i=0;i<4;i++){xz_ui_init(&queue[tail].u);xz_ui_menu_open(&queue[tail].u,(enum xz_ui_page)roots[i]);tail++;}
 while(head<tail){
  ui=queue[head++].u;
  if(seen[ui.page]>3)continue;
  seen[ui.page]++;
  publish();
  assert(menu.page==(int)ui.page&&menu.root>=0);
  assert(menu.row_count<=XZ_SHELL_MENU_ROWS&&menu.choice_count<=XZ_SHELL_MENU_CHOICES);
  struct xz_ui at=ui;
  for(int r=0;r<menu.row_count;r++){
   const struct xz_shell_menu_row *row=&menu.row[r];
   assert(row->label[0]);
   assert(row->target.kind!=XZ_UI_KEY_SHIFT&&row->target.kind!=XZ_UI_KEY_SYNC);
   assert(!((row->target.kind==XZ_UI_PANEL||row->target.kind==XZ_UI_FEATURE_LINK)&&row->target.index==XZ_UI_KEY_SETUP));
   for(int k=r+1;k<menu.row_count;k++)assert(menu.row[k].key!=row->key);
   for(int c=0;c<row->choice_count;c++)assert(menu.choice[row->first_choice+c].label[0]);
   if(!row->available)assert(row->reason[0]);
   /* the row's own control is on the page: running it never reports a changed control */
   const struct xz_shell_menu_target *t=row->choice_count?&menu.choice[row->first_choice].target:&row->target;
   ui=at;ran_count=0;run_target(t);
   assert(strcmp(ui.notice,"THAT CONTROL HAS CHANGED: LOOK AGAIN"));
   rows_checked++;
   if(link_like(row)&&ui.page!=at.page&&tail<256)queue[tail++].u=ui;
  }
  ui=at;
 }
 }
 /* pages only the runtime opens: the setups, reached by their own rows above or the runtime itself */
 for(int p=0;p<XZ_UI_PAGE_COUNT;p++){
  if(p==XZ_UI_KEY_SETUP){assert(!seen[p]);continue;}
  if(p==XZ_UI_THEMES||p==XZ_UI_BUTTONS||p==XZ_UI_THEME_PICK||p==XZ_UI_BROWSE_SETUP)continue;
  if(!seen[p])fprintf(stderr,"page %d (%s) not reached\n",p,xz_ui_page_title(p));
  assert(seen[p]);
 }
 /* The replacement is the PLAY readout and its existing key actions. */
 for(int deck=0;deck<2;deck++){
  struct xz_shell play;xz_shell_init(&play);struct xz_shell_model view;
  xz_shell_play_projection(&play,&sm,&view);
  struct xz_shell_rect r=xz_shell_play_rect(&view,XZ_SHELL_R_KEY,deck);
  assert(xz_shell_play_key_deck(&play,&sm,r.x+8,r.y+8)==deck);
  struct xz_play_key popup;xz_play_key_open(&popup,deck);struct xz_ui_action a;
  xz_play_key_touch(&popup,&um,0,0,0,&a);um.deck[deck].ready|=XZ_UI_KEY|XZ_UI_KEYSYNC;
  for(int k=0;k<4;k++){struct xz_ui_widget w=xz_play_key_button(k);
   assert(!xz_play_key_touch(&popup,&um,w.x+8,w.y+8,1,&a));
   assert(xz_play_key_touch(&popup,&um,0,0,0,&a)&&a.deck==deck&&a.kind==(k==3?XZ_UI_KEY_SYNC:XZ_UI_KEY_SHIFT));
  }
 }
 printf("PASS reachable MODS pages, absence of KEY settings navigation and replacement PLAY key actions; %d menu rows checked\n",rows_checked);
}

/* Each page draws inside the frame, from the rail and from inside, at every row and in every values pane. */
static void test_render_every_page(void) {
 static const int pages[]={XZ_UI_GENERAL,XZ_UI_FEATURE_SETUP,XZ_UI_LOCAL_STEM_SETUP,XZ_UI_PC_STEMS,XZ_UI_STEM_COLORS,XZ_UI_XPAD,
  XZ_UI_METRICS_SETUP,XZ_UI_CONTROLS,XZ_UI_BEAT_JUMP,XZ_UI_SETTINGS,XZ_UI_STEMS,XZ_UI_BROWSE_SETUP,XZ_UI_RAM_SETUP,
  XZ_UI_EXTRAS,XZ_UI_CONNECTION,XZ_UI_GAMES,XZ_UI_DOOM,XZ_UI_DOOM_SETUP,XZ_UI_DOOM_FILES,XZ_UI_MYHOUSE_SETUP,XZ_UI_UPDATES,
  XZ_UI_UPDATE_SETUP,XZ_UI_APPS,XZ_UI_APPS_SETUP,XZ_UI_PROFILE_SETUP,XZ_UI_PROFILE_EDIT};
 int frames=0;
 for(size_t i=0;i<sizeof pages/sizeof *pages;i++)for(int variant=0;variant<2;variant++){
  struct xz_shell s;fresh(&s);
  if(variant){um.enabled=0;um.dj=NULL;um.sdk.host=XZ_SDK_UI_ABSENT;um.connection.ready=0;
   for(int d=0;d<2;d++){um.deck[d].ready=0;um.deck[d].track=NULL;um.deck[d].usb_reason=XZ_UI_USB_WHY_ENGINE;}}
  xz_ui_menu_open(&ui,pages[i]==XZ_UI_PROFILE_EDIT||pages[i]==XZ_UI_PROFILE_SETUP||pages[i]==XZ_UI_APPS||pages[i]==XZ_UI_APPS_SETUP?XZ_UI_EXTRAS:XZ_UI_GENERAL);
  xz_ui_navigate(&ui,(enum xz_ui_page)pages[i]);
  if(pages[i]==XZ_UI_PROFILE_EDIT){ui.edit_step=variant?XZ_UI_EDIT_LOGO:XZ_UI_EDIT_NAME;ui.edit_entry=-1;}
  publish();
  int tab=mfx_tab_of(menu.root);assert(tab);
  xz_shell_mods_show(&s,&sm,tab);
  render_check(&s);frames++;
  for(int r=0;r<menu.row_count;r++){
   turn(&s,1);render_check(&s);frames++;
   const struct xz_shell_menu_row *row=cursor_row(&s);
   if(row&&row->kind==XZ_SHELL_MENU_CHOICE){
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

/* Press parity with the old route (MENU, push into the page card, push OPEN, the panel's control): never
   more pushes. */
static int pushes;
static void count_push(struct xz_shell *s) { push(s);pushes++; }
static void test_press_parity(void) {
 struct xz_shell s;
 /* GATE CUE on GENERAL: old 3 pushes after MENU, now 2 */
 fresh(&s);key(&s,XZ_SHELL_KEY_MENU);pushes=0;
 count_push(&s);cursor_to(&s,"SMART CUE");
 int before=um.enabled;count_push(&s);
 assert(ran_count&&ran[ran_count-1].kind==XZ_UI_ENABLE&&ran[ran_count-1].index==XZ_UI_SMART&&ran[ran_count-1].value==1);
 (void)before;
 assert(pushes==2);
 /* STEM SHORTCUT PAD MODE on CONTROLS: old 3 pushes, now 3 (values pane) */
 fresh(&s);key(&s,XZ_SHELL_KEY_MENU);turn(&s,1);pushes=0;
 count_push(&s);cursor_to(&s,"STEM SHORTCUT PAD MODE");count_push(&s);turn(&s,1);count_push(&s);
 assert(ran_count&&ran[ran_count-1].kind==XZ_UI_STEM_PAGE&&pushes<=3);
 /* a game from EXTRAS: old MENU, push, push, link, game = 4 pushes, now 3 */
 fresh(&s);key(&s,XZ_SHELL_KEY_MENU);turn(&s,5);pushes=0;
 count_push(&s);cursor_to(&s,"GAMES");count_push(&s);cursor_to(&s,"BEAT ARCADE");count_push(&s);
 assert(ran_count&&ran[ran_count-1].kind==XZ_UI_ARCADE_OPEN&&pushes==3);
 /* a beat jump size: old MENU, push, push, section, pair, push-turn-push = 5, now 4 */
 fresh(&s);key(&s,XZ_SHELL_KEY_MENU);turn(&s,1);pushes=0;
 count_push(&s);cursor_to(&s,"BEAT JUMP");count_push(&s);cursor_to(&s,"PADS E / F");count_push(&s);turn(&s,1);count_push(&s);
 assert(ran_count&&ran[ran_count-1].kind==XZ_UI_JUMP_SIZE&&ran[ran_count-1].index==2&&pushes==4);
 puts("PASS press parity: GATE/SMART CUE 2 pushes (was 3), pad mode 3 (3), a game 3 (4), a beat jump size 4 (5)");
}

/* Touch: touch-down highlights, a second touch confirms; a value is picked the same way; a key types at
   once; a pad is held while touched. */
static void test_touch(void) {
 struct xz_shell s;fresh(&s);key(&s,XZ_SHELL_KEY_MENU);
 struct xz_shell_rect r=xz_shell_menu_row_rect(&s,&sm,2);
 assert(r.w&&r.h>=36);
 assert(tap(&s,r.x+40,r.y+r.h/2)==0&&!s.mods_view.on_tabs&&cursor_row(&s)==&menu.row[2]);
 size_t n=tap(&s,r.x+40,r.y+r.h/2);
 assert(n==1&&out[0].id==XZ_SHELL_MENU_ACTIVATE);
 /* the profile name: a key types on one tap */
 fresh(&s);
 xz_ui_menu_open(&ui,XZ_UI_EXTRAS);xz_ui_navigate(&ui,XZ_UI_PROFILE_SETUP);
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
 xz_ui_menu_open(&ui,XZ_UI_GENERAL);xz_ui_navigate(&ui,XZ_UI_XPAD);publish();
 xz_shell_mods_show(&s,&sm,1);cursor_to(&s,"LOOP STRIP");
 struct xz_shell_rect pad=xz_shell_menu_pad_rect();
 ran_count=0;
 assert(send(&s,(struct xz_shell_event){XZ_SHELL_TOUCH_DOWN,pad.x+pad.w-2,pad.y+2,0,0,-1})==1&&out[0].id==XZ_SHELL_MENU_PRESS);
 assert(ran_count==1&&ran[0].kind==XZ_UI_STRIP&&ran[0].phase==XZ_UI_PRESS&&ran[0].index==5);
 send(&s,(struct xz_shell_event){XZ_SHELL_TOUCH_MOVE,pad.x+2,pad.y+pad.h-2,0,0,-1});
 assert(ran_count==2&&ran[1].kind==XZ_UI_STRIP&&ran[1].phase==XZ_UI_MOVE&&ran[1].index==0);
 send(&s,(struct xz_shell_event){XZ_SHELL_TOUCH_UP,pad.x+2,pad.y+pad.h-2,0,0,-1});
 assert(ran_count==3&&ran[2].kind==XZ_UI_STRIP&&ran[2].phase==XZ_UI_RELEASE);
 assert(!others&&!refusals);
 puts("PASS touch: highlight then confirm, a key types on one tap, the loop strip is held while touched");
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
 memset(&wild,0x7f,sizeof wild);wild.root=XZ_UI_GENERAL;wild.page=XZ_UI_GENERAL;
 for(int i=0;i<XZ_SHELL_MENU_ROWS;i++){wild.row[i].label[sizeof wild.row[i].label-1]=0;wild.row[i].value[sizeof wild.row[i].value-1]=0;
  wild.row[i].help[sizeof wild.row[i].help-1]=0;wild.row[i].reason[sizeof wild.row[i].reason-1]=0;}
 for(int i=0;i<XZ_SHELL_MENU_CHOICES;i++)wild.choice[i].label[sizeof wild.choice[i].label-1]=0;
 for(int i=0;i<XZ_SHELL_MENU_LINES;i++)wild.line[i].text[sizeof wild.line[i].text-1]=0;
 for(int i=0;i<XZ_SHELL_MENU_SECTIONS;i++)wild.sections[i][sizeof wild.sections[i]-1]=0;
 for(int i=0;i<XZ_SHELL_MENU_STEPS;i++)wild.steps[i][sizeof wild.steps[i]-1]=0;
 wild.title[sizeof wild.title-1]=0;wild.path[sizeof wild.path-1]=0;wild.status[sizeof wild.status-1]=0;
 wild.field[sizeof wild.field-1]=0;wild.field_label[sizeof wild.field_label-1]=0;
 sm.menu=&wild;
 for(int layout=0;layout<2;layout++){
  wild.layout=(uint8_t)layout;
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
 test_reach_every_page();
 test_render_every_page();
 test_press_parity();
 test_touch();
 test_wild();
 puts("PASS menu");
 return 0;
}
