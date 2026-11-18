#include "extra_timeline_projection.h"
#include "key/view.h"
/* SPDX-License-Identifier: MIT */
/* Pure parts of the FLAGSHIP shell runtime. No device, threads or allocation. */
#include "shell_runtime.h"
#include "shell/shell_settings.h"
#include "shell/shell_browse.h"
#include "encoder_push.h"
#include "settings.h"
#include "ui/deck_view.h"
#include "ui/extra_lane.h"
#include <stdint.h>
#include <string.h>

/* Codes from VJToolsLibrary docs/research/2026-10-18-xz-shell-abi.md R1. */
static const struct xz_shrt_key_map keys[]={
 {0x209,0,XZ_SHRT_ON_RELEASE,XZ_SHELL_KEY_BROWSE,XZ_SHELL_KEY_BROWSE_HOLD,-1},
 {0x20a,0,XZ_SHRT_ON_PRESS,XZ_SHELL_KEY_TAG_LIST,-1,-1},
 {0x206,0,XZ_SHRT_ON_PRESS,XZ_SHELL_KEY_INFO,-1,-1},
 {0x20b,0,XZ_SHRT_ON_RELEASE,XZ_SHELL_KEY_MENU,XZ_SHELL_KEY_MENU_HOLD,-1},
 {0x20e,0,XZ_SHRT_ON_PRESS,XZ_SHELL_KEY_SHORTCUT,-1,-1},
 /* 0x203 USB1, 0x204 USB2: BrowseUiIfDpl::onKey sends 0x204 to kind 2 (UiKey_Usb2, 0x2aacb0). */
 {0x203,0,XZ_SHRT_ON_PRESS,XZ_SHELL_KEY_SOURCE_USB1,-1,-1},
 {0x204,0,XZ_SHRT_ON_PRESS,XZ_SHELL_KEY_SOURCE_USB2,-1,-1},
 {0x420d,0,XZ_SHRT_ON_PRESS,XZ_SHELL_KEY_TRACK_FILTER,-1,-1},
 /* LINK: MODERN's SOURCE screen with the LINK tile highlighted (what the rear USB port holds, the linked
    players and DJ software ways in). Pressed again there (or the tile pushed), stock's media and connection
    screen opens (linked players' media, CONTROL MODE for LOAD 1/2) with a visible hand-off: the shell's
    stick() hands off when SOURCE already shows LINK. REKORDBOX does what MODS > CONTROLS > BUTTONS assigns
    (xz_shrt_rekordbox_verdict); its stock action hands off. */
 {0x205,0,XZ_SHRT_ON_PRESS,XZ_SHELL_KEY_SOURCE_LINK,-1,-1},{0x202,0,XZ_SHRT_ASSIGNED,-1,-1,-1},
 {0x420c,0,XZ_SHRT_ON_PRESS,XZ_SHELL_KEY_TAG_TRACK,-1,-1},
 {0x4208,0,XZ_SHRT_ON_RELEASE,XZ_SHELL_KEY_BACK,XZ_SHELL_KEY_BACK_HOLD,-1},
 {0x430f,1,XZ_SHRT_ON_PRESS,XZ_SHELL_KEY_LOAD,-1,0},{0x430f,2,XZ_SHRT_ON_PRESS,XZ_SHELL_KEY_LOAD,-1,1},
 /* TIME MODE and QUANTIZE change deck state the shell reads back. */
 {0x430b,0,XZ_SHRT_STOCK,-1,-1,-1},{0x430c,0,XZ_SHRT_STOCK,-1,-1,-1},
};
const struct xz_shrt_key_map *xz_shrt_key_lookup(uint16_t code, unsigned channel) {
 for(size_t i=0;i<sizeof keys/sizeof *keys;i++)
  if(keys[i].code==code&&(!keys[i].channel||keys[i].channel==channel))return &keys[i];
 return NULL;
}
enum xz_shrt_phase xz_shrt_phase_of(unsigned op) { return op==0?XZ_SHRT_PRESS:op==1?XZ_SHRT_HOLD:XZ_SHRT_RELEASE; }
int xz_shrt_key_reclaims(const struct xz_shrt_key_map *k) {
 return k&&k->rule==XZ_SHRT_ON_PRESS&&(k->key==XZ_SHELL_KEY_SOURCE_USB1||k->key==XZ_SHELL_KEY_SOURCE_USB2||
  k->key==XZ_SHELL_KEY_SOURCE_LINK||k->key==XZ_SHELL_KEY_SOURCE_COMPUTER);
}

/* Runs one key on a copy of the shell. Only sh_key runs for these keys, and it
   writes nothing but the shell struct, so the copy keeps the probe side-effect free. */
struct probe { int effect, hands_off; };
static struct probe probe(const struct xz_shell *u, const struct xz_shell_model *m, int key) {
 struct probe p={0,0};
 if(key<0)return p;
 struct xz_shell copy;memcpy(&copy,u,sizeof copy);
 struct xz_shell_event e={XZ_SHELL_KEY,0,0,0,(enum xz_shell_key)key,-1};
 struct xz_shell_action out[XZ_SHELL_ACTIONS];
 size_t n=xz_shell_input(&copy,m,&e,out);
 for(size_t i=0;i<n;i++)if(out[i].kind==XZ_SHELL_ACT_HAND_OFF)p.hands_off=1;
 p.effect=n>0||memcmp(&copy,u,sizeof copy)!=0;
 return p;
}
enum xz_shrt_verdict xz_shrt_key_verdict(const struct xz_shell *u, const struct xz_shell_model *m, const struct xz_shrt_key_map *k) {
 if(!u||!m||!k||k->rule==XZ_SHRT_STOCK)return XZ_SHRT_PASS;
 /* An ASSIGNED key without its assignment behaves as its stock action. */
 if(k->rule==XZ_SHRT_HAND_OFF||k->rule==XZ_SHRT_ASSIGNED)return XZ_SHRT_PASS_HAND_OFF;
 /* While the shell owns the screen, a browse-section key either acts in the shell (SWALLOW), is
    inert (SWALLOW too) or hands the screen to stock visibly (PASS_HAND_OFF). It is never passed to a
    hidden stock screen: stock would act on its own invisible browse cursor (LOAD would load a track
    the shell is not showing, BACK or TAG TRACK would change state behind the shell). */
 if(k->key==XZ_SHELL_KEY_LOAD)return XZ_SHRT_SWALLOW; /* loads the shell's highlighted track or nothing */
 /* BACK never hands off (its table entries are POP, HOME or NONE) and a list screen's BACK runs
    screen input with state outside the shell struct, so it is not probed. */
 if(k->key==XZ_SHELL_KEY_BACK)return XZ_SHRT_SWALLOW;
 /* MENU belongs to the shell on every screen: short does the screen's own MENU job (a list's MENU panel,
    closing a popup or UTILITY) or asks for MODS where it has none (ui/menu_key.h), held opens UTILITY. Stock
    would act on its hidden screen, so it is never passed. */
 if(k->key==XZ_SHELL_KEY_MENU)return XZ_SHRT_SWALLOW;
 struct probe a=probe(u,m,k->key),b=k->rule==XZ_SHRT_ON_RELEASE?probe(u,m,k->hold):(struct probe){0,0};
 /* TAG TRACK the shell cannot do: on the bare PLAY screen stock tags the loaded track, so the key goes
    there with a visible hand-off. On a list stock would tag its own hidden cursor row: swallowed, and the
    runtime says why. */
 if(k->key==XZ_SHELL_KEY_TAG_TRACK&&!a.effect)
  return u->depth==1&&xz_shell_screen(u)==XZ_SHELL_PLAY&&u->popup.kind==XZ_SHELL_POPUP_NONE?XZ_SHRT_PASS_HAND_OFF:XZ_SHRT_SWALLOW;
 if(!a.effect&&!b.effect)return XZ_SHRT_SWALLOW;
 if((a.hands_off||!a.effect)&&(b.hands_off||!b.effect))return XZ_SHRT_PASS_HAND_OFF;
 return XZ_SHRT_SWALLOW;
}
enum xz_shrt_verdict xz_shrt_rekordbox_verdict(int action, enum xz_shrt_rb_effect *effect) {
 enum xz_shrt_rb_effect e=XZ_SHRT_RB_NONE;enum xz_shrt_verdict v;
 switch(action){
 case XZ_REKORDBOX_MODS:e=XZ_SHRT_RB_SHELL_MODS;v=XZ_SHRT_SWALLOW;break;
 case XZ_REKORDBOX_STEMS:e=XZ_SHRT_RB_STEMS;v=XZ_SHRT_SWALLOW;break;
 /* INTERFACE, VJ.TOOLS SCREEN and NOTHING run once in ui_runtime.c's REKORDBOX handler hook, which
    never calls stock for them: the key passes without a hand-off. */
 case XZ_REKORDBOX_INTERFACE:case XZ_REKORDBOX_VJ_SCREEN:case XZ_REKORDBOX_NOTHING:v=XZ_SHRT_PASS;break;
 default:v=XZ_SHRT_PASS_HAND_OFF;break; /* STOCK, and anything unknown: stock's own action, seen */
 }
 if(effect)*effect=e;
 return v;
}
int xz_shrt_key_for_phase(const struct xz_shrt_key_map *k, enum xz_shrt_phase p, int held) {
 if(!k||k->rule==XZ_SHRT_STOCK||k->rule==XZ_SHRT_HAND_OFF||k->rule==XZ_SHRT_ASSIGNED)return -1;
 if(k->rule==XZ_SHRT_ON_PRESS)return p==XZ_SHRT_PRESS?k->key:-1;
 if(p==XZ_SHRT_HOLD)return held?-1:k->hold;
 if(p==XZ_SHRT_RELEASE)return held?-1:k->key;
 return -1;
}

void xz_shrt_contact_press(struct xz_shrt_contacts *c, uint16_t code, unsigned channel, int swallow, uint32_t now) {
 int slot=-1;
 for(int i=0;i<XZ_SHRT_CONTACTS;i++){
  if(c->c[i].used&&c->c[i].code==code&&c->c[i].channel==channel){slot=i;break;}
  if(!c->c[i].used&&slot<0)slot=i;
 }
 if(slot<0)slot=0; /* more than eight keys held: reuse the oldest slot */
 c->c[slot].code=code;c->c[slot].channel=(uint8_t)channel;c->c[slot].used=1;
 c->c[slot].swallow=(uint8_t)!!swallow;c->c[slot].held=0;c->c[slot].at=now;
}
uint32_t xz_shrt_hold_ms(uint16_t code) { return code==0x209||code==0x20b?330u:code==0x4208?500u:0u; }
const struct xz_shrt_key_map *xz_shrt_contact_due(struct xz_shrt_contacts *c, uint32_t now) {
 for(int i=0;i<XZ_SHRT_CONTACTS;i++){
  if(!c->c[i].used||!c->c[i].swallow||c->c[i].held)continue;
  uint32_t hold=xz_shrt_hold_ms(c->c[i].code);
  const struct xz_shrt_key_map *k=xz_shrt_key_lookup(c->c[i].code,c->c[i].channel);
  if(!hold||!k||k->rule!=XZ_SHRT_ON_RELEASE||k->hold<0||(uint32_t)(now-c->c[i].at)<hold)continue;
  c->c[i].held=1;
  return k;
 }
 return NULL;
}
int xz_shrt_load_duplicate(const struct xz_shrt_contacts *c,const struct xz_shrt_key_map *map,enum xz_shrt_phase phase) {
 if(!c||!map||map->key!=XZ_SHELL_KEY_LOAD||phase!=XZ_SHRT_PRESS)return -1;
 for(unsigned i=0;i<XZ_SHRT_CONTACTS;i++)if(c->c[i].used&&c->c[i].code==map->code&&c->c[i].channel==map->channel)return c->c[i].swallow;
 return -1;
}
int xz_shrt_contact_follow(struct xz_shrt_contacts *c, uint16_t code, unsigned channel, enum xz_shrt_phase p, int *held) {
 if(held)*held=0;
 for(int i=0;i<XZ_SHRT_CONTACTS;i++){
  if(!c->c[i].used||c->c[i].code!=code||c->c[i].channel!=channel)continue;
  int swallow=c->c[i].swallow;
  if(held)*held=c->c[i].held;
  if(p==XZ_SHRT_HOLD)c->c[i].held=1;
  else if(p==XZ_SHRT_RELEASE)c->c[i].used=0;
  return swallow;
 }
 return -1;
}

struct xz_shrt_touch_step xz_shrt_touch_route(struct xz_shrt_touch *t, int owns, int x, int y, int down, int stock_down) {
 struct xz_shrt_touch_step s={0,0,-1,t->x,t->y};
 /* Native releases may clear the coordinates; the release keeps the last ones. */
 if(down||x||y){t->x=x;t->y=y;s.x=x;s.y=y;}
 switch(t->owner){
 case XZ_SHRT_T_NONE:
  if(!down){s.to_stock=!owns;return s;}
  if(!owns){t->owner=XZ_SHRT_T_STOCK;s.to_stock=1;return s;}
  if(stock_down){t->owner=XZ_SHRT_T_SWALLOW;s.release_stock=1;return s;}
  t->owner=XZ_SHRT_T_SHELL;s.event=XZ_SHELL_TOUCH_DOWN;return s;
 case XZ_SHRT_T_SHELL:
  s.event=down?XZ_SHELL_TOUCH_MOVE:XZ_SHELL_TOUCH_UP;
  if(!down)t->owner=XZ_SHRT_T_NONE;
  return s;
 case XZ_SHRT_T_STOCK:
  /* The shell took the screen mid-gesture: end the stock gesture now and keep the rest. */
  if(owns&&down){s.release_stock=stock_down;t->owner=XZ_SHRT_T_SWALLOW;return s;}
  s.to_stock=1;if(!down)t->owner=XZ_SHRT_T_NONE;
  return s;
 case XZ_SHRT_T_SWALLOW:
  if(!down)t->owner=XZ_SHRT_T_NONE;
  return s;
 }
 return s;
}

int xz_shrt_push_event(struct xz_shrt_push *p, int phase) {
 switch((enum xz_push_phase)phase){
 case XZ_PUSH_DOWN:p->down=1;p->held=0;return -1;
 case XZ_PUSH_HOLD:
  if(p->held)return -1;
  p->down=1;p->held=1;return XZ_SHELL_ENCODER_HOLD;
 case XZ_PUSH_UP:case XZ_PUSH_UP_HELD:{
  int click=phase==XZ_PUSH_UP&&!p->held;
  p->down=p->held=0;
  return click?XZ_SHELL_ENCODER_PUSH:-1;
 }
 default:return -1;
 }
}

void xz_shrt_handoff_begin(struct xz_shrt_handoff *h, uint32_t now) {
 if(!h->handed)h->count++;
 h->handed=1;h->left_play=0;h->since=now;h->play_since=0;
}
void xz_shrt_handoff_end(struct xz_shrt_handoff *h) {
 if(h->handed)h->resumes++;
 h->handed=0;h->left_play=0;h->play_since=0;
}
int xz_shrt_handoff_step(struct xz_shrt_handoff *h, uint32_t now, int play) {
 if(!h->handed)return 0;
 if(play<0){h->play_since=0;return 0;}
 if(!play){h->left_play=1;h->play_since=0;return 0;}
 if(!h->play_since)h->play_since=now?now:1;
 if((uint32_t)(now-h->play_since)<XZ_SHRT_RESUME_STABLE_MS)return 0;
 if(!h->left_play&&(uint32_t)(now-h->since)<XZ_SHRT_RESUME_GRACE_MS)return 0;
 xz_shrt_handoff_end(h);return 1;
}
void xz_shrt_handoff_caution(struct xz_shrt_handoff *h, uint32_t caution) { h->caution=caution!=0; }
void xz_shrt_handoff_shorten(struct xz_shrt_handoff *h, uint32_t now, uint32_t remaining) {
 if(!h->handed||h->left_play||remaining>=XZ_SHRT_RESUME_GRACE_MS)return;
 uint32_t since=now-(XZ_SHRT_RESUME_GRACE_MS-remaining);
 if((int32_t)(since-h->since)<0)h->since=since;
}
uint32_t xz_shrt_caution_filter(struct xz_shrt_caution *c, uint32_t caution) {
 uint32_t v;
 if(caution!=UINT32_MAX){c->unknown_run=0;c->last=caution;v=caution;}
 else{
  if(c->unknown_run<XZ_SHRT_CAUTION_RUNS)c->unknown_run++;
  v=c->unknown_run>=XZ_SHRT_CAUTION_RUNS?UINT32_MAX:c->last;
 }
 if(v!=c->ack)c->ack=0; /* a new or cleared value ends the acknowledgement */
 c->shown=v;
 return v&&v==c->ack?0:v;
}
void xz_shrt_caution_ack(struct xz_shrt_caution *c) { if(c->shown)c->ack=c->shown; }
int xz_shrt_handoff_yields(const struct xz_shrt_handoff *h) { return h->handed||h->caution; }

int xz_shrt_queue_push(struct xz_shrt_queue *q, const struct xz_shrt_command *c) {
 if(c->kind==XZ_SHRT_CMD_LOAD&&(!c->media_generation||(c->device_kind!=2&&c->device_kind!=3))){q->dropped++;return 0;}
 for(unsigned i=0;i<q->count;i++){
  struct xz_shrt_command *e=&q->c[(q->head+i)%XZ_SHRT_QUEUE];
  if(e->kind==c->kind&&e->deck==c->deck&&c->kind!=XZ_SHRT_CMD_LOAD){*e=*c;q->coalesced++;return 1;}
 }
 if(q->count>=XZ_SHRT_QUEUE){q->dropped++;return 0;}
 q->c[(q->head+q->count)%XZ_SHRT_QUEUE]=*c;q->count++;
 return 1;
}
int xz_shrt_queue_pop(struct xz_shrt_queue *q, struct xz_shrt_command *out) {
 if(!q->count)return 0;
 *out=q->c[q->head];q->head=(q->head+1)%XZ_SHRT_QUEUE;q->count--;
 return 1;
}
int xz_shrt_load_generation_current(const struct xz_shrt_command *c,uint32_t observed) {
 return c&&c->kind==XZ_SHRT_CMD_LOAD&&(c->device_kind==2||c->device_kind==3)&&
        c->media_generation&&observed&&c->media_generation==observed;
}
unsigned xz_shrt_queue_drop_source(struct xz_shrt_queue *q,uint8_t device_kind) {
 unsigned kept=0,before=q->count;
 for(unsigned i=0;i<before;i++){
  struct xz_shrt_command c=q->c[(q->head+i)%XZ_SHRT_QUEUE];
  if(c.kind==XZ_SHRT_CMD_LOAD&&c.device_kind==device_kind)continue;
  q->c[(q->head+kept++)%XZ_SHRT_QUEUE]=c;
 }
 q->count=kept;q->dropped+=before-kept;return before-kept;
}

const char *const xz_shrt_quantize_choices[4]={"1/8","1/4","1/2","1"};
int xz_shrt_quantize_index(int den) { return den==8?0:den==4?1:den==2?2:den==1?3:-1; }
int xz_shrt_shell_owned_setting(int id) {
 return id==XZ_SHELL_SET_WAVE_COLOR||id==XZ_SHELL_SET_WAVE_POSITION||id==XZ_SHELL_ITEM_KEY_DISPLAY||id==XZ_SHELL_ITEM_WAVE_ZOOM
  ||id==XZ_SHELL_ITEM_MODE_LAMPS||id==XZ_SHELL_ITEM_QUANTIZE_LAMP||id==XZ_SHELL_ITEM_WAVE_TITLES||id==XZ_SHELL_ITEM_BYPASS_BUTTON||id==XZ_SHELL_ITEM_USB_COLORS;
}
/* Legacy deck settings only. Stock UTILITY ids use xz_shut_reason and the guarded
   XZ_SHRT_CMD_UTILITY path before this fallback, including NEEDLE LOCK. */
int xz_shrt_setting_writable(int id, int time_mode, int quantize_beat) {
 return id==XZ_SHELL_SET_TIME_MODE?!!time_mode:id==XZ_SHELL_ITEM_QUANTIZE_BEAT?!!quantize_beat:0;
}
int xz_shrt_setting_command(const struct xz_shell_action *a, uint32_t now, struct xz_shrt_command *out) {
 if(!a||!out||a->kind!=XZ_SHELL_ACT_SET_SETTING)return 0;
 memset(out,0,sizeof *out);out->at_ms=now;
 int deck=a->deck>=0&&a->deck<XZ_SHELL_NATIVE_DECKS?a->deck:-1;
 switch(a->id){
 case XZ_SHELL_SET_TIME_MODE: /* UiSetKeyTimeMode(channel, remain) */
  if(a->value!=XZ_SHELL_TIME_REMAIN&&a->value!=XZ_SHELL_TIME_ELAPSED)return 0;
  out->kind=XZ_SHRT_CMD_TIME_MODE;out->deck=deck;out->value=a->value==XZ_SHELL_TIME_REMAIN;return 1;
 case XZ_SHELL_ITEM_QUANTIZE_BEAT: /* UiSetUtilQuantizeValue: 0 = 1 beat .. 3 = 1/8; choices run 1/8 .. 1 */
  if(a->value<0||a->value>3)return 0;
  out->kind=XZ_SHRT_CMD_QUANTIZE_BEAT;out->deck=-1;out->value=3-a->value;return 1;
 default:
  /* Other ids do not use this legacy command path. UTILITY ids are dispatched first. */
  return 0;
 }
}

uint32_t xz_shrt_frame_period_ms(enum xz_shell_screen s, int any_moving) { return s==XZ_SHELL_PLAY&&any_moving?XZ_SHRT_FRAME_LIVE_MS:XZ_SHRT_FRAME_IDLE_MS; }
struct xz_shell_ableton xz_shrt_ableton(const struct xz_link_glance *g) {
 struct xz_shell_ableton a={0};
 if(!g)return a;
 a.known=1;a.enabled=!!g->enabled;
 switch(g->state){
 case XZ_LINK_RUN_OFF:a.state=XZ_SHELL_ABLETON_OFF;break;
 case XZ_LINK_RUN_STARTING:a.state=XZ_SHELL_ABLETON_STARTING;break;
 case XZ_LINK_RUN_ENABLED:case XZ_LINK_RUN_NO_MASTER:a.state=XZ_SHELL_ABLETON_ON;a.peers=g->peers;break;
 case XZ_LINK_RUN_NO_HELPER:case XZ_LINK_RUN_FAILED:a.state=XZ_SHELL_ABLETON_FAILED;break;
 default:a.known=0;a.enabled=0;break; /* a state this build does not know draws nothing */
 }
 return a;
}
unsigned xz_shrt_moving_decks(const struct xz_shell_model *m) {
 unsigned bits=0;int count=m->deck_count==4?4:2;
 for(int i=0;i<count;i++){
  const struct xz_shell_deck *d=&m->deck[i];
  if(!(d->known&XZ_SHELL_KNOWN(XZ_SHELL_F_LOADED))||!d->loaded)continue;
  if(d->known&XZ_SHELL_KNOWN(XZ_SHELL_F_MOTION)){
   if(d->moving)bits|=1u<<i;
  }else if((d->known&XZ_SHELL_KNOWN(XZ_SHELL_F_PLAYING))&&d->playing)bits|=1u<<i;
 }
 return bits;
}

int xz_shrt_motion_step(struct xz_shrt_motion *m, int32_t elapsed_ms, uint32_t read_at) {
 if(!m->valid){m->valid=1;m->last_ms=elapsed_ms;m->changed_at=read_at-XZ_SHRT_MOTION_MS;return 0;} /* one reading proves nothing */
 if(elapsed_ms!=m->last_ms){m->last_ms=elapsed_ms;m->changed_at=read_at;}
 return (uint32_t)(read_at-m->changed_at)<XZ_SHRT_MOTION_MS;
}

void xz_shrt_position_reset(struct xz_shrt_position *p) { memset(p,0,sizeof *p); }
int32_t xz_shrt_rate_from_tempo(int32_t tempo_x100) { return 1000000+tempo_x100*100; }
static int position_step_us(struct xz_shrt_position *p, int playing, int64_t elapsed_us, uint32_t read_at,
                          int32_t rate, int measure, int32_t *out_ms, uint32_t *out_at) {
 int32_t elapsed_ms=(int32_t)(elapsed_us/1000);
 int64_t rate_ppm=rate<0?0:rate;
 int fresh=read_at!=p->read_at||elapsed_ms!=p->read_ms;
 /* Observed speed from two readings of a precise source. */
 if(fresh&&measure&&playing&&p->have_last&&read_at!=p->last_at){
  int32_t dt=(int32_t)(read_at-p->last_at);
  if(dt>0&&dt<=250){
   int64_t observed=((int64_t)elapsed_ms-p->last_ms)*1000000/dt;
   p->observed_ppm=observed>INT32_MAX?INT32_MAX:observed<INT32_MIN?INT32_MIN:(int32_t)observed;
   int64_t off=observed-rate_ppm;if(off<0)off=-off;
   int regular=off*1000000<=(int64_t)XZ_SHRT_RATE_TOLERANCE_PPM*rate_ppm;
   if(!regular){p->following=1;p->regular_run=0;}
   else if(p->following&&++p->regular_run>=XZ_SHRT_REGULAR_RUNS){
    p->following=0;p->regular_run=0;p->base_us=elapsed_us;p->base_at=read_at;
    p->read_at=read_at;p->read_ms=elapsed_ms;p->last_ms=elapsed_ms;p->last_at=read_at;
    *out_ms=elapsed_ms;*out_at=read_at;return 0;
   }
  }
 }
 if(fresh&&measure){p->have_last=1;p->last_ms=elapsed_ms;p->last_at=read_at;}
 if(!measure||!playing){p->following=0;p->regular_run=0;}
 if(p->following){
  p->valid=1;p->base_us=elapsed_us;p->base_at=read_at;p->read_at=read_at;p->read_ms=elapsed_ms;
  *out_ms=elapsed_ms;*out_at=0;return 1;
 }
 if(!p->valid||!playing){
  p->valid=1;p->base_us=elapsed_us;p->base_at=read_at;
 }else if(fresh){
  /* A repeated reading (a held snapshot) carries no information and changes nothing. */
  int64_t predicted=p->base_us+(int64_t)(int32_t)(read_at-p->base_at)*rate_ppm/1000;
  int64_t error=elapsed_us-predicted;
  if(error>XZ_SHRT_POSITION_SNAP_MS*1000||error< -XZ_SHRT_POSITION_SNAP_MS*1000){p->base_us=elapsed_us;}
  else{
   int64_t nudge=error/8;
   if(nudge>XZ_SHRT_POSITION_NUDGE_MS*1000)nudge=XZ_SHRT_POSITION_NUDGE_MS*1000;
   if(nudge< -XZ_SHRT_POSITION_NUDGE_MS*1000)nudge=-XZ_SHRT_POSITION_NUDGE_MS*1000;
   p->base_us=predicted+nudge;
  }
  p->base_at=read_at;
 }
 p->read_at=read_at;p->read_ms=elapsed_ms;
 int64_t ms=(p->base_us+(p->base_us<0?-500:500))/1000;
 *out_ms=ms<INT32_MIN?INT32_MIN:ms>INT32_MAX?INT32_MAX:(int32_t)ms;*out_at=p->base_at?p->base_at:1;
 return 0;
}

int xz_shrt_position_step(struct xz_shrt_position *p,int playing,int32_t ms,uint32_t at,int32_t rate,int measure,int32_t *out_ms,uint32_t *out_at){
 return position_step_us(p,playing,(int64_t)ms*1000,at,rate,measure,out_ms,out_at);
}

int xz_shrt_native_position_step_us(struct xz_shrt_position *p,int playing,int64_t us,uint32_t at,
                                int32_t rate,int32_t *out_ms,uint32_t *out_at){
 int32_t ms=(int32_t)(us/1000);
 if(!playing||rate<=0){p->native_seen=0;p->native_count=0;return position_step_us(p,0,us,at,rate,0,out_ms,out_at);}
 if(p->native_seen&&(int32_t)(at-p->native_poll_at)<=0){
  *out_ms=(int32_t)((p->base_us+(p->base_us<0?-500:500))/1000);
  *out_at=p->following?0:(p->base_at?p->base_at:1);return p->following;
 }
 if(!p->native_seen)p->valid=0;
 int changed=!p->native_seen||us!=p->native_us;
 int rate_changed=p->native_seen&&p->native_rate!=rate;
 if(p->native_seen&&!rate_changed){
  int64_t jump=us-p->native_us-(int64_t)(uint32_t)(at-p->native_poll_at)*rate/1000;
  if(jump>XZ_SHRT_POSITION_SNAP_MS*1000||jump< -XZ_SHRT_POSITION_SNAP_MS*1000)p->display.valid=0;
 }
 int reverse=p->native_seen&&us<p->native_us;
 if(!p->native_seen||p->native_rate!=rate)p->native_count=p->native_head=0;
 if(changed)p->native_progress_at=at;
 if(reverse){p->display.valid=0;p->native_count=p->native_head=0;p->following=1;p->regular_run=0;}
 if(!p->native_seen||at!=p->native_poll_at){
  while(p->native_count&&(uint32_t)(at-p->native_sample[p->native_head].at)>500u){p->native_head=(p->native_head+1)%32;p->native_count--;}
  if(p->native_count==32){p->native_head=(p->native_head+1)%32;p->native_count--;}
  unsigned tail=(p->native_head+p->native_count++)%32;
  p->native_sample[tail].at=at;p->native_sample[tail].us=us;
 }
 if(p->native_count>=2&&(uint32_t)(at-p->native_sample[p->native_head].at)>=250u&&!reverse){
  double sx=0,sy=0,sxx=0,sxy=0;uint32_t origin=p->native_sample[p->native_head].at;
  int64_t source=p->native_sample[p->native_head].us;
  for(unsigned i=0;i<p->native_count;i++){
   unsigned j=(p->native_head+i)%32;double x=(uint32_t)(p->native_sample[j].at-origin),y=(double)p->native_sample[j].us-source;
   sx+=x;sy+=y;sxx+=x*x;sxy+=x*y;
  }
  double n=p->native_count,den=n*sxx-sx*sx;
  double observed=den>0?(n*sxy-sx*sy)*1000./den:0;
  p->observed_ppm=observed>INT32_MAX?INT32_MAX:observed<INT32_MIN?INT32_MIN:(int32_t)observed;
  int regular=fabs(observed-rate)*1000000.<=XZ_SHRT_RATE_TOLERANCE_PPM*(double)rate;
  if(!regular){p->following=1;p->regular_run=0;}
  else if(p->following&&++p->regular_run>=XZ_SHRT_REGULAR_RUNS){p->following=0;p->regular_run=0;p->valid=0;}
 }
 p->native_seen=1;p->native_poll_at=at;p->native_us=us;p->native_rate=rate;
 if((uint32_t)(at-p->native_progress_at)>XZ_SHRT_NATIVE_PROGRESS_MS){p->following=1;p->regular_run=0;}
 if(p->following){
  p->valid=1;p->base_us=us;p->base_at=at;p->read_at=at;p->read_ms=ms;
  *out_ms=ms;*out_at=0;return 1;
 }
 if(changed||!p->valid)return position_step_us(p,1,us,at,rate,0,out_ms,out_at);
 int64_t base=(p->base_us+(p->base_us<0?-500:500))/1000;
 *out_ms=(int32_t)base;*out_at=p->base_at?p->base_at:1;return 0;
}

/* Preserve the clock estimate across a tempo step. A previously displayed forecast
   cannot be undone; recover its phase against actual forward source progress.
   This does not modify source observations or extend either prediction deadline. */
int64_t xz_shrt_native_display_step(struct xz_shrt_position *p,uint32_t target){
 uint32_t age=p->native_poll_at-p->native_progress_at;
 uint32_t limit=age<XZ_SHRT_NATIVE_PROGRESS_MS?XZ_SHRT_NATIVE_PROGRESS_MS-age:0;
 if(limit>XZ_SHELL_TIMELINE_EXTRAPOLATE_MS)limit=XZ_SHELL_TIMELINE_EXTRAPOLATE_MS;
 int32_t ahead=(int32_t)(target-p->native_poll_at);
 if(ahead<0)ahead=0;if((uint32_t)ahead>limit)ahead=(int32_t)limit;
 target=p->native_poll_at+(uint32_t)ahead;
 int32_t display_rate=p->following?0:p->native_rate;
 int64_t raw=p->base_us+(int64_t)(int32_t)(target-p->base_at)*display_rate/1000;
 if(!p->display.valid){p->display.valid=1;p->display.phase_us=0;}
 else {
  int32_t dt=(int32_t)(target-p->display.at);
  if(dt<=0)return p->display.shown_us;
  if(display_rate!=p->display.rate)
   p->display.phase_us=p->display.shown_us+(int64_t)dt*display_rate/1000-raw;
  else {
   int64_t progress=raw-p->display.raw_us;
   if(progress>0){
    if(p->display.phase_us>0)p->display.phase_us-=progress<p->display.phase_us?progress:p->display.phase_us;
    else if(p->display.phase_us<0)p->display.phase_us+=progress< -p->display.phase_us?progress: -p->display.phase_us;
   }
  }
 }
 p->display.at=target;p->display.rate=display_rate;p->display.raw_us=raw;
 p->display.shown_us=raw+p->display.phase_us;return p->display.shown_us;
}

void xz_shrt_frame_mark(struct xz_shrt_frames *f, uint64_t now_us) {
 if(f->last_us&&now_us>f->last_us){
  uint64_t gap=now_us-f->last_us;
  if(gap<1000000u){
   if(gap>f->gap_max_us)f->gap_max_us=(uint32_t)gap;
   if(gap>XZ_SHRT_LATE_US)f->late++;
   f->hist[gap<14000u?0:gap<18000u?1:gap<26000u?2:gap<34000u?3:4]++;
  }
 }
 f->last_us=now_us;f->frames++;
}

/* ---- DECK 3 / DECK 4 ---- */
_Static_assert(sizeof(struct xz_extra_column)==3&&sizeof(struct xz_shell_column)==3,"three bytes a column");
_Static_assert(XZ_EXTRA_HOT_CUES==XZ_SHELL_HOT_CUES,"one hot cue per pad");
#define EK(f) (s->known&XZ_EXTRA_KNOWN(XZ_EXTRA_F_##f))
#define SK(f) XZ_SHELL_KNOWN(XZ_SHELL_F_##f)
/* The loop length in beats at the track's BPM: whole beats, or 1/2..1/32. 0 when the BPM is unknown. */
static void loop_beats(struct xz_shell_loop *l, int32_t ms, int32_t bpm_x100) {
 l->beats_num=l->beats_den=0;
 if(ms<=0||bpm_x100<=0)return;
 int64_t milli=(int64_t)ms*bpm_x100/6000;       /* beats x 1000 */
 if(milli>=750){l->beats_num=(int)((milli+500)/1000);l->beats_den=1;return;}
 int den=2;
 while(den<32&&milli*den<750)den*=2;
 l->beats_num=1;l->beats_den=den;
}
static void map_deck(struct xz_shell_deck *d, const struct xz_extra_deck *s, const struct xz_shrt_extra_parts *p,
                     uint32_t model_now, uint32_t mono_now) {
 memset(d,0,sizeof *d);
 d->known=SK(LOADED);d->loaded=!!s->loaded;
 if(EK(TITLE)){d->title=s->title;d->known|=SK(TITLE);}
 if(EK(ARTIST)&&s->artist[0]){d->artist=s->artist;d->known|=SK(ARTIST);}
 if(EK(SOURCE)&&(s->source==1||s->source==2)){d->source=s->source==1?XZ_SHELL_SRC_USB1:XZ_SHELL_SRC_USB2;d->known|=SK(SOURCE);}
 if(EK(ELAPSED)){
  d->elapsed_ms=s->elapsed_ms;d->known|=SK(ELAPSED);
  uint32_t age=mono_now-s->elapsed_at_ms;
  if(s->elapsed_at_ms&&(int32_t)age>=0&&age<60000u){d->elapsed_at_ms=model_now-age;if(!d->elapsed_at_ms)d->elapsed_at_ms=1;}
 }
 if(EK(TIMELINE)){
  const struct xz_extra_timeline *t=&s->timeline;
  d->elapsed_at_ms=0; /* unknown observed rate must not fall back to the nominal fader */
  if(t->flags&XZ_EXTRA_TIMELINE_MOTION){d->moving=!!t->moving;d->known|=SK(MOTION);}
  double projected;
  if(xz_extra_project_ms_precise(s,mono_now,&projected)){
   d->elapsed_ms=(int32_t)projected;d->elapsed_fraction_ms=projected-d->elapsed_ms;d->known|=SK(ELAPSED);
   uint32_t age=mono_now-t->at_ms;d->playback_projection_age_ms=age>50u?50u:age;
   /* This model is already projected to mono_now, including the fractional tag. */
   if(t->flags&XZ_EXTRA_TIMELINE_RATE){
    d->elapsed_at_ms=model_now?model_now:1;
    d->playback_rate_ppm=t->rate_ppm;d->known|=SK(PLAYBACK_RATE);
   }
  }
 }
 if(EK(TOTAL)){d->total_ms=s->total_ms;d->known|=SK(TOTAL);}
 if(EK(PLAYING)){d->playing=!!s->playing;d->known|=SK(PLAYING);}
 d->needle_locked=s->held_playing&&!s->platter_contact;
 if(EK(MEMORY_CUES)){
  _Static_assert(XZ_EXTRA_MEMORY_CUES==XZ_SHELL_MEMORY_CUES,"cue model capacity");
  d->memory_count=s->memory_count>XZ_SHELL_MEMORY_CUES?XZ_SHELL_MEMORY_CUES:(int)s->memory_count;
  for(int i=0;i<d->memory_count;i++)d->memory_cue[i]=(struct xz_shell_cue){s->memory_cue[i].set,s->memory_cue[i].ms,s->memory_cue[i].rgb,s->memory_cue[i].end_ms>s->memory_cue[i].ms};
  d->known|=SK(MEMORY_CUES);
 }
 if(EK(BPM)){
  d->bpm_x100=s->bpm_x100;d->known|=SK(BPM);
  if(s->track_bpm_x100>0){d->track_bpm_x100=s->track_bpm_x100;d->known|=SK(TRACK_BPM);}
 }
 if(EK(TEMPO)){d->tempo_x100=s->tempo_x100;d->known|=SK(TEMPO);}
 if(EK(TEMPO_RANGE)){d->tempo_range=s->tempo_range;d->known|=SK(TEMPO_RANGE);}
 if(EK(MASTER_TEMPO)){d->master_tempo=!!s->master_tempo;d->known|=SK(MASTER_TEMPO);}
 if(EK(VINYL)){d->vinyl=!!s->vinyl;d->known|=SK(VINYL);}
 if(EK(QUANTIZE)){
  unsigned den=s->quantize_den;int valid=den==1||den==2||den==4||den==8;
  d->quantize_on=!!s->quantize;d->quantize_num=valid?1:0;d->quantize_den=valid?(int)den:0;d->known|=SK(QUANTIZE);
 }
 if(EK(TIME_MODE)){d->time_mode=s->remain?XZ_SHELL_TIME_REMAIN:XZ_SHELL_TIME_ELAPSED;d->known|=SK(TIME_MODE);}
 if(EK(MASTER)){d->master=!!s->master;d->master_scope=s->master_scope;d->known|=SK(MASTER);}
 if(EK(SYNC)){
  struct xz_extra_sync_presentation sync=xz_extra_sync_present(s);
  d->sync=!!s->sync.requested;d->sync_master_deck=sync.master_deck;d->sync_status=sync.status;
  d->known|=SK(SYNC)|SK(SYNC_STATUS);
 }
 if(EK(KEY)&&s->key[0]){d->key=EK(KEY_STATE)&&s->key_state.playing_valid&&s->playing_key[0]?s->playing_key:s->key;d->known|=SK(KEY);}
 if(EK(KEY_STATE)){
  d->key_proof=s->key_state.playing_valid?1:-1;d->known|=SK(KEY_PROOF);
  d->key_synced=s->key_state.engaged;d->key_shift=s->key_state.desired_cents/100;
  d->known|=SK(KEY_SYNC)|SK(KEY_SHIFT);
  if(s->key_state.playing_valid){
   d->key_original=s->key;d->key_cents=s->key_state.playing.cents;d->known|=SK(KEY_ORIGINAL);
  }
 }
 if(EK(CUE)){d->cue_ms=s->cue_ms;d->known|=SK(CUE_POINT);}
 if(EK(HOT_CUES)){
  for(int i=0;i<XZ_SHELL_HOT_CUES;i++)
   d->hot_cue[i]=(struct xz_shell_cue){!!s->hot_cue[i].set,s->hot_cue[i].ms,s->hot_cue[i].rgb,s->hot_cue[i].end_ms>s->hot_cue[i].ms};
  d->known|=SK(HOT_CUES);
 }
 if(EK(LOOP)){
  d->loop.active=!!s->loop.active;d->loop.in_ms=s->loop.in_ms;d->loop.out_ms=s->loop.out_ms;
  if(EK(BPM))loop_beats(&d->loop,s->loop.out_ms-s->loop.in_ms,s->track_bpm_x100);
  d->known|=SK(LOOP);
  /* LOOP IN / OUT ADJUST: the edge the jog moves, drawn as on the stock decks (shell.h XZ_SHELL_F_LOOP_EDIT). */
  if(s->loop.active&&(s->loop.edit==1||s->loop.edit==2)){
   d->loop_edit.endpoint=s->loop.edit==2;d->loop_edit.ms=s->loop.edit==2?s->loop.out_ms:s->loop.in_ms;d->known|=SK(LOOP_EDIT);
  }
 }
 if(EK(GRID)&&p->grid&&p->grid_n){d->grid=p->grid;d->grid_count=p->grid_n;d->known|=SK(GRID);}
 if(EK(WAVE)&&p->scroll&&p->scroll_n&&p->span_ms){
  d->wave=(struct xz_shell_wave){p->scroll,p->scroll_n,p->span_ms};d->known|=SK(WAVE);
  if(p->overview&&p->overview_n){d->overview=(struct xz_shell_wave){p->overview,p->overview_n,p->span_ms};d->known|=SK(OVERVIEW);}
 }
}
#undef EK
#undef SK
static void map_master(struct xz_shell_model *m,const struct xz_extra_decks *s,uint32_t now){
 if(!s||!s->master_intent.epoch)return;
 const struct xz_master_observation *a=&s->master_actual;
 int known=a->revision&&a->operator_epoch&&a->deck>=XZ_MASTER_NONE&&a->deck<=XZ_MASTER_DECK4&&
  (uint32_t)(now-a->observed_ms)<=XZ_MASTER_MAX_AGE_MS;
 if(a->deck>=XZ_MASTER_DECK3&&!s->route_up)known=0;
 for(int i=0;i<XZ_SHELL_DECKS;i++){
  struct xz_shell_deck *d=&m->deck[i];d->master=known&&a->deck==i;
  d->master_scope=d->master?(XZ_EXTRA_MASTER_SCOPE_NATIVE|(i>=XZ_EXTRA_FIRST_DECK?XZ_EXTRA_MASTER_SCOPE_SOFTWARE:0)):0;
  if(known)d->known|=XZ_SHELL_KNOWN(XZ_SHELL_F_MASTER);
  else d->known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_MASTER);
 }
}
void xz_shrt_extra_map(struct xz_shell_model *m, const struct xz_extra_decks *s, const struct xz_shrt_extra_parts parts[XZ_EXTRA_DECKS],
                       uint32_t model_now, uint32_t mono_now) {
 for(int i=0;i<XZ_EXTRA_DECKS;i++)memset(&m->deck[XZ_EXTRA_FIRST_DECK+i],0,sizeof m->deck[0]);
 m->sides_extra=0;
 if(!s||!s->route_up){m->deck_count=XZ_SHELL_NATIVE_DECKS;map_master(m,s,mono_now);return;}
 m->deck_count=XZ_SHELL_NATIVE_DECKS+XZ_EXTRA_DECKS;
 for(int i=0;i<XZ_EXTRA_DECKS;i++)map_deck(&m->deck[XZ_EXTRA_FIRST_DECK+i],&s->deck[i],&parts[i],model_now,mono_now);
 m->sides_extra=(uint8_t)((s->left_on_3?1u:0u)|(s->right_on_4?2u:0u));
 map_master(m,s,mono_now);
}
uint32_t xz_shrt_extra_caps(struct xz_shell_model *m, uint32_t caps, int native_load, int extra_load, int deck_select) {
 caps&=~(uint32_t)(XZ_SHELL_CAP_LOAD|XZ_SHELL_CAP_SEEK|XZ_SHELL_CAP_DECK_SELECT);
 for(int d=0;d<XZ_SHELL_DECKS;d++)m->deck[d].caps_off=0;
 if(m->deck_count!=XZ_SHELL_NATIVE_DECKS+XZ_EXTRA_DECKS){
  if(native_load)caps|=XZ_SHELL_CAP_LOAD;
  return m->capabilities=caps;
 }
 caps|=XZ_SHELL_CAP_SEEK;
 if(native_load||extra_load)caps|=XZ_SHELL_CAP_LOAD;
 if(deck_select)caps|=XZ_SHELL_CAP_DECK_SELECT;
 for(int d=0;d<XZ_SHELL_NATIVE_DECKS;d++)m->deck[d].caps_off=XZ_SHELL_CAP_SEEK|(native_load?0u:(uint32_t)XZ_SHELL_CAP_LOAD);
 for(int i=0;i<XZ_EXTRA_DECKS;i++)
  m->deck[XZ_EXTRA_FIRST_DECK+i].caps_off=(XZ_SHRT_DECK_CAPS&~(uint32_t)XZ_SHRT_EXTRA_CAPS)|(extra_load?0u:(uint32_t)XZ_SHELL_CAP_LOAD);
 return m->capabilities=caps;
}
enum xz_shrt_extra_route xz_shrt_extra_action(const struct xz_shell_model *m, const struct xz_shell_action *a, int browse_slot,
                                              struct xz_extra_deck_event *e) {
 switch(a->kind){
 case XZ_SHELL_ACT_SEEK:case XZ_SHELL_ACT_GRID_ADJUST:case XZ_SHELL_ACT_SET_SETTING:case XZ_SHELL_ACT_LOAD:
 case XZ_SHELL_ACT_TAG_TRACK:case XZ_SHELL_ACT_STEM_MUTE:case XZ_SHELL_ACT_STEM_GAIN:case XZ_SHELL_ACT_LOAD_CUE:
 case XZ_SHELL_ACT_GRID_EDIT:case XZ_SHELL_ACT_STEM_BYPASS:break;
 default:return XZ_SHRT_EXTRA_NONE;
 }
 int i=a->deck-XZ_EXTRA_FIRST_DECK;
 if(i<0||i>=XZ_EXTRA_DECKS)return XZ_SHRT_EXTRA_NONE;
 if(m->deck_count!=XZ_SHELL_NATIVE_DECKS+XZ_EXTRA_DECKS)return XZ_SHRT_EXTRA_REFUSE;
 uint32_t caps=m->capabilities&~m->deck[a->deck].caps_off;
 memset(e,0,sizeof *e);e->deck=i;e->pressed=1;
 if(a->kind==XZ_SHELL_ACT_LOAD&&(caps&XZ_SHELL_CAP_LOAD)&&a->id>0&&(browse_slot==0||browse_slot==1)){
  e->kind=XZ_EXTRA_IN_LOAD;e->load.source=browse_slot+1;e->load.track_id=(uint32_t)a->id;
  return XZ_SHRT_EXTRA_INPUT;
 }
 /* NEEDLE LOCK (stock default LOCK, manual p.63): touching the waveform moves a software deck only while it
    is paused or held on the platter in VINYL mode (the player reports neither as playing). */
 if(a->kind==XZ_SHELL_ACT_SEEK&&(caps&XZ_SHELL_CAP_SEEK)&&a->value>=0){
  if(m->deck[a->deck].needle_locked||m->deck[a->deck].playing)return XZ_SHRT_EXTRA_REFUSE;
  e->kind=XZ_EXTRA_IN_SEEK;e->seek_ms=a->value;return XZ_SHRT_EXTRA_INPUT;
 }
 return XZ_SHRT_EXTRA_REFUSE;
}
const char *xz_shrt_pair_notice(struct xz_shrt_pair_seen *seen, const struct xz_shell_model *m, int deck_view, uint32_t now_ms) {
 if(!seen||!m)return NULL;
 if(m->deck_count!=XZ_SHELL_DECKS){seen->valid=0;return NULL;}
 unsigned pair=m->sides_extra&3u;const char *text=NULL;
 if(seen->valid&&now_ms-seen->at_ms<1000u&&deck_view==XZ_LAYOUT_TWO)text=xz_deck_view_pair_notice(seen->pair,pair);
 seen->valid=1;seen->pair=pair;seen->at_ms=now_ms;
 return text;
}
unsigned xz_shrt_cable_step(struct xz_shrt_cable *c, const struct xz_shrt_cable_in *in) {
 if(!c||!in)return 0;
 if(!c->seen){memset(c,0,sizeof *c);c->seen=1;c->up=!!in->route_up;return 0;}
 unsigned act=0;
 if(in->route_up){
  c->dropping=0;
  if(!c->up){c->up=1;c->pending=1;c->layout_revision=in->layout_revision;}
 }else if(c->up){
  if(!c->dropping){c->dropping=1;c->dropped_at=in->now_ms;}
  else if(in->now_ms-c->dropped_at>=XZ_SHRT_CABLE_DROP_MS){c->up=0;c->dropping=0;c->pending=0;act|=XZ_SHRT_CABLE_BACK_TO_TWO;}
 }
 if(c->pending&&c->up&&!c->dropping){
  if(in->view!=XZ_LAYOUT_TWO||in->layout_revision!=c->layout_revision)c->pending=0;
  else if(in->ready){c->pending=0;act|=XZ_SHRT_CABLE_SHOW_FOUR;}
 }
 return act;
}
int xz_shrt_extra_notice(uint32_t *seen, const struct xz_extra_decks *s, uint32_t mono_now) {
 if(!s||s->notice_seq==*seen)return 0;
 *seen=s->notice_seq;
 return s->notice[0]&&mono_now-s->notice_at_ms<(uint32_t)XZ_SHELL_NOTICE_MS;
}
static void copy_text(char *out, size_t cap, const char *in) {
 size_t full=in?strlen(in):0,n=full<cap?full:cap-1;
 /* A cut never ends inside a UTF-8 sequence. */
 if(n<full)while(n>0&&((unsigned char)in[n]&0xc0u)==0x80u)n--;
 if(n)memcpy(out,in,n);
 out[n]=0;
}
int xz_shrt_extra_track(struct xz_extra_track *out, int source, uint32_t track_id, const char *root, const char *rel,
                        const char *title, const char *artist, const char *key, uint32_t bpm_x100) {
 memset(out,0,sizeof *out);
 out->source=source;out->track_id=track_id;
 size_t r=root?strlen(root):0,n=rel?strlen(rel):0;
 if(!n||r+n>=sizeof out->path)return -1;
 memcpy(out->path,root,r);memcpy(out->path+r,rel,n+1);
 copy_text(out->title,sizeof out->title,title);copy_text(out->artist,sizeof out->artist,artist);copy_text(out->key,sizeof out->key,key);
 out->track_bpm_x100=bpm_x100<=(uint32_t)INT32_MAX?(int32_t)bpm_x100:0;
 return 0;
}
uint32_t xz_shrt_band_peak(const uint8_t *bands, uint32_t count) {
 uint32_t histogram[256]={0};
 for(uint32_t i=0;i<count;i++){unsigned m=bands[i*3];if(bands[i*3+1]>m)m=bands[i*3+1];if(bands[i*3+2]>m)m=bands[i*3+2];histogram[m]++;}
 uint64_t target=((uint64_t)count*995+999)/1000,seen=0;
 for(unsigned v=0;v<256;v++){seen+=histogram[v];if(seen>=target)return v?v:1;}
 return 255;
}
uint32_t xz_shrt_bin_columns(const uint8_t *bands, uint32_t count, uint32_t peak, struct xz_shell_column *out, uint32_t bins) {
 uint32_t n=count<bins?count:bins;
 if(!peak)peak=1;
 for(uint32_t i=0;i<n;i++){
  uint32_t first=(uint32_t)((uint64_t)i*count/n),end=(uint32_t)((uint64_t)(i+1)*count/n);
  unsigned v[3]={0,0,0};
  for(uint32_t j=first;j<end;j++)for(int b=0;b<3;b++)if(bands[j*3+b]>v[b])v[b]=bands[j*3+b];
  unsigned s[3];
  for(int b=0;b<3;b++){s[b]=v[b]*255u/peak;if(s[b]>255)s[b]=255;}
  out[i]=(struct xz_shell_column){(uint8_t)s[0],(uint8_t)s[1],(uint8_t)s[2]};
 }
 return n;
}
