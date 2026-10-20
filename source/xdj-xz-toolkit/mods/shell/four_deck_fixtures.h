#ifndef XZ_SHELL_FOUR_DECK_FIXTURES_H
#define XZ_SHELL_FOUR_DECK_FIXTURES_H
/* A four-deck model for the host tests and previews: decks 1 and 2 as fixtures.h has them, deck 3
   paused at its cue on the second stick, deck 4 playing an eight-beat loop. Never device data. */
#include "fixtures.h"

static void fx_deck_three(struct xz_shell_deck *d, const struct fx_track *t) {
 fx_deck_one(d,t);
 d->title="Sunrise Theme";d->artist="Sample Collective";d->source=XZ_SHELL_SRC_USB2;d->track_no=7;
 d->bpm_x100=12800;d->tempo_x100=0;d->tempo_range=6;d->master=0;d->master_tempo=1;d->playing=0;d->cue_standby=1;
 d->elapsed_ms=fx_beat_ms(32);d->time_mode=XZ_SHELL_TIME_REMAIN;
 d->key="Am";d->key_original="Am";d->key_matches_master=0;
 d->quantize_on=1;d->quantize_num=1;d->quantize_den=4;
 memset(d->hot_cue,0,sizeof d->hot_cue);
 d->hot_cue[0]=(struct xz_shell_cue){1,fx_beat_ms(32),0x20c8ff,0};
 d->hot_cue[1]=(struct xz_shell_cue){1,fx_beat_ms(160),0xffe020,0};
 d->memory_count=1;d->memory_cue[0]=(struct xz_shell_cue){1,fx_beat_ms(288),0,0};
 d->cue_ms=fx_beat_ms(32);
 d->bar_phase=1;
}
static void fx_deck_four(struct xz_shell_deck *d, const struct fx_track *t) {
 fx_deck_two(d,t);
 d->title="Warehouse Tool (Dub)";d->artist="Test Pattern";d->source=XZ_SHELL_SRC_USB2;d->track_no=23;
 d->bpm_x100=12966;d->tempo_x100=133;d->tempo_range=16;d->master=0;d->sync=1;d->master_tempo=0;
 d->elapsed_ms=fx_beat_ms(258);d->time_mode=XZ_SHELL_TIME_ELAPSED;
 d->key="Gm";d->key_original="Gm";d->key_cents=0;d->key_synced=0;d->key_shift=0;
 d->quantize_on=1;d->quantize_num=1;d->quantize_den=2;
 d->loop=(struct xz_shell_loop){1,8,1,fx_beat_ms(256),fx_beat_ms(264)};
 memset(d->hot_cue,0,sizeof d->hot_cue);
 d->hot_cue[0]=(struct xz_shell_cue){1,fx_beat_ms(64),0xff50c8,0};
 d->hot_cue[2]=(struct xz_shell_cue){1,fx_beat_ms(256),0xff8c1a,1};
 d->hot_cue[5]=(struct xz_shell_cue){1,fx_beat_ms(448),0x9b3cff,0};
 d->memory_count=2;
 d->memory_cue[0]=(struct xz_shell_cue){1,fx_beat_ms(128),0,0};
 d->memory_cue[1]=(struct xz_shell_cue){1,fx_beat_ms(384),0,0};
 d->cue_ms=fx_beat_ms(64);
 d->bar_phase=(int)((d->elapsed_ms*100/FX_BEAT_MS)%4)+1;
}
static inline void fx_model_four(struct xz_shell_model *m, const struct fx_track *t) {
 fx_model_full(m,t);
 m->deck_count=4;
 fx_deck_three(&m->deck[2],t);fx_deck_four(&m->deck[3],t);
}
/* A deck with a stick in it and nothing loaded, as the runtime reports one. */
static inline void fx_deck_unloaded(struct xz_shell_deck *d, enum xz_shell_source source) {
 memset(d,0,sizeof *d);
 d->known=XZ_SHELL_KNOWN(XZ_SHELL_F_LOADED)|XZ_SHELL_KNOWN(XZ_SHELL_F_SOURCE)|XZ_SHELL_KNOWN(XZ_SHELL_F_TEMPO)
  |XZ_SHELL_KNOWN(XZ_SHELL_F_TEMPO_RANGE)|XZ_SHELL_KNOWN(XZ_SHELL_F_QUANTIZE)|XZ_SHELL_KNOWN(XZ_SHELL_F_MASTER_TEMPO);
 d->source=source;d->tempo_range=10;d->quantize_num=1;d->quantize_den=1;
}
#endif
