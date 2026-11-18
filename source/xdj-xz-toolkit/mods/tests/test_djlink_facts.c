/* SPDX-License-Identifier: MIT */
/* Deck facts for the Pro DJ Link helper, from constructed DECK 3/4 views. */
#include "../djlink_facts.h"
#include "../djlink/djl_bus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define BEATS 20
static struct xz_extra_beat grid[BEATS]; /* 120 BPM: beat k at 100 + 500 k ms, bars 1..4 */
static const uint64_t NOW = 5000123456ull;   /* CLOCK_MONOTONIC us */
#define NOW_MS ((uint32_t)(NOW / 1000u))

static void make_grid(void)
{
 for (unsigned k = 0; k < BEATS; k++) grid[k] = (struct xz_extra_beat){(int32_t)(100 + 500 * k), (uint8_t)(k % 4 + 1), 12000};
}

/* Loaded, paused at elapsed_ms, every readout known. */
static struct xz_extra_deck deck_at(int32_t elapsed_ms)
{
 struct xz_extra_deck d;
 memset(&d, 0, sizeof d);
 d.known = XZ_EXTRA_KNOWN(XZ_EXTRA_F_TIMELINE) | XZ_EXTRA_KNOWN(XZ_EXTRA_F_GRID) | XZ_EXTRA_KNOWN(XZ_EXTRA_F_TOTAL) |
           XZ_EXTRA_KNOWN(XZ_EXTRA_F_BPM) | XZ_EXTRA_KNOWN(XZ_EXTRA_F_CUE) | XZ_EXTRA_KNOWN(XZ_EXTRA_F_ELAPSED);
 d.loaded = 1;
 d.source = 1;
 d.track_id = 4242;
 d.total_ms = 10000;
 d.elapsed_ms = elapsed_ms;
 d.elapsed_at_ms = NOW_MS - 40;
 d.cue_ms = 100;
 d.track_bpm_x100 = d.bpm_x100 = 12000;
 d.grid = grid;
 d.grid_count = BEATS;
 d.timeline.source_rate = 44100;
 d.timeline.source_frame = (uint64_t)elapsed_ms * 441u / 10u;
 d.timeline.at_ms = NOW_MS - 40;
 d.timeline.flags = XZ_EXTRA_TIMELINE_ANCHOR | XZ_EXTRA_TIMELINE_RATE | XZ_EXTRA_TIMELINE_MOTION;
 d.timeline.route_epoch = 3;
 d.timeline.load_revision = 7;
 d.timeline.transport_generation = 11;
 d.timeline.media_generation = 0x500000002ull;
 return d;
}

/* Playing at 1.02 with a fresh completed-audio coordinate 5 ms old and a 441-frame (10 ms) wire gap. */
static struct xz_extra_deck playing_at(int32_t heard_ms)
{
 struct xz_extra_deck d = deck_at(heard_ms);
 d.playing = 1;
 d.timeline.flags |= XZ_EXTRA_TIMELINE_SINK_FRAME | XZ_EXTRA_TIMELINE_APPLIED_RATE;
 d.timeline.moving = 1;
 d.timeline.at_ms = NOW_MS - 5;
 d.timeline.sink_frame = 100000;
 d.timeline.observed_wire_frame = 100441;
 d.timeline.completed_rate = 1.02;
 d.timeline.rate_ppm = 1020000;
 d.bpm_x100 = 12240;
 return d;
}

/* The helper's reading of a timeline (djl.c djl_beat_time_us), without linking the wire core. */
static uint64_t beat_time_us(const struct djl_timeline *t, unsigned index)
{
 if (t->ahead_ms[index] == DJL_NO_TIME || t->speed_q20 <= 0) return 0;
 int64_t delta = (int64_t)t->ahead_ms[index] * 1000 - t->playhead_us;
 if (delta < 0) return 0;
 return t->captured_us + (uint64_t)((delta * (int64_t)DJL_PITCH_UNITY + t->speed_q20 - 1) / t->speed_q20);
}

static struct djl_deck_facts facts(const struct xz_extra_deck *d)
{
 struct djl_deck_facts f;
 xz_djlink_facts_from_deck(0, d, 1, NULL, NOW, &f);
 return f;
}

static void expect_beats(const struct djl_deck_facts *f, uint32_t next, const uint32_t *ms, unsigned count)
{
 CHECK(f->timeline.next_beat == next);
 for (unsigned k = 0; k < DJL_AHEAD; k++) {
  if (k < count) {
   CHECK(f->timeline.ahead_ms[k] == ms[k]);
   CHECK(f->timeline.ahead_bar[k] == (next + k - 1) % 4 + 1);
  } else {
   CHECK(f->timeline.ahead_ms[k] == DJL_NO_TIME);
  }
 }
}

static void test_empty_and_loading(void)
{
 struct xz_extra_deck d;
 memset(&d, 0, sizeof d);
 struct djl_deck_facts f = facts(&d);
 CHECK(f.play == DJL_PLAY_EMPTY && f.content_epoch == 0);
 CHECK(f.track.type == DJL_TRACK_NONE && f.track.player == 0 && f.track.slot == 0 && f.track.rekordbox_id == 0);
 CHECK(f.track_bpm_x100 == 0xffff && f.pitch_q20 == DJL_PITCH_UNITY && f.fader_pitch_q20 == DJL_PITCH_UNITY);
 CHECK(f.timeline.next_beat == 0 && f.timeline.track_ms == 0 && f.timeline.speed_q20 == 0 && f.timeline.captured_us == NOW);
 for (unsigned k = 0; k < DJL_AHEAD; k++) CHECK(f.timeline.ahead_ms[k] == DJL_NO_TIME);
 CHECK(!f.master && !f.sync && f.on_air && !f.wants_master);

 d.loaded = 1;
 d.source = 2;
 d.track_id = 77;
 f = facts(&d);
 CHECK(f.play == DJL_PLAY_LOADING && f.content_epoch == 0);
 CHECK(f.track.player == 1 && f.track.slot == DJL_SLOT_USB && f.track.type == DJL_TRACK_REKORDBOX && f.track.rekordbox_id == 77);
 CHECK(f.timeline.next_beat == 0);
}

static void test_slots_and_epoch(void)
{
 struct xz_extra_deck d = deck_at(500);
 struct djl_deck_facts f = facts(&d);
 CHECK(f.track.player == 1 && f.track.slot == DJL_SLOT_SD && f.track.type == DJL_TRACK_REKORDBOX && f.track.rekordbox_id == 4242);
 d.source = 2;
 CHECK(facts(&d).track.slot == DJL_SLOT_USB);
 d.source = 3;
 CHECK(facts(&d).track.type == DJL_TRACK_NONE);
 d.source = 1;
 d.track_id = 0;
 CHECK(facts(&d).track.type == DJL_TRACK_NONE);

 d = deck_at(500);
 uint32_t first = facts(&d).content_epoch;
 CHECK(first != 0 && first == xz_djlink_content_epoch(&d));
 CHECK(facts(&d).content_epoch == first);
 uint32_t seen[64];
 for (unsigned i = 0; i < 64; i++) {
  d.timeline.load_revision = 8 + i;
  seen[i] = facts(&d).content_epoch;
  CHECK(seen[i] != 0 && seen[i] != first);
  for (unsigned j = 0; j < i; j++) CHECK(seen[j] != seen[i]);
 }
 d.timeline.load_revision = 7;
 d.timeline.media_generation = 0x500000003ull;
 CHECK(facts(&d).content_epoch != first);
 d.loaded = 0;
 CHECK(facts(&d).content_epoch == 0);
}

static void test_play_states(void)
{
 struct xz_extra_deck d = deck_at(100);
 CHECK(facts(&d).play == DJL_PLAY_CUED);
 d = deck_at(2000);
 CHECK(facts(&d).play == DJL_PLAY_PAUSED);
 d = deck_at(10000);
 CHECK(facts(&d).play == DJL_PLAY_ENDED);
 d = playing_at(3200);
 CHECK(facts(&d).play == DJL_PLAY_PLAYING);
 d.loop.active = 1;
 d.loop.in_ms = 3100;
 d.loop.out_ms = 5100;
 CHECK(facts(&d).play == DJL_PLAY_LOOPING);
 d = playing_at(100);
 d.cue_play = 1;
 CHECK(facts(&d).play == DJL_PLAY_CUE_PLAY);
 d = deck_at(2000);
 d.held_playing = 1;
 CHECK(facts(&d).play == DJL_PLAY_PLAYING);
 d = deck_at(2000);
 d.known &= ~XZ_EXTRA_KNOWN(XZ_EXTRA_F_TIMELINE);
 CHECK(facts(&d).play == DJL_PLAY_LOADING);
}

static void test_beats(void)
{
 struct xz_extra_deck d = deck_at(500);
 struct djl_deck_facts f = facts(&d);
 uint32_t mid[DJL_AHEAD];
 for (unsigned k = 0; k < DJL_AHEAD; k++) mid[k] = 600 + 500 * k;
 expect_beats(&f, 2, mid, DJL_AHEAD);

 d = deck_at(600); /* exactly on a beat: that beat is no longer ahead */
 f = facts(&d);
 for (unsigned k = 0; k < DJL_AHEAD; k++) mid[k] = 1100 + 500 * k;
 expect_beats(&f, 3, mid, DJL_AHEAD);

 d = deck_at(50); /* before the first beat */
 f = facts(&d);
 for (unsigned k = 0; k < DJL_AHEAD; k++) mid[k] = 100 + 500 * k;
 expect_beats(&f, 1, mid, DJL_AHEAD);

 d = deck_at(9000); /* the last two beats of the track */
 f = facts(&d);
 const uint32_t last[] = {9100, 9600};
 expect_beats(&f, 19, last, 2);

 d = deck_at(9700); /* past the last beat */
 f = facts(&d);
 expect_beats(&f, 21, NULL, 0);

 d = deck_at(9000);
 d.total_ms = 9500; /* a grid beat past the end of the audio is never heard */
 f = facts(&d);
 expect_beats(&f, 19, last, 1);

 d = deck_at(500);
 d.grid = NULL;
 f = facts(&d);
 expect_beats(&f, 0, NULL, 0);
 d = deck_at(500);
 d.known &= ~XZ_EXTRA_KNOWN(XZ_EXTRA_F_GRID);
 CHECK(facts(&d).timeline.next_beat == 0);

 d = playing_at(3200);
 d.loop.active = 1;
 d.loop.in_ms = 3100;
 d.loop.out_ms = 5100;
 f = facts(&d);
 const uint32_t loop[] = {3600, 4100, 4600};
 expect_beats(&f, 8, loop, 3);
}

static void test_heard_position(void)
{
 struct xz_extra_deck d = playing_at(3200);
 struct djl_deck_facts f = facts(&d);
 uint64_t at = (uint64_t)(NOW_MS - 5) * 1000u + NOW % 1000u;
 CHECK(f.timeline.captured_us == at - 10000u);
 CHECK(f.timeline.playhead_us == 3200000);
 CHECK(f.timeline.speed_q20 == 1069548); /* round(1.02 * 2^20) */
 CHECK(f.timeline.track_ms == 10000);
 uint32_t ahead[DJL_AHEAD];
 for (unsigned k = 0; k < DJL_AHEAD; k++) ahead[k] = 3600 + 500 * k;
 expect_beats(&f, 8, ahead, DJL_AHEAD);
 CHECK(f.pitch_q20 == 1069548 && f.track_bpm_x100 == 12000 && f.fader_pitch_q20 == DJL_PITCH_UNITY);

 /* Same answer as the core expects: the next beat is heard (3600 - 3200) / 1.02 ms after capture. */
 uint64_t due = beat_time_us(&f.timeline, 0);
 CHECK(due > f.timeline.captured_us + 392150 && due < f.timeline.captured_us + 392170);

 d = playing_at(3200);
 d.timeline.at_ms = NOW_MS - 120; /* stale completed coordinate: fall back to the anchor and its slope */
 f = facts(&d);
 CHECK(f.timeline.captured_us == (uint64_t)(NOW_MS - 120) * 1000u);
 CHECK(f.timeline.playhead_us == 3200000 && f.timeline.speed_q20 == 1069548);

 d = playing_at(3200);
 d.timeline.observed_wire_frame = d.timeline.sink_frame + 4411; /* gap over 100 ms */
 f = facts(&d);
 CHECK(f.timeline.captured_us == (uint64_t)(NOW_MS - 5) * 1000u);

 d = playing_at(3200);
 d.timeline.rate_ppm = -1020000;
 CHECK(facts(&d).timeline.speed_q20 == -1069548);

 d = deck_at(2000); /* paused: position at the published anchor, not advancing */
 f = facts(&d);
 CHECK(f.timeline.playhead_us == 2000000 && f.timeline.speed_q20 == 0 && f.timeline.captured_us == (uint64_t)(NOW_MS - 40) * 1000u);
 CHECK(beat_time_us(&f.timeline, 0) == 0);

 d = deck_at(2000);
 d.timeline.flags = 0;
 f = facts(&d);
 CHECK(f.timeline.playhead_us == 2000000 && f.timeline.captured_us == (uint64_t)(NOW_MS - 40) * 1000u);
}

static void test_tempo(void)
{
 struct xz_extra_deck d = deck_at(500);
 d.tempo_x100 = 10000; /* WIDE +100 % */
 d.bpm_x100 = 24000;
 struct djl_deck_facts f = facts(&d);
 CHECK(f.fader_pitch_q20 == 2 * DJL_PITCH_UNITY && f.pitch_q20 == 2 * DJL_PITCH_UNITY);
 d.tempo_x100 = -10000; /* WIDE -100 %: stopped */
 d.bpm_x100 = 0;
 f = facts(&d);
 CHECK(f.fader_pitch_q20 == 0 && f.pitch_q20 == 0);
 d.tempo_x100 = -600; /* -6 % */
 d.bpm_x100 = 11280;
 f = facts(&d);
 CHECK(f.fader_pitch_q20 == 985661 && f.pitch_q20 == 985661);
 d.tempo_x100 = 1600;
 d.tempo_reset = 1; /* TEMPO RESET: the track's own speed whatever the fader says */
 d.bpm_x100 = 12000;
 f = facts(&d);
 CHECK(f.fader_pitch_q20 == DJL_PITCH_UNITY && f.pitch_q20 == DJL_PITCH_UNITY);
 d = deck_at(500);
 d.tempo_x100 = 800;
 d.bpm_x100 = 12600; /* SYNC moved the speed away from the fader */
 f = facts(&d);
 CHECK(f.fader_pitch_q20 == 1132462 && f.pitch_q20 == 1101005);
 d.known &= ~XZ_EXTRA_KNOWN(XZ_EXTRA_F_BPM);
 f = facts(&d);
 CHECK(f.track_bpm_x100 == 0xffff && f.pitch_q20 == f.fader_pitch_q20);
 d = deck_at(500);
 d.track_bpm_x100 = 99999;
 d.bpm_x100 = 99999;
 CHECK(facts(&d).track_bpm_x100 == 0xfffe);
}

static void test_flags(void)
{
 struct xz_extra_deck d = playing_at(3200);
 d.sync.requested = 1;
 d.sync.reason = XZ_EXTRA_SYNC_LOCKED;
 struct djl_deck_facts f = facts(&d);
 CHECK(f.sync && !f.bpm_only_sync);
 d.sync.reason = XZ_EXTRA_SYNC_TEMPO_FOLLOW;
 f = facts(&d);
 CHECK(f.sync && f.bpm_only_sync);
 xz_djlink_facts_from_deck(0, &d, 0, NULL, NOW, &f);
 CHECK(!f.on_air);

 struct xz_master_observation m = {.deck = XZ_MASTER_DECK3, .revision = 4, .observed_ms = NOW_MS - 30, .operator_epoch = 2,
                                    .request_epoch = 1, .lease_generation = 1, .route_epoch = 3, .load_revision = 7,
                                    .transport_generation = 11};
 xz_djlink_facts_from_deck(0, &d, 1, &m, NOW, &f);
 CHECK(f.master);
 xz_djlink_facts_from_deck(1, &d, 1, &m, NOW, &f);
 CHECK(!f.master);
 struct xz_master_observation stale = m;
 stale.observed_ms = NOW_MS - 101;
 xz_djlink_facts_from_deck(0, &d, 1, &stale, NOW, &f);
 CHECK(!f.master);
 struct xz_master_observation reloaded = m;
 reloaded.load_revision = 6;
 xz_djlink_facts_from_deck(0, &d, 1, &reloaded, NOW, &f);
 CHECK(!f.master);
 struct xz_master_observation unknown = m;
 unknown.deck = XZ_MASTER_UNKNOWN;
 xz_djlink_facts_from_deck(0, &d, 1, &unknown, NOW, &f);
 CHECK(!f.master);
 m.deck = XZ_MASTER_DECK4;
 xz_djlink_facts_from_deck(1, &d, 1, &m, NOW, &f);
 CHECK(f.master);
}

/* The facts are written to the bus as they are; padding must be zero so equal facts give equal bytes. */
static void test_bytes(void)
{
 struct xz_extra_deck d = playing_at(3200);
 struct djl_deck_facts a, b;
 memset(&a, 0xa5, sizeof a);
 memset(&b, 0x5a, sizeof b);
 xz_djlink_facts_from_deck(0, &d, 1, NULL, NOW, &a);
 xz_djlink_facts_from_deck(0, &d, 1, NULL, NOW, &b);
 CHECK(!memcmp(&a, &b, sizeof a));
}

int main(void)
{
 make_grid();
 test_empty_and_loading();
 test_slots_and_epoch();
 test_play_states();
 test_beats();
 test_heard_position();
 test_tempo();
 test_flags();
 test_bytes();
 printf("%s djlink facts: %u checks, %u failed\n", failures ? "FAIL" : "PASS", checks, failures);
 return failures != 0;
}
