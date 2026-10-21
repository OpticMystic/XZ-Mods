/* SPDX-License-Identifier: MIT */
#include "deck_select.h"
#include <math.h>
#include <string.h>

#define ROW(lo, hi, cls, path, kind) {lo, hi, XZ_DS_ROW_##cls, XZ_DS_PATH_##path, kind}
#define NONE (-1)
/* Key codes: VJToolsLibrary docs/research/2026-10-18-xz-shell-abi.md (deck keys reach the onPhysicalKey
   slot; LOAD, TIME MODE and QUANTIZE reach IKeyManager::onKey), and the codes kart, arcade and the cue
   decoder already take from this hook. While a side drives its software deck, a row decides where that
   side's key goes; on a side driving its native deck every key passes to stock. */
static const struct xz_ds_row rows[] = {
 /* SHIFT keeps its stock meaning (stock sets the +0x34 flag other keys read); the double press is only
    observed. */
 ROW(0x4103, 0x4103, STOCK, PLAYER, NONE),
 /* Transport the software deck plays. Never reaches the native deck the DJ cannot see. */
 ROW(0x4101, 0x4101, DELIVER, PLAYER, XZ_EXTRA_IN_PLAY),
 ROW(0x4102, 0x4102, DELIVER, PLAYER, XZ_EXTRA_IN_CUE),
 ROW(0x4305, 0x4305, DELIVER, PLAYER, XZ_EXTRA_IN_JOG_TURN),   /* op 4 */
 ROW(0x4306, 0x4306, DELIVER, PLAYER, XZ_EXTRA_IN_JOG_TOUCH),
 ROW(0x410a, 0x410a, DELIVER, PLAYER, XZ_EXTRA_IN_TEMPO),      /* op 5, through pickup */
 ROW(0x4313, 0x4313, DELIVER, PLAYER, XZ_EXTRA_IN_SYNC),
 ROW(0x4108, 0x4108, DELIVER, PLAYER, XZ_EXTRA_IN_MASTER_TEMPO),
 /* Pads A..H: HOT_CUE while the software deck's pad mode is HOT CUE, otherwise swallowed. */
 ROW(0x4119, 0x4120, DELIVER, PLAYER, XZ_EXTRA_IN_HOT_CUE),
 /* Pad mode buttons: they set the software deck's pad mode (HOT CUE alone, without SHIFT, is HOT CUE)
    and never change the native deck's. */
 ROW(0x4114, 0x4118, SWALLOW, PLAYER, NONE),
 ROW(0x4104, 0x4104, DELIVER, PLAYER, XZ_EXTRA_IN_VINYL),
 ROW(0x4107, 0x4107, DELIVER, PLAYER, XZ_EXTRA_IN_TEMPO_RANGE),
 ROW(0x4109, 0x4109, DELIVER, PLAYER, XZ_EXTRA_IN_TEMPO_RESET),
 ROW(0x410d, 0x410d, DELIVER, PLAYER, XZ_EXTRA_IN_LOOP_IN),
 ROW(0x410e, 0x410e, DELIVER, PLAYER, XZ_EXTRA_IN_LOOP_OUT),
 ROW(0x410f, 0x410f, DELIVER, PLAYER, XZ_EXTRA_IN_RELOOP),
 /* REVERSE and SLIP remain owned by the software side but have no input yet. */
 ROW(0x4110, 0x4111, SWALLOW, PLAYER, NONE),
 /* SEARCH FWD/REV, CUE/LOOP DELETE and MEMORY, NEEDLE SEARCH, MASTER, CUE/LOOP CALL NEXT/PREV,
    all move or change the deck they reach. */
 ROW(0x4121, 0x4122, SWALLOW, PLAYER, NONE),
 ROW(0x4126, 0x4128, SWALLOW, PLAYER, NONE),
 ROW(0x4312, 0x4312, DELIVER, GLOBAL, XZ_EXTRA_IN_MASTER),
 ROW(0x4324, 0x4324, DELIVER, PLAYER, XZ_EXTRA_IN_LOOP_DOUBLE), /* CALL NEXT / 2X */
 ROW(0x4325, 0x4325, DELIVER, PLAYER, XZ_EXTRA_IN_LOOP_HALF),   /* CALL PREV / 1/2X */
 /* TotalMainIfUi consumes TRACK SEARCH globally before the physical-player hook. */
 ROW(0x4212, 0x4212, DELIVER, GLOBAL, XZ_EXTRA_IN_TRACK_NEXT),
 ROW(0x4213, 0x4213, DELIVER, GLOBAL, XZ_EXTRA_IN_SEEK),
 /* LOAD 1 / LOAD 2 (channel 1/2): the track comes from the browser on screen, so the press is the
    caller's (XZ_DS_LOAD): the FLAGSHIP shell loads its highlighted row to the side's software deck, or the
    classic interface arms its one-shot divert and the key goes on to stock, whose own load the divert
    sends to DECK 3 / 4. Neither: swallowed, or DECK 1 would load what was meant for 3.
    TIME MODE / AUTO CUE and QUANTIZE: native deck display and quantize state. */
 ROW(0x430f, 0x430f, SWALLOW, GLOBAL, XZ_EXTRA_IN_LOAD),
 ROW(0x430b, 0x430c, SWALLOW, GLOBAL, NONE),
};

const struct xz_ds_row *xz_ds_row_for(uint16_t code, unsigned *slot) {
 unsigned base = 0;
 for(size_t i = 0; i < sizeof rows / sizeof *rows; i++) {
  if(code >= rows[i].lo && code <= rows[i].hi) {
   if(slot) *slot = base + (unsigned)(code - rows[i].lo);
   return &rows[i];
  }
  base += (unsigned)(rows[i].hi - rows[i].lo) + 1;
 }
 return NULL;
}

static void decode_packet(const unsigned char *p,struct xz_ds_key *k){
 memcpy(&k->code,p+8,2);k->op=p[11]&15u;
 if(k->op==XZ_DS_OP_JOG||k->op==XZ_DS_OP_FADER){memcpy(&k->delta,p+12,4);memcpy(&k->value,p+16,4);}
}
int xz_ds_key_decode_global(const void *input,struct xz_ds_key *k){
 const unsigned char *p=input;if(!p||!k||p[10]<1||p[10]>2)return 0;
 memset(k,0,sizeof *k);k->side=(int)p[10]-1;decode_packet(p,k);return 1;
}
int xz_ds_key_decode(const void *innards, const void *input, struct xz_ds_key *k) {
 const unsigned char *self = innards, *p = input;
 if(!self || !p || !k || (self[0x26] != 1 && self[0x26] != 2)) return 0;
 memset(k,0,sizeof *k);k->side=(int)self[0x26]-1;k->shift=self[0x34]!=0;
 decode_packet(p,k);return 1;
}

static int within_slack(int32_t a, int32_t b) { return a - b <= XZ_DS_PICKUP_SLACK && b - a <= XZ_DS_PICKUP_SLACK; }

void xz_ds_pickup_arm(struct xz_ds_pickup *p, int32_t target, int physical_known, int32_t physical) {
 p->target = target; p->have_prev = physical_known; p->prev = physical;
 p->armed = !(physical_known && within_slack(physical, target));
}

int xz_ds_pickup_feed(struct xz_ds_pickup *p, int32_t position) {
 if(!p->armed) return 1;
 if(within_slack(position, p->target) || (p->have_prev && (p->prev < p->target) != (position < p->target))) {
  p->armed = 0; return 1;
 }
 p->have_prev = 1; p->prev = position;
 return 0;
}

void xz_deck_select_init(struct xz_deck_select *d) {
 memset(d, 0, sizeof *d);
 for(int i = 0; i < 2; i++) {
  d->side[i].extra_hot_cue = 1;
  /* A software deck starts at 0 % tempo: the fader centre. */
  d->side[i].held_known[XZ_DS_EXTRA] = 1;
 }
}

static void switch_to(struct xz_ds_side *s, enum xz_ds_target target) {
 if(s->target == target) return;
 s->target = target;
 if(s->held_known[target]) xz_ds_pickup_arm(&s->pickup, s->held[target], s->physical_known, s->physical);
 else s->pickup.armed = 0;
}

void xz_deck_select_route(struct xz_deck_select *d, int route_up) {
 d->route_up = !!route_up;
 if(!d->route_up) for(int i = 0; i < 2; i++) switch_to(&d->side[i], XZ_DS_NATIVE);
}
void xz_deck_select_feedback(struct xz_deck_select *d,const struct xz_extra_decks *extra) {
 if(!d||!extra||!extra->route_up)return;
 for(int i=0;i<2;i++){
  const struct xz_extra_deck *deck=&extra->deck[i];
  struct xz_ds_side *side=&d->side[i];
  uint32_t needed=XZ_EXTRA_KNOWN(XZ_EXTRA_F_TEMPO)|XZ_EXTRA_KNOWN(XZ_EXTRA_F_TEMPO_RANGE);
  if((deck->known&needed)!=needed||(deck->tempo_range!=6&&deck->tempo_range!=10&&
     deck->tempo_range!=16&&deck->tempo_range!=100))continue;
  if(side->extra_revision_known&&side->extra_revision==deck->tempo_revision)continue;
  int64_t position=(int64_t)deck->tempo_x100*XZ_EXTRA_TEMPO_SPAN/(deck->tempo_range*100);
  if(position< -XZ_EXTRA_TEMPO_SPAN||position>XZ_EXTRA_TEMPO_SPAN)continue;
  side->extra_revision=deck->tempo_revision;side->extra_revision_known=1;
  side->held[XZ_DS_EXTRA]=(int32_t)position;side->held_known[XZ_DS_EXTRA]=1;
  if(side->target==XZ_DS_EXTRA)xz_ds_pickup_arm(&side->pickup,(int32_t)position,side->physical_known,side->physical);
 }
}

void xz_deck_select_shift(struct xz_deck_select *d, int side, int down, uint32_t now, struct xz_ds_outcome *out) {
 if(side < 0 || side > 1) return;
 struct xz_ds_side *s = &d->side[side];
 if(!down) {
  if(s->phase == XZ_DS_DOWN_ONCE) s->phase = XZ_DS_RELEASED;
  else if(s->phase != XZ_DS_RELEASED) s->phase = XZ_DS_IDLE;
  return;
 }
 if(s->phase == XZ_DS_RELEASED && now - s->first_down_ms <= XZ_DS_WINDOW_MS) {
  s->phase = XZ_DS_DOWN_TWICE;
  if(!d->route_up) { if(out) out->notice = XZ_DS_NOTICE_NO_ROUTE; return; }
  switch_to(s, s->target == XZ_DS_NATIVE ? XZ_DS_EXTRA : XZ_DS_NATIVE);
  if(out) out->toggled = 1;
  return;
 }
 /* Idle, or released too long ago: this press starts a new count. A press already down repeats. */
 if(s->phase == XZ_DS_IDLE || s->phase == XZ_DS_RELEASED) { s->phase = XZ_DS_DOWN_ONCE; s->first_down_ms = now; }
}

void xz_deck_select_toggle(struct xz_deck_select *d, int side, struct xz_ds_outcome *out) {
 if(!d||side<0||side>1)return;
 if(!d->route_up){if(out)out->notice=XZ_DS_NOTICE_NO_ROUTE;return;}
 struct xz_ds_side *s=&d->side[side];
 switch_to(s, s->target == XZ_DS_NATIVE ? XZ_DS_EXTRA : XZ_DS_NATIVE);
 if(out)out->toggled=1;
}

void xz_deck_select_load_route(struct xz_deck_select *d, int side, enum xz_ds_load_route route) {
 if(d&&side>=0&&side<=1&&d->side[side].load_down)d->side[side].load=route;
}

/* Any other event on the side: SHIFT held is now a modifier; a pending double press is broken. */
static void other_key(struct xz_ds_side *s) {
 if(s->phase == XZ_DS_DOWN_ONCE) s->phase = XZ_DS_MODIFIER;
 else if(s->phase == XZ_DS_RELEASED) s->phase = XZ_DS_IDLE;
}

static int remember_unknown(struct xz_deck_select *d, uint16_t code) {
 for(unsigned i = 0; i < d->logged_count; i++) if(d->logged[i] == code) return 0;
 if(d->logged_count < sizeof d->logged / sizeof *d->logged) d->logged[d->logged_count++] = code;
 return 1;
}

static void deliver(struct xz_ds_outcome *out, int side, enum xz_extra_input kind, const struct xz_ds_key *k) {
 out->verdict = XZ_DS_DELIVER;
 out->event.deck = side;
 out->event.kind = kind;
 out->event.shift = k->shift;
}

static int32_t fader_position(float value) {
 float scaled = value * (float)XZ_EXTRA_TEMPO_SPAN;
 if(scaled > (float)XZ_EXTRA_TEMPO_SPAN) scaled = (float)XZ_EXTRA_TEMPO_SPAN;
 if(scaled < -(float)XZ_EXTRA_TEMPO_SPAN) scaled = -(float)XZ_EXTRA_TEMPO_SPAN;
 return (int32_t)(scaled + (scaled < 0 ? -0.5f : 0.5f));
}

/* The native hook reports the fader in -1..1 with the minus end (physical up by default) negative. */
static void tempo(struct xz_ds_side *s, int side, const struct xz_ds_key *k, struct xz_ds_outcome *out) {
 int away = s->target == XZ_DS_EXTRA;
 if(!isfinite(k->value)) { out->verdict = away ? XZ_DS_SWALLOW : XZ_DS_PASS; return; }
 int32_t position = fader_position(k->value);
 s->physical_known = 1; s->physical = position;
 if(!xz_ds_pickup_feed(&s->pickup, position)) { out->verdict = XZ_DS_SWALLOW; return; }
 s->held_known[s->target] = 1; s->held[s->target] = position;
 if(!away) return;
 deliver(out, side, XZ_EXTRA_IN_TEMPO, k);
 out->event.tempo_position = position;
}

static void button(struct xz_ds_side *s, int side, const struct xz_ds_row *row, unsigned slot,
 const struct xz_ds_key *k, struct xz_ds_outcome *out) {
 uint64_t bit = (uint64_t)1 << slot;
 int away = s->target == XZ_DS_EXTRA;
 if(k->op == 0) {
  if(!away) { s->owner[XZ_DS_NATIVE] |= bit; return; }
  if(row->kind==XZ_EXTRA_IN_SYNC){s->owner[XZ_DS_EXTRA]|=bit;out->verdict=XZ_DS_SWALLOW;return;}
  if(k->code >= 0x4114 && k->code <= 0x4118) s->extra_hot_cue = k->code == 0x4114 && !k->shift;
  int pad = k->code >= 0x4119 && k->code <= 0x4120;
  if(row->cls != XZ_DS_ROW_DELIVER || (pad && !s->extra_hot_cue)) { out->verdict = XZ_DS_SWALLOW; return; }
  s->owner[XZ_DS_EXTRA] |= bit;
  deliver(out, side, (enum xz_extra_input)row->kind, k);
  out->event.pressed = 1;
  if(pad) out->event.hot_cue = (int)(k->code - 0x4119);
  return;
 }
 /* Hold and release follow whoever received the press, so neither deck is left with a key down. */
 int release = k->op == 2 || k->op == 3;
 if(s->owner[XZ_DS_NATIVE] & bit) { if(release) s->owner[XZ_DS_NATIVE] &= ~bit; return; }
 if(s->owner[XZ_DS_EXTRA] & bit) {
  if(k->op == 1 && row->kind == XZ_EXTRA_IN_LOOP_IN) {
   deliver(out, side, XZ_EXTRA_IN_LOOP_IN, k);
   out->event.pressed = out->event.held = 1;
   return;
  }
  if(!release) { out->verdict = XZ_DS_SWALLOW; return; }
  s->owner[XZ_DS_EXTRA] &= ~bit;
  if(row->kind==XZ_EXTRA_IN_SYNC){
   out->verdict=XZ_DS_SWALLOW;
   if(k->op==2){deliver(out,side,XZ_EXTRA_IN_SYNC,k);out->event.pressed=1;}
   return;
  }
  deliver(out, side, (enum xz_extra_input)row->kind, k);
  if(k->code >= 0x4119 && k->code <= 0x4120) out->event.hot_cue = (int)(k->code - 0x4119);
  return;
 }
 if(away) out->verdict = XZ_DS_SWALLOW;
}

struct xz_ds_outcome xz_deck_select_key(struct xz_deck_select *d, const struct xz_ds_key *k, uint32_t now) {
 struct xz_ds_outcome out;
 memset(&out, 0, sizeof out);
 if(!d || !k || k->side < 0 || k->side > 1) return out;
 struct xz_ds_side *s = &d->side[k->side];
 if(k->code == XZ_DS_KEY_SHIFT) {
  if(k->op == 0) xz_deck_select_shift(d, k->side, 1, now, &out);
  else if(k->op == 2 || k->op == 3) xz_deck_select_shift(d, k->side, 0, now, &out);
  return out;
 }
 other_key(s);
 if(k->code == XZ_DS_KEY_TEMPO && k->op == XZ_DS_OP_FADER) { tempo(s, k->side, k, &out); return out; }
 int away = s->target == XZ_DS_EXTRA;
 unsigned slot = 0;
 const struct xz_ds_row *row = xz_ds_row_for(k->code, &slot);
 if(!row) {
  /* A release reaching stock without its press is ignored there; a press held down on a deck the DJ
     cannot see is not. Unknown releases pass, everything else is swallowed. */
  if(away && k->op != 2 && k->op != 3) { out.verdict = XZ_DS_SWALLOW; out.unknown_first = remember_unknown(d, k->code); }
  return out;
 }
 if(row->cls == XZ_DS_ROW_STOCK) return out;
 uint64_t native_bit=(uint64_t)1<<slot;
 if((k->op==2||k->op==3)&&(s->early_native_release&native_bit)){
  s->early_native_release&=~native_bit;s->owner[XZ_DS_NATIVE]&=~native_bit;return out;
 }
 if(s->early_native_down&native_bit){
  if(k->op==0)s->owner[XZ_DS_NATIVE]|=native_bit;
  else if(k->op==2||k->op==3){s->early_native_down&=~native_bit;s->owner[XZ_DS_NATIVE]&=~native_bit;}
  return out;
 }
 if(row->path==XZ_DS_PATH_GLOBAL){if(away)out.verdict=XZ_DS_SWALLOW;return out;}
 int stream = row->kind == XZ_EXTRA_IN_JOG_TURN || row->kind == XZ_EXTRA_IN_TEMPO;
 if(stream || k->op == XZ_DS_OP_JOG || k->op == XZ_DS_OP_FADER) {
  if(!away) return out;
  if(row->kind != XZ_EXTRA_IN_JOG_TURN || k->op != XZ_DS_OP_JOG) { out.verdict = XZ_DS_SWALLOW; return out; }
  deliver(&out, k->side, XZ_EXTRA_IN_JOG_TURN, k);
  out.event.jog.delta = k->delta;
  out.event.jog.speed = isfinite(k->value) ? k->value : 0.0f;
  out.event.jog.speed_known = isfinite(k->value);
  return out;
 }
 if(k->op > 3) { if(away) out.verdict = XZ_DS_SWALLOW; return out; }
 button(s, k->side, row, slot, k, &out);
 return out;
}

/* Native contacts are recorded once by the physical callback. Software contacts finish
   before native PC listeners, including releases after selection or route changes. */
struct xz_ds_outcome xz_deck_select_global_player(struct xz_deck_select *d,const struct xz_ds_key *k,uint32_t now){
 struct xz_ds_outcome pass={0};
 if(!d||!k||k->side<0||k->side>1)return pass;
 unsigned slot=0;const struct xz_ds_row *row=xz_ds_row_for(k->code,&slot);
 if(!row||row->path!=XZ_DS_PATH_PLAYER||row->cls==XZ_DS_ROW_STOCK)return pass;
 struct xz_ds_side *s=&d->side[k->side];other_key(s);
 int stream=row->kind==XZ_EXTRA_IN_JOG_TURN||row->kind==XZ_EXTRA_IN_TEMPO;
 if(!stream){
  uint64_t bit=(uint64_t)1<<slot;int release=k->op==2||k->op==3;
  /* A new press retires an old release token when a PC listener ate the fallback. */
  if(k->op==0&&(s->early_native_release&bit)){s->early_native_release&=~bit;s->owner[XZ_DS_NATIVE]&=~bit;}
  if(s->owner[XZ_DS_EXTRA]&bit){
   if(k->op==0){pass.verdict=XZ_DS_SWALLOW;return pass;}
   return xz_deck_select_key(d,k,now);
  }
  if(s->early_native_down&bit){
   if(release){s->early_native_down&=~bit;s->early_native_release|=bit;}
   return pass;
  }
  if(s->owner[XZ_DS_NATIVE]&bit)return pass;
  if(s->target==XZ_DS_NATIVE){if(k->op==0)s->early_native_down|=bit;return pass;}
  if(release&&(s->early_native_release&bit))return pass;
 }else if(s->target!=XZ_DS_EXTRA)return pass;
 return xz_deck_select_key(d,k,now);
}

struct xz_ds_outcome xz_deck_select_global_key(struct xz_deck_select *d, unsigned channel, uint16_t code,
 unsigned op, uint32_t now) {
 struct xz_ds_outcome out;
 memset(&out, 0, sizeof out);
 if(!d || (channel != 1 && channel != 2)) return out;
 int side = (int)channel - 1;
 if(code == XZ_DS_KEY_SHIFT) {
  if(op == 0) xz_deck_select_shift(d, side, 1, now, &out);
  else if(op == 2 || op == 3) xz_deck_select_shift(d, side, 0, now, &out);
  return out;
 }
 const struct xz_ds_row *row = xz_ds_row_for(code, NULL);
 struct xz_ds_side *s = &d->side[side];
 if(code==0x4212||code==0x4213) {
  unsigned slot=0;xz_ds_row_for(code,&slot);
  uint64_t bit=(uint64_t)1<<slot;
  int release=op==2||op==3;
  if(s->owner[XZ_DS_NATIVE]&bit){if(release)s->owner[XZ_DS_NATIVE]&=~bit;return out;}
  if(s->owner[XZ_DS_EXTRA]&bit){if(release)s->owner[XZ_DS_EXTRA]&=~bit;out.verdict=XZ_DS_SWALLOW;return out;}
  if(s->target==XZ_DS_NATIVE){if(op==0)s->owner[XZ_DS_NATIVE]|=bit;return out;}
  int shift=s->phase==XZ_DS_DOWN_ONCE||s->phase==XZ_DS_DOWN_TWICE||s->phase==XZ_DS_MODIFIER;
  other_key(s);out.verdict=XZ_DS_SWALLOW;
  if(op==0){
   s->owner[XZ_DS_EXTRA]|=bit;
   if(!shift){out.verdict=XZ_DS_DELIVER;out.event.deck=side;out.event.kind=code==0x4213?XZ_EXTRA_IN_SEEK:XZ_EXTRA_IN_TRACK_NEXT;out.event.pressed=1;out.event.seek_ms=0;}
  }
  return out;
 }
 if(row&&row->kind==XZ_EXTRA_IN_MASTER){
  unsigned slot=0;(void)xz_ds_row_for(code,&slot);uint64_t bit=(uint64_t)1<<slot;
  if(op==0&&((s->owner[XZ_DS_EXTRA]|s->owner[XZ_DS_NATIVE])&bit)){
   if(s->owner[XZ_DS_EXTRA]&bit)out.verdict=XZ_DS_SWALLOW;
   return out;
  }
  struct xz_ds_key key={.side=side,.code=code,.op=op};
  key.shift=s->phase==XZ_DS_DOWN_ONCE||s->phase==XZ_DS_DOWN_TWICE||s->phase==XZ_DS_MODIFIER;
  other_key(s);button(s,side,row,slot,&key,&out);return out;
 }
 if(row && row->kind == XZ_EXTRA_IN_LOAD) {
  if(op == 0) {
   /* A repeated press packet is still the opening contact, not a second
      numbered choice in the LOAD popup. Only a release arms another press. */
   if(s->load_down){out.verdict=XZ_DS_SWALLOW;return out;}
   s->load_down = s->target == XZ_DS_EXTRA;
   s->load = XZ_DS_LOAD_SWALLOW;
   if(!s->load_down) return out;
   other_key(s);
   out.verdict = XZ_DS_LOAD;
   out.event.deck = side;
   out.event.kind = XZ_EXTRA_IN_LOAD;
   out.event.pressed = 1;
   return out;
  }
  if(!s->load_down) return out;
  out.verdict = s->load == XZ_DS_LOAD_STOCK ? XZ_DS_PASS : XZ_DS_SWALLOW;
  if(op == 2 || op == 3) s->load_down = 0;
  return out;
 }
 if(row && row->path == XZ_DS_PATH_GLOBAL && s->target == XZ_DS_EXTRA) {
  other_key(s);
  out.verdict = XZ_DS_SWALLOW;
 }
 return out;
}
