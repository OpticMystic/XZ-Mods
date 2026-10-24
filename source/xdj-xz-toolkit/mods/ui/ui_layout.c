#include "ui_skin.h"
#include "ui_about.h"
#include "stem_pads.h"
#include "wave_viewport.h"
#include "../key/browser.h"
#include <stdio.h>
enum {X0=XZ_UI_CX,CW=XZ_UI_CW,TOP=XZ_UI_HEAD,ROW=48,GAP=8};
static void put(struct xz_ui_widget *a,int x,int y,int w,int h){a->x=x;a->y=y;a->w=w;a->h=h;}
/* Column i of n across the content width, rows of 40 px on a 48 px pitch. */
static void grid(struct xz_ui_widget *a,int n,int i,int row,int rows){
 int pitch=CW+GAP;put(a,X0+i*pitch/n,XZ_UI_CY+row*ROW,pitch/n-GAP,rows*ROW-GAP);
}
/* APPEARANCE sections: a caption at APPEARANCE_HEAD(row), its controls below in equal columns. */
enum { APPEARANCE_Y=50, APPEARANCE_PITCH=78, APPEARANCE_H=44 };
#define APPEARANCE_HEAD(row) (APPEARANCE_Y+(row)*APPEARANCE_PITCH)
static void appearance(struct xz_ui_widget *a,int row,int cols,int i){
 int pitch=CW+GAP;put(a,X0+i*pitch/cols,APPEARANCE_HEAD(row)+16,pitch/cols-GAP,APPEARANCE_H);
}
/* CONTROLS > BUTTONS: per slot a caption at BUTTONS_HEAD(slot), its six choices in two rows of three
   below, then one help line at BUTTONS_HELP(slot). */
enum { BUTTONS_Y=98, BUTTONS_PITCH=140, BUTTONS_H=40 };
#define BUTTONS_HEAD(slot) (BUTTONS_Y+(slot)*BUTTONS_PITCH)
#define BUTTONS_HELP(slot) (BUTTONS_HEAD(slot)+16+2*ROW+2)
static void buttons(struct xz_ui_widget *a,int k){
 int slot=k/XZ_UI_RB_CHOICES,choice=k%XZ_UI_RB_CHOICES,pitch=CW+GAP;
 put(a,X0+(choice%3)*pitch/3,BUTTONS_HEAD(slot)+16+(choice/3)*ROW,pitch/3-GAP,BUTTONS_H);
}
static int root(int page){return page==XZ_UI_GENERAL||page==XZ_UI_CONTROLS||page==XZ_UI_THEMES||page==XZ_UI_SETTINGS||page==XZ_UI_EXTRAS||page==XZ_UI_NETWORK;}
static int rail_slot(int page){return page==XZ_UI_GENERAL?0:page==XZ_UI_CONTROLS?1:page==XZ_UI_THEMES?2:page==XZ_UI_SETTINGS?3:page==XZ_UI_EXTRAS?4:5;}
/* The DJ Profile list hangs from the header's right corner: rows of 52 on a 56 pitch,
   then one row of buttons. With no profiles it holds a message and the setup button. */
static int profile_buttons_y(const struct xz_ui *u,const struct xz_ui_model *m){
 int rows=xz_ui_profile_window(u,m,NULL);
 return rows?XZ_UI_PROFILE_LIST_Y+28+rows*56+4:XZ_UI_PROFILE_LIST_Y+80;
}
int xz_ui_profile_list_bottom(const struct xz_ui *u,const struct xz_ui_model *m){return profile_buttons_y(u,m)+48;}
/* Pads mirror the two physical rows of four, split into pairs like the hardware. */
static void pad(struct xz_ui_widget *a,int k){put(a,X0+(k%4)*96+((k%4)/2)*16,192+(k/4)*96,88,88);}
static int general_row(const struct xz_ui_widget *a,int *side){
 *side=0;
 switch(a->kind){
 case XZ_UI_LOCAL_STEM_SETUP_OPEN:return 0;
 case XZ_UI_ENABLE:return a->index==XZ_UI_STEM?0:a->index==XZ_UI_GATE?3:a->index==XZ_UI_SMART?4:a->index==XZ_UI_PREVIEW?5:6;
 case XZ_UI_JUMP_ENABLE:return 2;
 case XZ_UI_FEATURE_SETUP_OPEN:return a->index==XZ_UI_GATE?3:a->index==XZ_UI_SMART?4:a->index==XZ_UI_PREVIEW?5:6;
 case XZ_UI_FEATURE_LINK:
  if(a->index==XZ_UI_PC_STEMS)return 1;
  if(a->index==XZ_UI_METRICS_SETUP)return 7;
  if(a->index==XZ_UI_NETWORK){*side=1;return 7;}
  *side=1;
  return a->index==XZ_UI_CONTROLS?0:a->index==XZ_UI_BEAT_JUMP?2:a->index==XZ_UI_XPAD?6:8;
 default:return -1;
 }
}
static void place_panel(const struct xz_ui *u,const struct xz_ui_model *m,struct xz_ui_widget *a){
 int k=a->index,page=u->page;
 if(root(k)){put(a,0,XZ_UI_CY+rail_slot(k)*60,XZ_UI_RAIL_W,56);return;}
 if(page==XZ_UI_SETTINGS){int row=k==XZ_UI_STEMS?0:k==XZ_UI_XPAD?1:2;put(a,X0,64+row*80,CW,64);return;}
 if(page==XZ_UI_EXTRAS){int row=k==XZ_UI_ABOUT?0:k==XZ_UI_CONNECTION?1:k==XZ_UI_GAMES?2:3;put(a,X0,56+row*80,CW,64);return;}
 if(page==XZ_UI_THEMES){
  if(k==XZ_UI_THEME_PICK){appearance(a,1,2,0);return;}
  if(k==XZ_UI_BROWSE_SETUP){appearance(a,3,3,1);return;}
  if(k==XZ_UI_STEM_COLORS){appearance(a,3,3,2);return;}
 }
 if(k==XZ_UI_PROFILE_SETUP&&u->profile_open){
  int y=profile_buttons_y(u,m);
  if(m->dj&&m->dj->count>XZ_UI_PROFILE_ROWS)put(a,XZ_UI_PROFILE_LIST_X+184,y,168,40);
  else put(a,XZ_UI_PROFILE_LIST_X+8,y,XZ_UI_PROFILE_LIST_W-16,40);
  return;
 }
 switch(k){
 case XZ_UI_STEMS:grid(a,4,0,1,1);break;
 case XZ_UI_XPAD:grid(a,4,1,1,1);break;
 case XZ_UI_KEY_SETUP:if(page==XZ_UI_BROWSE_SETUP)grid(a,3,2,6,1);else grid(a,4,2,1,1);break;
 case XZ_UI_RAM_SETUP:grid(a,4,3,1,1);break;
 case XZ_UI_SUPPORT:if(page==XZ_UI_SETTINGS)grid(a,4,2,1,1);else grid(a,2,0,6,1);break;
 case XZ_UI_BROWSE_SETUP:grid(a,2,1,5,1);break;
 /* Theme 24 takes the first slot of the last row; STEM COLORS keeps the right half. */
 case XZ_UI_STEM_COLORS:grid(a,2,1,6,1);break;
 case XZ_UI_CONNECTION:put(a,X0,XZ_UI_CY,324,170);break;
 case XZ_UI_GAMES:if(page==XZ_UI_EXTRAS)put(a,468,XZ_UI_CY,324,170);else grid(a,2,1,6,1);break;
 case XZ_UI_ABOUT:put(a,468,226,324,170);break;
 case XZ_UI_APPS:put(a,X0,226,324,170);break;
 case XZ_UI_UPDATES:if(page==XZ_UI_EXTRAS)put(a,468,226,324,170);else grid(a,2,1,6,1);break;
 case XZ_UI_DOOM_FILES:
  if(page==XZ_UI_GAMES)put(a,X0,224,324,80);
  else if(page==XZ_UI_DOOM)put(a,468,320,324,56);
  else grid(a,1,0,4,1);
  break;
 case XZ_UI_MYHOUSE_SETUP:put(a,468,224,324,80);break;
 case XZ_UI_DOOM:grid(a,2,1,6,1);break;
 case XZ_UI_DOOM_SETUP:put(a,632,364,160,40);break;
 case XZ_UI_UPDATE_SETUP:grid(a,2,1,6,1);break;
 default:grid(a,2,1,6,1);break;}
}
void xz_ui_place(const struct xz_ui *u,const struct xz_ui_model *m,struct xz_ui_widget *w,size_t n){
 for(size_t i=0;i<n;i++){
  struct xz_ui_widget *a=&w[i];int k=a->index,page=u->page;
  if(xz_ui_profile_place(u,m,a))continue;
  if(page==XZ_UI_GENERAL&&!u->profile_open){
   int side=0,row=general_row(a,&side);
   if(row>=0){
    int paired=row==0||row==2||row==6||row==7||row==8;
    if(a->kind==XZ_UI_FEATURE_LINK&&a->index==XZ_UI_SETTINGS){side=0;paired=1;}
    put(a,X0+(side?448:0),48+row*40,paired?(side?CW-448:440):CW,36);continue;
   }
  }
  switch(a->kind){
  case XZ_UI_FEATURE_LINK:
   /* PC STEMS sits beside the STANDALONE STEMS switch; stem settings join the bottom links. */
   if(k==XZ_UI_METRICS_SETUP){grid(a,1,0,5,1);break;}
   grid(a,2,k==XZ_UI_STEM_COLORS||k==XZ_UI_CONTROLS?0:1,k==XZ_UI_PC_STEMS?0:k==XZ_UI_BEAT_JUMP?1:k==XZ_UI_STEM_COLORS||k==XZ_UI_XPAD?4:k==XZ_UI_METRICS_SETUP?5:6,1);break;
  case XZ_UI_FEATURE_SETUP_OPEN:
   grid(a,2,k==XZ_UI_GATE||k==XZ_UI_PREVIEW?0:1,k==XZ_UI_GATE||k==XZ_UI_SMART?2:3,1);break;
  case XZ_UI_LOCAL_STEM_SETUP_OPEN:grid(a,2,0,0,1);break;
  case XZ_UI_FEATURE_SETUP_STEP:grid(a,1,0,6,1);break;
  case XZ_UI_METRICS_TOGGLE:grid(a,2,0,5,1);break;
  case XZ_UI_PANEL:place_panel(u,m,a);break;
  case XZ_UI_BACK:put(a,0,0,XZ_UI_RAIL_W,TOP);break;
  case XZ_UI_CLOSE:put(a,720,0,80,TOP);break;
  /* Header deck pickers give up width to the DJ Profile chip when there is one. */
  case XZ_UI_DECK:if(page==XZ_UI_PC_STEMS)put(a,X0+k*208,96,200,40);else if(m->dj)put(a,300+k*176,0,170,TOP);else put(a,k?510:300,0,206,TOP);break;
  /* DJ PROFILE: avatar, name and caret in the corner; avatar and caret only beside deck pickers. */
  case XZ_UI_PROFILE:if(xz_ui_header_decks(u->page))put(a,652,0,64,TOP);else put(a,568,0,148,TOP);break;
  case XZ_UI_PROFILE_PICK:{int first;xz_ui_profile_window(u,m,&first);
   put(a,XZ_UI_PROFILE_LIST_X+8,XZ_UI_PROFILE_LIST_Y+28+(k-first)*56,XZ_UI_PROFILE_LIST_W-16,52);break;}
  case XZ_UI_PROFILE_PAGE:put(a,XZ_UI_PROFILE_LIST_X+8+(k<0?0:88),profile_buttons_y(u,m),80,40);break;
  case XZ_UI_CONTROL_SECTION:grid(a,4,k,0,1);break;
  case XZ_UI_REKORDBOX_ASSIGN:buttons(a,k);break;
  /* CONTROLS: stem shortcuts */
  case XZ_UI_ENABLE:
   if(k==XZ_UI_STEM)grid(a,2,0,0,1);
   else grid(a,2,k==XZ_UI_GATE||k==XZ_UI_PREVIEW?0:1,k==XZ_UI_GATE||k==XZ_UI_SMART?2:3,1);
   break;
  case XZ_UI_STEMS_OVERLAY:grid(a,2,0,1,1);break;
  case XZ_UI_FOUR_WAVE_ORDER:grid(a,2,1,1,1);break;
  case XZ_UI_STEM_PAGE:grid(a,4,k,2,1);break;
  case XZ_UI_STEM_PAD_CONFIG:pad(a,k);break;
  case XZ_UI_PAD_FEEDBACK:put(a,552,192,240,40);break;
  case XZ_UI_STEM_ORDER:put(a,552,240,240,40);break;
  case XZ_UI_STEM_BANK:grid(a,4,k,3,1);break;
  /* BEAT JUMP */
  case XZ_UI_JUMP_ENABLE:grid(a,2,0,1,1);break;
  case XZ_UI_JUMP_SHIFT:grid(a,1,0,1,1);break;
  case XZ_UI_JUMP_EDIT_PAGE:put(a,X0+k*204,144,196,40);break;
  case XZ_UI_JUMP_PAIR:pad(a,k);break;
  case XZ_UI_JUMP_STEP:put(a,552,k>0?192:336,240,40);break;
  /* PC STEMS */
  case XZ_UI_PC_CHECK:case XZ_UI_PC_TEST:put(a,X0,336,440,40);break;
  case XZ_UI_PC_CONFIRM:if(k)put(a,X0,336,440,40);else put(a,584,336,208,40);break;
  /* ADVANCED */
  case XZ_UI_KEY_SHIFT:if(k<0)put(a,X0,144,96,40);else if(!k)put(a,240,144,124,40);else put(a,372,144,96,40);break;
  case XZ_UI_KEY_SYNC:put(a,476,144,150,40);break;
  case XZ_UI_SHIFT_KEYSYNC:if(page==XZ_UI_KEY_SETUP)grid(a,2,0,5,1);else put(a,634,144,158,40);break;
  case XZ_UI_WAVE_MODE:if(page==XZ_UI_THEMES)appearance(a,2,3,k);else grid(a,3,k,3,1);break;
  case XZ_UI_SHIFT_PAGES:grid(a,4,0,4,1);break;
  case XZ_UI_KEY_FEEDBACK:if(page==XZ_UI_BROWSE_SETUP)grid(a,1,0,3,1);else if(page==XZ_UI_THEMES)appearance(a,3,3,0);else grid(a,4,1,4,1);break;
  case XZ_UI_RAM_DEBUG:if(page==XZ_UI_RAM_SETUP)grid(a,2,0,6,1);else grid(a,4,2,4,1);break;
  case XZ_UI_REKORDBOX_KEY:grid(a,2,0,3,1);break;
  case XZ_UI_KEY_CHECK:grid(a,1,0,4,1);break;
  case XZ_UI_KEY_FORMAT:grid(a,3,k,2,1);break;
  case XZ_UI_BROWSE_COLUMN:put(a,236+(k%4)*139,96+(k/4)*52,131,44);break;
  case XZ_UI_BROWSE_KEY_PREFIX:grid(a,2,0,4,1);break;
  case XZ_UI_BROWSE_TRAFFIC:grid(a,2,1,4,1);break;
  case XZ_UI_RAM_DECK:grid(a,2,k,1,1);break;
  case XZ_UI_RAM_REQUEST:case XZ_UI_RAM_CANCEL:case XZ_UI_RAM_RELEASE:grid(a,1,0,5,1);break;
  case XZ_UI_LOCAL_STEM_SETUP_STEP:grid(a,1,0,6,1);break;
  case XZ_UI_METRICS_CHECK:grid(a,1,0,6,1);break;
  /* GROOVE PADS and X-PAD */
  case XZ_UI_GROOVE_PAD:case XZ_UI_HOTCUE_PAD:put(a,X0+(k%4)*166,152+(k/4)*120,158,112);break;
  case XZ_UI_STRIP:put(a,X0,140,480,148);break;
  case XZ_UI_HOLD:put(a,624,140,168,40);break;
  case XZ_UI_OVERDUB:put(a,624,188,168,40);break;
  case XZ_UI_VOLUME:put(a,624,250,168,40);break;
  case XZ_UI_SAMPLE_PAD:put(a,X0+k*82,304,76,72);break;
  /* APPEARANCE */
  case XZ_UI_ABOUT_LINK:xz_about_place(a,k);break;
  case XZ_UI_SET_THEME:grid(a,4,k%4,k/4,1);break;
  case XZ_UI_SET_INTERFACE:appearance(a,0,2,k==XZ_UI_INTERFACE_MODERN?0:1);break;
  case XZ_UI_SET_MODERN_THEME:appearance(a,1,2,0);break;
  case XZ_UI_STEM_COLOR:{int stem=k/XZ_STEM_COLOR_CHOICES,choice=k%XZ_STEM_COLOR_CHOICES;
   put(a,X0+stem*221+(choice%3)*72,84+(choice/3)*70,64,62);break;}
  case XZ_UI_STEM_COLORS_RESET:put(a,X0,348,CW,40);break;
  /* VJ.TOOLS: controls on the right, live status on the left */
  case XZ_UI_TAKEOVER_TOGGLE:put(a,468,XZ_UI_CY,324,44);break;
  case XZ_UI_TAKEOVER_ASSIGN:put(a,468+(k?165:0),114,159,40);break; /* LINK, ONSCREEN (REKORDBOX retired) */
  case XZ_UI_CONNECTION_ENABLE:put(a,468,184,324,44);break;
  case XZ_UI_DISCOVERY:put(a,468,236,324,44);break;
  /* GAMES */
  case XZ_UI_GAME_SELECT:put(a,k?468:X0,XZ_UI_CY,324,80);break;
  case XZ_UI_ARCADE_OPEN:put(a,X0,136,324,80);break;
  case XZ_UI_WAVE_RIDER_OPEN:put(a,468,136,324,80);break;
  case XZ_UI_RHYTHM_LAB_OPEN:put(a,X0,312,324,80);break;
  case XZ_UI_GLYPH_RUN_OPEN:put(a,468,312,324,80);break;
  case XZ_UI_MYHOUSE_CHECK:grid(a,1,0,5,1);break;
  case XZ_UI_DOOM_START:put(a,X0,320,324,56);break;
  case XZ_UI_DOOM_CHECK:grid(a,1,0,5,1);break;
  case XZ_UI_WAD_SELECT:put(a,X0,96+(k-m->doom.offset)*44,CW,40);break;
  case XZ_UI_WAD_PAGE:put(a,k<0?X0:220,364,76,40);break;
  case XZ_UI_WAD_SCAN:put(a,304,364,156,40);break;
  case XZ_UI_WAD_CLEAR:put(a,468,364,156,40);break;
  /* NETWORK UPDATES */
  case XZ_UI_UPDATE_CHECK:put(a,X0,page==XZ_UI_UPDATE_SETUP?240:192,CW,48);break;
  case XZ_UI_UPDATE_INSTALL:put(a,X0,248,324,48);break;
  case XZ_UI_UPDATE_ROLLBACK:put(a,468,248,324,48);break;
  default:grid(a,1,0,6,1);break;}
 }
}

/* APPEARANCE section captions with a hairline to the right edge, in every skin's own colours. */
void xz_ui_appearance_details(const struct xz_ui_scene *s){
 uint32_t bg,ink,accent;xz_ui_chrome_colors(s->m,s->p,&bg,&ink,&accent);(void)accent;
 uint32_t dim=xz_blend(bg,ink,150),rule=xz_blend(bg,ink,48);
 static const char *const heads[4]={"INTERFACE","THEME","WAVEFORM","KEYS AND STEMS"};
 for(int row=0;row<4;row++){
  int y=APPEARANCE_HEAD(row),w=xz_text_width(s->c,heads[row],1,40);
  xz_text(s->c,X0,y,heads[row],1,40,dim);
  xz_rect(s->c,X0+w+10,y+5,CW-w-10,1,rule);
 }
 if(s->m->ui_interface==XZ_UI_INTERFACE_MODERN){
  int pitch=CW+GAP,x=X0+pitch/2,y=APPEARANCE_HEAD(1)+16+(APPEARANCE_H-9)/2;
  xz_text(s->c,x,y,"MORE MODERN THEMES ARE COMING",1,40,dim);
 }
}
static void label(struct xz_canvas c,int x,int y,int width,const char *value,int scale,uint32_t color){
 struct xz_canvas box=xz_sub(c,x,y,width,c.height-y);
 if(xz_text_width(box,value,scale,200)>width)scale=1;
 xz_text(box,0,0,value,scale,200,color);
}
void xz_ui_buttons_details(const struct xz_ui_scene *s){
 uint32_t bg,ink,accent;xz_ui_chrome_colors(s->m,s->p,&bg,&ink,&accent);(void)accent;
 uint32_t dim=xz_blend(bg,ink,150),rule=xz_blend(bg,ink,48);
 static const char *const heads[2]={"REKORDBOX BUTTON","SHIFT + REKORDBOX"};
 for(int slot=0;slot<2;slot++){
  int y=BUTTONS_HEAD(slot),w=xz_text_width(s->c,heads[slot],1,40);
  xz_text(s->c,X0,y,heads[slot],1,40,dim);
  xz_rect(s->c,X0+w+10,y+5,CW-w-10,1,rule);
  const char *help=xz_ui_rekordbox_choice_help(s->m->rekordbox_assign[slot]);
  label(s->c,X0,BUTTONS_HELP(slot),CW,help?help:"",1,ink);
 }
}
/* Live state only. What a control does is the info dock's job. */
void xz_ui_page_details(const struct xz_ui_scene *s){
 struct xz_canvas c=s->c;const struct xz_ui *u=s->u;const struct xz_ui_model *m=s->m;
 const struct xz_ui_deck *d=s->d;const struct xz_theme_palette *p=s->p;
 uint32_t ink=m->theme==XZ_THEME_ANALOG?0x3a3020:p->ink,bg=m->theme==XZ_THEME_ANALOG?0xd9c9a3:p->bg;
 uint32_t dim=xz_blend(bg,ink,170);char buf[96];
 if(u->page==XZ_UI_FEATURE_SETUP){
  const char *name=u->feature_cap==XZ_UI_GATE?"GATE CUE":u->feature_cap==XZ_UI_SMART?"SMART CUE":u->feature_cap==XZ_UI_PREVIEW?"HOT-CUE PREVIEW":"X-PAD ENGINE";
  label(c,X0,52,CW,name,2,ink);
  label(c,X0,78,CW,"1 DETECT  /  2 CHOOSE DECK  /  3 ENABLE  /  4 TEST",1,dim);
  static const char *instructions[4]={"DETECT THE FEATURE ON A LOADED USB DECK.","CHOOSE DECK 1 OR DECK 2 ABOVE.","ENABLE THE FEATURE USING ITS SAVED SETTING.","TEST THE FEATURE ON THIS DECK, THEN CONFIRM BELOW."};
  int step=u->feature_step>=0&&u->feature_step<4?u->feature_step:0;
  label(c,X0,140,CW,instructions[step],1,ink);
  label(c,X0,172,CW,d->track&&*d->track?d->track:"NO TRACK LOADED ON THIS DECK",2,ink);
  label(c,X0,204,CW,(d->ready&(uint32_t)u->feature_cap)?"FEATURE DETECTED":"LOAD A USB TRACK. IF STILL MISSING, UPDATE THE USB LOADER IN XZ MODS.",1,dim);
  if(step==3){
   label(c,X0,248,CW,"CLOSE MODS TO TEST, THEN REOPEN THIS SETUP TO CONFIRM.",1,dim);
   label(c,X0,276,CW,u->feature_cap==XZ_UI_GATE?"HOLD AND RELEASE A HOT CUE TO CHECK GATE CUE.":u->feature_cap==XZ_UI_SMART?"RECALL A HOT CUE AND CHECK THAT THE CUE POINT FOLLOWS.":u->feature_cap==XZ_UI_PREVIEW?"HOLD A HOT CUE WHILE PAUSED AND LISTEN FOR ITS PREVIEW.":"OPEN X-PAD SETTINGS AND CHECK A LOADED SAMPLE.",1,ink);
  }
 }else if(u->page==XZ_UI_LOCAL_STEM_SETUP){
  /* One deck, one reason, one next step. The button below is that step. */
  int deck=u->deck>=0&&u->deck<2?u->deck:0,four=xz_ui_stem_count(m,deck)==4;
  enum xz_ui_usb_reason why=xz_ui_usb_blocked(m,deck);
  uint32_t tone=why==XZ_UI_USB_WHY_ERROR||why==XZ_UI_USB_WHY_ENGINE?p->alarm:why==XZ_UI_USB_WHY_NONE?p->accent:ink;
  label(c,X0,52,CW,"STANDALONE STEMS SETUP",2,ink);
  label(c,X0,78,CW,"1 CHECK ENGINE  /  2 TURN ON (ALL DECKS)  /  3 LOAD A PREPARED TRACK",1,dim);
  label(c,X0,104,CW,d->track&&*d->track?d->track:"NO TRACK LOADED ON THIS DECK",2,ink);
  snprintf(buf,sizeof(buf),"DECK %d: %s",deck+1,xz_ui_usb_reason_text(why));
  label(c,X0,132,CW,buf,2,tone);
  /* An error names what is wrong with the set; otherwise say what was found. */
  if(why==XZ_UI_USB_WHY_ERROR)label(c,X0,158,CW,d->stem_error&&*d->stem_error?d->stem_error:d->status?d->status:"THE STEM SET COULD NOT BE READ",1,p->alarm);
  else if(d->stem_format&&*d->stem_format){
   snprintf(buf,sizeof(buf),four?"FOUND: %s / BASS ON PAD D OR H":"FOUND: %s",d->stem_format);label(c,X0,158,CW,buf,1,ink);
  }
  label(c,X0,180,CW,xz_ui_usb_next_step(why),1,ink);
  label(c,X0,204,CW,"CHOOSE DECK 1 OR DECK 2 ABOVE / PC STEMS HAVE THEIR OWN SETUP",1,dim);
  if(four)label(c,X0,224,CW,"4-STEM SETS CARRY NO STEM WAVEFORM: THE STEMS WAVEFORM SHOWS 3-BAND",1,dim);
 }else if(u->page==XZ_UI_STEMS||u->page==XZ_UI_XPAD){
  int wave_y=u->page==XZ_UI_STEMS?108:100,half=u->page==XZ_UI_STEMS?22:16;
  label(c,X0,52,460,d->track?d->track:"NO TRACK LOADED",2,ink);
  if(d->bpm>0)snprintf(buf,sizeof(buf),"%.1f BPM",(double)d->bpm);else snprintf(buf,sizeof(buf),"---.- BPM");
  label(c,612,52,180,buf,2,ink);
  if(d->wave_peaks&&d->wave_count){
   size_t n=d->wave_count<328?d->wave_count:328;
   for(size_t j=0;j<n;j++){int h=d->wave_peaks[j*d->wave_count/n]*half*2/255;if(h<2)h=2;xz_rect(c,X0+(int)(j*CW/n),wave_y-h/2,2,h,p->accent);}
  }else{xz_rect(c,X0,wave_y,CW,1,dim);xz_rect(c,X0+CW/2-110,wave_y-10,220,20,bg);label(c,X0+CW/2-66,wave_y-4,220,"WAVEFORM NOT AVAILABLE",1,dim);}
  if(u->page==XZ_UI_XPAD){
   snprintf(buf,sizeof(buf),"LOOP %s BEATS  /  %+.1f KEY",xz_ui_lengths[d->loop_index>=0&&d->loop_index<6?d->loop_index:0],(double)d->pitch);
   label(c,X0,124,480,buf,1,ink);label(c,624,238,168,"VOLUME",1,dim);
  }
 }else if(u->page==XZ_UI_MYHOUSE_SETUP){
  label(c,X0,52,CW,"MY HOUSE / NATIVE ENGINE SETUP",2,ink);
  label(c,X0,96,CW,"1  DETECT THE NATIVE GZDOOM ENGINE",1,ink);
  label(c,X0,124,CW,"2  CHOOSE DOOM II DATA AND MYHOUSE.PK3 ON USB",1,ink);
  label(c,X0,152,CW,"3  CHECK THE ENGINE, THEN LIVE-VERIFY THE GAME",1,ink);
  label(c,X0,188,CW,"THE CLASSIC DOOM ENGINE CANNOT LOAD THIS MOD",1,dim);
 }else if(u->page==XZ_UI_DOOM){
  label(c,X0,52,CW,m->doom.chex?"CHEX QUEST":"DOOM",2,ink);
  label(c,X0,78,CW,m->doom_ready?"USB GAME FILES READY":"USB GAME SETUP NEEDED",1,m->doom_ready?p->accent:p->alarm);
  static const char *keys[][2]={{"JOG","TURN LEFT / RIGHT"},{"PLAY","WALK FORWARD"},{"CUE","SHOOT"},
   {"PAD A / B","FORWARD / BACK"},{"PAD C / D","SHOOT / USE"},{"PAD E / F","STRAFE"},{"PAD G / H","ENTER / MENU"}};
  for(int k=0;k<7;k++){
   int x=X0+(k%2)*332,y=104+(k/2)*30;
   xz_round_rect(c,x,y,92,22,6,15,xz_blend(bg,ink,34));label(c,x+8,y+7,80,keys[k][0],1,ink);
   label(c,x+102,y+7,220,keys[k][1],1,ink);
  }
  label(c,X0,232,CW,m->doom.selection[0]?m->doom.selection:"BUTTONS CONTROL THE GAME WHILE IT IS OPEN",1,dim);
  label(c,X0,252,CW,"EXIT RESTORES THE DJ SCREEN / GAME SOUND IS OFF",1,dim);
 }else if(u->page==XZ_UI_DOOM_SETUP){
  label(c,X0,52,CW,"USB GAME SETUP",2,ink);
  label(c,X0,92,CW,"1  DETECT THE DOOM ENGINE FROM THE USB LOADER",1,ink);
  label(c,X0,120,CW,"2  CHOOSE A USB BASE GAME AND OPTIONAL MAP",1,ink);
  label(c,X0,138,CW,"   USB: WADS OR VJTOOLS/DOOM",1,dim);
  label(c,X0,166,CW,"3  CHECK USB, THEN START DOOM TO VERIFY",1,ink);
  label(c,X0,200,CW,m->doom_ready?"ENGINE AND GAME DATA DETECTED":"PREPARE A DOOM USB WITH THE XZ MODS APP",1,m->doom_ready?p->accent:dim);
 }else if(u->page==XZ_UI_DOOM_FILES){
  label(c,X0,52,CW,"USB WAD LOADER",2,ink);
  label(c,X0,76,CW,m->doom.selection,1,ink);
  for(int i=0;i<m->doom.count;i++)label(c,X0+16,96+i*44+26,600,m->doom.details[i],1,dim);
  if(!m->doom.count)label(c,X0,160,CW,m->doom.scanning?"SEARCHING USB PORTS...":"PUT .WAD FILES ON USB, THEN RESCAN",2,ink);
 }else if(u->page==XZ_UI_UPDATES){
  label(c,X0,52,CW,"NETWORK UPDATES",2,ink);
  label(c,X0,88,CW,"DOWNLOAD A SIGNED XZ MOD RUNTIME TO USB",1,ink);
  label(c,X0,108,CW,"POWER CYCLE TO ACTIVATE / EXISTING MUSIC STAYS",1,dim);
  label(c,X0,136,CW,m->update.status,2,ink);
  label(c,X0,164,CW,"MODS ONLY / THE ORIGINAL USB LOADER IS THE RECOVERY",1,dim);
 }else if(u->page==XZ_UI_UPDATE_SETUP){
  label(c,X0,52,CW,"NETWORK UPDATE SETUP",2,ink);
  label(c,X0,92,CW,"1  USE THE UPDATED XZ MODS USB LOADER",1,ink);
  label(c,X0,120,CW,"2  START NETWORK UPDATES IN THE DESKTOP APP",1,ink);
  label(c,X0,148,CW,"3  CONNECT THE XZ TO THAT NETWORK",1,ink);
  label(c,X0,184,CW,m->update.status,1,ink);
 }else if(u->page==XZ_UI_BROWSE_SETUP){
  label(c,X0,52,CW,"KEY FORMAT",2,ink);
  label(c,X0,78,CW,"USED BY THE DECK KEY STRIPS / SAVED TO YOUR USB PROFILE",1,dim);
  label(c,X0,98,CW,"BROWSE COLUMNS AND SORT: TAP A COLUMN ABOVE THE TRACK LIST, OR PRESS MENU",1,dim);
 }else if(u->page==XZ_UI_RAM_SETUP){
  label(c,X0,52,CW,"RAM TRACKS",2,ink);
  label(c,X0,78,CW,"1 CHOOSE A DECK   2 LOAD INTO RAM   3 WAIT FOR READY",1,dim);
  if(u->deck>1){label(c,X0,160,CW,"CHOOSE DECK 1 OR DECK 2",2,ink);return;}
  const struct xz_ui_ram *r=&m->deck[u->deck].ram;
  static const char *hints[]={
   "CHECK AGAIN. IF IT STAYS HERE, REBUILD THE USB LOADER WITH THE CURRENT XZ MODS APP",
   "LOAD A USB TRACK ON THIS DECK, THEN LOAD IT INTO RAM",
   "KEEP THE USB CONNECTED UNTIL THIS SAYS ORIGINAL AUDIO IN RAM",
   "USB STILL CONNECTED / RELEASE FREES THE MEMORY",
   "RETRY. IF IT FAILS AGAIN, RELOAD THE TRACK FROM USB",
   "LOAD ANOTHER TRACK TO RELEASE THIS RAM COPY"};
  unsigned state=(unsigned)r->state<6?(unsigned)r->state:0;
  uint32_t tone=state==XZ_UI_RAM_ERROR?p->alarm:ink;
  snprintf(buf,sizeof(buf),"DECK %d / %s",u->deck+1,xz_ui_ram_state_text((enum xz_ui_ram_state)state));
  label(c,X0,148,CW,buf,2,tone);
  snprintf(buf,sizeof(buf),"SOURCE: %s",d->track&&*d->track?d->track:"NO TRACK REPORTED");
  label(c,X0,172,CW,buf,1,ink);
  label(c,X0,190,CW,r->message[0]?r->message:hints[state],1,tone);
  int fill=0;
  if(r->bytes)fill=(int)((uint64_t)(r->copied<r->bytes?r->copied:r->bytes)*(unsigned)CW/r->bytes);
  xz_round_rect(c,X0,210,CW,8,4,15,xz_blend(bg,ink,48));
  if(fill)xz_round_rect(c,X0,210,fill,8,4,15,state==XZ_UI_RAM_ERROR?p->alarm:p->accent);
  if(r->bytes)snprintf(buf,sizeof(buf),"%.1f / %.1f MB COPIED",(double)r->copied/1048576.0,(double)r->bytes/1048576.0);
  else snprintf(buf,sizeof(buf),"NOTHING COPIED YET");
  label(c,X0,226,CW,buf,1,dim);
  if(state==XZ_UI_RAM_DETACHED)label(c,X0,254,CW,"RELEASE IS OFF WHILE THE USB IS OUT",2,ink);
  label(c,468,346,324,"ONE TRACK PER DECK / ORIGINAL AUDIO ONLY",1,dim);
  label(c,468,362,324,"STEMS AND 3-BAND WAVES STILL READ THE USB",1,dim);
 }else if(u->page==XZ_UI_CONNECTION){
  const struct xz_ui_connection *v=&m->connection;
  label(c,X0,52,320,"VJ.TOOLS CONNECTION",2,ink);
  label(c,X0,76,320,"OPTIONAL COMPUTER LINK",1,dim);
  static const char *labels[]={"STATUS","DEVICE IP","PORT","PROTOCOL","RECEIVED FPS"};
  char port[20];if(v->port)snprintf(port,sizeof(port),"%u",v->port);else snprintf(port,sizeof(port),"NOT REPORTED");
  if(v->ready&&v->connected&&v->stats_valid)snprintf(buf,sizeof(buf),"%.1f HZ",(double)v->frame_hz);else snprintf(buf,sizeof(buf),"NOT REPORTED");
  const char *values[]={!v->ready?"NOT AVAILABLE":v->connected?"CONNECTED":v->enabled?"WAITING FOR VJ.TOOLS":"DISABLED",v->device_ip&&*v->device_ip?v->device_ip:"NOT REPORTED",port,v->protocol&&*v->protocol?v->protocol:"NOT REPORTED",buf};
  for(int k=0;k<5;k++){
   int y=100+k*34;
   xz_rect(c,X0,y-6,320,1,xz_blend(bg,ink,30));
   label(c,X0,y+4,96,labels[k],1,dim);label(c,X0+104,y,216,values[k],2,k==0&&v->ready&&v->connected?p->accent:ink);
  }
  label(c,X0,276,320,m->fb_takeover?"COMPUTER SCREEN ACTIVE":"NATIVE PLAY SCREEN ACTIVE",1,m->fb_takeover?p->accent:dim);
  label(c,468,100,324,"SCREEN SWITCH KEY",1,dim);
  label(c,468,296,324,"OPTIONAL VJ.TOOLS LIBRARY CONNECTION",1,dim);
  label(c,468,312,324,"vj.tools",2,ink);
  label(c,X0,380,CW,"DJ MODS: CDJ3K-MODS / NSAINTOT + CONTRIBUTORS",1,dim);
 }
}

static void chevron(struct xz_canvas c,int x,int y,int dir,uint32_t color){
 xz_line16(c,x*16,(y-5)*16,(x+5*dir)*16,y*16,28,color);xz_line16(c,(x+5*dir)*16,y*16,x*16,(y+5)*16,28,color);
}
void xz_ui_stem_setup(const struct xz_ui_scene *s){
 struct xz_canvas c=s->c;const struct xz_ui_model *m=s->m;const struct xz_theme_palette *p=s->p;
 uint32_t ink=m->theme==XZ_THEME_ANALOG?0x3a3020:p->ink,bg=m->theme==XZ_THEME_ANALOG?0xd9c9a3:p->bg;
 for(size_t i=0;i<s->n;i++)if(s->w[i].kind==XZ_UI_STEMS_OVERLAY){
  struct xz_ui_widget a=s->w[i];
  xz_frame(c,m->theme,a,bg,m->stems_overlay!=0);
  xz_rect(c,a.x+2,a.y+2,a.w-4,a.h-4,bg);
  label(c,a.x+10,a.y+5,a.w-60,"STEM ROW LAYOUT",1,xz_blend(bg,ink,170));
  label(c,a.x+10,a.y+19,a.w-60,xz_wave_layout_name(m->stems_overlay),2,ink);
  chevron(c,a.x+a.w-38,a.y+a.h/2,-1,p->accent);chevron(c,a.x+a.w-20,a.y+a.h/2,1,p->accent);
 }
 for(size_t i=0;i<s->n;i++)if(s->w[i].kind==XZ_UI_STEM_ORDER){
  struct xz_ui_widget a=s->w[i];static const char *chip[3]={"DRUM","HARM","VOC"};
  xz_frame(c,m->theme,a,bg,0);xz_rect(c,a.x+2,a.y+2,a.w-4,a.h-4,bg);
  label(c,a.x+10,a.y+5,a.w-60,"STEM ORDER",1,xz_blend(bg,ink,170));
  for(int col=0;col<3;col++){int st=xz_stem_at(m->stem_order,col);int x=a.x+10+col*60;uint32_t sc=p->stem[st];
   xz_round_rect(c,x,a.y+19,56,16,4,15,sc);
   label(c,x+6,a.y+23,44,chip[st],1,xz_theme_luma(sc)>142?0x000000:0xffffff);}
  chevron(c,a.x+a.w-38,a.y+a.h/2,-1,p->accent);chevron(c,a.x+a.w-20,a.y+a.h/2,1,p->accent);
 }
 static const char *roles[]={"DRUMS","HARMONICS","VOCALS"};
 static const char *modes[]={"HOT CUE","BEAT LOOP","SLIP LOOP","BEAT JUMP"};
 int conflict=m->stem_page==3&&m->jump.enabled;
 for(size_t i=0;i<s->n;i++)if(s->w[i].kind==XZ_UI_STEM_PAD_CONFIG){
  struct xz_ui_widget a=s->w[i];int pad=a.index,assigned=pad/4==m->stem_bank,k=pad%4;
  int st=k<3?xz_stem_at(m->stem_order,k):-1;uint32_t role=st>=0?p->stem[st]:p->accent;
  uint32_t fill=xz_blend(p->bg,assigned?role:p->ink,assigned?60:14);
  xz_round_rect(c,a.x,a.y,a.w,a.h,10,15,assigned?role:xz_blend(p->bg,p->ink,60));
  xz_round_rect(c,a.x+2,a.y+2,a.w-4,a.h-4,8,15,fill);
  if(assigned)xz_round_rect(c,a.x+8,a.y+a.h-10,a.w-16,4,2,15,role);
  char letter[2]={(char)('A'+pad),0};xz_text(c,a.x+10,a.y+8,letter,2,2,p->ink);
  label(c,a.x+10,a.y+34,a.w-20,assigned?(st>=0?roles[st]:"BYPASS"):"NATIVE",1,p->ink);
  /* Slot 3 is BASS on a 4-stem deck; say so only while one is loaded. */
  int bass=k==3&&(xz_ui_stem_count(m,0)==4||xz_ui_stem_count(m,1)==4);
  label(c,a.x+10,a.y+52,a.w-20,assigned?(conflict?"OVERRIDDEN":k<3?"MUTE":bass?"4-STEM: BASS":"BYPASS"):modes[m->stem_page],1,xz_blend(p->bg,p->ink,170));
 }
 label(c,552,292,240,"STEMS USE",1,xz_blend(bg,ink,170));label(c,552,306,240,m->stem_bank?"PADS E-H":"PADS A-D",2,ink);
 if(conflict){label(c,552,336,240,"BEAT JUMP HAS PRIORITY",1,p->alarm);label(c,552,352,240,"CHOOSE ANOTHER PAD MODE",1,ink);}
 else{label(c,552,336,240,"OTHER PADS KEEP",1,ink);label(c,552,352,240,"THEIR NORMAL ACTION",1,ink);}
 if(!(m->enabled&XZ_UI_STEM))label(c,552,376,240,"STEM AUDIO IS OFF",1,p->alarm);
}

/* Shared color editor keeps identical touch targets in all skins. */
void xz_ui_stem_colors(const struct xz_ui_scene *s){
 struct xz_canvas c=s->c;const struct xz_theme_palette *p=s->p;
 static const char *roles[3]={"DRUMS","HARMONICS","VOCALS"};
 xz_rect(c,X0,XZ_UI_CY,CW,XZ_UI_CB-XZ_UI_CY,p->bg);
 for(int stem=0;stem<3;stem++){
  int x=X0+stem*221;uint32_t color=p->stem[stem];
  xz_round_rect(c,x,54,8,20,4,15,color);label(c,x+16,56,190,roles[stem],2,p->ink);
 }
 for(size_t i=0;i<s->n;i++){
  struct xz_ui_widget a=s->w[i];if(a.kind!=XZ_UI_STEM_COLOR&&a.kind!=XZ_UI_STEM_COLORS_RESET)continue;
  int selected=s->look[i].selected;uint32_t color=s->look[i].color;
  xz_frame(c,s->m->theme,a,xz_blend(p->bg,p->ink,24),selected);
  if(a.kind==XZ_UI_STEM_COLOR){
   xz_round_rect(c,a.x+8,a.y+7,a.w-16,28,6,15,color);
   if(selected){xz_border(c,a.x+4,a.y+3,a.w-8,36,p->ink);xz_border(c,a.x+5,a.y+4,a.w-10,34,p->bg);}
   label(c,a.x+5,a.y+44,a.w-10,a.label,1,p->ink);
  }else label(c,a.x+12,a.y+13,a.w-24,a.label,2,p->ink);
 }
 /* A readout, not a picker: 4-stem bass takes the theme colour that stays
    clear of the three choices above, the same colour as rekordbox bass. */
 uint32_t bass=xz_ui_model_local_stem_color(s->m,XZ_STEM_BASS);
 xz_round_rect(c,X0,294,8,10,3,15,bass);
 label(c,X0+16,295,CW-16,"BASS FOLLOWS THEME / 4-STEM TRACKS",1,p->ink);
 /* Immediate two-sided STEMS preview: drums outside, harmonics, vocals inside. */
 for(int x=0;x<CW;x++){
  int h=7+(x/12)%7;
  xz_rect(c,X0+x,322-h,1,h*2,p->stem[0]);
  xz_rect(c,X0+x,322-h*2/3,1,h*4/3,p->stem[1]);
  xz_rect(c,X0+x,322-h/3,1,h*2/3,p->stem[2]);
 }
}
