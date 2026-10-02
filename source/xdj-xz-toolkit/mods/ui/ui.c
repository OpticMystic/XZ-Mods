#include "ui_skin.h"
#include "stem_pads.h"
#include "wave_viewport.h"
#include <stdio.h>
#include <string.h>

static const enum xz_ui_page menu_pages[4]={XZ_UI_CONTROLS,XZ_UI_THEMES,XZ_UI_SETTINGS,XZ_UI_EXTRAS};
static const char *menu_names[4]={"CONTROLS","APPEARANCE","ADVANCED","EXTRAS"};
static const char *stem_pages[4]={"HOT CUE","BEAT LOOP","SLIP LOOP","BEAT JUMP"};
static const char *stems[3]={"DRUMS","HARMONICS","VOCALS"};
static const char *pads[8]={"A","B","C","D","E","F","G","H"};
const char *const xz_ui_lengths[6]={"1/16","1/8","1/4","1/2","1","2"};
static float clampf(float v,float lo,float hi){return v<lo?lo:v>hi?hi:v;}
static const struct xz_ui_skin *skin_for(int theme){
 return theme==XZ_THEME_LCARS?&xz_skin_lcars:theme==XZ_THEME_MATRIX?&xz_skin_matrix:theme==XZ_THEME_ANALOG?&xz_skin_analog:&xz_skin_classic;
}
void xz_ui_render_native_buttons(uint16_t *pixels,size_t stride,int theme,int stems,int vj_visible,int vj_active){
 if(!pixels||stride<800)return;
 skin_for(theme)->render_buttons((struct xz_canvas){pixels,stride,800,480,theme},stems,vj_visible,vj_active);
}
const char *xz_ui_theme_name(int theme){return xz_theme_name(theme);}
uint32_t xz_ui_stem_color(int theme,int index){
 return xz_theme_palette(theme)->stem[index>=0&&index<3?index:0];
}
void xz_ui_init(struct xz_ui *u){memset(u,0,sizeof(*u));u->capture=-1;u->page=XZ_UI_CONTROLS;}
static void add(struct xz_ui_widget *w,size_t *n,enum xz_ui_action_kind kind,int index,uint32_t cap,const char *label){
 w[*n]=(struct xz_ui_widget){0,0,0,0,kind,index,cap,label,-1};(*n)++;
}
static size_t page_controls(const struct xz_ui *u,const struct xz_ui_model *m,struct xz_ui_widget w[XZ_UI_WIDGETS]){
 size_t n=0;int i;
 for(i=0;i<4;i++)add(w,&n,XZ_UI_PANEL,menu_pages[i],0,menu_names[i]);
 add(w,&n,XZ_UI_CLOSE,0,0,"CLOSE");
 if(u->page==XZ_UI_CONTROLS||u->page==XZ_UI_BEAT_JUMP){
  add(w,&n,XZ_UI_CONTROL_SECTION,0,0,"STEM SHORTCUTS");
  add(w,&n,XZ_UI_CONTROL_SECTION,1,0,"BEAT JUMP");
 }
 if(u->page==XZ_UI_STEMS||u->page==XZ_UI_XPAD||u->page==XZ_UI_SETTINGS)
  for(i=0;i<2;i++)add(w,&n,XZ_UI_DECK,i,0,"USB DECK");
 if(u->page==XZ_UI_STEMS){
  for(i=0;i<8;i++){
   int assigned=(m->deck[u->deck].groove_assigned&(1u<<i))!=0;
   add(w,&n,assigned?XZ_UI_GROOVE_PAD:XZ_UI_HOTCUE_PAD,i,assigned?(XZ_UI_GROOVE|XZ_UI_STEM):XZ_UI_HOTCUE,pads[i]);
  }
 }else if(u->page==XZ_UI_XPAD){
  add(w,&n,XZ_UI_STRIP,0,XZ_UI_SAMPLE,"");
  add(w,&n,XZ_UI_HOLD,0,XZ_UI_SAMPLE,"HOLD");
  add(w,&n,XZ_UI_OVERDUB,0,XZ_UI_SAMPLE,"OVERDUB");
  add(w,&n,XZ_UI_VOLUME,0,XZ_UI_SAMPLE,"");
  for(i=0;i<8;i++)add(w,&n,XZ_UI_SAMPLE_PAD,i,XZ_UI_SAMPLE,pads[i]);
 }else if(u->page==XZ_UI_SETTINGS){
  add(w,&n,XZ_UI_ENABLE,XZ_UI_GATE,XZ_UI_GATE,"GATE CUE");
  add(w,&n,XZ_UI_ENABLE,XZ_UI_SMART,XZ_UI_SMART,"SMART CUE");
  add(w,&n,XZ_UI_ENABLE,XZ_UI_PREVIEW,XZ_UI_PREVIEW,"HOT-CUE PREVIEW");
  add(w,&n,XZ_UI_ENABLE,XZ_UI_SAMPLE,XZ_UI_SAMPLE,"X-PAD ENGINE");
  add(w,&n,XZ_UI_PANEL,XZ_UI_STEMS,0,"GROOVE PADS");
  add(w,&n,XZ_UI_PANEL,XZ_UI_XPAD,0,"X-PAD CONTROLS");
  add(w,&n,XZ_UI_KEY_SHIFT,-1,XZ_UI_KEY,"KEY -");
  add(w,&n,XZ_UI_KEY_SHIFT,0,XZ_UI_KEY,"RESET");
  add(w,&n,XZ_UI_KEY_SHIFT,1,XZ_UI_KEY,"KEY +");
  add(w,&n,XZ_UI_KEY_SYNC,0,XZ_UI_KEYSYNC,"KEY SYNC");
  add(w,&n,XZ_UI_SHIFT_KEYSYNC,0,XZ_UI_KEYSYNC,"SHIFT + SYNC");
  add(w,&n,XZ_UI_SHIFT_PAGES,0,0,"SHIFT + PAD MODES");
  add(w,&n,XZ_UI_WAVE_MODE,0,0,"STOCK WAVE");
  add(w,&n,XZ_UI_WAVE_MODE,1,0,"3-BAND");
  add(w,&n,XZ_UI_WAVE_MODE,2,0,"STEMS");
 }else if(u->page==XZ_UI_EXTRAS){
  add(w,&n,XZ_UI_PANEL,XZ_UI_CONNECTION,0,"VJ.TOOLS");
  add(w,&n,XZ_UI_PANEL,XZ_UI_GAMES,0,"GAMES");
  add(w,&n,XZ_UI_PANEL,XZ_UI_UPDATES,0,"NETWORK UPDATES");
 }else if(u->page==XZ_UI_GAMES){
  add(w,&n,XZ_UI_GAME_SELECT,0,0,"DOOM / CHOOSE WAD");
  add(w,&n,XZ_UI_GAME_SELECT,1,0,"CHEX QUEST");
  add(w,&n,XZ_UI_PANEL,XZ_UI_DOOM_FILES,0,"USB WAD LOADER");
  add(w,&n,XZ_UI_PANEL,XZ_UI_MYHOUSE_SETUP,0,"MY HOUSE / SETUP");
  add(w,&n,XZ_UI_ARCADE_OPEN,0,0,"BEAT ARCADE");
  add(w,&n,XZ_UI_WAVE_RIDER_OPEN,0,0,"WAVE RIDER");
 }else if(u->page==XZ_UI_MYHOUSE_SETUP){
  add(w,&n,XZ_UI_MYHOUSE_CHECK,0,0,"CHECK ENGINE AND USB FILES");
  add(w,&n,XZ_UI_PANEL,XZ_UI_GAMES,0,"BACK TO GAMES");
 }else if(u->page==XZ_UI_DOOM){
  add(w,&n,XZ_UI_DOOM_START,0,0,m->doom.chex?(m->doom_ready?"START CHEX QUEST":"SET UP CHEX QUEST"):(m->doom_ready?"START DOOM":"SET UP DOOM"));
  add(w,&n,XZ_UI_PANEL,XZ_UI_DOOM_FILES,0,"USB WADS");
 }else if(u->page==XZ_UI_DOOM_SETUP){
  add(w,&n,XZ_UI_PANEL,XZ_UI_DOOM_FILES,0,"CHOOSE USB GAME FILES");
  add(w,&n,XZ_UI_PANEL,XZ_UI_DOOM,0,"BACK TO DOOM");
 }else if(u->page==XZ_UI_DOOM_FILES){
  for(i=0;i<m->doom.count;i++)add(w,&n,XZ_UI_WAD_SELECT,m->doom.offset+i,0,m->doom.labels[i]);
  add(w,&n,XZ_UI_WAD_PAGE,-1,0,"PREV");add(w,&n,XZ_UI_WAD_PAGE,1,0,"NEXT");
  add(w,&n,XZ_UI_WAD_SCAN,0,0,"RESCAN USB");add(w,&n,XZ_UI_WAD_CLEAR,0,0,"NO ADD-ON");
  add(w,&n,XZ_UI_PANEL,XZ_UI_DOOM_SETUP,0,"SETUP");
 }else if(u->page==XZ_UI_UPDATES){
  add(w,&n,XZ_UI_UPDATE_CHECK,0,0,m->update.ready&&m->update.phase!=XZ_UPDATE_ERROR?"CHECK FOR UPDATES":"SET UP NETWORK UPDATES");
  add(w,&n,XZ_UI_UPDATE_INSTALL,0,0,"DOWNLOAD UPDATE");
  add(w,&n,XZ_UI_UPDATE_ROLLBACK,0,0,"PREVIOUS VERSION");
  add(w,&n,XZ_UI_PANEL,XZ_UI_UPDATE_SETUP,0,"NETWORK SETUP");
 }else if(u->page==XZ_UI_UPDATE_SETUP){
  add(w,&n,XZ_UI_UPDATE_CHECK,0,0,"DETECT UPDATE SERVER");
  add(w,&n,XZ_UI_PANEL,XZ_UI_UPDATES,0,"BACK TO UPDATES");
 }else if(u->page==XZ_UI_CONTROLS){
  add(w,&n,XZ_UI_ENABLE,XZ_UI_STEM,XZ_UI_STEM,"STEM AUDIO");
  add(w,&n,XZ_UI_STEMS_OVERLAY,0,0,"SHOW STEM ROWS");
  for(i=0;i<8;i++)add(w,&n,XZ_UI_STEM_PAD_CONFIG,i,0,"");
  add(w,&n,XZ_UI_PAD_FEEDBACK,0,0,"PAD LIGHTS");
  for(i=0;i<4;i++)add(w,&n,XZ_UI_STEM_PAGE,i,0,stem_pages[i]);
 }else if(u->page==XZ_UI_BEAT_JUMP){
  add(w,&n,XZ_UI_JUMP_ENABLE,0,0,"CUSTOM BEAT JUMP");
  add(w,&n,XZ_UI_JUMP_SHIFT,0,0,"SHIFT + BEAT JUMP");
  add(w,&n,XZ_UI_JUMP_EDIT_PAGE,0,0,"EDIT PAGE 1");
  add(w,&n,XZ_UI_JUMP_EDIT_PAGE,1,0,"EDIT PAGE 2");
  for(i=0;i<8;i++)add(w,&n,XZ_UI_JUMP_PAIR,i,0,"");
  add(w,&n,XZ_UI_JUMP_STEP,1,0,"UP +");
  add(w,&n,XZ_UI_JUMP_STEP,-1,0,"DOWN -");
 }else if(u->page==XZ_UI_THEMES){
  for(i=0;i<XZ_THEME_COUNT;i++)add(w,&n,XZ_UI_SET_THEME,i,0,xz_theme_name(i));
 }else if(u->page==XZ_UI_CONNECTION){
  add(w,&n,XZ_UI_TAKEOVER_TOGGLE,0,0,"VJ.TOOLS VIEW");
  add(w,&n,XZ_UI_TAKEOVER_ASSIGN,0,0,"LINK");
  add(w,&n,XZ_UI_TAKEOVER_ASSIGN,1,0,"REKORDBOX");
  add(w,&n,XZ_UI_TAKEOVER_ASSIGN,2,0,"ONSCREEN");
  add(w,&n,XZ_UI_CONNECTION_ENABLE,0,XZ_UI_VJ_CONNECTION,"VJ.TOOLS CONNECTION");
  add(w,&n,XZ_UI_DISCOVERY,0,XZ_UI_VJ_DISCOVERY,"DISCOVERABLE");
 }
 return n;
}
size_t xz_ui_layout(const struct xz_ui *u,const struct xz_ui_model *m,struct xz_ui_widget w[XZ_UI_WIDGETS]){
 size_t n=page_controls(u,m,w);
 xz_ui_place(u,m,w,n);
 for(size_t i=0;i<n;i++){
  struct xz_ui_widget *a=&w[i];int k=a->index;
  if(a->kind==XZ_UI_PANEL&&k==XZ_UI_BEAT_JUMP){a->x=474;a->y=296;a->w=318;a->h=40;}
  if(a->kind==XZ_UI_JUMP_ENABLE||a->kind==XZ_UI_JUMP_SHIFT||a->kind==XZ_UI_JUMP_EDIT_PAGE||a->kind==XZ_UI_JUMP_PAIR||a->kind==XZ_UI_JUMP_STEP){
   a->x=160;a->w=300;a->h=44;
   if(a->kind==XZ_UI_JUMP_ENABLE)a->y=144;
   else if(a->kind==XZ_UI_JUMP_SHIFT){a->x=476;a->y=144;}
   else if(a->kind==XZ_UI_JUMP_EDIT_PAGE){a->x+=316*k;a->y=200;}
   else if(a->kind==XZ_UI_JUMP_PAIR){a->x=160+94*(k%4)+20*((k%4)/2);a->y=254+96*(k/4);a->w=84;a->h=84;}
   else{a->x=594;a->y=k>0?254:390;a->w=182;a->h=44;}
  }
 }
 return n;
}
static int ready(const struct xz_ui_model *m,int deck,const struct xz_ui_widget *w){
 const struct xz_ui_deck *d=&m->deck[deck];
 if(w->kind==XZ_UI_CONNECTION_ENABLE)return m->connection.ready&&m->connection.can_enable;
 if(w->kind==XZ_UI_DISCOVERY)return m->connection.ready&&m->connection.can_discover;
 uint32_t available=d->ready;
 if(w->kind==XZ_UI_ENABLE&&(w->index==XZ_UI_STEM||w->index==XZ_UI_GATE||w->index==XZ_UI_SMART))
  available=m->deck[0].ready|m->deck[1].ready;
 if(w->requires && (available&w->requires)!=w->requires)return 0;
 switch(w->kind){
 case XZ_UI_LEVEL:case XZ_UI_MUTE:case XZ_UI_BYPASS:return (m->enabled&XZ_UI_STEM)!=0;
 case XZ_UI_GROOVE_PAD:return (m->enabled&XZ_UI_STEM)&&(d->groove_loaded&(1u<<w->index));
 case XZ_UI_SAMPLE_PAD:return (m->enabled&XZ_UI_SAMPLE)&&(d->sample_loaded&(1u<<w->index));
 case XZ_UI_STRIP:case XZ_UI_HOLD:case XZ_UI_OVERDUB:case XZ_UI_VOLUME:return (m->enabled&XZ_UI_SAMPLE)!=0;
 default:return 1;}
}
static struct xz_ui_look look(const struct xz_ui *u,const struct xz_ui_model *m,const struct xz_ui_widget *a){
 int deck=a->deck>=0?a->deck:u->deck,k=a->index;const struct xz_ui_deck *d=&m->deck[deck];
 const struct xz_theme_palette *p=xz_theme_palette(m->theme);
 struct xz_ui_look l={ready(m,deck,a),0,p->accent};
 switch(a->kind){
 case XZ_UI_DECK:l.selected=k==u->deck;break;
 case XZ_UI_PANEL:l.selected=k==(int)u->page||(k==XZ_UI_CONTROLS&&u->page==XZ_UI_BEAT_JUMP)||
  (k==XZ_UI_SETTINGS&&(u->page==XZ_UI_CONNECTION||u->page==XZ_UI_STEMS||u->page==XZ_UI_XPAD));break;
 case XZ_UI_SET_THEME:l.selected=k==m->theme;break;
 case XZ_UI_JUMP_ENABLE:l.selected=m->jump.enabled;break;
 case XZ_UI_JUMP_SHIFT:l.selected=m->jump.shift_page;break;
 case XZ_UI_JUMP_EDIT_PAGE:l.selected=k==u->jump_edit_page;break;
 case XZ_UI_JUMP_PAIR:l.selected=k/2==u->jump_edit_pair;break;
 case XZ_UI_JUMP_SIZE:l.selected=k==m->jump.sizes[u->jump_edit_page*4+u->jump_edit_pair];break;
 case XZ_UI_STEM_PAGE:l.selected=k==m->stem_page;break;
 case XZ_UI_WAVE_MODE:l.selected=k==m->wave_mode;break;
 case XZ_UI_SHIFT_PAGES:l.selected=m->shift_pages;break;
 case XZ_UI_PAD_FEEDBACK:l.selected=m->pad_feedback;break;
 case XZ_UI_STEMS_OVERLAY:l.selected=m->stems_overlay;break;
 case XZ_UI_SHIFT_KEYSYNC:l.selected=m->shift_keysync;break;
 case XZ_UI_CONTROL_SECTION:l.selected=(k==1)==(u->page==XZ_UI_BEAT_JUMP);break;
 case XZ_UI_STEM_PAD_CONFIG:l.selected=k/4==m->stem_bank;break;
 case XZ_UI_STEM_BANK:l.selected=m->stem_bank==k;break;
 case XZ_UI_WAD_SELECT:l.selected=k==m->doom.base||k==m->doom.map;break;
 case XZ_UI_TAKEOVER_TOGGLE:l.color=0x52d794;l.selected=m->fb_takeover!=0;break;
 case XZ_UI_TAKEOVER_ASSIGN:l.selected=k==m->takeover_assign;break;
 case XZ_UI_BYPASS:l.selected=d->bypass;break;
 case XZ_UI_HOLD:l.selected=d->hold;break;
 case XZ_UI_OVERDUB:l.selected=d->overdub;break;
 case XZ_UI_ENABLE:l.selected=(m->enabled&(uint32_t)k)!=0;break;
 case XZ_UI_CONNECTION_ENABLE:l.selected=m->connection.ready&&m->connection.enabled;break;
 case XZ_UI_DISCOVERY:l.selected=m->connection.ready&&m->connection.discoverable;break;
 case XZ_UI_MUTE:l.color=p->stem[k];l.selected=(d->muted&(1u<<k))!=0;break;
 case XZ_UI_GROOVE_PAD:l.color=p->stem[d->groove_stem[k]<3?d->groove_stem[k]:0];l.selected=d->groove_active==k;break;
 case XZ_UI_SAMPLE_PAD:l.selected=d->sample_active==k;break;
 default:break;}
 return l;
}
const char *xz_ui_footer_text(const struct xz_ui *u,const struct xz_ui_model *m){
 const struct xz_ui_deck *d=&m->deck[u->deck];
 const char *saved=m->settings_status?m->settings_status:"INSERT USB TO SAVE SETTINGS";
 if(u->page==XZ_UI_THEMES&&m->theme_status)return m->theme_status;
 if(u->page==XZ_UI_CONTROLS)return saved;
 if(u->notice[0])return u->notice;
 if(u->page==XZ_UI_BEAT_JUMP)return saved;
 if(u->page==XZ_UI_SETTINGS)return saved;
 if(u->page==XZ_UI_CONNECTION)
  return m->connection.status?m->connection.status:m->connection.ready&&m->connection.connected?"VJ.Tools CONNECTED":"VJ.Tools CONNECTION AVAILABLE";
 if(u->page==XZ_UI_EXTRAS)return "OPTIONAL EXTRAS / DJ CONTROLS STAY AVAILABLE";
 if(u->page==XZ_UI_DOOM||u->page==XZ_UI_DOOM_SETUP)return m->doom_ready?"DOOM READY / GAME RUNS ENTIRELY ON THE XZ":"OPEN SETUP TO CHECK THE USB GAME FILES";
 if(u->page==XZ_UI_DOOM_FILES)return m->doom.status;
 if(u->page==XZ_UI_UPDATES||u->page==XZ_UI_UPDATE_SETUP)return m->update.status;
 return d->status?d->status:"LOAD A TRACK TO USE DECK CONTROLS";
}
static struct xz_ui_scene scene(const struct xz_ui *u,const struct xz_ui_model *m,struct xz_canvas c,
 const struct xz_ui_deck *d,const struct xz_ui_widget *w,struct xz_ui_look *looks,size_t n){
 for(size_t i=0;i<n;i++)looks[i]=look(u,m,&w[i]);
 return (struct xz_ui_scene){c,u,m,d,xz_theme_palette(m->theme),w,looks,n};
}
int xz_ui_render(const struct xz_ui *u,const struct xz_ui_model *m,uint16_t *pixels,size_t count,size_t stride){
 struct xz_ui_widget w[XZ_UI_WIDGETS];struct xz_ui_look looks[XZ_UI_WIDGETS];
 if(!u||!m||!pixels||u->deck<0||u->deck>3||stride<800||stride>count/480)return 0;
 size_t n=xz_ui_layout(u,m,w);
 struct xz_ui_scene s=scene(u,m,(struct xz_canvas){pixels,stride,800,480,m->theme},&m->deck[u->deck],w,looks,n);
 skin_for(m->theme)->render(&s);
 if(u->page>=XZ_UI_EXTRAS&&u->page!=XZ_UI_CONNECTION)xz_ui_page_details(&s);
 if(u->page==XZ_UI_CONTROLS)xz_ui_stem_setup(&s);
 if(u->page==XZ_UI_BEAT_JUMP){
  const struct xz_theme_palette *p=xz_theme_palette(m->theme);
  uint32_t text_ink=m->theme==XZ_THEME_ANALOG?0x3a3020:p->ink;
  xz_text(s.c,160,116,"BEAT JUMP / 4 BEATS = 1 BAR",2,60,text_ink);
  xz_text(s.c,160,132,"HOLD SHIFT + TAP BEAT JUMP FOR PAGE 2",1,80,text_ink);
  for(size_t i=0;i<n;i++)if(w[i].kind==XZ_UI_JUMP_PAIR){
   struct xz_ui_widget a=w[i];int pad=a.index,selected=pad/2==u->jump_edit_pair;
   uint32_t edge=selected?p->accent:xz_blend(p->bg,p->ink,100);
   uint32_t fill=xz_blend(p->bg,p->accent,selected?64:16);char label[24];
   int size=m->jump.sizes[u->jump_edit_page*4+pad/2];
   xz_rect(s.c,a.x,a.y,a.w,a.h,edge);xz_rect(s.c,a.x+3,a.y+3,a.w-6,a.h-6,fill);
   snprintf(label,sizeof(label),"%c   %s",'A'+pad,pad%2?">":"<");xz_text(s.c,a.x+9,a.y+9,label,1,16,p->ink);
   int beats=xz_jump_beats(size);snprintf(label,sizeof(label),"%d",beats);
   xz_text(s.c,a.x+8,a.y+28,label,2,6,p->ink);
   xz_text(s.c,a.x+45,a.y+33,beats==1?"BEAT":"BEATS",1,8,p->ink);
   xz_rect(s.c,a.x+4,a.y+56,a.w-8,24,xz_blend(p->bg,p->ink,36));
   if(beats>=4){
    snprintf(label,sizeof(label),"%d",beats/4);xz_text(s.c,a.x+8,a.y+60,label,2,6,p->ink);
    xz_text(s.c,a.x+45,a.y+65,beats==4?"BAR":"BARS",1,8,p->ink);
   }else xz_text(s.c,a.x+8,a.y+64,beats==1?"1/4 BAR":"1/2 BAR",1,14,p->ink);
  }
  char selected[40];snprintf(selected,sizeof(selected),"PAIR %c / %c",'A'+u->jump_edit_pair*2,'B'+u->jump_edit_pair*2);
  xz_text(s.c,604,309,selected,1,26,text_ink);
  int beats=xz_jump_beats(m->jump.sizes[u->jump_edit_page*4+u->jump_edit_pair]);
  snprintf(selected,sizeof(selected),"%d BEATS",beats);xz_text(s.c,604,334,selected,2,14,text_ink);
  if(beats>=4){snprintf(selected,sizeof(selected),"= %d %s",beats/4,beats==4?"BAR":"BARS");xz_text(s.c,604,356,selected,2,26,text_ink);}
  xz_text(s.c,604,376,m->jump_encoder?"ENCODER OR UP/DOWN":"USE UP / DOWN",1,28,text_ink);
 }
 return 1;
}
static struct xz_ui_action action(enum xz_ui_action_kind kind,enum xz_ui_phase phase,int deck,int index,float value,float second){
 return (struct xz_ui_action){kind,phase,deck,index,value,second};
}
size_t xz_ui_cancel(struct xz_ui *u,struct xz_ui_action out[XZ_UI_ACTIONS]){
 size_t n=0;if(u->held_kind!=XZ_UI_NONE)out[n++]=action(u->held_kind,XZ_UI_RELEASE,u->held_deck,u->held_index,0,0);
 u->held_kind=XZ_UI_NONE;u->capture=-1;u->down=0;u->dragged=0;return n;
}
static size_t touch_widgets(struct xz_ui *u,const struct xz_ui_model *m,int x,int y,int down,struct xz_ui_action out[XZ_UI_ACTIONS],const struct xz_ui_widget *w,size_t count){
 struct xz_ui_widget a;size_t n,i;const struct xz_ui_deck *d;int press,deck;
 if(!u||!m||!out||u->deck<0||u->deck>3)return 0;
 if(!down)return xz_ui_cancel(u,out);
 press=!u->down;u->down=1;n=0;
 if(press){u->capture=-1;for(i=0;i<count;i++)if(x>=w[i].x&&x<w[i].x+w[i].w&&y>=w[i].y&&y<w[i].y+w[i].h){u->capture=(int)i;break;}}
 if(u->capture<0||(size_t)u->capture>=count)return 0;a=w[u->capture];
 deck=a.deck>=0?a.deck:u->deck;d=&m->deck[deck];
 if(!ready(m,deck,&a)){
  if(!press)return 0;
  if(a.kind==XZ_UI_CONNECTION_ENABLE||a.kind==XZ_UI_DISCOVERY)snprintf(u->notice,sizeof(u->notice),"%s CONTROL IS NOT AVAILABLE",a.label);
  else snprintf(u->notice,sizeof(u->notice),"%s / NOT READY - CHECK FEATURE, MEDIA AND RUNTIME",a.label[0]?a.label:"CONTROL");
  out[n++]=action(XZ_UI_UNAVAILABLE,XZ_UI_PRESS,deck,(int)a.requires,0,0);return n;
 }
 if(press)u->notice[0]=0;
 if(a.kind==XZ_UI_LEVEL||a.kind==XZ_UI_VOLUME){out[n++]=action(a.kind,press?XZ_UI_PRESS:XZ_UI_MOVE,deck,a.index,clampf((float)(x-a.x-8)/(float)(a.w-16),0,1),0);return n;}
 if(a.kind==XZ_UI_STRIP){static const float beats[6]={.0625f,.125f,.25f,.5f,1,2};int column=(x-a.x)*6/a.w;float pitch=clampf(12-24*(float)(y-a.y)/(float)(a.h-1),-12,12);
  column=column<0?0:column>5?5:column;
  u->held_kind=a.kind;u->held_deck=deck;u->held_index=column;out[n++]=action(a.kind,press?XZ_UI_PRESS:XZ_UI_MOVE,deck,column,beats[column],pitch);return n;}
 if(!press)return 0;
 if(a.kind==XZ_UI_CONTROL_SECTION){u->page=a.index?XZ_UI_BEAT_JUMP:XZ_UI_CONTROLS;u->capture=-1;out[0]=action(a.kind,XZ_UI_PRESS,deck,a.index,0,0);return 1;}
 if(a.kind==XZ_UI_JUMP_EDIT_PAGE){u->jump_edit_page=a.index;u->capture=-1;out[0]=action(a.kind,XZ_UI_PRESS,deck,a.index,0,0);return 1;}
 if(a.kind==XZ_UI_JUMP_PAIR){u->jump_edit_pair=a.index/2;u->capture=-1;out[0]=action(a.kind,XZ_UI_PRESS,deck,a.index,0,0);return 1;}
 if(a.kind==XZ_UI_JUMP_SIZE){out[0]=action(a.kind,XZ_UI_PRESS,deck,u->jump_edit_page*4+u->jump_edit_pair,(float)a.index,0);return 1;}
 if(a.kind==XZ_UI_PANEL){enum xz_ui_page old=u->page;u->page=(enum xz_ui_page)a.index;u->capture=-1;out[n++]=action(a.kind,XZ_UI_PRESS,u->deck,(int)old,(float)u->page,0);}
 else if(a.kind==XZ_UI_DECK){int old=u->deck;u->deck=a.index;u->capture=-1;out[n++]=action(a.kind,XZ_UI_PRESS,old,a.index,0,0);}
 else if(a.kind==XZ_UI_MUTE||a.kind==XZ_UI_SAMPLE_PAD||a.kind==XZ_UI_HOTCUE_PAD){u->held_kind=a.kind;u->held_deck=deck;u->held_index=a.index;out[n++]=action(a.kind,XZ_UI_PRESS,deck,a.index,1,0);}
 else{float value=1;
  switch(a.kind){case XZ_UI_ENABLE:value=(m->enabled&(uint32_t)a.index)?0:1;break;case XZ_UI_BYPASS:value=d->bypass?0:1;break;
  case XZ_UI_HOLD:value=d->hold?0:1;break;case XZ_UI_OVERDUB:value=d->overdub?0:1;break;case XZ_UI_SERVER_AUTO:value=m->server_auto?0:1;break;
  case XZ_UI_CONNECTION_ENABLE:value=m->connection.enabled?0:1;break;case XZ_UI_DISCOVERY:value=m->connection.discoverable?0:1;break;
  case XZ_UI_JUMP_ENABLE:value=m->jump.enabled?0:1;break;
  case XZ_UI_JUMP_SHIFT:value=m->jump.shift_page?0:1;break;
  case XZ_UI_SHIFT_PAGES:value=m->shift_pages?0:1;break;
  case XZ_UI_PAD_FEEDBACK:value=m->pad_feedback?0:1;break;
  case XZ_UI_SHIFT_KEYSYNC:value=m->shift_keysync?0:1;break;
  case XZ_UI_TAKEOVER_TOGGLE:value=m->fb_takeover?0:1;break;
  case XZ_UI_TAKEOVER_ASSIGN:value=(float)a.index;break;
  case XZ_UI_STEM_PAGE:value=(float)a.index;break;
  case XZ_UI_WAVE_MODE:value=(float)a.index;break;
  case XZ_UI_KEY_SHIFT:value=(float)a.index;break;case XZ_UI_SET_THEME:value=(float)a.index;break;default:break;}
  out[n++]=action(a.kind,XZ_UI_PRESS,deck,a.index,value,0);
 }
 return n;
}
size_t xz_ui_touch(struct xz_ui *u,const struct xz_ui_model *m,int x,int y,int down,struct xz_ui_action out[XZ_UI_ACTIONS]){
 struct xz_ui_widget w[XZ_UI_WIDGETS];
 if(!u||!m||u->deck<0||u->deck>3)return 0;
 size_t count=xz_ui_layout(u,m,w);
 return touch_widgets(u,m,x,y,down,out,w,count);
}
size_t xz_ui_inline_layout(const struct xz_ui *u,int width,int height,struct xz_ui_widget w[XZ_UI_WIDGETS]){
 size_t n=0;
 if(width<400||width>800||height!=XZ_WAVE_INLINE_HEIGHT||u->deck<0||u->deck>1)return 0;
 for(int deck=0;deck<2;deck++)for(int slot=0;slot<4;slot++){
  int x=width*slot/4,end=width*(slot+1)/4;
  int stem=slot<3?xz_stem_for_pad(slot):0;
  w[n++]=(struct xz_ui_widget){x+1,deck*XZ_WAVE_CONTROL_HEIGHT+1,end-x-2,XZ_WAVE_CONTROL_HEIGHT-2,
   slot<3?XZ_UI_MUTE:XZ_UI_BYPASS,stem,XZ_UI_STEM,slot<3?stems[stem]:"BYPASS",deck};
 }
 return n;
}
size_t xz_ui_inline_touch(struct xz_ui *u,const struct xz_ui_model *m,int width,int height,int x,int y,int down,struct xz_ui_action out[XZ_UI_ACTIONS]){
 struct xz_ui_widget w[XZ_UI_WIDGETS];
 if(!u||!m)return 0;
 size_t count=xz_ui_inline_layout(u,width,height,w);
 if(down&&!u->down)for(size_t i=0;i<count;i++){
  struct xz_ui_widget a=w[i];
  if(a.kind==XZ_UI_MUTE&&x>=a.x&&x<a.x+a.w&&y>=a.y&&y<a.y+a.h&&ready(m,a.deck,&a)){
   u->down=1;u->capture=(int)i;u->held_kind=XZ_UI_MUTE;u->held_deck=a.deck;u->held_index=a.index;
   u->touch_start_x=x;u->touch_start_level=(m->deck[a.deck].muted&(1u<<a.index))?0:m->deck[a.deck].levels[a.index];u->dragged=0;return 0;
  }
 }
 if(u->held_kind==XZ_UI_MUTE&&u->capture>=0&&(size_t)u->capture<count){
  struct xz_ui_widget a=w[u->capture];int dx=x-u->touch_start_x;
  if(down){
   if(dx>8||dx< -8)u->dragged=1;
   if(!u->dragged)return 0;
   out[0]=action(XZ_UI_LEVEL,XZ_UI_MOVE,u->held_deck,u->held_index,clampf(u->touch_start_level+(float)dx/(a.w-12),0,1),0);return 1;
  }
  size_t n=0;if(!u->dragged)out[n++]=action(XZ_UI_MUTE,XZ_UI_PRESS,u->held_deck,u->held_index,1,0);
  struct xz_ui_action ignored[XZ_UI_ACTIONS];xz_ui_cancel(u,ignored);return n;
 }
 return touch_widgets(u,m,x,y,down,out,w,count);
}
int xz_ui_inline_render(const struct xz_ui *u,const struct xz_ui_model *m,uint16_t *pixels,size_t count,size_t stride,int width,int height){
 struct xz_ui_widget w[XZ_UI_WIDGETS];struct xz_ui_look looks[XZ_UI_WIDGETS];
 if(!u||!m||!pixels||height<=0||width<=0||stride<(size_t)width||stride>count/(size_t)height)return 0;
 size_t n=xz_ui_inline_layout(u,width,height,w);if(!n)return 0;
 struct xz_ui_scene s=scene(u,m,(struct xz_canvas){pixels,stride,width,height,m->theme},&m->deck[0],w,looks,n);
 skin_for(m->theme)->render_inline(&s);
 return 1;
}
