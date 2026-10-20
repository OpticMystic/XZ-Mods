/* SPDX-License-Identifier: MIT */
/* Pure parts of the FLAGSHIP shell runtime. No device, threads or allocation. */
#include "shell_runtime.h"
#include "shell/shell_settings.h"
#include "shell/shell_browse.h"
#include "settings.h"
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
 /* LINK opens stock's media and connection screen (linked players' media, CONTROL MODE for LOAD 1/2): the
    shell steps aside until stock is back on PLAY. REKORDBOX does what MODS > CONTROLS > BUTTONS assigns
    (xz_shrt_rekordbox_verdict); its stock action hands off the same way. */
 {0x205,0,XZ_SHRT_HAND_OFF,-1,-1,-1},{0x202,0,XZ_SHRT_ASSIGNED,-1,-1,-1},
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
 /* MENU belongs to the shell on every screen: short opens its MODS screen (or a list's MENU panel), held
    opens MODS on UTILITY. Stock's own MENU does nothing on PLAY, so it is never passed. */
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

void xz_shrt_contact_press(struct xz_shrt_contacts *c, uint16_t code, unsigned channel, int swallow) {
 int slot=-1;
 for(int i=0;i<XZ_SHRT_CONTACTS;i++){
  if(c->c[i].used&&c->c[i].code==code&&c->c[i].channel==channel){slot=i;break;}
  if(!c->c[i].used&&slot<0)slot=i;
 }
 if(slot<0)slot=0; /* more than eight keys held: reuse the oldest slot */
 c->c[slot].code=code;c->c[slot].channel=(uint8_t)channel;c->c[slot].used=1;
 c->c[slot].swallow=(uint8_t)!!swallow;c->c[slot].held=0;
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

int xz_shrt_push_event(struct xz_shrt_push *p, uint32_t flags, uint32_t state) {
 /* The stock handler runs for every key pass while its state is set, so only
    the press phase (flags 1) and the first long state count as events. */
 if(flags==1){p->down=1;p->held=0;return XZ_SHELL_ENCODER_PUSH;}
 if(p->down&&!p->held&&state==3){p->held=1;return XZ_SHELL_ENCODER_HOLD;}
 return -1;
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

const char *const xz_shrt_quantize_choices[4]={"1/8","1/4","1/2","1"};
int xz_shrt_quantize_index(int den) { return den==8?0:den==4?1:den==2?2:den==1?3:-1; }
int xz_shrt_shell_owned_setting(int id) {
 return id==XZ_SHELL_SET_WAVE_COLOR||id==XZ_SHELL_SET_WAVE_POSITION||id==XZ_SHELL_ITEM_KEY_DISPLAY||id==XZ_SHELL_ITEM_WAVE_ZOOM
  ||id==XZ_SHELL_ITEM_MODE_LAMPS||id==XZ_SHELL_ITEM_QUANTIZE_LAMP;
}
int xz_shrt_setting_writable(int id, int time_mode, int quantize_beat) {
 return id==XZ_SHELL_SET_TIME_MODE?!!time_mode:id==XZ_SHELL_ITEM_QUANTIZE_BEAT?!!quantize_beat:0;
}
int xz_shrt_setting_command(const struct xz_shell_action *a, uint32_t now, struct xz_shrt_command *out) {
 if(!a||!out||a->kind!=XZ_SHELL_ACT_SET_SETTING)return 0;
 memset(out,0,sizeof *out);out->at_ms=now;
 int deck=a->deck>=0&&a->deck<XZ_SHELL_DECKS?a->deck:-1;
 switch(a->id){
 case XZ_SHELL_SET_TIME_MODE: /* UiSetKeyTimeMode(channel, remain) */
  if(a->value!=XZ_SHELL_TIME_REMAIN&&a->value!=XZ_SHELL_TIME_ELAPSED)return 0;
  out->kind=XZ_SHRT_CMD_TIME_MODE;out->deck=deck;out->value=a->value==XZ_SHELL_TIME_REMAIN;return 1;
 case XZ_SHELL_ITEM_QUANTIZE_BEAT: /* UiSetUtilQuantizeValue: 0 = 1 beat .. 3 = 1/8; choices run 1/8 .. 1 */
  if(a->value<0||a->value>3)return 0;
  out->kind=XZ_SHRT_CMD_QUANTIZE_BEAT;out->deck=-1;out->value=3-a->value;return 1;
 default:
  /* QUANTIZE on/off, PLAY MODE, AUTO CUE, HOT CUE AUTO LOAD and the rest have no C3 setter. */
  return 0;
 }
}

uint32_t xz_shrt_frame_period_ms(enum xz_shell_screen s, int any_playing) { return s==XZ_SHELL_PLAY&&any_playing?XZ_SHRT_FRAME_LIVE_MS:XZ_SHRT_FRAME_IDLE_MS; }

int xz_shrt_motion_step(struct xz_shrt_motion *m, int32_t elapsed_ms, uint32_t read_at) {
 if(!m->valid){m->valid=1;m->last_ms=elapsed_ms;m->changed_at=read_at-XZ_SHRT_MOTION_MS;return 0;} /* one reading proves nothing */
 if(elapsed_ms!=m->last_ms){m->last_ms=elapsed_ms;m->changed_at=read_at;}
 return (uint32_t)(read_at-m->changed_at)<XZ_SHRT_MOTION_MS;
}

void xz_shrt_position_reset(struct xz_shrt_position *p) { memset(p,0,sizeof *p); }
int32_t xz_shrt_rate_from_tempo(int32_t tempo_x100) { return 1000000+tempo_x100*100; }
int xz_shrt_position_step(struct xz_shrt_position *p, int playing, int32_t elapsed_ms, uint32_t read_at,
                          int32_t rate, int measure, int32_t *out_ms, uint32_t *out_at) {
 int64_t rate_ppm=rate<250000?250000:rate>4000000?4000000:rate;
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
    p->following=0;p->regular_run=0;p->base_us=(int64_t)elapsed_ms*1000;p->base_at=read_at;
    p->read_at=read_at;p->read_ms=elapsed_ms;p->last_ms=elapsed_ms;p->last_at=read_at;
    *out_ms=elapsed_ms<0?0:elapsed_ms;*out_at=read_at;return 0;
   }
  }
 }
 if(fresh&&measure){p->have_last=1;p->last_ms=elapsed_ms;p->last_at=read_at;}
 if(!measure||!playing){p->following=0;p->regular_run=0;}
 if(p->following){
  p->valid=1;p->base_us=(int64_t)elapsed_ms*1000;p->base_at=read_at;p->read_at=read_at;p->read_ms=elapsed_ms;
  *out_ms=elapsed_ms<0?0:elapsed_ms;*out_at=0;return 1;
 }
 if(!p->valid||!playing){
  p->valid=1;p->base_us=(int64_t)elapsed_ms*1000;p->base_at=read_at;
 }else if(fresh){
  /* A repeated reading (a held snapshot) carries no information and changes nothing. */
  int64_t predicted=p->base_us+(int64_t)(int32_t)(read_at-p->base_at)*rate_ppm/1000;
  int64_t error=(int64_t)elapsed_ms*1000-predicted;
  if(error>XZ_SHRT_POSITION_SNAP_MS*1000||error< -XZ_SHRT_POSITION_SNAP_MS*1000)p->base_us=(int64_t)elapsed_ms*1000;
  else{
   int64_t nudge=error/8;
   if(nudge>XZ_SHRT_POSITION_NUDGE_MS*1000)nudge=XZ_SHRT_POSITION_NUDGE_MS*1000;
   if(nudge< -XZ_SHRT_POSITION_NUDGE_MS*1000)nudge=-XZ_SHRT_POSITION_NUDGE_MS*1000;
   p->base_us=predicted+nudge;
  }
  p->base_at=read_at;
 }
 p->read_at=read_at;p->read_ms=elapsed_ms;
 int64_t ms=(p->base_us+500)/1000;
 *out_ms=ms<0?0:ms>INT32_MAX?INT32_MAX:(int32_t)ms;*out_at=p->base_at?p->base_at:1;
 return 0;
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
