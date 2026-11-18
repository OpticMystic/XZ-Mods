/* SPDX-License-Identifier: MIT */
/* Host test: the generated stock UTILITY table and the pure mapping in shell_utility.c. */
#ifdef NDEBUG
#error "Shell utility acceptance requires active assertions"
#endif
#include "shell_utility_table.h"
#include "shell_runtime.h"
#include "shell/fixtures.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const struct xz_shut_def *by_key(const char *key) {
 for(int i=0;i<XZ_SHUT_DEFS;i++)if(!strcmp(xz_shut_defs[i].key,key))return &xz_shut_defs[i];
 return NULL;
}
static int ascii(const char *s) { for(;*s;s++)if((unsigned char)*s<32||(unsigned char)*s>126)return 0;return 1; }


static struct xz_shell_action actions[XZ_SHELL_ACTIONS];
static size_t input(struct xz_shell *u, struct xz_shell_model *m, int kind, int x, int y, int delta, int key) {
 struct xz_shell_event e={kind,x,y,delta,key,-1};
 return xz_shell_input(u,m,&e,actions);
}
static size_t tap(struct xz_shell *u, struct xz_shell_model *m, int x, int y) {
 size_t n=input(u,m,XZ_SHELL_TOUCH_DOWN,x,y,0,0);
 struct xz_shell_action down[XZ_SHELL_ACTIONS];memcpy(down,actions,sizeof down);
 size_t end=input(u,m,XZ_SHELL_TOUCH_UP,x,y,0,0);
 if(n){assert(!end);memcpy(actions,down,sizeof down);}
 return n+end;
}
static uint16_t pixels[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT];
static unsigned white_ring(const struct xz_shell *u, const struct xz_shell_model *m) {
 assert(xz_shell_render(u,m,pixels,XZ_SHELL_WIDTH));
 struct xz_shell_rect v=xz_shell_settings_rect(XZ_SHELL_SR_VALUES);
 unsigned n=0;
 /* The first alternative's radio ring, above the value text. */
 for(int y=v.y+10;y<v.y+12;y++)for(int x=v.x+15;x<v.x+30;x++)
  n+=pixels[y*XZ_SHELL_WIDTH+x]>0x8000;
 return n;
}
static void test_controls(void) {
 struct fx_track track;fx_track_build(&track,1);
 int32_t raw[XZ_SHUT_DEFS];uint8_t known[XZ_SHUT_DEFS],get[XZ_SHUT_DEFS],set[XZ_SHUT_DEFS];
 struct xz_shell_setting_item rows[XZ_SHUT_DEFS];const char *notes[XZ_SHUT_DEFS];
 for(int i=0;i<XZ_SHUT_DEFS;i++){raw[i]=xz_shut_defs[i].args[xz_shut_defs[i].count-1];known[i]=get[i]=set[i]=1;}
 /* Normal development; explicit RAM opt-in; RAM read-only; public USB read-only;
    USB opt-in; missing getter; missing setter/live-apply guard; missing global capability. */
 const int readonly[]={0,1,1,1,1,0,0,0},writes[]={0,1,0,0,1,0,0,0};
 int trial[8];
 for(int mode=0;mode<8;mode++)trial[mode]=xz_shut_trial(readonly[mode],writes[mode]);
 assert(!xz_shut_trial(0,1));
 for(unsigned mode=0;mode<sizeof trial/sizeof *trial;mode++)for(int i=0;i<XZ_SHUT_DEFS;i++){
  get[i]=mode!=5;set[i]=mode!=6;
  xz_shut_items(xz_shut_defs,XZ_SHUT_DEFS,raw,known,get,set,trial[mode],rows,notes);
  const struct xz_shut_def *d=&xz_shut_defs[i];
  struct xz_shell_model m;fx_model_full(&m,&track);
  m.capabilities|=XZ_SHELL_CAP_SET_SETTING;
  if(mode==7)m.capabilities&=~XZ_SHELL_CAP_SET_SETTING;
  struct xz_shell_settings_model sm={1,rows+i,notes+i};m.settings_screen=&sm;
  struct xz_shell u;xz_shell_init(&u);xz_shell_mods_show(&u,&m,0);
  u.stack[u.depth-1].cursor=d->id+1;
  int live=rows[i].writable&&mode!=7&&d->count>1;
  struct xz_shell_rect v=xz_shell_settings_rect(XZ_SHELL_SR_VALUES);
  if(d->count>1){unsigned ring=white_ring(&u,&m);if(!!ring!=live)fprintf(stderr,"mode=%u item=%s ring=%u live=%d\n",mode,d->key,ring,live);assert(!!ring==live);}
  size_t n=tap(&u,&m,v.x+50,v.y+18);
  assert(n==(size_t)live);
  if(live){
   assert(actions[0].kind==XZ_SHELL_ACT_SET_SETTING&&actions[0].id==d->id&&actions[0].value==0);
   int32_t arg;assert(xz_shut_arg(d,actions[0].value,&arg));assert(arg==d->args[0]);
   /* Confirmed stock readback is published on the next refresh. */
   raw[i]=arg;xz_shut_items(xz_shut_defs,XZ_SHUT_DEFS,raw,known,get,set,trial[mode],rows,notes);
   assert(rows[i].current==0&&tap(&u,&m,v.x+50,v.y+18)==0);
   raw[i]=d->args[d->count-1];
  }else{
   assert((u.stack[u.depth-1].focus&3)==1);
   assert(input(&u,&m,XZ_SHELL_ENCODER_PUSH,0,0,0,0)==0);
   assert((u.stack[u.depth-1].focus&3)==1);
  }
  if(d->id==XZ_SHELL_ITEM_NEEDLE_LOCK||d->id==XZ_SHELL_ITEM_QUANTIZE_BEAT||d->id==XZ_SHELL_ITEM_HOT_CUE_AUTO_LOAD){
   xz_shut_items(xz_shut_defs,XZ_SHUT_DEFS,raw,known,get,set,trial[mode],rows,notes);
   xz_shell_init(&u);assert(!input(&u,&m,XZ_SHELL_KEY,0,0,0,XZ_SHELL_KEY_SHORTCUT));
   int found=0;
   for(int k=0;k<16;k++){
    input(&u,&m,XZ_SHELL_ENCODER_TURN,0,0,k?1:0,0);
    if(u.stack[u.depth-1].cursor==d->id+1){found=1;break;}
   }
   assert(found);
   n=input(&u,&m,XZ_SHELL_ENCODER_PUSH,0,0,0,0);assert(n==(size_t)live);
   if(live)assert(actions[0].kind==XZ_SHELL_ACT_SET_SETTING&&actions[0].id==d->id&&actions[0].value==0);
   /* In the singleton model QUANTIZE is slot 0; HOT CUE and NEEDLE follow the
      fallback QUANTIZE, BEAT JUMP and HOT CUE rows. */
   struct xz_shell_rect slot=xz_shell_shortcut_slot(0,d->id==XZ_SHELL_ITEM_QUANTIZE_BEAT?0:d->id==XZ_SHELL_ITEM_NEEDLE_LOCK?3:2);
   n=tap(&u,&m,slot.x+30,slot.y+25);assert(n==(size_t)live);
   if(live)assert(actions[0].kind==XZ_SHELL_ACT_SET_SETTING&&actions[0].id==d->id&&actions[0].value==0);
  }
  get[i]=set[i]=1;
 }
 puts("PASS UTILITY/SHORTCUT: every stock item, write modes, guards, no dead choices or hand-offs, confirmed readback");
}

int main(void) {
 test_controls();
 assert(XZ_SHUT_DEFS>=34&&XZ_SHUT_DEFS<=40);
 int writable=0;
 for(int i=0;i<XZ_SHUT_DEFS;i++){
  const struct xz_shut_def *d=&xz_shut_defs[i];
  assert(d->key&&d->label&&ascii(d->label)&&d->count>=1&&d->choices&&d->args);
  assert(!strstr(d->label,"rekordbox"));
  for(int k=0;k<d->count;k++){
   assert(d->choices[k]&&ascii(d->choices[k])&&!strstr(d->choices[k],"rekordbox"));
   /* Every choice has its own setter argument, and the reading maps back to it. */
   int32_t arg=-99;assert(xz_shut_arg(d,k,&arg));assert(xz_shut_index(d,arg)==k);
   for(int j=0;j<k;j++)assert(d->args[j]!=d->args[k]);
  }
  int32_t arg;assert(!xz_shut_arg(d,-1,&arg)&&!xz_shut_arg(d,d->count,&arg));
  assert(d->category>=0&&d->category<XZ_SHELL_SETTING_CAT_COUNT);
  /* One id per item; never a shell-owned display id. */
  for(int j=0;j<i;j++)assert(xz_shut_defs[j].id!=d->id);
  assert(!xz_shrt_shell_owned_setting(d->id));
  assert(xz_shut_find(xz_shut_defs,XZ_SHUT_DEFS,d->id)==i);
  if(d->get&&d->set){assert(d->get_code[0]||d->get_code[1]);assert(d->set_code[0]||d->set_code[1]);}
  writable+=d->mapped&&d->get&&d->set;
 }
 assert(writable>=33);
 assert(xz_shut_find(xz_shut_defs,XZ_SHUT_DEFS,XZ_SHELL_ITEM_WAVE_COLOR)<0);

 /* The forms the firmware uses. */
 const struct xz_shut_def *q=by_key("QuantizeValue"),*h=by_key("HotCueAutoLoad"),*l=by_key("LcdBrightness"),
  *lang=by_key("Language"),*lock=by_key("EjectLoadLock"),*touch=by_key("TouchPanelCalibration"),*fader=by_key("ChFaderCurve");
 assert(q&&h&&l&&lang&&lock&&touch&&fader);
 assert(q->id==XZ_SHELL_ITEM_QUANTIZE_BEAT&&q->count==4);
 /* The same encoding as the existing QUANTIZE path: choices 1/8 .. 1, UiSetUtilQuantizeValue 3 .. 0. */
 for(int k=0;k<4;k++)assert(q->args[k]==3-k);
 assert(!strncmp(q->choices[0],"1/8",3)&&!strncmp(q->choices[3],"1 ",2));
 assert(h->count==3&&h->args[0]==0&&h->args[1]==2&&h->args[2]==1&&!strcmp(h->choices[1],"LIBRARY SETTING"));
 assert(l->count==5&&l->args[0]==1&&l->args[4]==5&&l->apply);
 assert(lang->count==18&&lang->args[0]==1&&!strcmp(lang->choices[0],"ENGLISH"));
 assert(lock->id==XZ_SHELL_ITEM_LOAD_LOCK&&lock->args[1]==1&&!lock->apply);
 assert(fader->apply&&fader->apply_code[0]|fader->apply_code[1]);
 assert(touch->id==XZ_SHRT_UTIL_ID(16)&&!touch->get&&!touch->set&&touch->count==1);
 assert(by_key("NeedleLock")->id==XZ_SHELL_ITEM_NEEDLE_LOCK); /* the shell item; runtime id 0 stays reserved */

 /* Reasons: stock-only, guards, trial. */
 assert(xz_shut_reason(touch,1,1,0)&&!strcmp(xz_shut_reason(touch,1,1,0),XZ_SHUT_NOTE_STOCK));
 assert(!xz_shut_reason(lock,1,1,0));
 assert(!strcmp(xz_shut_reason(lock,0,1,0),XZ_SHUT_NOTE_FIRMWARE));
 assert(!strcmp(xz_shut_reason(lock,1,0,0),XZ_SHUT_NOTE_FIRMWARE));
 assert(!strcmp(xz_shut_reason(lock,1,1,1),XZ_SHUT_NOTE_TRIAL));
 assert(strlen(XZ_SHUT_NOTE_TRIAL)<=40&&strlen(XZ_SHUT_NOTE_FIRMWARE)<=40&&strlen(XZ_SHUT_NOTE_STOCK)<=40);

 /* Published rows. */
 int32_t raw[XZ_SHUT_DEFS];uint8_t known[XZ_SHUT_DEFS],ok[XZ_SHUT_DEFS];
 struct xz_shell_setting_item items[XZ_SHUT_DEFS];const char *notes[XZ_SHUT_DEFS];
 for(int i=0;i<XZ_SHUT_DEFS;i++){raw[i]=xz_shut_defs[i].args[xz_shut_defs[i].count-1];known[i]=1;ok[i]=1;}
 int qi=(int)(q-xz_shut_defs),li=(int)(lock-xz_shut_defs),ti=(int)(touch-xz_shut_defs),hi=(int)(h-xz_shut_defs);
 raw[qi]=0;   /* 1 beat */
 raw[hi]=77;  /* not a choice: unknown, never a wrong value */
 known[li]=0;
 xz_shut_items(xz_shut_defs,XZ_SHUT_DEFS,raw,known,ok,ok,0,items,notes);
 assert(items[qi].known&&items[qi].current==3&&items[qi].writable&&!notes[qi]);
 assert(!items[hi].known);
 assert(!items[li].known&&items[li].writable);
 assert(items[ti].known&&items[ti].current==0&&!items[ti].writable&&!strcmp(notes[ti],XZ_SHUT_NOTE_STOCK));
 xz_shut_items(xz_shut_defs,XZ_SHUT_DEFS,raw,known,ok,ok,1,items,notes);
 for(int i=0;i<XZ_SHUT_DEFS;i++){assert(!items[i].writable);assert(notes[i]);}
 assert(!strcmp(notes[qi],XZ_SHUT_NOTE_TRIAL));
 puts("PASS stock UTILITY table: ids, choices, setter arguments, reasons and rows");
 return 0;
}
