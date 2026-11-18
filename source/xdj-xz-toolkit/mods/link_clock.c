/* SPDX-License-Identifier: MIT */
#include "link_clock.h"
#include <math.h>
#include <string.h>

static uint32_t epoch_of(int source, uint32_t a, uint32_t b)
{
 uint32_t e = (uint32_t)source * 0x9e3779b1u ^ a * 0x85ebca6bu ^ (b + 0x27d4eb2fu) * 0xc2b2ae35u;
 return e ? e : 1u;
}

static double wrap4(double beats)
{
 double w = fmod(beats, 4.0);
 if (w < 0) w += 4.0;
 return w >= 4.0 ? 0.0 : w;
}

void xz_link_master_none(struct xz_link_master *out)
{
 memset(out, 0, sizeof *out);
 out->source = XZ_LINK_SOURCE_NONE;
}

void xz_link_master_from_stock(const struct xz_sync_native_snapshot *s, struct xz_link_master *out)
{
 xz_link_master_none(out);
 if (!s || s->native_deck < 0 || s->native_deck > 1) return;
 out->source = XZ_LINK_SOURCE_DECK1 + s->native_deck;
 double bpm = s->bpm, phase = s->bar_phase;
 if (!s->available || !s->phase_available || !s->captured_us || !isfinite(bpm) || bpm <= 0 || bpm >= 1000 ||
     !isfinite(phase) || phase < 0 || phase >= 4)
  return;
 int64_t at = (int64_t)s->captured_us + s->output_latency_us;
 if (at <= 0) return;
 out->valid = 1;
 out->bpm = bpm;
 out->bar_phase = phase;
 out->at_us = (uint64_t)at;
 out->uncertainty_us = s->uncertainty_us;
 out->playing = !!s->playing;
 out->manual = !!s->manual;
 out->epoch = epoch_of(out->source, s->source_revision, s->track_identity);
}

void xz_link_master_from_facts(int deck_index, const struct djl_deck_facts *f, int manual, uint32_t uncertainty_us,
                               uint32_t transport_generation, struct xz_link_master *out)
{
 xz_link_master_none(out);
 if (!f || deck_index < 0 || deck_index > 1) return;
 out->source = XZ_LINK_SOURCE_DECK3 + deck_index;
 const struct djl_timeline *t = &f->timeline;
 if (!f->content_epoch || !t->captured_us || !t->next_beat || t->ahead_ms[0] == DJL_NO_TIME || f->track_bpm_x100 == 0 ||
     f->track_bpm_x100 == 0xffff || t->ahead_bar[0] < 1 || t->ahead_bar[0] > 4)
  return;
 int moving = f->play == DJL_PLAY_PLAYING || f->play == DJL_PLAY_LOOPING;
 if (moving && t->speed_q20 <= 0) return;
 double grid_bpm = f->track_bpm_x100 / 100.0;
 double beat_us = 60e6 / grid_bpm;
 double to_next = (double)t->ahead_ms[0] * 1000.0 - (double)t->playhead_us;
 /* The first beat ahead is at most one beat away; more means the grid skipped (a gap) and the bar is unknown. */
 if (to_next < 0 || to_next > beat_us * 1.01) return;
 out->valid = 1;
 out->playing = moving;
 out->bpm = grid_bpm * (moving ? (double)t->speed_q20 / DJL_PITCH_UNITY : (double)f->pitch_q20 / DJL_PITCH_UNITY);
 out->bar_phase = wrap4((double)(t->ahead_bar[0] - 1) - to_next / beat_us);
 out->at_us = t->captured_us;
 out->uncertainty_us = uncertainty_us;
 out->manual = !!manual;
 out->epoch = epoch_of(out->source, f->content_epoch, transport_generation);
}

int xz_link_pick_master(const struct xz_master_observation *o, int observed, uint64_t now_us,
                        const struct xz_sync_native_snapshot *stock, int stock_ok)
{
 uint32_t now_ms = (uint32_t)(now_us / 1000u);
 if (observed && o && o->deck >= XZ_MASTER_NONE && o->deck <= XZ_MASTER_DECK4 && o->revision && o->operator_epoch &&
     (uint32_t)(now_ms - o->observed_ms) <= XZ_MASTER_MAX_AGE_MS)
  return o->deck;
 if (stock_ok && stock && stock->available && (stock->native_deck == 0 || stock->native_deck == 1) &&
     stock->master_id == stock->native_deck)
  return XZ_MASTER_DECK1 + stock->native_deck;
 return XZ_MASTER_UNKNOWN;
}

void xz_link_master_select(int deck, const struct xz_sync_native_snapshot *stock, int stock_ok,
                           const struct xz_link_master soft[2], struct xz_link_master *out)
{
 xz_link_master_none(out);
 if (deck == XZ_MASTER_DECK1 || deck == XZ_MASTER_DECK2) {
  if (stock_ok && stock && stock->native_deck == deck - XZ_MASTER_DECK1) xz_link_master_from_stock(stock, out);
  else out->source = XZ_LINK_SOURCE_DECK1 + (deck - XZ_MASTER_DECK1);
 } else if (deck == XZ_MASTER_DECK3 || deck == XZ_MASTER_DECK4) {
  const struct xz_link_master *m = soft ? &soft[deck - XZ_MASTER_DECK3] : NULL;
  if (m && m->source == XZ_LINK_SOURCE_DECK3 + (deck - XZ_MASTER_DECK3)) *out = *m;
  else out->source = XZ_LINK_SOURCE_DECK3 + (deck - XZ_MASTER_DECK3);
 } else if (deck == XZ_MASTER_EXTERNAL) {
  out->source = XZ_LINK_SOURCE_EXTERNAL;
 }
}
