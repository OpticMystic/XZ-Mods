/* SHORTCUT (2x2 quadrants) and the MODS screen (a page rail, items, values)
   at 800x480, after the flagship player's settings screens (the
   2026-10-18 flagship UI study in docs/research, sections 3.4-3.6 and 6.2).
   Both keep a compact two-deck status strip at the bottom, as every flagship
   non-play screen does.

   MODS is the XZ_SHELL_UTILITY screen: its rail's first entry is UTILITY, the
   device's own menu, the others are the mod pages. BACK steps one pane left,
   then onto the rail, then closes the screen (xz_shell_settings_back). */
#include "shell_internal.h"
#include "shell_settings.h"
#include "shell_help.h"
#include "shell_browse.h"
#include <stdio.h>
#include <string.h>

enum {
 W=XZ_SHELL_WIDTH, H=XZ_SHELL_HEIGHT, TITLE_H=40, BODY_Y=42,
 ROW_H=XZ_SHELL_SETTINGS_ROW_H, ROWS=XZ_SHELL_SETTINGS_ROWS, RAIL_H=XZ_SHELL_SETTINGS_RAIL_H,
 U_Y=BODY_Y, STATUS_Y=U_Y+ROWS*ROW_H+2, STATUS_H=H-STATUS_Y,
 RAIL_W=88, LIST_X=RAIL_W+2, LIST_W=408, VALUE_X=LIST_X+LIST_W+2, VALUE_W=W-VALUE_X,
 QUAD_W=399, QUAD_H=190, QUAD_TITLE=18, SLOT_H=43, SLOTS=4, CHIP_PAD=12, CHIP_GAP=3, CHIP_Y=15, CHIP_H=25,
 MAX_ITEMS=128, DEEP_BLUE=XZ_FLAGSHIP_DEEP_BLUE, RAIL_SELECTED=XZ_FLAGSHIP_RAIL_SELECTED, GREY_TEXT=0x8c8c8c,
 READ_ONLY_GREY=0x6e6e6e,
 TITLE_RIGHT=XZ_SHELL_HEADER_X-12   /* the mod header buttons own the title bar right of this */
};
static const struct xz_canvas measure={NULL,W,W,H,XZ_THEME_FLAGSHIP};
static size_t emit_one(struct xz_shell_action out[XZ_SHELL_ACTIONS], struct xz_shell_action a) { out[0]=a;return 1; }

/* ---- model access ----
   The screens read one merged table: the runtime's items, with the shell-owned
   display items taking the shell's own state, plus the items the shell can
   name from the play model when the runtime's table lacks them. */
struct table { const struct xz_shell_setting_item *items; int count; };
static int shell_owned(int id) {
 return id==XZ_SHELL_ITEM_WAVE_COLOR||id==XZ_SHELL_ITEM_WAVE_POSITION||id==XZ_SHELL_ITEM_KEY_DISPLAY||id==XZ_SHELL_ITEM_WAVE_ZOOM
  ||id==XZ_SHELL_ITEM_USB_COLORS||id==XZ_SHELL_ITEM_MODE_LAMPS||id==XZ_SHELL_ITEM_QUANTIZE_LAMP||id==XZ_SHELL_ITEM_BYPASS_BUTTON||id==XZ_SHELL_ITEM_WAVE_TITLES;
}
static const char *const wave_color_choices[]={"BLUE","RGB","3BAND","STEMS"};
static const char *const position_choices[]={"CENTER","LEFT"};
static const char *const key_display_choices[]={"AS DECK","CLASSIC","ALPHANUM."};
static const char *const zoom_choices[XZ_SHELL_ZOOM_MAX+1]={"1","2","3","4","5","6","7"};
static const char *const usb_colors_choices[]={"DECK + WAVE","DECK ONLY","OFF"};
static const char *const show_hide_choices[]={"SHOW","HIDE"};
static const char *const time_mode_choices[]={"REMAIN","TIME"};
static const char *const play_mode_choices[]={"SINGLE","CONTINUE"};
static const char *const off_on_choices[]={"OFF","ON"};
static const char *const quantize_choices[]={"1/8","1/4","1/2","1"};
/* enum xz_shell_hot_cue_auto_load order */
static const char *const hot_cue_load_choices[]={"OFF","ON","LIBRARY SETTING"};
static const char *const beat_jump_choices[]={"1/2","1","2","4","8","16","32","64"};
static int deck_quantize_index(const struct xz_shell_deck *d) {
 if(!sh_has(d,XZ_SHELL_F_QUANTIZE)||d->quantize_num!=1)return -1;
 return d->quantize_den==8?0:d->quantize_den==4?1:d->quantize_den==2?2:d->quantize_den<=1?3:-1;
}
static int deck_jump_index(const struct xz_shell_deck *d) {
 if(!sh_has(d,XZ_SHELL_F_BEAT_JUMP))return -1;
 if(d->beat_jump_num==1&&d->beat_jump_den==2)return 0;
 if(d->beat_jump_den>1)return -1;
 for(int i=1;i<8;i++)if(d->beat_jump_num==1<<(i-1))return i;
 return -1;
}
static struct xz_shell_setting_item make_item(int id, enum xz_shell_setting_category cat, const char *label, const char *const *choices, int n, int current) {
 return (struct xz_shell_setting_item){id,cat,label,choices,n,current<0?0:current,current>=0,0};
}
/* The item as the shell supplies it from its own state or the play model; id -1 when it cannot. */
static struct xz_shell_setting_item own_item(const struct xz_shell *u, const struct xz_shell_model *m, int id) {
 const struct xz_shell_settings *st=&m->settings;
 const struct xz_shell_deck *d=&m->deck[sh_active_deck(u,m)];
 struct xz_shell_setting_item it;memset(&it,0,sizeof it);it.id=-1;
 switch(id){
 case XZ_SHELL_ITEM_WAVE_COLOR:it=make_item(id,XZ_SHELL_SETTING_CAT_DISPLAY_LCD,"WAVEFORM COLOR",wave_color_choices,4,(int)sh_wave_color(u));break;
 case XZ_SHELL_ITEM_WAVE_POSITION:it=make_item(id,XZ_SHELL_SETTING_CAT_DISPLAY_LCD,"WAVEFORM CURRENT POSITION",position_choices,2,(int)sh_wave_position(u));break;
 case XZ_SHELL_ITEM_KEY_DISPLAY:it=make_item(id,XZ_SHELL_SETTING_CAT_DISPLAY_LCD,"KEY DISPLAY FORMAT",key_display_choices,3,(int)sh_key_display(u));break;
 case XZ_SHELL_ITEM_WAVE_ZOOM:it=make_item(id,XZ_SHELL_SETTING_CAT_DISPLAY_LCD,"WAVEFORM ZOOM",zoom_choices,XZ_SHELL_ZOOM_MAX+1,sh_max(0,sh_min(u->zoom,XZ_SHELL_ZOOM_MAX)));break;
 case XZ_SHELL_ITEM_USB_COLORS:it=make_item(id,XZ_SHELL_SETTING_CAT_DISPLAY_LCD,"USB COLORS",usb_colors_choices,3,(int)sh_usb_colors(u));break;
 case XZ_SHELL_ITEM_MODE_LAMPS:it=make_item(id,XZ_SHELL_SETTING_CAT_DISPLAY_LCD,"MODE LAMPS",show_hide_choices,2,!sh_indicator_shown(u,XZ_SHELL_IND_MODE_LAMPS));break;
 case XZ_SHELL_ITEM_QUANTIZE_LAMP:it=make_item(id,XZ_SHELL_SETTING_CAT_DISPLAY_LCD,"QUANTIZE LAMP",show_hide_choices,2,!sh_indicator_shown(u,XZ_SHELL_IND_QUANTIZE));break;
 case XZ_SHELL_ITEM_BYPASS_BUTTON:it=make_item(id,XZ_SHELL_SETTING_CAT_DISPLAY_LCD,"BYPASS BUTTON",show_hide_choices,2,!sh_indicator_shown(u,XZ_SHELL_IND_BYPASS));break;
 case XZ_SHELL_ITEM_WAVE_TITLES:it=make_item(id,XZ_SHELL_SETTING_CAT_DISPLAY_LCD,"WAVEFORM TITLES",show_hide_choices,2,!sh_indicator_shown(u,XZ_SHELL_IND_WAVE_TITLES));break;
 case XZ_SHELL_ITEM_TIME_MODE:it=make_item(id,XZ_SHELL_SETTING_CAT_DJ_SETTING,"TIME MODE",time_mode_choices,2,sh_setting(m,XZ_SHELL_SET_TIME_MODE)?(st->time_mode==XZ_SHELL_TIME_ELAPSED):-1);break;
 case XZ_SHELL_ITEM_PLAY_MODE:it=make_item(id,XZ_SHELL_SETTING_CAT_DJ_SETTING,"PLAY MODE",play_mode_choices,2,sh_setting(m,XZ_SHELL_SET_PLAY_MODE)?(st->play_mode==XZ_SHELL_PLAY_CONTINUE):-1);break;
 case XZ_SHELL_ITEM_AUTO_CUE:it=make_item(id,XZ_SHELL_SETTING_CAT_DJ_SETTING,"AUTO CUE",off_on_choices,2,sh_setting(m,XZ_SHELL_SET_AUTO_CUE)?!!st->auto_cue:-1);break;
 case XZ_SHELL_ITEM_HOT_CUE_AUTO_LOAD:it=make_item(id,XZ_SHELL_SETTING_CAT_DJ_SETTING,"HOT CUE AUTO LOAD",hot_cue_load_choices,3,
  sh_setting(m,XZ_SHELL_SET_HOT_CUE_AUTO_LOAD)?sh_max(0,sh_min(st->hot_cue_auto_load,XZ_SHELL_HCAL_LIBRARY)):-1);break;
 case XZ_SHELL_ITEM_QUANTIZE_BEAT:it=make_item(id,XZ_SHELL_SETTING_CAT_DJ_SETTING,"QUANTIZE BEAT VALUE",quantize_choices,4,deck_quantize_index(d));break;
 case XZ_SHELL_ITEM_BEAT_JUMP_BEAT:it=make_item(id,XZ_SHELL_SETTING_CAT_DJ_SETTING,"BEAT JUMP BEAT VALUE",beat_jump_choices,8,deck_jump_index(d));break;
 default:return it;
 }
 it.writable=shell_owned(id)||(int)((st->writable>>id)&1);
 return it;
}
static const int own_ids[]={XZ_SHELL_ITEM_TIME_MODE,XZ_SHELL_ITEM_QUANTIZE_BEAT,XZ_SHELL_ITEM_BEAT_JUMP_BEAT,XZ_SHELL_ITEM_PLAY_MODE,
 XZ_SHELL_ITEM_AUTO_CUE,XZ_SHELL_ITEM_HOT_CUE_AUTO_LOAD,XZ_SHELL_ITEM_WAVE_COLOR,XZ_SHELL_ITEM_WAVE_POSITION,XZ_SHELL_ITEM_KEY_DISPLAY,XZ_SHELL_ITEM_WAVE_ZOOM,XZ_SHELL_ITEM_USB_COLORS};
enum { OWN_IDS=sizeof own_ids/sizeof *own_ids };
/* One shell drives these screens on one thread (shell.h rule 4), so one merged table serves every call. */
static struct xz_shell_setting_item merged[MAX_ITEMS+OWN_IDS];
/* The open MODS tab other than UTILITY, or NULL. */
static const struct xz_shell_mods_tab *mods_tab(const struct xz_shell *u, const struct xz_shell_model *m) {
 int t=u->mods_view.tab;
 if(xz_shell_screen(u)!=XZ_SHELL_UTILITY||t<1||!m->mods||!m->mods->tabs||(uint32_t)t>m->mods->count)return NULL;
 return &m->mods->tabs[t-1];
}
static int on_mod_tab;
/* Notes for the table last built: base items and their parallel notes. */
static const struct xz_shell_setting_item *note_items;
static const char *const *note_lines;
static int note_count;
static const char *merged_notes[MAX_ITEMS+16];
static struct table table_of(const struct xz_shell *u, const struct xz_shell_model *m) {
 const struct xz_shell_settings_model *s=m->settings_screen;
 int n=0;
 const struct xz_shell_mods_tab *tab=mods_tab(u,m);
 note_items=NULL;note_lines=NULL;note_count=0;
 on_mod_tab=tab!=NULL;
 if(tab){
  /* A mod tab is its own table, as the runtime gives it. */
  const struct xz_shell_settings_model *t=tab->kind==XZ_SHELL_TAB_ITEMS?tab->items:NULL;
  if(!t||!t->items||!t->count)return (struct table){NULL,0};
  note_items=t->items;note_lines=t->notes;note_count=(int)sh_min((int)t->count,MAX_ITEMS);
  return (struct table){t->items,note_count};
 }
 if(!s)return (struct table){NULL,0};
 if(s->items)for(uint32_t i=0;i<s->count&&n<MAX_ITEMS;i++){
  merged[n]=s->items[i];
  merged_notes[n]=s->notes?s->notes[i]:NULL;
  if(shell_owned(merged[n].id)){enum xz_shell_setting_category cat=merged[n].category;merged[n]=own_item(u,m,merged[n].id);merged[n].category=cat;}
  n++;
 }
 for(int k=0;k<OWN_IDS;k++){
  int listed=0;
  for(int i=0;i<n&&!listed;i++)listed=merged[i].id==own_ids[k];
  if(!listed){merged_notes[n]=NULL;merged[n++]=own_item(u,m,own_ids[k]);}
 }
 note_items=merged;note_lines=merged_notes;note_count=n;
 return (struct table){merged,n};
}
static int valid_category(const struct xz_shell_setting_item *it) { return (unsigned)it->category<XZ_SHELL_SETTING_CAT_COUNT; }
static int find_id(struct table t, int id) {
 for(int i=0;i<t.count;i++)if(t.items[i].id==id)return i;
 return -1;
}
static int choice_count(const struct xz_shell_setting_item *it) { return it->choices&&it->choice_count>0?sh_min(it->choice_count,255):0; }
/* A list of values the DJ picks from; a single choice is information, not a control. */
static int selectable(const struct xz_shell_setting_item *it) { return choice_count(it)>=2; }
static int current_of(const struct xz_shell_setting_item *it) {
 return it->known&&it->current>=0&&it->current<choice_count(it)?it->current:-1;
}
/* Mod-tab items are the runtime's own settings: their writable flag alone says whether they can change. */
static int settable(const struct xz_shell_model *m, const struct xz_shell_setting_item *it) {
 if(on_mod_tab)return it->writable;
 return shell_owned(it->id)||(it->writable&&(m->capabilities&XZ_SHELL_CAP_SET_SETTING));
}
/* How to change an item the runtime cannot write: the runtime's line, else the honest default. Pushing
   the item hands the screen to the stock deck, where a held MENU opens its UTILITY. */
static const char *read_only_note(const struct xz_shell_setting_item *it) {
 if(note_items&&it>=note_items&&it<note_items+note_count&&note_lines&&note_lines[it-note_items])return note_lines[it-note_items];
 return NULL;
}
/* A shell-owned item changes the shell at once; the action lets the runtime store it. A waveform colour
   picked here while decks follow their stick's colours is a local override: decks keep the stick's tile
   colour and draw this waveform colour. */
size_t sh_settings_set(struct xz_shell *u, int id, int index, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 if(shell_owned(id)&&!xz_shell_restore_setting(u,id,index))return 0;
 if(id==XZ_SHELL_ITEM_WAVE_ZOOM)return emit_one(out,(struct xz_shell_action){XZ_SHELL_ACT_ZOOM,-1,index,0});
 out[0]=(struct xz_shell_action){XZ_SHELL_ACT_SET_SETTING,-1,index,id};
 if(id!=XZ_SHELL_ITEM_WAVE_COLOR||sh_usb_colors(u)!=XZ_SHELL_USB_COLORS_ALL)return 1;
 xz_shell_restore_setting(u,XZ_SHELL_ITEM_USB_COLORS,XZ_SHELL_USB_COLORS_TILE);
 out[1]=(struct xz_shell_action){XZ_SHELL_ACT_SET_SETTING,-1,XZ_SHELL_USB_COLORS_TILE,XZ_SHELL_ITEM_USB_COLORS};
 return 2;
}
int sh_settings_item(const struct xz_shell *u, const struct xz_shell_model *m, int id, struct xz_shell_setting_item *out) {
 *out=own_item(u,m,id);
 return out->id>=0;
}
static size_t set_value(struct xz_shell *u, const struct xz_shell_setting_item *it, int index, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 if(on_mod_tab)return emit_one(out,(struct xz_shell_action){XZ_SHELL_ACT_SET_MOD,u->mods_view.tab-1,index,it->id});
 return sh_settings_set(u,it->id,index,out);
}
static const char *choice_text(const struct xz_shell_setting_item *it, int index) {
 if(index<0||index>=choice_count(it)||!it->choices[index])return "---";
 return it->choices[index];
}
static const char *value_text(const struct xz_shell_setting_item *it) {
 if(!it->known)return "UNKNOWN";
 return choice_text(it,current_of(it));
}
static const char *label_of(const struct xz_shell_setting_item *it) { return it->label?it->label:"---"; }

/* ---- text ---- */
static size_t utf8_step(const char *s) {
 unsigned char ch=(unsigned char)*s;
 size_t n=ch<0xc2?1:ch<0xe0?2:ch<0xf0?3:4,i;
 for(i=1;i<n;i++)if((s[i]&0xc0)!=0x80)return i;
 return n;
}
/* Longest whole-character prefix of s that fits width, copied to out. */
static int fit(const char *s, enum sh_face face, int width, char *out, size_t cap) {
 size_t used=0;int w=0;
 out[0]=0;
 if(!s)return 0;
 while(*s&&used+5<cap){
  size_t step=utf8_step(s);
  memcpy(out+used,s,step);out[used+step]=0;
  int next=sh_text_width(measure,out,face);
  if(next>width){out[used]=0;break;}
  used+=step;s+=step;w=next;
 }
 return w;
}
static void text_fit(struct xz_canvas c, int x, int y, int width, const char *s, enum sh_face face, uint32_t ink) {
 char buf[160];fit(s,face,width,buf,sizeof buf);sh_text(c,x,y,buf,face,ink);
}
static void text_fit_right(struct xz_canvas c, int right, int y, int width, const char *s, enum sh_face face, uint32_t ink) {
 char buf[160];int w=fit(s,face,width,buf,sizeof buf);sh_text(c,right-w,y,buf,face,ink);
}
static void text_fit_center(struct xz_canvas c, int cx, int y, int width, const char *s, enum sh_face face, uint32_t ink) {
 char buf[160];int w=fit(s,face,width,buf,sizeof buf);sh_text(c,cx-w/2,y,buf,face,ink);
}
static void h_triangle(struct xz_canvas c, int x, int cy, int size, int right, uint32_t ink) {
 for(int col=0;col<size;col++){
  int half=right?size-1-col:col;
  xz_rect(c,x+col,cy-half,1,half*2+1,ink);
 }
}
static void lock_icon(struct xz_canvas c, int x, int y, uint32_t ink) {
 xz_ring(c,x+4,y+4,4,1,ink);xz_rect(c,x,y+4,9,7,ink);xz_rect(c,x+4,y+6,1,3,SH_CHROME);
}

/* ---- descriptions (shell_settings.h, shell_help.h) ---- */
static int lower(int ch) { return ch>='A'&&ch<='Z'?ch-'A'+'a':ch; }
static int same_text(const char *a, const char *b) {
 if(!a||!b)return 0;
 while(*a&&*b&&lower((unsigned char)*a)==lower((unsigned char)*b)){a++;b++;}
 return !*a&&!*b;
}
static int has_text(const char *hay, const char *needle) {
 if(!hay||!needle||!*needle)return 0;
 for(;*hay;hay++){
  const char *h=hay,*n=needle;
  while(*h&&*n&&lower((unsigned char)*h)==lower((unsigned char)*n)){h++;n++;}
  if(!*n)return 1;
 }
 return 0;
}
static const struct xz_shell_item_help *model_help(const struct xz_shell_model *m, int scope, int id) {
 if(!m->help||!m->help->items)return NULL;
 for(uint32_t i=0;i<m->help->count&&i<1024;i++)if(m->help->items[i].scope==scope&&m->help->items[i].id==id)return &m->help->items[i];
 return NULL;
}
/* The shell's own text serves UTILITY, SHORTCUT and the stick's stored values: by id, then by label. */
static const struct sh_help *table_help(int scope, const struct xz_shell_setting_item *it) {
 if(scope!=XZ_SHELL_HELP_UTILITY&&scope!=XZ_SHELL_HELP_MY_SETTINGS)return NULL;
 enum { N=sizeof sh_help_table/sizeof *sh_help_table };
 if(scope==XZ_SHELL_HELP_UTILITY)for(int i=0;i<N;i++)if(sh_help_table[i].id==it->id)return &sh_help_table[i];
 for(int i=0;i<N;i++)if(same_text(sh_help_table[i].label,it->label))return &sh_help_table[i];
 return NULL;
}
const char *sh_item_help(const struct xz_shell_model *m, int scope, const struct xz_shell_setting_item *it) {
 const struct xz_shell_item_help *h=model_help(m,scope,it->id);
 if(h&&h->help&&*h->help)return h->help;
 const struct sh_help *t=table_help(scope,it);
 return t?t->text:NULL;
}
const char *sh_choice_help(const struct xz_shell_model *m, int scope, const struct xz_shell_setting_item *it, int index) {
 if(index<0||index>=choice_count(it))return NULL;
 const struct xz_shell_item_help *h=model_help(m,scope,it->id);
 if(h&&h->choice_help&&index<h->choice_count&&h->choice_help[index])return h->choice_help[index];
 const struct sh_help *t=table_help(scope,it);
 const char *label=it->choices[index];
 if(!t||!label)return NULL;
 for(int i=0;i<6&&t->choices[i].label;i++)if(t->choices[i].label[0]!='*'&&same_text(t->choices[i].label,label))return t->choices[i].text;
 for(int i=0;i<6&&t->choices[i].label;i++)if(t->choices[i].label[0]=='*'&&has_text(label,t->choices[i].label+1))return t->choices[i].text;
 return NULL;
}
/* Words wrapped to width; a word wider than the line is cut. Returns the line count. */
enum { DESC_LINE=14, DESC_PAD=10, DESC_CHARS=SH_WRAP_CHARS, DESC_LINES=8, HELP_LINES=4, CHOICE_LINES=2 };
int sh_wrap(const char *text, int width, char out[][SH_WRAP_CHARS], int max) {
 int n=0;
 if(!text)return 0;
 while(*text&&n<max){
  while(*text==' ')text++;
  if(!*text)break;
  char line[DESC_CHARS];size_t len=0,keep=0;const char *after=text;
  for(const char *p=text;;){
   const char *w=p;while(*w&&*w!=' ')w++;
   size_t add=(size_t)(w-p),need=len+(len?1:0)+add;
   if(need>=DESC_CHARS)break;
   char trial[DESC_CHARS];memcpy(trial,line,len);
   size_t t=len;if(len)trial[t++]=' ';memcpy(trial+t,p,add);t+=add;trial[t]=0;
   if(sh_text_width(measure,trial,SH_SMALL)>width&&len)break;
   memcpy(line,trial,t+1);len=t;keep=len;after=w;
   if(!*w)break;
   p=w+1;
  }
  if(!keep){fit(text,SH_SMALL,width,line,sizeof line);keep=strlen(line);after=text+keep;}
  line[keep]=0;
  memcpy(out[n++],line,keep+1);
  text=after;
 }
 return n;
}
/* What the panel holds for one item: its help, the line for one choice, and the runtime's note. */
struct desc { char line[DESC_LINES][DESC_CHARS]; uint32_t ink[DESC_LINES]; int n, lock_line; };
static void desc_of(const struct xz_shell_model *m, int scope, const struct xz_shell_setting_item *it, int choice, int width, struct desc *d) {
 memset(d,0,sizeof *d);d->lock_line=-1;
 int h=sh_wrap(sh_item_help(m,scope,it),width,d->line,HELP_LINES);
 for(int i=0;i<h;i++)d->ink[i]=0xd8d8d8;
 d->n=h;
 const char *ch=sh_choice_help(m,scope,it,choice);
 if(ch){
  char joined[DESC_CHARS*2];
  snprintf(joined,sizeof joined,"%s: %s",choice_text(it,choice),ch);
  int k=sh_wrap(joined,width,d->line+d->n,CHOICE_LINES);
  for(int i=0;i<k;i++)d->ink[d->n+i]=0x9ec8ff;
  d->n+=k;
 }
 const char *note=selectable(it)&&!settable(m,it)?read_only_note(it):NULL;
 if(note&&d->n<DESC_LINES){
  d->lock_line=d->n;
  d->n+=sh_wrap(note,width-16,d->line+d->n,1);
  d->ink[d->lock_line]=SH_DIM;
 }else if(selectable(it)&&settable(m,it)&&current_of(it)<0&&d->n<DESC_LINES){
  snprintf(d->line[d->n],DESC_CHARS,"The current value has not been read yet.");
  d->ink[d->n++]=SH_DIM;
 }
}
/* Panel height in rows, the same whichever choice is lit so the values never jump. */
static int desc_rows(const struct xz_shell_model *m, int scope, const struct xz_shell_setting_item *it, int width) {
 struct desc d;
 int lines=0,n=choice_count(it);
 desc_of(m,scope,it,-1,width,&d);lines=d.n;
 int most=0;
 for(int i=0;i<n&&i<64;i++){
  const char *ch=sh_choice_help(m,scope,it,i);
  if(!ch)continue;
  char joined[DESC_CHARS*2],tmp[CHOICE_LINES][DESC_CHARS];
  snprintf(joined,sizeof joined,"%s: %s",choice_text(it,i),ch);
  most=sh_max(most,sh_wrap(joined,width,tmp,CHOICE_LINES));
 }
 lines+=most;
 if(!lines)return 0;
 return (lines*DESC_LINE+2*DESC_PAD+ROW_H-1)/ROW_H;
}
static void render_desc(struct xz_canvas p, const struct desc *d, int y, int width) {
 if(!d->n)return;
 xz_rect(p,0,y,p.width,p.height-y,SH_BLACK);
 xz_rect(p,12,y+4,width,1,SH_RAIL);
 for(int i=0;i<d->n;i++){
  int x=12,ty=y+DESC_PAD+i*DESC_LINE;
  if(i==d->lock_line){lock_icon(p,x,ty,SH_DIM);x+=16;}
  sh_text(p,x,ty,d->line[i],SH_SMALL,d->ink[i]);
 }
}

/* ---- shared chrome ---- */
static size_t emit(struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t n, struct xz_shell_action a) {
 if(n<XZ_SHELL_ACTIONS)out[n++]=a;
 return n;
}
struct xz_shell_rect xz_shell_settings_rect(enum xz_shell_settings_region r) {
 switch(r){
 case XZ_SHELL_SR_BACK_BUTTON:return (struct xz_shell_rect){0,0,52,TITLE_H+4};
 case XZ_SHELL_SR_RAIL:return (struct xz_shell_rect){0,U_Y,RAIL_W,ROWS*ROW_H};
 case XZ_SHELL_SR_ITEMS:return (struct xz_shell_rect){LIST_X,U_Y,LIST_W,ROWS*ROW_H};
 case XZ_SHELL_SR_VALUES:return (struct xz_shell_rect){VALUE_X,U_Y,VALUE_W,ROWS*ROW_H};
 case XZ_SHELL_SR_STATUS:return (struct xz_shell_rect){0,STATUS_Y,W,STATUS_H};
 }
 return (struct xz_shell_rect){0,0,0,0};
}
static void render_title(struct xz_canvas c, const char *title) {
 xz_rect(c,0,0,W,TITLE_H,SH_CHROME);
 xz_border(c,8,5,30,30,0x646464);
 xz_line16(c,15*16,20*16,31*16,20*16,32,SH_WHITE);
 xz_line16(c,15*16,20*16,22*16,13*16,32,SH_WHITE);
 xz_line16(c,15*16,20*16,22*16,27*16,32,SH_WHITE);
 sh_text(c,52,13,title,SH_TEXT,SH_WHITE);
}

/* Compact two-deck strip: deck tag, time, mini overview, tempo, BPM. */
static void fmt_time(char *out, size_t cap, int32_t ms) {
 if(ms<0)ms=0;
 int32_t s=ms/1000,m=s/60;if(m>99){m=99;s=99*60+59;}
 snprintf(out,cap,"%02d:%02d",(int)m,(int)(s%60));
}
static enum xz_shell_time_mode time_mode_of(const struct xz_shell_model *m, const struct xz_shell_deck *d, int *known) {
 if(sh_has(d,XZ_SHELL_F_TIME_MODE)){*known=1;return d->time_mode;}
 if(sh_setting(m,XZ_SHELL_SET_TIME_MODE)){*known=1;return m->settings.time_mode;}
 *known=0;return XZ_SHELL_TIME_REMAIN;
}
static void render_mini_overview(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c, int deck, struct xz_shell_rect r) {
 const struct xz_shell_deck *d=&m->deck[deck];
 int loaded=sh_has(d,XZ_SHELL_F_LOADED)&&d->loaded;
 int32_t e=loaded?xz_shell_elapsed_now(m,deck):-1;
 int ph=e>=0&&sh_has(d,XZ_SHELL_F_TOTAL)&&d->total_ms>0?r.x+(int)((int64_t)e*(r.w-1)/d->total_ms):-1;
 xz_rect(c,r.x,r.y,r.w,r.h,SH_BLACK);
 if(loaded)sh_deck_overview(c,(struct xz_shell_rect){r.x,r.y,r.w,r.h-1},u,m,d,ph);
 xz_rect(c,r.x,r.y+r.h-1,r.w,1,SH_RAIL);
 if(ph>=0)xz_rect(c,ph,r.y,1,r.h,SH_PLAYHEAD);
}
static void render_status_deck(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c, int deck) {
 const struct xz_shell_deck *d=&m->deck[deck];
 int x0=deck*(W/2)+(deck?1:0),w=W/2-1,y=STATUS_Y;
 struct xz_canvas s=xz_sub(c,x0,y,w,STATUS_H);
 char text[32];
 int loaded_known=sh_has(d,XZ_SHELL_F_LOADED),loaded=loaded_known&&d->loaded;
 xz_rect(s,0,0,w,STATUS_H,SH_CHROME);
 /* deck box */
 xz_rect(s,8,7,40,STATUS_H-14,u->deck==deck?SH_BLUE:SH_RAIL);
 sh_text_center(s,28,11,"DECK",SH_SMALL,SH_WHITE);
 snprintf(text,sizeof text,"%d",deck+1);
 sh_text_center(s,28,25,text,SH_TEXT,SH_WHITE);
 /* time */
 int tm_known;enum xz_shell_time_mode mode=time_mode_of(m,d,&tm_known);
 sh_text(s,58,8,tm_known&&mode==XZ_SHELL_TIME_ELAPSED?"TIME":"REMAIN",SH_SMALL,tm_known?SH_WHITE:SH_DIM);
 int32_t shown=-1,e=loaded?xz_shell_elapsed_now(m,deck):-1;
 if(loaded_known&&!loaded)shown=0;
 else if(mode==XZ_SHELL_TIME_REMAIN&&e>=0&&sh_has(d,XZ_SHELL_F_TOTAL))shown=d->total_ms-e;
 else if(mode==XZ_SHELL_TIME_ELAPSED&&e>=0)shown=e;
 if(shown>=0)fmt_time(text,sizeof text,shown);else snprintf(text,sizeof text,"--:--");
 sh_num(s,58,24,text,SH_NUM_MID,shown>=0?SH_WHITE:SH_DIM);
 /* title over the mini overview */
 struct xz_shell_rect ov={134,25,120,22};
 if(loaded_known&&!loaded)sh_text(s,ov.x,8,"Not Loaded.",SH_SMALL,SH_RED);
 else if(loaded&&sh_has(d,XZ_SHELL_F_TITLE)&&d->title){sh_note(s,ov.x,6,SH_WHITE);text_fit(s,ov.x+14,8,ov.w-14,d->title,SH_SMALL,SH_WHITE);}
 else sh_text(s,ov.x,8,"---",SH_SMALL,SH_DIM);
 render_mini_overview(u,m,s,deck,ov);
 /* tempo */
 int tx=262,bx=w-66,bw=58;
 if(sh_has(d,XZ_SHELL_F_TEMPO_RANGE)){
  if(d->tempo_range>=100)snprintf(text,sizeof text,"WIDE");else snprintf(text,sizeof text,"\xb1%d",d->tempo_range);
  int rw=sh_text_width(s,text,SH_SMALL)+8;
  xz_rect(s,tx,7,rw,13,SH_RED);sh_text(s,tx+4,9,text,SH_SMALL,SH_WHITE);
 }
 if(sh_has(d,XZ_SHELL_F_TEMPO)){
  int32_t t=d->tempo_x100,a=t<0?-t:t;
  if(a>9999)a=9999;
  snprintf(text,sizeof text,"%s%d.%02d",t>0?"+":t<0?"-":"",(int)(a/100),(int)(a%100));
 }else snprintf(text,sizeof text,"--.--");
 int known_tempo=sh_has(d,XZ_SHELL_F_TEMPO),pw=sh_text_width(s,"%",SH_SMALL);
 char shown_tempo[32];int tw=fit(text,SH_TEXT,bx-6-tx-pw-2,shown_tempo,sizeof shown_tempo);
 sh_text(s,tx,27,shown_tempo,SH_TEXT,known_tempo?SH_WHITE:SH_DIM);
 sh_text(s,tx+tw+2,32,"%",SH_SMALL,known_tempo?SH_WHITE:SH_DIM);
 /* BPM box */
 int master=sh_has(d,XZ_SHELL_F_MASTER)&&d->master;
 xz_border(s,bx,6,bw,STATUS_H-12,master?SH_RED:SH_WHITE);
 sh_text(s,bx+4,9,master?"MASTER":"BPM",SH_SMALL,master?SH_RED:SH_WHITE);
 if(loaded&&sh_has(d,XZ_SHELL_F_BPM)){
  int32_t b=d->bpm_x100<0?0:d->bpm_x100>99999?99999:d->bpm_x100;
  snprintf(text,sizeof text,"%d.%d",(int)(b/100),(int)((b/10)%10));
  text_fit_right(s,bx+bw-5,27,bw-8,text,SH_TEXT,master?SH_RED:SH_WHITE);
 }else sh_text_right(s,bx+bw-5,27,"---.-",SH_TEXT,SH_DIM);
}
static void render_status(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 xz_rect(c,0,STATUS_Y-2,W,STATUS_H+2,SH_BLACK);
 if(sh_deck_quarters(u,m,c,(struct xz_shell_rect){0,STATUS_Y-1,W,STATUS_H+1}))return;
 for(int deck=0;deck<sh_deck_count(m);deck++)render_status_deck(u,m,c,deck);
}

/* ================= MODS (the XZ_SHELL_UTILITY screen) ================= */

/* One rail on the left: UTILITY, the player's own menu, then each mod page. The open page fills the
   right: an item list whose categories (or sections) are header rows, and the values pane, as the stock
   UTILITY screen draws them. The rail is u->mods_view: tab = the open page, on_tabs = the rail has the
   encoder. The content's state is packed into the shell's view slot:
   cursor = id+1 of the item under the item cursor (0 = first item);
   focus  = pane | value cursor<<4 | touch pane+1<<16 | armed<<20 | dragged<<21;
   scroll = item scroll (in list rows, headers included) | value scroll<<12. */
enum pane { PANE_RAIL, PANE_ITEMS, PANE_VALUES };
struct ustate { int pane, item, value, item_scroll, value_scroll, touch_pane, armed, dragged; };
static struct ustate unpack(const struct xz_shell_view *v) {
 return (struct ustate){v->focus&3,v->cursor-1,(v->focus>>4)&0xfff,v->scroll&0xfff,(v->scroll>>12)&0xfff,
  ((v->focus>>16)&7)-1,(v->focus>>20)&1,(v->focus>>21)&1};
}
static void pack(struct xz_shell_view *v, struct ustate s) {
 v->cursor=s.item+1;
 v->focus=(s.pane&3)|((s.value&0xfff)<<4)|(((s.touch_pane+1)&7)<<16)|((s.armed&1)<<20)|((s.dragged&1)<<21);
 v->scroll=(s.item_scroll&0xfff)|((s.value_scroll&0xfff)<<12);
}

/* Derived layout for one model snapshot and one view. A list row is a table index, or -1-category for a
   header row. */
enum { LIST_MAX=MAX_ITEMS+16+XZ_SHELL_SETTING_CAT_COUNT };
struct ulayout {
 struct table t;
 int rows[LIST_MAX],n_rows,list_row;  /* the list, and the cursor item's row */
 int n_cats;
 int item;                            /* table index under the cursor, -1 when empty */
 int value_rows,desc_rows;            /* value pane: choice rows, then the description panel */
 int scope;                           /* whose descriptions: XZ_SHELL_HELP_UTILITY or the mod tab */
};
enum { DESC_W=VALUE_W-28 };
static struct ulayout ulayout_of(const struct xz_shell *u, const struct xz_shell_model *m, int item_id) {
 struct ulayout l;memset(&l,0,sizeof l);
 l.t=table_of(u,m);l.item=-1;
 int present[XZ_SHELL_SETTING_CAT_COUNT]={0};
 for(int i=0;i<l.t.count;i++)if(valid_category(&l.t.items[i]))present[l.t.items[i].category]=1;
 for(int cat=0;cat<XZ_SHELL_SETTING_CAT_COUNT;cat++)l.n_cats+=present[cat];
 if(!l.n_cats)return l;
 int at=item_id>=0?find_id(l.t,item_id):-1;
 if(at>=0&&!valid_category(&l.t.items[at]))at=-1;
 for(int cat=0;cat<XZ_SHELL_SETTING_CAT_COUNT;cat++){
  if(!present[cat])continue;
  if(l.n_cats>1&&l.n_rows<LIST_MAX)l.rows[l.n_rows++]=-1-cat;
  for(int i=0;i<l.t.count&&l.n_rows<LIST_MAX;i++)if(valid_category(&l.t.items[i])&&(int)l.t.items[i].category==cat){
   if(at<0)at=i;
   if(i==at)l.list_row=l.n_rows;
   l.rows[l.n_rows++]=i;
  }
 }
 l.item=at;
 l.scope=mods_tab(u,m)?u->mods_view.tab:XZ_SHELL_HELP_UTILITY;
 l.desc_rows=desc_rows(m,l.scope,&l.t.items[at],DESC_W);
 l.value_rows=sh_max(1,ROWS-l.desc_rows);
 return l;
}
static int clamp(int v, int lo, int hi) { return v<lo?lo:v>hi?hi:v; }
/* Scroll that keeps row visible, moving as little as possible from scroll. */
static int keep_visible(int scroll, int row, int count, int rows) {
 scroll=clamp(scroll,0,sh_max(0,count-rows));
 if(row<scroll)scroll=row;
 if(row>=scroll+rows)scroll=row-rows+1;
 return clamp(scroll,0,sh_max(0,count-rows));
}
/* The list scroll for the cursor row, with the header above a section's first item in view. */
static int list_scroll(const struct ulayout *l, int scroll) {
 scroll=keep_visible(scroll,l->list_row,l->n_rows,ROWS);
 if(l->list_row>0&&l->rows[l->list_row-1]<0&&scroll==l->list_row)scroll--;
 return scroll;
}
static int item_id_at(const struct ulayout *l, int table_index) { return table_index>=0?l->t.items[table_index].id:-1; }
/* Re-derive everything from the stored ids after any change. */
static struct ustate settle(const struct xz_shell *u, const struct xz_shell_model *m, struct ustate s, struct ulayout *l) {
 *l=ulayout_of(u,m,s.item);
 s.item=item_id_at(l,l->item);
 if(l->item<0){s.pane=PANE_ITEMS;s.item_scroll=s.value_scroll=s.value=0;return s;}
 const struct xz_shell_setting_item *it=&l->t.items[l->item];
 s.pane=clamp(s.pane,PANE_ITEMS,PANE_VALUES);
 if(s.pane==PANE_VALUES&&!selectable(it))s.pane=PANE_ITEMS;
 s.item_scroll=list_scroll(l,s.item_scroll);
 int n=choice_count(it);
 s.value=clamp(s.value,0,sh_max(0,n-1));
 s.value_scroll=keep_visible(s.value_scroll,s.value,n,l->value_rows);
 return s;
}
static int value_start(const struct xz_shell_setting_item *it) { int c=current_of(it);return c<0?0:c; }

static void scrollbar(struct xz_canvas c, int x, int y, int h, int scroll, int count, int rows) {
 if(count<=rows)return;
 int thumb=sh_max(16,h*rows/count),travel=h-thumb;
 int top=y+(count-rows>0?travel*scroll/(count-rows):0);
 xz_rect(c,x,top,3,thumb,0x8c8c8c);
}
/* UTILITY's categories, in the stock menu's words. */
static const char *const cat_names[XZ_SHELL_SETTING_CAT_COUNT]={
 "DJ SETTING","DISPLAY (LCD)","DISPLAY (INDICATOR)","PRO DJ LINK","MIXER","SYSTEM","INFO"
};
static const char *header_name(const struct xz_shell_mods_tab *tab, int cat) {
 if(!tab)return cat_names[cat];
 return tab->sections&&cat<tab->section_count&&tab->sections[cat]?tab->sections[cat]:"---";
}

/* ---- the page rail ---- */
static int tab_count(const struct xz_shell_model *m) { return 1+(m->mods&&m->mods->tabs?(int)sh_min((int)m->mods->count,11):0); }
static const char *tab_label(const struct xz_shell_model *m, int t) {
 if(t==0)return "UTILITY";
 const char *l=m->mods->tabs[t-1].label;
 return l?l:"---";
}
static int open_tab(const struct xz_shell *u, const struct xz_shell_model *m) { return sh_max(0,sh_min(u->mods_view.tab,tab_count(m)-1)); }
/* Entries that fit: UTILITY, the divider, then the mod pages; the mod pages scroll to keep the open one in view. */
enum { RAIL_GAP=8, RAIL_FIT=(ROWS*ROW_H-RAIL_GAP)/RAIL_H };
static int rail_first(const struct xz_shell *u, const struct xz_shell_model *m) {
 int n=tab_count(m)-1,shown=RAIL_FIT-1,t=u?open_tab(u,m):0;
 return n<=shown||t<1?0:clamp(t-shown,0,n-shown);
}
struct xz_shell_rect xz_shell_mods_tab_rect(const struct xz_shell *u, const struct xz_shell_model *m, int tab) {
 if(!m||tab<0||tab>=tab_count(m))return (struct xz_shell_rect){0,0,0,0};
 if(tab==0)return (struct xz_shell_rect){0,U_Y,RAIL_W,RAIL_H};
 int slot=tab-1-rail_first(u,m);
 if(slot<0||slot>=RAIL_FIT-1)return (struct xz_shell_rect){0,0,0,0};
 return (struct xz_shell_rect){0,U_Y+RAIL_H+RAIL_GAP+slot*RAIL_H,RAIL_W,RAIL_H};
}
struct xz_shell_rect xz_shell_mods_page_button(void) { return (struct xz_shell_rect){LIST_X+(W-LIST_X)/2-150,U_Y+190,300,48}; }
/* A page name in a rail cell: one line when it fits, else split at the space nearest the middle. */
static void rail_label(struct xz_canvas c, int y, const char *name, uint32_t ink) {
 int room=RAIL_W-8;
 if(sh_text_width(measure,name,SH_SMALL)<=room){text_fit_center(c,RAIL_W/2,y+RAIL_H/2-6,room,name,SH_SMALL,ink);return;}
 size_t len=strlen(name),best=0;
 for(size_t i=1;i<len&&i<63;i++)if(name[i]==' '&&(!best||(i>len/2?i-len/2:len/2-i)<(best>len/2?best-len/2:len/2-best)))best=i;
 if(!best){text_fit_center(c,RAIL_W/2,y+RAIL_H/2-6,room,name,SH_SMALL,ink);return;}
 char first[64];memcpy(first,name,best);first[best]=0;
 text_fit_center(c,RAIL_W/2,y+RAIL_H/2-13,room,first,SH_SMALL,ink);
 text_fit_center(c,RAIL_W/2,y+RAIL_H/2+1,room,name+best+1,SH_SMALL,ink);
}
static void render_pages(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 int cur=open_tab(u,m),n=tab_count(m),first=rail_first(u,m);
 xz_rect(c,0,U_Y,RAIL_W,ROWS*ROW_H,SH_CHROME);
 for(int t=0;t<n;t++){
  struct xz_shell_rect r=xz_shell_mods_tab_rect(u,m,t);
  if(!r.w)continue;
  int selected=t==cur;
  uint32_t fill=selected?(u->mods_view.on_tabs?SH_BLUE:RAIL_SELECTED):SH_CHROME,ink=selected?SH_WHITE:SH_DIM;
  xz_rect(c,r.x,r.y,r.w,r.h-2,fill);
  xz_rect(c,r.x,r.y+r.h-2,r.w,2,SH_BLACK);   /* the stock rail's cell gap */
  if(t==0){
   /* The player's own menu, said in a caption under its name. */
   text_fit_center(c,RAIL_W/2,r.y+10,RAIL_W-8,"UTILITY",SH_SMALL,ink);
   text_fit_center(c,RAIL_W/2,r.y+26,RAIL_W-8,"STOCK",SH_SMALL,selected?0xc8dcff:0x8c8c8c);
  }else rail_label(c,r.y,tab_label(m,t),ink);
 }
 /* The divider between the stock menu and the mod pages, labelled MODS when there are any. */
 int dy=U_Y+RAIL_H;
 xz_rect(c,0,dy,RAIL_W,RAIL_GAP,SH_BLACK);
 if(n>1)xz_rect(c,6,dy+RAIL_GAP/2-1,RAIL_W-12,2,0x5a5a5a);
 /* more mod pages above or below the ones shown */
 if(first>0)sh_triangle(c,RAIL_W-10,dy+RAIL_GAP+4,4,0,SH_DIM);
 if(n-1-first>RAIL_FIT-1)sh_triangle(c,RAIL_W-10,U_Y+ROWS*ROW_H-8,4,1,SH_DIM);
}

/* ---- the open page ---- */
static void render_items(const struct xz_shell_model *m, struct xz_canvas c, const struct ulayout *l, struct ustate s, const struct xz_shell_mods_tab *tab) {
 struct xz_canvas p=xz_sub(c,LIST_X,U_Y,LIST_W,ROWS*ROW_H);
 for(int r=0;r<ROWS;r++){
  int y=r*ROW_H,index=s.item_scroll+r;
  if(index>=l->n_rows){xz_rect(p,0,y,LIST_W,ROW_H-2,SH_CHROME);continue;}
  int at=l->rows[index];
  if(at<0){
   /* a category header, as the stock rail names it */
   xz_rect(p,0,y,LIST_W,ROW_H-2,SH_BLACK);
   text_fit(p,14,y+ROW_H-SH_CAP_SMALL-10,LIST_W-28,header_name(tab,-1-at),SH_SMALL,0x8c8c8c);
   xz_rect(p,14,y+ROW_H-5,LIST_W-28,1,SH_RAIL);
   continue;
  }
  int cursor=at==l->item;
  uint32_t fill=cursor?(s.pane==PANE_ITEMS?SH_BLUE:RAIL_SELECTED):SH_CHROME;
  xz_rect(p,0,y,LIST_W,ROW_H-2,fill);
  const struct xz_shell_setting_item *it=&l->t.items[at];
  int ro=selectable(it)&&!settable(m,it);
  const char *value=value_text(it);
  uint32_t value_ink=!it->known||current_of(it)<0?(cursor?SH_WHITE:READ_ONLY_GREY):cursor?SH_WHITE:GREY_TEXT;
  char vbuf[160];int vw=fit(value,SH_TEXT,170,vbuf,sizeof vbuf);
  int right=LIST_W-14;
  sh_text(p,right-vw,y+12,vbuf,SH_TEXT,value_ink);
  if(ro)lock_icon(p,right-vw-17,y+13,cursor?SH_WHITE:READ_ONLY_GREY);
  int name_w=right-vw-(ro?17:0)-16-14;
  text_fit(p,14,y+12,name_w,label_of(it),SH_TEXT,SH_WHITE);
 }
 scrollbar(p,LIST_W-4,2,ROWS*ROW_H-6,s.item_scroll,l->n_rows,ROWS);
}
static void render_values(const struct xz_shell_model *m, struct xz_canvas c, const struct ulayout *l, struct ustate s) {
 struct xz_canvas p=xz_sub(c,VALUE_X,U_Y,VALUE_W,ROWS*ROW_H);
 const struct xz_shell_setting_item *it=l->item>=0?&l->t.items[l->item]:NULL;
 for(int r=0;r<ROWS;r++)xz_rect(p,0,r*ROW_H,VALUE_W,ROW_H-2,r&1?SH_BLACK:SH_CHROME);
 if(!it)return;
 if(!selectable(it)){
  /* Information: the value itself, large, no radio; what it means under it. */
  text_fit(p,16,12,VALUE_W-32,value_text(it),SH_TEXT,it->known?SH_WHITE:SH_DIM);
  struct desc d;desc_of(m,l->scope,it,-1,DESC_W,&d);
  render_desc(p,&d,ROW_H,DESC_W);
  return;
 }
 int n=choice_count(it),cur=current_of(it);
 for(int r=0;r<l->value_rows;r++){
  int index=s.value_scroll+r,y=r*ROW_H;
  if(index>=n)break;
  int cursor=s.pane==PANE_VALUES&&index==s.value;
  if(cursor)xz_rect(p,0,y,VALUE_W,ROW_H-2,SH_BLUE);
  xz_ring(p,22,y+18,8,1,SH_WHITE);
  if(index==cur)xz_disc(p,22,y+18,4,cursor?SH_WHITE:SH_BLUE);
  text_fit(p,40,y+12,VALUE_W-56,choice_text(it,index),SH_TEXT,SH_WHITE);
 }
 scrollbar(p,VALUE_W-5,2,l->value_rows*ROW_H-6,s.value_scroll,n,l->value_rows);
 /* What the item does, and what the lit choice (or the current one) does, right under the choices. */
 if(l->desc_rows){
  struct desc d;desc_of(m,l->scope,it,s.pane==PANE_VALUES?s.value:cur,DESC_W,&d);
  render_desc(p,&d,sh_min(n,l->value_rows)*ROW_H,DESC_W);
 }
}
static void render_empty_note(struct xz_canvas c, const char *text) {
 xz_rect(c,LIST_X,U_Y,W-LIST_X,ROWS*ROW_H,SH_CHROME);
 sh_text_center(c,LIST_X+(W-LIST_X)/2,U_Y+ROW_H*4+12,text,SH_TEXT,SH_DIM);
}
static void version_of(struct table t, char *out, size_t cap) {
 out[0]=0;
 int at=find_id(t,XZ_SHELL_ITEM_VERSION);
 if(at<0)return;
 const struct xz_shell_setting_item *it=&t.items[at];
 int cur=current_of(it);
 if(cur<0||!it->choices[cur])return;
 const char *v=it->choices[cur];
 snprintf(out,cap,"%s%s",v[0]=='V'||v[0]=='v'?"":"Ver.",v);
}
static void render_page_tab(const struct xz_shell *u, const struct xz_shell_mods_tab *tab, struct xz_canvas c) {
 int cx=LIST_X+(W-LIST_X)/2,cw=W-LIST_X-60;
 xz_rect(c,LIST_X,U_Y,W-LIST_X,ROWS*ROW_H,SH_CHROME);
 text_fit_center(c,cx,U_Y+80,cw,tab->label?tab->label:"---",SH_TEXT,SH_WHITE);
 if(tab->summary)text_fit_center(c,cx,U_Y+116,cw,tab->summary,SH_SMALL,SH_DIM);
 text_fit_center(c,cx,U_Y+140,cw,"THIS PAGE OPENS IN THE MOD PANEL",SH_SMALL,SH_DIM);
 struct xz_shell_rect b=xz_shell_mods_page_button();
 int focused=!u->mods_view.on_tabs;
 if(focused)xz_rect(c,b.x,b.y,b.w,b.h,SH_BLUE);else{xz_rect(c,b.x,b.y,b.w,b.h,SH_RAIL);xz_border(c,b.x,b.y,b.w,b.h,SH_BLUE);}
 char text[48];snprintf(text,sizeof text,"OPEN %s",tab->label?tab->label:"PAGE");
 text_fit_center(c,b.x+b.w/2,b.y+(b.h-SH_CAP_TEXT)/2,b.w-20,text,SH_TEXT,SH_WHITE);
}
static void utility_render(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 struct ulayout l;
 struct ustate s=settle(u,m,unpack(&u->stack[u->depth-1]),&l);
 const struct xz_shell_mods_tab *tab=mods_tab(u,m);
 xz_rect(c,0,0,W,H,SH_BLACK);
 /* UTILITY reads as the player's own menu: its stock title and version, and one line saying so. */
 const char *title=tab?"MODS":"UTILITY";
 render_title(c,title);
 int after=52+sh_text_width(c,title,SH_TEXT)+14;
 render_pages(u,m,c);
 if(tab&&tab->kind==XZ_SHELL_TAB_MENU){
  /* A mod page the runtime describes: the shell draws its path in the title and the page itself. */
  sh_menu_render(u,m,c,tab,after,TITLE_RIGHT);
  render_status(u,m,c);
  return;
 }
 char version[64];version_of(l.t,version,sizeof version);
 int vw=!tab&&version[0]?sh_min(160,sh_text_width(c,version,SH_TEXT)):0;
 text_fit(c,after,17,TITLE_RIGHT-after-vw-12,tab?"ADDED BY THE MOD, NOT PART OF THE PLAYER":"THE PLAYER'S OWN MENU",SH_SMALL,SH_DIM);
 if(vw)text_fit_right(c,TITLE_RIGHT,13,160,version,SH_TEXT,DEEP_BLUE);
 if(tab&&tab->kind==XZ_SHELL_TAB_PAGE)render_page_tab(u,tab,c);
 else if(l.item<0)render_empty_note(c,tab?"NOTHING TO SET ON THIS PAGE":m->settings_screen?"NO SETTINGS READ YET":"SETTINGS NOT AVAILABLE HERE");
 else{
  struct ustate shown=s;
  if(u->mods_view.on_tabs)shown.pane=PANE_RAIL;   /* the rail has the encoder: no content row is lit */
  render_items(m,c,&l,shown,tab);
  render_values(m,c,&l,shown);
 }
 render_status(u,m,c);
}

/* ---- MODS input ---- */
static size_t hand_off(enum xz_shell_screen screen, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 return emit(out,0,(struct xz_shell_action){XZ_SHELL_ACT_HAND_OFF,-1,0,(int)screen});
}
/* A read-only item: the stock screen takes over at that item (value = 1 + its id, shell.h). */
static size_t hand_off_item(enum xz_shell_screen screen, const struct xz_shell_setting_item *it, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 return emit(out,0,(struct xz_shell_action){XZ_SHELL_ACT_HAND_OFF,-1,it->id+1,(int)screen});
}
static size_t apply(struct xz_shell *u, const struct xz_shell_model *m, struct ustate *s, const struct ulayout *l, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 const struct xz_shell_setting_item *it=&l->t.items[l->item];
 if(!settable(m,it))return on_mod_tab?0:hand_off_item(XZ_SHELL_UTILITY,it,out);
 s->pane=PANE_ITEMS;
 if(s->value==current_of(it))return 0;
 return set_value(u,it,s->value,out);
}
static void enter_values(struct ustate *s, const struct ulayout *l) {
 const struct xz_shell_setting_item *it=&l->t.items[l->item];
 if(!selectable(it))return;
 s->pane=PANE_VALUES;s->value=value_start(it);s->value_scroll=keep_visible(0,s->value,choice_count(it),l->value_rows);
}
static void select_row(struct ustate *s, const struct ulayout *l, int row) {
 s->item=l->t.items[l->rows[row]].id;s->value=0;s->value_scroll=0;
}
/* The item row delta items away from the cursor, past header rows; the cursor's own row at the ends. */
static int item_row(const struct ulayout *l, int delta) {
 int row=l->list_row,step=delta>0?1:-1;
 for(int k=delta>0?delta:-delta;k>0;k--){
  int next=row+step;
  while(next>=0&&next<l->n_rows&&l->rows[next]<0)next+=step;
  if(next<0||next>=l->n_rows)break;
  row=next;
 }
 return row;
}
static void move(struct ustate *s, const struct ulayout *l, int delta) {
 if(s->pane==PANE_ITEMS){int r=item_row(l,delta);if(r!=l->list_row)select_row(s,l,r);return;}
 s->value=clamp(s->value+delta,0,choice_count(&l->t.items[l->item])-1);
}
/* Encoder hold: the next category's first item (the stock rail's job), or the next page of values. */
static void page_jump(struct ustate *s, const struct ulayout *l) {
 const struct xz_shell_setting_item *it=&l->t.items[l->item];
 if(s->pane==PANE_ITEMS){
  int r=l->list_row+1;
  while(r<l->n_rows&&l->rows[r]>=0)r++;
  if(r>=l->n_rows)r=0;
  while(r<l->n_rows&&l->rows[r]<0)r++;
  if(r<l->n_rows){select_row(s,l,r);s->item_scroll=r>0&&l->rows[r-1]<0?r-1:r;}
 }else if(s->pane==PANE_VALUES){
  int n=choice_count(it);
  s->value=s->value>=n-1?0:sh_min(s->value+l->value_rows,n-1);s->value_scroll=s->value;
 }
}
static int pane_at(int x, int y) {
 for(int p=PANE_ITEMS;p<=PANE_VALUES;p++)
  if(sh_inside(xz_shell_settings_rect((enum xz_shell_settings_region)(XZ_SHELL_SR_RAIL+p)),x,y))return p;
 return -1;
}
static void utility_touch_down(struct xz_shell *u, struct ustate *s, const struct ulayout *l, int x, int y) {
 int p=pane_at(x,y);
 s->touch_pane=p;s->armed=0;s->dragged=0;
 u->down=1;u->down_region=XZ_SHELL_TOUCH_OTHER;u->down_deck=y;
 if(p<0||l->item<0)return;
 const struct xz_shell_setting_item *it=&l->t.items[l->item];
 int row=(y-U_Y)/ROW_H;
 if(p==PANE_ITEMS){
  int index=s->item_scroll+row;
  if(index>=l->n_rows||l->rows[index]<0)return;
  if(index!=l->list_row)select_row(s,l,index);
  s->pane=PANE_ITEMS;s->armed=1;
  return;
 }
 if(!selectable(it)||row>=l->value_rows)return;
 int index=s->value_scroll+row;
 if(index>=choice_count(it))return;
 s->value=index;s->pane=PANE_VALUES;s->armed=1;
 return;
}
static void utility_drag(struct xz_shell *u, struct ustate *s, const struct ulayout *l, int y) {
 int dy=y-u->down_deck;
 if(!s->dragged&&(dy>=8||dy<=-8))s->dragged=1;
 if(!s->dragged||s->touch_pane<PANE_ITEMS||l->item<0)return;
 int rows=dy/ROW_H;
 if(!rows)return;
 u->down_deck+=rows*ROW_H;
 const struct xz_shell_setting_item *it=&l->t.items[l->item];
 if(s->touch_pane==PANE_ITEMS){
  s->item_scroll=clamp(s->item_scroll-rows,0,sh_max(0,l->n_rows-ROWS));
  /* the cursor stays on an item row inside the window */
  int r=clamp(l->list_row,s->item_scroll,s->item_scroll+ROWS-1);
  if(r!=l->list_row){
   int down=r,up=r;
   while(down<l->n_rows&&down<s->item_scroll+ROWS&&l->rows[down]<0)down++;
   while(up>=s->item_scroll&&l->rows[up]<0)up--;
   r=down<l->n_rows&&down<s->item_scroll+ROWS&&l->rows[down]>=0?down:up>=s->item_scroll?up:l->list_row;
   if(r!=l->list_row)select_row(s,l,r);
  }
 }else if(selectable(it)){
  int n=choice_count(it);
  s->value_scroll=clamp(s->value_scroll-rows,0,sh_max(0,n-l->value_rows));
  s->value=clamp(s->value,s->value_scroll,s->value_scroll+l->value_rows-1);
 }
}
/* A MENU tab asks the runtime for its root page whenever it becomes the open tab. */
size_t sh_mods_opened(struct xz_shell *u, const struct xz_shell_model *m, struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t n) {
 const struct xz_shell_mods_tab *tab=mods_tab(u,m);
 u->menu_view.page=-1;
 if(!tab||tab->kind!=XZ_SHELL_TAB_MENU)return n;
 return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_MOD_MENU,-1,tab->page,XZ_SHELL_MENU_OPEN});
}
static size_t open_page(struct xz_shell *u, const struct xz_shell_model *m, struct xz_shell_view *v, int t, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 u->mods_view.on_tabs=1;
 if(t==u->mods_view.tab)return 0;
 u->mods_view.tab=t;*v=(struct xz_shell_view){XZ_SHELL_UTILITY,0,0,0};
 return sh_mods_opened(u,m,out,0);
}
const struct xz_shell_mods_tab *sh_mods_tab(const struct xz_shell *u, const struct xz_shell_model *m) { return mods_tab(u,m); }
void xz_shell_mods_show(struct xz_shell *u, const struct xz_shell_model *m, int tab) {
 if(!u||!m||tab<0||tab>=tab_count(m)||u->depth<1||u->depth>XZ_SHELL_STACK)return;
 int at=-1;
 for(int i=0;i<u->depth;i++)if(u->stack[i].screen==XZ_SHELL_UTILITY)at=i;
 memset(&u->panel,0,sizeof u->panel);u->grab.active=0;u->popup.kind=XZ_SHELL_POPUP_NONE;
 if(at>=0)u->depth=at+1;
 else{if(u->depth>=XZ_SHELL_STACK)return;u->depth++;}
 u->stack[u->depth-1]=(struct xz_shell_view){XZ_SHELL_UTILITY,0,0,0};
 u->mods_view.tab=tab;u->mods_view.on_tabs=0;
 u->menu_view.page=-1;
}
void xz_shell_mods_close(struct xz_shell *u) {
 if(!u||u->depth<1||u->depth>XZ_SHELL_STACK)return;
 for(int i=1;i<u->depth;i++)if(u->stack[i].screen==XZ_SHELL_UTILITY){u->depth=i;return;}
}
static size_t utility_input(struct xz_shell *u, const struct xz_shell_model *m, const struct xz_shell_event *e, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 struct xz_shell_view *v=&u->stack[u->depth-1];
 struct ulayout l;
 struct ustate s=settle(u,m,unpack(v),&l);
 size_t n=0;
 const struct xz_shell_mods_tab *tab=mods_tab(u,m);
 if(e->kind==XZ_SHELL_TOUCH_DOWN&&sh_inside(xz_shell_settings_rect(XZ_SHELL_SR_BACK_BUTTON),e->x,e->y)){
  if(!tab||tab->kind!=XZ_SHELL_TAB_MENU)pack(v,s);
  return sh_back(u,m,out);
 }
 /* The rail: a touch picks a page, a second touch on it enters; with the encoder on the rail, turn picks
    and push enters. */
 if(e->kind==XZ_SHELL_TOUCH_DOWN)for(int t=0;t<tab_count(m);t++)if(sh_inside(xz_shell_mods_tab_rect(u,m,t),e->x,e->y)){
  if(t==open_tab(u,m)&&u->mods_view.on_tabs){
   u->mods_view.on_tabs=0;
   if(!tab||tab->kind==XZ_SHELL_TAB_ITEMS){s.pane=PANE_ITEMS;pack(v,s);}
   if(tab&&tab->kind==XZ_SHELL_TAB_MENU)return sh_menu_enter(u,m,tab,out);
   return 0;
  }
  return open_page(u,m,v,t,out);
 }
 if(u->mods_view.on_tabs&&(e->kind==XZ_SHELL_ENCODER_TURN||e->kind==XZ_SHELL_ENCODER_PUSH||e->kind==XZ_SHELL_ENCODER_HOLD)){
  if(e->kind==XZ_SHELL_ENCODER_TURN){
   int t=sh_max(0,sh_min(tab_count(m)-1,open_tab(u,m)+e->delta));
   return t!=open_tab(u,m)?open_page(u,m,v,t,out):0;
  }
  if(e->kind==XZ_SHELL_ENCODER_PUSH){
   u->mods_view.on_tabs=0;
   if(!tab||tab->kind==XZ_SHELL_TAB_ITEMS){s.pane=PANE_ITEMS;pack(v,s);}
   if(tab&&tab->kind==XZ_SHELL_TAB_MENU)return sh_menu_enter(u,m,tab,out);
  }
  return 0;
 }
 if(tab&&tab->kind==XZ_SHELL_TAB_MENU)return sh_menu_input(u,m,tab,e,out);
 if(tab&&tab->kind==XZ_SHELL_TAB_PAGE){
  int open=(e->kind==XZ_SHELL_ENCODER_PUSH)||(e->kind==XZ_SHELL_TOUCH_DOWN&&sh_inside(xz_shell_mods_page_button(),e->x,e->y));
  if(e->kind==XZ_SHELL_TOUCH_DOWN&&open)u->mods_view.on_tabs=0;
  return open?emit(out,0,(struct xz_shell_action){XZ_SHELL_ACT_OPEN_MODS,-1,tab->page,0}):0;
 }
 /* A touch in the content takes the encoder off the rail. */
 if(e->kind==XZ_SHELL_TOUCH_DOWN&&pane_at(e->x,e->y)>=0)u->mods_view.on_tabs=0;
 /* The slot went away while the screen was open: the stock menu takes over on the next push or touch. */
 if(!tab&&!m->settings_screen&&(e->kind==XZ_SHELL_ENCODER_PUSH||e->kind==XZ_SHELL_TOUCH_DOWN))return hand_off(XZ_SHELL_UTILITY,out);
 switch(e->kind){
 case XZ_SHELL_TOUCH_DOWN:utility_touch_down(u,&s,&l,e->x,e->y);break;
 case XZ_SHELL_TOUCH_MOVE:if(u->down)utility_drag(u,&s,&l,e->y);break;
 case XZ_SHELL_TOUCH_UP:
  if(u->down&&s.armed&&!s.dragged&&l.item>=0){
   if(s.touch_pane==PANE_ITEMS)enter_values(&s,&l);
   else if(s.touch_pane==PANE_VALUES)n=apply(u,m,&s,&l,out);
  }
  u->down=0;u->down_region=XZ_SHELL_TOUCH_NONE;s.touch_pane=-1;s.armed=s.dragged=0;
  break;
 case XZ_SHELL_ENCODER_TURN:if(l.item>=0&&e->delta)move(&s,&l,e->delta);break;
 case XZ_SHELL_ENCODER_PUSH:
  if(l.item<0)break;
  if(s.pane==PANE_ITEMS)enter_values(&s,&l);
  else n=apply(u,m,&s,&l,out);
  break;
 case XZ_SHELL_ENCODER_HOLD:if(l.item>=0)page_jump(&s,&l);break;
 default:break;
 }
 struct ulayout after;
 s=settle(u,m,s,&after);
 pack(v,s);
 return n;
}
size_t sh_settings_back_step(struct xz_shell *u, const struct xz_shell_model *m, struct xz_shell_action out[XZ_SHELL_ACTIONS], int *taken) {
 *taken=0;
 if(u->depth>=1&&u->depth<=XZ_SHELL_STACK&&xz_shell_screen(u)==XZ_SHELL_UTILITY&&!u->mods_view.on_tabs){
  const struct xz_shell_mods_tab *tab=mods_tab(u,m);
  if(tab&&tab->kind==XZ_SHELL_TAB_MENU){*taken=1;return sh_menu_back(u,m,tab,out);}
 }
 *taken=xz_shell_settings_back(u,m);
 return 0;
}
int xz_shell_settings_back(struct xz_shell *u, const struct xz_shell_model *m) {
 if(!u||!m||u->depth<1||u->depth>XZ_SHELL_STACK||xz_shell_screen(u)!=XZ_SHELL_UTILITY)return 0;
 struct xz_shell_view *v=&u->stack[u->depth-1];
 if(u->mods_view.on_tabs)return 0;
 struct ulayout l;
 struct ustate s=settle(u,m,unpack(v),&l);
 const struct xz_shell_mods_tab *tab=mods_tab(u,m);
 if(tab&&tab->kind==XZ_SHELL_TAB_MENU){struct xz_shell_action dropped[XZ_SHELL_ACTIONS];return sh_menu_back(u,m,tab,dropped),1;}
 if((tab&&tab->kind==XZ_SHELL_TAB_PAGE)||l.item<0||s.pane<=PANE_ITEMS){xz_shell_mods_close(u);return 1;}
 s.pane--;
 pack(v,s);
 return 1;
}

/* ================= SHORTCUT ================= */

enum { Q_PLAYER, Q_DEVICE_INFO, Q_DEVICE, Q_MY, QUADS };
static const char *const quad_titles[QUADS]={"PLAYER SETTINGS","DECK STATUS","DEVICE SETTINGS","MY SETTINGS"};
/* Where each item sits on SHORTCUT: the first SLOTS candidates the table holds, in order.
   DEVICE INFORMATION lists table items after the rows the shell reads from the play model. */
enum { CANDIDATES=6 };
static const int placement[QUADS][CANDIDATES]={
 {XZ_SHELL_ITEM_QUANTIZE_BEAT,XZ_SHELL_ITEM_BEAT_JUMP_BEAT,XZ_SHELL_ITEM_HOT_CUE_AUTO_LOAD,XZ_SHELL_ITEM_WAVE_PHASE_METER,XZ_SHELL_ITEM_SLIP_FLASHING,-1},
 {XZ_SHELL_ITEM_VERSION,XZ_SHELL_ITEM_PLAYER_NUMBER,XZ_SHELL_ITEM_MIDI_CHANNEL,-1,-1,-1},
 {XZ_SHELL_ITEM_WAVE_COLOR,XZ_SHELL_ITEM_WAVE_POSITION,XZ_SHELL_ITEM_KEY_DISPLAY,XZ_SHELL_ITEM_WAVE_ZOOM,-1,-1},
 {XZ_SHELL_ITEM_TIME_MODE,XZ_SHELL_ITEM_PLAY_MODE,XZ_SHELL_ITEM_AUTO_CUE,XZ_SHELL_ITEM_AUTO_CUE_LEVEL,XZ_SHELL_ITEM_VINYL_SPEED_ADJUST,-1},
};
struct xz_shell_rect xz_shell_shortcut_quadrant(int q) {
 if(q<0||q>=QUADS)return (struct xz_shell_rect){0,0,0,0};
 return (struct xz_shell_rect){(q&1)*(QUAD_W+2),BODY_Y+(q>>1)*(QUAD_H+1),QUAD_W,QUAD_H};
}
struct xz_shell_rect xz_shell_shortcut_slot(int q, int slot) {
 struct xz_shell_rect r=xz_shell_shortcut_quadrant(q);
 if(!r.w||slot<0||slot>=SLOTS)return (struct xz_shell_rect){0,0,0,0};
 return (struct xz_shell_rect){r.x,r.y+QUAD_TITLE+slot*SLOT_H,r.w,SLOT_H+1};
}
/* Table indices on screen: slots[q][k], -1 for empty. */
struct slayout { struct table t; int slots[QUADS][SLOTS]; int order[QUADS*SLOTS],n_order; };
static struct slayout slayout_of(const struct xz_shell *u, const struct xz_shell_model *m) {
 struct slayout l;memset(&l,0,sizeof l);
 l.t=table_of(u,m);
 for(int q=0;q<QUADS;q++){
  int k=0;
  for(int i=0;i<SLOTS;i++)l.slots[q][i]=-1;
  for(int i=0;i<CANDIDATES&&k<SLOTS;i++){
   int at=placement[q][i]>=0?find_id(l.t,placement[q][i]):-1;
   if(at>=0)l.slots[q][k++]=at;
  }
 }
 /* Focus visits the setting quadrants in reading order; DEVICE INFORMATION only reads. */
 static const int reading[]={Q_PLAYER,Q_DEVICE_INFO,Q_DEVICE,Q_MY};
 for(int i=0;i<QUADS;i++){
  int q=reading[i];
  if(q==Q_DEVICE_INFO)continue;
  for(int k=0;k<SLOTS;k++)if(l.slots[q][k]>=0)l.order[l.n_order++]=l.slots[q][k];
 }
 return l;
}
static int focus_of(const struct slayout *l, int cursor) {
 for(int i=0;i<l->n_order;i++)if(l->t.items[l->order[i]].id==cursor-1)return i;
 return l->n_order?0:-1;
}
/* Chip row: equal segments in the large face, then the small face, else a stepper. */
enum chip_mode { CHIPS_TEXT, CHIPS_SMALL, CHIPS_STEPPER };
struct chips { enum chip_mode mode; int n, x, y, w, h, cw; };
static struct chips chips_of(const struct xz_shell_setting_item *it, struct xz_shell_rect slot) {
 struct chips k={CHIPS_STEPPER,choice_count(it),slot.x+CHIP_PAD,slot.y+CHIP_Y,slot.w-2*CHIP_PAD,CHIP_H,0};
 if(k.n<1)return k;
 k.cw=(k.w-CHIP_GAP*(k.n-1))/k.n;
 int widest_text=0,widest_small=0;
 for(int i=0;i<k.n;i++){
  widest_text=sh_max(widest_text,sh_text_width(measure,choice_text(it,i),SH_TEXT));
  widest_small=sh_max(widest_small,sh_text_width(measure,choice_text(it,i),SH_SMALL));
 }
 if(widest_text+12<=k.cw)k.mode=CHIPS_TEXT;
 else if(widest_small+8<=k.cw)k.mode=CHIPS_SMALL;
 return k;
}
static struct xz_shell_rect chip_rect(const struct chips *k, int i) {
 int x=k->x+i*(k->cw+CHIP_GAP);
 int w=i==k->n-1?k->x+k->w-x:k->cw;
 return (struct xz_shell_rect){x,k->y,w,k->h};
}
enum { STEP_W=40 };
/* A read-only item is not drawn as a control: its value in a flat box with a lock, and the runtime's
   note when it gives one. */
static void render_read_only(struct xz_canvas c, const struct xz_shell_setting_item *it, struct xz_shell_rect slot) {
 int x=slot.x+CHIP_PAD,y=slot.y+CHIP_Y,cur=current_of(it);
 const char *value=cur>=0?choice_text(it,cur):"---";
 int w=sh_max(84,sh_min(150,sh_text_width(c,value,SH_TEXT)+24));
 xz_rect(c,x,y,w,CHIP_H,SH_ROW_ALT);
 text_fit_center(c,x+w/2,y+5,w-10,value,SH_TEXT,cur>=0?0xd8d8d8:SH_DIM);
 int nx=x+w+10,nw=slot.x+slot.w-CHIP_PAD-nx-16;
 lock_icon(c,nx,y+7,READ_ONLY_GREY);
 const char *note=read_only_note(it);
 if(note)text_fit(c,nx+16,y+7,nw,note,SH_SMALL,GREY_TEXT);
}
/* ---- graphic controls ---- */
/* OFF / ON items draw as a switch: the whole row flips it. */
static int is_toggle(const struct xz_shell_setting_item *it) {
 return choice_count(it)==2&&it->choices[0]&&it->choices[1]&&!strcmp(it->choices[0],"OFF")&&!strcmp(it->choices[1],"ON");
}
static void render_switch(struct xz_canvas c, struct xz_shell_rect r, int on, int known) {
 int w=58,h=r.h,x=r.x,y=r.y;
 xz_round_rect(c,x,y,w,h,h/2,15,!known?SH_ROW_ALT:on?SH_BLUE:SH_RAIL);
 int knob=h/2-3,kx=on?x+w-h/2:x+h/2;
 if(known)xz_disc(c,kx,y+h/2,knob,SH_WHITE);
 sh_text(c,x+w+12,y+5,!known?"---":on?"ON":"OFF",SH_TEXT,!known?SH_DIM:on?SH_WHITE:0xd8d8d8);
}
/* A few columns that read as music, for the waveform colour swatches. */
static const struct xz_shell_column swatch_columns[40]={
 {250,120,90},{160,90,70},{90,100,120},{60,80,150},{240,130,80},{150,100,70},{80,150,200},{50,110,170},
 {230,140,90},{140,110,80},{70,160,210},{40,90,120},{220,120,70},{130,170,110},{60,190,230},{50,120,160},
 {250,150,100},{170,110,80},{100,130,200},{60,90,140},{240,130,90},{150,160,90},{80,170,220},{40,110,150},
 {230,150,80},{140,120,70},{70,140,210},{50,100,130},{220,130,90},{160,150,100},{90,180,230},{60,120,160},
 {250,140,90},{170,110,80},{100,140,200},{50,90,140},{240,120,80},{150,130,90},{80,160,220},{40,100,150}};
static void render_swatch(const struct xz_shell_model *m, struct xz_canvas c, struct xz_shell_rect r, enum xz_shell_wave_color mode) {
 struct xz_shell sw;memset(&sw,0,sizeof sw);sw.display.wave_color=mode;
 struct xz_shell_deck d;memset(&d,0,sizeof d);
 d.known=XZ_SHELL_KNOWN(XZ_SHELL_F_WAVE)|XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_WAVE);
 d.wave=d.stems=(struct xz_shell_wave){swatch_columns,40,1000};
 xz_rect(c,r.x,r.y,r.w,r.h,SH_BLACK);
 sh_deck_lane(c,r,r.h/2-1,&sw,m,&d,0,40);
}
/* WAVEFORM CURRENT POSITION: a small lane with its playhead where the choice puts it. */
static void render_position_icon(struct xz_canvas c, int x, int y, int left, uint32_t ink) {
 xz_border(c,x,y,30,14,ink);
 for(int i=0;i<26;i+=3)xz_rect(c,x+2+i,y+7-(i*7%5),1,1+2*(i*7%5),ink);
 xz_rect(c,x+(left?8:15),y-2,2,18,SH_PLAYHEAD);
}
/* TIME MODE: a bar that counts down (REMAIN) or up (TIME). */
static void render_time_icon(struct xz_canvas c, int x, int y, int remain, uint32_t ink) {
 xz_border(c,x,y,24,8,ink);
 if(remain)xz_rect(c,x+10,y+2,12,4,SH_RED);else xz_rect(c,x+2,y+2,12,4,ink);
}
static void render_chip_row(const struct xz_shell_model *m, struct xz_canvas c, const struct xz_shell_setting_item *it, struct xz_shell_rect slot) {
 struct chips k=chips_of(it,slot);
 int cur=current_of(it),live=settable(m,it);
 if(!live){render_read_only(c,it,slot);return;}
 if(is_toggle(it)){render_switch(c,(struct xz_shell_rect){k.x,k.y,k.w,k.h},cur==1,cur>=0);return;}
 if(it->id==XZ_SHELL_ITEM_WAVE_COLOR||it->id==XZ_SHELL_ITEM_WAVE_POSITION||it->id==XZ_SHELL_ITEM_TIME_MODE){
  for(int i=0;i<k.n;i++){
   struct xz_shell_rect r=chip_rect(&k,i);
   int selected=i==cur;
   xz_rect(c,r.x,r.y,r.w,r.h,selected?SH_BLUE:SH_ROW_ALT);
   if(!selected)xz_border(c,r.x,r.y,r.w,r.h,SH_RAIL);
   uint32_t ink=selected?SH_WHITE:0xd8d8d8;
   int tx=r.x+8;
   if(it->id==XZ_SHELL_ITEM_WAVE_COLOR){render_swatch(m,c,(struct xz_shell_rect){r.x+3,r.y+3,36,r.h-6},(enum xz_shell_wave_color)i);tx=r.x+44;}
   else if(it->id==XZ_SHELL_ITEM_WAVE_POSITION){render_position_icon(c,r.x+10,r.y+6,i==1,ink);tx=r.x+50;}
   else{render_time_icon(c,r.x+10,r.y+9,i==0,ink);tx=r.x+44;}
   text_fit(c,tx,r.y+8,r.x+r.w-tx-4,choice_text(it,i),SH_SMALL,ink);
  }
  return;
 }
 uint32_t on=live?SH_BLUE:RAIL_SELECTED,off_ink=live?0xd8d8d8:GREY_TEXT;
 if(k.mode==CHIPS_STEPPER){
  struct xz_shell_rect r={k.x,k.y,k.w,k.h};
  xz_rect(c,r.x,r.y,STEP_W,r.h,SH_RAIL);xz_rect(c,r.x+r.w-STEP_W,r.y,STEP_W,r.h,SH_RAIL);
  h_triangle(c,r.x+STEP_W/2-3,r.y+r.h/2,6,0,cur>0||cur<0?SH_WHITE:READ_ONLY_GREY);
  h_triangle(c,r.x+r.w-STEP_W/2-3,r.y+r.h/2,6,1,cur<k.n-1?SH_WHITE:READ_ONLY_GREY);
  int mx=r.x+STEP_W+CHIP_GAP,mw=r.w-2*(STEP_W+CHIP_GAP);
  xz_rect(c,mx,r.y,mw,r.h,cur>=0?on:SH_ROW_ALT);
  text_fit_center(c,mx+mw/2,r.y+5,mw-12,cur>=0?choice_text(it,cur):"---",SH_TEXT,cur>=0?SH_WHITE:SH_DIM);
  return;
 }
 enum sh_face face=k.mode==CHIPS_TEXT?SH_TEXT:SH_SMALL;
 for(int i=0;i<k.n;i++){
  struct xz_shell_rect r=chip_rect(&k,i);
  int selected=i==cur;
  xz_rect(c,r.x,r.y,r.w,r.h,selected?on:SH_ROW_ALT);
  if(!selected)xz_border(c,r.x,r.y,r.w,r.h,SH_RAIL);
  int ty=face==SH_TEXT?r.y+5:r.y+8;
  text_fit_center(c,r.x+r.w/2,ty,r.w-6,choice_text(it,i),face,selected?SH_WHITE:off_ink);
 }
}
/* ---- DECK STATUS lamps ---- */
enum { LAMP_W=64, LAMP_H=30, LAMP_GAP=6, LAMP_ROW=50, SELECT_LAMP_W=54, SELECT_GAP=8 };
static int deck_select_shown(const struct xz_shell_model *m) {
 return m&&sh_deck_count(m)==4&&(m->capabilities&XZ_SHELL_CAP_DECK_SELECT);
}
struct xz_shell_rect xz_shell_shortcut_lamp(const struct xz_shell_model *m, int deck, int lamp) {
 struct xz_shell_rect q=xz_shell_shortcut_quadrant(Q_DEVICE_INFO);
 if(deck<0||deck>=XZ_SHELL_SHORTCUT_DECKS||lamp<0||lamp>=XZ_SHELL_LAMP_COUNT)return (struct xz_shell_rect){0,0,0,0};
 int w=deck_select_shown(m)?SELECT_LAMP_W:LAMP_W;
 return (struct xz_shell_rect){q.x+CHIP_PAD+lamp*(w+LAMP_GAP),q.y+QUAD_TITLE+18+deck*LAMP_ROW,w,LAMP_H};
}
struct xz_shell_rect xz_shell_shortcut_deck_select(const struct xz_shell_model *m, int side) {
 if(!deck_select_shown(m)||side<0||side>=XZ_SHELL_SHORTCUT_DECKS)return (struct xz_shell_rect){0,0,0,0};
 struct xz_shell_rect q=xz_shell_shortcut_quadrant(Q_DEVICE_INFO),last=xz_shell_shortcut_lamp(m,side,XZ_SHELL_LAMP_COUNT-1);
 int x=last.x+last.w+SELECT_GAP;
 return (struct xz_shell_rect){x,last.y,q.x+q.w-CHIP_PAD-x,LAMP_H};
}
/* The side's two decks as one control: the deck its controls drive now is the lit half. */
static void render_deck_select(const struct xz_shell_model *m, struct xz_canvas c, int side) {
 struct xz_shell_rect r=xz_shell_shortcut_deck_select(m,side);
 if(!r.w)return;
 sh_text_right(c,r.x+r.w,r.y-14,"DECK SELECT",SH_SMALL,SH_DIM);
 int extra=(m->sides_extra>>side)&1,half=r.w/2;
 xz_rect(c,r.x,r.y,r.w,r.h,XZ_FLAGSHIP_TILE);
 for(int i=0;i<2;i++){
  int x=r.x+i*half,w=i?r.w-half:half,lit=i==extra;
  char text[4];snprintf(text,sizeof text,"%d",side+1+2*i);
  if(lit)xz_rect(c,x+2,r.y+2,w-4,r.h-4,SH_BLUE);
  sh_text_center(c,x+w/2,r.y+(r.h-SH_CAP_TEXT)/2,text,SH_TEXT,lit?SH_WHITE:GREY_TEXT);
 }
}
static void lamp(struct xz_canvas c, struct xz_shell_rect r, const char *label, int known, int on, uint32_t lit) {
 int ty=r.y+(r.h-SH_CAP_SMALL)/2;
 if(!known){xz_rect(c,r.x,r.y,r.w,r.h,0x141414);xz_border(c,r.x,r.y,r.w,r.h,0x2c2c2c);text_fit_center(c,r.x+r.w/2,ty,r.w-6,label,SH_SMALL,0x4a4a4a);return;}
 if(on){xz_rect(c,r.x,r.y,r.w,r.h,lit);text_fit_center(c,r.x+r.w/2,ty,r.w-6,label,SH_SMALL,xz_theme_luma(lit)>150?SH_BLACK:SH_WHITE);return;}
 xz_rect(c,r.x,r.y,r.w,r.h,0x141414);xz_border(c,r.x,r.y,r.w,r.h,xz_blend(0,lit,110));
 text_fit_center(c,r.x+r.w/2,ty,r.w-6,label,SH_SMALL,0x8c8c8c);
}
static void render_deck_status(const struct xz_shell_model *m, struct xz_canvas c, struct xz_shell_rect q) {
 char text[16];
 for(int deck=0;deck<XZ_SHELL_SHORTCUT_DECKS;deck++){
  const struct xz_shell_deck *d=&m->deck[deck];
  struct xz_shell_rect first=xz_shell_shortcut_lamp(m,deck,0);
  snprintf(text,sizeof text,"DECK %d",deck+1);
  sh_text(c,first.x,first.y-14,text,SH_SMALL,SH_DIM);
  if(sh_has(d,XZ_SHELL_F_QUANTIZE)&&d->quantize_on)snprintf(text,sizeof text,"Q %d%s%d",d->quantize_num,d->quantize_den>1?"/":"",d->quantize_den>1?d->quantize_den:0);
  else snprintf(text,sizeof text,"QUANTIZE");
  if(sh_has(d,XZ_SHELL_F_QUANTIZE)&&d->quantize_on&&d->quantize_den<=1)snprintf(text,sizeof text,"Q %d",d->quantize_num);
  if(sh_has(d,XZ_SHELL_F_QUANTIZE)&&d->quantize_on&&(d->quantize_num<=0||d->quantize_den<=0))snprintf(text,sizeof text,"Q ?");
  lamp(c,xz_shell_shortcut_lamp(m,deck,XZ_SHELL_LAMP_QUANTIZE),text,sh_has(d,XZ_SHELL_F_QUANTIZE),d->quantize_on,SH_RED);
  lamp(c,xz_shell_shortcut_lamp(m,deck,XZ_SHELL_LAMP_SYNC),"SYNC",sh_has(d,XZ_SHELL_F_SYNC),d->sync,SH_BLUE);
  lamp(c,xz_shell_shortcut_lamp(m,deck,XZ_SHELL_LAMP_SLIP),"SLIP",sh_has(d,XZ_SHELL_F_SLIP),d->slip,0xffbd59);
  lamp(c,xz_shell_shortcut_lamp(m,deck,XZ_SHELL_LAMP_MASTER),"MASTER",sh_has(d,XZ_SHELL_F_MASTER),d->master,0xff8c1a);
  int tm_known=sh_has(d,XZ_SHELL_F_TIME_MODE)||sh_setting(m,XZ_SHELL_SET_TIME_MODE);
  enum xz_shell_time_mode mode=sh_has(d,XZ_SHELL_F_TIME_MODE)?d->time_mode:m->settings.time_mode;
  lamp(c,xz_shell_shortcut_lamp(m,deck,XZ_SHELL_LAMP_TIME),mode==XZ_SHELL_TIME_REMAIN?"REMAIN":"TIME",tm_known,1,0x3c3c3c);
  render_deck_select(m,c,deck);
 }
 /* LINK beside the quadrant title */
 lamp(c,(struct xz_shell_rect){q.x+q.w-CHIP_PAD-72,q.y+1,72,15},m->link_known&&m->link_connected?"LINK":"LINK OFF",m->link_known,m->link_known&&m->link_connected,SH_BLUE);
 /* jump tiles */
 static const char *const labels[XZ_SHELL_JUMP_COUNT]={"TRACK","PLAYLIST","SEARCH","MATCHING"};
 for(int j=0;j<XZ_SHELL_JUMP_COUNT;j++){
  struct xz_shell_rect r=xz_shell_shortcut_jump(j);
  xz_rect(c,r.x,r.y,r.w,r.h,XZ_FLAGSHIP_TILE);
  int cx=r.x+r.w/2,iy=r.y+6;
  switch(j){
  case XZ_SHELL_JUMP_TRACK:sh_note(c,cx-5,iy,SH_WHITE);break;
  case XZ_SHELL_JUMP_PLAYLIST:
   xz_border(c,cx-8,iy,16,14,SH_WHITE);
   for(int i=0;i<3;i++){xz_rect(c,cx-5,iy+3+i*3,2,2,SH_WHITE);xz_rect(c,cx-2,iy+3+i*3,7,1,SH_WHITE);}
   break;
  case XZ_SHELL_JUMP_SEARCH:xz_ring(c,cx-2,iy+6,5,1,SH_WHITE);xz_line16(c,(cx+2)*16,(iy+10)*16,(cx+7)*16,(iy+15)*16,32,SH_WHITE);break;
  default:xz_ring(c,cx-4,iy+7,6,1,SH_WHITE);xz_ring(c,cx+4,iy+7,6,1,SH_WHITE);break;
  }
  sh_text_center(c,cx,r.y+r.h-14,labels[j],SH_SMALL,SH_WHITE);
 }
}
struct xz_shell_rect xz_shell_shortcut_jump(int jump) {
 struct xz_shell_rect q=xz_shell_shortcut_quadrant(Q_DEVICE_INFO);
 if(jump<0||jump>=XZ_SHELL_JUMP_COUNT)return (struct xz_shell_rect){0,0,0,0};
 int w=(q.w-2*CHIP_PAD-3*6)/4,y=q.y+QUAD_TITLE+18+2*LAMP_ROW-4;
 return (struct xz_shell_rect){q.x+CHIP_PAD+jump*(w+6),y,w,q.y+q.h-y-4};
}
/* A jump opens that list as the stock SHORTCUT does; MATCHING needs the runtime's matching list. */
static size_t jump_to(struct xz_shell *u, const struct xz_shell_model *m, int jump, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 switch(jump){
 case XZ_SHELL_JUMP_PLAYLIST:return sh_key(u,m,XZ_SHELL_KEY_PLAYLIST,-1,out);
 case XZ_SHELL_JUMP_SEARCH:return sh_key(u,m,XZ_SHELL_KEY_SEARCH,-1,out);
 default:{
  enum xz_shell_browse_category k=jump==XZ_SHELL_JUMP_TRACK?XZ_SHELL_CAT_TRACK:XZ_SHELL_CAT_MATCHING;
  const struct xz_shell_browse_model *b=m->browse;
  size_t n=sh_key(u,m,XZ_SHELL_KEY_BROWSE,-1,out);
  if(xz_shell_screen(u)!=XZ_SHELL_BROWSE)return n;
  if(!b||!((b->category_available>>k)&1)){
   u->notice="MATCHING OPENS IN THE DECK'S OWN BROWSER";u->notice_at_ms=m->clock_ms;
   return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_HAND_OFF,-1,0,XZ_SHELL_BROWSE});
  }
  return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_BROWSE,-1,(int32_t)k,XZ_SHELL_BROWSE_CATEGORY});
 }
 }
}
static size_t jump_to(struct xz_shell *u, const struct xz_shell_model *m, int jump, struct xz_shell_action out[XZ_SHELL_ACTIONS]);
static size_t lamp_touch(struct xz_shell *u, const struct xz_shell_model *m, int x, int y, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 for(int j=0;j<XZ_SHELL_JUMP_COUNT;j++)if(sh_inside(xz_shell_shortcut_jump(j),x,y))return jump_to(u,m,j,out);
 for(int deck=0;deck<XZ_SHELL_SHORTCUT_DECKS;deck++){
  const struct xz_shell_deck *d=&m->deck[deck];
  struct xz_shell_rect qz=xz_shell_shortcut_lamp(m,deck,XZ_SHELL_LAMP_QUANTIZE),tm=xz_shell_shortcut_lamp(m,deck,XZ_SHELL_LAMP_TIME);
  struct xz_shell_rect ds=xz_shell_shortcut_deck_select(m,deck);
  qz.y-=7;qz.h+=14;tm.y-=7;tm.h+=14;ds.y-=7;ds.h+=14;   /* 44 px targets */
  if(ds.w&&sh_inside(ds,x,y))return emit_one(out,(struct xz_shell_action){XZ_SHELL_ACT_DECK_SELECT,deck,0,0});
  if(sh_inside(qz,x,y)&&sh_writable(m,XZ_SHELL_SET_QUANTIZE)&&sh_has(d,XZ_SHELL_F_QUANTIZE))
   return emit_one(out,(struct xz_shell_action){XZ_SHELL_ACT_SET_SETTING,deck,!d->quantize_on,XZ_SHELL_SET_QUANTIZE});
  if(sh_inside(tm,x,y)&&sh_writable(m,XZ_SHELL_SET_TIME_MODE)){
   enum xz_shell_time_mode mode=sh_has(d,XZ_SHELL_F_TIME_MODE)?d->time_mode:m->settings.time_mode;
   return emit_one(out,(struct xz_shell_action){XZ_SHELL_ACT_SET_SETTING,deck,mode==XZ_SHELL_TIME_REMAIN?XZ_SHELL_TIME_ELAPSED:XZ_SHELL_TIME_REMAIN,XZ_SHELL_SET_TIME_MODE});
  }
 }
 return 0;
}
static void render_quadrant(const struct xz_shell_model *m, struct xz_canvas c, const struct slayout *l, int q, int focus_index) {
 struct xz_shell_rect r=xz_shell_shortcut_quadrant(q);
 xz_rect(c,r.x,r.y,r.w,r.h,SH_CHROME);
 sh_text(c,r.x+CHIP_PAD,r.y+5,quad_titles[q],SH_SMALL,SH_DIM);
 xz_rect(c,r.x+CHIP_PAD,r.y+QUAD_TITLE-1,r.w-2*CHIP_PAD,1,SH_RAIL);
 int shown=0;
 for(int k=0;k<SLOTS;k++){
  int at=l->slots[q][k];
  if(at<0)continue;
  shown++;
  const struct xz_shell_setting_item *it=&l->t.items[at];
  struct xz_shell_rect slot=xz_shell_shortcut_slot(q,k);slot.h=SLOT_H;
  if(q==Q_DEVICE_INFO){
   /* The table's VERSION reads at the foot, beside LINK. */
   if(k==0)text_fit_right(c,r.x+r.w-CHIP_PAD-82,r.y+5,140,value_text(it),SH_SMALL,SH_DIM);
   continue;
  }
  int focused=focus_index>=0&&l->order[focus_index]==at;
  if(focused){xz_rect(c,slot.x,slot.y,slot.w,slot.h,0x242a33);xz_rect(c,slot.x,slot.y+2,4,slot.h-4,SH_BLUE);}
  const char *word=settable(m,it)&&(!it->known||current_of(it)<0)?"UNKNOWN":NULL;
  int ww=word?sh_text_width(c,word,SH_SMALL):0;
  if(word)sh_text(c,slot.x+slot.w-CHIP_PAD-ww,slot.y+3,word,SH_SMALL,READ_ONLY_GREY);
  text_fit(c,slot.x+CHIP_PAD,slot.y+3,slot.w-2*CHIP_PAD-ww-10,label_of(it),SH_SMALL,focused?SH_WHITE:SH_DIM);
  render_chip_row(m,c,it,slot);
 }
 if(q==Q_DEVICE_INFO){render_deck_status(m,c,r);shown=1;}
 if(!shown)sh_text_center(c,r.x+r.w/2,r.y+r.h/2-6,"NOT READ ON THIS DECK YET",SH_SMALL,SH_DIM);
}
static struct xz_shell_rect wave_order_button(const struct xz_shell_model *m) {
 return sh_deck_count(m)==4?(struct xz_shell_rect){442,0,220,40}:(struct xz_shell_rect){0,0,0,0};
}
static void shortcut_render(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 struct slayout l=slayout_of(u,m);
 int focus=focus_of(&l,u->stack[u->depth-1].cursor);
 xz_rect(c,0,0,W,H,SH_BLACK);
 render_title(c,"SHORTCUT");
 /* The title line says what the focused setting does: its first sentence. */
 if(focus>=0){
  const char *help=sh_item_help(m,XZ_SHELL_HELP_UTILITY,&l.t.items[l.order[focus]]);
  if(help){
   char first[160];size_t k=0;
   while(help[k]&&k<sizeof first-1&&!(help[k]=='.'&&(help[k+1]==' '||!help[k+1]))){first[k]=help[k];k++;}
   first[k]=0;
   text_fit(c,190,16,(sh_deck_count(m)==4?430:TITLE_RIGHT)-190,first,SH_SMALL,0xc8c8c8);
  }
 }
 struct xz_shell_rect order=wave_order_button(m);
 if(order.w){
  xz_rect(c,order.x,order.y+2,order.w,order.h-4,XZ_FLAGSHIP_TILE);
  xz_border(c,order.x,order.y+2,order.w,order.h-4,SH_BLUE);
  sh_text_center(c,order.x+order.w/2,6,"WAVE ORDER",SH_SMALL,SH_DIM);
  sh_text_center(c,order.x+order.w/2,21,m->wave_order==1?"3  1  2  4":"1  2  3  4",SH_TEXT,SH_WHITE);
 }
 for(int q=0;q<QUADS;q++)render_quadrant(m,c,&l,q,focus);
 render_status(u,m,c);
}

/* ---- SHORTCUT input ---- */
static size_t set_or_hand_off(struct xz_shell *u, const struct xz_shell_model *m, const struct xz_shell_setting_item *it, int index, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 if(!settable(m,it))return hand_off_item(XZ_SHELL_SHORTCUT,it,out);
 if(index<0||index>=choice_count(it)||index==current_of(it))return 0;
 return set_value(u,it,index,out);
}
static size_t shortcut_touch(struct xz_shell *u, const struct xz_shell_model *m, const struct slayout *l, int x, int y, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 struct xz_shell_view *v=&u->stack[u->depth-1];
 if(sh_inside(xz_shell_settings_rect(XZ_SHELL_SR_BACK_BUTTON),x,y))return sh_back(u,m,out);
 if(sh_inside(xz_shell_shortcut_quadrant(Q_DEVICE_INFO),x,y))return lamp_touch(u,m,x,y,out);
 for(int q=0;q<QUADS;q++){
  if(q==Q_DEVICE_INFO)continue;
  for(int k=0;k<SLOTS;k++){
   int at=l->slots[q][k];
   struct xz_shell_rect slot=xz_shell_shortcut_slot(q,k);
   if(at<0||!sh_inside(slot,x,y))continue;
   const struct xz_shell_setting_item *it=&l->t.items[at];
   v->cursor=it->id+1;
   struct chips c=chips_of(it,slot);
   if(c.n<1||y<c.y-6)return 0;     /* the label line only moves focus */
   if(!settable(m,it))return hand_off_item(XZ_SHELL_SHORTCUT,it,out);
   int cur=current_of(it);
   if(is_toggle(it))return set_or_hand_off(u,m,it,cur==1?0:1,out);
   if(c.mode==CHIPS_STEPPER){
    if(x<c.x+STEP_W+CHIP_GAP)return set_or_hand_off(u,m,it,cur<0?0:sh_max(0,cur-1),out);
    if(x>=c.x+c.w-STEP_W-CHIP_GAP)return set_or_hand_off(u,m,it,cur<0?0:sh_min(c.n-1,cur+1),out);
    return 0;
   }
   for(int i=0;i<c.n;i++){
    struct xz_shell_rect r=chip_rect(&c,i);r.y=slot.y;r.h=slot.h;
    if(sh_inside(r,x,y))return set_or_hand_off(u,m,it,i,out);
   }
   return 0;
  }
 }
 return 0;
}
static size_t shortcut_input(struct xz_shell *u, const struct xz_shell_model *m, const struct xz_shell_event *e, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 struct xz_shell_rect order=wave_order_button(m);if(order.w)order.h=44;
 if(e->kind==XZ_SHELL_TOUCH_DOWN&&sh_inside(order,e->x,e->y))
  return emit_one(out,(struct xz_shell_action){XZ_SHELL_ACT_WAVE_ORDER,-1,m->wave_order!=1,0});
 struct xz_shell_view *v=&u->stack[u->depth-1];
 struct slayout l=slayout_of(u,m);
 int focus=focus_of(&l,v->cursor);
 if(!m->settings_screen&&(e->kind==XZ_SHELL_ENCODER_PUSH||(e->kind==XZ_SHELL_TOUCH_DOWN&&!sh_inside(xz_shell_settings_rect(XZ_SHELL_SR_BACK_BUTTON),e->x,e->y))))
  return hand_off(XZ_SHELL_SHORTCUT,out);
 switch(e->kind){
 case XZ_SHELL_TOUCH_DOWN:u->down=1;u->down_region=XZ_SHELL_TOUCH_OTHER;return shortcut_touch(u,m,&l,e->x,e->y,out);
 case XZ_SHELL_TOUCH_UP:u->down=0;u->down_region=XZ_SHELL_TOUCH_NONE;return 0;
 case XZ_SHELL_ENCODER_TURN:
  if(focus<0)return 0;
  focus=clamp(focus+e->delta,0,l.n_order-1);
  v->cursor=l.t.items[l.order[focus]].id+1;
  return 0;
 case XZ_SHELL_ENCODER_PUSH:{
  if(focus<0)return 0;
  const struct xz_shell_setting_item *it=&l.t.items[l.order[focus]];
  v->cursor=it->id+1;
  int n=choice_count(it),cur=current_of(it);
  if(n<1)return 0;
  return set_or_hand_off(u,m,it,cur<0?0:(cur+1)%n,out);
 }
 default:return 0;
 }
}

const struct sh_screen xz_shell_shortcut_screen={shortcut_render,shortcut_input};
const struct sh_screen xz_shell_utility_screen={utility_render,utility_input};
