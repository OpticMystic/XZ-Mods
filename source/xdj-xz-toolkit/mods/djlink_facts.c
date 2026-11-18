/* SPDX-License-Identifier: MIT */
#include "djlink_facts.h"
#include "extra_timeline_projection.h"
#include <math.h>
#include <string.h>

#define Q20 DJL_PITCH_UNITY

uint32_t xz_djlink_content_epoch(const struct xz_extra_deck *d)
{
 if (!d->loaded || !(d->known & XZ_EXTRA_KNOWN(XZ_EXTRA_F_TIMELINE)) || !d->timeline.load_revision) return 0;
 uint64_t g = d->timeline.media_generation;
 /* An odd multiplier is a bijection mod 2^32: two loads of one stick never share an epoch. */
 uint32_t e = d->timeline.load_revision * 0x9e3779b1u ^ (uint32_t)g ^ (uint32_t)(g >> 32);
 return e ? e : 1u;
}

static struct djl_track_ref track_of(const struct xz_extra_deck *d)
{
 struct djl_track_ref t;
 memset(&t, 0, sizeof t);
 if (!d->loaded || !d->track_id || (d->source != 1 && d->source != 2)) return t;
 /* The stock unit reports its USB 1 stick as slot SD and USB 2 as slot USB, both under player 1, whose
    database server answers for both sticks. */
 t.player = 1;
 t.slot = d->source == 1 ? DJL_SLOT_SD : DJL_SLOT_USB;
 t.type = DJL_TRACK_REKORDBOX;
 t.rekordbox_id = d->track_id;
 return t;
}

static uint8_t play_of(const struct xz_extra_deck *d)
{
 if (!d->loaded) return DJL_PLAY_EMPTY;
 if (!(d->known & XZ_EXTRA_KNOWN(XZ_EXTRA_F_TIMELINE))) return DJL_PLAY_LOADING;
 if (d->playing || d->held_playing) {
  if (d->cue_play) return DJL_PLAY_CUE_PLAY;
  return d->loop.active ? DJL_PLAY_LOOPING : DJL_PLAY_PLAYING;
 }
 if ((d->known & XZ_EXTRA_KNOWN(XZ_EXTRA_F_TOTAL)) && d->total_ms > 0 && d->elapsed_ms >= d->total_ms) return DJL_PLAY_ENDED;
 if ((d->known & XZ_EXTRA_KNOWN(XZ_EXTRA_F_CUE)) && d->elapsed_ms == d->cue_ms) return DJL_PLAY_CUED;
 return DJL_PLAY_PAUSED;
}

static int moving(uint8_t play)
{
 return play == DJL_PLAY_PLAYING || play == DJL_PLAY_LOOPING || play == DJL_PLAY_CUE_PLAY;
}

static int master_of(int deck_index, const struct xz_extra_deck *d, const struct xz_master_observation *m,
                     uint64_t now_mono_us)
{
 if (!m || !d->loaded) return 0;
 uint32_t now_ms = (uint32_t)(now_mono_us / 1000u);
 return m->deck >= XZ_MASTER_NONE && m->deck <= XZ_MASTER_DECK4 && m->revision && m->operator_epoch &&
        (uint32_t)(now_ms - m->observed_ms) <= XZ_MASTER_MAX_AGE_MS && m->deck == deck_index + XZ_EXTRA_FIRST_DECK &&
        m->route_epoch == d->timeline.route_epoch && m->load_revision == d->timeline.load_revision &&
        m->transport_generation == d->timeline.transport_generation;
}

static uint32_t q20_ratio(int64_t num, int64_t den)
{
 if (den <= 0 || num <= 0) return 0;
 uint64_t v = ((uint64_t)num * Q20 + (uint64_t)den / 2u) / (uint64_t)den;
 return v > UINT32_MAX ? UINT32_MAX : (uint32_t)v;
}

static int32_t q20_of(double rate)
{
 if (!isfinite(rate)) return 0;
 double v = rate * (double)Q20;
 if (v > (double)INT32_MAX) return INT32_MAX;
 if (v < (double)INT32_MIN) return INT32_MIN;
 return (int32_t)llround(v);
}

static int64_t frame_us(uint64_t frame, uint32_t rate)
{
 return (int64_t)((frame * 1000000u + rate / 2u) / rate);
}

/* Heard position: the completed-audio anchor the sync bridge uses (wire gap removed), else the published
   anchor at its observation time, else the elapsed readout. */
static void place(const struct xz_extra_deck *d, uint8_t play, uint64_t now_mono_us, struct djl_timeline *t)
{
 const struct xz_extra_timeline *tl = &d->timeline;
 uint64_t heard;
 t->captured_us = now_mono_us;
 if (moving(play) && xz_extra_timeline_heard_us(tl, now_mono_us, &heard)) {
  t->captured_us = heard;
  t->playhead_us = frame_us(tl->source_frame, tl->source_rate);
  int32_t speed = q20_of(tl->completed_rate);
  t->speed_q20 = tl->rate_ppm < 0 ? -speed : speed;
 } else if ((tl->flags & XZ_EXTRA_TIMELINE_ANCHOR) && tl->source_rate) {
  uint64_t at = (uint64_t)tl->at_ms * 1000u;
  t->captured_us = tl->at_ms && at <= now_mono_us ? at : now_mono_us;
  t->playhead_us = frame_us(tl->source_frame, tl->source_rate);
  if (moving(play) && tl->moving && (tl->flags & XZ_EXTRA_TIMELINE_RATE)) t->speed_q20 = q20_of(tl->rate_ppm / 1000000.);
 } else if (d->known & XZ_EXTRA_KNOWN(XZ_EXTRA_F_ELAPSED)) {
  uint64_t at = (uint64_t)d->elapsed_at_ms * 1000u;
  t->captured_us = d->elapsed_at_ms && at <= now_mono_us ? at : now_mono_us;
  t->playhead_us = d->elapsed_ms > 0 ? (int64_t)d->elapsed_ms * 1000 : 0;
 }
 if (t->playhead_us < 0) t->playhead_us = 0;
}

/* The 12 grid beats after the heard position. While a loop plays, beats at or past its end are not heard
   before the jump back; the publication after the jump lists the beats from the loop start. */
static void beats_ahead(const struct xz_extra_deck *d, uint8_t play, struct djl_timeline *t)
{
 if (!(d->known & XZ_EXTRA_KNOWN(XZ_EXTRA_F_GRID)) || !d->grid || !d->grid_count) return;
 unsigned lo = 0, hi = d->grid_count;
 while (lo < hi) {
  unsigned mid = lo + (hi - lo) / 2;
  if ((int64_t)d->grid[mid].ms * 1000 <= t->playhead_us) lo = mid + 1;
  else hi = mid;
 }
 t->next_beat = lo + 1u;
 int64_t end = t->track_ms ? (int64_t)t->track_ms : INT64_MAX;
 if (play == DJL_PLAY_LOOPING && d->loop.out_ms > d->loop.in_ms && t->playhead_us < (int64_t)d->loop.out_ms * 1000 &&
     d->loop.out_ms < end)
  end = (int64_t)d->loop.out_ms - 1;
 for (unsigned k = 0; k < DJL_AHEAD && lo + k < d->grid_count; k++) {
  const struct xz_extra_beat *b = &d->grid[lo + k];
  if (b->ms < 0 || (int64_t)b->ms > end) break;
  t->ahead_ms[k] = (uint32_t)b->ms;
  t->ahead_bar[k] = b->beat_in_bar >= 1 && b->beat_in_bar <= 4 ? b->beat_in_bar : (uint8_t)((lo + k) % 4u + 1u);
 }
}

void xz_djlink_facts_from_deck(int deck_index, const struct xz_extra_deck *d, int route_up,
                               const struct xz_master_observation *master, uint64_t now_mono_us,
                               struct djl_deck_facts *out)
{
 memset(out, 0, sizeof *out);
 for (unsigned k = 0; k < DJL_AHEAD; k++) out->timeline.ahead_ms[k] = DJL_NO_TIME;
 out->track_bpm_x100 = 0xffff;
 out->content_epoch = xz_djlink_content_epoch(d);
 out->track = track_of(d);
 uint8_t play = play_of(d);
 out->play = play;
 out->master = (uint8_t)master_of(deck_index, d, master, now_mono_us);
 out->sync = (uint8_t)!!d->sync.requested;
 out->bpm_only_sync = (uint8_t)(d->sync.requested && d->sync.reason == XZ_EXTRA_SYNC_TEMPO_FOLLOW);
 out->on_air = (uint8_t)!!route_up;
 out->fader_pitch_q20 = d->tempo_reset ? Q20 : q20_ratio(10000 + (int64_t)d->tempo_x100, 10000);
 out->pitch_q20 = out->fader_pitch_q20;
 if ((d->known & XZ_EXTRA_KNOWN(XZ_EXTRA_F_BPM)) && d->track_bpm_x100 > 0) {
  out->track_bpm_x100 = (uint16_t)(d->track_bpm_x100 < 0xffff ? d->track_bpm_x100 : 0xfffe);
  out->pitch_q20 = q20_ratio(d->bpm_x100, d->track_bpm_x100);
 }
 struct djl_timeline *t = &out->timeline;
 t->captured_us = now_mono_us;
 if (play == DJL_PLAY_EMPTY || play == DJL_PLAY_LOADING) return;
 if ((d->known & XZ_EXTRA_KNOWN(XZ_EXTRA_F_TOTAL)) && d->total_ms > 0) t->track_ms = (uint32_t)d->total_ms;
 place(d, play, now_mono_us, t);
 beats_ahead(d, play, t);
}
