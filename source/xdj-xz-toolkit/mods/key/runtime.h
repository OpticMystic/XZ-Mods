/* SPDX-License-Identifier: MPL-2.0 */
#ifndef XZ_KEY_RUNTIME_H
#define XZ_KEY_RUNTIME_H
#include <stdint.h>

int xz_key_runtime_start(void);
void xz_key_runtime_observe(int observe);
void xz_key_runtime_stop(void);
int xz_key_set_desired_semitones(int deck, int semitones);
int xz_key_get_desired_semitones(int deck, int *semitones_out);
/* Bind UI requests to the loaded output epoch. New tracks start at unity. */
int xz_key_set_desired_cents(int deck, uint32_t epoch, int cents);
int xz_key_get_desired_cents(int deck, uint32_t epoch, int *cents);
/* Clear only the request owned by this Key Sync action; preserve newer/manual requests. */
int xz_key_clear_desired_cents(int deck, uint32_t epoch, int expected_cents);
/* Last pitch ratio actually processed for this loaded track; pending requests
 * and another track's proof must never change the displayed playing key. */
int xz_key_has_applied(int deck,uint32_t epoch);
int xz_key_get_applied_cents(int deck, uint32_t epoch, int *cents);
struct xz_key_audio_deck_proof { uint32_t epoch, blocks, ratio_bits; int32_t cents; };
struct xz_key_audio_proof { uint32_t version;struct xz_key_audio_deck_proof deck[2]; };
extern struct xz_key_audio_proof xz_mods_key_audio_proof_v1;

#endif
