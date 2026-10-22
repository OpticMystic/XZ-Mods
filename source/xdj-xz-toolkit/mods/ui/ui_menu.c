/* SPDX-License-Identifier: MIT */
/* The MODS panel's open page as a flat menu (ui_menu.h). Rows come from the panel's own layout
   (xz_ui_layout), named and described by xz_ui_describe_widget, so a control the panel adds or drops
   shows up or goes here too. A few controls the panel draws as pictures (pad grids, beat-jump pairs,
   the profile list) become ordinary choices that run the panel's own actions. */
#include "ui_skin.h"
#include "ui_menu.h"
#include "stem_pads.h"
#include "wave_viewport.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

struct build {
 struct xz_shell_menu *p;
 const char *dead;            /* why the page's pad or sampler controls cannot work in this loader */
 const struct xz_ui *u;
 const struct xz_ui_model *m;
 int section, came_from;
 struct xz_ui_widget focus;
 int has_focus;
 int32_t focus_key;
};

static void copy(char *dst, size_t cap, const char *src) { snprintf(dst,cap,"%s",src?src:""); }
/* The panel's own copy names the computer's DJ software; this menu says what it is instead. The physical
   key keeps its label ("REKORDBOX BUTTON", "REKORDBOX KEY"). */
static void neutral(char *s, size_t cap) {
 static const char word[]="REKORDBOX";
 for(char *at=s;*at;){
  size_t i=0;
  while(word[i]&&at[i]&&(at[i]==word[i]||at[i]==word[i]+32))i++;
  if(word[i]){at++;continue;}
  const char *rest=at+i;
  if(!strncmp(rest," BUTTON",7)||!strncmp(rest," KEY",4)||!strncmp(rest," button",7)){at+=i;continue;}
  int lower=at[0]=='r';
  const char *with=lower?"the DJ software":"THE DJ SOFTWARE";
  size_t wl=strlen(with),len=strlen(s);
  if(len+wl-i+1>cap){at+=i;continue;}
  memmove(at+wl,rest,strlen(rest)+1);memcpy(at,with,wl);at+=wl;
 }
}
static int deck_of(const struct xz_ui *u) { return u->deck>=0&&u->deck<2?u->deck:0; }
static void section(struct build *b, const char *name) {
 struct xz_shell_menu *p=b->p;
 for(int i=0;i<p->section_count;i++)if(!strcmp(p->sections[i],name)){b->section=i;return;}
 if(p->section_count>=XZ_SHELL_MENU_SECTIONS){b->section=p->section_count-1;return;}
 copy(p->sections[p->section_count],sizeof p->sections[0],name);
 b->section=p->section_count++;
}
static int32_t widget_key(const struct xz_ui_widget *w) { return ((int32_t)w->kind<<16)^(int32_t)((w->index&0xfff)<<2)^(int32_t)(w->deck+1); }
static struct xz_shell_menu_target touch_of(const struct xz_ui_widget *w) {
 return (struct xz_shell_menu_target){(int16_t)w->kind,(int16_t)w->index,(int8_t)w->deck,XZ_UI_MENU_TOUCH,0};
}
static struct xz_shell_menu_target direct(int kind, int index, int value) {
 return (struct xz_shell_menu_target){(int16_t)kind,(int16_t)index,-1,XZ_UI_MENU_DIRECT,(int16_t)value};
}
static int is_focus(const struct build *b, const struct xz_ui_widget *w) {
 return b->has_focus&&w->kind==b->focus.kind&&w->index==b->focus.index&&w->deck==b->focus.deck;
}
static struct xz_shell_menu_row *add_row(struct build *b, int kind, int32_t key, const char *label) {
 struct xz_shell_menu *p=b->p;
 if(p->row_count>=XZ_SHELL_MENU_ROWS)return NULL;
 struct xz_shell_menu_row *r=&p->row[p->row_count++];
 memset(r,0,sizeof *r);
 r->kind=(uint8_t)kind;r->section=(uint8_t)b->section;r->available=1;
 r->first_choice=(int16_t)p->choice_count;r->current=-1;r->key=key;
 copy(r->label,sizeof r->label,label);
 return r;
}
static struct xz_shell_menu_choice *add_choice(struct build *b, struct xz_shell_menu_row *r, const char *label, struct xz_shell_menu_target t, int available) {
 struct xz_shell_menu *p=b->p;
 if(!r||p->choice_count>=XZ_SHELL_MENU_CHOICES||r->first_choice+r->choice_count!=p->choice_count)return NULL;
 struct xz_shell_menu_choice *c=&p->choice[p->choice_count++];
 memset(c,0,sizeof *c);
 copy(c->label,sizeof c->label,label);c->available=(uint8_t)!!available;c->target=t;
 r->choice_count++;
 return c;
}
static void line(struct build *b, int tone, const char *format, ...) __attribute__((format(printf,3,4)));
static void line(struct build *b, int tone, const char *format, ...) {
 struct xz_shell_menu *p=b->p;
 if(p->line_count>=XZ_SHELL_MENU_LINES)return;
 va_list args;va_start(args,format);
 vsnprintf(p->line[p->line_count].text,sizeof p->line[0].text,format,args);
 va_end(args);
 p->line[p->line_count++].tone=(uint8_t)tone;
}
static void steps(struct build *b, const char *const *names, int n, int at) {
 struct xz_shell_menu *p=b->p;
 p->step_count=(uint8_t)(n<XZ_SHELL_MENU_STEPS?n:XZ_SHELL_MENU_STEPS);
 for(int i=0;i<p->step_count;i++)copy(p->steps[i],sizeof p->steps[0],names[i]);
 p->step=(uint8_t)(at<0?0:at);
}

/* ---- the page chain: top page first, the open page last ---- */
static int chain_of(const struct xz_ui *u, int out[24]) {
 int n=0,first=u->history_count?u->history[0]:u->page;
 /* pages above the first known one, from its parents */
 int up[8],k=0,at=first;
 while(!xz_ui_page_is_top(at)&&k<8){int parent=xz_ui_page_parent(at);if(parent==at)break;up[k++]=parent;at=parent;}
 while(k>0&&n<20)out[n++]=up[--k];
 for(unsigned i=0;i<u->history_count&&i<16&&n<23;i++)out[n++]=u->history[i];
 out[n++]=u->page;
 return n;
}
int xz_ui_menu_root(const struct xz_ui *u, int *depth) {
 int chain[24],n=chain_of(u,chain);
 if(depth)*depth=n-1;
 return chain[0];
}
void xz_ui_menu_open(struct xz_ui *u, enum xz_ui_page top) {
 if(!u||(unsigned)top>=XZ_UI_PAGE_COUNT)return;
 u->page=top;u->history_count=0;u->capture=-1;u->notice[0]=0;u->focus_kind=XZ_UI_NONE;u->adjusting=0;u->profile_open=0;
}

/* ---- why a control cannot act now: one line ---- */
static const char *reason_of(const struct xz_ui *u, const struct xz_ui_model *m, const struct xz_ui_widget *w) {
 int deck=w->deck>=0?w->deck:deck_of(u);
 switch(w->kind){
 case XZ_UI_MUTE:case XZ_UI_BYPASS:case XZ_UI_LEVEL:return xz_ui_usb_next_step(xz_ui_usb_blocked(m,deck));
 case XZ_UI_ENABLE:if(w->index==XZ_UI_STEM)return "THIS USB LOADER HAS NO STEM ENGINE.";break;
 case XZ_UI_CONNECTION_ENABLE:case XZ_UI_DISCOVERY:
  return !m->connection.ready?"THE VJ.TOOLS CONNECTION SERVICE IS NOT RUNNING IN THIS LOADER.":"THIS LOADER CANNOT SWITCH THIS SETTING.";
 case XZ_UI_PC_TEST:case XZ_UI_PC_CONFIRM:return xz_ui_pc_reason_text(xz_ui_pc_blocked(m,deck));
 case XZ_UI_PROFILE_PICK:return m->dj&&m->dj->pending>=0?"STILL SWITCHING TO THE LAST PROFILE.":"THIS PROFILE FILE IS DAMAGED. SAVE IT AGAIN.";
 case XZ_UI_GROOVE_PAD:return "NO GROOVE IS LOADED ON THIS PAD.";
 case XZ_UI_HOTCUE_PAD:return "THIS LOADER DOES NOT READ PAD HOT CUES.";
 case XZ_UI_STRIP:case XZ_UI_HOLD:case XZ_UI_OVERDUB:case XZ_UI_VOLUME:case XZ_UI_SAMPLE_PAD:
  return "THIS USB LOADER HAS NO X-PAD SAMPLE ENGINE.";
 case XZ_UI_KEY_SHIFT:return "LOAD AN ANALYSED TRACK ON THIS DECK.";
 case XZ_UI_KEY_SYNC:return "BOTH DECKS NEED ANALYSED KEYS.";
 default:break;
 }
 if(xz_ui_profile_owns(w->kind)){const char *why=xz_ui_profile_blocked(u,m,w);if(why)return why;}
 if(w->requires&(XZ_UI_GATE|XZ_UI_SMART))return "LOAD A TRACK FROM USB ON THIS DECK.";
 return "NOT READY ON THIS DECK YET.";
}

/* ---- one control, one row ---- */
static struct xz_shell_menu_row *control_row(struct build *b, const struct xz_ui_widget *w, int kind, const char *label) {
 struct xz_ui_info info;xz_ui_describe_widget(b->u,b->m,w,&info);
 struct xz_shell_menu_row *r=add_row(b,kind,widget_key(w),label?label:info.title);
 if(!r)return NULL;
 r->available=(uint8_t)!!info.available;
 r->target=touch_of(w);
 /* the panel's catch-all line says how to use a control, not what it does: leave it out */
 if(info.body&&strcmp(info.body,"Push to use this control."))copy(r->help,sizeof r->help,info.body);
 if(!info.available)copy(r->reason,sizeof r->reason,reason_of(b->u,b->m,w));
 if(is_focus(b,w))b->focus_key=r->key;
 return r;
}
/* A control the panel toggles on each touch: OFF and ON, both its own touch. */
static struct xz_shell_menu_row *toggle_row(struct build *b, const struct xz_ui_widget *w, const char *label) {
 struct xz_shell_menu_row *r=control_row(b,w,XZ_SHELL_MENU_TOGGLE,label);
 if(!r)return NULL;
 struct xz_ui_look look=xz_ui_widget_look(b->u,b->m,w);
 add_choice(b,r,"OFF",touch_of(w),r->available);
 add_choice(b,r,"ON",touch_of(w),r->available);
 r->current=(int16_t)(look.selected?1:0);
 return r;
}
/* Strips the panel's link arrow ("STEM COLORS >"). */
static void link_label(const char *in, char *out, size_t cap) {
 copy(out,cap,in);
 size_t n=strlen(out);
 while(n&&(out[n-1]=='>'||out[n-1]==' '))out[--n]=0;
}

/* Widgets of one kind (one slot for REKORDBOX_ASSIGN and STEM_COLOR) gathered into one choice row. */
static int group_of(const struct xz_ui_widget *w) {
 switch(w->kind){
 case XZ_UI_KEY_FORMAT:case XZ_UI_WAVE_MODE:case XZ_UI_SET_INTERFACE:case XZ_UI_SET_THEME:case XZ_UI_STEM_PAGE:
 case XZ_UI_TAKEOVER_ASSIGN:case XZ_UI_SDK_CHOICE:case XZ_UI_DECK:case XZ_UI_RAM_DECK:case XZ_UI_SET_MODERN_THEME:
  return (int)w->kind<<4;
 case XZ_UI_REKORDBOX_ASSIGN:return ((int)w->kind<<4)|(w->index/XZ_UI_RB_CHOICES);
 case XZ_UI_STEM_COLOR:return ((int)w->kind<<4)|(w->index/XZ_STEM_COLOR_CHOICES);
 default:return 0;
 }
}
static const char *group_label(const struct build *b, const struct xz_ui_widget *w) {
 static const char *const stems[3]={"DRUMS COLOR","HARMONICS COLOR","VOCALS COLOR"};
 switch(w->kind){
 case XZ_UI_KEY_FORMAT:return "KEY FORMAT";
 case XZ_UI_WAVE_MODE:return "WAVEFORM";
 case XZ_UI_SET_INTERFACE:return "INTERFACE";
 case XZ_UI_SET_THEME:return "CLASSIC THEME";
 case XZ_UI_SET_MODERN_THEME:return "THEME";
 case XZ_UI_STEM_PAGE:return "STEM SHORTCUT PAD MODE";
 case XZ_UI_TAKEOVER_ASSIGN:return "SCREEN SWITCH KEY";
 case XZ_UI_SDK_CHOICE:return "START APPS";
 case XZ_UI_REKORDBOX_ASSIGN:return w->index<XZ_UI_RB_CHOICES?"REKORDBOX BUTTON":"SHIFT + REKORDBOX";
 case XZ_UI_STEM_COLOR:return stems[w->index/XZ_STEM_COLOR_CHOICES<3?w->index/XZ_STEM_COLOR_CHOICES:0];
 case XZ_UI_DECK:case XZ_UI_RAM_DECK:return b->u->page==XZ_UI_PC_STEMS?"DECK TO SET UP":"DECK";
 default:return "";
 }
}
static void choice_label(const struct build *b, const struct xz_ui_widget *w, char *out, size_t cap) {
 if(w->kind==XZ_UI_DECK&&b->u->page==XZ_UI_PC_STEMS)snprintf(out,cap,"DECK %d: %s",w->index+1,w->label?w->label:"");
 else if(w->kind==XZ_UI_DECK)snprintf(out,cap,"DECK %d",w->index+1);
 else if(w->kind==XZ_UI_SDK_CHOICE)copy(out,cap,w->index?"ON USB INSERT":"WHEN I TAP RUN");
 else copy(out,cap,w->label);
}
static void group_row(struct build *b, const struct xz_ui_widget *w, size_t n, int *used, size_t at) {
 int g=group_of(&w[at]),count=0;
 for(size_t i=at;i<n;i++)count+=group_of(&w[i])==g;
 struct xz_ui_info info;xz_ui_describe_widget(b->u,b->m,&w[at],&info);
 struct xz_shell_menu_row *r=add_row(b,count==2?XZ_SHELL_MENU_TOGGLE:XZ_SHELL_MENU_CHOICE,0x40000000|g,group_label(b,&w[at]));
 if(!r)return;
 if(w[at].kind==XZ_UI_STEM_COLOR)copy(r->help,sizeof r->help,"The colour for this stem on the pads, the stem rows and the STEMS waveform.");
 else if(w[at].kind==XZ_UI_DECK&&b->u->page!=XZ_UI_PC_STEMS)copy(r->help,sizeof r->help,"The deck the controls on this page act on.");
 else if(w[at].kind==XZ_UI_REKORDBOX_ASSIGN)copy(r->help,sizeof r->help,w[at].index<XZ_UI_RB_CHOICES?"What the REKORDBOX button does.":"What the REKORDBOX button does while SHIFT is held on either deck.");
 /* the panel describes each choice of these; the row says what the choice is about */
 else if(w[at].kind==XZ_UI_KEY_FORMAT)copy(r->help,sizeof r->help,"How the deck key strips write keys: as the player's own setting, as note names, or as wheel numbers.");
 else if(w[at].kind==XZ_UI_TAKEOVER_ASSIGN)copy(r->help,sizeof r->help,"Which control switches between the play screen and the VJ.Tools screen.");
 else if(w[at].kind==XZ_UI_WAVE_MODE)copy(r->help,sizeof r->help,"How the scrolling waveform is drawn.");
 else if(w[at].kind==XZ_UI_SET_INTERFACE)copy(r->help,sizeof r->help,"Who draws the player screen.");
 else if(w[at].kind==XZ_UI_SDK_CHOICE)copy(r->help,sizeof r->help,"How the apps on a USB start.");
 else copy(r->help,sizeof r->help,info.body);
 int any=0;
 for(size_t i=at;i<n;i++){
  if(group_of(&w[i])!=g)continue;
  used[i]=1;
  struct xz_ui_look look=xz_ui_widget_look(b->u,b->m,&w[i]);
  char label[40];choice_label(b,&w[i],label,sizeof label);
  struct xz_shell_menu_choice *c=add_choice(b,r,label,touch_of(&w[i]),look.available);
  if(!c)break;
  any|=look.available;
  if(w[i].kind==XZ_UI_STEM_COLOR){c->has_rgb=1;c->rgb=look.color&0xffffff;}
  if(look.selected&&r->current<0)r->current=(int16_t)(r->choice_count-1);
  if(is_focus(b,&w[i]))b->focus_key=r->key;
 }
 r->available=(uint8_t)any;
 if(!any)copy(r->reason,sizeof r->reason,reason_of(b->u,b->m,&w[at]));
}

/* ---- sections, per page ---- */
static const char *section_for(const struct build *b, const struct xz_ui_widget *w) {
 int page=b->u->page,k=w->index;
 switch(page){
 case XZ_UI_GENERAL:
  if((w->kind==XZ_UI_ENABLE||w->kind==XZ_UI_LOCAL_STEM_SETUP_OPEN)&&k==XZ_UI_STEM)return "STEMS";
  if(w->kind==XZ_UI_FEATURE_LINK&&(k==XZ_UI_PC_STEMS||k==XZ_UI_STEM_COLORS||k==XZ_UI_CONTROLS))return "STEMS";
  if(w->kind==XZ_UI_JUMP_ENABLE||(w->kind==XZ_UI_FEATURE_LINK&&k==XZ_UI_BEAT_JUMP))return "BEAT JUMP";
  if(w->kind==XZ_UI_FEATURE_LINK&&(k==XZ_UI_METRICS_SETUP||k==XZ_UI_SETTINGS))return "SYSTEM";
  return "CUES AND PADS";
 case XZ_UI_CONTROLS:
  if(w->kind==XZ_UI_STEMS_OVERLAY||w->kind==XZ_UI_FOUR_WAVE_ORDER)return "PLAY SCREEN";
  if(w->kind==XZ_UI_CONTROL_SECTION)return "MORE CONTROLS";
  return "STEM SHORTCUTS";
 case XZ_UI_SETTINGS:
  if(w->kind==XZ_UI_PANEL&&(k==XZ_UI_STEMS||k==XZ_UI_XPAD))return "PADS";
  if(w->kind==XZ_UI_PANEL&&k==XZ_UI_RAM_SETUP)return "MEMORY";
  return "KEY";
 case XZ_UI_EXTRAS:return "EXTRAS";
 case XZ_UI_CONNECTION:return w->kind==XZ_UI_TAKEOVER_TOGGLE||w->kind==XZ_UI_TAKEOVER_ASSIGN?"SCREEN":"CONNECTION";
 case XZ_UI_XPAD:return w->kind==XZ_UI_SAMPLE_PAD?"SAMPLE PADS":"LOOP";
 case XZ_UI_STEMS:return w->kind==XZ_UI_DECK?"":"PADS";
 case XZ_UI_APPS:return w->kind==XZ_UI_SDK_RUN||w->kind==XZ_UI_SDK_STOP||w->kind==XZ_UI_SDK_FIX||w->kind==XZ_UI_SDK_SAVE?"APPS ON USB":"MANAGE";
 case XZ_UI_PROFILE_EDIT:return w->kind==XZ_UI_LOGO_PICK&&k>=0?"IMAGES ON USB":"";
 default:return "";
 }
}
/* Sections in the order a page lists them. */
static void page_sections(struct build *b) {
 static const char *const general[]={"STEMS","CUES AND PADS","BEAT JUMP","SYSTEM",NULL};
 static const char *const controls[]={"STEM SHORTCUTS","PLAY SCREEN","MORE CONTROLS",NULL};
 static const char *const advanced[]={"KEY","PADS","MEMORY",NULL};
 static const char *const extras[]={"DJ PROFILE","EXTRAS",NULL};
 static const char *const apps[]={"APPS ON USB","MANAGE",NULL};
 static const char *const connection[]={"SCREEN","CONNECTION",NULL};
 static const char *const xpad[]={"LOOP","SAMPLE PADS",NULL};
 const char *const *list=NULL;
 switch(b->u->page){
 case XZ_UI_GENERAL:list=general;break;
 case XZ_UI_CONTROLS:list=controls;break;
 case XZ_UI_SETTINGS:list=advanced;break;
 case XZ_UI_EXTRAS:list=extras;break;
 case XZ_UI_APPS:list=apps;break;
 case XZ_UI_CONNECTION:list=connection;break;
 case XZ_UI_XPAD:list=xpad;break;
 default:break;
 }
 if(list)for(int i=0;list[i];i++)section(b,list[i]);
}

/* ---- the controls the panel draws as pictures ---- */
static const int jump_order[]={0,1,2,3,4,5,6,8,9};
static void beat_jump_rows(struct build *b) {
 const struct xz_ui_model *m=b->m;
 static const char *const pairs[4]={"PADS A / B","PADS C / D","PADS E / F","PADS G / H"};
 section(b,"");
 struct xz_shell_menu_row *on=add_row(b,XZ_SHELL_MENU_TOGGLE,0x50000001,"CUSTOM JUMPS ON PAGE 1");
 if(on){
  copy(on->help,sizeof on->help,"Page 1 uses the sizes below instead of the player's own. SHIFT + BEAT JUMP page 2 always uses them.");
  add_choice(b,on,"OFF",direct(XZ_UI_JUMP_ENABLE,0,0),1);add_choice(b,on,"ON",direct(XZ_UI_JUMP_ENABLE,0,1),1);
  on->current=(int16_t)!!m->jump.enabled;
 }
 for(int page=0;page<2;page++){
  section(b,page?"PAGE 2 (SHIFT + BEAT JUMP)":"PAGE 1");
  for(int pair=0;pair<4;pair++){
   int slot=page*4+pair,size=m->jump.sizes[slot];
   struct xz_shell_menu_row *r=add_row(b,XZ_SHELL_MENU_CHOICE,0x50000100|slot,pairs[pair]);
   if(!r)return;
   copy(r->help,sizeof r->help,"How far this pair jumps. The first pad jumps back, the second forward. 4 beats are 1 bar.");
   for(size_t i=0;i<sizeof jump_order/sizeof *jump_order;i++){
    add_choice(b,r,xz_jump_label(jump_order[i]),direct(XZ_UI_JUMP_SIZE,slot,jump_order[i]),1);
    if(xz_jump_beats(jump_order[i])==xz_jump_beats(size)&&r->current<0)r->current=(int16_t)i;
   }
  }
 }
}
static void stem_pad_row(struct build *b, const struct xz_ui_widget *w) {
 struct xz_shell_menu_row *r=add_row(b,XZ_SHELL_MENU_TOGGLE,0x50000200,"STEM SHORTCUT PADS");
 if(!r)return;
 copy(r->help,sizeof r->help,"Which four pads carry the stem mutes in the chosen pad mode. The other pads keep their normal action.");
 add_choice(b,r,"PADS A-D",direct(XZ_UI_STEM_BANK,0,0),1);add_choice(b,r,"PADS E-H",direct(XZ_UI_STEM_BANK,1,0),1);
 r->current=(int16_t)(b->m->stem_bank?1:0);
 if(is_focus(b,w))b->focus_key=r->key;
}
static void cycle_row(struct build *b, const struct xz_ui_widget *w) {
 /* This menu is MODERN's, so the layout row is MODERN's own (modern_stems), in the STEMS button's order. */
 int layout=w->kind==XZ_UI_STEMS_OVERLAY,count=layout?XZ_MODERN_STEMS_COUNT:XZ_STEM_ORDER_COUNT;
 struct xz_shell_menu_row *r=control_row(b,w,XZ_SHELL_MENU_CHOICE,layout?"STEM BUTTONS":"STEM ORDER");
 if(!r)return;
 for(int i=0;i<count;i++){
  int value=layout?xz_modern_stems_order[i]:i;
  struct xz_shell_menu_target t={(int16_t)w->kind,(int16_t)value,-1,XZ_UI_MENU_CYCLE,0};
  add_choice(b,r,layout?xz_modern_stems_name(value):xz_stem_order_name(i),t,r->available);
 }
 int now=layout?xz_modern_stems_position(b->m->modern_stems):b->m->stem_order;
 r->current=(int16_t)(now>=0&&now<count?now:-1);
 if(layout)copy(r->help,sizeof r->help,"Where the stem buttons sit on the play screen: small beside the hot cues, rows by the waveforms, or hidden. STEMS steps through them.");
 if(!layout)copy(r->help,sizeof r->help,"The left-to-right stem order of the play screen rows and pads A, B and C. Bass stays on pad D or H.");
}
static void volume_row(struct build *b, const struct xz_ui_widget *w) {
 struct xz_shell_menu_row *r=control_row(b,w,XZ_SHELL_MENU_CHOICE,"SAMPLE VOLUME");
 if(!r)return;
 int deck=deck_of(b->u);
 for(int i=0;i<=10;i++){
  char label[12];snprintf(label,sizeof label,"%d %%",i*10);
  struct xz_shell_menu_target t={XZ_UI_VOLUME,(int16_t)w->index,(int8_t)deck,XZ_UI_MENU_SLIDE,(int16_t)(i*10)};
  add_choice(b,r,label,t,r->available);
 }
 float v=b->m->deck[deck].sample_volume;
 r->current=(int16_t)(v<0?0:v>1?10:(int)(v*10.f+.5f));
}
static void profile_choice_rows(struct build *b) {
 const struct xz_ui_dj *dj=b->m->dj;
 if(!dj)return;
 section(b,"DJ PROFILE");
 int count=dj->count<XZ_UI_DJ_ENTRIES?dj->count:XZ_UI_DJ_ENTRIES;
 if(count>0){
  struct xz_shell_menu_row *r=add_row(b,XZ_SHELL_MENU_CHOICE,0x50000300,"ACTIVE DJ PROFILE");
  if(r){
   copy(r->help,sizeof r->help,"Your DJ name and logo from the USB. VJ software shows the profile of the USB playing on the master deck. Picking one writes only that USB.");
   for(int i=0;i<count&&r;i++){
    const struct xz_ui_dj_entry *e=&dj->entry[i];
    char label[40];snprintf(label,sizeof label,"%.28s (USB %d)",e->name&&e->name[0]?e->name:"---",e->usb+1);
    struct xz_shell_menu_choice *c=add_choice(b,r,label,direct(XZ_UI_PROFILE_PICK,i,0),!e->damaged&&dj->pending<0);
    if(c&&e->has_accent){c->has_rgb=1;c->rgb=e->accent&0xffffff;}
   }
   r->current=(int16_t)(dj->current>=0&&dj->current<count?dj->current:-1);
   if(dj->pending>=0){r->available=0;copy(r->reason,sizeof r->reason,"STILL SWITCHING TO THE LAST PROFILE.");}
  }
 }
 struct xz_shell_menu_row *page=add_row(b,XZ_SHELL_MENU_LINK,0x50000301,count?"DJ PROFILE":"SET UP DJ PROFILE");
 if(page){
  page->target=(struct xz_shell_menu_target){XZ_UI_PANEL,XZ_UI_PROFILE_SETUP,-1,XZ_UI_MENU_PAGE,0};
  copy(page->help,sizeof page->help,xz_ui_page_body(XZ_UI_PROFILE_SETUP));
 }
}

/* ---- the page's live lines and setup steps ---- */
static int tone_for(enum xz_ui_usb_reason why) {
 return why==XZ_UI_USB_WHY_ERROR||why==XZ_UI_USB_WHY_ENGINE?XZ_SHELL_TONE_ALERT:why==XZ_UI_USB_WHY_NONE?XZ_SHELL_TONE_GOOD:XZ_SHELL_TONE_PLAIN;
}
static void page_text(struct build *b) {
 const struct xz_ui *u=b->u;const struct xz_ui_model *m=b->m;
 int deck=deck_of(u);const struct xz_ui_deck *d=&m->deck[deck];
 const char *track=d->track&&*d->track?d->track:NULL;
 switch(u->page){
 case XZ_UI_FEATURE_SETUP:{
  static const char *const names[4]={"DETECT","CHOOSE DECK","ENABLE","TEST"};
  static const char *const what[4]={"Detect the feature on a loaded USB deck.","Choose deck 1 or deck 2.",
   "Enable the feature with its saved setting.","Test it on this deck, then confirm."};
  int step=u->feature_step>=0&&u->feature_step<4?u->feature_step:0,cap=u->feature_cap;
  steps(b,names,4,step);
  line(b,XZ_SHELL_TONE_HEAD,"%s",cap==XZ_UI_GATE?"GATE CUE":cap==XZ_UI_SMART?"SMART CUE":cap==XZ_UI_PREVIEW?"HOT-CUE PREVIEW":"X-PAD ENGINE");
  line(b,XZ_SHELL_TONE_PLAIN,"%s",what[step]);
  line(b,XZ_SHELL_TONE_PLAIN,"DECK %d: %s",deck+1,track?track:"NO TRACK LOADED");
  line(b,(d->ready&(uint32_t)cap)?XZ_SHELL_TONE_GOOD:XZ_SHELL_TONE_DIM,"%s",(d->ready&(uint32_t)cap)?"FEATURE DETECTED":"LOAD A USB TRACK. IF IT STAYS MISSING, UPDATE THE USB LOADER IN XZ MODS.");
  if(step==3)line(b,XZ_SHELL_TONE_DIM,"%s",cap==XZ_UI_GATE?"Hold and release a hot cue: playback returns to it.":cap==XZ_UI_SMART?"Recall a hot cue: the cue point follows it.":
   cap==XZ_UI_PREVIEW?"Hold a hot cue while paused and listen for its preview.":"Open X-PAD and play a loaded sample.");
  break;}
 case XZ_UI_LOCAL_STEM_SETUP:{
  static const char *const names[3]={"CHECK ENGINE","TURN ON","PREPARED TRACK"};
  enum xz_ui_usb_reason why=xz_ui_usb_blocked(m,deck);
  int step=why==XZ_UI_USB_WHY_ENGINE?(u->stem_setup_step==1?1:0):why==XZ_UI_USB_WHY_OFF?1:why==XZ_UI_USB_WHY_NONE?3:2;
  steps(b,names,3,step);
  line(b,tone_for(why),"DECK %d: %s",deck+1,xz_ui_usb_reason_text(why));
  line(b,XZ_SHELL_TONE_PLAIN,"%s",track?track:"NO TRACK LOADED ON THIS DECK");
  if(why==XZ_UI_USB_WHY_ERROR)line(b,XZ_SHELL_TONE_ALERT,"%s",d->stem_error&&*d->stem_error?d->stem_error:"THE STEM SET COULD NOT BE READ");
  else if(d->stem_format&&*d->stem_format)line(b,XZ_SHELL_TONE_PLAIN,"FOUND: %s",d->stem_format);
  if(xz_ui_stem_count(m,deck)==4)line(b,XZ_SHELL_TONE_DIM,"%s","4-stem sets carry no stem waveform: STEMS shows 3-BAND.");
  break;}
 case XZ_UI_METRICS_SETUP:{
  static const char *const names[4]={"DETECT","SENSORS","CONNECT","LIVE VERIFY"};
  static const char *const what[4]={"Detect CPU, memory and the SoC sensor.","Use the XZ's own read-only counters.",
   "Connect the background sampler. No computer needed.","Check that fresh readings arrive."};
  int step=u->metrics_step>=0&&u->metrics_step<4?u->metrics_step:0;
  steps(b,names,4,step);
  line(b,XZ_SHELL_TONE_PLAIN,"%s",what[step]);
  line(b,XZ_SHELL_TONE_PLAIN,"CPU %s / RAM %s / TEMP %s",m->metrics.valid&1?"FOUND":"WAITING",m->metrics.valid&2?"FOUND":"WAITING",m->metrics.valid&4?"FOUND":"NOT EXPOSED");
  line(b,XZ_SHELL_TONE_DIM,"RAM USED %u / TOTAL %u / FREE %u MiB",m->metrics.used_mib,m->metrics.total_mib,m->metrics.free_mib);
  line(b,XZ_SHELL_TONE_DIM,"%s","CPU 100% is all four cores busy. 80C turns yellow, 90C flashes red.");
  break;}
 case XZ_UI_PC_STEMS:{
  static const char *const names[4]={"DETECT","MAP STEMS","LIVE TEST","CONFIRM"};
  static const char *const what[5]={
   "Connect the XZ to the computer by USB and open the DJ software in performance mode. DETECT checks this deck plays from the computer.",
   "Import the XZ stems map (XZ-onscreen-stems-4deck.midi.csv) in the DJ software, then turn stems on for the loaded track.",
   "Load a track on this deck in the DJ software. The test sends one DRUMS switch: watch the drums stem there.",
   "Did the drums stem switch on the computer? NO goes back to the test.",
   "The stem buttons now switch the computer's stems on this deck."};
  const struct xz_ui_pc_stems *pc=&d->pc_stems;
  int step=pc->setup_step>=XZ_UI_PC_STEP_DETECT&&pc->setup_step<=XZ_UI_PC_STEP_DONE?pc->setup_step:0;
  enum xz_ui_pc_reason why=xz_ui_pc_blocked(m,deck);
  steps(b,names,4,step);
  line(b,XZ_SHELL_TONE_PLAIN,"DECK %d SOURCE: %s",deck+1,pc->source==XZ_UI_SOURCE_PC?"COMPUTER":pc->source==XZ_UI_SOURCE_LOCAL?"XZ USB":"NOT CONFIRMED");
  line(b,pc->source==XZ_UI_SOURCE_PC&&!pc->connected?XZ_SHELL_TONE_ALERT:XZ_SHELL_TONE_PLAIN,"COMPUTER LINK: %s",pc->source!=XZ_UI_SOURCE_PC?"---":pc->connected?"CONNECTED":"LOST, NOTHING SENT");
  line(b,why==XZ_UI_PC_WHY_NONE?XZ_SHELL_TONE_GOOD:XZ_SHELL_TONE_PLAIN,"STEM SWITCHING: %s",why==XZ_UI_PC_WHY_NONE?"TESTED":"NOT TESTED");
  line(b,XZ_SHELL_TONE_PLAIN,"%s",what[step]);
  if(why!=XZ_UI_PC_WHY_NONE&&pc->reason)line(b,XZ_SHELL_TONE_ALERT,"%s",pc->reason);
  break;}
 case XZ_UI_RAM_SETUP:{
  const struct xz_ui_ram *r=&d->ram;
  unsigned state=(unsigned)r->state<6?(unsigned)r->state:0;
  static const char *const hints[6]={"Check again. If it stays here, rebuild the USB loader with the current XZ Mods app.",
   "Load a USB track on this deck, then load it into RAM.","Keep the USB connected until this says ORIGINAL AUDIO IN RAM.",
   "USB still connected. RELEASE frees the memory.","Retry. If it fails again, reload the track from USB.","Load another track to release this RAM copy."};
  line(b,state==XZ_UI_RAM_ERROR?XZ_SHELL_TONE_ALERT:state==XZ_UI_RAM_READY?XZ_SHELL_TONE_GOOD:XZ_SHELL_TONE_HEAD,"DECK %d: %s",deck+1,xz_ui_ram_state_text((enum xz_ui_ram_state)state));
  line(b,XZ_SHELL_TONE_PLAIN,"SOURCE: %s",track?track:"NO TRACK REPORTED");
  line(b,XZ_SHELL_TONE_PLAIN,"%s",r->message[0]?r->message:hints[state]);
  if(r->bytes)line(b,XZ_SHELL_TONE_DIM,"%.1f / %.1f MB COPIED",(double)r->copied/1048576.0,(double)r->bytes/1048576.0);
  line(b,XZ_SHELL_TONE_DIM,"%s","One track per deck, original audio only. Stems and 3-band waves still read the USB.");
  break;}
 case XZ_UI_CONNECTION:{
  const struct xz_ui_connection *v=&m->connection;
  line(b,v->ready&&v->connected?XZ_SHELL_TONE_GOOD:XZ_SHELL_TONE_PLAIN,"STATUS: %s",!v->ready?"NOT AVAILABLE":v->connected?"CONNECTED":v->enabled?"WAITING FOR VJ.TOOLS":"DISABLED");
  line(b,XZ_SHELL_TONE_PLAIN,"DEVICE IP: %s",v->device_ip&&*v->device_ip?v->device_ip:"NOT REPORTED");
  if(v->port)line(b,XZ_SHELL_TONE_PLAIN,"PORT: %u",v->port);
  if(v->ready&&v->connected&&v->stats_valid)line(b,XZ_SHELL_TONE_PLAIN,"RECEIVED: %.1f HZ",(double)v->frame_hz);
  line(b,m->fb_takeover?XZ_SHELL_TONE_GOOD:XZ_SHELL_TONE_DIM,"%s",m->fb_takeover?"THE COMPUTER SCREEN IS SHOWING":"THE PLAY SCREEN IS SHOWING");
  break;}
 case XZ_UI_DOOM:{
  static const char *const keys[7]={"JOG: TURN","PLAY: WALK FORWARD","CUE: SHOOT","PAD A / B: FORWARD / BACK","PAD C / D: SHOOT / USE","PAD E / F: STRAFE","PAD G / H: ENTER / MENU"};
  line(b,m->doom_ready?XZ_SHELL_TONE_GOOD:XZ_SHELL_TONE_ALERT,"%s",m->doom_ready?"USB GAME FILES READY":"USB GAME SETUP NEEDED");
  for(int k=0;k<7;k++)line(b,XZ_SHELL_TONE_PLAIN,"%s",keys[k]);
  if(m->doom.selection[0])line(b,XZ_SHELL_TONE_DIM,"%s",m->doom.selection);
  line(b,XZ_SHELL_TONE_DIM,"%s","Exit restores the DJ screen. Game sound is off.");
  break;}
 case XZ_UI_DOOM_SETUP:
  line(b,XZ_SHELL_TONE_PLAIN,"%s","1 The Doom engine comes with the USB loader.");
  line(b,XZ_SHELL_TONE_PLAIN,"%s","2 Choose a base game and an optional map from the USB (WADS or VJTOOLS/DOOM).");
  line(b,XZ_SHELL_TONE_PLAIN,"%s","3 Check the USB, then start Doom to verify.");
  line(b,m->doom_ready?XZ_SHELL_TONE_GOOD:XZ_SHELL_TONE_DIM,"%s",m->doom_ready?"ENGINE AND GAME DATA DETECTED":"PREPARE A DOOM USB WITH THE XZ MODS APP");
  break;
 case XZ_UI_DOOM_FILES:
  if(m->doom.selection[0])line(b,XZ_SHELL_TONE_HEAD,"%s",m->doom.selection);
  if(!m->doom.count)line(b,XZ_SHELL_TONE_PLAIN,"%s",m->doom.scanning?"SEARCHING THE USB PORTS":"PUT .WAD FILES ON A USB, THEN RESCAN");
  break;
 case XZ_UI_MYHOUSE_SETUP:
  line(b,XZ_SHELL_TONE_PLAIN,"%s","1 Detect the native GZDoom engine.");
  line(b,XZ_SHELL_TONE_PLAIN,"%s","2 Put the Doom II data and MYHOUSE.PK3 on the USB.");
  line(b,XZ_SHELL_TONE_PLAIN,"%s","3 Check the engine, then start the game to verify.");
  line(b,XZ_SHELL_TONE_DIM,"%s","The classic Doom engine cannot load this mod.");
  break;
 case XZ_UI_UPDATES:
  line(b,XZ_SHELL_TONE_HEAD,"%s",m->update.status[0]?m->update.status:"NO UPDATE CHECKED YET");
  line(b,XZ_SHELL_TONE_PLAIN,"%s","Downloads a signed XZ mod runtime to the USB. It starts at the next power cycle; music stays.");
  line(b,XZ_SHELL_TONE_DIM,"%s","Mods only. The original USB loader is the recovery.");
  break;
 case XZ_UI_UPDATE_SETUP:
  line(b,XZ_SHELL_TONE_PLAIN,"%s","1 Use the updated XZ Mods USB loader.");
  line(b,XZ_SHELL_TONE_PLAIN,"%s","2 Start network updates in the desktop app.");
  line(b,XZ_SHELL_TONE_PLAIN,"%s","3 Connect the XZ to that network.");
  if(m->update.status[0])line(b,XZ_SHELL_TONE_HEAD,"%s",m->update.status);
  break;
 case XZ_UI_STEMS:case XZ_UI_XPAD:
  line(b,XZ_SHELL_TONE_HEAD,"%s",track?track:"NO TRACK LOADED");
  if(d->bpm>0)line(b,XZ_SHELL_TONE_PLAIN,"DECK %d / %.1f BPM",deck+1,(double)d->bpm);
  if(u->page==XZ_UI_XPAD)line(b,XZ_SHELL_TONE_DIM,"LOOP %s BEATS / %+.1f KEY",xz_ui_lengths[d->loop_index>=0&&d->loop_index<6?d->loop_index:0],(double)d->pitch);
  break;
 case XZ_UI_SETTINGS:case XZ_UI_KEY_SETUP:
  for(int k=0;k<2;k++)line(b,k==deck?XZ_SHELL_TONE_HEAD:XZ_SHELL_TONE_PLAIN,"DECK %d KEY: %s%s",k+1,m->deck[k].key_readout[0]?m->deck[k].key_readout:"---",m->deck[k].key_synced?" (KEY SYNC)":"");
  break;
 case XZ_UI_BROWSE_SETUP:
  line(b,XZ_SHELL_TONE_DIM,"%s","Used by the deck key strips. Saved to your USB profile.");
  break;
 case XZ_UI_CONTROLS:
  if(m->stem_page==3&&m->jump.enabled)line(b,XZ_SHELL_TONE_ALERT,"%s","BEAT JUMP HAS PRIORITY ON ITS PADS: CHOOSE ANOTHER PAD MODE.");
  if(!(m->enabled&XZ_UI_STEM))line(b,XZ_SHELL_TONE_ALERT,"%s","STEM AUDIO IS OFF.");
  break;
 case XZ_UI_APPS_SETUP:{
  static const char *const names[XZ_SDK_UI_STEPS]={"DETECT","FOLDERS","INSTALL","LIVE TEST"};
  static const char *const what[XZ_SDK_UI_STEPS]={"Find the app manager and the apps on the USB.","Choose how apps start.",
   "Copy apps to a USB with the XZ Mods app.","Run one app to prove it works."};
  int step=u->sdk_step>=0&&u->sdk_step<XZ_SDK_UI_STEPS?u->sdk_step:0;
  steps(b,names,XZ_SDK_UI_STEPS,step);
  line(b,XZ_SHELL_TONE_PLAIN,"%s",what[step]);
  line(b,m->sdk.host==XZ_SDK_UI_RUNNING?XZ_SHELL_TONE_GOOD:XZ_SHELL_TONE_ALERT,"APP MANAGER: %s",m->sdk.host==XZ_SDK_UI_RUNNING?"RUNNING":"NOT RUNNING");
  line(b,XZ_SHELL_TONE_PLAIN,"APPS FOUND: %u",m->sdk.snapshot.count<XZ_SDK_PACKAGES?m->sdk.snapshot.count:XZ_SDK_PACKAGES);
  break;}
 case XZ_UI_PROFILE_SETUP:{
  int shown=xz_ui_profile_shown(u,m);
  if(shown<0){line(b,XZ_SHELL_TONE_PLAIN,"%s","No DJ profile on the USB sticks yet.");break;}
  const struct xz_ui_dj_entry *e=&m->dj->entry[shown];
  line(b,XZ_SHELL_TONE_HEAD,"%s",e->name&&e->name[0]?e->name:"---");
  if(e->tagline&&e->tagline[0])line(b,XZ_SHELL_TONE_PLAIN,"%s",e->tagline);
  line(b,e->stick_active?XZ_SHELL_TONE_GOOD:XZ_SHELL_TONE_DIM,"USB %d / %s",e->usb+1,e->stick_active?"ACTIVE ON ITS USB":"NOT ACTIVE");
  if(e->damaged)line(b,XZ_SHELL_TONE_ALERT,"%s","THIS PROFILE FILE CANNOT BE READ.");
  else if(e->locked)line(b,XZ_SHELL_TONE_DIM,"%s","A single profile card: nothing to switch.");
  break;}
 case XZ_UI_PROFILE_EDIT:{
  static const char *const names[4]={"NAME","LOGO","TAGLINE","REVIEW"};
  steps(b,names,4,u->edit_step);
  if(u->edit_step==XZ_UI_EDIT_REVIEW||u->edit_step==XZ_UI_EDIT_LOGO){
   line(b,XZ_SHELL_TONE_HEAD,"%s",u->edit_name[0]?u->edit_name:"NO NAME YET");
   if(u->edit_tagline[0])line(b,XZ_SHELL_TONE_PLAIN,"%s",u->edit_tagline);
   const struct xz_ui_dj *dj=m->dj;
   if(u->edit_logo==XZ_UI_LOGO_NONE)line(b,XZ_SHELL_TONE_DIM,"%s","LOGO: NONE");
   else if(u->edit_logo==XZ_UI_LOGO_KEEP)line(b,XZ_SHELL_TONE_DIM,"%s","LOGO: THE CURRENT ONE");
   else if(dj&&u->edit_logo>=0&&u->edit_logo<dj->image_count)line(b,XZ_SHELL_TONE_DIM,"LOGO: %.70s",dj->images[u->edit_logo]);
   line(b,XZ_SHELL_TONE_DIM,"SAVES TO USB %d",u->edit_usb+1);
  }
  break;}
 default:break;
 }
}

/* ---- the page ---- */
static int back_link(const struct build *b, const struct xz_ui_widget *w) {
 return w->kind==XZ_UI_PANEL&&w->index==b->came_from;
}
/* Controls whose engine this loader lacks cannot be fixed from MODS: they leave the page, which says why
   in one line (and offers the setup that checks for the engine, where there is one). */
static const char *dead_reason(const struct build *b, const struct xz_ui_widget *a) {
 switch(a->kind){
 case XZ_UI_GROOVE_PAD:case XZ_UI_HOTCUE_PAD:case XZ_UI_STRIP:case XZ_UI_HOLD:case XZ_UI_OVERDUB:case XZ_UI_VOLUME:case XZ_UI_SAMPLE_PAD:
  {int deck=a->deck>=0?a->deck:deck_of(b->u);if((b->m->deck[deck].ready&a->requires)==a->requires)return NULL;}
  return a->kind==XZ_UI_GROOVE_PAD||a->kind==XZ_UI_HOTCUE_PAD?"THIS USB LOADER HAS NO GROOVE PAD ENGINE YET.":"THIS USB LOADER HAS NO X-PAD SAMPLE ENGINE.";
 case XZ_UI_CONNECTION_ENABLE:case XZ_UI_DISCOVERY:
  return b->m->connection.ready?NULL:"THE VJ.TOOLS CONNECTION SERVICE IS NOT RUNNING IN THIS LOADER.";
 default:return NULL;
 }
}
static void control(struct build *b, const struct xz_ui_widget *w, size_t n, int *used, size_t i) {
 const struct xz_ui *u=b->u;const struct xz_ui_widget *a=&w[i];
 char label[48];
 const char *dead=dead_reason(b,a);
 if(dead){b->dead=dead;return;}
 section(b,section_for(b,a));
 if(group_of(a)){group_row(b,w,n,used,i);return;}
 switch(a->kind){
 case XZ_UI_PANEL:case XZ_UI_FEATURE_LINK:{
  link_label(a->label,label,sizeof label);
  struct xz_shell_menu_row *r=control_row(b,a,XZ_SHELL_MENU_LINK,label[0]?label:xz_ui_page_title(a->index));
  if(r)copy(r->help,sizeof r->help,xz_ui_page_body(a->index));
  return;}
 case XZ_UI_CONTROL_SECTION:{
  static const char *const names[4]={"STEM SHORTCUTS","BEAT JUMP","PC STEMS","BUTTONS"};
  control_row(b,a,XZ_SHELL_MENU_LINK,names[a->index>=0&&a->index<4?a->index:0]);
  return;}
 case XZ_UI_FEATURE_SETUP_OPEN:case XZ_UI_LOCAL_STEM_SETUP_OPEN:case XZ_UI_METRICS_SETUP_OPEN:
  control_row(b,a,XZ_SHELL_MENU_LINK,a->label);return;
 case XZ_UI_ENABLE:
  toggle_row(b,a,a->index==XZ_UI_STEM?"STANDALONE STEMS":a->label);return;
 case XZ_UI_JUMP_ENABLE:toggle_row(b,a,"CUSTOM JUMPS ON PAGE 1");return;
 case XZ_UI_FOUR_WAVE_ORDER:{
  struct xz_shell_menu_row *r=control_row(b,a,XZ_SHELL_MENU_CHOICE,"FOUR-DECK WAVE ORDER");
  if(r){
   add_choice(b,r,"1 2 3 4 / NUMERIC",direct(XZ_UI_FOUR_WAVE_ORDER,0,0),1);
   add_choice(b,r,"3 1 2 4 / MIXER",direct(XZ_UI_FOUR_WAVE_ORDER,0,1),1);
   r->current=(int16_t)!!b->m->four_wave_mixer;
   copy(r->help,sizeof r->help,"Shared waveform order in Classic and Modern, until reboot. Audio channels and jog control sides stay unchanged.");
  }
  return;
 }
 case XZ_UI_PAD_FEEDBACK:case XZ_UI_KEY_FEEDBACK:case XZ_UI_HOLD:case XZ_UI_OVERDUB:case XZ_UI_TAKEOVER_TOGGLE:
 case XZ_UI_CONNECTION_ENABLE:case XZ_UI_DISCOVERY:case XZ_UI_BROWSE_KEY_PREFIX:case XZ_UI_BROWSE_TRAFFIC:
 case XZ_UI_SHIFT_KEYSYNC:case XZ_UI_KEY_SYNC:case XZ_UI_METRICS_TOGGLE:
  toggle_row(b,a,a->label);return;
 case XZ_UI_SDK_AUTOLOAD:toggle_row(b,a,"AUTOLOAD");return;
 case XZ_UI_STEMS_OVERLAY:case XZ_UI_STEM_ORDER:cycle_row(b,a);return;
 case XZ_UI_STEM_PAD_CONFIG:
  for(size_t k=i;k<n;k++)if(w[k].kind==XZ_UI_STEM_PAD_CONFIG)used[k]=1;
  stem_pad_row(b,a);return;
 case XZ_UI_VOLUME:volume_row(b,a);return;
 case XZ_UI_GROOVE_PAD:case XZ_UI_HOTCUE_PAD:case XZ_UI_SAMPLE_PAD:{
  snprintf(label,sizeof label,"PAD %c",'A'+(a->index&7));
  struct xz_shell_menu_row *r=control_row(b,a,XZ_SHELL_MENU_PAD,label);
  if(r)copy(r->value,sizeof r->value,a->kind==XZ_UI_GROOVE_PAD?"GROOVE":a->kind==XZ_UI_HOTCUE_PAD?"HOT CUE":"SAMPLE");
  return;}
 case XZ_UI_STRIP:control_row(b,a,XZ_SHELL_MENU_PAD,"LOOP STRIP");return;
 case XZ_UI_KEYBOARD:{
  if(a->index>=32&&a->index<127&&a->index!=' ')snprintf(label,sizeof label,"%c",(char)a->index);
  else copy(label,sizeof label,a->label);
  control_row(b,a,XZ_SHELL_MENU_KEY,label);
  return;}
 case XZ_UI_KEY_SHIFT:
  control_row(b,a,XZ_SHELL_MENU_ACTION,a->index<0?"KEY DOWN":a->index>0?"KEY UP":"KEY RESET");return;
 case XZ_UI_PROFILE_VIEW:control_row(b,a,XZ_SHELL_MENU_ACTION,a->index<0?"PREVIOUS PROFILE":"NEXT PROFILE");return;
 case XZ_UI_WAD_PAGE:case XZ_UI_SDK_PAGE:case XZ_UI_LOGO_PAGE:
  control_row(b,a,XZ_SHELL_MENU_ACTION,a->index<0?"PREVIOUS PAGE":"NEXT PAGE");return;
 case XZ_UI_WAD_SELECT:{
  struct xz_shell_menu_row *r=control_row(b,a,XZ_SHELL_MENU_ACTION,a->label);
  int k=a->index-b->m->doom.offset;
  if(r&&k>=0&&k<6&&b->m->doom.details[k][0])copy(r->help,sizeof r->help,b->m->doom.details[k]);
  if(r&&xz_ui_widget_look(u,b->m,a).selected)copy(r->value,sizeof r->value,"IN USE");
  return;}
 case XZ_UI_LOGO_PICK:{
  const char *name=a->label;
  if(a->index>=0){const char *slash=strrchr(a->label?a->label:"",'/');if(slash)name=slash+1;}
  struct xz_shell_menu_row *r=control_row(b,a,XZ_SHELL_MENU_ACTION,name);
  if(r&&u->edit_logo==a->index)copy(r->value,sizeof r->value,"CHOSEN");
  if(r&&a->index>=0)copy(r->help,sizeof r->help,a->label);
  return;}
 case XZ_UI_SDK_RUN:case XZ_UI_SDK_STOP:case XZ_UI_SDK_FIX:case XZ_UI_SDK_SAVE:
  if(u->page==XZ_UI_APPS&&a->index>=0&&a->index<(int)b->m->sdk.snapshot.count&&a->index<XZ_SDK_PACKAGES){
   const struct xz_sdk_item *item=&b->m->sdk.snapshot.items[a->index];
   snprintf(label,sizeof label,"%s%.38s",a->kind==XZ_UI_SDK_SAVE?"SAVE ":"",item->name[0]?item->name:item->id);
   struct xz_shell_menu_row *r=control_row(b,a,XZ_SHELL_MENU_ACTION,label);
   if(r){
    copy(r->value,sizeof r->value,a->label);
    if(item->detail[0]&&item->state==XZ_SDK_BLOCKED)copy(r->help,sizeof r->help,item->detail);
   }
   return;
  }
  control_row(b,a,XZ_SHELL_MENU_ACTION,a->label);return;
 default:
  control_row(b,a,XZ_SHELL_MENU_ACTION,a->label&&a->label[0]?a->label:NULL);return;
 }
}
static int skipped(const struct build *b, const struct xz_ui_widget *a) {
 const struct xz_ui *u=b->u;
 switch(a->kind){
 case XZ_UI_NONE:case XZ_UI_CLOSE:case XZ_UI_BACK:case XZ_UI_PROFILE:return 1;
 /* the rail tabs: the MODS screen has its own rail */
 case XZ_UI_PANEL:return xz_ui_page_is_top(a->index)||back_link(b,a);
 /* CONTROLS lists its sibling pages as links; they need no tabs of their own */
 case XZ_UI_CONTROL_SECTION:return u->page!=XZ_UI_CONTROLS||a->index==0||a->index==3;
 /* BEAT JUMP is rebuilt as one choice per pair; the setup steps are the strip */
 case XZ_UI_JUMP_EDIT_PAGE:case XZ_UI_JUMP_PAIR:case XZ_UI_JUMP_STEP:case XZ_UI_JUMP_SIZE:case XZ_UI_SDK_STEP:return 1;
 default:return 0;
 }
}
int xz_ui_menu_build(const struct xz_ui *u, const struct xz_ui_model *m, struct xz_shell_menu *out) {
 if(!u||!m||!out)return 0;
 memset(out,0,sizeof *out);
 struct build b={out,NULL,u,m,0,-1,{0},0,0};
 int chain[24],n=chain_of(u,chain),depth=n-1;
 out->page=u->page;out->root=chain[0];out->depth=(uint8_t)(depth>255?255:depth);
 b.came_from=n>1?chain[n-2]:-1;
 copy(out->title,sizeof out->title,u->page==XZ_UI_PROFILE_EDIT?(u->edit_entry<0?"NEW PROFILE":"EDIT PROFILE"):
  u->page==XZ_UI_METRICS_SETUP?"METRICS":xz_ui_page_title(u->page));
 for(int i=0;i<n-1;i++){
  size_t at=strlen(out->path);
  snprintf(out->path+at,sizeof out->path-at,"%s%s",i?" / ":"",xz_ui_page_title(chain[i]));
 }
 if(u->page==XZ_UI_PROFILE_EDIT&&(u->edit_step==XZ_UI_EDIT_NAME||u->edit_step==XZ_UI_EDIT_TAGLINE)){
  out->layout=XZ_SHELL_MENU_KEYBOARD;
  copy(out->field_label,sizeof out->field_label,u->edit_step==XZ_UI_EDIT_NAME?"DJ NAME":"TAGLINE");
  copy(out->field,sizeof out->field,u->edit_step==XZ_UI_EDIT_NAME?u->edit_name:u->edit_tagline);
 }
 /* A top page opened from the rail starts on its first row; a deeper page where the panel's focus is (its
    first control, or after BACK the link that led away), so BACK then push re-enters. */
 b.has_focus=(u->focus_kind!=XZ_UI_NONE||!xz_ui_page_is_top(u->page))&&xz_ui_focused(u,m,&b.focus);
 page_sections(&b);
 if(u->page==XZ_UI_EXTRAS)profile_choice_rows(&b);
 if(u->page==XZ_UI_BEAT_JUMP)beat_jump_rows(&b);
 struct xz_ui_widget w[XZ_UI_WIDGETS];int used[XZ_UI_WIDGETS]={0};
 size_t count=xz_ui_layout(u,m,w);
 for(size_t i=0;i<count;i++){
  if(used[i]||skipped(&b,&w[i]))continue;
  used[i]=1;control(&b,w,count,used,i);
 }
 if(b.dead){
  /* the deck picker only served the controls that left */
  if(u->page==XZ_UI_STEMS||u->page==XZ_UI_XPAD){
   int k=0;for(int i=0;i<out->row_count;i++)if(out->row[i].key!=(0x40000000|((int)XZ_UI_DECK<<4)))out->row[k++]=out->row[i];
   out->row_count=(uint8_t)k;
  }
  if(u->page==XZ_UI_XPAD){
   section(&b,"");
   struct xz_shell_menu_row *r=add_row(&b,XZ_SHELL_MENU_LINK,0x50000400,"SET UP X-PAD");
   if(r){r->target=direct(XZ_UI_FEATURE_SETUP_OPEN,XZ_UI_SAMPLE,0);copy(r->help,sizeof r->help,"Detects the X-PAD engine on a loaded USB deck, then enables and tests it.");}
  }
 }
 /* rows in section order, the panel's order inside each section; on a setup page its step comes before
    the deck it applies to, so the first push runs it */
 int setup=u->page==XZ_UI_FEATURE_SETUP||u->page==XZ_UI_LOCAL_STEM_SETUP||u->page==XZ_UI_PC_STEMS||u->page==XZ_UI_RAM_SETUP;
 for(int i=1;i<out->row_count;i++){
  struct xz_shell_menu_row r=out->row[i];int k=i;
  while(k>0&&out->row[k-1].section>r.section){out->row[k]=out->row[k-1];k--;}
  out->row[k]=r;
 }
 if(setup){
  struct xz_shell_menu_row tail[4];int t=0,k=0;
  for(int i=0;i<out->row_count;i++){
   int deck=out->row[i].key==(0x40000000|((int)XZ_UI_DECK<<4))||out->row[i].key==(0x40000000|((int)XZ_UI_RAM_DECK<<4));
   if(deck&&t<4)tail[t++]=out->row[i];else out->row[k++]=out->row[i];
  }
  for(int i=0;i<t;i++)out->row[k++]=tail[i];
 }
 out->focus_row=-1;
 for(int i=0;i<out->row_count;i++)if(out->row[i].key==b.focus_key){out->focus_row=(int16_t)i;break;}
 page_text(&b);
 if(b.dead)line(&b,XZ_SHELL_TONE_ALERT,"%s",b.dead);
 /* the last result, else what the page is waiting for */
 if(u->notice[0]){copy(out->status,sizeof out->status,u->notice);out->status_tone=XZ_SHELL_TONE_HEAD;}
 else if(m->settings_flash&&m->settings_status){copy(out->status,sizeof out->status,m->settings_status);out->status_tone=XZ_SHELL_TONE_PLAIN;}
 else if(xz_sdk_ui_page(u->page)){copy(out->status,sizeof out->status,xz_sdk_ui_footer(u,m));out->status_tone=XZ_SHELL_TONE_DIM;}
 for(int i=0;i<out->row_count;i++){neutral(out->row[i].help,sizeof out->row[i].help);neutral(out->row[i].reason,sizeof out->row[i].reason);}
 for(int i=0;i<out->line_count;i++)neutral(out->line[i].text,sizeof out->line[i].text);
 neutral(out->status,sizeof out->status);
 return 1;
}

/* ---- running a row ---- */
static int find(const struct xz_ui *u, const struct xz_ui_model *m, const struct xz_shell_menu_target *t, struct xz_ui_widget *out) {
 struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(u,m,w);
 for(size_t i=0;i<n;i++)if((int)w[i].kind==t->kind&&w[i].index==t->index&&w[i].deck==t->deck){*out=w[i];return 1;}
 return 0;
}
static size_t take(struct xz_ui_action *dst, size_t at, size_t cap, const struct xz_ui_action *src, size_t n) {
 for(size_t i=0;i<n&&at<cap;i++)dst[at++]=src[i];
 return at;
}
size_t xz_ui_menu_run(struct xz_ui *u, const struct xz_ui_model *m, const struct xz_shell_menu_target *t, struct xz_ui_action out[XZ_UI_MENU_ACTIONS]) {
 if(!u||!m||!t||!out)return 0;
 struct xz_ui_action a[XZ_UI_ACTIONS];size_t total=take(out,0,XZ_UI_MENU_ACTIONS,a,xz_ui_cancel(u,a));
 switch(t->how){
 case XZ_UI_MENU_TOUCH:{
  struct xz_ui_widget w;
  if(!find(u,m,t,&w)){snprintf(u->notice,sizeof u->notice,"%s","THAT CONTROL HAS CHANGED: LOOK AGAIN");return total;}
  /* exactly the panel's encoder push: a press and a release at the control's centre */
  u->focus_kind=w.kind;u->focus_index=w.index;u->focus_deck=w.deck;u->adjusting=0;
  total=take(out,total,XZ_UI_MENU_ACTIONS,a,xz_ui_touch(u,m,w.x+w.w/2,w.y+w.h/2,1,a));
  total=take(out,total,XZ_UI_MENU_ACTIONS,a,xz_ui_touch(u,m,w.x+w.w/2,w.y+w.h/2,0,a));
  if(w.kind==XZ_UI_CONTROL_SECTION){u->focus_kind=w.kind;u->focus_index=w.index;u->focus_deck=w.deck;}
  return total;}
 case XZ_UI_MENU_DIRECT:case XZ_UI_MENU_SLIDE:
  if(t->kind==XZ_UI_FEATURE_SETUP_OPEN){
   /* the X-PAD page's way into its setup, as GENERAL's SET UP X-PAD opens it */
   enum xz_ui_page old=u->page;xz_ui_navigate(u,XZ_UI_FEATURE_SETUP);u->feature_cap=t->index;u->feature_step=0;
   if(u->deck>1)u->deck=0;
   if(total<XZ_UI_MENU_ACTIONS)out[total++]=(struct xz_ui_action){.kind=XZ_UI_PANEL,.phase=XZ_UI_PRESS,.deck=u->deck,.index=(int)old,.value=(float)u->page,.pc_target=-1};
   return total;
  }
  if(total<XZ_UI_MENU_ACTIONS)out[total++]=(struct xz_ui_action){.kind=(enum xz_ui_action_kind)t->kind,
   .phase=t->how==XZ_UI_MENU_SLIDE?XZ_UI_MOVE:XZ_UI_PRESS,.deck=t->deck>=0?t->deck:u->deck,.index=t->index,
   .value=t->how==XZ_UI_MENU_SLIDE?(float)t->value/100.f:(float)t->value,.pc_target=-1};
  return total;
 case XZ_UI_MENU_CYCLE:{
  int layout=t->kind==XZ_UI_STEMS_OVERLAY,now=layout?m->modern_stems:m->stem_order;
  for(int step=0;step<(layout?XZ_MODERN_STEMS_COUNT:XZ_STEM_ORDER_COUNT)&&now!=t->index&&total<XZ_UI_MENU_ACTIONS;step++){
   out[total++]=(struct xz_ui_action){.kind=(enum xz_ui_action_kind)t->kind,.phase=XZ_UI_PRESS,.deck=u->deck,.pc_target=-1};
   now=layout?xz_modern_stems_next(now):(now+1)%XZ_STEM_ORDER_COUNT;
  }
  return total;}
 case XZ_UI_MENU_PAGE:{
  enum xz_ui_page old=u->page;
  xz_ui_navigate(u,(enum xz_ui_page)t->index);
  if(u->page!=old&&total<XZ_UI_MENU_ACTIONS)
   out[total++]=(struct xz_ui_action){.kind=XZ_UI_PANEL,.phase=XZ_UI_PRESS,.deck=u->deck,.index=(int)old,.value=(float)u->page,.pc_target=-1};
  return total;}
 default:return total;
 }
}
size_t xz_ui_menu_pad(struct xz_ui *u, const struct xz_ui_model *m, const struct xz_shell_menu_target *t, int x, int y, int down, struct xz_ui_action out[XZ_UI_ACTIONS]) {
 if(!u||!m||!t||!out)return 0;
 if(!down)return xz_ui_cancel(u,out);
 struct xz_ui_widget w;
 if(t->how!=XZ_UI_MENU_TOUCH||!find(u,m,t,&w))return 0;
 x=x<0?0:x>1000?1000:x;y=y<0?0:y>1000?1000:y;
 return xz_ui_touch(u,m,w.x+(w.w-1)*x/1000,w.y+(w.h-1)*y/1000,1,out);
}
size_t xz_ui_menu_back(struct xz_ui *u, struct xz_ui_action out[XZ_UI_ACTIONS]) {
 int depth=0;
 if(!u||!out)return 0;
 xz_ui_menu_root(u,&depth);
 if(!depth)return 0;
 size_t n=xz_ui_back(u,out),k=0;
 for(size_t i=0;i<n;i++)if(out[i].kind!=XZ_UI_CLOSE)out[k++]=out[i];
 return k;
}
