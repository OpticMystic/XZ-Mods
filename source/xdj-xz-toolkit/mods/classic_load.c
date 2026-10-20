/* SPDX-License-Identifier: MIT */
#include "classic_load.h"
#include <string.h>

static int expired(uint32_t now_ms, uint32_t until_ms) { return (int32_t)(now_ms - until_ms) >= 0; }

void xz_classic_load_arm(struct xz_classic_load *state, int side, uint32_t now_ms) {
 if(!state || side < 0 || side > 1) return;
 state->side[side].phase = XZ_CL_ARMED;
 state->side[side].until_ms = now_ms + XZ_CLASSIC_LOAD_MS;
}

void xz_classic_load_cancel(struct xz_classic_load *state, int side) {
 if(state && side >= 0 && side <= 1) state->side[side].phase = XZ_CL_IDLE;
}

int xz_classic_load_source(uint32_t id_hi) {
 if(id_hi & 0xffu) return 0;
 switch((id_hi >> 8) & 0xffu) {
 case 2: return 1;
 case 3: return 2;
 default: return 0;
 }
}

enum xz_cl_verdict xz_classic_load_message(struct xz_classic_load *state, const struct xz_cl_message *message,
                                           uint32_t now_ms, struct xz_extra_deck_event *load) {
 if(!state || !message || !load || message->deck < 0 || message->deck > 1) return XZ_CL_PASS;
 enum xz_cl_phase want = message->id == XZ_CL_MSG_LOAD_ROW ? XZ_CL_ARMED :
                         message->id == XZ_CL_MSG_ROW_TRACK ? XZ_CL_ASKED : XZ_CL_IDLE;
 if(want == XZ_CL_IDLE) return XZ_CL_PASS;
 int side = message->deck;
 if(state->side[side].phase != want) return XZ_CL_PASS;
 if(expired(now_ms, state->side[side].until_ms)) { state->side[side].phase = XZ_CL_IDLE; return XZ_CL_PASS; }
 if(want == XZ_CL_ARMED) {
  state->side[side].phase = XZ_CL_ASKED;
  state->side[side].until_ms = now_ms + XZ_CLASSIC_LOAD_MS;
  return XZ_CL_ASK;
 }
 state->side[side].phase = XZ_CL_IDLE;
 int source = xz_classic_load_source(message->id_hi);
 if(!source || !message->id_lo || message->id_lo == UINT32_MAX) return XZ_CL_REFUSE;
 memset(load, 0, sizeof *load);
 load->deck = side;
 load->kind = XZ_EXTRA_IN_LOAD;
 load->pressed = 1;
 load->load.source = source;
 load->load.track_id = message->id_lo;
 return XZ_CL_DELIVER;
}

#ifndef XZ_CLASSIC_LOAD_PORTABLE_TEST
#include "runtime.h"
#include "doom/native_bridge.h"
#include <pthread.h>
#if !defined(__arm__) || defined(__aarch64__)
#error Classic LOAD adapter requires verified 32-bit ARM ABI
#endif
_Static_assert(sizeof(void*)==4,"XZ classic LOAD pointer ABI");

/* Firmware 1.26. Total_MainLoadMessageProc(id, message) and the sender its 0x711 branch uses for 0x3fc,
   Total_MainSendMessage_Arg.constprop.5(id, mailbox, reply mailbox, row, then eight stack words). */
#define TOTAL_LOAD_PROC 0xbe400u
#define TOTAL_SEND_ARG 0xb9858u
#define MBX_DB_CLIENT 0x3eabe90u        /* one mailbox word per native deck */
#define MBX_TOTAL_MAIN 0x2247c94u
#define LITERAL_POOL 0xbf3d8u           /* the 0x711 branch's own references to the two mailboxes */
#define MSG_ROW_REQUEST 0x3fcu

typedef int (*load_proc_fn)(uint32_t, void *);
typedef int (*send_arg_fn)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                           uint32_t, uint32_t, uint32_t, uint32_t);
static load_proc_fn stock_proc;
static int installed;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static struct xz_classic_load state;
/* Bounded counters: 0 asks, 1 deliveries, 2 player refusals, 3 non-USB refusals, 4 failed asks. */
__attribute__((visibility("default"))) uint32_t xz_mods_classic_load_v1[6] = {1};
#define COUNT(i) __atomic_fetch_add(&xz_mods_classic_load_v1[(i)+1],1,__ATOMIC_RELAXED)

static uint32_t field(const void *message, unsigned offset) {
 uint32_t value; memcpy(&value, (const unsigned char *)message + offset, 4); return value;
}

static int ask_row(int deck, uint32_t row, uint32_t title_flag) {
 uint32_t mailbox, reply;
 if(xz_read_memory(MBX_DB_CLIENT + 4u * (uint32_t)deck, &mailbox, 4) || xz_read_memory(MBX_TOTAL_MAIN, &reply, 4))
  return 0;
 ((send_arg_fn)(uintptr_t)TOTAL_SEND_ARG)(MSG_ROW_REQUEST, mailbox, reply, row, (uint32_t)deck, title_flag,
                                          0, 0, 0, 0, 0, 0);
 return 1;
}

static int load_proc_hook(uint32_t id, void *message) {
 if(!message || (id != XZ_CL_MSG_LOAD_ROW && id != XZ_CL_MSG_ROW_TRACK)) return stock_proc(id, message);
 struct xz_cl_message view = { .id = id };
 if(id == XZ_CL_MSG_LOAD_ROW) {
  view.deck = (int)field(message, 0x30); view.row = field(message, 0x24); view.title_flag = field(message, 0x34);
 } else {
  view.deck = (int)field(message, 0x54); view.id_hi = field(message, 0x24); view.id_lo = field(message, 0x28);
 }
 struct xz_extra_deck_event load;
 pthread_mutex_lock(&mutex);
 enum xz_cl_verdict verdict = xz_classic_load_message(&state, &view, xz_game_milliseconds(), &load);
 pthread_mutex_unlock(&mutex);
 switch(verdict) {
 case XZ_CL_PASS: return stock_proc(id, message);
 case XZ_CL_ASK:
  if(ask_row(view.deck, view.row, view.title_flag)) { COUNT(0); return 1; }
  /* Still kept from stock: passing it on would load DECK 1/2 with the track meant for DECK 3/4. */
  pthread_mutex_lock(&mutex); xz_classic_load_cancel(&state, view.deck); pthread_mutex_unlock(&mutex);
  COUNT(4); xz_log("Classic LOAD: DB row request unavailable; DECK 3/4 not loaded");
  return 1;
 case XZ_CL_DELIVER:
  if(xz_extra_deck_input(&load)) COUNT(1);
  else { COUNT(2); xz_log("Classic LOAD: the DECK 3/4 player refused the track"); }
  return 1;
 case XZ_CL_REFUSE:
  COUNT(3); xz_log("Classic LOAD: DECK 3/4 loads USB tracks only");
  return 1;
 }
 return stock_proc(id, message);
}

int xz_classic_load_start(void) {
 static const unsigned char proc_guard[8] = {0x12,0x37,0x00,0xe3,0x03,0x00,0x50,0xe1};
 static const unsigned char send_guard[8] = {0xf0,0x4f,0x2d,0xe9,0x14,0xd0,0x4d,0xe2};
 static const uint32_t pool[2] = {MBX_DB_CLIENT, MBX_TOTAL_MAIN};
 unsigned char actual[8]; uint32_t words[2];
 if(stock_proc) { __atomic_store_n(&installed, 1, __ATOMIC_RELEASE); return 0; }
 if(xz_read_memory(TOTAL_LOAD_PROC, actual, 8) || memcmp(actual, proc_guard, 8)) return -1;
 if(xz_read_memory(TOTAL_SEND_ARG, actual, 8) || memcmp(actual, send_guard, 8)) return -1;
 if(xz_read_memory(LITERAL_POOL, words, 8) || memcmp(words, pool, 8)) return -1;
 if(xz_hook_arm(TOTAL_LOAD_PROC, proc_guard, (void *)load_proc_hook, (void **)&stock_proc)) return -1;
 __atomic_store_n(&installed, 1, __ATOMIC_RELEASE);
 return 0;
}

int xz_classic_load_divert_arm(int side) {
 if(!__atomic_load_n(&installed, __ATOMIC_ACQUIRE) || side < 0 || side > 1 || !xz_extra_decks_route().route_up)
  return 0;
 pthread_mutex_lock(&mutex);
 xz_classic_load_arm(&state, side, xz_game_milliseconds());
 pthread_mutex_unlock(&mutex);
 return 1;
}
#endif
