/* SPDX-License-Identifier: MIT */
#include "ui_skin.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Same grid as the WAD loader and PC STEMS setup: content from x=136, info dock at 412. */
enum {X0=XZ_UI_CX,ROW_Y=96,ROW_PITCH=44,ROW_H=40,ROW_W=528,PROFILE_W=424,BOTTOM_Y=364,BOTTOM_H=40,
 STEP_Y=96,STEP_H=40,ACTION_Y=336,ACTION_H=48};
static const char *step_names[XZ_SDK_UI_STEPS]={"1 DETECT","2 FOLDERS","3 INSTALL","4 LIVE TEST"};
static const char *step_titles[XZ_SDK_UI_STEPS]={"FIND THE APP MANAGER AND USB APPS","CHOOSE HOW APPS START",
 "COPY APPS TO A USB","RUN ONE APP TO PROVE IT WORKS"};
struct choice { enum xz_ui_action_kind kind; int index; const char *label; };

int xz_sdk_ui_page(int page){return page==XZ_UI_APPS||page==XZ_UI_APPS_SETUP;}
int xz_sdk_ui_kind(int kind){return kind>=XZ_UI_SDK_RUN&&kind<=XZ_UI_SDK_DONE;}
int xz_sdk_ui_game_launch(int kind){
 return kind==XZ_UI_DOOM_START||kind==XZ_UI_MYHOUSE_START||kind==XZ_UI_ARCADE_OPEN||kind==XZ_UI_WAVE_RIDER_OPEN||
  kind==XZ_UI_RHYTHM_LAB_OPEN||kind==XZ_UI_GLYPH_RUN_OPEN;
}
static unsigned count(const struct xz_sdk_ui_model *s){return s->snapshot.count<XZ_SDK_PACKAGES?s->snapshot.count:XZ_SDK_PACKAGES;}
static int running(const struct xz_sdk_ui_model *s){return s->host==XZ_SDK_UI_RUNNING;}
static int first_row(const struct xz_ui *u,const struct xz_sdk_ui_model *s){
 int n=(int)count(s),offset=u->sdk_offset<0?0:u->sdk_offset;
 if(offset>=n)offset=n?n-1:0;
 return offset/XZ_SDK_UI_ROWS*XZ_SDK_UI_ROWS;
}
static int step_of(const struct xz_ui *u){return u->sdk_step>=0&&u->sdk_step<XZ_SDK_UI_STEPS?u->sdk_step:XZ_SDK_UI_DETECT;}
/* Profiles offer APPLY (this session) and SAVE (also queue an XZ Mods settings save). */
static int two_buttons(const struct xz_sdk_item *item){return item->kind==XZ_SDK_PROFILE&&item->state!=XZ_SDK_BLOCKED;}
/* The live-test package: the SDK example, else the first application, else -1. */
static int test_target(const struct xz_sdk_ui_model *s){
 int first=-1;
 for(unsigned i=0;i<count(s);i++){
  const struct xz_sdk_item *item=&s->snapshot.items[i];
  if(item->kind!=XZ_SDK_APPLICATION)continue;
  if(!strcmp(item->id,XZ_SDK_UI_TEST_ID))return (int)i;
  if(first<0)first=(int)i;
 }
 return first;
}
static struct choice row_action(const struct xz_sdk_item *item,int index){
 if(item->state==XZ_SDK_BLOCKED)return (struct choice){XZ_UI_SDK_FIX,index,"SET UP"};
 if(item->kind==XZ_SDK_PROFILE)return (struct choice){XZ_UI_SDK_RUN,index,"APPLY"};
 if(item->state==XZ_SDK_RUNNING)return (struct choice){XZ_UI_SDK_STOP,index,"STOP"};
 return (struct choice){XZ_UI_SDK_RUN,index,item->state==XZ_SDK_FAILED?"RETRY":"RUN"};
}
static struct choice none(void){return (struct choice){XZ_UI_NONE,0,""};}
static struct choice wizard_action(const struct xz_ui *u,const struct xz_ui_model *m,int secondary){
 const struct xz_sdk_ui_model *s=&m->sdk;int t;
 switch(step_of(u)){
 case XZ_SDK_UI_DETECT:
  if(!running(s))return secondary?none():(struct choice){XZ_UI_SDK_SCAN,0,"CHECK AGAIN"};
  return secondary?(struct choice){XZ_UI_SDK_SCAN,0,"SEARCH USB AGAIN"}:(struct choice){XZ_UI_SDK_GOTO,XZ_SDK_UI_FOLDERS,"NEXT: CHOOSE FOLDERS"};
 case XZ_SDK_UI_FOLDERS:
  return secondary?(struct choice){XZ_UI_SDK_AUTOLOAD,0,s->snapshot.autoload_enabled?"AUTOLOAD: ON":"AUTOLOAD: OFF"}:
   (struct choice){XZ_UI_SDK_GOTO,XZ_SDK_UI_INSTALL,"NEXT: INSTALL APPS"};
 case XZ_SDK_UI_INSTALL:
  if(!count(s))return secondary?none():(struct choice){XZ_UI_SDK_SCAN,0,"DETECT INSTALLED APPS"};
  return secondary?(struct choice){XZ_UI_SDK_SCAN,0,"DETECT AGAIN"}:(struct choice){XZ_UI_SDK_GOTO,XZ_SDK_UI_VERIFY,"NEXT: LIVE TEST"};
 default:
  if(!running(s))return secondary?none():(struct choice){XZ_UI_SDK_GOTO,XZ_SDK_UI_DETECT,"BACK TO DETECT"};
  t=test_target(s);
  if(t<0)return secondary?(struct choice){XZ_UI_SDK_GOTO,XZ_SDK_UI_INSTALL,"BACK: INSTALL"}:(struct choice){XZ_UI_SDK_SCAN,0,"DETECT AGAIN"};
  switch(s->snapshot.items[t].state){
  case XZ_SDK_RUNNING:return secondary?(struct choice){XZ_UI_SDK_DONE,0,"IT WORKS"}:(struct choice){XZ_UI_SDK_STOP,t,"STOP TEST APP"};
  case XZ_SDK_BLOCKED:return secondary?none():(struct choice){XZ_UI_SDK_FIX,t,"HOW TO FIX"};
  case XZ_SDK_FAILED:return secondary?none():(struct choice){XZ_UI_SDK_RUN,t,"RUN AGAIN"};
  default:return secondary?none():(struct choice){XZ_UI_SDK_RUN,t,"RUN TEST APP"};
  }
 }
}
static void add(struct xz_ui_widget *w,size_t *n,struct choice c){
 if(*n<XZ_UI_WIDGETS&&c.kind!=XZ_UI_NONE)w[(*n)++]=(struct xz_ui_widget){0,0,0,0,c.kind,c.index,0,c.label,-1};
}
size_t xz_sdk_ui_controls(const struct xz_ui *u,const struct xz_ui_model *m,struct xz_ui_widget *w,size_t n){
 const struct xz_sdk_ui_model *s=&m->sdk;
 if(u->page==XZ_UI_EXTRAS){add(w,&n,(struct choice){XZ_UI_PANEL,XZ_UI_APPS,"APPLICATIONS"});return n;}
 if(u->page==XZ_UI_APPS){
  add(w,&n,(struct choice){XZ_UI_SDK_AUTOLOAD,0,running(s)&&s->snapshot.autoload_enabled?"AUTOLOAD: ON":"AUTOLOAD: OFF"});
  int first=first_row(u,s);
  for(int i=first;i<first+XZ_SDK_UI_ROWS&&i<(int)count(s);i++){
   const struct xz_sdk_item *item=&s->snapshot.items[i];
   add(w,&n,row_action(item,i));
   if(two_buttons(item))add(w,&n,(struct choice){XZ_UI_SDK_SAVE,i,"SAVE"});
  }
  add(w,&n,(struct choice){XZ_UI_SDK_PAGE,-1,"PREV"});add(w,&n,(struct choice){XZ_UI_SDK_PAGE,1,"NEXT"});
  add(w,&n,(struct choice){XZ_UI_SDK_SCAN,0,"RESCAN USB"});
  add(w,&n,(struct choice){XZ_UI_SDK_SETUP,XZ_SDK_UI_DETECT,"SET UP APPS"});
 }else if(u->page==XZ_UI_APPS_SETUP){
  for(int i=0;i<XZ_SDK_UI_STEPS;i++)add(w,&n,(struct choice){XZ_UI_SDK_STEP,i,step_names[i]});
  if(step_of(u)==XZ_SDK_UI_FOLDERS){
   add(w,&n,(struct choice){XZ_UI_SDK_CHOICE,0,"START WHEN I TAP RUN"});
   add(w,&n,(struct choice){XZ_UI_SDK_CHOICE,1,"START ON USB INSERT"});
  }
  add(w,&n,wizard_action(u,m,0));add(w,&n,wizard_action(u,m,1));
  add(w,&n,(struct choice){XZ_UI_PANEL,XZ_UI_APPS,"BACK TO APPS"});
 }
 return n;
}
static void put(struct xz_ui_widget *a,int x,int y,int w,int h){a->x=x;a->y=y;a->w=w;a->h=h;}
static int is(struct choice c,const struct xz_ui_widget *a){return c.kind==a->kind&&c.index==a->index;}
void xz_sdk_ui_place(const struct xz_ui *u,const struct xz_ui_model *m,struct xz_ui_widget *w,size_t n){
 const struct xz_sdk_ui_model *s=&m->sdk;int first=first_row(u,s);
 struct choice primary=wizard_action(u,m,0),secondary=wizard_action(u,m,1);
 for(size_t i=0;i<n;i++){
  struct xz_ui_widget *a=&w[i];int k=a->index;
  /* EXTRAS: GAMES and APPLICATIONS share the games row; both stay full height. */
  if(!xz_sdk_ui_page(u->page))continue;
  /* Wizard pages keep the way back to the app list top right, clear of the step actions. */
  if(a->kind==XZ_UI_PANEL&&k==XZ_UI_APPS){put(a,592,XZ_UI_CY,200,40);continue;}
  if(!xz_sdk_ui_kind(a->kind))continue;
  if(u->page==XZ_UI_APPS)switch(a->kind){
  case XZ_UI_SDK_AUTOLOAD:put(a,592,XZ_UI_CY,200,40);break;
  case XZ_UI_SDK_RUN:case XZ_UI_SDK_STOP:case XZ_UI_SDK_FIX:case XZ_UI_SDK_SAVE:{
   int y=ROW_Y+(k-first)*ROW_PITCH;
   if(k>=0&&k<(int)count(s)&&two_buttons(&s->snapshot.items[k]))put(a,a->kind==XZ_UI_SDK_SAVE?684:568,y,108,ROW_H);
   else put(a,672,y,120,ROW_H);
   break;}
  case XZ_UI_SDK_PAGE:put(a,k<0?X0:228,BOTTOM_Y,76,BOTTOM_H);break;
  case XZ_UI_SDK_SCAN:put(a,308,BOTTOM_Y,156,BOTTOM_H);break;
  default:put(a,472,BOTTOM_Y,320,BOTTOM_H);break;
  }else{
   if(a->kind==XZ_UI_SDK_STEP)put(a,X0+k*166,STEP_Y,158,STEP_H);
   else if(a->kind==XZ_UI_SDK_CHOICE)put(a,k?468:X0,166,324,56);
   else if(is(primary,a))put(a,X0,ACTION_Y,432,ACTION_H);
   else if(is(secondary,a))put(a,576,ACTION_Y,216,ACTION_H);
  }
 }
}
int xz_sdk_ui_selected(const struct xz_ui *u,const struct xz_ui_model *m,const struct xz_ui_widget *a){
 switch(a->kind){
 case XZ_UI_SDK_STEP:return a->index==step_of(u);
 case XZ_UI_SDK_CHOICE:return a->index==(u->sdk_choice?1:0);
 case XZ_UI_SDK_AUTOLOAD:return running(&m->sdk)&&m->sdk.snapshot.autoload_enabled!=0;
 case XZ_UI_SDK_STOP:return 1;
 default:return 0;
 }
}
int xz_sdk_ui_primary(const struct xz_ui *u,const struct xz_ui_model *m,int *kind,int *index){
 struct choice c=none();
 if(u->page==XZ_UI_APPS&&(!running(&m->sdk)||!count(&m->sdk)))c=(struct choice){XZ_UI_SDK_SETUP,XZ_SDK_UI_DETECT,""};
 else if(u->page==XZ_UI_APPS_SETUP)c=wizard_action(u,m,0);
 if(c.kind==XZ_UI_NONE)return 0;
 if(kind)*kind=(int)c.kind;
 if(index)*index=c.index;
 return 1;
}
const char *xz_sdk_ui_footer(const struct xz_ui *u,const struct xz_ui_model *m){
 const struct xz_sdk_ui_model *s=&m->sdk;
 if(u->page==XZ_UI_APPS_SETUP)return s->snapshot.autoload_enabled?"AUTOLOAD IS ON / CHOOSE MANUAL START TO TURN IT OFF":"AUTOLOAD IS OFF / APPS START WHEN YOU TAP RUN";
 if(!running(s))return "APP MANAGER NOT RUNNING / TAP SET UP APPS";
 if(s->snapshot.scanning)return "SEARCHING USB FOR APPS";
 return s->snapshot.status[0]?s->snapshot.status:"DJ CONTROLS KEEP WORKING WHILE APPS RUN";
}
size_t xz_sdk_ui_touch(struct xz_ui *u,const struct xz_ui_model *m,const struct xz_ui_widget *a,struct xz_ui_action *out){
 const struct xz_sdk_ui_model *s=&m->sdk;int k=a->index;float value=0;
 switch(a->kind){
 case XZ_UI_SDK_PAGE:{int next=first_row(u,s)+(k<0?-XZ_SDK_UI_ROWS:XZ_SDK_UI_ROWS);
  if(next>=0&&next<(int)count(s))u->sdk_offset=next;
  break;}
 case XZ_UI_SDK_SETUP:case XZ_UI_SDK_STEP:case XZ_UI_SDK_GOTO:
  xz_ui_navigate(u,XZ_UI_APPS_SETUP);u->sdk_step=k>=0&&k<XZ_SDK_UI_STEPS?k:XZ_SDK_UI_DETECT;break;
 case XZ_UI_SDK_FIX:
  xz_ui_navigate(u,XZ_UI_APPS_SETUP);u->sdk_step=XZ_SDK_UI_INSTALL;
  if(k>=0&&k<(int)count(s)){const struct xz_sdk_item *item=&s->snapshot.items[k];
   snprintf(u->notice,sizeof(u->notice),"%.40s: %.50s",item->name[0]?item->name:item->id,item->detail[0]?item->detail:"BLOCKED");}
  break;
 case XZ_UI_SDK_CHOICE:u->sdk_choice=k?1:0;value=k?!s->snapshot.autoload_enabled:s->snapshot.autoload_enabled;break;
 case XZ_UI_SDK_AUTOLOAD:value=!s->snapshot.autoload_enabled;break;
 case XZ_UI_SDK_SAVE:value=1;break;
 case XZ_UI_SDK_DONE:xz_ui_navigate(u,XZ_UI_APPS);snprintf(u->notice,sizeof(u->notice),"LIVE TEST CONFIRMED / RUN APPS FROM THIS LIST");break;
 default:break;
 }
 /* Without a manager, list controls lead to the one place that explains the fix. */
 if(!running(s)&&u->page==XZ_UI_APPS&&(a->kind==XZ_UI_SDK_SCAN||a->kind==XZ_UI_SDK_AUTOLOAD)){xz_ui_navigate(u,XZ_UI_APPS_SETUP);u->sdk_step=XZ_SDK_UI_DETECT;}
 u->capture=-1;
 out[0]=(struct xz_ui_action){.kind=a->kind,.phase=XZ_UI_PRESS,.deck=u->deck,.index=k,.value=value,.pc_target=-1,
  .sdk_generation=s->snapshot.generation};
 return 1;
}
static void note(struct xz_sdk_ui_plan *p,const char *format,...) __attribute__((format(printf,2,3)));
static void note(struct xz_sdk_ui_plan *p,const char *format,...){
 va_list args;va_start(args,format);vsnprintf(p->notice,sizeof(p->notice),format,args);va_end(args);
}
void xz_sdk_ui_plan(const struct xz_sdk_ui_model *s,const struct xz_ui_action *a,struct xz_sdk_ui_plan *p){
 enum xz_sdk_command_kind command=0;uint32_t value=0,index=0;int k=a->index;
 memset(p,0,sizeof(*p));
 if(!xz_sdk_ui_kind(a->kind)||a->phase!=XZ_UI_PRESS)return;
 switch(a->kind){
 case XZ_UI_SDK_RUN:case XZ_UI_SDK_SAVE:case XZ_UI_SDK_STOP:{
  if(a->sdk_generation!=s->snapshot.generation||k<0||k>=(int)count(s)){note(p,"APP LIST CHANGED / CHECK THE ROW AND TAP AGAIN");return;}
  const struct xz_sdk_item *item=&s->snapshot.items[k];const char *name=item->name[0]?item->name:item->id;
  index=(uint32_t)k;
  if(a->kind==XZ_UI_SDK_STOP){
   if(item->state!=XZ_SDK_RUNNING){note(p,"%.60s IS NOT RUNNING",name);return;}
   command=XZ_SDK_STOP;note(p,"STOPPING %.60s",name);break;
  }
  if(item->state==XZ_SDK_BLOCKED){note(p,"%.40s: %.50s",name,item->detail[0]?item->detail:"BLOCKED");return;}
  if(item->kind!=XZ_SDK_PROFILE&&item->state==XZ_SDK_RUNNING){note(p,"%.60s IS ALREADY RUNNING",name);return;}
  if(item->kind==XZ_SDK_APPLICATION&&s->display_busy){note(p,"EXIT THE GAME BEFORE RUNNING %.40s",name);return;}
  if(item->kind==XZ_SDK_APPLICATION&&s->snapshot.foreground){note(p,"STOP THE RUNNING APP BEFORE STARTING %.35s",name);return;}
  command=XZ_SDK_RUN;
  /* RUN value 1 asks the manager to pass persist=1 to apply_profile. */
  value=item->kind==XZ_SDK_PROFILE&&a->kind==XZ_UI_SDK_SAVE;
  if(item->kind!=XZ_SDK_PROFILE)note(p,"STARTING %.60s",name);
  else note(p,value?"APPLYING AND SAVING %.60s":"APPLYING %.60s FOR THIS SESSION",name);
  break;}
 case XZ_UI_SDK_SCAN:command=XZ_SDK_SCAN;note(p,"SEARCHING USB FOR APPS");break;
 case XZ_UI_SDK_AUTOLOAD:
  command=XZ_SDK_AUTOLOAD;value=a->value!=0;
  note(p,"%s",value?"AUTOLOAD ON / AUTOLOAD FOLDER APPS START ON USB INSERT":"AUTOLOAD OFF / APPS START ONLY FROM THIS LIST");
  break;
 case XZ_UI_SDK_CHOICE:
  if(a->value!=0){command=XZ_SDK_AUTOLOAD;value=k?1u:0u;note(p,"%s",k?"AUTOLOAD REQUESTED FOR THE CONFIGURED FOLDER":"MANUAL START REQUESTED / AUTOLOAD OFF");}
  break;
 case XZ_UI_SDK_SETUP:case XZ_UI_SDK_STEP:case XZ_UI_SDK_GOTO:
  if(k==XZ_SDK_UI_DETECT)command=XZ_SDK_SCAN; /* detection runs on arrival */
  break;
 default:break;
 }
 if(!command)return;
 if(!running(s)){note(p,"%s","APP MANAGER IS NOT RUNNING / RESTART THE XZ FROM ITS XZ MODS USB");return;}
 p->submit=1;p->command=(struct xz_sdk_command){command,s->snapshot.generation,index,value};
}
static void terminate(char *text,size_t size){text[size-1]=0;}
void xz_sdk_ui_model_read(struct xz_sdk_ui_model *m,const struct xz_sdk_snapshot *fresh,int display_busy){
 m->fresh=fresh!=NULL;m->display_busy=display_busy!=0;
 if(!fresh)return;
 m->snapshot=*fresh;m->foreground=fresh->foreground!=0;
 struct xz_sdk_snapshot *s=&m->snapshot;
 if(s->count>XZ_SDK_PACKAGES)s->count=XZ_SDK_PACKAGES;
 terminate(s->status,sizeof(s->status));
 for(unsigned i=0;i<XZ_SDK_PACKAGES;i++){
  struct xz_sdk_item *item=&s->items[i];
  terminate(item->id,sizeof(item->id));terminate(item->name,sizeof(item->name));
  terminate(item->version,sizeof(item->version));terminate(item->detail,sizeof(item->detail));
 }
}
struct xz_sdk_ui_change xz_sdk_ui_session_step(struct xz_sdk_ui_session *s,int foreground,int takeover){
 struct xz_sdk_ui_change change={0,takeover,0};
 if(foreground&&!s->active){s->active=1;s->previous=takeover;change=(struct xz_sdk_ui_change){1,1,1};}
 else if(!foreground&&s->active){s->active=0;change=(struct xz_sdk_ui_change){1,s->previous,0};}
 return change;
}
int xz_sdk_ui_saved_takeover(const struct xz_sdk_ui_session *s,int takeover){return s->active?s->previous:takeover;}
int xz_sdk_ui_session_profile(struct xz_sdk_ui_session *s,int profile_takeover){
 if(!s->active)return profile_takeover;
 s->previous=profile_takeover;return 1;
}
unsigned xz_sdk_ui_roots(const char *usb,const char *roots[XZ_SDK_ROOTS],char *storage,size_t size,int *extra){
 static const char *const defaults[2]={"/media/usb1","/media/usb2"};
 unsigned n=0;size_t length=usb?strlen(usb):0;
 *extra=0;
 for(;n<2;n++)roots[n]=defaults[n];
 while(length>1&&usb[length-1]=='/')length--;
 /* The manager opens each root as a USB root, and the loader USB is usually a
    partition mount beneath /media/usbN, so only an exact duplicate collapses. */
 if(usb&&usb[0]=='/'&&length>1&&length<size&&length<XZ_SDK_PATH&&!strstr(usb,"..")&&!strchr(usb,'\n')&&!strchr(usb,'\\')){
  int duplicate=0;
  for(unsigned i=0;i<n;i++)duplicate|=strlen(roots[i])==length&&!strncmp(usb,roots[i],length);
  if(!duplicate){memcpy(storage,usb,length);storage[length]=0;roots[n++]=storage;*extra=1;}
 }
 for(unsigned i=n;i<XZ_SDK_ROOTS;i++)roots[i]=NULL;
 return n;
}

/* Rendering. Text sits only in rows the controls leave free, in theme colours. */
struct inks { uint32_t ink,bg,dim,accent,alarm,panel; };
static struct inks inks(const struct xz_ui_scene *s){
 const struct xz_theme_palette *p=s->p;int analog=s->m->theme==XZ_THEME_ANALOG;
 uint32_t ink=analog?0x3a3020:p->ink,bg=analog?0xd9c9a3:p->bg;
 return (struct inks){ink,bg,xz_blend(bg,ink,170),p->accent,p->alarm,xz_blend(bg,ink,22)};
}
static void text(struct xz_canvas c,int x,int y,int width,const char *value,int scale,uint32_t color){
 if(scale>1&&xz_text_width(c,value,2,200)>width)scale=1;
 xz_text(c,x,y,value,scale,width/(6*scale),color);
}
static const char *kind_name(uint32_t kind){
 return kind==XZ_SDK_APPLICATION?"APP":kind==XZ_SDK_EXTENSION?"SERVICE":kind==XZ_SDK_PROFILE?"PROFILE":"PACKAGE";
}
static const char *state_name(const struct xz_sdk_item *item){
 switch(item->state){
 case XZ_SDK_RUNNING:return item->kind==XZ_SDK_APPLICATION?"RUNNING ON SCREEN":item->kind==XZ_SDK_PROFILE?"APPLIED":"RUNNING IN BACKGROUND";
 case XZ_SDK_FAILED:return "FAILED";
 case XZ_SDK_BLOCKED:return "BLOCKED";
 case XZ_SDK_READY:return "READY";
 default:return "CHECKING";
 }
}
static uint32_t state_color(const struct inks *k,const struct xz_sdk_item *item){
 return item->state==XZ_SDK_RUNNING?k->accent:item->state==XZ_SDK_FAILED||item->state==XZ_SDK_BLOCKED?k->alarm:k->ink;
}
static void row(struct xz_canvas c,int theme,const struct inks *k,const struct xz_sdk_item *item,int y){
 int width=two_buttons(item)?PROFILE_W:ROW_W,x=X0+14;char line[64];
 xz_theme_frame(xz_surface(c),theme,(struct xz_theme_rect){X0,y,width,ROW_H},k->panel,0,XZ_THEME_LIST);
 xz_rect(c,X0+3,y+4,4,ROW_H-8,item->state==XZ_SDK_READY?xz_blend(k->bg,k->ink,90):state_color(k,item));
 snprintf(line,sizeof(line),"V%.30s",item->version);
 int version=item->version[0]?xz_text_width(c,line,1,40):0;
 text(c,x,y+5,width-28-(version?version+12:0),item->name[0]?item->name:item->id,2,k->ink);
 if(version)xz_text(c,X0+width-10-version,y+8,line,1,40,k->dim);
 snprintf(line,sizeof(line),"%s%s",kind_name(item->kind),item->autoload?" / AUTOLOAD":"");
 xz_text(c,x,y+28,line,1,24,k->dim);x+=xz_text_width(c,line,1,24)+12;
 const char *state=state_name(item);
 xz_text(c,x,y+28,state,1,24,state_color(k,item));x+=xz_text_width(c,state,1,24)+12;
 if(item->detail[0]&&x<X0+width-40)xz_text(c,x,y+28,item->detail,1,(X0+width-10-x)/6,k->dim);
}
static void browser(const struct xz_ui_scene *s,const struct inks *k){
 struct xz_canvas c=s->c;const struct xz_ui *u=s->u;const struct xz_sdk_ui_model *sdk=&s->m->sdk;char buf[96];
 int n=(int)count(sdk),first=first_row(u,sdk),last=first+XZ_SDK_UI_ROWS<n?first+XZ_SDK_UI_ROWS:n;
 text(c,X0,52,430,"APPLICATIONS",2,k->ink);
 if(!running(sdk))snprintf(buf,sizeof(buf),"APP MANAGER NOT RUNNING");
 else if(sdk->snapshot.scanning)snprintf(buf,sizeof(buf),"SEARCHING USB / %d FOUND SO FAR",n);
 else if(!n)snprintf(buf,sizeof(buf),"NO PACKAGES FOUND");
 else snprintf(buf,sizeof(buf),"%d PACKAGE%s / SHOWING %d-%d",n,n==1?"":"S",first+1,last);
 text(c,X0,76,430,buf,1,running(sdk)?k->dim:k->alarm);
 for(int i=first;i<last;i++)row(c,s->m->theme,k,&sdk->snapshot.items[i],ROW_Y+(i-first)*ROW_PITCH);
 if(n)return;
 if(!running(sdk)){
  text(c,X0,150,656,"THE APP MANAGER IS NOT RUNNING",2,k->alarm);
  text(c,X0,184,656,"SET UP APPS CHECKS IT AND SHOWS THE ONE THING TO DO.",1,k->ink);
 }else if(sdk->snapshot.scanning)text(c,X0,150,656,"SEARCHING USB FOR APPS...",2,k->ink);
 else{
  text(c,X0,150,656,"NO APPS ON USB YET",2,k->ink);
  text(c,X0,184,656,"PUT XZ SDK PACKAGES IN " XZ_SDK_UI_APPLICATIONS_FOLDER " ON A USB.",1,k->ink);
  text(c,X0,204,656,"SET UP APPS SHOWS THE DESKTOP PACKER COMMAND AND RUNS A LIVE TEST.",1,k->dim);
 }
}
static void detect(struct xz_canvas c,const struct inks *k,const struct xz_sdk_ui_model *sdk){
 char buf[96];unsigned kinds[4]={0},trouble=0;
 if(!running(sdk)){
  text(c,X0,146,656,"APP MANAGER: NOT RUNNING",2,k->alarm);
  text(c,X0,178,656,"RESTART THE XZ WITH ITS XZ MODS USB INSERTED, THEN CHECK AGAIN.",1,k->ink);
  text(c,X0,198,656,"THE MANAGER STARTS WITH XZ MODS. DJ PLAYBACK IS NOT AFFECTED.",1,k->dim);
  return;
 }
 for(unsigned i=0;i<count(sdk);i++){
  const struct xz_sdk_item *item=&sdk->snapshot.items[i];
  kinds[item->kind<4?item->kind:0]++;trouble+=item->state==XZ_SDK_FAILED||item->state==XZ_SDK_BLOCKED;
 }
 text(c,X0,146,656,"APP MANAGER: RUNNING",2,k->accent);
 snprintf(buf,sizeof(buf),"SEARCHING USB1 AND USB2%s",sdk->extra_root?" AND THE XZ MODS USB":"");
 text(c,X0,178,656,buf,1,k->ink);
 text(c,X0,196,656,"FOLDERS: " XZ_SDK_UI_APPLICATIONS_FOLDER " AND " XZ_SDK_UI_AUTOLOAD_FOLDER,1,k->dim);
 if(sdk->snapshot.scanning)snprintf(buf,sizeof(buf),"SEARCHING USB...");
 else if(!count(sdk))snprintf(buf,sizeof(buf),"NO APPS FOUND YET");
 else snprintf(buf,sizeof(buf),"FOUND %u APP%s, %u SERVICE%s, %u PROFILE%s",kinds[XZ_SDK_APPLICATION],kinds[XZ_SDK_APPLICATION]==1?"":"S",
  kinds[XZ_SDK_EXTENSION],kinds[XZ_SDK_EXTENSION]==1?"":"S",kinds[XZ_SDK_PROFILE],kinds[XZ_SDK_PROFILE]==1?"":"S");
 text(c,X0,226,656,buf,2,count(sdk)?k->ink:k->dim);
 if(trouble){snprintf(buf,sizeof(buf),"%u NEED ATTENTION / APPLICATIONS SHOWS WHY",trouble);text(c,X0,256,656,buf,1,k->alarm);}
 if(sdk->snapshot.status[0])text(c,X0,280,656,sdk->snapshot.status,1,k->dim);
}
static void folders(struct xz_canvas c,const struct inks *k,const struct xz_sdk_ui_model *sdk){
 text(c,X0,146,656,"HOW SHOULD APPS START? DJ CONTROLS KEEP WORKING EITHER WAY.",1,k->ink);
 text(c,X0,230,324,"DEFAULT: " XZ_SDK_UI_APPLICATIONS_FOLDER,1,k->dim);
 text(c,X0,248,324,"YOU TAP RUN IN APPLICATIONS",1,k->dim);
 text(c,468,230,324,"DEFAULT: " XZ_SDK_UI_AUTOLOAD_FOLDER,1,k->dim);
 text(c,468,248,324,"RUNS ONCE EACH TIME THE USB GOES IN",1,k->dim);
 if(!running(sdk))text(c,X0,282,656,"AUTOLOAD: APP MANAGER NOT RUNNING",2,k->alarm);
 else text(c,X0,282,656,sdk->snapshot.autoload_enabled?"AUTOLOAD IS ON":"AUTOLOAD IS OFF",2,sdk->snapshot.autoload_enabled?k->accent:k->ink);
 text(c,X0,312,656,"VJ.Tools/XZ-SDK.cfg CAN REPLACE THESE DEFAULT FOLDERS.",1,k->dim);
}
static void install(struct xz_canvas c,int theme,const struct inks *k,const struct xz_ui *u,const struct xz_sdk_ui_model *sdk){
 char buf[128];int t=test_target(sdk);
 text(c,X0,146,656,"ON YOUR COMPUTER, WITH THE USB PLUGGED IN, RUN THE XZ SDK PACKER:",1,k->ink);
 xz_theme_frame(xz_surface(c),theme,(struct xz_theme_rect){X0,162,656,52},xz_blend(k->bg,k->ink,30),0,XZ_THEME_LIST);
 text(c,X0+10,170,624,"python tools/sdk.py build examples/test-card --output build/test-card",1,k->ink);
 snprintf(buf,sizeof(buf),"python tools/sdk.py stage build/test-card --root <USB DRIVE> --folder %s",u->sdk_choice?XZ_SDK_UI_AUTOLOAD_FOLDER:XZ_SDK_UI_APPLICATIONS_FOLDER);
 text(c,X0+10,190,624,buf,1,k->ink);
 text(c,X0,224,656,"EJECT THE USB, PLUG IT INTO THE XZ, THEN TAP DETECT.",1,k->ink);
 if(!running(sdk))text(c,X0,252,656,"APP MANAGER NOT RUNNING / SEE STEP 1",2,k->alarm);
 else if(sdk->snapshot.scanning)text(c,X0,252,656,"SEARCHING USB...",2,k->ink);
 else if(count(sdk)){snprintf(buf,sizeof(buf),"FOUND %u PACKAGE%s",count(sdk),count(sdk)==1?"":"S");text(c,X0,252,656,buf,2,k->accent);}
 else text(c,X0,252,656,"NOTHING FOUND YET",2,k->ink);
 if(t>=0){snprintf(buf,sizeof(buf),"TEST APP: %.60s",sdk->snapshot.items[t].name);text(c,X0,280,656,buf,1,k->ink);}
 else text(c,X0,280,656,"NO APPLICATION YET / THE COMMANDS ABOVE INSTALL THE SDK TEST CARD",1,k->dim);
 for(unsigned i=0;i<count(sdk);i++)if(sdk->snapshot.items[i].state==XZ_SDK_BLOCKED){
  snprintf(buf,sizeof(buf),"BLOCKED: %.30s / %.60s",sdk->snapshot.items[i].name,sdk->snapshot.items[i].detail);text(c,X0,300,656,buf,1,k->alarm);break;
 }
}
static void verify(struct xz_canvas c,const struct inks *k,const struct xz_ui_model *m){
 const struct xz_sdk_ui_model *sdk=&m->sdk;char buf[128];int t=test_target(sdk);
 text(c,X0,146,656,"THE LIVE TEST RUNS ONE APP ON THIS SCREEN. DJ AUDIO KEEPS PLAYING.",1,k->ink);
 if(!running(sdk)){text(c,X0,176,656,"APP MANAGER NOT RUNNING",2,k->alarm);text(c,X0,204,656,"STEP 1 SHOWS THE FIX.",1,k->ink);return;}
 if(t<0){text(c,X0,176,656,"NO APPLICATION ON USB",2,k->ink);text(c,X0,204,656,"INSTALL ONE IN STEP 3, THEN DETECT AGAIN.",1,k->ink);return;}
 const struct xz_sdk_item *item=&sdk->snapshot.items[t];
 text(c,X0,176,656,item->name[0]?item->name:item->id,2,k->ink);
 snprintf(buf,sizeof(buf),"%.60s / VERSION %.30s",item->id,item->version[0]?item->version:"?");text(c,X0,200,656,buf,1,k->dim);
 text(c,X0,224,656,item->state==XZ_SDK_READY?"READY TO RUN":state_name(item),2,state_color(k,item));
 if(item->detail[0])text(c,X0,250,656,item->detail,1,k->dim);
 if(item->state==XZ_SDK_RUNNING){
  /* The receiver's own frame count, not a guess: frames arrive or they do not. */
  int frames=m->connection.ready&&m->connection.connected&&m->connection.stats_valid&&m->connection.frame_hz>0;
  if(frames)snprintf(buf,sizeof(buf),"RECEIVER: %.1f FPS FROM THE APP",(double)m->connection.frame_hz);
  text(c,X0,270,656,frames?buf:"RECEIVER: NO FRAMES YET",1,frames?k->accent:k->alarm);
  text(c,X0,294,656,"MODS (TOP RIGHT) REOPENS THIS PAGE. STOP HERE OR THE APP'S EXIT ENDS THE TEST.",1,k->dim);
 }else if(sdk->display_busy)text(c,X0,294,656,"A GAME HAS THE SCREEN / EXIT IT BEFORE THE TEST",1,k->alarm);
 else text(c,X0,294,656,"RUN CLOSES THIS PANEL AND SHOWS THE APP.",1,k->dim);
}
void xz_sdk_ui_render(const struct xz_ui_scene *s){
 struct xz_canvas c=s->c;const struct xz_ui *u=s->u;const struct xz_ui_model *m=s->m;struct inks k=inks(s);char buf[96];
 if(u->page==XZ_UI_EXTRAS){
  /* The APPLICATIONS card carries its own title; only live state is added here. */
  if(m->sdk.foreground)text(c,468+16,48+146,292,"AN APP IS RUNNING / OPEN TO STOP IT",1,k.accent);
  return;
 }
 if(u->page==XZ_UI_APPS)browser(s,&k);
 else if(u->page==XZ_UI_APPS_SETUP){
  int step=step_of(u);
  text(c,X0,52,430,"SET UP APPS",2,k.ink);
  snprintf(buf,sizeof(buf),"STEP %d OF %d / %s",step+1,XZ_SDK_UI_STEPS,step_titles[step]);text(c,X0,76,656,buf,1,k.dim);
  if(step==XZ_SDK_UI_DETECT)detect(c,&k,&m->sdk);
  else if(step==XZ_SDK_UI_FOLDERS)folders(c,&k,&m->sdk);
  else if(step==XZ_SDK_UI_INSTALL)install(c,m->theme,&k,u,&m->sdk);
  else verify(c,&k,m);
 }else return;
 int kind,index;
 if(m->theme==XZ_THEME_LCARS||!xz_sdk_ui_primary(u,m,&kind,&index))return;
 for(size_t i=0;i<s->n;i++)if((int)s->w[i].kind==kind&&s->w[i].index==index){
  struct xz_ui_widget a=s->w[i];xz_rect(c,a.x,a.y+a.h-3,a.w,3,s->p->accent);
 }
}
