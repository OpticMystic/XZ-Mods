/* SPDX-License-Identifier: MIT */
/* The DJ PROFILE page and the on-device editor: the shown profile,
   an on-screen keyboard for the name and tagline, and a logo picker over the
   PNG and JPEG files on the USB. Typing and step changes stay in the UI; the
   runtime reads the draft from xz_ui when SAVE is pressed. */
#include "ui_skin.h"
#include <stdio.h>
#include <string.h>

enum { X0=XZ_UI_CX, CW=XZ_UI_CW, KEY_W=60, KEY_PITCH=65, KEY_H=42, KEY_Y=100, ROW=48, IMAGE_ROWS=6 };
static const char *const letters[4]={"1234567890","qwertyuiop","asdfghjkl'","zxcvbnm,.-"};
static const char *const symbols[4]={"1234567890","!@#$%&*()+","/:;\"?=_~[]","<>{}|\\^`.,"};

int xz_ui_profile_owns(enum xz_ui_action_kind k){
 return k==XZ_UI_PROFILE_VIEW||k==XZ_UI_PROFILE_EDIT_OPEN||k==XZ_UI_KEYBOARD||k==XZ_UI_EDIT_STEP||
  k==XZ_UI_LOGO_PICK||k==XZ_UI_LOGO_PAGE||k==XZ_UI_LOGO_RESCAN||k==XZ_UI_PROFILE_SAVE||k==XZ_UI_PROFILE_CHECK;
}
static int count(const struct xz_ui_model *m){return m->dj?(m->dj->count<XZ_UI_DJ_ENTRIES?m->dj->count:XZ_UI_DJ_ENTRIES):0;}
static const struct xz_ui_dj_entry *entry(const struct xz_ui_model *m,int k){return k>=0&&k<count(m)?&m->dj->entry[k]:NULL;}
int xz_ui_profile_shown(const struct xz_ui *u,const struct xz_ui_model *m){
 if(!u||!m||!m->dj)return -1;
 if(u->page==XZ_UI_PROFILE_EDIT)return entry(m,u->edit_entry)?u->edit_entry:-1;
 if(entry(m,u->profile_view))return u->profile_view;
 return entry(m,m->dj->current)?m->dj->current:count(m)?0:-1;
}
/* Characters, not bytes: the limits are in characters like the standard's. */
static int chars(const char *s){int n=0;for(;s&&*s;s++)n+=((unsigned char)*s&0xc0)!=0x80;return n;}
static int blank(const char *s){for(;s&&*s;s++)if(*s!=' ')return 0;return 1;}
static int usb_ready(const struct xz_ui_model *m,int usb){return m->dj&&usb>=0&&usb<2&&(m->dj->usb_present&(1u<<usb));}
static int lower(int c){return c>='A'&&c<='Z'?c+32:c;}
/* a, up to its first '/' or the end, equals b ignoring ASCII case; returns the length matched or 0. */
static size_t segment_is(const char *a,const char *b){
 size_t i=0;for(;a[i]&&a[i]!='/'&&b[i];i++)if(lower((unsigned char)a[i])!=lower((unsigned char)b[i]))return 0;
 return (!a[i]||a[i]=='/')&&!b[i]?i:0;
}
/* The image in the DJ folder named like the typed name (DJPROFILE/<name>/ or DJPROFILES/<name>/):
   logo.* when there is one, else the first listed. -1 when that folder has no image or the list is not read. */
static int name_logo(const struct xz_ui *u,const struct xz_ui_model *m){
 const struct xz_ui_dj *dj=m->dj;int best=-1,best_logo=0;
 if(!dj||dj->images_usb!=u->edit_usb||dj->images_reading||blank(u->edit_name))return -1;
 for(int i=0;i<dj->image_count;i++){
  const char *p=dj->images[i];size_t n=segment_is(p,"djprofile");if(!n)n=segment_is(p,"djprofiles");
  if(!n||p[n]!='/')continue;
  size_t k=segment_is(p+n+1,u->edit_name);if(!k)continue;
  const char *file=p+n+1+k+1;if(p[n+1+k]!='/'||strchr(file,'/'))continue;
  int logo=segment_is(file,"logo.png")||segment_is(file,"logo.jpg")||segment_is(file,"logo.jpeg");
  if(best<0||(logo&&!best_logo)){best=i;best_logo=logo;}
 }
 return best;
}
/* A new profile without a logo in its DJ folder gets the copy instructions above the list. */
static int folder_hint(const struct xz_ui *u,const struct xz_ui_model *m){
 const struct xz_ui_dj *dj=m->dj;
 return dj&&u->edit_entry<0&&dj->images_usb==u->edit_usb&&!dj->images_reading&&dj->image_count>0&&name_logo(u,m)<0;
}
static int image_rows(const struct xz_ui *u,const struct xz_ui_model *m){return folder_hint(u,m)?4:IMAGE_ROWS;}
static int image_top(const struct xz_ui *u,const struct xz_ui_model *m){return folder_hint(u,m)?124:52;}
static char *field(struct xz_ui *u,size_t *cap,int *limit){
 if(u->edit_step==XZ_UI_EDIT_TAGLINE){*cap=sizeof(u->edit_tagline);*limit=XZ_UI_DJ_TAGLINE_TYPED;return u->edit_tagline;}
 *cap=sizeof(u->edit_name);*limit=XZ_UI_DJ_NAME_TYPED;return u->edit_name;
}
static const char *const *layout(const struct xz_ui *u){return u->kb_symbols?symbols:letters;}
static int key_byte(const struct xz_ui *u,char c){return !u->kb_symbols&&u->kb_shift&&c>='a'&&c<='z'?c-32:c;}
/* Row and column of a typed byte in the current layout, or -1. */
static int key_slot(const struct xz_ui *u,int byte);
/* The same keyboard for REPORT AN ISSUE (ui_support.c). */
const char *xz_ui_kb_row(const struct xz_ui *u,int row){return row>=0&&row<4?layout(u)[row]:"";}
int xz_ui_kb_byte(const struct xz_ui *u,char c){return key_byte(u,c);}
int xz_ui_kb_slot(const struct xz_ui *u,int byte){return key_slot(u,byte);}
static int key_slot(const struct xz_ui *u,int byte){
 char c=(char)(byte>='A'&&byte<='Z'&&!u->kb_symbols?byte+32:byte);
 for(int r=0;r<4;r++){const char *p=strchr(layout(u)[r],c);if(p&&c)return r*10+(int)(p-layout(u)[r]);}
 return -1;
}
static int next_step(const struct xz_ui *u){
 if(u->edit_origin==XZ_UI_EDIT_REVIEW||u->edit_step==XZ_UI_EDIT_TAGLINE)return XZ_UI_EDIT_REVIEW;
 return u->edit_step+1;
}
static int previous_step(const struct xz_ui *u){
 if(u->edit_step==u->edit_origin)return -1;
 return u->edit_origin==XZ_UI_EDIT_REVIEW?XZ_UI_EDIT_REVIEW:u->edit_step-1;
}
static void add(struct xz_ui_widget *w,size_t *n,enum xz_ui_action_kind kind,int index,const char *label){
 if(*n<XZ_UI_WIDGETS)w[(*n)++]=(struct xz_ui_widget){0,0,0,0,kind,index,0,label,-1};
}
static const char *next_label(const struct xz_ui *u){
 int to=next_step(u);
 if(to==XZ_UI_EDIT_REVIEW&&u->edit_step==XZ_UI_EDIT_TAGLINE&&blank(u->edit_tagline)&&u->edit_origin!=XZ_UI_EDIT_REVIEW)return "SKIP TAGLINE";
 if(u->edit_origin==XZ_UI_EDIT_REVIEW)return "DONE";
 return to==XZ_UI_EDIT_LOGO?"NEXT: LOGO":to==XZ_UI_EDIT_TAGLINE?"NEXT: TAGLINE":"NEXT: REVIEW";
}
size_t xz_ui_profile_controls(const struct xz_ui *u,const struct xz_ui_model *m,struct xz_ui_widget *w,size_t n){
 if(!m->dj)return n;
 if(u->page==XZ_UI_PROFILE_SETUP){
  int shown=xz_ui_profile_shown(u,m),usb=usb_ready(m,0)||usb_ready(m,1);
  if(shown<0){
   add(w,&n,XZ_UI_PROFILE_EDIT_OPEN,-1,"CREATE ON THIS XZ");
   add(w,&n,XZ_UI_PROFILE_CHECK,0,"CHECK USB FOR PROFILES");
   (void)usb;return n;
  }
  if(count(m)>1){add(w,&n,XZ_UI_PROFILE_VIEW,-1,"< PREV");add(w,&n,XZ_UI_PROFILE_VIEW,1,"NEXT >");}
  add(w,&n,XZ_UI_PROFILE_EDIT_OPEN,shown,"EDIT");
  if(!entry(m,shown)->stick_active)add(w,&n,XZ_UI_PROFILE_PICK,shown,"MAKE ACTIVE");
  add(w,&n,XZ_UI_PROFILE_EDIT_OPEN,-1,"NEW PROFILE");
  add(w,&n,XZ_UI_PROFILE_CHECK,0,"CHECK USB");
  return n;
 }
 if(u->page!=XZ_UI_PROFILE_EDIT)return n;
 switch(u->edit_step){
 case XZ_UI_EDIT_NAME:case XZ_UI_EDIT_TAGLINE:
  for(int r=0;r<4;r++)for(int k=0;k<10;k++)add(w,&n,XZ_UI_KEYBOARD,key_byte(u,layout(u)[r][k]),"");
  add(w,&n,XZ_UI_KEYBOARD,XZ_UI_KB_SHIFT,u->kb_shift?"SHIFT ON":"SHIFT");
  add(w,&n,XZ_UI_KEYBOARD,XZ_UI_KB_SYMBOLS,u->kb_symbols?"ABC":"#+=");
  add(w,&n,XZ_UI_KEYBOARD,' ',"SPACE");
  add(w,&n,XZ_UI_KEYBOARD,XZ_UI_KB_DELETE,"DELETE");
  if(previous_step(u)>=0&&previous_step(u)!=next_step(u))add(w,&n,XZ_UI_EDIT_STEP,previous_step(u),"BACK");
  add(w,&n,XZ_UI_EDIT_STEP,next_step(u),next_label(u));
  break;
 case XZ_UI_EDIT_LOGO:{
  const struct xz_ui_dj *dj=m->dj;int images=dj->images_usb==u->edit_usb&&!dj->images_reading?dj->image_count:0;
  int rows=image_rows(u,m);
  for(int i=0;i<rows&&u->image_offset+i<images;i++)add(w,&n,XZ_UI_LOGO_PICK,u->image_offset+i,dj->images[u->image_offset+i]);
  if(images>rows){add(w,&n,XZ_UI_LOGO_PAGE,-1,"PREV");add(w,&n,XZ_UI_LOGO_PAGE,1,"NEXT");}
  add(w,&n,XZ_UI_LOGO_RESCAN,0,images?"RESCAN":"CHECK USB AGAIN");
  add(w,&n,XZ_UI_LOGO_PICK,XZ_UI_LOGO_NONE,"NO LOGO");
  if(u->edit_entry>=0)add(w,&n,XZ_UI_LOGO_PICK,XZ_UI_LOGO_KEEP,"KEEP CURRENT");
  if(previous_step(u)>=0&&previous_step(u)!=next_step(u))add(w,&n,XZ_UI_EDIT_STEP,previous_step(u),"BACK");
  add(w,&n,XZ_UI_EDIT_STEP,next_step(u),next_label(u));
  break;}
 default:
  add(w,&n,XZ_UI_EDIT_STEP,XZ_UI_EDIT_NAME,"CHANGE NAME");
  add(w,&n,XZ_UI_EDIT_STEP,XZ_UI_EDIT_LOGO,"CHANGE LOGO");
  add(w,&n,XZ_UI_EDIT_STEP,XZ_UI_EDIT_TAGLINE,"CHANGE TAGLINE");
  /* A new profile may go to either USB; an edit stays on its own. */
  if(u->edit_entry<0&&usb_ready(m,0)&&usb_ready(m,1)){add(w,&n,XZ_UI_PROFILE_SAVE,0,"SAVE TO USB 1");add(w,&n,XZ_UI_PROFILE_SAVE,1,"SAVE TO USB 2");}
  else add(w,&n,XZ_UI_PROFILE_SAVE,u->edit_usb,u->edit_usb?"SAVE TO USB 2":"SAVE TO USB 1");
  break;}
 return n;
}
static void put(struct xz_ui_widget *a,int x,int y,int w,int h){a->x=x;a->y=y;a->w=w;a->h=h;}
int xz_ui_profile_place(const struct xz_ui *u,const struct xz_ui_model *m,struct xz_ui_widget *a){
 int k=a->index;
 if(u->page==XZ_UI_PROFILE_SETUP&&!u->profile_open){
  switch(a->kind){
  case XZ_UI_PROFILE_VIEW:put(a,152+(k>0?88:0),240,80,40);return 1;
  case XZ_UI_PROFILE_EDIT_OPEN:
   if(xz_ui_profile_shown(u,m)<0)put(a,X0,292,CW,44);
   else if(k>=0)put(a,X0,352,150,44);else put(a,X0+316,352,160,44);
   return 1;
  case XZ_UI_PROFILE_PICK:put(a,X0+158,352,150,44);return 1;
  case XZ_UI_PROFILE_CHECK:if(xz_ui_profile_shown(u,m)<0)put(a,X0,344,CW,44);else put(a,X0+484,352,172,44);return 1;
  default:return 0;}
 }
 if(u->page!=XZ_UI_PROFILE_EDIT)return 0;
 switch(a->kind){
 case XZ_UI_KEYBOARD:{
  if(k==XZ_UI_KB_SHIFT){put(a,X0,KEY_Y+4*ROW,125,KEY_H);return 1;}
  if(k==XZ_UI_KB_SYMBOLS){put(a,X0+130,KEY_Y+4*ROW,125,KEY_H);return 1;}
  if(k==' '){put(a,X0+260,KEY_Y+4*ROW,255,KEY_H);return 1;}
  if(k==XZ_UI_KB_DELETE){put(a,X0+520,KEY_Y+4*ROW,130,KEY_H);return 1;}
  int slot=key_slot(u,k);if(slot<0)slot=0;
  put(a,X0+(slot%10)*KEY_PITCH,KEY_Y+(slot/10)*ROW,KEY_W,KEY_H);return 1;}
 case XZ_UI_EDIT_STEP:
  if(u->edit_step==XZ_UI_EDIT_REVIEW){put(a,X0+k*220,252,k==2?216:210,44);return 1;}
  if(u->edit_step==XZ_UI_EDIT_LOGO){if(k==next_step(u))put(a,552,352,240,44);else put(a,X0+272,344,128,40);return 1;}
  if(k==next_step(u))put(a,X0+210,KEY_Y+5*ROW+4,440,44);else put(a,X0,KEY_Y+5*ROW+4,200,44);
  return 1;
 case XZ_UI_LOGO_PICK:
  if(k==XZ_UI_LOGO_NONE){put(a,552,260,116,40);return 1;}
  if(k==XZ_UI_LOGO_KEEP){put(a,676,260,116,40);return 1;}
  put(a,X0,image_top(u,m)+(k-u->image_offset)*ROW,400,42);return 1;
 case XZ_UI_LOGO_PAGE:put(a,X0+(k>0?68:0),344,64,40);return 1;
 case XZ_UI_LOGO_RESCAN:{int images=m->dj&&m->dj->images_usb==u->edit_usb&&!m->dj->images_reading?m->dj->image_count:0;
  if(images)put(a,X0+136,344,128,40);else put(a,X0,200,400,44);return 1;}
 case XZ_UI_PROFILE_SAVE:
  if(u->edit_entry<0&&usb_ready(m,0)&&usb_ready(m,1))put(a,X0+k*332,316,324,52);else put(a,X0,316,CW,52);
  return 1;
 default:return 0;}
}
static int name_ok(const struct xz_ui *u){return !blank(u->edit_name)&&chars(u->edit_name)<=160;}
int xz_ui_profile_ready(const struct xz_ui *u,const struct xz_ui_model *m,const struct xz_ui_widget *a){
 const struct xz_ui_dj *dj=m->dj;
 if(!dj)return 0;
 switch(a->kind){
 case XZ_UI_PROFILE_EDIT_OPEN:{
  if(a->index<0)return usb_ready(m,0)||usb_ready(m,1);
  const struct xz_ui_dj_entry *e=entry(m,a->index);return e&&!e->damaged&&!e->locked&&dj->pending<0&&!dj->saving;}
 case XZ_UI_KEYBOARD:{
  if(a->index<0)return 1;
  size_t cap;int limit;char *text=field((struct xz_ui *)u,&cap,&limit);
  return chars(text)<limit&&strlen(text)+1<cap;}
 case XZ_UI_EDIT_STEP:
  /* Leaving the name step forward needs a name. */
  return u->edit_step!=XZ_UI_EDIT_NAME||a->index<u->edit_step||name_ok(u);
 case XZ_UI_LOGO_PICK:return a->index<0||(dj->images_usb==u->edit_usb&&!dj->images_reading&&a->index<dj->image_count);
 case XZ_UI_LOGO_RESCAN:return !dj->images_reading;
 case XZ_UI_PROFILE_SAVE:
  return name_ok(u)&&!dj->saving&&usb_ready(m,a->index)&&
   (u->edit_logo<0||(dj->preview==u->edit_logo&&dj->preview_state==XZ_UI_PREVIEW_READY));
 default:return 1;}
}
static struct xz_ui_action make(enum xz_ui_action_kind kind,int deck,int index,float value){
 return (struct xz_ui_action){.kind=kind,.phase=XZ_UI_PRESS,.deck=deck,.index=index,.value=value,.pc_target=-1};
}
static void go_step(struct xz_ui *u,int step){
 u->edit_step=step;u->focus_kind=XZ_UI_NONE;u->adjusting=0;u->notice[0]=0;
 if(step==XZ_UI_EDIT_NAME||step==XZ_UI_EDIT_TAGLINE)u->kb_symbols=0;
}
static void open_editor(struct xz_ui *u,const struct xz_ui_model *m,int k){
 const struct xz_ui_dj_entry *e=entry(m,k);
 u->edit_entry=e?k:-1;u->edit_logo=XZ_UI_LOGO_KEEP;u->edit_logo_chosen=0;u->image_offset=0;u->kb_symbols=0;
 snprintf(u->edit_name,sizeof(u->edit_name),"%s",e&&e->name?e->name:"");
 snprintf(u->edit_tagline,sizeof(u->edit_tagline),"%s",e&&e->tagline?e->tagline:"");
 snprintf(u->edit_id,sizeof(u->edit_id),"%s",e&&e->dj_id&&!e->folder?e->dj_id:"");
 /* A new profile goes to the USB the header follows, else the first one present. */
 int shown=m->dj->current>=0?m->dj->entry[m->dj->current].usb:-1;
 u->edit_usb=e?e->usb:usb_ready(m,shown)?shown:usb_ready(m,0)?0:1;
 u->kb_shift=!e;
 u->edit_origin=e?XZ_UI_EDIT_REVIEW:XZ_UI_EDIT_NAME;
 if(!e)u->edit_logo=XZ_UI_LOGO_NONE;
 xz_ui_navigate(u,XZ_UI_PROFILE_EDIT);
 go_step(u,u->edit_origin);
}
int xz_ui_profile_touch(struct xz_ui *u,const struct xz_ui_model *m,const struct xz_ui_widget *a,struct xz_ui_action out[XZ_UI_ACTIONS]){
 int k=a->index;
 switch(a->kind){
 case XZ_UI_PROFILE_VIEW:{
  int n=count(m),shown=xz_ui_profile_shown(u,m);if(n<1)return 0;
  u->profile_view=((shown<0?0:shown)+k+n)%n;u->capture=-1;
  out[0]=make(a->kind,u->deck,u->profile_view,0);return 1;}
 case XZ_UI_PROFILE_EDIT_OPEN:
  open_editor(u,m,k);u->capture=-1;
  out[0]=make(a->kind,u->edit_usb,u->edit_entry,0);return 1;
 case XZ_UI_KEYBOARD:{
  size_t cap;int limit;char *text=field(u,&cap,&limit);size_t len=strlen(text);
  if(k==XZ_UI_KB_SHIFT)u->kb_shift=!u->kb_shift;
  else if(k==XZ_UI_KB_SYMBOLS)u->kb_symbols=!u->kb_symbols;
  else if(k==XZ_UI_KB_DELETE){while(len&&((unsigned char)text[len-1]&0xc0)==0x80)len--;if(len)len--;text[len]=0;}
  else if(k>=32&&k<127&&len+1<cap&&chars(text)<limit){text[len]=(char)k;text[len+1]=0;}
  u->capture=-1;out[0]=make(a->kind,u->deck,k,0);return 1;}
 case XZ_UI_EDIT_STEP:
  go_step(u,k);u->capture=-1;
  /* Focus the step's main control so the encoder can go straight on. */
  if(k==XZ_UI_EDIT_REVIEW){u->focus_kind=XZ_UI_PROFILE_SAVE;u->focus_index=u->edit_usb;u->focus_deck=-1;}
  out[0]=make(a->kind,u->edit_usb,k,0);return 1;
 case XZ_UI_LOGO_PICK:
  u->edit_logo=k;u->edit_logo_chosen=1;u->capture=-1;
  out[0]=make(a->kind,u->edit_usb,k,0);return 1;
 case XZ_UI_LOGO_PAGE:{
  int images=m->dj?m->dj->image_count:0,rows=image_rows(u,m),first=u->image_offset+k*rows;
  if(first>images-1)first=(images-1)/rows*rows;if(first<0)first=0;
  u->image_offset=first;u->capture=-1;return 0;}
 case XZ_UI_LOGO_RESCAN:u->image_offset=0;u->capture=-1;out[0]=make(a->kind,u->edit_usb,0,0);return 1;
 case XZ_UI_PROFILE_SAVE:
  u->edit_usb=k;u->capture=-1;
  /* The DJ PROFILE page shows the saved profile and its result. */
  /* Return to the page the editor came from, so BACK does not reopen the editor. */
  if(u->history_count&&u->history[u->history_count-1]==XZ_UI_PROFILE_SETUP){
   u->history_count--;u->page=XZ_UI_PROFILE_SETUP;u->focus_kind=XZ_UI_NONE;u->notice[0]=0;
  }else xz_ui_navigate(u,XZ_UI_PROFILE_SETUP);
  u->profile_view=-1;
  out[0]=make(a->kind,k,k,0);return 1;
 case XZ_UI_PROFILE_CHECK:u->capture=-1;out[0]=make(a->kind,u->deck,0,0);return 1;
 default:return -1;}
}
int xz_ui_profile_autopick(struct xz_ui *u,const struct xz_ui_model *m){
 const struct xz_ui_dj *dj=m?m->dj:NULL;
 if(!u||!dj||u->page!=XZ_UI_PROFILE_EDIT||u->edit_step!=XZ_UI_EDIT_LOGO||u->edit_entry>=0||u->edit_logo_chosen||
    u->edit_logo!=XZ_UI_LOGO_NONE)return -1;
 int k=name_logo(u,m);if(k<0)return -1;
 u->edit_logo=k;
 /* Show the picked row. */
 int rows=image_rows(u,m);u->image_offset=k/rows*rows;
 return k;
}
int xz_ui_profile_back(struct xz_ui *u){
 if(u->page!=XZ_UI_PROFILE_EDIT)return 0;
 int to=previous_step(u);if(to<0)return 0;
 go_step(u,to);return 1;
}

/* Drawing. Every theme gets the same controls; colours follow the theme. */
struct look { uint32_t bg, ink, dim, accent, alarm, panel; };
static struct look colors(const struct xz_ui_scene *s){
 struct look l;xz_ui_chrome_colors(s->m,s->p,&l.bg,&l.ink,&l.accent);
 l.dim=xz_blend(l.bg,l.ink,160);l.alarm=s->p->alarm;l.panel=xz_blend(l.bg,l.ink,26);return l;
}
static void label(struct xz_canvas c,int x,int y,int w,const char *text,int scale,uint32_t color){
 if(scale>1&&xz_text_width(c,text,scale,200)>w)scale=1;
 xz_ui_chrome_fit(c,x,y,w,text,scale,color);
}
static void button(struct xz_canvas c,const struct xz_ui_widget *a,const struct look *l,int primary,int selected,int ready,const char *text){
 uint32_t fill=!ready?xz_blend(l->bg,l->ink,12):primary?l->accent:selected?xz_blend(l->bg,l->accent,70):l->panel;
 xz_round_rect(c,a->x,a->y,a->w,a->h,8,15,fill);
 if(selected&&!primary)xz_border(c,a->x+2,a->y+2,a->w-4,a->h-4,l->accent);
 int scale=a->h>=40&&xz_text_width(c,text,2,60)<=a->w-16?2:1,tw=xz_text_width(c,text,scale,60);
 xz_text(c,a->x+(a->w-tw)/2,a->y+(a->h-7*scale)/2,text,scale,60,!ready?l->dim:primary?l->bg:l->ink);
}
static void tile(const struct xz_ui_scene *s,int x,int y,int side,int k,const struct look *l){
 const struct xz_ui_dj *dj=s->m->dj;const struct xz_ui_dj_entry *e=entry(s->m,k);
 if(e&&dj->detail==k&&dj->detail_tile)xz_ui_chrome_logo(s->c,x,y,side,0,dj->detail_tile,XZ_UI_DJ_TILE,e,l->bg,l->ink,l->accent);
 else xz_ui_chrome_logo(s->c,x,y,side,0,e?e->thumb:NULL,XZ_UI_DJ_THUMB,e,l->bg,l->ink,l->accent);
}
static void home(const struct xz_ui_scene *s,const struct look *l){
 struct xz_canvas c=s->c;const struct xz_ui *u=s->u;const struct xz_ui_model *m=s->m;const struct xz_ui_dj *dj=m->dj;
 int k=xz_ui_profile_shown(u,m);char line[160];
 if(k<0){
  int present=dj->usb_present!=0;
  label(c,X0,52,CW,"DJ PROFILE SETUP",2,l->ink);
  static const char *steps[3]={"1 INSERT YOUR USB","2 CREATE YOUR PROFILE","3 CHOOSE IT"};
  for(int i=0;i<3;i++){
   int x=X0+i*221,current=i==present,done=i<present;uint32_t edge=current?l->accent:done?xz_blend(l->bg,l->accent,150):xz_blend(l->bg,l->ink,70);
   xz_round_rect(c,x,84,213,28,8,15,edge);xz_round_rect(c,x+1,85,211,26,7,15,xz_blend(l->bg,current?l->accent:l->ink,current?60:done?30:12));
   label(c,x+10,94,170,steps[i],1,current||done?l->ink:l->dim);
   if(done)label(c,x+213-10-xz_text_width(c,"DONE",1,8),94,40,"DONE",1,l->ink);
  }
  if(dj->reading){label(c,X0,136,CW,"READING USB...",2,l->ink);return;}
  if(!present){
   label(c,X0,136,CW,"INSERT THE USB WITH YOUR MUSIC",2,l->ink);
   label(c,X0,170,CW,"YOUR PROFILE LIVES ON YOUR USB ONLY. NOTHING IS KEPT ON THIS XZ.",1,l->dim);
   return;
  }
  label(c,X0,136,CW,"NO DJ PROFILE ON THIS USB YET",2,l->ink);
  label(c,X0,170,CW,"ON A COMPUTER, MAKE A FOLDER NAMED DJPROFILE ON THIS USB.",1,l->ink);
  label(c,X0,188,CW,"INSIDE IT, MAKE A FOLDER WITH YOUR DJ NAME AND PUT YOUR LOGO IN IT:",1,l->ink);
  label(c,X0+20,206,CW-20,"DJPROFILE / YOUR DJ NAME / LOGO.PNG",1,l->accent);
  label(c,X0,224,CW,"PUT THE USB BACK AND CHECK USB. OR CREATE IT HERE: TYPE YOUR NAME, PICK A LOGO.",1,l->ink);
  label(c,X0,250,CW,"PROFILES STAY ON THE USB. NOTHING IS KEPT ON THIS XZ.",1,l->dim);
  return;
 }
 const struct xz_ui_dj_entry *e=entry(m,k);
 tile(s,152,56,160,k,l);
 label(c,328,60,224,e->name,2,l->ink);
 label(c,328,88,224,e->tagline&&e->tagline[0]?e->tagline:"NO TAGLINE",1,e->tagline&&e->tagline[0]?l->ink:l->dim);
 snprintf(line,sizeof(line),"USB %d / %s",e->usb+1,e->damaged?"FILE DAMAGED":e->stick_active?"ACTIVE ON THIS USB":"NOT ACTIVE");
 label(c,328,112,224,line,1,e->damaged?l->alarm:l->dim);
 if(k==dj->current)label(c,328,130,224,"SHOWN TO THE VJ NOW",1,l->accent);
 if(count(m)>1){snprintf(line,sizeof(line),"%d OF %d",k+1,count(m));label(c,336,254,100,line,1,l->dim);}
 if(e->locked)label(c,328,152,224,"OLDER CARD: EDIT IT IN VJ.TOOLS",1,l->dim);
 if(e->folder)label(c,328,152,224,"FROM ITS FOLDER IN DJPROFILE",1,l->dim);
}
static void text_box(const struct xz_ui_scene *s,const struct look *l){
 struct xz_canvas c=s->c;const struct xz_ui *u=s->u;
 int tagline=u->edit_step==XZ_UI_EDIT_TAGLINE;const char *text=tagline?u->edit_tagline:u->edit_name;
 int limit=tagline?XZ_UI_DJ_TAGLINE_TYPED:XZ_UI_DJ_NAME_TYPED;char shown[700],count_text[24];
 xz_round_rect(c,X0,50,CW,44,8,15,xz_blend(l->bg,l->accent,120));xz_round_rect(c,X0+2,52,CW-4,40,7,15,xz_blend(l->bg,l->ink,10));
 label(c,X0+12,55,200,tagline?"TAGLINE (OPTIONAL)":"DJ NAME",1,l->dim);
 snprintf(count_text,sizeof(count_text),"%d / %d",chars(text),limit);
 xz_text(c,X0+CW-12-xz_text_width(c,count_text,1,24),55,count_text,1,24,l->dim);
 if(!text[0]){label(c,X0+12,70,CW-24,tagline?"A FEW WORDS: GENRE, CITY, CREW":"TYPE YOUR DJ NAME",2,l->dim);return;}
 /* Show the end of a long entry, with a caret. */
 snprintf(shown,sizeof(shown),"%s_",text);const char *start=shown;
 while(*start&&xz_text_width(c,start,2,200)>CW-24){start++;while(((unsigned char)*start&0xc0)==0x80)start++;}
 xz_text(c,X0+12,70,start,2,200,l->ink);
}
/* DJPROFILE/<the typed name>, the folder a new profile's logo goes in. */
static void folder_path(struct xz_canvas c,int x,int y,int w,const struct xz_ui *u,uint32_t color){
 char path[700];snprintf(path,sizeof(path),"DJPROFILE/%s",blank(u->edit_name)?"YOUR DJ NAME":u->edit_name);
 label(c,x,y,w,path,1,color);
}
static void logo_step(const struct xz_ui_scene *s,const struct look *l){
 struct xz_canvas c=s->c;const struct xz_ui *u=s->u;const struct xz_ui_dj *dj=s->m->dj;char line[96];
 int images=dj->images_usb==u->edit_usb&&!dj->images_reading?dj->image_count:-1;
 if(images<0)label(c,X0,60,400,"READING USB...",2,l->ink);
 else if(!images){
  snprintf(line,sizeof(line),"NO LOGO ON USB %d YET",u->edit_usb+1);
  label(c,X0,60,400,line,2,l->ink);
  label(c,X0,92,400,"1  TAKE THE USB TO A COMPUTER",1,l->ink);
  label(c,X0,110,400,"2  PUT YOUR LOGO (PNG OR JPEG) IN THE FOLDER",1,l->ink);
  folder_path(c,X0+20,128,380,u,l->accent);
  label(c,X0,146,400,"3  PUT THE USB BACK, THEN CHECK USB AGAIN",1,l->ink);
  label(c,X0,172,400,"OR CHOOSE NO LOGO: YOUR INITIAL IS SHOWN.",1,l->dim);
  return;
 }
 if(folder_hint(u,s->m)){
  label(c,X0,52,400,"ON A COMPUTER, PUT YOUR LOGO IN THE FOLDER",1,l->ink);
  folder_path(c,X0+20,70,380,u,l->accent);
  label(c,X0,88,400,"THEN PRESS RESCAN. OR PICK AN IMAGE ON THE USB:",1,l->dim);
 }
 int rows=image_rows(u,s->m);
 if(images>rows){
  snprintf(line,sizeof(line),"%d-%d OF %d",u->image_offset+1,u->image_offset+rows<images?u->image_offset+rows:images,images);
  label(c,X0+272,358,128,line,1,l->dim);
 }
 /* The chosen logo, as the XZ and the VJ will see it. */
 int x=592,y=56;
 if(u->edit_logo>=0){
  if(dj->preview==u->edit_logo&&dj->preview_state==XZ_UI_PREVIEW_READY&&dj->preview_tile)
   xz_ui_chrome_logo(c,x,y,160,0,dj->preview_tile,XZ_UI_DJ_TILE,NULL,l->bg,l->ink,l->accent);
  else{xz_round_rect(c,x,y,160,160,32,15,xz_blend(l->bg,l->ink,20));
   label(c,x+20,y+76,130,dj->preview==u->edit_logo&&dj->preview_state==XZ_UI_PREVIEW_FAILED?"CANNOT USE":"LOADING...",1,
    dj->preview_state==XZ_UI_PREVIEW_FAILED?l->alarm:l->dim);}
  const char *why=dj->preview==u->edit_logo&&dj->preview_state==XZ_UI_PREVIEW_FAILED&&dj->preview_error?dj->preview_error:
   u->edit_logo<dj->image_count?dj->images[u->edit_logo]:"";
  label(c,552,224,240,why,1,dj->preview_state==XZ_UI_PREVIEW_FAILED?l->alarm:l->dim);
 }else{
  struct xz_ui_dj_entry mono={u->edit_name,"","",u->edit_usb,0,0,0,0,0,NULL,0};
  if(u->edit_logo==XZ_UI_LOGO_KEEP&&u->edit_entry>=0)tile(s,x,y,160,u->edit_entry,l);
  else xz_ui_chrome_logo(c,x,y,160,0,NULL,0,&mono,l->bg,l->ink,l->accent);
  label(c,552,224,240,u->edit_logo==XZ_UI_LOGO_KEEP?"CURRENT LOGO":"NO LOGO: YOUR INITIAL",1,l->dim);
 }
}
static void review(const struct xz_ui_scene *s,const struct look *l){
 struct xz_canvas c=s->c;const struct xz_ui *u=s->u;const struct xz_ui_dj *dj=s->m->dj;char line[96];
 int x=152,y=60;
 if(u->edit_logo>=0&&dj->preview==u->edit_logo&&dj->preview_state==XZ_UI_PREVIEW_READY&&dj->preview_tile)
  xz_ui_chrome_logo(c,x,y,160,0,dj->preview_tile,XZ_UI_DJ_TILE,NULL,l->bg,l->ink,l->accent);
 else if(u->edit_logo==XZ_UI_LOGO_KEEP&&u->edit_entry>=0)tile(s,x,y,160,u->edit_entry,l);
 else{struct xz_ui_dj_entry mono={u->edit_name,"","",u->edit_usb,0,0,0,0,0,NULL,0};xz_ui_chrome_logo(c,x,y,160,0,NULL,0,&mono,l->bg,l->ink,l->accent);}
 label(c,328,64,464,u->edit_name[0]?u->edit_name:"NO NAME YET",2,u->edit_name[0]?l->ink:l->alarm);
 label(c,328,96,464,u->edit_tagline[0]?u->edit_tagline:"NO TAGLINE",1,u->edit_tagline[0]?l->ink:l->dim);
 snprintf(line,sizeof(line),u->edit_entry<0?"NEW PROFILE / BECOMES ACTIVE ON ITS USB":"EDITING THE PROFILE ON USB %d",u->edit_usb+1);
 label(c,328,124,464,line,1,l->dim);
 label(c,328,144,464,"ONLY THE DJPROFILE FOLDER ON THE USB IS WRITTEN",1,l->dim);
 if(u->edit_logo>=0&&dj->preview==u->edit_logo&&dj->preview_state==XZ_UI_PREVIEW_FAILED)
  label(c,328,164,464,"THIS LOGO CANNOT BE USED: CHANGE LOGO",1,l->alarm);
}
void xz_ui_profile_render(const struct xz_ui_scene *s){
 const struct xz_ui *u=s->u;const struct xz_ui_model *m=s->m;
 if(!m->dj||(u->page!=XZ_UI_PROFILE_SETUP&&u->page!=XZ_UI_PROFILE_EDIT))return;
 struct look l=colors(s);struct xz_canvas c=s->c;
 xz_rect(c,X0,XZ_UI_CY,CW,XZ_UI_CB-XZ_UI_CY,l.bg);
 if(u->page==XZ_UI_PROFILE_SETUP)home(s,&l);
 else if(u->edit_step==XZ_UI_EDIT_LOGO)logo_step(s,&l);
 else if(u->edit_step==XZ_UI_EDIT_REVIEW)review(s,&l);
 else text_box(s,&l);
 for(size_t i=0;i<s->n;i++){
  const struct xz_ui_widget *a=&s->w[i];
  if(!xz_ui_profile_owns(a->kind)&&!(a->kind==XZ_UI_PROFILE_PICK&&!u->profile_open))continue;
  int ready=xz_ui_profile_ready(u,m,a),k=a->index;
  if(a->kind==XZ_UI_KEYBOARD){
   char glyph[2]={(char)k,0};const char *text=k>=32&&k<127&&k!=' '?glyph:a->label;
   int on=(k==XZ_UI_KB_SHIFT&&u->kb_shift)||(k==XZ_UI_KB_SYMBOLS&&u->kb_symbols);
   button(c,a,&l,0,on,ready,text);continue;
  }
  if(a->kind==XZ_UI_LOGO_PICK&&k>=0){
   int chosen=u->edit_logo==k;
   xz_round_rect(c,a->x,a->y,a->w,a->h,8,15,chosen?xz_blend(l.bg,l.accent,70):l.panel);
   if(chosen)xz_round_rect(c,a->x,a->y+8,4,a->h-16,2,15,l.accent);
   label(c,a->x+12,a->y+(a->h-7)/2,a->w-24,a->label,1,l.ink);continue;
  }
  int primary=(a->kind==XZ_UI_EDIT_STEP&&k==next_step(u)&&u->edit_step!=XZ_UI_EDIT_REVIEW)||a->kind==XZ_UI_PROFILE_SAVE||
   (a->kind==XZ_UI_PROFILE_EDIT_OPEN&&xz_ui_profile_shown(u,m)<0)||(a->kind==XZ_UI_LOGO_RESCAN&&m->dj->image_count==0);
  int selected=a->kind==XZ_UI_LOGO_PICK&&u->edit_logo==k;
  button(c,a,&l,primary,selected,ready,a->label);
 }
}
const char *xz_ui_profile_describe(const struct xz_ui *u,const struct xz_ui_model *m,const struct xz_ui_widget *a,const char **title){
 int k=a->index;
 switch(a->kind){
 case XZ_UI_PROFILE_VIEW:return k<0?"Shows the previous profile on your USBs.":"Shows the next profile on your USBs.";
 case XZ_UI_PROFILE_EDIT_OPEN:
  if(k<0)return u->page==XZ_UI_PROFILE_SETUP&&xz_ui_profile_shown(u,m)<0?
   "Type your DJ name, pick a logo image from the USB and save the profile to the USB.":"Makes another profile on the USB, for an alias or a B2B.";
  return "Change this profile's name, logo or tagline.";
 case XZ_UI_KEYBOARD:
  *title=k==XZ_UI_KB_SHIFT?"SHIFT":k==XZ_UI_KB_SYMBOLS?"SYMBOLS":k==XZ_UI_KB_DELETE?"DELETE":k==' '?"SPACE":"KEY";
  return k==XZ_UI_KB_SHIFT?"Capital letters on or off.":k==XZ_UI_KB_SYMBOLS?"Switches between letters and symbols.":
   k==XZ_UI_KB_DELETE?"Deletes the last character.":"Types this character.";
 case XZ_UI_EDIT_STEP:
  if(u->edit_step==XZ_UI_EDIT_REVIEW)return "Change this part, then come back here to save.";
  return k<u->edit_step||(u->edit_origin==XZ_UI_EDIT_REVIEW&&k==XZ_UI_EDIT_REVIEW&&k!=next_step(u))?"Goes back a step.":
   u->edit_step==XZ_UI_EDIT_NAME&&!name_ok(u)?"Type your DJ name first.":"Goes to the next step.";
 case XZ_UI_LOGO_PICK:
  return k==XZ_UI_LOGO_NONE?"No logo. Your initial is shown on the accent colour.":k==XZ_UI_LOGO_KEEP?"Keeps the logo the profile has now.":
   "Uses this image as your logo. Logos in DJPROFILE/<DJ name> folders are listed first.";
 case XZ_UI_LOGO_PAGE:return k<0?"Previous images.":"More images.";
 case XZ_UI_LOGO_RESCAN:return "Looks again. Put your logo in DJPROFILE/<your DJ name> on the USB first.";
 case XZ_UI_PROFILE_SAVE:return u->edit_entry<0?"Saves the new profile to this USB and makes it active there.":"Saves the changes to the profile on its USB.";
 case XZ_UI_PROFILE_CHECK:return "Reads both USB ports again for DJ profiles.";
 case XZ_UI_PROFILE_PICK:return "Makes this the active profile on its USB. Only the USB is written.";
 default:return NULL;}
}
const char *xz_ui_profile_footer(const struct xz_ui *u,const struct xz_ui_model *m){
 const struct xz_ui_dj *dj=m->dj;
 if(!dj)return NULL;
 if(dj->flash&&dj->status)return dj->status;
 if(dj->saving)return "SAVING YOUR PROFILE TO THE USB / KEEP THE USB IN";
 if(u->page==XZ_UI_PROFILE_EDIT){
  if(u->edit_step==XZ_UI_EDIT_LOGO&&dj->preview==u->edit_logo&&dj->preview_state==XZ_UI_PREVIEW_FAILED&&dj->preview_error)return dj->preview_error;
  if(u->notice[0])return u->notice;
  return u->edit_step==XZ_UI_EDIT_LOGO?"PNG OR JPEG / LOGOS ARE SHOWN SQUARE OVER BLACK":
   u->edit_step==XZ_UI_EDIT_REVIEW?"NOTHING IS SAVED UNTIL YOU PRESS SAVE":"TURN AND PUSH THE ENCODER, OR TAP THE KEYS";
 }
 if(u->notice[0])return u->notice;
 if(dj->status)return dj->status;
 if(dj->reading)return "READING USB PROFILES";
 return dj->count?"PROFILES STAY ON THE USB / NOTHING IS KEPT ON THE XZ":"NO DJ PROFILE ON THE USB";
}
const char *xz_ui_profile_blocked(const struct xz_ui *u,const struct xz_ui_model *m,const struct xz_ui_widget *a){
 const struct xz_ui_dj *dj=m->dj;const struct xz_ui_dj_entry *e=entry(m,a->index);
 if(!dj)return "DJ PROFILES ARE NOT AVAILABLE IN THIS RUNTIME";
 switch(a->kind){
 case XZ_UI_PROFILE_EDIT_OPEN:
  if(a->index<0)return "INSERT A USB FIRST";
  if(e&&e->damaged)return "THIS PROFILE FILE IS DAMAGED / SAVE IT AGAIN FROM VJ.TOOLS";
  if(e&&e->locked)return "OLDER PROFILE CARD / EDIT IT IN VJ.TOOLS OR MAKE A NEW PROFILE";
  return "WAIT FOR THE USB TO FINISH WRITING";
 case XZ_UI_KEYBOARD:return u->edit_step==XZ_UI_EDIT_TAGLINE?"THE TAGLINE IS FULL":"THE NAME IS FULL";
 case XZ_UI_EDIT_STEP:return "TYPE YOUR DJ NAME FIRST";
 case XZ_UI_LOGO_PICK:case XZ_UI_LOGO_RESCAN:return "STILL READING THE USB";
 case XZ_UI_PROFILE_SAVE:
  if(!name_ok(u))return "TYPE YOUR DJ NAME FIRST";
  if(dj->saving)return "STILL SAVING";
  if(!usb_ready(m,a->index))return "THAT USB IS NOT IN / PUT IT BACK";
  return dj->preview_state==XZ_UI_PREVIEW_FAILED?"THIS LOGO CANNOT BE USED / CHANGE LOGO":"THE LOGO IS STILL LOADING";
 default:return "NOT READY";}
}
