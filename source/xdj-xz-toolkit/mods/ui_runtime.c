#define _GNU_SOURCE
#include "writes.h"
#include "ui_runtime.h"
#include "ui/play_key.h"
#include "ui/play_key_classic.h"
#include "ui/menu_key.h"
#pragma weak xz_extra_decks_snapshot
#pragma weak xz_extra_decks_route_status
#pragma weak xz_extra_decks_rescan
#include "network/runtime.h"
#include "ui/ui_network.h"
#include "ui/ui_support.h"
#include "metrics_runtime.h"
#include "diary/runtime.h"
#pragma weak xz_diary_notice_code
#pragma weak xz_diary_error_code
/* Portable UI harnesses omit the OS sampler. */
#pragma weak xz_metrics_start
#pragma weak xz_metrics_stop
#pragma weak xz_metrics_enable
#pragma weak xz_metrics_snapshot
#include "runtime.h"
#include "audio/runtime.h"
#include "key/runtime.h"
#include "key/control.h"
#pragma weak xz_key_has_applied
#pragma weak xz_audio_output_epoch
extern int xz_sync_link_runtime_master_observe(struct xz_master_observation *) __attribute__((weak));
#pragma weak xz_extra_decks_route

#include "key/native.h"
#include "key/display.h"
#include "key/browser.h"
#include "key/traffic.h"
#include "key/native_browse.h"
#include "ui/key_strip.h"
#include "ui/key_frame_cache.h"
#include "ui/transient_notice.h"
/* Root's native browse operations. Weak so UI tests link without the stock ABI. */
extern int xz_native_browse_open_sort(void) __attribute__((weak));
extern unsigned xz_native_browse_mode(void) __attribute__((weak));
/* Live counters for the frame-level key strip and browse header. */
struct xz_key_strip_proof { uint32_t version, drawn[2], errors[2], head_drawn, head_taps, head_opened, head_failed; };
__attribute__((visibility("default"))) struct xz_key_strip_proof xz_mods_key_strip_proof_v1 = { .version = 1 };
__attribute__((visibility("default"),used)) struct {
    uint32_t version,enabled,play_drawn[2],browse_drawn[2];
} xz_mods_key_feedback_proof_v1={.version=1,.enabled=1};
__attribute__((visibility("default"),used)) struct {
    uint32_t version,enabled,free_mib,valid,samples;
} xz_mods_ram_debug_v1={.version=1};
static uint32_t ram_debug_sample_ms;
#include "ram/runtime.h"
#include "ram/decks.h"
#pragma weak xz_ram_deck_status
#pragma weak xz_ram_deck_command
#pragma weak xz_ram_usage
#include "ui/native_touch.h"
#include "ui/stem_pads.h"
#include "pc_control/stems.h"
#include "pc_control/wave.h"
#include "ui/native_led.h"
#include "ui/native_wave_runtime.h"
#include "ui/layered_wave_runtime.h"
#include "ui/native_skin_runtime.h"
#include "ui/wave_viewport.h"
#include "ui/extra_detail.h"
#include "ui/classic_mixer.h"
#include "ui/classic_mixer_runtime.h"
#include "settings.h"
#include "settings_internal.h"
#include "usb_profile.h"
#include "dj_runtime.h"
#include "shell_runtime.h"
/* Deck select (runtime.c). Weak so UI harnesses link without it. */
#pragma weak xz_runtime_deck_global_key
#pragma weak xz_runtime_deck_global_input
#pragma weak xz_runtime_deck_early_key
#pragma weak xz_runtime_deck_games_pending
#pragma weak xz_shell_runtime_extra_load_key
#pragma weak xz_shell_runtime_key_setup_load
/* The classic four-deck detail panel: its state (ui/native_wave_runtime.c), the DECK 3/4 players
   (extra_decks.c) and DECK SELECT (runtime.c). Weak like the rest: without them the panel never opens. */
#pragma weak xz_native_four_detail
#pragma weak xz_native_four_set_detail
#pragma weak xz_native_four_detail_notice
#pragma weak xz_extra_decks_snapshot
#pragma weak xz_extra_deck_input
#pragma weak xz_runtime_deck_select_target
#pragma weak xz_classic_mixer_runtime_start
#pragma weak xz_classic_mixer_runtime_stop
#pragma weak xz_classic_mixer_runtime_frame
#pragma weak xz_classic_mixer_runtime_layout
#pragma weak xz_classic_mixer_runtime_visible
#pragma weak xz_classic_mixer_runtime_hide
#pragma weak xz_classic_mixer_runtime_native_point
#pragma weak xz_classic_mixer_runtime_pair_frame
#pragma weak xz_extra_detail_notice_text
#include "ui/layout_cycle.h"
#include "ui/deck_view.h"
#include "ui/route_card.h"
#include "ui/classic_column.h"
#pragma weak xz_native_four_card
#pragma weak xz_native_four_card_input
/* 2 DECKS follows the sides (FOUR_DECK.md): the halves the wave last painted as DECK 3 or 4. */
#pragma weak xz_native_two_pair
/* CLASSIC's calm card for the loopback cable's E-8307 window and the RAM-only notice in the wave lanes. */
#pragma weak xz_native_raw_skin_hub_calm
#pragma weak xz_native_ram_notice_set
#include "ui/native_raw_skin.h"
#pragma weak xz_shell_runtime_take_deck_view
#pragma weak xz_shell_runtime_screen
#pragma weak xz_support_identity_load
#include "audio/vendor/sha256/sha256.h"
/* The settings fingerprint for REPORT AN ISSUE, the diary's; host harnesses without it send none. */
#pragma weak sha256_init
#pragma weak sha256_update
#pragma weak sha256_final
/* FLAGSHIP shell. Weak so UI harnesses link without it; the device build links it. */
#pragma weak xz_shell_runtime_start
#pragma weak xz_shell_runtime_stop
#pragma weak xz_shell_runtime_follow_interface
#pragma weak xz_shell_runtime_key_hit
#pragma weak xz_shell_runtime_key_frame
#pragma weak xz_shell_runtime_owns
#pragma weak xz_shell_runtime_idle
#pragma weak xz_shell_runtime_frame
#pragma weak xz_shell_runtime_touch
#pragma weak xz_shell_runtime_key
#pragma weak xz_shell_runtime_encoder_turn
#pragma weak xz_shell_runtime_encoder_push
#pragma weak xz_shell_runtime_take_ui_requests
#pragma weak xz_shell_runtime_notice
#pragma weak xz_shell_runtime_will_refresh
#pragma weak xz_shell_runtime_take_stem_mute
#pragma weak xz_shell_runtime_take_mods_page
#pragma weak xz_shell_runtime_take_dj_pick
#pragma weak xz_shell_runtime_take_ui_action
#pragma weak xz_shell_runtime_rekordbox_action
#pragma weak xz_shell_runtime_take_stock_key
#pragma weak xz_shell_runtime_stock_key_ran
#pragma weak xz_shell_runtime_show_utility
#pragma weak xz_shell_runtime_stock_play
#pragma weak xz_shell_runtime_look
#pragma weak xz_shell_runtime_look_set
#pragma weak xz_shell_runtime_close_mods
#pragma weak xz_shell_runtime_mods_open
#pragma weak xz_shrt_key_lookup
/* The device build links the internal store. Host fixtures that do not test
   it leave these unset, so the XZ simply forgets settings between boots there. */
extern int xz_settings_internal_load(struct xz_settings *, unsigned *) __attribute__((weak));
extern int xz_settings_internal_save(const struct xz_settings *) __attribute__((weak));
#include <sys/time.h>
#include <sys/sysinfo.h>
#include "doom/native_bridge.h"
#include "kart/xz_kart_runtime.h"
#include "ota/native_update.h"
#include "arcade/arcade.h"
#include "arcade/input.h"
#include "arcade/clock.h"
#include "arcade/native_clock.h"
#include "arcade/wave_rider_runtime.h"
#include "arcade/rhythm_lab_runtime.h"
#include "arcade/game_nav.h"
#include "../vendor/tools/xz_runtime/mods_bridge.h"
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "stock_dialog.h"
#pragma weak xz_stock_dialog_active
#pragma weak xz_stock_counter_ids
#pragma weak xz_stock_usb_hub_text
#pragma weak xz_stock_window_text
static int stock_dialog_up(void);
static int stock_counter_classic=1;
static unsigned stock_counter_mask(uint32_t ids[2]){return xz_stock_counter_ids?xz_stock_counter_ids(ids):0;}
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>

static pthread_mutex_t ui_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct xz_ui ui;
static struct xz_ui inline_ui;
static struct xz_play_key play_key;
static uint64_t play_key_instance;
static int play_key_raw_down;
static int panel_contact; /* the current contact began on MODS in MODERN */
static void play_key_open_locked(int deck){xz_play_key_open(&play_key,deck);play_key.instance=++play_key_instance;}
static struct xz_classic_key_slots classic_keys;
static int classic_key_visible;
static int play_key_dirty;
static int play_key_visible; /* atomic: a busy frame keeps the last complete overlay frame */
static int play_key_back_contact;
static struct xz_ui_model model;
static struct xz_native_touch touch;
static struct xz_audio_status audio_status[2];
/* DJ Profiles. Weak so harnesses without the USB reader still link; the header chip then stays hidden. */
extern int xz_dj_runtime_start(pthread_mutex_t *,struct xz_ui_dj *) __attribute__((weak));
extern void xz_dj_runtime_stop(void) __attribute__((weak));
extern void xz_dj_runtime_rescan(void) __attribute__((weak));
extern void xz_dj_runtime_pick(int) __attribute__((weak));
extern void xz_dj_runtime_current(int,int,int) __attribute__((weak));
extern int xz_dj_runtime_usb_of(const char *) __attribute__((weak));
extern void xz_dj_runtime_detail(int) __attribute__((weak));
extern void xz_dj_runtime_images(int) __attribute__((weak));
extern void xz_dj_runtime_preview(int) __attribute__((weak));
extern void xz_dj_runtime_save(const struct xz_ui *) __attribute__((weak));
static struct xz_ui_dj dj_view;
static xz_stock_touch stock_touch;
static void (*forward_touch)(int, int, int);
static int started, visible, audio_available, key_available, forwarded_down;
/* A live player window takes the screen, keys, encoder and touch only from CLASSIC's own pages. MODERN and an open
   MODS (either interface) keep all of them and show the window's text themselves: the dock strip here, the play
   screen's notice in the shell. Polls every call, so the hub calm card and the window text stay current. */
static int stock_dialog_up(void){
    if(!(xz_stock_dialog_active&&xz_stock_dialog_active()))return 0;
    if(__atomic_load_n(&visible,__ATOMIC_ACQUIRE)||!__atomic_load_n(&stock_counter_classic,__ATOMIC_ACQUIRE))return 0;
    return !(xz_shell_runtime_owns&&xz_shell_runtime_owns());
}
static struct xz_key_snapshot key_snapshot[2];
static int key_valid[2];
/* List generation last seen while the play screen showed. A newer local USB list
   draw with the play screen hidden means the user is on a browse page. */
static uint32_t play_list_generation;
static int browse_context(void) {
    uint32_t generation = xz_key_browser_list_generation();
    if (xz_native_play_visible()) { play_list_generation = generation; return 0; }
    return generation != play_list_generation;
}
/* A successful KEY SYNC stays shown until that deck loads another track or its shift changes. */
static unsigned key_sync_led_mask;
int xz_ui_runtime_key_synced(int deck){return deck>=0&&deck<2&&!!(__atomic_load_n(&key_sync_led_mask,__ATOMIC_ACQUIRE)&(1u<<deck));}
static struct { int valid,cents,mt_before,target_deck; uint32_t epoch;struct xz_key_owner target_owner; } key_sync[2];
static unsigned profile_slot = ~0u;
_Static_assert((int)XZ_BROWSE_TITLE==XZ_KEY_COLUMN_TITLE&&(int)XZ_BROWSE_ARTIST==XZ_KEY_COLUMN_ARTIST&&
    (int)XZ_BROWSE_KEY==XZ_KEY_COLUMN_KEY&&(int)XZ_BROWSE_BPM==XZ_KEY_COLUMN_BPM,"browse column values");
struct xz_key_ui_deck_proof {uint32_t valid,epoch,original,minor,playing,master,master_tempo;int32_t cents,desired;};
struct xz_key_ui_proof {uint32_t version,sequence,requests;struct xz_key_ui_deck_proof deck[2];};
__attribute__((visibility("default"))) struct xz_key_ui_proof xz_mods_key_proof_v1={.version=1};
static int fb_takeover_active;
static int doom_phase,doom_previous_takeover;
static uint32_t doom_launched_at;
static struct xz_arcade arcade;
static struct xz_arcade_input arcade_input;
static int arcade_input_pending;
static unsigned arcade_contact_count;
static uint16_t arcade_contacts[2][64];
static int arcade_present,arcade_opening_touch,arcade_touch_owned,arcade_previous_takeover;
/* Encoder focus over the on-screen controls of the game being drawn. */
static struct xz_game_nav game_nav;
static struct xz_game_targets game_frame;
static uint32_t arcade_clock_sequence[2];
static uint32_t arcade_clock_poll;
static int arcade_native_clock_checked,arcade_native_clock_ready;
static int wave_rider_previous_takeover,lab_previous_takeover;
struct xz_arcade_proof {uint32_t version,frames,inputs,phase,mode,live,deck;float bpm,x,y;};
__attribute__((visibility("default"))) struct xz_arcade_proof xz_arcade_proof_v1={.version=1};
static int network_enabled, network_connected;
static char device_ip[16];
static struct xz_stem_pads pads;
static struct xz_jump_state jump_state;
static int deck_page[2] = {-1,-1};
static unsigned sync_down, sync_owned;
static unsigned master_tempo_down, master_tempo_owned;
static pthread_cond_t settings_changed = PTHREAD_COND_INITIALIZER;
static pthread_t settings_thread;
static int settings_running, settings_pending, settings_readonly, settings_created;
static struct xz_usb_profile settings_profile,settings_owner;
static int settings_owner_valid,settings_target_empty;
static int settings_slot = -1;
static uint64_t settings_revision,settings_save_revision;
static struct { struct xz_usb_profile profile; struct xz_settings values; uint64_t revision,ui_revision; int usb_writable,slot; } settings_request;
static void profile_apply_locked(const struct xz_settings *saved,const char *origin);
static int settings_owner_matches_locked(void);
static void refresh_local(void);
static int requested_stems;
static int native_focus=-1;
static uint32_t followed_generation;
static int inline_gesture_layout=XZ_STEM_LAYOUT_BELOW;
static int inline_redraw_pending;
static int inline_requested, inline_contact, inline_contact_deck, inline_wave_contact, inline_wave_deck, inline_cancelled;
/* A contact that began on the four-deck surface keeps the lane order and lane it began on. One that began
   on the open DECK 3/4 detail panel (ui/extra_detail.h) keeps the panel control and deck it began on; one
   that began on a DECK 3/4 lane is a tap that opens, switches or closes the panel while it keeps to that lane. */
static int four_contact, four_contact_order, four_contact_deck;
static int four_panel_hit=-1, four_panel_deck=-1, four_tap, four_seek_column=-1, four_seek_noticed;
static struct { int active,deck,kind,cancelled,native_down,last_x,last_y,layout; } mixer_contact;
static uint32_t displayed_generation[2], reported_generation[2];
static unsigned char overview[2][192];
struct prepared_proof_deck { uint32_t state,rate,frames,mixed,missed; int32_t lag; float correlation; };
struct prepared_proof { uint32_t version,sequence; struct prepared_proof_deck deck[2]; };
__attribute__((visibility("default"))) struct prepared_proof xz_prepared_proof_v1={1,0,{{0}}};
static char settings_usb[1024];
static char settings_message[96];
static uint32_t settings_flash_at; /* nonzero: when the last save result was reported */
static uint32_t settings_profile_applies, stem_layout_actions, stem_layout_revision;
static const char *stem_layout_origin="boot";

static int stem_json_string(char *out,size_t capacity,const char *text){
    size_t n=0;static const char hex[]="0123456789abcdef";
    if(capacity<3)return 0;out[n++]='"';
    for(const unsigned char *p=(const unsigned char *)text;*p;p++){
        if(n+7>=capacity)return 0;
        if(*p=='"'||*p=='\\'){out[n++]='\\';out[n++]=(char)*p;}
        else if(*p<32){out[n++]='\\';out[n++]='u';out[n++]='0';out[n++]='0';out[n++]=hex[*p>>4];out[n++]=hex[*p&15];}
        else out[n++]=(char)*p;
    }
    out[n++]='"';out[n]=0;return 1;
}
/* The screen observer can inspect readiness without a shell or any control action. */
__attribute__((visibility("default"))) size_t xz_mods_stem_status_json_v1(char *out,size_t capacity){
    struct xz_audio_status state[2]={{0}};int available,on,layout,metrics,slot,owner_usb,owner_matches,target_empty;
    char boot_json[128];const char *boot=getenv("XZ_MODS_BOOT_RUNTIME");
    if(!boot||strlen(boot)>60||!stem_json_string(boot_json,sizeof(boot_json),boot))strcpy(boot_json,"null");
    uint32_t revision,profiles,actions;const char *origin;
    if(!out||capacity<128||pthread_mutex_trylock(&ui_mutex))return 0;
    available=audio_available;on=!!(model.enabled&XZ_UI_STEM);
    layout=model.stems_overlay;metrics=model.metrics_enabled;slot=settings_profile.root[0]?settings_slot:-1;
    owner_usb=!settings_owner_valid?0:!strncmp(settings_owner.root,"/media/usb1/",12)?1:!strncmp(settings_owner.root,"/media/usb2/",12)?2:0;
    owner_matches=settings_owner_matches_locked();target_empty=settings_target_empty;
    revision=stem_layout_revision;profiles=settings_profile_applies;actions=stem_layout_actions;origin=stem_layout_origin;
    pthread_mutex_unlock(&ui_mutex);
    if(available)for(int i=0;i<2;i++)if(xz_audio_get_status(i,&state[i]))return 0;
    int n=snprintf(out,capacity,"{\"version\":1,\"runtime_pid\":%ld,\"boot_runtime\":%s,\"ovpgz004\":true,\"engine\":%s,\"enabled\":%s,\"layout\":%d,\"layout_revision\":%u,\"layout_origin\":\"%s\",\"layout_control_count\":%u,\"profile_apply_count\":%u,\"profile_usb\":%d,\"profile_owner_usb\":%d,\"profile_owner_matches_target\":%s,\"profile_target_empty\":%s,\"metrics_enabled\":%s,\"decks\":[",(long)getpid(),boot_json,available?"true":"false",on?"true":"false",layout,revision,origin,actions,profiles,slot<0?0:slot+1,owner_usb,owner_matches?"true":"false",target_empty?"true":"false",metrics?"true":"false");
    if(n<0||(size_t)n>=capacity)return 0;size_t used=(size_t)n;
    for(int i=0;i<2;i++){
        char path[sizeof(state[i].path)*6+3],error[sizeof(state[i].error)*6+3],format[sizeof(state[i].format_name)*6+3];
        if(!stem_json_string(path,sizeof(path),state[i].path)||!stem_json_string(error,sizeof(error),state[i].error)||
           !stem_json_string(format,sizeof(format),state[i].format_name))return 0;
        n=snprintf(out+used,capacity-used,"%s{\"deck\":%d,\"state\":%d,\"status\":\"%s\",\"prepared\":%s,\"generation\":%u,\"reader_rate\":%u,\"mixed_blocks\":%u,\"skipped_blocks\":%u,\"alignment_frames\":%d,\"alignment_correlation\":%.6f,\"path\":%s,\"error\":%s,\"format\":%s}",
            i?",":"",i+1,state[i].state,xz_audio_state_name(state[i].state),state[i].prepared?"true":"false",state[i].generation,
            state[i].reader_rate,state[i].mixed_blocks,state[i].skipped_blocks,state[i].alignment_frames,(double)state[i].alignment_correlation,path,error,format);
        if(n<0||(size_t)n>=capacity-used)return 0;used+=(size_t)n;
    }
    if(used+3>capacity)return 0;out[used++]=']';out[used++]='}';out[used]=0;return used;
}

static enum xz_ui_usb_reason usb_stem_reason(const struct xz_audio_status *state){
    if(!audio_available)return XZ_UI_USB_WHY_ENGINE;
    if(!(model.enabled&XZ_UI_STEM))return XZ_UI_USB_WHY_OFF;
    switch(state->state){
    case XZ_AUDIO_STOPPED:case XZ_AUDIO_WAITING_TRACK:return XZ_UI_USB_WHY_NO_TRACK;
    case XZ_AUDIO_LOADING_TRACK:case XZ_AUDIO_DECODING:case XZ_AUDIO_PREPARED_BUFFERING:
    case XZ_AUDIO_DISABLED:return XZ_UI_USB_WHY_LOADING;
    case XZ_AUDIO_PREPARED_ALIGNING:return XZ_UI_USB_WHY_ALIGNING;
    case XZ_AUDIO_EXPERIMENTAL_READY:return XZ_UI_USB_WHY_NONE;
    case XZ_AUDIO_CACHE_MISSING:return XZ_UI_USB_WHY_NOT_PREPARED;
    default:return XZ_UI_USB_WHY_ERROR;
    }
}

/* XZ SDK HOST BINDING V1. The manager worker owns files, hashing and processes.
   Render and touch copy its snapshot with a trylock read and never wait on it.
   Host-only runtime fixtures link no manager: these weak references stay unset
   and the panel reports the manager as absent. The device build links mods/sdk. */
#pragma weak xz_sdk_host_start
#pragma weak xz_sdk_host_stop
#pragma weak xz_sdk_host_read
#pragma weak xz_sdk_host_submit
#pragma weak xz_sdk_host_foreground
static int sdk_started;
static struct xz_sdk_ui_session sdk_session;
static struct xz_sdk_snapshot sdk_read;
static char sdk_usb_root[XZ_SDK_PATH];

static int (*stock_uikey_link)(void *);
/* UiKey handlers get their arguments in r0-r3; forwarding all four keeps the stock call exact. */
static int (*stock_uikey_rekordbox)(void *,void *,void *,void *);
static int uikey_rekordbox_hook(void *,void *,void *,void *);
static void *key_player(int deck);
_Static_assert((int)XZ_UI_RB_MODS==(int)XZ_REKORDBOX_MODS&&(int)XZ_UI_RB_STOCK==(int)XZ_REKORDBOX_STOCK&&(int)XZ_UI_RB_INTERFACE==(int)XZ_REKORDBOX_INTERFACE&&
    (int)XZ_UI_RB_VJ_SCREEN==(int)XZ_REKORDBOX_VJ_SCREEN&&(int)XZ_UI_RB_STEMS==(int)XZ_REKORDBOX_STEMS&&(int)XZ_UI_RB_NOTHING==(int)XZ_REKORDBOX_NOTHING&&
    (int)XZ_UI_RB_DECK_VIEW==(int)XZ_REKORDBOX_DECK_VIEW&&(int)XZ_UI_RB_CHOICES==(int)XZ_REKORDBOX_ACTIONS,"one REKORDBOX action numbering");
_Static_assert((int)XZ_LAYOUT_TWO==(int)XZ_DECK_VIEW_TWO&&(int)XZ_LAYOUT_FOUR==(int)XZ_DECK_VIEW_FOUR&&(int)XZ_LAYOUT_MIXER==(int)XZ_DECK_VIEW_MIXER&&
    (int)XZ_LAYOUT_COUNT==(int)XZ_DECK_VIEW_COUNT,"one DECK VIEW numbering");
_Static_assert((int)XZ_UI_ROUTE_NO_ENGINE==(int)XZ_EXTRA_ROUTE_NO_ENGINE&&(int)XZ_UI_ROUTE_NOT_DETECTED==(int)XZ_EXTRA_ROUTE_NOT_DETECTED&&
    (int)XZ_UI_ROUTE_SCANNING==(int)XZ_EXTRA_ROUTE_SCANNING&&(int)XZ_UI_ROUTE_STARTING==(int)XZ_EXTRA_ROUTE_STARTING&&
    (int)XZ_UI_ROUTE_WAITING_NATIVE_STOP==(int)XZ_EXTRA_ROUTE_WAITING_NATIVE_STOP&&(int)XZ_UI_ROUTE_READY==(int)XZ_EXTRA_ROUTE_READY&&
    (int)XZ_UI_ROUTE_FAILED==(int)XZ_EXTRA_ROUTE_FAILED&&(int)XZ_UI_ROUTE_LOST==(int)XZ_EXTRA_ROUTE_LOST&&
    (int)XZ_UI_ROUTE_PART_NONE==(int)XZ_EXTRA_ROUTE_PART_NONE&&(int)XZ_UI_ROUTE_PART_PORT==(int)XZ_EXTRA_ROUTE_PART_PORT&&
    (int)XZ_UI_ROUTE_PART_CABLE==(int)XZ_EXTRA_ROUTE_PART_CABLE&&(int)XZ_UI_ROUTE_PART_SOCKET==(int)XZ_EXTRA_ROUTE_PART_SOCKET&&
    (int)XZ_UI_ROUTE_PART_SELECTOR==(int)XZ_EXTRA_ROUTE_PART_SELECTOR&&
    sizeof(((struct xz_ui_model *)0)->route.reason)==sizeof(((struct xz_extra_route_status *)0)->reason),
    "model.route mirrors struct xz_extra_route_status");

static void arcade_contact_update_locked(unsigned channel,uint16_t key,unsigned op,int consumed){
 if(!key||channel<1||channel>2||(op!=0&&op!=2&&op!=3))return;
 uint16_t *contacts=arcade_contacts[channel-1];int empty=-1;
 for(int i=0;i<64;i++){
  if(contacts[i]==key){if(op==2||op==3){contacts[i]=0;if(arcade_contact_count)arcade_contact_count--;}return;}
  if(!contacts[i]&&empty<0)empty=i;
 }
 if(op==0&&consumed&&empty>=0){contacts[empty]=key;arcade_contact_count++;}
}
static void arcade_input_publish_locked(void){
 __atomic_store_n(&arcade_input_pending,arcade_present||xz_wave_rider_present()||xz_rhythm_lab_present()||arcade_contact_count,__ATOMIC_RELEASE);
}
int xz_ui_runtime_arcade_input_pending(void){return __atomic_load_n(&started,__ATOMIC_ACQUIRE)&&__atomic_load_n(&arcade_input_pending,__ATOMIC_ACQUIRE);}
static void rhythm_lab_to_dj(void) {
    xz_rhythm_lab_close();arcade_input_publish_locked();xz_native_touch_visible(&touch,0);
    __atomic_store_n(&visible,0,__ATOMIC_RELEASE);model.fb_takeover=lab_previous_takeover;
    __atomic_store_n(&fb_takeover_active,model.fb_takeover,__ATOMIC_RELEASE);xz_ui_navigate(&ui,XZ_UI_GAMES);
}
static void wave_rider_to_dj(void) {
    xz_wave_rider_close();arcade_input_publish_locked();
    xz_native_touch_visible(&touch,0);
    __atomic_store_n(&visible,0,__ATOMIC_RELEASE);
    model.fb_takeover=wave_rider_previous_takeover;
    __atomic_store_n(&fb_takeover_active,model.fb_takeover,__ATOMIC_RELEASE);
    xz_ui_navigate(&ui,XZ_UI_GAMES);
}
static void arcade_to_dj(void) {
    arcade_present=0;arcade.leave=0;arcade_input_publish_locked();
    xz_native_touch_visible(&touch,0);
    __atomic_store_n(&visible,0,__ATOMIC_RELEASE);
    model.fb_takeover=arcade_previous_takeover;
    __atomic_store_n(&fb_takeover_active,model.fb_takeover,__ATOMIC_RELEASE);
    xz_ui_navigate(&ui,XZ_UI_EXTRAS);
}
/* Entering the paused screen, or DJ VIEW pressed on it, returns to the deck. */
static int arcade_leaving(int was_music){
    return arcade_present&&(arcade.leave||(!was_music&&arcade.phase==XZ_ARCADE_MUSIC));
}
static int arcade_read_memory(void *unused,uint32_t address,void *out,size_t n){
    (void)unused;return xz_read_memory(address,out,n);
}
static void arcade_clock_tick(uint32_t now){
    if((uint32_t)(now-arcade_clock_poll)<30)return;
    arcade_clock_poll=now;
    if(!arcade_native_clock_checked){arcade_native_clock_ready=xz_arcade_native_clock_valid(arcade_read_memory,NULL);arcade_native_clock_checked=1;}
    if(arcade.clock_deck==3)return;
    if(arcade.clock_deck<3&&arcade_native_clock_ready){
        struct xz_arcade_track_clock sample[2];
        int valid[2]={xz_arcade_native_clock_read(arcade_read_memory,NULL,0,&sample[0]),xz_arcade_native_clock_read(arcade_read_memory,NULL,1,&sample[1])};
        unsigned deck=arcade.clock_deck<2?arcade.clock_deck:arcade.clock_selected;
        if(arcade.clock_deck==2){
            if(valid[1-deck]&&sample[1-deck].playing&&(!valid[deck]||!sample[deck].playing))deck=1-deck;
            else if(!valid[deck]&&valid[1-deck])deck=1-deck;
        }
        if(valid[deck]){xz_arcade_music(&arcade,sample[deck].bpm,sample[deck].bar_phase,sample[deck].playing,sample[deck].track,deck,now);return;}
    }
    if(arcade.clock_deck<3){
        struct xz_arcade_beat packet[2];int valid[2];
        for(unsigned i=0;i<2;i++)valid[i]=xz_arcade_clock_read(i,&packet[i])&&(uint32_t)(now-packet[i].at)<1800;
        unsigned deck=arcade.clock_deck<2?arcade.clock_deck:arcade.clock_selected;
        if(arcade.clock_deck==2&&!valid[deck]&&valid[1-deck])deck=1-deck;
        if(valid[deck]){
            if(packet[deck].sequence!=arcade_clock_sequence[deck]){
                arcade_clock_sequence[deck]=packet[deck].sequence;arcade.clock_selected=deck;
                xz_arcade_sync(&arcade,packet[deck].bpm,packet[deck].bar,packet[deck].at);
            }
            return;
        }
    }
    if(arcade.clock_deck==4||(arcade.clock_deck==2&&!arcade.music_known)){
        float bpm;unsigned tap_mode;
        if(xz_arcade_mixer_clock_read(arcade_read_memory,NULL,&bpm,&tap_mode)){
            (void)tap_mode;xz_arcade_tempo(&arcade,bpm,now);return;
        }
    }
    if(arcade.music_known&&(uint32_t)(now-arcade.music_at)>300)xz_arcade_music_missing(&arcade);
}
static void arcade_tick(void) {
    enum xz_arcade_phase previous=arcade.phase;
    uint32_t now=xz_game_milliseconds();
    arcade_clock_tick(now);
    xz_arcade_step(&arcade,now);
    if(previous!=XZ_ARCADE_MUSIC&&arcade.phase==XZ_ARCADE_MUSIC)arcade_to_dj();
}
int xz_ui_runtime_arcade_key(void *self,const void *input) {
    if(!__atomic_load_n(&started,__ATOMIC_ACQUIRE)||!self||!input)return 0;
    uint16_t key;memcpy(&key,(const unsigned char *)input+8,2);
    if(key==0x4101||(key>=0x4103&&key<=0x4113))return 0;
    if((key==0x4305||key==0x4306)&&!xz_ui_runtime_arcade_input_pending())return 0;
    unsigned channel=((const unsigned char *)self)[0x26];
    if(channel<1||channel>2)return 0;
    pthread_mutex_lock(&ui_mutex);
    int was_music=arcade.phase==XZ_ARCADE_MUSIC;
    if(xz_rhythm_lab_input((int)channel-1,input,xz_game_milliseconds())) {
        if(xz_rhythm_lab_leave_requested())rhythm_lab_to_dj();
        arcade_contact_update_locked(channel,key,((const unsigned char *)input)[11]&15u,1);arcade_input_publish_locked();
        pthread_mutex_unlock(&ui_mutex);return 1;
    }
    if(xz_wave_rider_input((int)channel-1,input,xz_game_milliseconds())) {
        if(xz_wave_rider_leave_requested())wave_rider_to_dj();
        arcade_contact_update_locked(channel,key,((const unsigned char *)input)[11]&15u,1);arcade_input_publish_locked();
        pthread_mutex_unlock(&ui_mutex);return 1;
    }
    int consumed=xz_arcade_input_event(&arcade_input,&arcade,arcade_present,(int)channel-1,input,xz_game_milliseconds());
    if(consumed)xz_arcade_proof_v1.inputs++;
    arcade_contact_update_locked(channel,key,((const unsigned char *)input)[11]&15u,consumed);
    if(arcade_leaving(was_music))arcade_to_dj();
    arcade_input_publish_locked();
    pthread_mutex_unlock(&ui_mutex);return consumed;
}

static int uikey_link_hook(void *arg0) {
    xz_ui_runtime_on_source_key(0);
    return stock_uikey_link ? stock_uikey_link(arg0) : 0;
}

int xz_ui_runtime_stock_source_key(int which, int run) {
    if (!run) return which ? stock_uikey_rekordbox != NULL : stock_uikey_link != NULL;
    /* Both handlers read only the entry's flags word and may clear its state (rbp 1.26: UiKey_rekordbox
       0xe0a50, UiKey_Link 0xdf994); r1-r3 are unused. */
    uint32_t entry[4] = {1, 0, 2, which ? 0xe0a50u : 0xdf994u};
    if (which) { if (!stock_uikey_rekordbox) return -1; stock_uikey_rekordbox(entry, NULL, NULL, NULL); }
    else { if (!stock_uikey_link) return -1; stock_uikey_link(entry); }
    return 0;
}

/* MODERN display preferences stay under ui_mutex, with the other saved values. */
static int modern_browse_columns;
static struct { int modern_wave_color, wave_position, key_display, wave_zoom, usb_colors, hidden_parts; }
    modern_display = {2,0,0,3,0,0};
static struct xz_settings settings_snapshot(void) {
    return (struct xz_settings){.stems=requested_stems,
        .gate=!!(model.enabled & XZ_UI_GATE), .smart=!!(model.enabled & XZ_UI_SMART), .theme=model.theme,
        .ui_interface=model.ui_interface, .classic_theme=model.classic_theme, .modern_theme=model.modern_theme,
        .stem_page=model.stem_page, .shift_pages=model.shift_pages, .pad_feedback=model.pad_feedback, .shift_keysync=model.shift_keysync,
        .fb_takeover=xz_sdk_ui_saved_takeover(&sdk_session,model.fb_takeover), .takeover_assign=model.takeover_assign, .stem_bank=model.stem_bank, .jump=model.jump,
        .stems_overlay=model.stems_overlay, .modern_stems=model.modern_stems, .wave_mode=model.wave_mode,
        .browse_columns={model.browse_columns[0],model.browse_columns[1],model.browse_columns[2]},
        .browse_key_prefix=model.browse_key_prefix, .browse_traffic=model.browse_traffic, .key_format=model.key_format,
        .stem_colors={model.stem_colors[0],model.stem_colors[1],model.stem_colors[2]},.stem_order=model.stem_order,.key_feedback=model.key_feedback,.ram_debug=model.ram_debug,
        .rekordbox_menu=model.rekordbox_assign[0]==XZ_REKORDBOX_MODS,.rekordbox_press=model.rekordbox_assign[0],.rekordbox_shift=model.rekordbox_assign[1],.metrics_enabled=model.metrics_enabled,
        .deck_view=model.deck_view,.deck_view_button=1,.browse_preview=model.browse_preview,
        .band_colors={model.band_colors[0],model.band_colors[1],model.band_colors[2]},
        .modern_wave_color=modern_display.modern_wave_color,.wave_position=modern_display.wave_position,
        .key_display=modern_display.key_display,.wave_zoom=modern_display.wave_zoom,
        .usb_colors=modern_display.usb_colors,.hidden_parts=modern_display.hidden_parts,.modern_browse_columns=modern_browse_columns,.controller_locale=(int)model.controller_locale};
}
/* Profile values that only the UI owns. The backend already rejects invalid profiles. */
static void browse_settings(const struct xz_settings *saved) {
    if (xz_key_browser_columns_valid(saved->browse_columns))
        memcpy(model.browse_columns, saved->browse_columns, sizeof(model.browse_columns));
    model.browse_preview = !!saved->browse_preview;
    model.browse_key_prefix = !!saved->browse_key_prefix; model.browse_traffic = !!saved->browse_traffic;
    model.key_format = saved->key_format >= 0 && saved->key_format <= 2 ? saved->key_format : 0;
    model.metrics_enabled=1;
    if(xz_metrics_enable)xz_metrics_enable(model.metrics_enabled);
    model.ram_debug=1;
    __atomic_store_n(&xz_mods_ram_debug_v1.enabled,model.ram_debug,__ATOMIC_RELEASE);
    model.key_feedback = !!saved->key_feedback;
    __atomic_store_n(&xz_mods_key_feedback_proof_v1.enabled,model.key_feedback,__ATOMIC_RELEASE);
}
#pragma weak xz_layered_band_configure
#pragma weak xz_native_skin_band_colors
/* Stem layers use the saved pad colours: drums, harmonics, vocals. */
static void wave_configure(void) {
    uint32_t colors[3];
    for (int i = 0; i < 3; i++) colors[i] = xz_ui_model_stem_color(&model, i);
    xz_layered_wave_configure(model.wave_mode, colors);
    for(int i=0;i<3;i++)colors[i]=xz_band_palette_color(model.band_colors[i],0);
    if(xz_layered_band_configure)xz_layered_band_configure(colors);
    if(xz_native_skin_band_colors)xz_native_skin_band_colors(colors);
}
/* The interface and classic theme decide the look; every renderer, the shell
   included, reads the derived model.theme. */
_Static_assert((int)XZ_UI_INTERFACE_CLASSIC==(int)XZ_INTERFACE_CLASSIC&&(int)XZ_UI_INTERFACE_MODERN==(int)XZ_INTERFACE_MODERN&&(int)XZ_UI_MODERN_THEMES==(int)XZ_MODERN_THEME_COUNT,"one interface numbering");
/* Interface choice owns drawing; the shared media/SYNC worker remains active in CLASSIC. */
static void shell_follow_interface_locked(void) {
    if(started&&xz_shell_runtime_follow_interface&&xz_shell_runtime_follow_interface(model.ui_interface==XZ_UI_INTERFACE_MODERN))
        xz_log("FLAGSHIP shell unavailable; the restyled stock screen stays");
}
static void appearance_changed(void) {
    __atomic_store_n(&stock_counter_classic,model.ui_interface==XZ_UI_INTERFACE_CLASSIC,__ATOMIC_RELEASE);
    int look = xz_settings_look(model.ui_interface, model.classic_theme);
    if (look == model.theme) return;
    model.theme = look; xz_native_skin_request(look); wave_configure();
    shell_follow_interface_locked();
}
/* The XZ owns applied settings. Browsing nominates a USB without applying it;
   boot restore and explicit profile actions choose settings. A queued edit
   keeps its revision, proven owner and mount destination so a source switch
   cannot lose it or write another user's USB. The XZ remembers the latest edit independently. */
static void settings_claim_owner_locked(const struct xz_usb_profile *profile){
    settings_owner=*profile;settings_owner_valid=!!profile->root[0];
}
static int settings_owner_matches_locked(void){
    return settings_owner_valid&&settings_owner.mount_id==settings_profile.mount_id&&!strcmp(settings_owner.root,settings_profile.root);
}
static void settings_restore_usb_locked(const char *usb,int restored){
    struct xz_settings saved; unsigned present=0;
    int rc=xz_usb_profile_load(&settings_profile,usb,xz_settings_mount_id(usb),&saved,&present);
    if(rc!=-2 && settings_profile.root[0]) {
        snprintf(settings_usb,sizeof(settings_usb),"%s",settings_profile.root);
        if(!strncmp(usb,"/media/usb1/",12)) settings_slot=0;
        else if(!strncmp(usb,"/media/usb2/",12)) settings_slot=1;
        settings_target_empty=rc==1;
        struct xz_settings merged=settings_snapshot();
        if(!rc&&!xz_settings_overlay(&merged,&saved,present)){
            profile_apply_locked(&merged,"boot-usb");settings_claim_owner_locked(&settings_profile);
            model.settings_status="SETTINGS RESTORED FROM USB";
        }
        if(rc<0)model.settings_status="INVALID USB SETTINGS: KEPT CURRENT";
        else if(rc&&!restored)model.settings_status="USB SETTINGS READY";
    }
}
#pragma weak xz_extra_decks_set_performance_settings
#pragma weak xz_extra_decks_jump_snapshot
static void performance_publish_locked(void){
 if(xz_extra_decks_set_performance_settings){
  struct xz_performance_settings settings={.revision=settings_revision,.jump=model.jump,
   .gate=!!(model.enabled&XZ_UI_GATE),.smart=!!(model.enabled&XZ_UI_SMART)};
  xz_extra_decks_set_performance_settings(&settings);
 }
}
int xz_ui_runtime_jump_snapshot(int deck,struct xz_jump_snapshot *out){
 if(!out||(unsigned)deck>=4)return 0;
 if(deck>=2)return xz_extra_decks_jump_snapshot?xz_extra_decks_jump_snapshot(deck-2,out):0;
 pthread_mutex_lock(&ui_mutex);*out=xz_jump_snapshot(&model.jump,jump_state.page[deck]);
 pthread_mutex_unlock(&ui_mutex);return 1;
}
/* ---- DECK VIEW (saved), the pad page per deck, and decks 3/4's route ---- */
static unsigned layout_seen; /* the xz_deck_layout_revision the settings hold */
static void settings_queue(void);
/* The saved DECK VIEW setting. xz_deck_layout_get() returns view from here on, every frame, and settings_queue
   saves it. Every view change reaches it through deck_view_apply (play-screen picker and card, MODS, the button
   next to LINK, the retired wave order rows). ui_mutex held. */
static void deck_view_save_locked(int view){
    if((unsigned)view>=XZ_DECK_VIEW_COUNT)return;
    xz_deck_layout_set(view);layout_seen=xz_deck_layout_revision();
    model.deck_view=view;model.four_wave_mixer=view==XZ_DECK_VIEW_MIXER;
    settings_queue();
}
/* Any other xz_deck_layout_set is saved like a MODS change on the next refresh. */
static void deck_view_follow_locked(void){
    unsigned now=xz_deck_layout_revision();
    if(now==layout_seen)return;
    layout_seen=now;
    int view=xz_deck_layout_get();
    if(view==model.deck_view)return;
    model.deck_view=view;model.four_wave_mixer=view==XZ_DECK_VIEW_MIXER;settings_queue();
}
/* Decks 3 and 4's BEAT JUMP pad page: the software player's own, through the parity branch's snapshot (no
   ui_mutex for logical decks 2 and 3). -1 while their players do not run. */
static int extra_jump_page(int logical_deck){
    struct xz_jump_snapshot s;
    return xz_ui_runtime_jump_snapshot(logical_deck,&s)?(int)s.page:-1;
}
/* Decks 3 and 4's route: the connector's published status, copied field for field (no IO, no lock). Without
   the connector in this build every field stays zero: NO_ENGINE. */
/* The player's live window for the dock (stock_window), and CLASSIC's calm card over the E-8307 hub window, which
   the loopback cable raises and is never an error (ui/native_raw_skin.c). */
static char stock_window_copy[XZ_STOCK_WINDOW_TEXT];
static int hub_calm_shown;
static void stock_window_refresh_locked(void){
    model.stock_window=xz_stock_window_text&&xz_stock_window_text(stock_window_copy)?stock_window_copy:NULL;
    int calm=xz_stock_usb_hub_text&&xz_stock_usb_hub_text()!=NULL;
    if(calm!=hub_calm_shown){
        hub_calm_shown=calm;
        if(xz_native_raw_skin_hub_calm)xz_native_raw_skin_hub_calm(calm);
        xz_native_skin_request_repaint();
    }
}
/* The number of a window's E-code ("E-8304: ..." gives 8304), or 0. */
static unsigned stock_window_code(const char *t){
    unsigned n=0;
    if(!t||t[0]!='E'||t[1]!='-')return 0;
    for(int i=2;i<6;i++){if(t[i]<'0'||t[i]>'9')return 0;n=n*10+(unsigned)(t[i]-'0');}
    return n;
}
static void route_refresh_locked(void){
    struct xz_extra_route_status s={0};
    if(xz_extra_decks_route_status)s=xz_extra_decks_route_status();
    model.route.state=(int)s.state;model.route.fail_part=(int)s.fail_part;
    memcpy(model.route.reason,s.reason,sizeof model.route.reason);model.route.reason[sizeof model.route.reason-1]=0;
    model.route.checked_at_ms=s.checked_at_ms;model.route.revision=s.revision;
    model.route.ready_revision=s.ready_revision;
    stock_window_refresh_locked();
}
static void deck_status_refresh_locked(void){
    deck_view_follow_locked();
    if(!xz_shell_runtime_look||!xz_shell_runtime_look(&model.modern_look))memset(&model.modern_look,0,sizeof model.modern_look);
    for(int deck=0;deck<4;deck++)model.jump_page[deck]=deck<2?jump_state.page[deck]:extra_jump_page(deck);
    route_refresh_locked();
}
static void settings_queue(void) {
    settings_revision++;performance_publish_locked();
    if (settings_readonly) { model.settings_status = "CHANGES LAST UNTIL REBOOT"; return; }
    if (!settings_running) { model.settings_status = "SETTINGS SAVE UNAVAILABLE"; return; }
    if (!settings_profile.root[0] && !xz_settings_internal_save) { model.settings_status = "SETTINGS NOT SAVED: INSERT A USB"; return; }
    /* Pin the values and user when the action happens, before another USB can load. */
    settings_request.profile = settings_profile;
    settings_request.values = settings_snapshot();
    settings_request.revision = ++settings_save_revision;
    settings_request.ui_revision = settings_revision;
    int selected=xz_ui_runtime_profile_selected();
    settings_request.usb_writable=(selected<0||selected==settings_slot)&&((settings_target_empty&&!settings_owner_valid)||settings_owner_matches_locked());
    settings_request.slot=settings_slot;
    model.settings_status = settings_profile.root[0]&&settings_request.usb_writable ? "SAVING SETTINGS TO USB" : "SAVING SETTINGS ON THIS XZ";
    settings_pending = 1; pthread_cond_signal(&settings_changed);
}
/* Shows the save result in the MODS footer for a few seconds on every page. */
static void settings_report(const char *status) {
    model.settings_status = status;
    settings_flash_at = xz_game_milliseconds() | 1u;
}
static void profile_status(int slot, int rc) {
    snprintf(settings_message,sizeof(settings_message),"USB %d %s",slot+1,
        rc < 0 ? "SETTINGS INVALID: KEPT CURRENT" : rc ? "HAS NO SETTINGS: KEPT CURRENT" : settings_owner_matches_locked()?"SAVE TARGET READY":"PROFILE KEPT");
    model.settings_status = settings_readonly ? "CHANGES LAST UNTIL REBOOT" : settings_message;
}
static void *settings_writer(void *unused) {
    (void)unused;
    pthread_mutex_lock(&ui_mutex);
    while (settings_running || settings_pending) {
        int selected = xz_ui_runtime_profile_selected();
        int previous_selected = selected;
        int previous_slot = settings_slot;
        struct xz_usb_profile previous = settings_profile;
        pthread_mutex_unlock(&ui_mutex);
        char root[1024] = {0}; unsigned long mount_id = 0;
        if (selected >= 0) {
            if (xz_settings_find_usb((unsigned)selected,root)) root[0]=0;
        } else if (previous.root[0]) {
            snprintf(root,sizeof(root),"%s",previous.root);
            selected=previous_slot;
        } else {
            for (int slot=0;slot<2;slot++) if (!xz_settings_find_usb((unsigned)slot,root)) { selected=slot; break; }
        }
        if (root[0]) {
            struct stat volume;
            if (stat(root,&volume)||!S_ISDIR(volume.st_mode)) root[0]=0;
            else {
                mount_id=xz_settings_mount_id(root);
                if(!mount_id&&!strncmp(root,"/media/",7))root[0]=0;
            }
        }
        int switched=strcmp(root,previous.root)||mount_id!=previous.mount_id||selected!=previous_slot;
        struct xz_usb_profile next=previous; struct xz_settings loaded; xz_settings_default(&loaded);
        int load_result=0; unsigned present=0;
        if (switched) {
            if(root[0]) load_result=xz_usb_profile_load(&next,root,mount_id,&loaded,&present);
            else xz_usb_profile_removed(&next);
        }
        pthread_mutex_lock(&ui_mutex);
        /* A source selection during disk I/O is handled in the next poll. */
        if(switched && previous_selected==xz_ui_runtime_profile_selected() &&
            previous.generation==settings_profile.generation && next.generation!=previous.generation && load_result!=-2) {
            settings_profile=next; settings_slot=selected;
            settings_target_empty=next.root[0]&&load_result==1;
            snprintf(settings_usb,sizeof(settings_usb),"%s",next.root);
            /* Browsing only changes the save destination. Boot and explicit
               profile actions own restoration; painting a track list must not
               change live controls, levels, or readiness. Keep any queued edit
               so it can still save to the XZ if its USB destination changed. */
            if(next.root[0]) profile_status(selected,load_result);
            else model.settings_status=settings_readonly?"CHANGES LAST UNTIL REBOOT":"USB REMOVED: SETTINGS KEPT";
        }
        if(settings_pending) {
            struct xz_usb_profile target=settings_request.profile;
            struct xz_settings saved=settings_request.values;
            uint64_t revision=settings_request.revision;
            uint64_t ui_revision=settings_request.ui_revision;
            int writable=settings_request.usb_writable;
            int current=revision==settings_save_revision;
            settings_pending=0;
            int current_slot=xz_ui_runtime_profile_selected();
            int same_user=(current_slot<0||current_slot==settings_slot) && target.generation==settings_profile.generation &&
                !strcmp(target.root,settings_profile.root) && target.mount_id==settings_profile.mount_id;
            int slot=settings_request.slot;
            pthread_mutex_unlock(&ui_mutex);
            /* The captured owner and mount identity, not the current browse source,
               authorize this write. save_mount rechecks the physical mount. */
            int usb_rc=!current?-1:!target.root[0]?1:!writable?-1:xz_usb_profile_save(&target,target.generation,&saved);
            int internal_rc=current&&xz_settings_internal_save?xz_settings_internal_save(&saved):XZ_INTERNAL_SETTINGS_UNAVAILABLE;
            pthread_mutex_lock(&ui_mutex);
            if(usb_rc==0){
                if(target.generation==settings_profile.generation)settings_target_empty=0;
                if(current&&revision==settings_save_revision&&ui_revision==settings_revision)settings_claim_owner_locked(&target);
            }
            if(!settings_pending && current && revision==settings_save_revision && ui_revision==settings_revision) {
                if(usb_rc==0) snprintf(settings_message,sizeof(settings_message),"SAVED TO USB %d",slot+1);
                else if(target.root[0]&&!writable&&same_user) snprintf(settings_message,sizeof(settings_message),"%s",internal_rc==XZ_INTERNAL_SETTINGS_OK?"SAVED ON THIS XZ / USB PROFILE KEPT":"NOT SAVED: USB PROFILE KEPT");
                else if(!same_user&&internal_rc==XZ_INTERNAL_SETTINGS_OK) snprintf(settings_message,sizeof(settings_message),"SAVED ON THIS XZ / USB CHANGED");
                else if(usb_rc<0) snprintf(settings_message,sizeof(settings_message),"NOT SAVED TO USB %d: CHECK USB",slot+1);
                else snprintf(settings_message,sizeof(settings_message),"%s",
                    internal_rc==XZ_INTERNAL_SETTINGS_OK?"SAVED ON THIS XZ / NO USB":"NOT SAVED: INSERT A USB");
                settings_report(settings_message);
            }
        }
        if(settings_running&&!settings_pending) {
            struct timeval now; gettimeofday(&now,NULL);
            struct timespec until={now.tv_sec+1,now.tv_usec*1000};
            pthread_cond_timedwait(&settings_changed,&ui_mutex,&until);
        }
    }
    pthread_mutex_unlock(&ui_mutex); return NULL;
}


/* PC STEM HOST BINDING V1. Source and gestures stay separate from local audio. */
static struct xz_pc_stems pc_stems;
static pthread_mutex_t pc_mutex=PTHREAD_MUTEX_INITIALIZER;
static int pc_observing,pc_present,pc_map_confirmed[4],pc_setup_step[2],pc_last_link;
static void inline_layout_publish(void) {
    if((touch.visible||model.ui_interface==XZ_UI_INTERFACE_MODERN||(model.fb_takeover&&model.connection.connected))&&
       xz_native_four_detail&&xz_native_four_set_detail&&xz_native_four_detail()>=0)xz_native_four_set_detail(-1);
    if((touch.visible||model.ui_interface==XZ_UI_INTERFACE_MODERN||(model.fb_takeover&&model.connection.connected))&&
       xz_classic_mixer_runtime_hide)xz_classic_mixer_runtime_hide();
    int layout=(unsigned)model.stems_overlay<XZ_STEM_LAYOUT_COUNT?model.stems_overlay:XZ_STEM_LAYOUT_HIDDEN;
    __atomic_store_n(&inline_requested,(requested_stems||pc_present)&&!touch.visible?layout:XZ_STEM_LAYOUT_HIDDEN,__ATOMIC_RELEASE);
}
/* ---- MODS ----
   One menu, this panel, in both interfaces: MODERN draws it in the FLAGSHIP skin over the shell (the shell is
   not eligible while it shows, shell_eligible_locked). Closing it in MODERN also closes the shell's UTILITY when
   it was under the panel, so MODS always closes to the screen it was opened from. */
/* REPORT AN ISSUE: the screen under MODS when it opened, as an enum only (native_support.h). */
static int report_before=XZ_SUPPORT_AT_UNKNOWN;
static enum xz_menu_place classic_menu_place_locked(void);
static int report_place_locked(void){
    if(model.ui_interface==XZ_UI_INTERFACE_MODERN){
        switch(xz_shell_runtime_screen?xz_shell_runtime_screen():-1){
        case XZ_SHELL_PLAY:return XZ_SUPPORT_AT_PLAY;
        case XZ_SHELL_BROWSE:return XZ_SUPPORT_AT_BROWSE;
        case XZ_SHELL_PLAYLIST:return XZ_SUPPORT_AT_PLAYLIST;
        case XZ_SHELL_TAG_LIST:return XZ_SUPPORT_AT_TAG_LIST;
        case XZ_SHELL_SEARCH:return XZ_SUPPORT_AT_SEARCH;
        case XZ_SHELL_INFO:return XZ_SUPPORT_AT_INFO;
        case XZ_SHELL_SHORTCUT:return XZ_SUPPORT_AT_SHORTCUT;
        case XZ_SHELL_UTILITY:return XZ_SUPPORT_AT_UTILITY;
        case XZ_SHELL_SOURCE:return XZ_SUPPORT_AT_SOURCE;
        default:return XZ_SUPPORT_AT_UNKNOWN;}
    }
    switch(classic_menu_place_locked()){
    case XZ_MENU_AT_PLAY:return XZ_SUPPORT_AT_PLAY;
    case XZ_MENU_AT_STOCK_MENU:return XZ_SUPPORT_AT_STOCK_MENU;
    case XZ_MENU_AT_TRACK_LIST:return XZ_SUPPORT_AT_TRACK_LIST;
    case XZ_MENU_AT_TAG_LIST:return XZ_SUPPORT_AT_TAG_LIST;
    case XZ_MENU_AT_GRID_ADJUST:return XZ_SUPPORT_AT_GRID_ADJUST;
    case XZ_MENU_AT_SOURCE:return XZ_SUPPORT_AT_SOURCE;
    case XZ_MENU_AT_INFO:return XZ_SUPPORT_AT_INFO;
    case XZ_MENU_AT_SHORTCUT:return XZ_SUPPORT_AT_SHORTCUT;
    case XZ_MENU_AT_UTILITY:return XZ_SUPPORT_AT_UTILITY;
    case XZ_MENU_AT_TAKEOVER:return XZ_SUPPORT_AT_TAKEOVER;
    case XZ_MENU_AT_GAME:return XZ_SUPPORT_AT_GAME;
    case XZ_MENU_AT_OTHER:return XZ_SUPPORT_AT_OTHER;
    default:return XZ_SUPPORT_AT_UNKNOWN;}
}
static void panel_show_locked(void){
    if(!touch.visible)report_before=report_place_locked();
    play_key.open=0;play_key_dirty=1;__atomic_store_n(&play_key_visible,0,__ATOMIC_RELEASE);
    xz_native_touch_visible(&touch,1);__atomic_store_n(&visible,1,__ATOMIC_RELEASE);
}
/* A setup that ends back on the play screen (to_play) closes MODS. */
static void panel_hide_locked(int to_play){
    xz_native_touch_visible(&touch,0);__atomic_store_n(&visible,0,__ATOMIC_RELEASE);
    if(to_play&&model.ui_interface==XZ_UI_INTERFACE_MODERN&&xz_shell_runtime_close_mods)xz_shell_runtime_close_mods();
}
static uint32_t pc_link_epoch=1;
static unsigned pc_available_roles;
static struct {int valid,target;uint32_t epoch;unsigned known,active;} pc_test[2];
static void (*stock_pc_midi)(void *,void *,const void *);
static int pc_previous_source[2],pc_previous_target[2]={-1,-1};
static uint32_t pc_previous_epoch[2];
static void pc_midi_hook(void *self,void *input,const void *message){
    if(stock_pc_midi)stock_pc_midi(self,input,message);
    pthread_mutex_lock(&pc_mutex);
    struct xz_pc_stem_state before[XZ_PC_DECKS];memcpy(before,pc_stems.deck,sizeof(before));
    int accepted=xz_pc_stems_native_feedback(&pc_stems,message),changed=0;
    if(accepted)for(unsigned d=0;d<XZ_PC_DECKS;d++){
        const struct xz_pc_stem_state *after=&pc_stems.deck[d];
        if(before[d].known!=after->known||before[d].active!=after->active||
           before[d].pending!=after->pending||before[d].caps!=after->caps)changed=1;
    }
    pthread_mutex_unlock(&pc_mutex);
    if(changed)xz_native_skin_request_repaint();
}
static int pc_source(unsigned deck,struct xz_pc_stem_source *out){
    return deck<2&&xz_pc_stems_native_source(deck,out)&&(!out->pc||(out->slot>=0&&out->slot<4));
}
static int local_stem_source(int deck){
    struct xz_pc_stem_source source;
    return deck>=0&&deck<2&&pc_source((unsigned)deck,&source)&&!source.pc;
}
static void pc_refresh_locked(void){
    pc_available_roles=xz_pc_wave_available_roles();
    struct xz_pc_stem_source source[2];int valid[2],connected[4]={0};uint32_t epochs[4]={0};
    int link=xz_pc_stems_native_ready();
    pthread_mutex_lock(&pc_mutex);
    if(pc_last_link&&!link){memset(pc_map_confirmed,0,sizeof(pc_map_confirmed));pc_link_epoch++;}
    pc_last_link=link;
    for(unsigned d=0;d<2;d++){
        valid[d]=pc_source(d,&source[d]);
        if(valid[d]&&source[d].pc&&source[d].slot>=0){
            unsigned target=(unsigned)source[d].slot;
            epochs[target]=source[d].epoch;
            connected[target]=link&&pc_observing&&source[d].linked&&source[d].rekordbox&&source[d].loaded;
        }
    }
    for(unsigned target=0;target<4;target++){
        xz_pc_stems_session(&pc_stems,target,connected[target],((uint64_t)pc_link_epoch<<32)|epochs[target]);
        if(connected[target]&&pc_map_confirmed[target])xz_pc_stems_verify(&pc_stems,target);
    }
    xz_pc_stems_tick(&pc_stems,xz_game_milliseconds());
    pc_present=0;
    for(unsigned d=0;d<2;d++){
        int kind=valid[d]?(source[d].pc?XZ_UI_SOURCE_PC:XZ_UI_SOURCE_LOCAL):XZ_UI_SOURCE_UNKNOWN;
        int target=valid[d]?source[d].slot:-1;
        uint32_t epoch=valid[d]?source[d].epoch:0;
        if(pc_previous_source[d]!=kind||pc_previous_target[d]!=target||pc_previous_epoch[d]!=epoch){
            struct xz_ui_action cancelled[XZ_UI_ACTIONS];
            if(inline_contact&&inline_contact_deck==(int)d){xz_ui_cancel(&inline_ui,cancelled);inline_cancelled=1;}
            if(ui.deck==(int)d){xz_ui_cancel(&ui,cancelled);if(touch.capture)touch.opening_contact=1;}
            pc_test[d].valid=0;
            pc_previous_source[d]=kind;pc_previous_target[d]=target;pc_previous_epoch[d]=epoch;
            if(kind!=XZ_UI_SOURCE_LOCAL)pc_setup_step[d]=XZ_UI_PC_STEP_DETECT;
        }
        struct xz_ui_pc_stems *out=&model.deck[d].pc_stems;
        *out=(struct xz_ui_pc_stems){.source=kind,.target=target,.epoch=epoch,.available=pc_available_roles,.setup_step=pc_setup_step[d]};
        if(kind!=XZ_UI_SOURCE_LOCAL)pc_present=1;
        if(target>=0&&target<4){
            struct xz_pc_stem_state *state=&pc_stems.deck[target];
            out->connected=state->connected;out->verified=state->verified;out->caps=state->caps;
            out->known=state->known;out->active=state->active;out->pending=state->pending;
        }
        if(!valid[d])out->reason="PLAYER SOURCE IS NOT READABLE";
        else if(kind==XZ_UI_SOURCE_PC&&!pc_observing)out->reason="PC SOURCE OBSERVER IS UNAVAILABLE";
        else if(kind==XZ_UI_SOURCE_PC&&!source[d].linked)out->reason="RECONNECT THE COMPUTER BY USB";
        else if(kind==XZ_UI_SOURCE_PC&&!source[d].rekordbox)out->reason="OPEN THE DJ SOFTWARE IN PERFORMANCE MODE";
        else if(kind==XZ_UI_SOURCE_PC&&!source[d].loaded)out->reason="LOAD AN ANALYZED TRACK IN THE DJ SOFTWARE";
        else if(kind==XZ_UI_SOURCE_PC&&!out->verified)out->reason="IMPORT THE XZ STEMS MAP AND RUN THE LIVE TEST";
    }
    pthread_mutex_unlock(&pc_mutex);
    inline_layout_publish();
}
static int pc_action_current(const struct xz_ui_action *a,struct xz_pc_stem_source *source){
    if(a->deck<0||a->deck>1||!pc_source((unsigned)a->deck,source)||!source->pc||source->slot<0||
       !source->linked||!source->rekordbox||!source->loaded||!pc_observing||!xz_pc_stems_native_ready())return 0;
    if(a->pc_target!=source->slot||a->pc_epoch!=source->epoch)return 0;
    return 1;
}
static void pc_apply(const struct xz_ui_action *a){
    if(a->deck<0||a->deck>1||a->phase!=XZ_UI_PRESS)return;
    int d=a->deck;struct xz_pc_stem_source source;
    if(a->kind==XZ_UI_PC_SETUP){
        xz_ui_navigate(&ui,XZ_UI_PC_STEMS);ui.deck=d;
        snprintf(ui.notice,sizeof(ui.notice),"%s",xz_ui_pc_reason_text((enum xz_ui_pc_reason)a->index));
        panel_show_locked();
        pc_setup_step[d]=XZ_UI_PC_STEP_DETECT;pc_refresh_locked();return;
    }
    if(a->kind==XZ_UI_PC_CHECK){
        pc_refresh_locked();
        struct xz_ui_pc_stems *state=&model.deck[d].pc_stems;
        if(state->source!=XZ_UI_SOURCE_PC||!state->connected){snprintf(ui.notice,sizeof(ui.notice),"%s",state->reason?state->reason:"SELECT A DECK PLAYING FROM THE PC");return;}
        if(a->index==XZ_UI_PC_STEP_DONE)pc_setup_step[d]=XZ_UI_PC_STEP_DETECT;
        else if(pc_setup_step[d]==XZ_UI_PC_STEP_DETECT)pc_setup_step[d]=XZ_UI_PC_STEP_MAPPING;
        else if(pc_setup_step[d]==XZ_UI_PC_STEP_MAPPING)pc_setup_step[d]=XZ_UI_PC_STEP_TEST;
        pc_refresh_locked();return;
    }
    if(a->kind==XZ_UI_PC_TOGGLE&&a->phase==XZ_UI_PRESS){
        pc_refresh_locked();
        if(a->index<0||a->index>=XZ_PC_ROLES||!(pc_available_roles&(1u<<a->index))){snprintf(ui.notice,sizeof(ui.notice),"BASS IS PART OF INST / TURN ON 4 STEMS IN THE DJ SOFTWARE");return;}
        if(!pc_action_current(a,&source)){snprintf(ui.notice,sizeof(ui.notice),"PC SOURCE OR TRACK CHANGED / CHECK AGAIN");return;}
        pthread_mutex_lock(&pc_mutex);
        int sent=xz_pc_stems_toggle(&pc_stems,(unsigned)source.slot,(unsigned)a->index,xz_game_milliseconds());
        pthread_mutex_unlock(&pc_mutex);
        snprintf(ui.notice,sizeof(ui.notice),"%s",sent?"STEM COMMAND SENT / WAITING FOR THE DJ SOFTWARE":"STEM COMMAND NOT SENT / CHECK THE PC LINK");
        pc_refresh_locked();return;
    }
    if(a->kind==XZ_UI_PC_TEST&&a->phase==XZ_UI_PRESS){
        pc_refresh_locked();
        if(!pc_action_current(a,&source)){snprintf(ui.notice,sizeof(ui.notice),"PC SOURCE OR TRACK CHANGED / CHECK AGAIN");return;}
        pthread_mutex_lock(&pc_mutex);
        struct xz_pc_stem_state *state=&pc_stems.deck[source.slot];
        pc_test[d].valid=1;pc_test[d].target=source.slot;pc_test[d].epoch=source.epoch;
        pc_test[d].known=state->known;pc_test[d].active=state->active;
        int was_verified=state->verified;xz_pc_stems_verify(&pc_stems,(unsigned)source.slot);
        int sent=xz_pc_stems_toggle(&pc_stems,(unsigned)source.slot,XZ_PC_DRUMS,xz_game_milliseconds());
        if(!was_verified){state->verified=0;state->caps&=~XZ_PC_TOGGLE;}
        pthread_mutex_unlock(&pc_mutex);
        if(!sent){pc_test[d].valid=0;snprintf(ui.notice,sizeof(ui.notice),"TEST NOT SENT / CHECK MIDI MAPPING AND USB");return;}
        pc_setup_step[d]=XZ_UI_PC_STEP_CONFIRM;snprintf(ui.notice,sizeof(ui.notice),"TEST SENT / CHECK DRUMS IN THE DJ SOFTWARE");
        pc_refresh_locked();return;
    }
    if(a->kind==XZ_UI_PC_CONFIRM&&a->phase==XZ_UI_PRESS){
        if(!a->index){pc_setup_step[d]=XZ_UI_PC_STEP_TEST;pc_test[d].valid=0;pc_refresh_locked();return;}
        pc_refresh_locked();
        if(!pc_test[d].valid||!pc_action_current(a,&source)||pc_test[d].target!=source.slot||pc_test[d].epoch!=source.epoch){
            snprintf(ui.notice,sizeof(ui.notice),"PC SOURCE OR TRACK CHANGED / RUN THE TEST AGAIN");return;
        }
        pthread_mutex_lock(&pc_mutex);
        pc_map_confirmed[source.slot]=1;xz_pc_stems_verify(&pc_stems,(unsigned)source.slot);
        pthread_mutex_unlock(&pc_mutex);
        pc_setup_step[d]=XZ_UI_PC_STEP_DONE;pc_test[d].valid=0;
        snprintf(ui.notice,sizeof(ui.notice),"ONSCREEN STEMS CONTROL THE DJ SOFTWARE");pc_refresh_locked();
    }
}

static void levels(int deck) {
    if (deck < 0 || deck > 1) return;
    struct xz_ui_deck *d = &model.deck[deck];
    float value[4] = {1, 1, 1, 1};
    if (!d->bypass) {
        for(unsigned r=0;r<(unsigned)xz_stem_count(d->stem_count);r++)value[r]=d->muted&(1u<<r)?0:d->levels[r];
    }
    (void)xz_audio_set_levels4_for_generation(deck,displayed_generation[deck],value);
}

/* Always-visible key strip per deck, and the inline Shift+Sync error it shows for 4 s. */
static struct xz_key_strip_deck key_strip[2];
static _Thread_local struct xz_key_frame_cache key_frame_cache;
static _Thread_local struct {
    int valid,theme,layout,vj,takeover,shortcut,selected,ram_debug,ram_valid;
    uint32_t ram_free_mib;
    int metrics_enabled;struct xz_metrics_sample metrics;
    uint32_t scene,colors[3];
    struct xz_ui_transient_notice notice;
    int dv,dv_view,dv_ready,dv_open,dv_tile,dv_focus,dv_pair; /* the DECK VIEW button and picker badge() painted */
} badge_frame_cache;
static struct xz_ui_transient_notice async_notice;
static _Thread_local struct xz_ui_notice_pixels notice_pixels;
/* A STEMS button notice where the DJ is looking: the shell's own notice in MODERN, the badge in CLASSIC or over
   MODS. Under ui_mutex. */
static void stems_notice_locked(const char *text){
    if(!text)return;
    if(model.ui_interface==XZ_UI_INTERFACE_MODERN&&!touch.visible&&xz_shell_runtime_notice){xz_shell_runtime_notice(text);return;}
    xz_ui_notice_set(&async_notice,text,xz_game_milliseconds());
    snprintf(ui.notice,sizeof ui.notice,"%s",async_notice.text);
}
void xz_ui_runtime_notice(const char *text){
    pthread_mutex_lock(&ui_mutex);
    xz_ui_notice_set(&async_notice,text,xz_game_milliseconds());
    snprintf(ui.notice,sizeof ui.notice,"%s",async_notice.text);
    pthread_mutex_unlock(&ui_mutex);
    xz_native_skin_request_repaint();
}
__attribute__((visibility("default"))) struct {
    uint32_t version,busy,cached;
} xz_mods_key_present_proof_v1={.version=1};
static char strip_error[2][40];
static uint32_t strip_error_until[2];
static int key_sync_fail(int deck,enum xz_key_sync_failure failure) {
    xz_key_sync_failure_text(failure,deck,strip_error[deck]);
    strip_error_until[deck]=xz_game_milliseconds()+4000u;if(!strip_error_until[deck])strip_error_until[deck]=1;
    snprintf(ui.notice,sizeof(ui.notice),"D%d KEY SYNC: %s",deck+1,strip_error[deck]);
    __atomic_add_fetch(&xz_mods_key_strip_proof_v1.errors[deck],1,__ATOMIC_RELAXED);
    return 0;
}
static int (*native_master_tempo_on)(void *,int);
/* Same local UI manager/array and +0x138 innards path used by ram/runtime.c. */
static void *key_player(int deck) {
    uint32_t manager,array,count,player,innards;unsigned char channel;
    if(deck<0||deck>1||xz_read_memory(0x1c8a308,&manager,4)||manager<0x10000||(manager&3)||manager>UINT32_MAX-0x4c||
       xz_read_memory(manager+0x40,&array,4)||xz_read_memory(manager+0x48,&count,4)||count<2||
       array<0x10000||(array&3)||array>UINT32_MAX-8||xz_read_memory(array+deck*4,&player,4)||
       player<0x10000||(player&3)||player>UINT32_MAX-0x13c||xz_read_memory(player+0x138,&innards,4)||
       innards<0x10000||(innards&3)||innards>UINT32_MAX-0x34||xz_read_memory(innards+0x26,&channel,1)||channel!=deck+1)return NULL;
    return (void *)(uintptr_t)innards;
}
static int key_sync_end(int deck,void *player,int reset) {
    if(!key_sync[deck].valid)return 1;
    if(!player||!native_master_tempo_on)return 0;
    /* Clear only this toggle's request, including while key metadata is temporarily unavailable. */
    if(reset)(void)xz_key_clear_desired_cents(deck,key_sync[deck].epoch,key_sync[deck].cents);
    /* 1.26 boolean setter at 0x289790 forwards 0 and 1 unchanged. */
    native_master_tempo_on(player,key_sync[deck].mt_before);
    __atomic_fetch_and(&key_sync_led_mask,~(1u<<deck),__ATOMIC_RELEASE);
    key_sync[deck].valid=0;model.deck[deck].key_synced=key_strip[deck].synced=0;
    strip_error_until[deck]=0;return 1;
}
static uint32_t key_notice_seen[2];
static pthread_mutex_t key_projection_mutex=PTHREAD_MUTEX_INITIALIZER;
static struct { struct xz_deck_key master;int deck,format,feedback,traffic;uint32_t at; } key_projection;
void xz_key_project_extra(struct xz_extra_decks *extra){
    struct xz_deck_key master;int md,format,feedback,traffic;uint32_t at;
    pthread_mutex_lock(&key_projection_mutex);
    master=key_projection.master;md=key_projection.deck;format=key_projection.format;
    feedback=key_projection.feedback;traffic=key_projection.traffic;at=key_projection.at;
    pthread_mutex_unlock(&key_projection_mutex);
    int valid=master.playing_valid&&(uint32_t)(xz_game_milliseconds()-at)<=250u&&extra->master_actual.deck==md;
    if(valid&&md<2)valid=xz_audio_output_epoch&&xz_audio_output_epoch((unsigned)md)==master.owner.native_epoch;
    if(valid&&md>=2)valid=xz_key_owner_equal(md,master.owner,extra->deck[md-2].key_state.owner);
    for(int i=0;i<2;i++){
        struct xz_extra_deck *d=&extra->deck[i];d->key_feedback=feedback;d->key_color=0;
        if(d->key_state.metadata_valid)xz_music_key_format(d->key_state.original,format,d->original_display);
        if(d->key_state.playing_valid)xz_music_key_format(d->key_state.playing.key,format,d->playing_key);
        if(valid&&traffic&&md!=i+2&&d->key_state.playing_valid){
            enum xz_key_match m=xz_key_match(d->key_state.playing,master.playing);
            d->key_color=m==XZ_KEY_COMPATIBLE?0x52d794:m==XZ_KEY_DETUNED?0xffbd59:0;
        }
    }
}
static int deck_key_read(int deck,struct xz_deck_key *out,struct xz_key_snapshot *native){
    memset(out,0,sizeof *out);
    if((unsigned)deck>=4)return 0;
    if(deck>=2){
        struct xz_extra_decks extra={0};
        if(!xz_extra_decks_snapshot||!xz_extra_decks_snapshot(&extra))return 0;
        const struct xz_extra_deck *d=&extra.deck[deck-2];
        if(!d->loaded||!(d->known&XZ_EXTRA_KNOWN(XZ_EXTRA_F_KEY_STATE)))return 0;
        *out=d->key_state;return out->engine_ready;
    }
    struct xz_key_snapshot s;
    if(model.deck[deck].pc_stems.source!=XZ_UI_SOURCE_LOCAL||!xz_key_native_snapshot((unsigned)deck,&s))return 0;
    if(native)*native=s;
    out->owner.native_epoch=s.epoch;out->original=s.original;out->metadata_valid=1;
    out->engine_ready=key_available;out->master_tempo=s.master_tempo;out->engaged=key_sync[deck].valid;out->target_deck=key_sync[deck].target_deck;
    xz_key_get_desired_cents(deck,s.epoch,&out->desired_cents);
    out->playing_valid=!xz_key_get_applied_cents(deck,s.epoch,&out->applied_cents)&&
        xz_key_playing(&s,out->applied_cents,&out->playing);
    out->audio_state=out->playing_valid&&xz_key_has_applied&&xz_key_has_applied(deck,s.epoch)?XZ_KEY_AUDIO_APPLIED:XZ_KEY_AUDIO_PREPARED;
    if(out->audio_state==XZ_KEY_AUDIO_PREPARED&&out->desired_cents)out->playing_valid=0;
    return 1;
}
static struct xz_master_observation key_master(void){
    struct xz_master_observation m={.deck=XZ_MASTER_UNKNOWN};
    if(xz_sync_link_runtime_master_observe){
        if(!xz_sync_link_runtime_master_observe(&m)||!m.revision||!m.operator_epoch||
           (uint32_t)(xz_game_milliseconds()-m.observed_ms)>XZ_MASTER_MAX_AGE_MS)
            m=(struct xz_master_observation){.deck=XZ_MASTER_UNKNOWN};
        return m; /* UNKNOWN never falls back to native flags. */
    }
    if(xz_extra_decks_route&&xz_extra_decks_route().available)return m;
    /* Standalone native-only fixtures/builds have no global authority observer. */
    for(int d=0;d<2;d++){
        struct xz_key_snapshot s;
        if(model.deck[d].pc_stems.source==XZ_UI_SOURCE_LOCAL&&xz_key_native_snapshot((unsigned)d,&s)&&s.master){
            if(m.deck>=0)return (struct xz_master_observation){.deck=XZ_MASTER_UNKNOWN};
            m.deck=d;m.revision=s.epoch;m.operator_epoch=1;
        }
    }
    return m;
}
static int key_master_same(struct xz_master_observation a,struct xz_master_observation b){
    return a.deck==b.deck&&a.revision==b.revision&&a.operator_epoch==b.operator_epoch&&
        a.request_epoch==b.request_epoch&&a.lease_generation==b.lease_generation&&
        a.route_epoch==b.route_epoch&&a.load_revision==b.load_revision&&a.transport_generation==b.transport_generation;
}
static int key_request_locked(int deck,enum xz_key_command_kind kind,int step){
    if((unsigned)deck>=4)return 0;
    if(deck<2&&kind==XZ_KEY_SYNC_TOGGLE&&key_sync[deck].valid)return key_sync_end(deck,key_player(deck),1);
    struct xz_master_observation m=key_master();
    struct xz_deck_key source,target={0},again;struct xz_key_snapshot native={0};
    struct xz_key_command c={.kind=kind,.step=step,.target_deck=-1};
    enum xz_key_refusal refusal=XZ_KEY_OK;
    if(!deck_key_read(deck,&source,&native)||!source.engine_ready)refusal=XZ_KEY_NO_SOURCE;
    else if(kind==XZ_KEY_SYNC_TOGGLE){
        if(m.deck>=0&&m.deck<4)(void)deck_key_read(m.deck,&target,NULL);
        refusal=xz_key_plan_sync(deck,&source,&target,&m,&c);
    }else if((kind!=XZ_KEY_STEP&&kind!=XZ_KEY_RESET)||(kind==XZ_KEY_STEP&&step!=-1&&step!=1))return 0;
    else c.source_owner=source.owner;
    if(refusal!=XZ_KEY_OK){
        snprintf(ui.notice,sizeof ui.notice,"%s",xz_key_refusal_text(refusal));
        if(deck<2)key_sync_fail(deck,refusal==XZ_KEY_IS_MASTER?XZ_KEY_SYNC_IS_MASTER:
            refusal==XZ_KEY_NO_MASTER?XZ_KEY_SYNC_NEED_MASTER:XZ_KEY_SYNC_NO_MASTER_KEY);
        return 0;
    }
    if(!deck_key_read(deck,&again,NULL)||!xz_key_owner_equal(deck,source.owner,again.owner))return 0;
    if(c.target_valid&&(!key_master_same(m,key_master())||!deck_key_read(c.target_deck,&again,NULL)||
        !xz_key_owner_equal(c.target_deck,c.target_owner,again.owner)))return 0;
    if(deck>=2){
        struct xz_extra_decks before={0};
        if(xz_extra_decks_snapshot)(void)xz_extra_decks_snapshot(&before);
        key_notice_seen[deck-2]=before.notice_seq;
        struct xz_extra_deck_event e={.deck=deck-2,.kind=XZ_EXTRA_IN_KEY,.pressed=1,.key=c};
        int accepted=xz_extra_deck_input&&xz_extra_deck_input(&e);
        snprintf(ui.notice,sizeof ui.notice,"%s",accepted?"KEY REQUEST QUEUED / PLAY TO VERIFY":"KEY QUEUE BUSY / RETRY");
        return accepted;
    }
    int cents=kind==XZ_KEY_RESET?0:source.desired_cents+step*100;
    void *player=NULL;
    if(kind==XZ_KEY_SYNC_TOGGLE){
        player=key_player(deck);
        if(!player||!native_master_tempo_on)return key_sync_fail(deck,XZ_KEY_SYNC_NO_ENGINE);
        native.master_tempo=1;
        if(!xz_key_sync_cents(&native,&c.target,&cents))return key_sync_fail(deck,XZ_KEY_SYNC_RANGE);
        native_master_tempo_on(player,1);
    }
    if(xz_key_set_desired_cents(deck,native.epoch,cents)){
        if(player)native_master_tempo_on(player,source.master_tempo);
        return key_sync_fail(deck,XZ_KEY_SYNC_RANGE);
    }
    key_sync[deck].valid=kind==XZ_KEY_SYNC_TOGGLE;
    key_sync[deck].target_deck=c.target_deck;key_sync[deck].target_owner=c.target_owner;
    key_sync[deck].mt_before=source.master_tempo;key_sync[deck].epoch=native.epoch;key_sync[deck].cents=cents;
    model.deck[deck].key_synced=key_sync[deck].valid;
    if(key_sync[deck].valid)__atomic_fetch_or(&key_sync_led_mask,1u<<deck,__ATOMIC_RELEASE);
    else __atomic_fetch_and(&key_sync_led_mask,~(1u<<deck),__ATOMIC_RELEASE);
    strip_error_until[deck]=0;__atomic_add_fetch(&xz_mods_key_proof_v1.requests,1,__ATOMIC_RELAXED);
    snprintf(ui.notice,sizeof ui.notice,"D%d KEY %+dc%s",deck+1,cents,kind==XZ_KEY_SYNC_TOGGLE?" / MASTER CAPTURED":"");
    return 1;
}
static int sync_key(int deck){return key_request_locked(deck,XZ_KEY_SYNC_TOGGLE,0);}
int xz_key_control_request(int deck,enum xz_key_command_kind kind,int step){
    pthread_mutex_lock(&ui_mutex);
    int accepted=key_request_locked(deck,kind,step);
    char notice[sizeof ui.notice];snprintf(notice,sizeof notice,"%s",ui.notice);
    xz_ui_notice_set(&async_notice,notice,xz_game_milliseconds());
    pthread_mutex_unlock(&ui_mutex);
    if(xz_shell_runtime_notice)xz_shell_runtime_notice(notice);
    xz_native_skin_request_repaint();return accepted;
}

static int (*stock_master_tempo_key)(void *,const void *);
static int master_tempo_key_hook(void *self,const void *input) {
    const unsigned char *player=self,*key=input;
    int deck=(int)player[0x26]-1,operation=key[0x0b]&15,consumed=0;
    if(deck<0||deck>1)return stock_master_tempo_key?stock_master_tempo_key(self,input):0;
    unsigned bit=1u<<deck;
    pthread_mutex_lock(&ui_mutex);
    if(operation==0&&!(master_tempo_down&bit)) {
        master_tempo_down|=bit;
        /* +0x34 is SHIFT: SHIFT+MASTER TEMPO keeps its stock function and leaves KEY SYNC on. */
        if(started&&key_available&&!player[0x34]&&key_sync[deck].valid&&native_master_tempo_on) {
            if(key_sync_end(deck,self,1)) {
                master_tempo_owned|=bit;
            }
        }
    }
    consumed=!!(master_tempo_owned&bit);
    if(operation==2||operation==3) {
        master_tempo_down&=~bit;master_tempo_owned&=~bit;
    }
    pthread_mutex_unlock(&ui_mutex);
    return consumed?1:stock_master_tempo_key?stock_master_tempo_key(self,input):0;
}

static int sdk_display_busy(void) {
    return xz_doom_running() || arcade_present || xz_wave_rider_present() || xz_rhythm_lab_present();
}
/* Manager worker only, with no manager lock held. The record parses before ui_mutex. */
static int sdk_apply_profile(void *context, const char *record, int persist) {
    struct xz_settings saved; unsigned present = 0;
    (void)context;
    if (!record || xz_settings_parse_present(record, &saved, &present)) return -1;
    pthread_mutex_lock(&ui_mutex);
    struct xz_settings merged = settings_snapshot();
    int applied = __atomic_load_n(&started, __ATOMIC_ACQUIRE) && !xz_settings_overlay(&merged, &saved, present);
    if (applied) {
        profile_apply_locked(&merged,"app-profile");
        if(persist){settings_claim_owner_locked(&settings_profile);settings_queue();}
        else model.settings_status="PROFILE APPLIED / NOT SAVED";
    }
    pthread_mutex_unlock(&ui_mutex);
    return applied ? 0 : -1;
}
/* Manager worker only: games keep the display until they exit. */
static int sdk_display_available(void *context) {
    (void)context;
    pthread_mutex_lock(&ui_mutex);
    int available = __atomic_load_n(&started, __ATOMIC_ACQUIRE) && !sdk_display_busy();
    pthread_mutex_unlock(&ui_mutex);
    return available;
}
static void sdk_takeover(struct xz_sdk_ui_change change) {
    if (!change.set_takeover) return;
    model.fb_takeover = change.takeover;
    __atomic_store_n(&fb_takeover_active, model.fb_takeover, __ATOMIC_RELEASE);
}
/* Under ui_mutex from refresh(). Takeover follows snapshot.foreground (a child is
   running), not the brief launch reservation in xz_sdk_host_foreground(). The
   display is taken once and the panel closes once; the MODS badge reopens it. */
static void sdk_refresh(void) {
    if (!sdk_started) return;
    int fresh = xz_sdk_host_read(&sdk_read) == 0;
    xz_sdk_ui_model_read(&model.sdk, fresh ? &sdk_read : NULL, sdk_display_busy());
    struct xz_sdk_ui_change change = xz_sdk_ui_session_step(&sdk_session, model.sdk.foreground, model.fb_takeover);
    sdk_takeover(change);
    if (change.close_panel && touch.visible) { xz_native_touch_visible(&touch, 0); __atomic_store_n(&visible, 0, __ATOMIC_RELEASE); }
}
static void sdk_apply(const struct xz_ui_action *a) {
    struct xz_sdk_ui_plan plan;
    xz_sdk_ui_plan(&model.sdk, a, &plan);
    if (plan.notice[0]) snprintf(ui.notice, sizeof(ui.notice), "%s", plan.notice);
    if (plan.submit && (!sdk_started || xz_sdk_host_submit(&plan.command)))
        snprintf(ui.notice, sizeof(ui.notice), "%s", sdk_started ? "APP MANAGER BUSY OR LIST CHANGED / TAP AGAIN" : "APP MANAGER IS NOT RUNNING / OPEN SET UP APPS");
}
/* A foreground app, or one being launched, keeps the display; games wait for STOP. */
static int sdk_refuses_game(const struct xz_ui_action *a) {
    if (!xz_sdk_ui_game_launch(a->kind) || !sdk_started || !xz_sdk_host_foreground()) return 0;
    ui.page = XZ_UI_APPS;
    snprintf(ui.notice, sizeof(ui.notice), "STOP THE RUNNING APP BEFORE STARTING A GAME");
    return 1;
}
/* Lifecycle only: called from xz_ui_runtime_start after the UI is live. */
static void sdk_start(void) {
    static const char *roots[XZ_SDK_ROOTS];
    static struct xz_sdk_host_config config;
    int extra = 0;
    unsigned count = xz_sdk_ui_roots(getenv("XZ_MODS_USB"), roots, sdk_usb_root, sizeof(sdk_usb_root), &extra);
    const char *kart_test_root = xz_kart_runtime_sdk_test_root();
    if(kart_test_root && count < XZ_SDK_ROOTS) { roots[count++] = kart_test_root; extra = 1; }
    config = (struct xz_sdk_host_config){ .root_count = count,
        .applications_folder = XZ_SDK_UI_APPLICATIONS_FOLDER, .autoload_folder = XZ_SDK_UI_AUTOLOAD_FOLDER,
        .stage_root = XZ_SDK_UI_STAGE_ROOT, .autoload_enabled = 1,
        .apply_profile = sdk_apply_profile, .display_available = sdk_display_available };
    memcpy(config.roots, roots, sizeof(config.roots));
    int ok = xz_sdk_host_start && xz_sdk_host_start(&config) == 0;
    pthread_mutex_lock(&ui_mutex);
    sdk_started = ok; memset(&sdk_session, 0, sizeof(sdk_session));
    model.sdk.host = ok ? XZ_SDK_UI_RUNNING : XZ_SDK_UI_ABSENT; model.sdk.roots = count; model.sdk.extra_root = extra;
    pthread_mutex_unlock(&ui_mutex);
    if (ok) xz_game_display_owner(xz_sdk_host_foreground);
    else xz_log("XZ SDK app manager unavailable; MODS > EXTRAS > APPLICATIONS shows setup");
}
/* Lifecycle only. The worker may be waiting for ui_mutex, so stop runs unlocked. */
static void sdk_stop(void) {
    if (!sdk_started) return;
    xz_game_display_owner(NULL);
    xz_sdk_host_stop();
    pthread_mutex_lock(&ui_mutex);
    sdk_started = 0; model.sdk.host = XZ_SDK_UI_ABSENT; model.sdk.foreground = 0;
    sdk_takeover(xz_sdk_ui_session_step(&sdk_session, 0, model.fb_takeover));
    pthread_mutex_unlock(&ui_mutex);
}

static void ram_refresh(int deck) {
    struct xz_ram_status s;memset(&s,0,sizeof(s));
    if(xz_ram_deck_status)xz_ram_deck_status((unsigned)deck,&s);
    else if(deck<2)xz_ram_status(deck,&s);
    struct xz_ui_ram *r=&model.deck[deck].ram;
    switch(s.state){
    case XZ_RAM_EMPTY:r->state=XZ_UI_RAM_EMPTY;break;
    case XZ_RAM_LOADING:r->state=XZ_UI_RAM_LOADING;break;
    case XZ_RAM_READY:r->state=XZ_UI_RAM_READY;break;
    case XZ_RAM_ERROR:r->state=XZ_UI_RAM_ERROR;break;
    case XZ_RAM_DETACHED:r->state=XZ_UI_RAM_DETACHED;break;
    default:r->state=XZ_UI_RAM_UNAVAILABLE;break;}
    r->copied=s.copied;r->bytes=s.bytes;r->retained_stems_bytes=0;
    r->reason=s.reason;r->phase=s.phase;r->key=s.key;r->supported=s.supported;
    r->route_up=s.route_up;r->source_current=s.source_current;r->can_request=s.can_request;r->can_release=s.can_release;
    r->retained_assets=s.retained_assets;r->software=deck>=2;
    s.message[sizeof(s.message)-1]=0;snprintf(r->message,sizeof(r->message),"%s",s.message);
    if(deck<2&&(s.state==XZ_RAM_READY||s.state==XZ_RAM_DETACHED)){
        struct xz_audio_status audio={0};xz_audio_get_status(deck,&audio);
        r->retained_stems_bytes=audio.retained_stems_bytes;
        snprintf(r->message,sizeof(r->message),"ORIGINAL IN RAM / %s",audio.retained_stems_bytes?"PREPARED STEMS IN RAM":"STEMS STILL NEED USB");
    }
}

/* The worker copies asynchronously; these only start, stop or free it and report the next step. */
static void ram_action(const struct xz_ui_action *a) {
    int deck=a->deck;
    if(deck<0||deck>=4)return;
    enum xz_ram_command command=a->kind==XZ_UI_RAM_REQUEST?XZ_RAM_LOAD:a->kind==XZ_UI_RAM_CANCEL?XZ_RAM_CANCEL:XZ_RAM_RELEASE;
    enum xz_ram_result result=XZ_RAM_REFUSED;
    if(xz_ram_deck_command)result=xz_ram_deck_command((unsigned)deck,command,&a->ram_key);
    else if(deck<2){
        int rc=command==XZ_RAM_LOAD?xz_ram_request(deck):command==XZ_RAM_CANCEL?xz_ram_cancel(deck):xz_ram_release(deck);
        result=rc?XZ_RAM_REFUSED:XZ_RAM_ACCEPTED;
    }
    const char *message=result==XZ_RAM_ACCEPTED?(command==XZ_RAM_LOAD?"RAM COPY QUEUED":command==XZ_RAM_CANCEL?"COPY CANCELLATION REQUESTED":"RAM RELEASE REQUESTED"):
        result==XZ_RAM_ALREADY_HELD?"RAM ATTACHMENT COMPLETED; COPY RETAINED":result==XZ_RAM_STALE?"TRACK CHANGED; CHECK THE SELECTED DECK":
        result==XZ_RAM_ROUTE_DOWN?"CONNECT THE FOUR-DECK AUDIO ROUTE":result==XZ_RAM_SOURCE_MISSING?"LOAD A CURRENT USB TRACK":"RAM REQUEST REFUSED; CHECK STATUS";
    snprintf(ui.notice,sizeof(ui.notice),"DECK %d: %s",deck+1,message);
    ram_refresh(deck);
}

static uint64_t network_request_id;
static void network_refresh(void) {
    struct xz_net_view fresh;
    enum xz_net_result result=xz_net_runtime_read?xz_net_runtime_read(&fresh):XZ_NET_NEEDS_BACKEND;
    if(result==XZ_NET_OK)model.network=fresh;
    else if(result==XZ_NET_NEEDS_BACKEND){model.network.result=result;model.network.phase=XZ_NET_NEEDS_SETUP;snprintf(model.network.detail,sizeof model.network.detail,"Start the network worker in this RAM loader, then detect again");}
}
static void network_apply(const struct xz_ui_action *a) {
    if(a->phase!=XZ_UI_PRESS)return;
    struct xz_net_command command={.request_id=++network_request_id};
    if(a->kind==XZ_UI_NETWORK_DETECT)command.kind=XZ_NET_CMD_DETECT;
    else if(a->kind==XZ_UI_NETWORK_APPLY){
        if(a->network_revision!=ui.network.revision){snprintf(ui.notice,sizeof ui.notice,"Network draft changed; review again");return;}
        command.kind=XZ_NET_CMD_APPLY_TRIAL;
        if(!xz_ui_network_take_apply(&ui,&model,a,&command.body.apply)){snprintf(ui.notice,sizeof ui.notice,"Network review paint/gesture changed; review again");return;}
    }else if(a->kind==XZ_UI_NETWORK_REVERT){command.kind=XZ_NET_CMD_REVERT;command.body.trial_id=model.network.trial_id;}
    else if(a->kind==XZ_UI_NETWORK_CANCEL){command.kind=XZ_NET_CMD_CANCEL;command.body.trial_id=model.network.trial_id;}
    else if(a->kind==XZ_UI_NETWORK_STEP&&a->index==XZ_NETWORK_VERIFY){command.kind=XZ_NET_CMD_VERIFY;command.body.trial_id=model.network.trial_id;}
    else return;
    enum xz_net_result result=xz_net_runtime_submit?xz_net_runtime_submit(&command):XZ_NET_NEEDS_BACKEND;
    if(result!=XZ_NET_OK)snprintf(ui.notice,sizeof ui.notice,"%s",xz_net_result_text(result));
}
static void support_refresh(void) {
    if (xz_support_read) xz_support_read(&model.support, 0);
}
/* What REPORT AN ISSUE adds to the DJ's answers: the interface, the screen under MODS, the deck view, the error codes
   on screen and the settings with the diary's fingerprint of them. Enums and numbers only. */
static void support_context_locked(struct xz_support_report *r){
    struct xz_settings s=settings_snapshot();
    r->ui_interface=model.ui_interface==XZ_UI_INTERFACE_MODERN;
    r->before=report_before;
    r->deck_view=model.deck_view>=0&&model.deck_view<=2?model.deck_view:0;
    unsigned errors=xz_ui_counter_errors(&model);
    for(int d=0;d<2;d++)r->alert[d]=(errors>>d&1)&&xz_diary_error_code?xz_diary_error_code(model.counter_error[d].code):0;
    r->alert[2]=xz_ui_stock_window_notice(&model)?stock_window_code(model.stock_window):0;
    r->have_settings=1;
    r->settings[0]=!!s.stems;r->settings[1]=!!s.gate;r->settings[2]=s.ui_interface;r->settings[3]=s.classic_theme;
    r->settings[4]=s.deck_view;r->settings[5]=!!s.browse_preview;
    char text[1024];int n=xz_settings_format(&s,text,sizeof text);
    if(n>0&&sha256_init&&sha256_update&&sha256_final){
        SHA256_CTX hash;unsigned char digest[32];static const char hex[]="0123456789abcdef";
        sha256_init(&hash);sha256_update(&hash,(unsigned char *)text,(size_t)n);sha256_final(&hash,digest);
        for(int i=0;i<32;i++){r->settings_sha256[2*i]=hex[digest[i]>>4];r->settings_sha256[2*i+1]=hex[digest[i]&15];}
        r->settings_sha256[64]=0;
    }
}
static void support_apply(const struct xz_ui_action *a) {
    struct xz_support_request_args req;
    if (!xz_ui_support_take_request(&ui, &model, a, &req)) return;
    if (req.op == XZ_SUPPORT_OP_REPORT) support_context_locked(&req.report);
    if (!xz_support_request || xz_support_request(&req) != 0) {
        if (xz_support_read) xz_support_read(&model.support, 0);
        if (model.support.status[0]) snprintf(ui.notice, sizeof ui.notice, "%.95s", model.support.status);
    } else if (xz_support_read) {
        xz_support_read(&model.support, 0);
    }
}
static void deck_view_apply(int view);
static void apply(void *context, const struct xz_ui_action *actions, size_t count) {
    (void)context;
    for (size_t i = 0; i < count; i++) {
        const struct xz_ui_action *a = &actions[i];
        if(xz_ui_network_kind(a->kind)){network_apply(a);continue;}
        if(xz_ui_support_kind(a->kind)){support_apply(a);continue;}
        if (xz_sdk_ui_kind(a->kind)) { sdk_apply(a); continue; }
        if (sdk_refuses_game(a)) continue;
        switch (a->kind) {
        case XZ_UI_ROUTE_RESCAN:
            /* Wakes the connector only (D10); its status says SCANNING at once, so the menus show it now. */
            if(xz_extra_decks_rescan)xz_extra_decks_rescan();
            route_refresh_locked();
            break;
        case XZ_UI_PC_SETUP:case XZ_UI_PC_CHECK:case XZ_UI_PC_TEST:case XZ_UI_PC_CONFIRM:case XZ_UI_PC_TOGGLE:
            pc_apply(a);break;
        case XZ_UI_PROFILE:
            /* Opening the list reads the USBs again so a just-inserted stick shows. */
            if(a->value&&xz_dj_runtime_rescan)xz_dj_runtime_rescan();
            break;
        case XZ_UI_PROFILE_CHECK:
            if(xz_dj_runtime_rescan)xz_dj_runtime_rescan();
            break;
        case XZ_UI_PROFILE_PICK:
            if(xz_dj_runtime_pick)xz_dj_runtime_pick(a->index);
            break;
        /* The editor: the image list follows the USB it saves to; a picked image is rendered for review. */
        case XZ_UI_PROFILE_EDIT_OPEN:case XZ_UI_LOGO_RESCAN:
            if(xz_dj_runtime_images)xz_dj_runtime_images(ui.edit_usb);
            break;
        case XZ_UI_EDIT_STEP:
            if(a->index==XZ_UI_EDIT_LOGO&&model.dj&&model.dj->images_usb!=ui.edit_usb&&xz_dj_runtime_images)xz_dj_runtime_images(ui.edit_usb);
            break;
        case XZ_UI_LOGO_PICK:
            if(a->index>=0&&xz_dj_runtime_preview)xz_dj_runtime_preview(a->index);
            break;
        case XZ_UI_PROFILE_SAVE:
            if(xz_dj_runtime_save)xz_dj_runtime_save(&ui);
            break;
        case XZ_UI_METRICS_TOGGLE:
            model.metrics_enabled=1;
            memset(&model.metrics,0,sizeof(model.metrics));
            if(xz_metrics_enable)xz_metrics_enable(model.metrics_enabled);
            break;
        case XZ_UI_METRICS_SETUP_OPEN:
            xz_ui_navigate(&ui,XZ_UI_METRICS_SETUP);ui.metrics_step=0;
            panel_show_locked();break;
        case XZ_UI_METRICS_CHECK:
            if(!xz_metrics_start||xz_metrics_start()){
                snprintf(ui.notice,sizeof(ui.notice),"SAMPLER MISSING: REBUILD USB LOADER WITH CURRENT XZ MODS");break;
            }
            model.metrics_enabled=1;if(xz_metrics_enable)xz_metrics_enable(1);
            if(ui.metrics_step<3){ui.metrics_step++;break;}
            if(xz_metrics_snapshot)xz_metrics_snapshot(&model.metrics);
            if(xz_metrics_fresh(&model.metrics,xz_game_milliseconds())&&(model.metrics.valid&3)==3){
                snprintf(ui.notice,sizeof(ui.notice),model.metrics.valid&4?"CPU / RAM / SOC SENSOR LIVE":"CPU / RAM LIVE; SOC SENSOR NOT EXPOSED");
                panel_hide_locked(0);
            }else snprintf(ui.notice,sizeof(ui.notice),"WAIT ONE SECOND, THEN LIVE VERIFY AGAIN");
            break;
        case XZ_UI_LOCAL_STEM_SETUP_OPEN:
            xz_ui_navigate(&ui,XZ_UI_LOCAL_STEM_SETUP);ui.deck=a->deck>=0&&a->deck<2?a->deck:0;
            refresh_local();ui.stem_setup_step=!audio_available?0:(model.enabled&XZ_UI_STEM)?2:1;
            panel_show_locked();inline_layout_publish();break;
        case XZ_UI_LOCAL_STEM_SETUP_STEP: {
            int deck=ui.deck>=0&&ui.deck<2?ui.deck:0;
            if(model.deck[deck].pc_stems.source!=XZ_UI_SOURCE_LOCAL){xz_ui_navigate(&ui,XZ_UI_PC_STEMS);break;}
            refresh_local();
            if(!audio_available){snprintf(ui.notice,sizeof(ui.notice),"STEM ENGINE MISSING / UPDATE USB LOADER IN XZ MODS");break;}
            if(a->index==0){ui.stem_setup_step=(model.enabled&XZ_UI_STEM)?2:1;snprintf(ui.notice,sizeof(ui.notice),"%s",xz_ui_usb_next_step(xz_ui_usb_blocked(&model,deck)));}
            else if(a->index==1){requested_stems=1;model.enabled|=XZ_UI_STEM;xz_audio_set_enabled(1);levels(0);levels(1);ui.stem_setup_step=2;settings_queue();refresh_local();}
            /* Live verify means a prepared track is actually playing stems, not just an engine present. */
            else if((model.deck[deck].ready&XZ_UI_STEM)&&audio_status[deck].prepared&&audio_status[deck].state==XZ_AUDIO_EXPERIMENTAL_READY){panel_hide_locked(1);inline_redraw_pending=1;}
            else snprintf(ui.notice,sizeof(ui.notice),"%s",model.deck[deck].stem_error&&*model.deck[deck].stem_error?
                model.deck[deck].stem_error:xz_ui_usb_next_step(xz_ui_usb_blocked(&model,deck)));
            inline_layout_publish();break;
        }
        case XZ_UI_UNAVAILABLE:
            if(a->index==XZ_UI_STEM){xz_ui_navigate(&ui,XZ_UI_LOCAL_STEM_SETUP);ui.stem_setup_step=0;panel_show_locked();inline_layout_publish();}
            if(a->index&(XZ_UI_KEY|XZ_UI_KEYSYNC)){
                play_key_open_locked(a->deck>=0&&a->deck<4?a->deck:0);play_key.setup=1;play_key.down=0;
                play_key_dirty=1;__atomic_store_n(&play_key_visible,1,__ATOMIC_RELEASE);
            }
            break;
        case XZ_UI_GAME_SELECT:
            if(a->index==0){xz_ui_navigate(&ui,XZ_UI_DOOM_FILES);xz_doom_browser_scan();}
            else if(xz_doom_choose_game(1)==0)xz_ui_navigate(&ui,XZ_UI_DOOM);
            else {xz_ui_navigate(&ui,XZ_UI_DOOM_FILES);snprintf(ui.notice,sizeof(ui.notice),"DOWNLOAD %s IN THE WINDOWS APP, THEN COPY TO USB",a->index==1?"CHEX QUEST":"DOOM");}
            break;
        case XZ_UI_MYHOUSE_CHECK: {
            struct stat engine;
            int found=stat("/dev/shm/gzdoom-xz",&engine)==0&&S_ISREG(engine.st_mode);
            snprintf(ui.notice,sizeof(ui.notice),"%s",found?"NATIVE ENGINE FOUND / SELECT GAME DATA ON USB":"NATIVE GZDOOM ENGINE IS NOT INSTALLED YET");
            break;
        }
        case XZ_UI_GLYPH_RUN_OPEN:
        case XZ_UI_RHYTHM_LAB_OPEN:
            if(xz_doom_running()){snprintf(ui.notice,sizeof(ui.notice),"EXIT DOOM BEFORE RHYTHM LAB");break;}
            lab_previous_takeover=model.fb_takeover;model.fb_takeover=0;
            __atomic_store_n(&fb_takeover_active,0,__ATOMIC_RELEASE);xz_game_nav_reset(&game_nav);if(a->kind==XZ_UI_GLYPH_RUN_OPEN)xz_rhythm_lab_open_glyph(xz_game_milliseconds());else xz_rhythm_lab_open(xz_game_milliseconds());
            arcade_input_publish_locked();xz_native_touch_visible(&touch,1);__atomic_store_n(&visible,1,__ATOMIC_RELEASE);break;
        case XZ_UI_WAVE_RIDER_OPEN:
            if(xz_doom_running()){snprintf(ui.notice,sizeof(ui.notice),"EXIT DOOM BEFORE STARTING WAVE RIDER");break;}
            wave_rider_previous_takeover=model.fb_takeover;model.fb_takeover=0;
            __atomic_store_n(&fb_takeover_active,0,__ATOMIC_RELEASE);
            xz_game_nav_reset(&game_nav);xz_wave_rider_open(xz_game_milliseconds());arcade_input_publish_locked();
            xz_native_touch_visible(&touch,1);__atomic_store_n(&visible,1,__ATOMIC_RELEASE);
            break;
        case XZ_UI_ARCADE_OPEN:
            if(xz_doom_running()){snprintf(ui.notice,sizeof(ui.notice),"EXIT DOOM BEFORE STARTING BEAT ARCADE");break;}
            if(!arcade.initialized)xz_arcade_init(&arcade,xz_game_milliseconds());
            arcade.last_ms=xz_game_milliseconds();
            arcade_previous_takeover=model.fb_takeover;model.fb_takeover=0;
            __atomic_store_n(&fb_takeover_active,0,__ATOMIC_RELEASE);
            arcade_present=1;arcade_input_publish_locked();arcade_opening_touch=1;arcade.touch_down=1;xz_game_nav_reset(&game_nav);
            xz_native_touch_visible(&touch,1);__atomic_store_n(&visible,1,__ATOMIC_RELEASE);
            break;
        case XZ_UI_PANEL:
            xz_native_skin_request_repaint();
            if(ui.page==XZ_UI_DOOM_FILES)xz_doom_browser_scan();
            break;
        case XZ_UI_WAD_SCAN:xz_doom_browser_scan();break;
        case XZ_UI_WAD_SELECT:xz_doom_browser_select(a->index,ui.notice);break;
        case XZ_UI_WAD_CLEAR:xz_doom_browser_clear_map();break;
        case XZ_UI_WAD_PAGE:
            ui.wad_offset+=a->index*6;if(ui.wad_offset<0)ui.wad_offset=0;
            break;
        case XZ_UI_UPDATE_CHECK:
            xz_update_read(&model.update);
    support_refresh();
            if(!model.update.ready || (ui.page!=XZ_UI_UPDATE_SETUP&&model.update.phase==XZ_UPDATE_ERROR)){xz_ui_navigate(&ui,XZ_UI_UPDATE_SETUP);snprintf(ui.notice,sizeof(ui.notice),"DETECT THE UPDATE SERVER IN SETUP");}
            else xz_update_request(0);
            break;
        case XZ_UI_UPDATE_INSTALL:
            xz_update_read(&model.update);
    support_refresh();
            if(model.update.phase==XZ_UPDATE_AVAILABLE)xz_update_request(1);
            else snprintf(ui.notice,sizeof(ui.notice),"CHECK FOR AN AVAILABLE UPDATE FIRST");
            break;
        case XZ_UI_UPDATE_ROLLBACK:xz_update_request(2);break;
        case XZ_UI_DOOM_CHECK:
            model.doom_ready=xz_doom_ready();
            snprintf(ui.notice,sizeof(ui.notice),"%s",model.doom_ready?"ENGINE + WAD FOUND / READY TO START":"GAME FILES MISSING / REBUILD YOUR DOOM USB");
            break;
        case XZ_UI_DOOM_START:
            if(!xz_doom_ready()) { xz_ui_navigate(&ui,XZ_UI_DOOM_SETUP); break; }
            if(xz_doom_launch()) { snprintf(ui.notice,sizeof(ui.notice),"DOOM COULD NOT START / CHECK USB"); break; }
            doom_previous_takeover=model.fb_takeover; doom_phase=1; doom_launched_at=xz_game_milliseconds();
            model.fb_takeover=1; __atomic_store_n(&fb_takeover_active,1,__ATOMIC_RELEASE);
            xz_native_touch_visible(&touch,0); __atomic_store_n(&visible,0,__ATOMIC_RELEASE);
            break;
        case XZ_UI_STEMS_OVERLAY:
            /* The STEMS button in a 4 DECKS view shows or hides the stem buttons, one press each; the 2 DECKS
               layout stays as it is (wave_viewport.h xz_stems_press). */
            if(a->index==XZ_UI_STEMS_BUTTON){
                enum xz_stems_press press=xz_stems_press(model.ui_interface==XZ_UI_INTERFACE_MODERN,
                    xz_deck_layout_get()!=XZ_LAYOUT_TWO,(modern_display.hidden_parts&XZ_HIDE_FOUR_STEMS)!=0);
                if(press!=XZ_STEMS_PRESS_STEP){
                    if(press==XZ_STEMS_PRESS_SHOW)modern_display.hidden_parts&=~XZ_HIDE_FOUR_STEMS;
                    else if(press==XZ_STEMS_PRESS_HIDE)modern_display.hidden_parts|=XZ_HIDE_FOUR_STEMS;
                    stems_notice_locked(press==XZ_STEMS_PRESS_SHOW&&!requested_stems&&!pc_present?
                        "STEM ROWS SHOW ONCE STEMS ARE ON IN MODS":xz_stems_press_notice(press));
                    xz_native_skin_request_repaint();
                    break;
                }
            }
            /* MODERN has its own layout (the shell draws it); CLASSIC's native rows keep stems_overlay. */
            /* A MODERN layout picture carries its layout (value = layout + 1); the STEMS button steps. */
            if(model.ui_interface==XZ_UI_INTERFACE_MODERN){
                int pick=(int)a->value-1;
                model.modern_stems=pick>=0&&xz_modern_stems_position(pick)>=0?pick:xz_modern_stems_next(model.modern_stems);
                break;
            }
            model.stems_overlay=xz_wave_layout_next(model.stems_overlay);
            stem_layout_actions++;stem_layout_revision++;stem_layout_origin="layout-control";
            inline_redraw_pending=1;
            if(inline_contact){
                struct xz_ui_action ignored[XZ_UI_ACTIONS];xz_ui_cancel(&inline_ui,ignored);inline_cancelled=1;
            }
            inline_layout_publish();
            break;
        case XZ_UI_STEM_ORDER:
            model.stem_order=(model.stem_order+1)%XZ_STEM_ORDER_COUNT;pads.order=model.stem_order;
            inline_redraw_pending=1;
            if(inline_contact){
                struct xz_ui_action ignored[XZ_UI_ACTIONS];xz_ui_cancel(&inline_ui,ignored);inline_cancelled=1;
            }
            snprintf(ui.notice,sizeof(ui.notice),"STEM ORDER: %s%s",xz_stem_order_name(model.stem_order),
                model.deck[0].stem_count==4||model.deck[1].stem_count==4?" +BASS":"");
            break;
        case XZ_UI_CLOSE:
            if(model.ui_interface==XZ_UI_INTERFACE_MODERN&&xz_shell_runtime_close_mods)xz_shell_runtime_close_mods();
            ui.history_count=0;
            xz_native_skin_request_repaint();
            xz_native_touch_visible(&touch, 0);
            __atomic_store_n(&visible, 0, __ATOMIC_RELEASE);
            break;
        case XZ_UI_RAM_DECK:
            if(a->index>=0&&a->index<4)ui.ram_deck=a->index;
            break;
        case XZ_UI_RAM_SETUP_STEP:
            for(int deck=0;deck<4;deck++)ram_refresh(deck);
            break;
        case XZ_UI_RAM_CHOOSE:
            if(xz_shell_runtime_key_setup_load&&xz_shell_runtime_key_setup_load(a->deck)){
                xz_native_touch_visible(&touch,0);__atomic_store_n(&visible,0,__ATOMIC_RELEASE);
                ui.ram_setup=1;ui.ram_step=1;
            }else snprintf(ui.notice,sizeof(ui.notice),"Track browser unavailable; check the loaded source.");
            break;
        case XZ_UI_DECK:
            if (a->index >= 0 && a->index < 2) { ui.deck = a->index; inline_ui.deck = a->index; }
            break;
        case XZ_UI_RAM_REQUEST:
        case XZ_UI_RAM_CANCEL:
        case XZ_UI_RAM_RELEASE:
            ram_action(a);
            break;
        case XZ_UI_ENABLE:
            if (a->index == XZ_UI_GATE || a->index == XZ_UI_SMART ||
                (a->index == XZ_UI_STEM && audio_available)) {
                uint32_t next = a->value != 0 ? model.enabled | (uint32_t)a->index : model.enabled & ~(uint32_t)a->index;
                if (a->index == XZ_UI_STEM) {
                    requested_stems = a->value != 0;
                    xz_audio_set_enabled(a->value != 0); model.enabled = next;
                    if (a->value != 0) { levels(0); levels(1); }
                    for(int deck=0;deck<2;deck++)model.deck[deck].usb_reason=usb_stem_reason(&audio_status[deck]);
                }
                else if (xz_runtime_set_cues(!!(next & XZ_UI_GATE), !!(next & XZ_UI_SMART)) == 0) model.enabled = next;
            }
            break;
        case XZ_UI_STEM_PAD_CONFIG:
            if(a->index>=0&&a->index<8){model.stem_bank=a->index/4;pads.bank=model.stem_bank;}break;
        case XZ_UI_STEM_BANK:
            if(a->index>=0&&a->index<=1){model.stem_bank=a->index;pads.bank=a->index;}break;
        case XZ_UI_LEVEL:
            if (local_stem_source(a->deck) && a->index >= 0 && a->index < xz_stem_count(model.deck[a->deck].stem_count)) {
                if (a->value > 0) {
                    pads.muted[a->deck] &= ~(1u << a->index);
                    model.deck[a->deck].muted = pads.muted[a->deck];
                }
                model.deck[a->deck].levels[a->index] = a->value; levels(a->deck);
                inline_redraw_pending=1;
            }
            break;
        case XZ_UI_MUTE:
            if (local_stem_source(a->deck) && a->index >= 0 && a->index < xz_stem_count(model.deck[a->deck].stem_count)) {
                if (a->phase != XZ_UI_PRESS) break;
                pads.muted[a->deck] ^= 1u << a->index;
                model.deck[a->deck].muted = pads.muted[a->deck];
                levels(a->deck);
                inline_redraw_pending=1;
            }
            break;
        case XZ_UI_BYPASS:
            if (local_stem_source(a->deck)) { model.deck[a->deck].bypass = a->value != 0; levels(a->deck); inline_redraw_pending=1; }
            break;
        case XZ_UI_BAND_COLOR:
            if(a->index>=0&&a->index<3&&a->value>=0&&a->value<XZ_BAND_COLOR_CHOICES){
                model.band_colors[a->index]=(int)a->value;wave_configure();
            }
            break;
        case XZ_UI_BAND_COLORS_RESET:
            memset(model.band_colors,0,sizeof model.band_colors);wave_configure();break;
        case XZ_UI_SHELL_BROWSE_COLUMNS:
            if(a->value>=0&&a->value<=2047&&a->value==(int)a->value)modern_browse_columns=(int)a->value;
            break;
        case XZ_UI_SHELL_DISPLAY: {
            int value=(int)a->value;
            if(a->value!=value||value<0)break;
            switch(a->index){
            case XZ_SHELL_SET_WAVE_COLOR:if(value<=3)modern_display.modern_wave_color=value;break;
            case XZ_SHELL_SET_WAVE_POSITION:if(value<=1)modern_display.wave_position=value;break;
            case XZ_SHELL_ITEM_KEY_DISPLAY:if(value<=2)modern_display.key_display=value;break;
            case XZ_SHELL_ITEM_WAVE_ZOOM:if(value<=6)modern_display.wave_zoom=value;break;
            case XZ_SHELL_ITEM_USB_COLORS:if(value<=2)modern_display.usb_colors=value;break;
            case XZ_SHELL_ITEM_MODE_LAMPS:case XZ_SHELL_ITEM_QUANTIZE_LAMP:
            case XZ_SHELL_ITEM_BYPASS_BUTTON:case XZ_SHELL_ITEM_WAVE_TITLES: {
                int bit=a->index==XZ_SHELL_ITEM_MODE_LAMPS?0:a->index==XZ_SHELL_ITEM_QUANTIZE_LAMP?1:a->index==XZ_SHELL_ITEM_BYPASS_BUTTON?2:3;
                if(value<=1)modern_display.hidden_parts=(modern_display.hidden_parts&~(1<<bit))|(value<<bit);
                break;}
            default:break;
            }
            break;}
        case XZ_UI_STEM_COLOR:
            if(a->index>=0&&a->index<3&&a->value>=0&&a->value<XZ_STEM_COLOR_CHOICES){
                model.stem_colors[a->index]=(int)a->value;wave_configure();
            }
            break;
        case XZ_UI_STEM_COLORS_RESET:
            memset(model.stem_colors,0,sizeof(model.stem_colors));wave_configure();break;
        case XZ_UI_SET_THEME:
            if (a->index >= 0 && a->index < XZ_CLASSIC_THEME_COUNT) {model.classic_theme = a->index;appearance_changed();}
            break;
        case XZ_UI_SET_INTERFACE:
            if (a->index == XZ_INTERFACE_CLASSIC || a->index == XZ_INTERFACE_MODERN) {
                model.ui_interface = a->index;appearance_changed();
                snprintf(ui.notice, sizeof(ui.notice), "%s", a->index == XZ_INTERFACE_MODERN ? "INTERFACE: FLAGSHIP" : "INTERFACE: CLASSIC");
            }
            break;
        case XZ_UI_SET_MODERN_THEME:
            if (a->index >= 0 && a->index < XZ_MODERN_THEME_COUNT) model.modern_theme = a->index;
            break;
        case XZ_UI_FOUR_WAVE_ORDER: /* retired rows: DECK VIEW replaced them; 0 number order, 1 mixer order */
            if(a->value==0||a->value==1){
                deck_view_apply(a->value!=0?XZ_LAYOUT_MIXER:XZ_LAYOUT_FOUR);
                snprintf(ui.notice,sizeof(ui.notice),"DECK VIEW: %s",xz_deck_layout_name(model.deck_view));
            }
            break;
        case XZ_UI_LANGUAGE:
            if(a->value>=0&&a->value<15&&(float)(int)a->value==a->value){model.controller_locale=(unsigned)a->value;settings_queue();}
            break;
        case XZ_UI_DECK_VIEW:
            if(a->value>=0&&a->value<XZ_DECK_VIEW_COUNT&&(float)(int)a->value==a->value){
                deck_view_apply((int)a->value);
                snprintf(ui.notice,sizeof(ui.notice),"DECK VIEW: %s",xz_deck_layout_name(model.deck_view));
            }
            break;
        case XZ_UI_SHELL_LOOK:{
            /* A MODERN play-screen part the shell owns: it applies and saves it; WAVEFORM COLOURS also sets the
               panel's own waveform mode (STOCK and RGB follow the player's colour, 3-BAND and STEMS are MODS's). */
            int choice=(int)a->value;
            if(!xz_shell_runtime_look_set||!xz_shell_runtime_look_set(a->index,choice)){snprintf(ui.notice,sizeof(ui.notice),"%s","THAT PART DID NOT CHANGE");break;}
            if(a->index==XZ_UI_LOOK_WAVE_COLOR){
                model.wave_mode=choice==XZ_UI_WAVE_STEMS?2:choice==XZ_UI_WAVE_3BAND?1:0;wave_configure();settings_queue();
                static const char *const names[XZ_UI_WAVE_COLORS]={"STOCK","RGB","3-BAND","STEMS"};
                snprintf(ui.notice,sizeof(ui.notice),"WAVEFORM: %s",choice>=0&&choice<XZ_UI_WAVE_COLORS?names[choice]:"STOCK");
            }
            if(xz_shell_runtime_look)xz_shell_runtime_look(&model.modern_look);
            break;}
        case XZ_UI_OPEN_UTILITY:
            /* The player's own settings: MODS steps aside (it reopens where it was) and the shell shows UTILITY. */
            if(!xz_shell_runtime_show_utility||!xz_shell_runtime_show_utility()){snprintf(ui.notice,sizeof(ui.notice),"%s","UTILITY IS NOT AVAILABLE NOW");break;}
            xz_native_touch_visible(&touch,0);__atomic_store_n(&visible,0,__ATOMIC_RELEASE);
            xz_native_skin_request_repaint();
            break;
        case XZ_UI_BROWSE_PREVIEW:
            if(a->value==0||a->value==1){
                model.browse_preview=(int)a->value;settings_queue();xz_native_skin_request_repaint();
            }
            break;
        case XZ_UI_WAVE_MODE:
            if (a->index >= 0 && a->index < XZ_WAVE_MODE_COUNT) {
                model.wave_mode = a->index;
                if(a->index>0)modern_display.modern_wave_color=a->index==2?3:2;
                wave_configure();
                snprintf(ui.notice, sizeof(ui.notice), "WAVEFORM: %s", a->index == 2 ? "STEMS" : a->index == 1 ? "3-BAND" : "STOCK");
            }
            break;
        case XZ_UI_JUMP_ENABLE:
            model.jump.enabled=a->value!=0;
            break;
        case XZ_UI_JUMP_SHIFT: model.jump.shift_page=1; break;
        case XZ_UI_JUMP_STEP: {
            int slot=ui.jump_edit_page*4+ui.jump_edit_pair;
            model.jump.sizes[slot]=xz_jump_step_size(model.jump.sizes[slot],a->index);
            break;
        }
        case XZ_UI_JUMP_SIZE:
            if(a->index>=0&&a->index<8&&a->value>=0&&a->value<XZ_JUMP_SIZES)
                model.jump.sizes[a->index]=(int)a->value;
            break;
        case XZ_UI_STEM_PAGE:
            if (a->index >= 0 && a->index < 4) model.stem_page = a->index;
            break;
        case XZ_UI_SHIFT_PAGES: model.shift_pages = 1; break;
        case XZ_UI_PAD_FEEDBACK: model.pad_feedback = a->value != 0; break;
        case XZ_UI_SHIFT_KEYSYNC: model.shift_keysync = 1; break;
        case XZ_UI_KEY_CHECK: {
            if(!play_key.open){
                play_key_open_locked(a->deck);play_key.down=0;play_key.setup=1;play_key.step=0;
                xz_play_key_changed(&play_key);__atomic_store_n(&play_key_visible,1,__ATOMIC_RELEASE);
            }
            struct xz_deck_key k;int have=deck_key_read(a->deck,&k,NULL);
            if(play_key.step==3&&(!have||!k.metadata_valid)){
                struct xz_extra_route r=xz_extra_decks_route?xz_extra_decks_route():(struct xz_extra_route){0};
                play_key.open=0;
                if(a->deck>=2&&!r.route_up){xz_ui_navigate(&ui,XZ_UI_FOUR_DECKS);panel_show_locked();}
                else if(xz_shell_runtime_key_setup_load&&xz_shell_runtime_key_setup_load(a->deck)){
                    xz_native_touch_visible(&touch,0);__atomic_store_n(&visible,0,__ATOMIC_RELEASE);
                }else {play_key.open=1;snprintf(ui.notice,sizeof ui.notice,"TRACK BROWSER UNAVAILABLE / RETRY");}
            }
            int live=have&&k.playing_valid&&k.audio_state==XZ_KEY_AUDIO_APPLIED&&
                k.applied_revision==k.request_revision&&abs(k.desired_cents-k.applied_cents)<=1;
            snprintf(ui.notice,sizeof ui.notice,"%s",live?"PROCESSED KEY VERIFIED":
                have?"KEY PREPARED / PLAY TO VERIFY REQUEST":"CONNECT ROUTE / LOAD ANALYZED TRACK");
            if(live&&play_key.step==3){play_key.setup=0;xz_play_key_changed(&play_key);}
            break;
        }
        case XZ_UI_KEY_SYNC: sync_key(a->deck); break;
        case XZ_UI_BROWSE_COLUMN:
            if (!xz_key_browser_column_choose(model.browse_columns, a->index/4, a->index%4))
                snprintf(ui.notice, sizeof(ui.notice), "BROWSE COLUMNS UNCHANGED");
            break;
        case XZ_UI_BROWSE_KEY_PREFIX: model.browse_key_prefix = a->value != 0; break;
        case XZ_UI_BROWSE_TRAFFIC: model.browse_traffic = a->value != 0; break;
        case XZ_UI_KEY_FORMAT: if (a->index >= 0 && a->index <= 2){model.key_format = a->index;modern_display.key_display=a->index;} break;
        case XZ_UI_RAM_DEBUG:
            model.ram_debug=1;
            __atomic_store_n(&xz_mods_ram_debug_v1.enabled,model.ram_debug,__ATOMIC_RELEASE);
            xz_native_skin_request_repaint();
            break;
        case XZ_UI_REKORDBOX_KEY: /* the retired switch: on = MODS MENU, off = the stock action */
            model.rekordbox_assign[0] = a->value != 0 ? XZ_REKORDBOX_MODS : XZ_REKORDBOX_STOCK;
            snprintf(ui.notice, sizeof(ui.notice), "BUTTON ABOVE LINK: %s", xz_ui_rekordbox_choice_name(model.rekordbox_assign[0]));
            break;
        case XZ_UI_REKORDBOX_ASSIGN: {
            int choice = (int)a->value;
            if ((a->index == 0 || a->index == 1) && choice >= 0 && choice < XZ_REKORDBOX_ACTIONS && (float)choice == a->value) {
                model.rekordbox_assign[a->index] = choice;
                snprintf(ui.notice, sizeof(ui.notice), "%s: %s", a->index ? "SHIFT + BUTTON ABOVE LINK" : "BUTTON ABOVE LINK", xz_ui_rekordbox_choice_name(choice));
            }
            break;
        }
        case XZ_UI_KEY_FEEDBACK:
            model.key_feedback = a->value != 0;
            __atomic_store_n(&xz_mods_key_feedback_proof_v1.enabled,model.key_feedback,__ATOMIC_RELEASE);
            break;
        case XZ_UI_KEY_SHIFT:
            key_request_locked(a->deck,a->index?XZ_KEY_STEP:XZ_KEY_RESET,a->index);
            break;
        case XZ_UI_TAKEOVER_TOGGLE:
            model.fb_takeover = !model.fb_takeover;
            __atomic_store_n(&fb_takeover_active, model.fb_takeover, __ATOMIC_RELEASE);
            snprintf(ui.notice, sizeof(ui.notice), "VJ.TOOLS VIEW %s", model.fb_takeover ? "ENABLED" : "DISABLED");
            break;
        case XZ_UI_TAKEOVER_ASSIGN:
            /* REKORDBOX (1) is retired: CONTROLS > BUTTONS gives that key VJ.TOOLS SCREEN. */
            if (a->index == XZ_TAKEOVER_LINK || a->index == XZ_TAKEOVER_ONSCREEN) {
                model.takeover_assign = a->index;
                snprintf(ui.notice, sizeof(ui.notice), "TAKEOVER KEY: %s", model.takeover_assign == XZ_TAKEOVER_LINK ? "LINK" : "ONSCREEN");
            }
            break;
        default:
            break; /* Unconnected capabilities remain disabled by the model. */
        }
        if (a->kind == XZ_UI_BAND_COLOR || a->kind == XZ_UI_BAND_COLORS_RESET || a->kind == XZ_UI_SHELL_DISPLAY || a->kind == XZ_UI_SHELL_BROWSE_COLUMNS || a->kind == XZ_UI_METRICS_TOGGLE || a->kind == XZ_UI_METRICS_CHECK || a->kind == XZ_UI_STEM_ORDER || a->kind == XZ_UI_STEM_COLOR || a->kind == XZ_UI_STEM_COLORS_RESET || a->kind == XZ_UI_STEM_PAD_CONFIG || a->kind == XZ_UI_STEMS_OVERLAY || a->kind == XZ_UI_STEM_BANK || a->kind == XZ_UI_JUMP_STEP || a->kind == XZ_UI_JUMP_ENABLE || a->kind == XZ_UI_JUMP_SHIFT || a->kind == XZ_UI_JUMP_SIZE || a->kind == XZ_UI_ENABLE || a->kind == XZ_UI_SET_THEME || a->kind == XZ_UI_SET_INTERFACE || a->kind == XZ_UI_SET_MODERN_THEME || a->kind == XZ_UI_WAVE_MODE ||
            a->kind == XZ_UI_STEM_PAGE || a->kind == XZ_UI_SHIFT_PAGES ||
            a->kind == XZ_UI_PAD_FEEDBACK || a->kind == XZ_UI_SHIFT_KEYSYNC || a->kind == XZ_UI_KEY_CHECK ||
            a->kind == XZ_UI_TAKEOVER_TOGGLE || a->kind == XZ_UI_TAKEOVER_ASSIGN ||
            a->kind == XZ_UI_BROWSE_COLUMN || a->kind == XZ_UI_BROWSE_KEY_PREFIX || a->kind == XZ_UI_BROWSE_TRAFFIC ||
            a->kind == XZ_UI_KEY_FORMAT || a->kind == XZ_UI_KEY_FEEDBACK || a->kind == XZ_UI_RAM_DEBUG ||
            a->kind == XZ_UI_REKORDBOX_KEY || a->kind == XZ_UI_REKORDBOX_ASSIGN) settings_queue(); /* DECK VIEW saves in deck_view_apply */
    }
}

/* Full-screen games take the same hardware keys as the menu: the encoder moves
   a focus ring over the game's on-screen controls and a push taps the focused
   one, BACK steps back inside the game and the menu key leaves it for MODS.
   Doom runs in its own process and receives these as queued menu keys. */
enum game { GAME_NONE, GAME_LAB, GAME_RIDER, GAME_ARCADE };
static enum game game_locked(void){
    if(xz_rhythm_lab_present())return GAME_LAB;
    if(xz_wave_rider_present())return GAME_RIDER;
    return arcade_present?GAME_ARCADE:GAME_NONE;
}
static void game_touch_locked(enum game g,int x,int y,int down,uint32_t now){
    if(g==GAME_LAB)xz_rhythm_lab_touch(x,y,down,now);
    else if(g==GAME_RIDER)xz_wave_rider_touch(x,y,down,now);
    else if(g==GAME_ARCADE)xz_arcade_touch(&arcade,x,y,down,now);
}
/* Leaves the game when the last input asked to, as the touch path does. */
static void game_settle_locked(enum game g,int arcade_was_music){
    if(g==GAME_LAB&&xz_rhythm_lab_leave_requested())rhythm_lab_to_dj();
    else if(g==GAME_RIDER&&xz_wave_rider_leave_requested())wave_rider_to_dj();
    else if(g==GAME_ARCADE&&arcade_leaving(arcade_was_music))arcade_to_dj();
}
static void game_push_locked(enum game g){
    int x,y,was_music=arcade.phase==XZ_ARCADE_MUSIC;uint32_t now=xz_game_milliseconds();
    if(!xz_game_nav_point(&game_nav,&x,&y))return;
    /* A release first clears the latch a game holds for the finger that opened
       it, which never lifts when the encoder opened the game. */
    game_touch_locked(g,-1,-1,0,now);
    if(g==GAME_ARCADE){arcade_opening_touch=0;arcade_touch_owned=0;}
    game_touch_locked(g,x,y,1,now);game_touch_locked(g,x,y,0,now);
    game_settle_locked(g,was_music);
}
static void game_back_locked(enum game g){
    int was_music=arcade.phase==XZ_ARCADE_MUSIC;uint32_t now=xz_game_milliseconds();
    if(g==GAME_LAB)xz_rhythm_lab_back(now);
    else if(g==GAME_RIDER)xz_wave_rider_back(now);
    else if(g==GAME_ARCADE)xz_arcade_back(&arcade,now);
    game_settle_locked(g,was_music);
}
static void game_to_menu_locked(enum game g){
    if(g==GAME_LAB)rhythm_lab_to_dj();
    else if(g==GAME_RIDER)wave_rider_to_dj();
    /* Same as DJ VIEW: the round is kept for when the game reopens. */
    else{xz_arcade_key(&arcade,0,XZ_ARCADE_PAUSE_KEY,xz_game_milliseconds());arcade_to_dj();}
}

static uint32_t shell_ui_refreshed_ms; /* last UI model refresh while the shell owned the screen */
/* The last frame input built under ui_mutex. Only the presenter thread renders, so it alone reads and
   writes this: a frame whose UI lock is busy reuses it instead of skipping a vblank. */
static struct xz_shell_runtime_frame_input shell_input_cache;
static int shell_input_cached;
/* The shell may own the screen only while MODS, a game, Doom and the VJ takeover leave it free. */
static int shell_eligible_locked(void){
    /* MODS (the shell's own in MODERN) opens over the VJ takeover, as the panel does in CLASSIC. */
    return !touch.visible&&game_locked()==GAME_NONE&&!xz_doom_running()&&
        (!__atomic_load_n(&fb_takeover_active,__ATOMIC_ACQUIRE)||(xz_shell_runtime_mods_open&&xz_shell_runtime_mods_open()));
}
static int shell_owns_locked(void){return xz_shell_runtime_owns&&shell_eligible_locked()&&xz_shell_runtime_owns();}
static void shell_frame_input_locked(struct xz_shell_runtime_frame_input *in){
    memset(in,0,sizeof(*in));in->eligible=shell_eligible_locked();in->theme=model.theme;in->key_controls_open=play_key.open;
    in->controller_locale=model.controller_locale;
    in->browse_preview=model.browse_preview;
    in->stems_known=1;in->stems_on=model.modern_stems!=XZ_STEM_LAYOUT_HIDDEN;
    for(int d=0;d<2;d++){
        /* 3-stem local playback only: with four stems or PC stems the bits mean something else. */
        in->stem_muted_known[d]=(model.deck[d].ready&XZ_UI_STEM)&&model.deck[d].pc_stems.source==XZ_UI_SOURCE_LOCAL&&model.deck[d].stem_count!=4;
        in->stem_muted[d]=model.deck[d].muted;in->stem_bypass[d]=model.deck[d].bypass!=0;
    }
    in->stem_rgb_known=1;for(int i=0;i<3;i++)in->stem_rgb[i]=xz_ui_model_stem_color(&model,i);
    in->four_decks_attention=xz_ui_four_decks_attention(&model);
    in->look_known=1;in->ui_interface=model.ui_interface;in->modern_theme=model.modern_theme;in->wave_mode=model.wave_mode;
    in->stem_order=model.stem_order;in->key_format=model.key_format;in->key_feedback=model.key_feedback;in->browse_traffic=model.browse_traffic;
    for(int i=0;i<3;i++){in->stem_colors[i]=model.stem_colors[i];in->band_colors[i]=model.band_colors[i];}
    in->modern_browse_columns=modern_browse_columns;
    in->modern_wave_color=modern_display.modern_wave_color;in->wave_position=modern_display.wave_position;
    in->key_display=modern_display.key_display;in->wave_zoom=modern_display.wave_zoom;
    in->usb_colors=modern_display.usb_colors;in->hidden_parts=modern_display.hidden_parts;
    in->stems_overlay=model.modern_stems;
    in->rekordbox_known=1;in->rekordbox_assign[0]=model.rekordbox_assign[0];in->rekordbox_assign[1]=model.rekordbox_assign[1];
    for(int d=0;d<2;d++){in->stem_level_known[d]=in->stem_muted_known[d];for(int i=0;i<3;i++)in->stem_level[d][i]=model.deck[d].levels[i];}
    /* DJ profiles for the shell's MY SETTINGS panel, copied (the view changes under this lock). */
    if(model.dj){
        in->dj_known=1;in->dj_count=model.dj->count<XZ_SHELL_RUNTIME_DJ?model.dj->count:XZ_SHELL_RUNTIME_DJ;
        for(int i=0;i<in->dj_count;i++){
            const struct xz_ui_dj_entry *e=&model.dj->entry[i];struct xz_shell_runtime_dj *o=&in->dj[i];
            snprintf(o->name,sizeof(o->name),"%s",e->name?e->name:"");
            o->usb=e->usb;o->stick_active=e->stick_active;o->locked=e->locked;o->damaged=e->damaged;
            o->has_accent=e->has_accent;o->accent=e->accent;
        }
    }
    for(int deck=0;deck<2;deck++){
        if(!key_valid[deck])continue;
        struct xz_shell_runtime_key *k=&in->key[deck];int other=1-deck,cents=0,other_cents=0;
        int format=model.key_format==1?XZ_KEY_CLASSIC:model.key_format==2?XZ_KEY_CAMELOT:key_snapshot[deck].format;
        xz_music_key_format(key_snapshot[deck].original,format,k->original);
        if(!model.key_feedback){
            memcpy(k->text,k->original,sizeof k->text);k->valid=1;
            continue;
        }
        if(key_available)xz_key_get_applied_cents(deck,key_snapshot[deck].epoch,&cents);
        k->shift=(int)round(cents*.01);
        struct xz_playing_key mine,master;
        if(!xz_key_playing(&key_snapshot[deck],cents,&mine))continue;
        xz_music_key_format(mine.key,format,k->text);k->valid=1;
        k->cents=mine.cents;k->synced=key_sync[deck].valid;
        if(key_valid[other]&&key_snapshot[other].master&&!key_snapshot[deck].master){
            if(key_available)xz_key_get_applied_cents(other,key_snapshot[other].epoch,&other_cents);
            k->matches_master=xz_key_playing(&key_snapshot[deck],cents,&mine)&&
                xz_key_playing(&key_snapshot[other],other_cents,&master)&&xz_key_match(mine,master)==XZ_KEY_COMPATIBLE;
        }
    }
}
/* ---- DECK VIEW (ui/deck_view.h, ui/route_card.h) ----
   deck_view_apply is the one entry point every view change takes: the picker's tiles and its encoder and
   the decks 3 and 4 card's TWO DECKS, in both interfaces (MODERN reaches it through XZ_SHELL_UI_DECK_VIEW,
   taken just below on the same input thread), MODS DECK VIEW, the button next to LINK and the retired wave
   order rows. Called with ui_mutex held. It applies the saved deck_view setting (deck_view_save_locked):
   xz_deck_layout_get() returns the new view from the next frame on, and settings_queue saves it.
   CLASSIC keeps its picker and the encoder's focus here. badge() paints the button and the picker; the wave
   transaction (native_wave_runtime.c) paints the card and publishes it for touch. */
enum { DV_FOCUS_NONE, DV_FOCUS_PRIMARY, DV_FOCUS_TWO, DV_FOCUS_BUTTON };
static struct { int open,tile,focus,contact,back_contact,painted,painted_open,painted_view; uint32_t input_at; } dv;
static void dv_publish_card(void){
    if(xz_native_four_card_input)xz_native_four_card_input(dv.input_at,
        dv.focus==DV_FOCUS_PRIMARY?XZ_ROUTE_CARD_HIT_PRIMARY:dv.focus==DV_FOCUS_TWO?XZ_ROUTE_CARD_HIT_TWO:-1);
}
static void dv_input(uint32_t now){dv.input_at=now?now:1;dv_publish_card();}
static int dv_recent(uint32_t now){return dv.input_at&&now-dv.input_at<(uint32_t)XZ_DECK_VIEW_PICKER_TIMEOUT_MS;}
static int dv_picker_live(uint32_t now){return dv.open&&dv_recent(now);}
static int dv_focus_live(uint32_t now){return !dv.open&&dv.focus!=DV_FOCUS_NONE&&dv_recent(now);}
static int deck_view_encoder_locked(int delta,int push,int press);
static void deck_view_apply(int view){
    if((unsigned)view>=XZ_LAYOUT_COUNT)return;
    deck_view_save_locked(view);
    dv.open=0;dv.focus=DV_FOCUS_NONE;dv_publish_card();
    xz_native_skin_request_repaint();
}
static void shell_ui_requests_locked(void){
    unsigned requests=xz_shell_runtime_take_ui_requests?xz_shell_runtime_take_ui_requests():0;
    if((requests&XZ_SHELL_UI_DECK_VIEW)&&xz_shell_runtime_take_deck_view){
        int view=xz_shell_runtime_take_deck_view();
        if(view>=0)deck_view_apply(view);
    }
    if(requests&XZ_SHELL_UI_TOGGLE_STEMS){
        int two=xz_deck_layout_get()==XZ_LAYOUT_TWO;
        struct xz_ui_action act={.kind=XZ_UI_STEMS_OVERLAY,.index=XZ_UI_STEMS_BUTTON};apply(NULL,&act,1);
        /* The layout changes as on the stock button, but rows only appear once stems are on. (4 DECKS: the
           press said what it did itself.) */
        if(two&&!requested_stems&&!pc_present&&xz_shell_runtime_notice)xz_shell_runtime_notice("STEM ROWS SHOW ONCE STEMS ARE ON IN MODS");
    }
    /* The shell's stem mute buttons: through the pads' own mute toggle, so the stem engine and the
       hot cue mute preservation see exactly what a pad press does. */
    /* The shell's APPEARANCE tab: the MODS panel's own actions (side effects and settings save included).
       An INTERFACE switch to CLASSIC stops the shell from inside apply(); nothing below needs it. */
    if((requests&XZ_SHELL_UI_ACTION)&&xz_shell_runtime_take_ui_action){
        int kind,deck,index;float value;
        while(xz_shell_runtime_take_ui_action(&kind,&deck,&index,&value)){
            struct xz_ui_action act={.kind=(enum xz_ui_action_kind)kind,.phase=XZ_UI_PRESS,.deck=deck,.index=index,.value=value};apply(NULL,&act,1);
        }
    }
    /* The shell's DJ PROFILE row: the same pick the MODS panel's DJ PROFILE page makes. */
    if((requests&XZ_SHELL_UI_DJ_PICK)&&xz_shell_runtime_take_dj_pick){
        int entry=xz_shell_runtime_take_dj_pick();
        if(entry>=0&&model.dj&&entry<model.dj->count&&xz_dj_runtime_pick)xz_dj_runtime_pick(entry);
    }
    if((requests&XZ_SHELL_UI_STEM_MUTE)&&xz_shell_runtime_take_stem_mute){
        int deck,stem,value;
        while(xz_shell_runtime_take_stem_mute(&deck,&stem,&value)){
            if(deck<0||deck>1||stem<0||stem>2)continue;
            int muted=(int)((model.deck[deck].muted>>stem)&1u);
            if(muted!=value){struct xz_ui_action act={.kind=XZ_UI_MUTE,.phase=XZ_UI_PRESS,.deck=deck,.index=stem};apply(NULL,&act,1);}
        }
    }
    /* MODS from the shell: the MODS key, MENU or the MODS button (the page MODS was last on), or a MODS section
       on UTILITY's rail (that section, focus on its first control). The opening contact stays the shell's. */
    int mods_page=(requests&XZ_SHELL_UI_OPEN_MODS)&&xz_shell_runtime_take_mods_page?xz_shell_runtime_take_mods_page():-1;
    if((requests&XZ_SHELL_UI_OPEN_MODS)&&!touch.visible){
        refresh_local();sdk_refresh();
        if(mods_page>=0){xz_ui_navigate(&ui,(enum xz_ui_page)mods_page);ui.focus_kind=XZ_UI_NONE;}
        else if(sdk_session.active&&ui.page!=XZ_UI_APPS_SETUP)xz_ui_navigate(&ui,XZ_UI_APPS);
        panel_show_locked();
        inline_layout_publish();xz_native_skin_request_repaint();
    }
}
static int shell_touch_locked(void *self,const struct xz_touch_status *status,const void *mode){
    if(!xz_shell_runtime_touch)return 0;
    struct xz_touch_status previous;memcpy(&previous,(const char *)self+4,sizeof(previous));
    int route=xz_shell_runtime_touch(shell_eligible_locked(),model.theme,(int)status->x,(int)status->y,!!status->down,!!previous.down);
    if(route==XZ_SHELL_TOUCH_TO_STOCK)return 0;
    /* A stock gesture in progress ends before the shell takes the contact. */
    if(route==XZ_SHELL_TOUCH_TAKEN_RELEASE_STOCK){previous.down=0;stock_touch(self,&previous,mode);}
    shell_ui_requests_locked();
    return 1;
}
static void (*stock_global_key)(void *,const void *);
/* REKORDBOX (key code 0x202): the press decided at the onKey hook, for the handler hook that follows it. */
#define REKORDBOX_KEY_CODE 0x202u
static struct { int valid, action; uint32_t at; } rekordbox_latch;
static int rekordbox_shift_native(void){
    for(int deck=0;deck<2;deck++){
        /* PlayerInnards +0x34 is SHIFT, as SHIFT + MASTER TEMPO reads it. */
        const unsigned char *innards=key_player(deck);unsigned char shift=0;
        if(innards&&!xz_read_memory((uint32_t)(uintptr_t)innards+0x34,&shift,1)&&shift)return 1;
    }
    return 0;
}
static int (*rekordbox_shift_held)(void)=rekordbox_shift_native;
static int rekordbox_decide_locked(void){
    int action=model.rekordbox_assign[rekordbox_shift_held()?1:0];
    return action>=0&&action<XZ_REKORDBOX_ACTIONS?action:XZ_REKORDBOX_STOCK;
}
static int menu_back_contact;
/* MENU pressed while MODS showed in MODERN: 1 until it is held or let go, 2 once the hold opened UTILITY. */
static int modern_menu_contact;
/* CLASSIC MENU (key 0x20b), decided at the press by ui/menu_key.h: 1 opens MODS and 2 closes it at a short
   release; 0 leaves the press to stock (its own job there), and a hold always does. */
#define MENU_KEY_CODE 0x20bu
static int classic_menu_contact;
static void mods_panel_open_locked(void);
static enum game game_locked(void);
/* Where a CLASSIC short MENU lands: MODS, a game, the VJ view, then the stock screen. The stock probe (shell
   runtime) says the play screen shows with no stock menu over it; GRID ADJUST cannot be read, so the play
   screen counts as PLAY (menu_key.h flags that row for the owner to confirm). */
static enum xz_menu_place classic_menu_place_locked(void){
    if(touch.visible)return XZ_MENU_AT_MODS;
    if(game_locked()!=GAME_NONE||xz_doom_running())return XZ_MENU_AT_GAME;
    if(model.fb_takeover)return XZ_MENU_AT_TAKEOVER;
    if(!xz_native_play_visible())return XZ_MENU_AT_OTHER;
    int play=xz_shell_runtime_stock_play?xz_shell_runtime_stock_play():-1;
    return play==0?XZ_MENU_AT_STOCK_MENU:XZ_MENU_AT_PLAY;
}
static int menu_owns_keys_locked(void);
static void menu_back_key_hook(void *self,const void *input){
    if(stock_dialog_up()){stock_global_key(self,input);return;}
    uint16_t key=0;unsigned operation=0;
    if(input){memcpy(&key,(const unsigned char *)input+8,2);operation=((const unsigned char *)input)[11]&15;}
    int consumed=0;
    /* Every key passes here (jog included), so only keys in the shell's table take the lock. */
    unsigned channel=input?((const unsigned char *)input)[10]:0;
    if(input && channel>=1 && channel<=2 && xz_runtime_deck_early_key && xz_runtime_deck_early_key(key) && xz_runtime_deck_global_input &&
       xz_runtime_deck_global_input(xz_runtime_deck_games_pending&&xz_runtime_deck_games_pending()?key_player((int)channel-1):NULL,input)) return;
    /* A side driving DECK 3 / 4: its TIME MODE and QUANTIZE reach neither the shell nor stock; its LOAD goes to
       the software deck through the shell (here) or the classic divert (passed on to stock). */
    int deck_key=input&&xz_runtime_deck_global_key?xz_runtime_deck_global_key(key,channel,operation):0;
    if(deck_key>=XZ_RUNTIME_KEY_SHELL_LOAD){
        if(xz_shell_runtime_extra_load_key&&__atomic_load_n(&started,__ATOMIC_ACQUIRE)){
            pthread_mutex_lock(&ui_mutex);
            xz_shell_runtime_extra_load_key(shell_eligible_locked(),model.theme,deck_key-XZ_RUNTIME_KEY_SHELL_LOAD);
            shell_ui_requests_locked();
            pthread_mutex_unlock(&ui_mutex);
        }
        return;
    }
    if(deck_key)return;
    /* REKORDBOX: decided once, here at the press (SHIFT read now), for both the shell and the handler hook. */
    int rekordbox_press=input&&key==REKORDBOX_KEY_CODE&&operation==0&&__atomic_load_n(&started,__ATOMIC_ACQUIRE);
    if(rekordbox_press){
        pthread_mutex_lock(&ui_mutex);
        rekordbox_latch.action=rekordbox_decide_locked();rekordbox_latch.at=xz_game_milliseconds();rekordbox_latch.valid=1;
        if(xz_shell_runtime_rekordbox_action)xz_shell_runtime_rekordbox_action(rekordbox_latch.action);
        pthread_mutex_unlock(&ui_mutex);
    }
    /* MODERN with MODS open: the screen keys the shell owns leave MODS and do their job on the screen under it,
       in the same press (as they did from MODERN's own MODS screen); a short MENU closes MODS, a held MENU opens
       UTILITY. BACK stays the menu's, the button next to LINK has its own path. */
    if(input&&xz_shrt_key_lookup&&__atomic_load_n(&started,__ATOMIC_ACQUIRE)){
        const struct xz_shrt_key_map *map=xz_shrt_key_lookup(key,channel);
        if(map&&map->key!=XZ_SHELL_KEY_BACK&&key!=REKORDBOX_KEY_CODE){
            pthread_mutex_lock(&ui_mutex);
            int menu=map->key==XZ_SHELL_KEY_MENU,own=model.ui_interface==XZ_UI_INTERFACE_MODERN&&menu_owns_keys_locked();
            if(menu&&(own||modern_menu_contact)){
                if(operation==0)modern_menu_contact=1;
                else if(operation==1&&modern_menu_contact==1){
                    modern_menu_contact=2;
                    struct xz_ui_action open={.kind=XZ_UI_OPEN_UTILITY,.phase=XZ_UI_PRESS,.deck=-1};apply(NULL,&open,1);
                }else if(operation>=2){
                    if(modern_menu_contact==1&&touch.visible){struct xz_ui_action close={.kind=XZ_UI_CLOSE};apply(NULL,&close,1);}
                    modern_menu_contact=0;
                }
                xz_native_skin_request_repaint();pthread_mutex_unlock(&ui_mutex);return;
            }
            if(own&&operation==0){
                struct xz_ui_action release[XZ_UI_ACTIONS];size_t n=xz_ui_cancel(&ui,release);apply(NULL,release,n);
                struct xz_ui_action close={.kind=XZ_UI_CLOSE};apply(NULL,&close,1);
                inline_layout_publish();xz_native_skin_request_repaint();
            }
            pthread_mutex_unlock(&ui_mutex);
        }
    }
    /* CLASSIC: a short MENU opens and closes MODS where it has no stock job (ui/menu_key.h, the table MODERN's
       shell reads too). The key always reaches stock whole, so its own jobs run and a held MENU opens stock's
       UTILITY with no extra press (and closes MODS, just below). */
    if(input&&key==MENU_KEY_CODE&&__atomic_load_n(&started,__ATOMIC_ACQUIRE)){
        pthread_mutex_lock(&ui_mutex);
        if(model.ui_interface!=XZ_UI_INTERFACE_CLASSIC)classic_menu_contact=0;
        else if(operation==0){
            enum xz_menu_job job=xz_menu_rule(classic_menu_place_locked())->job;
            classic_menu_contact=job==XZ_MENU_JOB_MODS?1:job==XZ_MENU_JOB_CLOSE_MODS?2:0;
        }else if(operation==1)classic_menu_contact=0;
        else{
            if(classic_menu_contact==1&&!touch.visible&&game_locked()==GAME_NONE)mods_panel_open_locked();
            else if(classic_menu_contact==2&&touch.visible){struct xz_ui_action close={.kind=XZ_UI_CLOSE};apply(NULL,&close,1);}
            if(classic_menu_contact){inline_layout_publish();xz_native_skin_request_repaint();}
            classic_menu_contact=0;
        }
        pthread_mutex_unlock(&ui_mutex);
    }
    if(key==0x20b&&operation==1){
        pthread_mutex_lock(&ui_mutex);
        if(model.ui_interface==XZ_UI_INTERFACE_CLASSIC&&touch.visible){
            struct xz_ui_action release[XZ_UI_ACTIONS];size_t n=xz_ui_cancel(&ui,release);apply(NULL,release,n);
            panel_hide_locked(0);inline_layout_publish();xz_native_skin_request_repaint();
        }
        pthread_mutex_unlock(&ui_mutex);
    }
    if(key==0x4208){
        pthread_mutex_lock(&ui_mutex);
        uint32_t now=xz_game_milliseconds();
        if(dv.back_contact){consumed=1;if(operation==2||operation==3)dv.back_contact=0;}
        else if(operation==0&&model.ui_interface==XZ_UI_INTERFACE_CLASSIC&&
                (dv_picker_live(now)||(dv.painted&&dv.painted_open)||dv_focus_live(now))){
            dv.open=0;dv.focus=DV_FOCUS_NONE;dv_publish_card();dv.back_contact=consumed=1;xz_native_skin_request_repaint();
        }
        pthread_mutex_unlock(&ui_mutex);if(consumed)return;
    }
    if(key==0x4208){
        pthread_mutex_lock(&ui_mutex);
        if(play_key_back_contact){consumed=1;if(operation==2||operation==3)play_key_back_contact=0;}
        else if(operation==0&&play_key.open){
            if(play_key.setup){play_key.setup=0;xz_play_key_changed(&play_key);}else play_key.open=0;
            __atomic_store_n(&play_key_visible,play_key.open,__ATOMIC_RELEASE);
            play_key_dirty=1;play_key_back_contact=consumed=1;xz_native_skin_request_repaint();
        }
        pthread_mutex_unlock(&ui_mutex);if(consumed)return;
    }
    if(operation==0&&xz_shrt_key_lookup&&xz_shrt_key_lookup(key,channel)){
        pthread_mutex_lock(&ui_mutex);
        if(play_key.open){play_key.open=0;play_key_dirty=1;__atomic_store_n(&play_key_visible,0,__ATOMIC_RELEASE);xz_native_skin_request_repaint();}
        pthread_mutex_unlock(&ui_mutex);
    }
    if(input&&xz_shell_runtime_key&&xz_shrt_key_lookup&&xz_shrt_key_lookup(key,channel)&&__atomic_load_n(&started,__ATOMIC_ACQUIRE)){
        pthread_mutex_lock(&ui_mutex);
        consumed=xz_shell_runtime_key(shell_eligible_locked(),model.theme,key,channel,operation);
        /* The shell ran this press itself (MODS, STEMS): stock and the handler hook never see it. */
        if(consumed&&rekordbox_press)rekordbox_latch.valid=0;
        shell_ui_requests_locked();
        pthread_mutex_unlock(&ui_mutex);
        if(consumed)return;
    }
    if(key==0x4208){
        pthread_mutex_lock(&ui_mutex);
        int live=operation==0&&__atomic_load_n(&started,__ATOMIC_ACQUIRE);
        if(menu_back_contact){consumed=1;if(operation==2||operation==3)menu_back_contact=0;}
        else if(live&&game_locked()!=GAME_NONE){
            menu_back_contact=consumed=1;game_back_locked(game_locked());xz_native_skin_request_repaint();
        }
        else if(live&&!touch.visible&&xz_doom_nav(XZ_GAME_NAV_BACK))menu_back_contact=consumed=1;
        else if(live&&touch.visible){
            struct xz_ui_action actions[XZ_UI_ACTIONS];int contact=ui.down||touch.capture;
            menu_back_contact=1;consumed=1;size_t n=xz_ui_back(&ui,actions);apply(NULL,actions,n);
            if(contact)touch.opening_contact=1;
            inline_layout_publish();xz_native_skin_request_repaint();
        }
        pthread_mutex_unlock(&ui_mutex);
    }
    if(!consumed&&stock_global_key)stock_global_key(self,input);
}
/* Hardware navigation. While MODS is open the browse encoder belongs to it:
   turning moves focus (or edits a value), pushing activates. A running game
   gets the same keys (see game_locked); otherwise they go to the firmware. */
static int menu_owns_keys_locked(void){
    return touch.visible&&game_locked()==GAME_NONE;
}
/* The MODS panel by hardware: a running game or Doom is left first (Doom exits on its own; MODS opens on the
   next press), an open panel closes, otherwise it opens. */
static void mods_panel_open_locked(void){
    refresh_local();sdk_refresh();
    panel_show_locked();
}
static void mods_panel_toggle_locked(void){
    enum game game=game_locked();
    if(!touch.visible&&game==GAME_NONE&&xz_doom_nav(XZ_GAME_NAV_QUIT)){}
    else if(game!=GAME_NONE){game_to_menu_locked(game);mods_panel_open_locked();inline_layout_publish();}
    else if(touch.visible){struct xz_ui_action close={.kind=XZ_UI_CLOSE};apply(NULL,&close,1);}
    else{
        mods_panel_open_locked();
        if(sdk_session.active&&ui.page!=XZ_UI_APPS_SETUP)xz_ui_navigate(&ui,XZ_UI_APPS);
        inline_layout_publish();
    }
    xz_native_skin_request_repaint();
}
/* ---- REKORDBOX button: one decider ----
   rekordbox_decide_locked picks the slot (either deck's SHIFT held: SHIFT + PRESS) and its action. The onKey
   hook decides at the press and latches it; in MODERN it gives that action to the shell first, which
   swallows only its own actions (MODS, STEMS: they run there and nothing else sees the key) and hands the
   screen to stock visibly for the stock action. Every other press reaches UiKey_rekordbox, whose hook runs
   the latched action once and calls stock only for the stock action. In CLASSIC the shell is stopped, so the
   hook alone acts. Without the onKey hook (or a stale latch) the handler hook decides itself. */
/* Runs one REKORDBOX action under ui_mutex; 1 = the caller runs stock's own handler. */
static int rekordbox_run_locked(int action){
    if(action==XZ_REKORDBOX_NOTHING)return 0;
    /* A full-screen game or Doom owns the screen: the key leaves it for MODS, as it always has. */
    if(game_locked()!=GAME_NONE||(!touch.visible&&xz_doom_running())){mods_panel_toggle_locked();return 0;}
    int modern=model.ui_interface==XZ_UI_INTERFACE_MODERN;
    switch(action){
    case XZ_REKORDBOX_MODS:
        /* The one MODS menu, in both interfaces: opens it on the page it was last on, or closes it. */
        mods_panel_toggle_locked();return 0;
    case XZ_REKORDBOX_INTERFACE:{
        struct xz_ui_action act={.kind=XZ_UI_SET_INTERFACE,.phase=XZ_UI_PRESS,.deck=-1,
            .index=modern?XZ_UI_INTERFACE_CLASSIC:XZ_UI_INTERFACE_MODERN};
        apply(NULL,&act,1);xz_native_skin_request_repaint();return 0;}
    case XZ_REKORDBOX_VJ_SCREEN:{
        const struct xz_ui_connection *v=&model.connection;
        if(!model.fb_takeover&&!(v->ready&&v->enabled&&v->connected)){
            /* Nothing to show: say so where the DJ is looking; on the classic screen, on the VJ.TOOLS page. */
            const char *why=!v->ready||!v->enabled?"VJ.TOOLS LINK IS OFF":"VJ.TOOLS IS NOT CONNECTED";
            if(modern&&!touch.visible&&shell_owns_locked()&&xz_shell_runtime_notice)xz_shell_runtime_notice(why);
            else{
                xz_ui_navigate(&ui,XZ_UI_CONNECTION);
                if(!touch.visible)mods_panel_open_locked();
                snprintf(ui.notice,sizeof(ui.notice),"%s",why);
                inline_layout_publish();xz_native_skin_request_repaint();
            }
            return 0;
        }
        struct xz_ui_action act={.kind=XZ_UI_TAKEOVER_TOGGLE,.phase=XZ_UI_PRESS,.deck=-1};
        apply(NULL,&act,1);xz_native_skin_request_repaint();return 0;}
    case XZ_REKORDBOX_DECK_VIEW:{
        /* 2 -> 4 (1 2 3 4) -> 4 (3 1 2 4) -> 2, saved, and said where the DJ is looking. */
        struct xz_ui_action act={.kind=XZ_UI_DECK_VIEW,.phase=XZ_UI_PRESS,.deck=-1,.value=(float)xz_deck_layout_next(model.deck_view)};
        apply(NULL,&act,1);
        char text[64];snprintf(text,sizeof(text),"DECK VIEW: %s",xz_deck_layout_name(model.deck_view));
        if(modern&&!touch.visible&&xz_shell_runtime_notice)xz_shell_runtime_notice(text);
        else{xz_ui_notice_set(&async_notice,text,xz_game_milliseconds());snprintf(ui.notice,sizeof ui.notice,"%s",async_notice.text);}
        xz_native_skin_request_repaint();
        return 0;}
    case XZ_REKORDBOX_STEMS:{
        /* The STEMS button's own press (a layout step, or show / hide in 4 DECKS), and the shell header's note
           when rows cannot show yet. */
        int two=xz_deck_layout_get()==XZ_LAYOUT_TWO;
        struct xz_ui_action act={.kind=XZ_UI_STEMS_OVERLAY,.index=XZ_UI_STEMS_BUTTON};apply(NULL,&act,1);
        if(two&&modern&&!requested_stems&&!pc_present&&xz_shell_runtime_notice)xz_shell_runtime_notice("STEM ROWS SHOW ONCE STEMS ARE ON IN MODS");
        xz_native_skin_request_repaint();return 0;}
    default:return 1; /* the stock action */
    }
}
static int uikey_rekordbox_hook(void *arg0,void *arg1,void *arg2,void *arg3) {
    if(stock_dialog_up())return stock_uikey_rekordbox?stock_uikey_rekordbox(arg0,arg1,arg2,arg3):0;
    int stock=1;
    if(__atomic_load_n(&started,__ATOMIC_ACQUIRE)){
        pthread_mutex_lock(&ui_mutex);
        int fresh=rekordbox_latch.valid&&(uint32_t)(xz_game_milliseconds()-rekordbox_latch.at)<2000u;
        int action=fresh?rekordbox_latch.action:rekordbox_decide_locked();
        rekordbox_latch.valid=0;
        stock=rekordbox_run_locked(action);
        pthread_mutex_unlock(&ui_mutex);
    }
    if(!stock)return 1;
    return stock_uikey_rekordbox ? stock_uikey_rekordbox(arg0,arg1,arg2,arg3) : 0;
}
static int (*stock_encoder_rotate)(int);
static int encoder_rotate_hook(int delta) {
    if(stock_dialog_up())return stock_encoder_rotate(delta);
    if(__atomic_load_n(&started,__ATOMIC_ACQUIRE)){
        pthread_mutex_lock(&ui_mutex);
        if(play_key.open){pthread_mutex_unlock(&ui_mutex);return 1;}
        if(deck_view_encoder_locked(delta,0,0)){pthread_mutex_unlock(&ui_mutex);return 1;}
        int owned=menu_owns_keys_locked(),shell=0;
        if(owned&&delta){
            struct xz_ui_action actions[XZ_UI_ACTIONS];
            size_t n=xz_ui_encoder_turn(&ui,&model,delta,actions);
            if(n)apply(NULL,actions,n);
            inline_layout_publish();
        }
        else if(game_locked()!=GAME_NONE){owned=1;if(delta)xz_game_nav_turn(&game_nav,delta);}
        else if(!touch.visible&&delta&&xz_shell_runtime_encoder_turn&&
                xz_shell_runtime_encoder_turn(shell_eligible_locked(),model.theme,delta))shell=1;
        else if(!touch.visible&&delta){
            /* One Doom menu step per detent, bounded like the menu's own turn. */
            int steps=delta<0?-delta:delta;if(steps>4)steps=4;
            for(int i=0;i<steps;i++)owned|=xz_doom_nav(delta>0?XZ_GAME_NAV_DOWN:XZ_GAME_NAV_UP);
        }
        if(shell)shell_ui_requests_locked();
        pthread_mutex_unlock(&ui_mutex);
        /* The shell redraws itself; a stock repaint would only cost a full native redraw. */
        if(shell)return 1;
        if(owned){xz_native_skin_request_repaint();return 1;}
    }
    return stock_encoder_rotate?stock_encoder_rotate(delta):0;
}
static int (*stock_encoder_push)(void *,void *,void *,void *);
static int encoder_push_hook(void *arg0,void *arg1,void *arg2,void *arg3) {
    if(stock_dialog_up())return stock_encoder_push(arg0,arg1,arg2,arg3);
    if(__atomic_load_n(&started,__ATOMIC_ACQUIRE)){
        pthread_mutex_lock(&ui_mutex);
        if(play_key.open){pthread_mutex_unlock(&ui_mutex);return 1;}
        int owned=menu_owns_keys_locked(),shell=0;
        /* Stock calls this for the press, the hold, the release and again on later key passes while the
           entry's state stays set: the panel, games and Doom act on the press phase (flags 1) only, and
           still swallow the rest, or one click activates the focused control twice. */
        uint32_t flags=1;if(arg0)memcpy(&flags,arg0,sizeof(flags));
        int press=flags==1;
        if(deck_view_encoder_locked(0,1,press)){pthread_mutex_unlock(&ui_mutex);return 1;}
        if(owned){
            if(press){
                struct xz_ui_action actions[XZ_UI_ACTIONS];
                size_t n=xz_ui_encoder_push(&ui,&model,actions);
                if(n)apply(NULL,actions,n);
                inline_layout_publish();
            }
        }
        else if(game_locked()!=GAME_NONE){owned=1;if(press)game_push_locked(game_locked());}
        else if(!touch.visible&&arg0&&xz_shell_runtime_encoder_push&&shell_eligible_locked()){
            /* pushKey entry {flags, 0, state, handler}: the shell reads the press and hold phases from it. */
            uint32_t entry[3];memcpy(entry,arg0,sizeof(entry));
            shell=xz_shell_runtime_encoder_push(1,model.theme,entry[0],entry[2]);
            if(!shell)owned=press?xz_doom_nav(XZ_GAME_NAV_ENTER):xz_doom_running();
        }
        else if(!touch.visible)owned=press?xz_doom_nav(XZ_GAME_NAV_ENTER):xz_doom_running();
        if(shell)shell_ui_requests_locked();
        pthread_mutex_unlock(&ui_mutex);
        if(shell){
            /* A SOURCE LINK / EXPORT MODE select: stock's key for it, here on the native key task (this
               handler runs from BrowseKeyProcessing), with no lock held. */
            int which=xz_shell_runtime_take_stock_key?xz_shell_runtime_take_stock_key():-1;
            if(which>=0){int ran=xz_ui_runtime_stock_source_key(which,1);if(xz_shell_runtime_stock_key_ran)xz_shell_runtime_stock_key_ran(which,ran);}
            return 1;
        }
        if(owned){xz_native_skin_request_repaint();return 1;}
    }
    return stock_encoder_push?stock_encoder_push(arg0,arg1,arg2,arg3):0;
}

/* Called under ui_mutex before a physical stem action as well as rendering.
   A late render must never erase a new-track pad press after the engine accepted it. */
static void stem_generation_sync_locked(int deck,const struct xz_audio_status *status){
    int count=xz_stem_count((int)status->stem_count);
    int new_generation=displayed_generation[deck]!=status->generation;
    if(!new_generation&&xz_stem_count(model.deck[deck].stem_count)==count)return;
    displayed_generation[deck]=status->generation;
    struct xz_ui_action cancelled[XZ_UI_ACTIONS];
    if(inline_contact&&inline_contact_deck==deck){xz_ui_cancel(&inline_ui,cancelled);inline_cancelled=1;}
    if(ui.deck==deck){xz_ui_cancel(&ui,cancelled);if(touch.capture)touch.opening_contact=1;}
    model.deck[deck].stem_count=pads.count[deck]=count;
    if(new_generation){
        pads.muted[deck]=model.deck[deck].muted=0;
        for(unsigned role=0;role<4;role++)model.deck[deck].levels[role]=1;
        model.deck[deck].bypass=0;
    }else{
        /* The final 4-stem publication may follow a provisional 3-stem row.
           Preserve controls on roles that still exist; retire only missing bass. */
        pads.muted[deck]&=(1u<<(unsigned)count)-1u;
        model.deck[deck].muted=pads.muted[deck];
        if(count==3)model.deck[deck].levels[3]=1;
    }
    levels(deck);
}

static void refresh_local(void) {
    deck_status_refresh_locked();
    model.settings_flash=settings_flash_at&&(uint32_t)(xz_game_milliseconds()-settings_flash_at)<3000u;
    model.doom_ready=xz_doom_ready();
    xz_doom_browser_read(ui.wad_offset,&model.doom);ui.wad_offset=model.doom.offset;
    xz_update_read(&model.update);
    support_refresh();
    if(!doom_phase && xz_doom_running()) {
        doom_previous_takeover=model.fb_takeover; doom_phase=2; model.fb_takeover=1;
        __atomic_store_n(&fb_takeover_active,1,__ATOMIC_RELEASE);
    }
    if(doom_phase) {
        int running=xz_doom_running();
        if(running) doom_phase=2;
        if(!running && (doom_phase==2 || (uint32_t)(xz_game_milliseconds()-doom_launched_at)>5000)) {
            doom_phase=0; model.fb_takeover=doom_previous_takeover;
            __atomic_store_n(&fb_takeover_active,model.fb_takeover,__ATOMIC_RELEASE);
        }
    }
    model.theme_status=xz_native_skin_status();
    model.key_browser_status=xz_key_browser_status();
    if(model.dj&&xz_dj_runtime_current&&xz_dj_runtime_usb_of){
        /* The header shows the profile of the stick the master deck plays from. */
        int master=-1,playing=-1;
        for(int deck=0;deck<2;deck++){
            int usb=xz_dj_runtime_usb_of(audio_status[deck].path);
            if(usb<0)continue;
            if(key_valid[deck]&&key_snapshot[deck].master)master=usb;
            else if(playing<0)playing=usb;
        }
        xz_dj_runtime_current(master,playing,xz_ui_runtime_profile_selected());
        /* The DJ PROFILE page and the editor show one profile's full logo. */
        if(touch.visible&&(ui.page==XZ_UI_PROFILE_SETUP||ui.page==XZ_UI_PROFILE_EDIT)&&xz_dj_runtime_detail){
            int shown=xz_ui_profile_shown(&ui,&model);
            if(shown>=0)xz_dj_runtime_detail(shown);
        }
        /* A logo copied into DJPROFILE/LOGOS is picked for a new profile. */
        int logo=xz_ui_profile_autopick(&ui,&model);
        if(logo>=0&&xz_dj_runtime_preview)xz_dj_runtime_preview(logo);
    }
    (void)browse_context();
    inline_layout_publish();
    __atomic_add_fetch(&xz_prepared_proof_v1.sequence,1,__ATOMIC_SEQ_CST);
    __atomic_add_fetch(&xz_mods_key_proof_v1.sequence,1,__ATOMIC_SEQ_CST);
    for(int deck=0;deck<4;deck++)ram_refresh(deck);
    /* CLASSIC's wave surface: the decks playing their whole track from RAM since the music USB went out. */
    if(xz_native_ram_notice_set){
        unsigned ram_only=0;
        for(int deck=0;deck<4;deck++)if(model.deck[deck].ram.state==XZ_UI_RAM_DETACHED)ram_only|=1u<<deck;
        xz_native_ram_notice_set(ram_only);
    }
    for (int deck = 0; deck < 2; deck++) {
        if(model.deck[deck].pc_stems.source!=XZ_UI_SOURCE_LOCAL){key_valid[deck]=0;continue;}
        model.deck[deck].ready = XZ_UI_GATE | XZ_UI_SMART | XZ_UI_THEME;
        model.deck[deck].usb_reason = XZ_UI_USB_WHY_ENGINE;
        key_valid[deck]=xz_key_native_snapshot((unsigned)deck,&key_snapshot[deck]);
        if(key_sync[deck].valid&&key_valid[deck]&&key_snapshot[deck].epoch!=key_sync[deck].epoch){
            /* The new epoch already resets audio cents; never reset a new track's request. */
            if(key_sync_end(deck,key_player(deck),0)&&key_valid[deck])
                key_valid[deck]=xz_key_native_snapshot((unsigned)deck,&key_snapshot[deck]);
        }
        int cents=0,applied_cents=0;
        if (key_available&&key_valid[deck]) {
            model.deck[deck].ready |= XZ_UI_KEY;
            xz_key_get_desired_cents(deck,key_snapshot[deck].epoch,&cents);
            model.deck[deck].key_semitones=(int)round(cents*.01);
            /* Displays and colours follow what the audio actually plays, not a pending request. */
            xz_key_get_applied_cents(deck,key_snapshot[deck].epoch,&applied_cents);
        }
        struct xz_key_snapshot shown=key_snapshot[deck];
        if(model.key_format==1)shown.format=XZ_KEY_CLASSIC;else if(model.key_format==2)shown.format=XZ_KEY_CAMELOT;
        xz_key_readout(key_valid[deck]?&shown:NULL,applied_cents,model.deck[deck].key_readout,sizeof(model.deck[deck].key_readout));
        /* Effective key: original + applied shift + tempo transpose unless Master Tempo holds it. */
        struct xz_playing_key playing={0};int valid=key_valid[deck]&&xz_key_playing(&key_snapshot[deck],applied_cents,&playing);
        if(key_sync[deck].valid&&valid&&key_snapshot[deck].epoch==key_sync[deck].epoch&&cents!=key_sync[deck].cents)
            key_sync_end(deck,key_player(deck),0);
        if(key_sync[deck].valid)__atomic_fetch_or(&key_sync_led_mask,1u<<deck,__ATOMIC_RELEASE);
        else __atomic_fetch_and(&key_sync_led_mask,~(1u<<deck),__ATOMIC_RELEASE);
        model.deck[deck].key_synced=key_sync[deck].valid;
        char line[32]="";
        if(valid&&(playing.shift_cents||key_sync[deck].valid)){
            /* Follows the stock key text in its own format: "> 9A -22c" when the key moved, "+16c" when only tuned. */
            char now[8];xz_music_key_format(playing.key,key_snapshot[deck].format,now);int n=0;
            if(playing.key.root!=key_snapshot[deck].original.root)n=snprintf(line,sizeof(line),"> %s",now);
            if(playing.cents&&n>=0&&(size_t)n<sizeof(line))n+=snprintf(line+n,sizeof(line)-(size_t)n,"%s%+dc",n?" ":"",playing.cents);
            if(key_sync[deck].valid&&n>=0&&(size_t)n<sizeof(line))snprintf(line+n,sizeof(line)-(size_t)n,"%sKEY SYNC",n?"  ":"");
        }
        xz_key_line_publish(deck,line);
        {
            /* Deck strip: original key, what the deck plays now, and KEY SYNC or its error. */
            struct xz_key_strip_deck *s=&key_strip[deck];memset(s,0,sizeof(*s));
            s->valid=valid;s->synced=key_sync[deck].valid;
            if(valid){
                xz_music_key_format(key_snapshot[deck].original,shown.format,s->original);
                xz_music_key_format(playing.key,shown.format,s->effective);
                s->cents=playing.cents;
                s->moved=playing.key.root!=key_snapshot[deck].original.root||playing.key.minor!=key_snapshot[deck].original.minor;
            }
            if(strip_error_until[deck]&&(int32_t)(xz_game_milliseconds()-strip_error_until[deck])<0)
                memcpy(s->error,strip_error[deck],sizeof(s->error));
            else strip_error_until[deck]=0;
        }
        xz_mods_key_proof_v1.deck[deck]=(struct xz_key_ui_deck_proof){valid,key_snapshot[deck].epoch,
            valid?(uint32_t)key_snapshot[deck].original.root:UINT32_MAX,valid?(uint32_t)key_snapshot[deck].original.minor:0,
            valid?(uint32_t)playing.key.root:UINT32_MAX,valid?(uint32_t)key_snapshot[deck].master:0,
            valid?(uint32_t)key_snapshot[deck].master_tempo:0,valid?playing.cents:0,cents};
        model.deck[deck].key_color=key_valid[deck]&&key_snapshot[deck].master?0x52d794:0xa8b2bf;
        if (audio_available) {
            model.deck[deck].ready |= XZ_UI_STEM;
            if (xz_audio_get_status(deck, &audio_status[deck]) == 0) {
                struct xz_audio_status *s = &audio_status[deck];
                model.deck[deck].usb_reason = usb_stem_reason(s);
                stem_generation_sync_locked(deck,s);
                model.deck[deck].stem_format=s->format_name[0]?s->format_name:NULL;
                model.deck[deck].stem_error=s->error[0]?s->error:NULL;
                if(s->state!=XZ_AUDIO_EXPERIMENTAL_READY&&s->state!=XZ_AUDIO_PREPARED_ALIGNING&&s->state!=XZ_AUDIO_PREPARED_BUFFERING)
                    model.deck[deck].ready&=~XZ_UI_STEM;
                xz_prepared_proof_v1.deck[deck] = (struct prepared_proof_deck){s->state,s->reader_rate,s->reader_frames,s->mixed_blocks,s->skipped_blocks,s->alignment_frames,s->alignment_correlation};
                if (s->prepared && s->state == XZ_AUDIO_EXPERIMENTAL_READY && reported_generation[deck] != s->generation) {
                    char line[160];snprintf(line,sizeof(line),"PREPARED_STEMS_READY: deck=%d rate=%u frames=%u lag=%d correlation=%.6f",deck+1,s->reader_rate,s->reader_frames,s->alignment_frames,s->alignment_correlation);
                    xz_log(line);reported_generation[deck] = s->generation;
                }
                /* UI refresh thread only. Immediate run/state changes, <=4 Hz duration updates. */
                {
                    static uint32_t generation[2],runs[2],last[2],mode[2],logged_at[2];
                    uint32_t now=xz_game_milliseconds();
                    if(generation[deck]!=s->generation){generation[deck]=s->generation;runs[deck]=last[deck]=mode[deck]=0;}
                    if(s->starve_runs&&(runs[deck]!=s->starve_runs||mode[deck]!=s->starvation||
                        (last[deck]!=s->starve_last_frames&&(uint32_t)(now-logged_at[deck])>=250))){
                        char line[192];snprintf(line,sizeof(line),"STEM_STARVE: deck=%d runs=%u last_ms=%u max_ms=%u last_frames=%u max_frames=%u fallback=%s",
                            deck+1,s->starve_runs,s->starve_last_ms,s->starve_max_ms,s->starve_last_frames,s->starve_max_frames,
                            s->starve_fallback==XZ_OC_STARVE_UNITY?"unity":"held");
                        xz_log(line);runs[deck]=s->starve_runs;last[deck]=s->starve_last_frames;mode[deck]=s->starvation;logged_at[deck]=now;
                    }
                }
                const char *path = audio_status[deck].path;
                const char *name = strrchr(path, '/');
                model.deck[deck].track = path[0] ? (name ? name + 1 : path) : "NO TRACK CAPTURED";
                model.deck[deck].status = xz_audio_state_name(audio_status[deck].state);
                model.deck[deck].stem_loading = s->state==XZ_AUDIO_PREPARED_ALIGNING || s->state==XZ_AUDIO_PREPARED_BUFFERING;
                model.deck[deck].wave_peaks = NULL; model.deck[deck].wave_count = 0;
                if (xz_audio_waveform(deck, 2, overview[deck], sizeof(overview[deck]), NULL)) {
                    for (unsigned i=0;i<sizeof(overview[deck]);i++) overview[deck][i]=(unsigned char)(overview[deck][i]*255/31);
                    model.deck[deck].wave_peaks=overview[deck];model.deck[deck].wave_count=sizeof(overview[deck]);
                }
            }
        }
    }
    struct xz_master_observation authority=key_master();
    struct xz_deck_key views[4];memset(views,0,sizeof views);
    for(int d=0;d<4;d++)(void)deck_key_read(d,&views[d],NULL);
    int have_master=authority.deck>=0&&authority.deck<4&&views[authority.deck].playing_valid;
    pthread_mutex_lock(&key_projection_mutex);
    key_projection.master=have_master?views[authority.deck]:(struct xz_deck_key){0};key_projection.deck=authority.deck;
    key_projection.format=model.key_format==2?XZ_KEY_CAMELOT:model.key_format==1?XZ_KEY_CLASSIC:
        key_valid[0]?key_snapshot[0].format:key_valid[1]?key_snapshot[1].format:XZ_KEY_CLASSIC;key_projection.feedback=model.key_feedback;
    key_projection.traffic=model.browse_traffic;key_projection.at=xz_game_milliseconds();
    pthread_mutex_unlock(&key_projection_mutex);
    for(int d=0;d<4;d++){
        struct xz_ui_deck *u=&model.deck[d];const struct xz_deck_key *k=&views[d];
        if(d>=2){
            u->ready&=~(XZ_UI_KEY|XZ_UI_KEYSYNC);u->key_synced=k->engaged;u->key_semitones=(int)round(k->desired_cents*.01);
            if(k->engine_ready&&k->metadata_valid&&k->audio_state!=XZ_KEY_AUDIO_EMERGENCY)u->ready|=XZ_UI_KEY;
            char original[8],playing[8];int format=key_projection.format;
            xz_music_key_format(k->original,format,original);xz_music_key_format(k->playing.key,format,playing);
            if(!k->metadata_valid)snprintf(u->key_readout,sizeof u->key_readout,"KEY --");
            else if(k->playing_valid&&k->request_revision!=k->applied_revision)
                snprintf(u->key_readout,sizeof u->key_readout,"%s > %s %+dc / REQ %+dc",original,playing,k->playing.cents,k->desired_cents);
            else if(k->playing_valid)snprintf(u->key_readout,sizeof u->key_readout,"%s > %s %+dc",original,playing,k->playing.cents);
            else snprintf(u->key_readout,sizeof u->key_readout,"TRACK %s / REQUEST %+dc",original,k->desired_cents);
        }
        u->ready&=~XZ_UI_KEYSYNC;u->key_master=have_master&&authority.deck==d;
        if(k->engine_ready&&(k->engaged||(have_master&&authority.deck!=d&&k->metadata_valid))&&
            (d>=2||native_master_tempo_on))u->ready|=XZ_UI_KEYSYNC;
        u->key_color=0xa8b2bf;
        if(have_master&&d!=authority.deck&&k->playing_valid){
            enum xz_key_match match=xz_key_match(k->playing,views[authority.deck].playing);
            u->key_color=match==XZ_KEY_COMPATIBLE?0x52d794:match==XZ_KEY_DETUNED?0xffbd59:0xa8b2bf;
        }
    }
    for(int d=0;d<2;d++){
        key_strip[d].sync_master=have_master&&authority.deck==d;
        key_strip[d].match_color=model.browse_traffic?model.deck[d].key_color:0;
    }
    xz_key_browser_master(have_master?&views[authority.deck].playing:NULL,model.key_format==2?XZ_KEY_CAMELOT:XZ_KEY_CLASSIC);
    xz_key_traffic_master(have_master?&views[authority.deck].playing:NULL);
    xz_key_browser_configure(model.browse_columns,model.browse_key_prefix,model.browse_traffic,model.key_format);
    int focus=xz_native_focus_deck();
    if(focus!=native_focus){native_focus=focus;followed_generation=0;}
    int pc_contact=inline_contact&&model.deck[inline_contact_deck].pc_stems.source==XZ_UI_SOURCE_PC;
    if(!pc_contact&&focus>=0&&focus<2&&model.deck[focus].pc_stems.source==XZ_UI_SOURCE_LOCAL&&audio_status[focus].prepared&&
       audio_status[focus].generation!=followed_generation){
        followed_generation=audio_status[focus].generation;
        if(inline_ui.deck!=focus){
            struct xz_ui_action cancelled[XZ_UI_ACTIONS];xz_ui_cancel(&inline_ui,cancelled);
            inline_cancelled=inline_contact;inline_ui.deck=focus;
            if(!touch.visible)ui.deck=focus;
        }
    }
    __atomic_add_fetch(&xz_prepared_proof_v1.sequence,1,__ATOMIC_SEQ_CST);
    __atomic_add_fetch(&xz_mods_key_proof_v1.sequence,1,__ATOMIC_SEQ_CST);
}
static void ram_debug_refresh(void){
    if(!model.ram_debug)return;
    uint32_t now=xz_game_milliseconds();
    if(xz_mods_ram_debug_v1.samples&&(uint32_t)(now-ram_debug_sample_ms)<1000)return;
    struct sysinfo info;model.ram_debug_valid=sysinfo(&info)==0;ram_debug_sample_ms=now;
    if(model.ram_debug_valid)model.ram_free_mib=(uint32_t)(((uint64_t)info.freeram*info.mem_unit)>>20);
    __atomic_store_n(&xz_mods_ram_debug_v1.free_mib,model.ram_free_mib,__ATOMIC_RELAXED);
    __atomic_store_n(&xz_mods_ram_debug_v1.valid,model.ram_debug_valid,__ATOMIC_RELAXED);
    __atomic_add_fetch(&xz_mods_ram_debug_v1.samples,1,__ATOMIC_RELAXED);
}
static void refresh(void){network_refresh();sdk_refresh();pc_refresh_locked();refresh_local();ram_debug_refresh();if(model.metrics_enabled&&xz_metrics_snapshot)xz_metrics_snapshot(&model.metrics);}

static int inline_enabled(void) { return __atomic_load_n(&started,__ATOMIC_ACQUIRE)?__atomic_load_n(&inline_requested,__ATOMIC_ACQUIRE):XZ_STEM_LAYOUT_HIDDEN; }
static int render_inline(void *context,uint16_t *pixels,size_t count,size_t stride,int width,int height) {
    (void)context;
    static _Thread_local uint16_t cached[536*XZ_WAVE_INLINE_HEIGHT];
    static _Thread_local int cached_valid,cached_height,cached_layout;
    if (!pixels || width!=536 || (height!=XZ_WAVE_INLINE_HEIGHT&&height!=XZ_WAVE_HALF_CONTROL_HEIGHT*2) || stride<536 || stride>count/(size_t)height) return 0;
    if (pthread_mutex_trylock(&ui_mutex) != 0) {
        if (!cached_valid || cached_layout!=inline_enabled() || cached_height!=height) return 0;
        for (int y=0;y<height;y++) memcpy(pixels+y*stride,cached+y*536,536*sizeof(*pixels));
        return 1;
    }
    sdk_refresh();pc_refresh_locked();
    if(model.deck[0].pc_stems.source!=XZ_UI_SOURCE_PC||model.deck[1].pc_stems.source!=XZ_UI_SOURCE_PC)refresh_local();
    struct xz_wave_layout geometry;
    if (!xz_wave_layout_get(model.stems_overlay,&geometry)||height!=(int)geometry.control_height*2||touch.visible || (!requested_stems && !pc_present) || !model.stems_overlay) { pthread_mutex_unlock(&ui_mutex); return 0; }
    int result=xz_ui_inline_render(&inline_ui,&model,pixels,count,stride,width,height);
    struct xz_ui_widget widgets[XZ_UI_WIDGETS];
    size_t widget_count=xz_ui_inline_layout(&inline_ui,&model,width,height,widgets);
    if(result)for(size_t column=0;column<widget_count;column++){
        struct xz_ui_widget widget=widgets[column];
        if(widget.kind!=XZ_UI_MUTE||widget.h<32||model.deck[widget.deck].stem_loading)continue;
        unsigned role=(unsigned)widget.index;
        int left=widget.x+4,right=widget.x+widget.w-4;
        unsigned char peaks[256];float progress=0;
        size_t n=(size_t)(right-left);if(n>sizeof(peaks))n=sizeof(peaks);
        if(!xz_audio_waveform(widget.deck,role,peaks,n,&progress))continue;
        uint32_t rgb=xz_ui_model_local_stem_color(&model,(int)role);
        uint16_t color=(uint16_t)(((rgb>>19)&31)<<11|((rgb>>10)&63)<<5|((rgb>>3)&31));
        float gain=model.deck[widget.deck].muted&(1u<<role)?0:model.deck[widget.deck].levels[role];
        int center=widget.y+26;
        for(size_t x=0;x<n;x++){
            int amplitude=(int)(peaks[x]*gain*3/31);if(amplitude>3)amplitude=3;
            /* Preserve the percentage fill under the miniature waveform. */
            for(int y=center-amplitude;y<=center+amplitude;y++)pixels[(size_t)y*stride+(size_t)left+x]=gain?color:0x3186;
        }
        int needle=left+(int)(progress*(float)(n-1));
        for(int y=center-4;y<=center+4;y++)pixels[(size_t)y*stride+(size_t)needle]=0xffff;
    }
    if (result) {
        for (int y=0;y<height;y++) memcpy(cached+y*536,pixels+y*stride,536*sizeof(*pixels));
        cached_valid=1;cached_height=height;cached_layout=model.stems_overlay;
    } else cached_valid=0;
    pthread_mutex_unlock(&ui_mutex);return result;
}

static int stem_controls_active(int deck) {
    return deck>=0 && deck<2 && audio_available && (model.enabled & XZ_UI_STEM);
}

int xz_ui_runtime_pad(const struct xz_cue_event *event,unsigned *trace_flags) {
    *trace_flags = 0;
    if (!__atomic_load_n(&started, __ATOMIC_ACQUIRE)) return 0;
    pthread_mutex_lock(&ui_mutex);
    pc_refresh_locked();
    if(event->deck>=0&&event->deck<2&&model.deck[event->deck].pc_stems.source!=XZ_UI_SOURCE_LOCAL){
        pthread_mutex_unlock(&ui_mutex);return 0;
    }
    if(audio_available&&event->deck>=0&&event->deck<2){
        struct xz_audio_status status;
        if(!xz_audio_get_status(event->deck,&status))stem_generation_sync_locked(event->deck,&status);
    }
    if (event->deck >= 0 && event->deck < 2) deck_page[event->deck] = event->pad_page;
    if (event->sync && event->deck >= 0 && event->deck < 2) {
        unsigned bit = 1u << event->deck;
        int owned = !!(sync_owned & bit);
        if (event->operation == 2 || event->operation == 3) {
            sync_down &= ~bit; sync_owned &= ~bit;
        } else if (event->operation == 0 && !(sync_down & bit)) {
            sync_down |= bit;
            if (event->shift) {
                /* Shift+Sync always belongs to KEY SYNC, with its repeats and release, so the
                   stock release toggle never fires. Plain Sync stays with the firmware. */
                sync_owned |= bit; owned = 1;
                /* The gesture enables key sync directly. An old saved checkbox
                   never gates a working track/master pair behind a setup page. */
                model.shift_keysync=1;
                int done = sync_key(event->deck);
                /* A failure is shown inline on the deck strip. Only a missing key engine,
                   which no gesture can fix, opens its setup page. */
                if (!done && !key_available) xz_native_skin_request_repaint();
            }
        }
        pthread_mutex_unlock(&ui_mutex); return owned;
    }
    *trace_flags = 1u | (touch.visible ? 2u : 0) | (ui.page == XZ_UI_STEMS ? 4u : 0) |
        (ui.deck == event->deck ? 8u : 0) | (model.enabled & XZ_UI_STEM ? 16u : 0) |
        (audio_available ? 32u : 0) | (event->hotcue_mode ? 64u : 0);
    int toggle,beats=0,jump_failed=0;
    int shifted_mode=event->shift&&event->mode_button>=0&&event->mode_button<3;
    int active=stem_controls_active(event->deck)||(audio_available&&shifted_mode);
    int jump_mode=event->mode_button==3;
    if(jump_mode||((model.jump.enabled||(event->deck>=0&&event->deck<2&&jump_state.page[event->deck]))&&event->pad_page==3&&event->pad>=0))active=0;
    /* Both handlers see releases, including after settings change while held. */
    int consumed=xz_stem_control_event(&pads,event,active,model.stem_page,model.shift_pages,&toggle);
    int stem_mode_press=consumed&&event->mode_button>=0&&event->operation==0;
    int jump_consumed=stem_mode_press?0:xz_jump_event(&jump_state,&model.jump,event,&beats);
    if(jump_consumed){
        *trace_flags|=16384u;
        if(beats&&!xz_runtime_beat_jump(event->deck,beats)){
            jump_failed=1;
            xz_ui_notice_set(&async_notice,"JUMP UNAVAILABLE: TRACK EDGE, BUFFER OR DECK STATE",xz_game_milliseconds());
            snprintf(ui.notice,sizeof ui.notice,"%s",async_notice.text);
        }
    }
    if (consumed) *trace_flags |= 128u;
    if (toggle >= 0) *trace_flags |= 256u;
    if (toggle >= 0) {
        int deck = event->deck;
        /* An explicit unused-combo action activates the same stem state. An
           automatic-on-load preference must not silently defeat this gesture. */
        if(!(model.enabled&XZ_UI_STEM)){
            requested_stems=1;model.enabled|=XZ_UI_STEM;xz_audio_set_enabled(1);
            levels(1-deck);settings_queue();
        }
        model.deck[deck].muted = pads.muted[deck];
        if (toggle == XZ_STEM_PAD_BYPASS) model.deck[deck].bypass = !model.deck[deck].bypass;
        levels(deck);
    }
    if (event->deck >= 0 && event->deck < 2) {
        if (model.deck[event->deck].bypass) *trace_flags |= 512u;
        *trace_flags |= (model.deck[event->deck].muted & 7u) << 10;
        if(model.deck[event->deck].muted&8u)*trace_flags|=32768u;
        if (audio_status[event->deck].state == XZ_AUDIO_EXPERIMENTAL_READY) *trace_flags |= 8192u;
    }
    pthread_mutex_unlock(&ui_mutex);
    if(jump_failed){
        xz_native_skin_request_repaint();
        if(xz_shell_runtime_notice)xz_shell_runtime_notice("JUMP UNAVAILABLE: TRACK EDGE, BUFFER OR DECK STATE");
    }
    return consumed||jump_consumed;
}

int xz_ui_runtime_pad_color(int deck,int pad,unsigned *rgb,int *lit) {
    if (!__atomic_load_n(&started,__ATOMIC_ACQUIRE) || deck < 0 || deck > 1 || pad < 0 || pad > 7) return 0;
    if (pthread_mutex_trylock(&ui_mutex)) return 0;
    if(xz_rhythm_lab_pad(deck,pad,rgb,lit)) {pthread_mutex_unlock(&ui_mutex);return 1;}
    if(xz_wave_rider_pad(deck,pad,rgb,lit)) {
        pthread_mutex_unlock(&ui_mutex);return 1;
    }
    if(arcade_present) {
        *rgb=pad==3?0xd992f2:pad==7?0xffb95c:deck?0xffb95c:0x54ebed;
        unsigned cost=pad==1?3:pad==2?4:pad==3?6:0;
        *lit=!cost||arcade.player[deck].energy>=cost;
        double beat_phase=arcade.clock-floor(arcade.clock);
        if(pad==0)*lit=beat_phase<.24||arcade.note_flash>arcade.clock;
        if(pad==3&&arcade.drop_pending)*lit=beat_phase<.5;
        if(pad==3&&arcade.drop_visual_until>arcade.beat)*lit=1;
        if(pad==7){*rgb=arcade.phase_aligned?0x54ebed:0xffb95c;*lit=beat_phase<.24;}
        pthread_mutex_unlock(&ui_mutex);return 1;
    }
    struct xz_pc_stem_source pad_source;
    if(!pc_source((unsigned)deck,&pad_source)||pad_source.pc){pthread_mutex_unlock(&ui_mutex);return 0;}
    if(jump_state.page[deck]==1&&deck_page[deck]==3){
        *rgb=xz_jump_page2_color(pad);*lit=1;
        pthread_mutex_unlock(&ui_mutex);return XZ_LED_HUE;
    }
    int slot=pad-(model.stem_bank==1?4:0);
    int active = !((model.jump.enabled||jump_state.page[deck])&&deck_page[deck]==3) && slot>=0&&slot<4&&stem_controls_active(deck) && model.pad_feedback && deck_page[deck] == model.stem_page;
    if (active) {
        int stem=xz_stem_pad_role(model.stem_order,model.deck[deck].stem_count,slot);
        xz_stem_pad_led(stem,model.stem_colors,model.deck[deck].muted,model.deck[deck].levels,model.deck[deck].bypass,rgb,lit);
    }
    pthread_mutex_unlock(&ui_mutex); return active;
}
void xz_ui_runtime_pad_native_page(int deck,int page) {
    if (deck < 0 || deck > 1) return;
    pthread_mutex_lock(&ui_mutex); deck_page[deck] = page; pthread_mutex_unlock(&ui_mutex);
}

static int shortcut_touch(void *self, const struct xz_touch_status *status, const void *mode, int previous_down);
/* Browse header cells. The header is touchable only while the last frame drew it,
   so a tap there on any other screen still reaches the firmware. */
__attribute__((visibility("default"),used)) struct {
    uint32_t version,releases;
    int32_t down_x,down_y,up_x,up_y;
} xz_mods_browse_tap_proof_v1={1,0,0,0,0,0};
static int head_shown, head_contact = -1, head_pressed = -1, head_cancelled;
static uint32_t head_notice_until;
static int browse_head_touch(const struct xz_touch_status *status, int previous_down) {
    if (head_contact < 0) {
        if(status->y<XZ_TOP_BAR_TOUCH_H&&((status->x>=XZ_METRICS_X&&status->x<XZ_METRICS_X+XZ_METRICS_W)||
           (model.metrics_enabled&&status->x>=XZ_METRICS_READOUT_X&&status->x<XZ_METRICS_X)))return 0;
        if (previous_down || !status->down || touch.visible || touch.capture || !head_shown || !xz_native_browse_open_sort) return 0;
        head_contact = xz_browse_head_hit((int)status->x, (int)status->y);
        if (head_contact < 0) return 0;
        head_cancelled = 0;
        __atomic_add_fetch(&xz_mods_key_strip_proof_v1.head_taps, 1, __ATOMIC_RELAXED);
    }
    if (status->down) {
        xz_mods_browse_tap_proof_v1.down_x=(int32_t)status->x;
        xz_mods_browse_tap_proof_v1.down_y=(int32_t)status->y;
        if (xz_browse_head_hit((int)status->x,(int)status->y)!=head_contact) head_cancelled=1;
        head_pressed=head_cancelled?-1:head_contact;
    }
    if (!status->down) {
        xz_mods_browse_tap_proof_v1.up_x=(int32_t)status->x;
        xz_mods_browse_tap_proof_v1.up_y=(int32_t)status->y;
        __atomic_add_fetch(&xz_mods_browse_tap_proof_v1.releases,1,__ATOMIC_RELAXED);
        /* Native releases may clear the coordinates. The captured down contact owns
           the click; a down move off the original cell cancels it permanently. */
        if ((status->x||status->y)&&xz_browse_head_hit((int)status->x,(int)status->y)!=head_contact) head_cancelled=1;
        if (!head_cancelled) {
            if (xz_native_browse_open_sort() > 0) __atomic_add_fetch(&xz_mods_key_strip_proof_v1.head_opened, 1, __ATOMIC_RELAXED);
            else {
                __atomic_add_fetch(&xz_mods_key_strip_proof_v1.head_failed, 1, __ATOMIC_RELAXED);
                head_notice_until = xz_game_milliseconds() + 2500u; if (!head_notice_until) head_notice_until = 1;
            }
        }
        head_contact = head_pressed = -1;head_cancelled=0;
    }
    return 1;
}
static int classic_native_two(void){return model.ui_interface==XZ_UI_INTERFACE_CLASSIC&&xz_native_four_order()<0;}
/* CLASSIC 2 DECKS: the sides whose half shows DECK 3 or 4 now (bit 0 left, bit 1 right), as the wave last painted
   them; 0 in four-deck views, MODERN or without the wave transaction. The column, panels, key slots and touch
   all follow this one value, so a half switches as a whole. */
static unsigned classic_pair(void){
    /* The wave publishes a pair only for 2 DECKS frames; the view test keeps a stale one out of a four-deck view. */
    return model.ui_interface==XZ_UI_INTERFACE_CLASSIC&&xz_native_two_pair&&xz_deck_layout_get()==XZ_LAYOUT_TWO?xz_native_two_pair()&3u:0u;
}
/* The software decks the CLASSIC key slots address: every side's in a four-deck view; in 2 DECKS only a side whose
   half shows its software deck, so the slot over a stock half stays that native deck's. */
static const struct xz_extra_decks *classic_key_extra(struct xz_extra_decks *extra,int have){
    if(!have)return NULL;
    if(!classic_native_two())return extra;
    unsigned pair=classic_pair();if(!pair)return NULL;
    extra->left_on_3=extra->left_on_3&&(pair&1u);extra->right_on_4=extra->right_on_4&&(pair&2u);
    return extra;
}
/* The card the wave transaction last painted, hit in its surface (screen 131,18). word: what was painted. */
static int dv_card_hit(int x,int y,uint32_t *word_out){
    uint32_t word=xz_native_four_card?xz_native_four_card():0;
    *word_out=word;
    struct xz_four_layout g;
    if(!(word&XZ_NATIVE_CARD_SHOWN)||!xz_four_layout_get(XZ_NATIVE_CARD_ORDER(word),&g))return XZ_ROUTE_CARD_HIT_NONE;
    struct xz_route_card_input in={0};in.state=XZ_NATIVE_CARD_STATE(word);in.focus=-1;
    if(word&XZ_NATIVE_CARD_SUCCESS){in.state=XZ_EXTRA_ROUTE_READY;in.ready_at_ms=in.now_ms=1;}
    return xz_route_card_hit(&xz_route_card_classic,(int)g.lane_y[2],(int)g.lane_y[3],&in,x-131,y-18);
}
/* The painted card still shows this view and this route state; otherwise the next frame repaints it first. */
static int dv_card_current(uint32_t word,int view){
    if(view==XZ_LAYOUT_TWO||XZ_NATIVE_CARD_ORDER(word)!=(view==XZ_LAYOUT_MIXER?XZ_FOUR_MIXER:XZ_FOUR_STACKED))return 0;
    if(word&XZ_NATIVE_CARD_SUCCESS)return 1;
    int state=xz_extra_decks_route_status?(int)xz_extra_decks_route_status().state:XZ_EXTRA_ROUTE_NO_ENGINE;
    if(state==XZ_EXTRA_ROUTE_READY)state=XZ_EXTRA_ROUTE_STARTING;
    return state==XZ_NATIVE_CARD_STATE(word);
}
static int dv_eligible(void){
    return model.ui_interface==XZ_UI_INTERFACE_CLASSIC&&!touch.visible&&!model.fb_takeover&&game_locked()==GAME_NONE&&xz_native_play_visible();
}
/* The deck whose stem row lies under (x,y) on the two-deck play screen, else -1. OUTER layouts put deck 1's
   row at the top of the waveform, under the DECKS button's touch band; the row keeps its touches there. */
static int stem_row_at_locked(int x,int y){
    if(touch.visible||(model.fb_takeover&&model.connection.connected)||!xz_native_inline_active()||
       xz_native_four_order()>=0||x<131||x>=667)return -1;
    struct xz_wave_layout g;
    int deck=xz_wave_layout_get(model.stems_overlay,&g)?xz_wave_layout_control_at(&g,y-18):-1;
    /* A half showing DECK 3 or 4 has no stem rows. */
    return deck>=0&&(classic_pair()&(1u<<deck))?-1:deck;
}
/* DECK VIEW touch in CLASSIC, before every other play-screen owner: the open picker takes any touch (a tile
   applies, outside closes), the button opens it, the card's RESCAN / TRY AGAIN wakes the connector and its
   TWO DECKS applies 2 DECKS. The contact stays owned until release. */
static int deck_view_touch_locked(const struct xz_touch_status *status,int fresh){
    if(dv.contact){if(!status->down)dv.contact=0;return 1;}
    if(!fresh||!status->down||!dv_eligible()||play_key.open||mixer_contact.active||four_contact||inline_contact||
       inline_wave_contact||touch.capture)return 0;
    uint32_t now=xz_game_milliseconds(),word=0;int x=(int)status->x,y=(int)status->y;
    int open=dv.painted&&dv.painted_open,button=dv.painted&&xz_deck_view_button_hit(&xz_deck_view_classic,x,y)&&
        (y<XZ_TOP_BAR_H||stem_row_at_locked(x,y)<0);
    int card=open||button?XZ_ROUTE_CARD_HIT_NONE:dv_card_hit(x,y,&word);
    if(!open&&!button&&card==XZ_ROUTE_CARD_HIT_NONE){
        /* Any other touch on the play screen wakes the card's motion and gives the encoder back. */
        if(dv.focus!=DV_FOCUS_NONE){dv.focus=DV_FOCUS_NONE;xz_native_skin_request_repaint();}
        dv_input(now);return 0;
    }
    dv.contact=1;
    int view=xz_deck_view_valid(xz_deck_layout_get());
    if((open||button)&&dv.painted_view!=view)return 1;
    if(card!=XZ_ROUTE_CARD_HIT_NONE&&!dv_card_current(word,view))return 1;
    dv.focus=DV_FOCUS_NONE;dv_input(now);
    if(open){
        int hit=xz_deck_view_picker_hit(&xz_deck_view_classic,x,y);
        if(hit>=0)deck_view_apply(hit);
        else if(hit==XZ_DECK_VIEW_HIT_OUTSIDE){dv.open=0;xz_native_skin_request_repaint();}
        else dv.open=1;
        return 1;
    }
    if(button){dv.open=1;dv.tile=view;xz_native_skin_request_repaint();return 1;}
    if(card==XZ_ROUTE_CARD_HIT_PRIMARY){if(xz_extra_decks_rescan)xz_extra_decks_rescan();xz_native_skin_request_repaint();}
    else if(card==XZ_ROUTE_CARD_HIT_TWO)deck_view_apply(XZ_LAYOUT_TWO);
    return 1;
}
/* The encoder in CLASSIC. While the picker is open it owns turn and push. While the card shows, a push
   focuses its first button (RESCAN or TRY AGAIN, else TWO DECKS); turn then moves across the card's buttons
   and the DECK VIEW button, push presses, BACK or six idle seconds give the encoder back to the player.
   Without the card the encoder stays the player's. Returns 1 when it took the event (every push phase). */
static int deck_view_encoder_locked(int delta,int push,int press){
    if(!dv_eligible()||play_key.open)return 0;
    uint32_t now=xz_game_milliseconds();
    if(dv_picker_live(now)||(dv.painted&&dv.painted_open)){
        if(delta){dv.tile=xz_deck_view_focus_step(dv.tile,delta);dv.open=1;dv_input(now);xz_native_skin_request_repaint();}
        else if(push&&press){dv_input(now);deck_view_apply(dv.tile);}
        return delta||push;
    }
    uint32_t word=xz_native_four_card?xz_native_four_card():0;
    int view=xz_deck_view_valid(xz_deck_layout_get());
    int card=(word&XZ_NATIVE_CARD_SHOWN)&&!(word&XZ_NATIVE_CARD_SUCCESS)&&dv_card_current(word,view);
    int list[3],n=0;
    if(card){
        struct xz_route_card_input in={0};in.state=XZ_NATIVE_CARD_STATE(word);in.focus=-1;int order[2];
        int k=xz_route_card_focus_order(&in,order);
        for(int i=0;i<k;i++)list[n++]=order[i]==XZ_ROUTE_CARD_HIT_PRIMARY?DV_FOCUS_PRIMARY:DV_FOCUS_TWO;
    }
    list[n++]=DV_FOCUS_BUTTON;
    if(!n){dv.focus=DV_FOCUS_NONE;return 0;}
    if(dv_focus_live(now)){
        int at=0;for(int i=0;i<n;i++)if(list[i]==dv.focus)at=i;
        if(delta){at+=delta<-3?-3:delta>3?3:delta;at=at<0?0:at>=n?n-1:at;dv.focus=list[at];dv_input(now);xz_native_skin_request_repaint();}
        else if(push&&press){
            int focus=list[at];dv.focus=DV_FOCUS_NONE;dv_input(now);
            if(focus==DV_FOCUS_BUTTON){dv.open=1;dv.tile=view;xz_native_skin_request_repaint();}
            else if(focus==DV_FOCUS_PRIMARY){if(xz_extra_decks_rescan)xz_extra_decks_rescan();xz_native_skin_request_repaint();}
            else deck_view_apply(XZ_LAYOUT_TWO);
        }
        return delta||push;
    }
    if(card&&push){if(press){dv.focus=list[0];dv_input(now);xz_native_skin_request_repaint();}return 1;}
    return 0;
}
static int play_key_extra_locked(struct xz_extra_decks *out){
    return xz_extra_decks_snapshot&&xz_extra_decks_snapshot(out);
}
static void play_key_view_locked(struct xz_play_key_view *out){
    xz_play_key_view(&play_key,&model,ui.notice,out);out->native_engine=!!key_available;
    out->native_sync=key_available&&native_master_tempo_on;
    if(play_key.deck>=2&&play_key.deck<4&&xz_extra_decks_snapshot){
        struct xz_extra_decks extra={0};
        if(xz_extra_decks_snapshot(&extra)&&extra.notice_seq!=key_notice_seen[play_key.deck-2]&&
           (uint32_t)(xz_game_milliseconds()-extra.notice_at_ms)<3500u)
            snprintf(out->notice,sizeof out->notice,"%s",extra.notice);
    }
    struct xz_deck_key k;
    if(deck_key_read(play_key.deck,&k,NULL))out->live_verified=k.audio_state==XZ_KEY_AUDIO_APPLIED&&
        k.playing_valid&&k.applied_revision==k.request_revision&&abs(k.desired_cents-k.applied_cents)<=1;

}
static int play_key_binding_valid(const struct xz_play_key *key){
    if(!key->open)return 1;
    if(key->source_frame&&(!xz_shell_runtime_key_frame||xz_shell_runtime_key_frame()!=key->source_frame))return 0;
    /* Projection 1 is a validated native-only frame; software players are optional. */
    if(key->source_frame==1u)return key->deck<2&&key->selected_deck==key->classic_side;
    if(key->classic_side>=0){
        struct xz_extra_decks extra;int have=play_key_extra_locked(&extra);
        if(!have&&(key->source_frame||xz_native_four_order()>=0))return 0;
        if(xz_classic_key_target(classic_key_extra(&extra,have),key->classic_side)!=key->selected_deck)return 0;
    }
    return 1;
}
static int play_key_source_valid_locked(void){
    return play_key_binding_valid(&play_key)&&(!play_key.open||
        (play_key.source_frame?shell_owns_locked():play_key.classic_side<0||model.ui_interface!=XZ_UI_INTERFACE_MODERN));
}
static int play_key_touch_locked(void *self,const struct xz_touch_status *status,int fresh){
    int previous_down=((const unsigned char *)self)[4]!=0,deck=-1,side=-1;
    uint32_t source_frame=0;
    struct xz_extra_decks extra;int have_extra=play_key_extra_locked(&extra);
    if(!play_key_source_valid_locked()){
        play_key.open=0;play_key.down=!!status->down;play_key_dirty=1;
        __atomic_store_n(&play_key_visible,0,__ATOMIC_RELEASE);xz_native_skin_request_repaint();return 1;
    }
    if(!play_key.open&&!play_key.down){
        if(touch.visible||model.fb_takeover||game_locked()!=GAME_NONE||!fresh||previous_down||
           inline_contact||inline_wave_contact||four_contact||mixer_contact.active||touch.capture)return 0;
        if(shell_owns_locked()){
            if(xz_shell_runtime_key_hit)deck=xz_shell_runtime_key_hit((int)status->x,(int)status->y,&source_frame);
            if(deck==-2){play_key.down=1;play_key_dirty=1;return 1;}
            if(deck>=0){
                side=deck&1;
                if(source_frame!=1u&&(!have_extra||xz_classic_key_target(&extra,side)!=xz_shell_key_frame_selected(source_frame,side))){
                    play_key.down=1;play_key_dirty=1;return 1;
                }
            }
        }
        else if(xz_native_play_visible()&&(xz_native_inline_active()||xz_native_four_order()>=0)){
            side=xz_classic_key_slot_at(&classic_keys,(int)status->x,(int)status->y);
            if(side>=0){
                int target=xz_classic_key_target(classic_key_extra(&extra,have_extra),side);
                if((!have_extra&&xz_native_four_order()>=0)||target!=classic_keys.deck[side]){
                    play_key.down=1;classic_keys.shown=0;xz_native_skin_request_repaint();return 1;
                }
                deck=target;
            }
        }
        if(deck<0)return 0;
        play_key_open_locked(deck);play_key.classic_side=side;play_key.source_frame=source_frame;
        play_key.selected_deck=source_frame?xz_shell_key_frame_selected(source_frame,side):deck;ui.notice[0]=0;
    }else if(!play_key.open){if(!status->down)play_key.down=0;return 1;}
    struct xz_ui_action action;
    if(xz_play_key_touch(&play_key,&model,(int)status->x,(int)status->y,!!status->down,&action))apply(NULL,&action,1);
    __atomic_store_n(&play_key_visible,play_key.open,__ATOMIC_RELEASE);
    play_key_dirty=1;xz_native_skin_request_repaint();return 1;
}
/* The MODS header strip over the wave surface's top rows (metrics readout, METRICS) keeps its own
   touches whichever lane lies underneath. Same tests as xz_native_touch_dispatch. */
static int four_header_strip(const struct xz_touch_status *s) {
    return s->y<XZ_TOP_BAR_TOUCH_H&&(xz_metrics_hit((int)s->x,(int)s->y)||
        (model.metrics_enabled&&s->x>=XZ_METRICS_READOUT_X&&s->x<XZ_METRICS_X));
}
/* Needle search from the panel's strip (ui_mutex held; the players only enqueue). The deck must be loaded,
   its length known and paused or physically held: stock NEEDLE LOCK still blocks released platter coast.
   One search per strip column; a refusal raises its notice once per contact. */
enum { SEEK_PANEL, SEEK_MIXER, SEEK_HALF };
static void four_seek(int deck, int x, int mixer) {
    struct xz_extra_detail_box b=xz_extra_detail_box(XZ_EXTRA_DETAIL_OVERVIEW);
    if(mixer==SEEK_MIXER){struct xz_classic_mixer_rect r=xz_classic_mixer_overview_rect(mixer_contact.layout,deck+2);b=(struct xz_extra_detail_box){r.x,r.y,r.w,r.h};}
    if(mixer==SEEK_HALF){struct xz_classic_mixer_rect r=xz_classic_half_overview_rect(deck);b=(struct xz_extra_detail_box){r.x,r.y,r.w,r.h};}
    int column=x<b.x?b.x:x>=b.x+b.w?b.x+b.w-1:x;
    if(column==four_seek_column)return;
    four_seek_column=column;
    int refusal=XZ_EXTRA_DETAIL_NOTICE_UNAVAILABLE;
    struct xz_extra_decks now;
    if(deck>=0&&deck<XZ_EXTRA_DECKS&&xz_extra_decks_snapshot&&xz_extra_deck_input&&xz_extra_decks_snapshot(&now)){
        refusal=xz_extra_detail_seek_refusal(&now.deck[deck]);
        if(refusal==XZ_EXTRA_DETAIL_NOTICE_NONE){
            struct xz_extra_deck_event event;memset(&event,0,sizeof(event));
            event.deck=deck;event.kind=XZ_EXTRA_IN_SEEK;event.pressed=1;
            event.seek_ms=mixer==SEEK_HALF?xz_classic_half_seek_ms(deck,column,now.deck[deck].total_ms):
                          mixer?xz_classic_mixer_seek_ms(mixer_contact.layout,deck+2,column,now.deck[deck].total_ms):
                               xz_extra_detail_seek_ms(column,now.deck[deck].total_ms);
            if(xz_extra_deck_input(&event))return;
            refusal=XZ_EXTRA_DETAIL_NOTICE_REFUSED;
        }
    }
    if(!four_seek_noticed){
        if(xz_native_four_detail_notice)xz_native_four_detail_notice(refusal);
        if(mixer){
            char line[64];const char *message=xz_extra_detail_notice_text?
                xz_extra_detail_notice_text(refusal,(unsigned)deck+3,line,sizeof line):"NEEDLE LOCK: PAUSE THE DECK TO SEARCH";
            xz_ui_notice_set(&async_notice,message,xz_game_milliseconds());
        }
        four_seek_noticed=1;
    }
}
/* The panel's DECK SELECT, after ui_mutex is released: the side switches only while it drives the other deck. */
static void four_side_select(int side, int want_extra) {
    if((!xz_runtime_deck_select_target||xz_runtime_deck_select_target(side,want_extra)!=1)&&xz_native_four_detail_notice)
        xz_native_four_detail_notice(XZ_EXTRA_DETAIL_NOTICE_SELECT_FAILED);
}
/* Bottom channels keep the logical deck of the initial contact, even across channel boundaries. */
static int mixer_touch(void *self,const struct xz_touch_status *status,const void *mode,int active,int previous_down){
    int x=(int)status->x,y=(int)status->y;
    if(!mixer_contact.active){
        if(!active||!status->down||previous_down||four_contact||inline_contact||inline_wave_contact||touch.capture)return 0;
        mixer_contact.layout=xz_deck_layout_get();
        if(!xz_classic_mixer_runtime_layout||xz_classic_mixer_runtime_layout()!=mixer_contact.layout)return 0;
        int deck=xz_classic_mixer_channel_at(mixer_contact.layout,x,y),column=-1;
        if(deck<0)deck=column=xz_classic_column_deck_at(mixer_contact.layout,x,y);
        if(deck<0)return 0;
        struct xz_classic_mixer_rect r=xz_classic_mixer_overview_rect(mixer_contact.layout,deck);
        mixer_contact.active=1;mixer_contact.deck=deck;mixer_contact.cancelled=0;mixer_contact.native_down=0;
        mixer_contact.kind=column>=0?3:x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h?1:deck>=2&&y<336?2:0;
        four_seek_column=-1;four_seek_noticed=0;
        if(deck<2)ui.deck=inline_ui.deck=deck;
        /* A tag for DECK 1 or 2 is the stock column's own tap at that deck's point (60,40) or (60,180). */
        if(mixer_contact.kind==3&&deck<2){
            struct xz_touch_status mapped=*status;mapped.x=60;mapped.y=deck?180:40;
            stock_touch(self,&mapped,mode);mixer_contact.native_down=1;mixer_contact.last_x=60;mixer_contact.last_y=deck?180:40;
        }
    }
    if(!active||mixer_contact.layout!=xz_deck_layout_get())mixer_contact.cancelled=1;
    int deck=mixer_contact.deck;
    if(mixer_contact.kind==2&&(status->down||status->x||status->y)&&
       (xz_classic_mixer_channel_at(mixer_contact.layout,x,y)!=deck||y>=336))mixer_contact.cancelled=1;
    if(mixer_contact.kind==3&&(status->down||status->x||status->y)&&xz_classic_column_deck_at(mixer_contact.layout,x,y)!=deck)
        mixer_contact.cancelled=1;
    if(mixer_contact.kind==1&&!mixer_contact.cancelled&&status->down){
        if(deck>=2)four_seek(deck-2,x,SEEK_MIXER);
        else{
            struct xz_classic_mixer_rect r=xz_classic_mixer_overview_rect(mixer_contact.layout,deck);
            if(x<r.x)x=r.x;else if(x>=r.x+r.w)x=r.x+r.w-1;
            if(y<r.y)y=r.y;else if(y>=r.y+r.h)y=r.y+r.h-1;
            int local_x,local_y,stock_x,stock_y;
            if(xz_classic_mixer_native_point(mixer_contact.layout,deck,x,y,&local_x,&local_y)&&xz_classic_mixer_runtime_native_point&&
               xz_classic_mixer_runtime_native_point(deck,local_x,local_y,&stock_x,&stock_y)){
                struct xz_touch_status mapped=*status;mapped.x=(uint32_t)stock_x;mapped.y=(uint32_t)stock_y;
                stock_touch(self,&mapped,mode);mixer_contact.native_down=1;
                mixer_contact.last_x=stock_x;mixer_contact.last_y=stock_y;
            }else{
                mixer_contact.cancelled=1;
                xz_ui_notice_set(&async_notice,"WAIT FOR THE NATIVE WAVEFORM TO REFRESH",xz_game_milliseconds());
            }
        }
    }
    if(mixer_contact.native_down&&(!status->down||mixer_contact.cancelled)){
        struct xz_touch_status up={0};up.x=(uint32_t)mixer_contact.last_x;up.y=(uint32_t)mixer_contact.last_y;
        stock_touch(self,&up,mode);mixer_contact.native_down=0;
    }
    if(!status->down){
        /* DECK 3 or 4 open their detail from the tag as from their channel title; not while NOT CONNECTED. */
        uint32_t card=xz_native_four_card?xz_native_four_card():0;
        if(!mixer_contact.cancelled&&mixer_contact.kind==3&&deck>=2&&!(card&XZ_NATIVE_CARD_SHOWN)&&xz_native_four_set_detail){
            int current=xz_native_four_detail?xz_native_four_detail():-1;
            xz_native_four_set_detail(current==deck-2?-1:deck-2);
        }
        if(!mixer_contact.cancelled&&mixer_contact.kind==2&&xz_native_four_set_detail){
            int current=xz_native_four_detail?xz_native_four_detail():-1;
            xz_native_four_set_detail(current==deck-2?-1:deck-2);
        }
        mixer_contact.active=0;
    }
    return 1;
}
/* CLASSIC 2 DECKS: a side whose half shows DECK 3 or 4 owns every contact that begins on that half: its wave half,
   its half of the side column and its bottom panel. Stock never sees such a contact as a DECK 1 or 2 touch. The
   panel's overview searches the software deck under the NEEDLE LOCK rule (a refusal says why); the key slot went
   to the key controls before this; the rest of the half is inert. The contact keeps its side and stays owned to
   its release, also when the half returns to stock under the finger (a switch, route loss). */
static struct { int active,side,seek; } pair_contact;
static int pair_half_at(const struct xz_touch_status *s){
    int x=(int)s->x,y=(int)s->y;
    if(x>=131&&x<131+XZ_WAVE_WIDTH&&y>=18&&y<18+XZ_WAVE_HEIGHT)
        return four_header_strip(s)?-1:y-18>=XZ_WAVE_SECOND_LANE_Y; /* the right half from the separator row */
    int side=xz_classic_column_half_at(x,y);
    return side>=0?side:xz_classic_half_panel_at(x,y);
}
static int pair_touch(const struct xz_touch_status *status,int previous_down){
    if(!pair_contact.active){
        if(!status->down||previous_down||touch.visible||(model.fb_takeover&&model.connection.connected)||
           game_locked()!=GAME_NONE||play_key.open||touch.capture||inline_contact||inline_wave_contact||four_contact||
           mixer_contact.active)return 0;
        unsigned pair=classic_pair();int side=pair?pair_half_at(status):-1;
        if(side<0||!(pair&(1u<<side))||!xz_native_play_visible())return 0;
        struct xz_classic_mixer_rect r=xz_classic_half_overview_rect(side);
        pair_contact.active=1;pair_contact.side=side;four_seek_column=-1;four_seek_noticed=0;
        pair_contact.seek=(int)status->x>=r.x&&(int)status->x<r.x+r.w&&(int)status->y>=r.y&&(int)status->y<r.y+r.h;
    }
    if(pair_contact.seek&&status->down&&(classic_pair()&(1u<<pair_contact.side)))four_seek(pair_contact.side,(int)status->x,SEEK_HALF);
    if(!status->down)pair_contact.active=0;
    return 1;
}
static int stock_dialog_contact;
static void touch_hook(void *self, const struct xz_touch_status *status, const void *mode) {
    if (!__atomic_load_n(&started, __ATOMIC_ACQUIRE)) { stock_touch(self, status, mode); return; }
    unsigned counter_mask=stock_counter_mask(NULL);int counter_touch=0;
    if(counter_mask){
        pthread_mutex_lock(&ui_mutex);
        counter_touch=model.ui_interface==XZ_UI_INTERFACE_CLASSIC&&status->down&&!play_key_raw_down&&!touch.capture&&!ui.down&&!inline_contact&&!inline_wave_contact&&!four_contact&&!pair_contact.active&&xz_stock_counter_hit(counter_mask,status->x,status->y);
        pthread_mutex_unlock(&ui_mutex);
    }
    if(stock_dialog_up()||stock_dialog_contact||counter_touch){
        pthread_mutex_lock(&ui_mutex);
        stock_dialog_contact=!!status->down;
        /* Revoke all mod contact owners before stock receives the unchanged contact. */
        touch.capture=touch.opening_contact=0;ui.down=0;play_key.down=0;play_key_raw_down=0;
        dv.contact=0;inline_contact=inline_wave_contact=four_contact=0;pair_contact.active=0;
        memset(&mixer_contact,0,sizeof mixer_contact);arcade_touch_owned=arcade_opening_touch=0;panel_contact=0;
        pthread_mutex_unlock(&ui_mutex);
        stock_touch(self,status,mode);return;
    }
    pthread_mutex_lock(&ui_mutex);
    int fresh=status->down&&!play_key_raw_down;play_key_raw_down=!!status->down;
    /* A touch that closes MODS in MODERN (CLOSE, UTILITY, a setup that ends on the play screen) keeps the rest
       of its contact: the shell under it never sees it as a tap on its own MODS button. */
    if(fresh)panel_contact=touch.visible&&model.ui_interface==XZ_UI_INTERFACE_MODERN;
    if(panel_contact&&!touch.visible){if(!status->down)panel_contact=0;pthread_mutex_unlock(&ui_mutex);return;}
    if(!status->down)panel_contact=0;
    if(mixer_contact.active&&(play_key.open||play_key.down||dv.contact||touch.visible||model.ui_interface==XZ_UI_INTERFACE_MODERN||
       (model.fb_takeover&&model.connection.connected)||game_locked()!=GAME_NONE||
       !xz_native_play_visible()||xz_native_four_order()<0||mixer_contact.layout!=xz_deck_layout_get())){
        mixer_touch(self,status,mode,0,0);pthread_mutex_unlock(&ui_mutex);return;
    }
    if(game_locked()!=GAME_NONE)game_nav.visible=0;
    if(xz_rhythm_lab_touch((int)status->x,(int)status->y,!!status->down,xz_game_milliseconds())) {
        touch.capture=0;touch.opening_contact=0;
        if(xz_rhythm_lab_leave_requested())rhythm_lab_to_dj();
        pthread_mutex_unlock(&ui_mutex);return;
    }
    if(xz_wave_rider_touch((int)status->x,(int)status->y,!!status->down,xz_game_milliseconds())) {
        if(!status->down){touch.capture=0;touch.opening_contact=0;}
        if(xz_wave_rider_leave_requested())wave_rider_to_dj();
        pthread_mutex_unlock(&ui_mutex);return;
    }
    if(arcade_present||arcade_touch_owned){
        int was_music=arcade.phase==XZ_ARCADE_MUSIC;
        if(arcade_present){
            arcade_touch_owned=!!status->down;
            if(arcade_opening_touch){if(!status->down){arcade_opening_touch=0;arcade.touch_down=0;touch.capture=0;touch.opening_contact=0;}}
            else xz_arcade_touch(&arcade,(int)status->x,(int)status->y,!!status->down,xz_game_milliseconds());
            if(arcade_leaving(was_music))arcade_to_dj();
        }else if(!status->down)arcade_touch_owned=0;
        pthread_mutex_unlock(&ui_mutex);return;
    }
    if(deck_view_touch_locked(status,fresh)){pthread_mutex_unlock(&ui_mutex);return;}
    if(play_key_touch_locked(self,status,fresh)){pthread_mutex_unlock(&ui_mutex);return;}
    if(shell_touch_locked(self,status,mode)){pthread_mutex_unlock(&ui_mutex);return;}
    sdk_refresh();pc_refresh_locked();
    if(touch.visible||model.deck[0].pc_stems.source!=XZ_UI_SOURCE_PC||model.deck[1].pc_stems.source!=XZ_UI_SOURCE_PC)refresh_local();
    touch.vj_btn_w=(model.fb_takeover||xz_native_play_visible())?XZ_TOP_VJ_W:0;
    touch.reach=xz_native_play_visible()&&!model.fb_takeover?XZ_TOP_BAR_DEEP_H:XZ_TOP_BAR_TOUCH_H;
    int previous_down=((const unsigned char *)self)[4]!=0;
    if(shortcut_touch(self,status,mode,previous_down)){pthread_mutex_unlock(&ui_mutex);return;}
    if(browse_head_touch(status,previous_down)){pthread_mutex_unlock(&ui_mutex);return;}
    if(pair_touch(status,previous_down)){pthread_mutex_unlock(&ui_mutex);return;}
    /* Stock DECK 1 / DECK 2 select ({0,18,115,140} and {0,161,115,140}); VJ.TOOLS ends above it. */
    if(!touch.visible&&!previous_down&&status->down&&status->x<115&&status->y>=XZ_TOP_BAR_H&&xz_native_four_order()<0){
        if(status->y>=18&&status->y<159)ui.deck=inline_ui.deck=0;
        else if(status->y>=161&&status->y<302)ui.deck=inline_ui.deck=1;
    }
    int stock_screen=!touch.visible&&!(model.fb_takeover&&model.connection.connected);
    int active=stock_screen&&xz_native_inline_active();
    int four=stock_screen?xz_native_four_order():-1;
    int mixer=stock_screen&&!play_key.open&&four>=0&&xz_classic_mixer_runtime_visible&&xz_classic_mixer_runtime_visible();
    if(mixer_touch(self,status,mode,mixer,previous_down)){
        inline_layout_publish();pthread_mutex_unlock(&ui_mutex);xz_native_skin_request_repaint();return;
    }
    /* The detail panel lives on the four-deck surface: MODS, a takeover or a revoked layout closes it. */
    int detail=xz_native_four_detail?xz_native_four_detail():-1,four_repaint=0,four_select=-1;
    if(four<0&&detail>=0){if(xz_native_four_set_detail)xz_native_four_set_detail(-1);detail=-1;four_repaint=1;}
    /* A revoked layout retires native-lane mapping; a contact the mod owns stays owned until release. */
    if(four<0&&four_contact&&four_contact_deck>=0&&four_contact_deck<2&&four_panel_hit<0)four_contact=0;
    /* A resized/modal/browse scene owns raw coordinates. Retire only the wave
     * mapping; a captured mod control consumes its cancelled up event. */
    if(!active){
        inline_wave_contact=0;
        if(inline_contact&&!inline_cancelled){
            struct xz_ui_action ignored[XZ_UI_ACTIONS];
            xz_ui_cancel(&inline_ui,ignored);inline_cancelled=1;
        }
    }
    struct xz_wave_layout geometry;
    if(!xz_wave_layout_get(model.stems_overlay,&geometry))xz_wave_layout_get(XZ_STEM_LAYOUT_HIDDEN,&geometry);
    int pressed_deck=stem_row_at_locked((int)status->x,(int)status->y);
    if(!inline_contact&&!previous_down&&status->down&&pressed_deck>=0){
        inline_contact=1;inline_contact_deck=pressed_deck;inline_cancelled=0;inline_gesture_layout=model.stems_overlay;
    }
    int owned,pc_redraw=inline_contact&&model.deck[inline_contact_deck].pc_stems.source==XZ_UI_SOURCE_PC;
    if(inline_contact){
        struct xz_ui_action actions[XZ_UI_ACTIONS];
        struct xz_wave_layout captured;
        int valid=xz_wave_layout_get(inline_gesture_layout,&captured)&&captured.control_height;
        if((!valid||model.stems_overlay!=inline_gesture_layout)&&!inline_cancelled){
            xz_ui_cancel(&inline_ui,actions);inline_cancelled=1;
        }
        if(valid&&!inline_cancelled){
            int row_y=(int)status->y-18-(int)captured.control_y[inline_contact_deck];
            if(row_y<0)row_y=0;else if(row_y>=(int)captured.control_height)row_y=(int)captured.control_height-1;
            size_t n=xz_ui_inline_touch(&inline_ui,&model,536,(int)captured.control_height*2,(int)status->x-131,row_y+inline_contact_deck*(int)captured.control_height,!!status->down,actions);
            if(n)apply(NULL,actions,n);
        }
        if(!status->down){inline_contact=0;inline_cancelled=0;}
        owned=1;
    }else{
        struct xz_four_layout four_geometry;
        int sx=(int)status->x-131,sy=(int)status->y-18;
        /* Stock never sees an owned contact, so its previous-down byte stays clear: four_contact,
           not previous_down, says whether this contact already began. */
        if(!previous_down&&status->down&&!four_contact){
            four_contact=four>=0&&sx>=0&&sx<XZ_WAVE_WIDTH&&sy>=0&&sy<XZ_WAVE_HEIGHT&&!four_header_strip(status)&&
                xz_four_layout_get(four,&four_geometry);
            four_contact_order=four;four_panel_hit=four_panel_deck=-1;four_tap=0;four_seek_column=-1;four_seek_noticed=0;
            if(four_contact){
                four_contact_deck=xz_four_layout_lane_at(&four_geometry,sy);
                if(detail>=0)four_panel_hit=xz_extra_detail_hit(sx,sy);
                if(four_panel_hit>=0)four_panel_deck=detail;
                four_tap=four_panel_hit<0&&(four_contact_deck==2||four_contact_deck==3);
            }
        }
        if(!previous_down&&status->down){
            inline_wave_deck=xz_wave_layout_lane_at(&geometry,(int)status->y-18);
            inline_gesture_layout=model.stems_overlay;
            inline_wave_contact=!four_contact&&active&&status->x>=131&&status->x<667&&inline_wave_deck>=0;
        }
        struct xz_touch_status mapped=*status;
        /* DECK 3/4 lanes, the gaps between lanes and the open panel are mod surface: never a native deck touch. */
        int four_owned=four_contact&&(four_panel_hit>=0||four_contact_deck<0||four_contact_deck>=2);
        /* Cleared release coordinates keep the last point. A supplied endpoint must still match the control. */
        if(four_contact&&four!=four_contact_order){
            four_tap=0;
            if(four_panel_hit>=0)four_panel_hit=XZ_EXTRA_DETAIL_PANEL;
        }
        if(four_contact&&(status->down||status->x||status->y)){
            if(four_panel_hit==XZ_EXTRA_DETAIL_OVERVIEW&&four_panel_deck==detail&&status->down){
                four_seek(four_panel_deck,sx,SEEK_PANEL);four_repaint=1;
            }
            else if(four_panel_hit>XZ_EXTRA_DETAIL_PANEL&&four_panel_hit!=XZ_EXTRA_DETAIL_OVERVIEW&&
                    xz_extra_detail_hit(sx,sy)!=four_panel_hit)four_panel_hit=XZ_EXTRA_DETAIL_PANEL;
            if(four_tap&&(sx<0||sx>=XZ_WAVE_WIDTH||sy<0||sy>=XZ_WAVE_HEIGHT||four!=four_contact_order||
               !xz_four_layout_get(four_contact_order,&four_geometry)||xz_four_layout_lane_at(&four_geometry,sy)!=four_contact_deck))
                four_tap=0;
        }
        /* A release acts once: a panel key still under the finger, or a lane tap on the order it began on. */
        if(four_contact&&!status->down){
            if(four_panel_hit>XZ_EXTRA_DETAIL_PANEL&&four_panel_deck>=0&&four_panel_deck==detail){
                if(four_panel_hit==XZ_EXTRA_DETAIL_CLOSE&&xz_native_four_set_detail){xz_native_four_set_detail(-1);four_repaint=1;}
                else if(four_panel_hit==XZ_EXTRA_DETAIL_NATIVE_SIDE||four_panel_hit==XZ_EXTRA_DETAIL_EXTRA_SIDE){
                    four_select=four_panel_deck*2+(four_panel_hit==XZ_EXTRA_DETAIL_EXTRA_SIDE);four_repaint=1;
                }
            }else if(four_tap&&four>=0&&four==four_contact_order&&xz_native_four_set_detail){
                int index=four_contact_deck-2;
                xz_native_four_set_detail(detail==index?-1:index);four_repaint=1;
            }
            four_panel_hit=four_panel_deck=-1;four_tap=0;
        }
        if(four_contact&&!four_owned&&(status->down||status->x||status->y)&&xz_four_layout_get(four_contact_order,&four_geometry)){
            int row=(int)mapped.y-18-(int)four_geometry.lane_y[four_contact_deck];
            if(row<0)row=0;else if(row>=XZ_FOUR_LANE_HEIGHT)row=XZ_FOUR_LANE_HEIGHT-1;
            mapped.y=18+xz_four_layout_source_row(&four_geometry,(unsigned)row+four_geometry.lane_y[four_contact_deck]);
        }
        else if(inline_wave_contact&&(status->down||status->x||status->y)){
            struct xz_wave_layout captured;xz_wave_layout_get(inline_gesture_layout,&captured);
            int row=(int)mapped.y-18-(int)captured.lane_y[inline_wave_deck];
            if(row<0)row=0;else if(row>=(int)captured.lane_height)row=(int)captured.lane_height-1;
            mapped.y=18+xz_wave_layout_source_row(&captured,(unsigned)row+captured.lane_y[inline_wave_deck]);
        }
        int was_visible=touch.visible;
        owned=four_owned||xz_native_touch_dispatch(&touch,self,&mapped,mode,stock_touch);
        /* Keep SDK app-session navigation; table editing stays in the browse header. */
        if(!was_visible&&touch.visible&&sdk_session.active&&ui.page!=XZ_UI_APPS_SETUP)
            xz_ui_navigate(&ui,XZ_UI_APPS);
        if(!status->down)inline_wave_contact=four_contact=0;
    }
    __atomic_store_n(&visible, touch.visible, __ATOMIC_RELEASE);
    inline_layout_publish();
    if ((owned || !model.fb_takeover || !model.connection.connected) && forwarded_down && forward_touch) {
        forward_touch(0, (int)status->x, (int)status->y); forwarded_down = 0;
    } else if (!owned && model.fb_takeover && model.connection.connected && forward_touch && !!status->down != forwarded_down) {
        forwarded_down = !!status->down;
        forward_touch(forwarded_down, (int)status->x, (int)status->y);
    }
    if(inline_redraw_pending){pc_redraw=1;inline_redraw_pending=0;}
    pthread_mutex_unlock(&ui_mutex);
    if(four_select>=0)four_side_select(four_select>>1,four_select&1);
    if(pc_redraw||four_repaint)xz_native_skin_request_repaint();
}

/* The receiver treats a shell-owned screen exactly like the open panel. */
__attribute__((visibility("default"))) int xz_mods_visible_v1(void) {
    if(stock_dialog_up())return 0;
    return __atomic_load_n(&visible, __ATOMIC_ACQUIRE)||__atomic_load_n(&play_key_visible,__ATOMIC_ACQUIRE)||(xz_shell_runtime_owns&&xz_shell_runtime_owns());
}
/* Lock-free: the next xz_mods_render_v1 that draws writes every pixel (the shell owns the screen), so
   the receiver can skip copying its base frame under it. */
__attribute__((visibility("default"))) int xz_mods_full_frame_v1(void) {
    if(stock_dialog_up())return 0;
    return !__atomic_load_n(&visible, __ATOMIC_ACQUIRE)&&!__atomic_load_n(&fb_takeover_active,__ATOMIC_ACQUIRE)&&
        xz_shell_runtime_owns&&xz_shell_runtime_owns();
}

__attribute__((visibility("default"))) int xz_mods_native_touch_v1(void) {
    return __atomic_load_n(&started, __ATOMIC_ACQUIRE);
}

__attribute__((visibility("default"))) int xz_mods_takeover_v1(void) {
    if(stock_dialog_up())return 0;
    return __atomic_load_n(&fb_takeover_active, __ATOMIC_ACQUIRE) &&
        __atomic_load_n(&network_enabled, __ATOMIC_ACQUIRE) &&
        __atomic_load_n(&network_connected, __ATOMIC_ACQUIRE);
}

/* Native Shortcut screen: window index 29 with the shortcut controller alive. */
static int shortcut_contact = -1;
static int shortcut_cancelled;
static int shortcut_visible(void) {
    uint32_t index, controller;
    return !touch.visible && !xz_read_memory(0x3c6fc7c, &index, 4) && index == 29 &&
           !xz_read_memory(0x1c888d0, &controller, 4) && controller;
}
/* Stock per-media waveform colour bytes (3 RGB, 1 BLUE) for player 0, media slots 1-5. */
static int shortcut_stock_rgb(void) {
    for (uint32_t slot = 1; slot <= 5; slot++) {
        uint8_t value;
        if (!xz_read_memory(0x222e36c + slot * 0xbc + 0xe, &value, 1) && value == 3) return 1;
    }
    return 0;
}
static void shortcut_wave(uint16_t *pixels, uint32_t stride) {
    if (!shortcut_visible()) return;
    uint32_t colors[3];
    for (int i = 0; i < 3; i++) colors[i] = xz_ui_model_stem_color(&model, i);
    int selected = model.wave_mode == 2 ? 3 : model.wave_mode == 1 ? 2 : shortcut_stock_rgb();
    xz_ui_render_shortcut_wave(pixels, stride, selected, colors);
}
/* Taps on the redrawn group. BLUE/RGB go to the stock buttons underneath, so the
 * stock setting and its saving stay native; 3BAND/STEMS switch the layered mode. */
static int shortcut_touch(void *self, const struct xz_touch_status *status, const void *mode, int previous_down) {
    if (shortcut_contact < 0) {
        if (previous_down || !status->down || !shortcut_visible()) return 0;
        shortcut_contact = xz_ui_shortcut_wave_hit((int)status->x, (int)status->y);
        if (shortcut_contact < 0) return 0;
        shortcut_cancelled=0;
    }
    int segment = shortcut_contact;
    if(!shortcut_visible()||
       ((status->down||status->x||status->y)&&xz_ui_shortcut_wave_hit((int)status->x,(int)status->y)!=segment))
        shortcut_cancelled=1;
    if (segment < 2) {
        struct xz_touch_status mapped = *status;
        if(!shortcut_cancelled){mapped.x = segment ? 455 : 385; mapped.y = 252;}
        stock_touch(self, &mapped, mode);
    }
    if (!status->down) {
        int wanted = segment >= 2 ? segment - 1 : XZ_WAVE_STOCK;
        if (!shortcut_cancelled&&wanted != model.wave_mode) {
            struct xz_ui_action action = { .kind = XZ_UI_WAVE_MODE, .index = wanted };
            apply(NULL, &action, 1);
            xz_native_skin_request_repaint();
        }
        shortcut_contact = -1;
        shortcut_cancelled=0;
    }
    return 1;
}
static void badge(uint16_t *pixels, uint32_t stride) {
    __atomic_store_n(&xz_mods_key_feedback_proof_v1.enabled,!!model.key_feedback,__ATOMIC_RELAXED);
    shortcut_wave(pixels, stride);
    int play=xz_native_play_visible();
    if(model.theme==XZ_THEME_LCARS&&play&&!model.fb_takeover)xz_ui_render_lcars_play_header(pixels,stride);
    if(!model.fb_takeover){
        /* Each surface draws only when its stock pixels are on this frame (see key_strip.h). */
        for(int deck=0;deck<2;deck++) {
            if(xz_key_strip_render_deck(pixels,stride,model.theme,deck,&key_strip[deck],model.key_feedback)) {
                __atomic_add_fetch(&xz_mods_key_strip_proof_v1.drawn[deck],1,__ATOMIC_RELAXED);
                struct xz_key_strip_box box;
                if(xz_key_strip_area(pixels,stride,model.theme,deck,&box))
                    __atomic_add_fetch(box.kind==XZ_KEY_STRIP_PLAY?&xz_mods_key_feedback_proof_v1.play_drawn[deck]:&xz_mods_key_feedback_proof_v1.browse_drawn[deck],1,__ATOMIC_RELAXED);
            }
        }
        /* Browse header: native mode 2..9 except 6, the stock sort screen itself. Without
           the mode reader the cells are labels only, still teaching TAP MENU. */
        unsigned browse_mode=xz_native_browse_mode?xz_native_browse_mode():0;
        int interactive=xz_native_browse_open_sort&&browse_mode>1&&browse_mode!=6;
        if(head_notice_until&&(int32_t)(xz_game_milliseconds()-head_notice_until)>=0)head_notice_until=0;
        struct xz_browse_head head={interactive,head_pressed,head_notice_until?"SORT MENU NOT AVAILABLE ON THIS LIST":NULL};
        head_shown=0;
        if(!play&&browse_mode!=6&&xz_browse_head_render(pixels,stride,model.theme,&head)){
            head_shown=interactive;__atomic_add_fetch(&xz_mods_key_strip_proof_v1.head_drawn,1,__ATOMIC_RELAXED);
        }
    }else head_shown=0;
    if(!model.fb_takeover)xz_ui_render_ram_debug(pixels,stride,model.theme,model.ram_debug&&!model.metrics_enabled,model.ram_debug_valid,model.ram_free_mib);
    xz_ui_notice_restore(&notice_pixels,pixels,stride);
    xz_ui_render_native_buttons(pixels,stride,model.theme,xz_classic_stems_lamp(model.stems_overlay,xz_deck_layout_get()!=XZ_LAYOUT_TWO),
        model.connection.enabled&&model.connection.connected&&(model.fb_takeover||play),model.fb_takeover);
    badge_frame_cache.notice=async_notice;
    if(!model.fb_takeover)xz_ui_notice_badge(pixels,stride,model.theme,&async_notice,xz_game_milliseconds(),&notice_pixels);
    xz_metrics_draw(pixels,stride,model.theme,model.metrics_enabled,&model.metrics,xz_game_milliseconds());
    struct xz_extra_decks extra;int have_extra=play_key_extra_locked(&extra);
    xz_classic_key_render(&classic_keys,&model,classic_key_extra(&extra,have_extra),play&&!model.fb_takeover,
        xz_native_four_order(),pixels,stride);
    dv.painted=0;badge_frame_cache.dv=0;
    if(play&&!model.fb_takeover&&model.ui_interface==XZ_UI_INTERFACE_CLASSIC){
        /* DECK VIEW: the button in every view, decks 3 and 4 grey while their route is down; the open picker. */
        uint32_t now=xz_game_milliseconds();
        int view=xz_deck_view_valid(xz_deck_layout_get()),ready=have_extra&&extra.route_up;
        if(!dv_picker_live(now))dv.open=0;
        if(dv.focus!=DV_FOCUS_NONE&&!dv_focus_live(now)){dv.focus=DV_FOCUS_NONE;dv_publish_card();}
        struct xz_canvas c={pixels,stride,800,480,model.theme};
        int pair=view==XZ_LAYOUT_TWO?(int)classic_pair():0;
        xz_deck_view_button_draw(c,&xz_deck_view_classic,view,ready,pair,dv.open,dv.focus==DV_FOCUS_BUTTON);
        if(dv.open)xz_deck_view_picker_draw(c,&xz_deck_view_classic,view,ready,dv.tile);
        dv.painted=1;dv.painted_open=dv.open;dv.painted_view=view;
        badge_frame_cache.dv=1;badge_frame_cache.dv_view=view;badge_frame_cache.dv_ready=ready;badge_frame_cache.dv_pair=pair;
        badge_frame_cache.dv_open=dv.open;badge_frame_cache.dv_tile=dv.tile;badge_frame_cache.dv_focus=dv.focus==DV_FOCUS_BUTTON;
    }
    __atomic_store_n(&classic_key_visible,!!classic_keys.shown,__ATOMIC_RELEASE);
}

void xz_ui_runtime_profile_usb(unsigned slot) {
    if (slot < 2 && __atomic_exchange_n(&profile_slot,slot,__ATOMIC_RELAXED)!=slot)
        pthread_cond_signal(&settings_changed);
}
int xz_ui_runtime_profile_selected(void) {
    unsigned slot = __atomic_load_n(&profile_slot, __ATOMIC_RELAXED);
    return slot < 2 ? (int)slot : -1;
}
void xz_ui_runtime_profile_snapshot(struct xz_settings *out) {
    if (!out) return;
    pthread_mutex_lock(&ui_mutex); *out = settings_snapshot(); pthread_mutex_unlock(&ui_mutex);
}
static void profile_apply_locked(const struct xz_settings *saved,const char *origin) {
    int previous_theme=model.theme;
    settings_revision++;settings_profile_applies++;settings_owner_valid=0;
    model.settings_status="PROFILE APPLIED / NOT SAVED";
    if(model.stems_overlay!=saved->stems_overlay){stem_layout_revision++;stem_layout_origin=origin;}
    model.jump = saved->jump; model.jump.shift_page=1; model.stems_overlay = saved->stems_overlay; model.wave_mode = saved->wave_mode;
    model.modern_stems = (unsigned)saved->modern_stems < XZ_MODERN_STEMS_COUNT ? saved->modern_stems : XZ_MODERN_STEMS_PADS;
    requested_stems = saved->stems; model.stem_page = saved->stem_page;
    model.ui_interface = saved->ui_interface; model.classic_theme = saved->classic_theme; model.modern_theme = saved->modern_theme;
    model.theme = xz_settings_look(model.ui_interface, model.classic_theme);
    shell_follow_interface_locked();
    model.shift_pages = 1; model.pad_feedback = saved->pad_feedback;
    model.shift_keysync = 1; model.takeover_assign = saved->takeover_assign == XZ_TAKEOVER_REKORDBOX ? XZ_TAKEOVER_LINK : saved->takeover_assign;
    model.rekordbox_assign[0] = (unsigned)saved->rekordbox_press < XZ_REKORDBOX_ACTIONS ? saved->rekordbox_press : XZ_REKORDBOX_INTERFACE;
    model.rekordbox_assign[1] = (unsigned)saved->rekordbox_shift < XZ_REKORDBOX_ACTIONS ? saved->rekordbox_shift : XZ_REKORDBOX_STOCK;
    model.deck_view = (unsigned)saved->deck_view < XZ_DECK_VIEW_COUNT ? saved->deck_view : XZ_DECK_VIEW_TWO;
    model.four_wave_mixer = model.deck_view == XZ_DECK_VIEW_MIXER;
    xz_deck_layout_load(model.deck_view); layout_seen = xz_deck_layout_revision();
    model.stem_bank = saved->stem_bank; pads.bank = saved->stem_bank;
    model.stem_order = (unsigned)saved->stem_order < XZ_STEM_ORDER_COUNT ? saved->stem_order : 0; pads.order = model.stem_order;
    browse_settings(saved);
    memcpy(model.stem_colors,saved->stem_colors,sizeof(model.stem_colors));
    memcpy(model.band_colors,saved->band_colors,sizeof model.band_colors);
    modern_browse_columns=saved->modern_browse_columns;
    model.controller_locale=(unsigned)saved->controller_locale;
    modern_display.modern_wave_color=saved->modern_wave_color;modern_display.wave_position=saved->wave_position;
    modern_display.key_display=saved->key_display;modern_display.wave_zoom=saved->wave_zoom;
    modern_display.usb_colors=saved->usb_colors;modern_display.hidden_parts=saved->hidden_parts;
    xz_runtime_set_cues(saved->gate, saved->smart);
    model.enabled = xz_runtime_cue_flags();performance_publish_locked();
    if (audio_available) { xz_audio_set_enabled(saved->stems); if (saved->stems) model.enabled |= XZ_UI_STEM; }
    model.fb_takeover=xz_sdk_ui_session_profile(&sdk_session,saved->fb_takeover);
    __atomic_store_n(&fb_takeover_active,model.fb_takeover,__ATOMIC_RELEASE);
    if(previous_theme!=model.theme)xz_native_skin_request(model.theme);
    wave_configure();
    inline_layout_publish();
}
void xz_ui_runtime_profile_apply(const struct xz_settings *saved) {
    if (!saved) return;
    pthread_mutex_lock(&ui_mutex); profile_apply_locked(saved,"explicit-profile"); pthread_mutex_unlock(&ui_mutex);
}

void xz_ui_runtime_on_source_key(int source) {
    if (!__atomic_load_n(&started, __ATOMIC_ACQUIRE)) return;
    pthread_mutex_lock(&ui_mutex);
    if (model.takeover_assign == source) {
        struct xz_ui_action act = { .kind = XZ_UI_TAKEOVER_TOGGLE };
        apply(NULL, &act, 1);
    }
    pthread_mutex_unlock(&ui_mutex);
}

static int classic_mixer_present(uint16_t *pixels,size_t stride,size_t count,int eligible,int theme,
                                const struct xz_key_strip_deck keys[2]) {
    if(!xz_classic_mixer_runtime_frame&&!xz_classic_mixer_runtime_pair_frame)return 0;
    struct xz_classic_mixer_key names[2];memset(names,0,sizeof names);
    for(int d=0;d<2;d++)if(keys&&keys[d].valid){
        names[d].valid=1;names[d].cents=keys[d].cents;
        snprintf(names[d].effective,sizeof names[d].effective,"%s",keys[d].effective);
        snprintf(names[d].original,sizeof names[d].original,"%s",keys[d].original);
    }
    int layout=xz_deck_layout_get(),order=xz_native_four_order();
    if(layout==XZ_LAYOUT_TWO&&xz_classic_mixer_runtime_pair_frame){
        unsigned pair=eligible?classic_pair():0u;
        if(pair&&!xz_native_play_visible())pair=0;
        return xz_classic_mixer_runtime_pair_frame(pixels,stride,count,eligible&&pair,theme,xz_game_milliseconds(),names,pair);
    }
    if(!xz_classic_mixer_runtime_frame)return 0;
    eligible=eligible&&xz_native_play_visible()&&layout!=XZ_LAYOUT_TWO&&
        order==(layout==XZ_LAYOUT_MIXER?XZ_FOUR_MIXER:XZ_FOUR_STACKED);
    return xz_classic_mixer_runtime_frame(pixels,stride,count,eligible,theme,xz_game_milliseconds(),names);
}
/* MODERN's MODS dock keeps a known counter error in view (stock errors are never covered); CLASSIC leaves the stock
   deck area visible instead. Unknown, failed or torn reads show nothing. */
static void counter_errors_locked(void){
    uint32_t ids[2]={0};unsigned mask=model.ui_interface==XZ_UI_INTERFACE_MODERN?stock_counter_mask(ids):0;
    for(int d=0;d<2;d++){
        struct xz_shell_alert a={0};
        if(!(mask>>d&1u)||!xz_stock_counter_alert(ids[d],&a))a.code=a.text=NULL;
        model.counter_error[d]=(struct xz_ui_counter_error){a.code,a.text};
    }
}
static int render_mods_frame(uint16_t *pixels, uint32_t width,
        uint32_t height, uint32_t stride, const struct xz_vj_connection_v1 *connection) {
    if (!__atomic_load_n(&started, __ATOMIC_ACQUIRE) || !pixels || width != 800 || height != 480 || stride < 800 || stride > 16384)
        return 0;
    if(stock_dialog_up())return 0;
    struct xz_ram_usage ram_usage={0};
    if(xz_ram_usage)xz_ram_usage(&ram_usage);
    if (pthread_mutex_trylock(&ui_mutex) != 0) {
        if(__atomic_load_n(&play_key_visible,__ATOMIC_ACQUIRE)||__atomic_load_n(&classic_key_visible,__ATOMIC_ACQUIRE))return 0;
        __atomic_add_fetch(&xz_mods_key_present_proof_v1.busy,1,__ATOMIC_RELAXED);
        /* The shell needs nothing from the UI model for one frame: draw it from the previous input. */
        if(shell_input_cached&&xz_shell_runtime_frame&&xz_shell_runtime_owns&&xz_shell_runtime_owns()&&
           !__atomic_load_n(&visible,__ATOMIC_ACQUIRE)&&!__atomic_load_n(&fb_takeover_active,__ATOMIC_ACQUIRE)){
            shell_input_cache.cached=1;
            return xz_shell_runtime_frame(pixels,stride,&shell_input_cache)>0;
        }
        if(!__atomic_load_n(&visible,__ATOMIC_ACQUIRE)&&
           !__atomic_load_n(&fb_takeover_active,__ATOMIC_ACQUIRE)&&!(xz_shell_runtime_owns&&xz_shell_runtime_owns())) {
            int drawn=0;
            uint32_t scene;
            if(badge_frame_cache.valid&&!xz_read_memory(0x3c6fc7c,&scene,4)&&scene==badge_frame_cache.scene) {
                if(badge_frame_cache.shortcut)
                    xz_ui_render_shortcut_wave(pixels,stride,badge_frame_cache.selected,badge_frame_cache.colors);
                xz_ui_notice_restore(&notice_pixels,pixels,stride);
                xz_ui_render_native_buttons(pixels,stride,badge_frame_cache.theme,badge_frame_cache.layout,
                    badge_frame_cache.vj,badge_frame_cache.takeover);
                xz_ui_notice_badge(pixels,stride,badge_frame_cache.theme,&badge_frame_cache.notice,xz_game_milliseconds(),&notice_pixels);
                xz_metrics_draw(pixels,stride,badge_frame_cache.theme,badge_frame_cache.metrics_enabled,&badge_frame_cache.metrics,xz_game_milliseconds());
                if(badge_frame_cache.dv){
                    struct xz_canvas c={pixels,stride,800,480,badge_frame_cache.theme};
                    xz_deck_view_button_draw(c,&xz_deck_view_classic,badge_frame_cache.dv_view,badge_frame_cache.dv_ready,badge_frame_cache.dv_pair,
                        badge_frame_cache.dv_open,badge_frame_cache.dv_focus);
                    if(badge_frame_cache.dv_open)xz_deck_view_picker_draw(c,&xz_deck_view_classic,badge_frame_cache.dv_view,
                        badge_frame_cache.dv_ready,badge_frame_cache.dv_tile);
                }
                if(__atomic_load_n(&xz_mods_ram_debug_v1.enabled,__ATOMIC_ACQUIRE))
                    xz_ui_render_ram_debug(pixels,stride,badge_frame_cache.theme,badge_frame_cache.ram_debug&&!badge_frame_cache.metrics_enabled,badge_frame_cache.ram_valid,badge_frame_cache.ram_free_mib);
                drawn=1;
            }
            if(__atomic_load_n(&xz_mods_key_feedback_proof_v1.enabled,__ATOMIC_ACQUIRE))
                drawn|=xz_key_frame_cache_draw(&key_frame_cache,pixels,stride);
            drawn|=classic_mixer_present(pixels,stride,(size_t)stride*height,badge_frame_cache.valid,
                                        badge_frame_cache.theme,key_frame_cache.valid?key_frame_cache.deck:NULL);
            if(drawn){__atomic_add_fetch(&xz_mods_key_present_proof_v1.cached,1,__ATOMIC_RELAXED);return 1;}
        }
        return 0;
    }
    model.ram_original_bytes=ram_usage.original_pending+ram_usage.original_held;
    model.ram_asset_bytes=ram_usage.assets_pending+ram_usage.assets_held;
    /* Between shell frames there is nothing to refresh or draw; the receiver keeps the last frame. */
    int shell_path=xz_shell_runtime_frame&&shell_owns_locked();
    if(!play_key_source_valid_locked()){
        play_key.open=0;play_key_dirty=1;__atomic_store_n(&play_key_visible,0,__ATOMIC_RELEASE);
    }
    if(shell_path){classic_keys.shown=0;__atomic_store_n(&classic_key_visible,0,__ATOMIC_RELEASE);}
    if(shell_path&&!play_key.open&&!play_key_dirty&&xz_shell_runtime_idle&&xz_shell_runtime_idle()){pthread_mutex_unlock(&ui_mutex);return 0;}
    /* The shell draws from its own model and reads only the key snapshots from this one, so while it
       owns the screen the UI model refreshes at 10 Hz instead of on every 60 Hz frame. */
    {
        uint32_t now=xz_game_milliseconds();
        /* While the shell owns the screen: 10 Hz, and not in a frame where the shell refreshes its model
           (both together could outlast a vblank); never more than 200 ms apart. */
        uint32_t age=now-shell_ui_refreshed_ms;
        int due=!shell_ui_refreshed_ms||age>=200u||(age>=100u&&!(xz_shell_runtime_will_refresh&&xz_shell_runtime_will_refresh()));
        if(!shell_path||due){refresh();shell_ui_refreshed_ms=now?now:1;}
    }
    if (connection && connection->version == 1 && connection->size == sizeof(*connection)) {
        memcpy(device_ip, connection->ipv4, sizeof(device_ip)); device_ip[15] = 0;
        model.connection.ready = 1;
        model.connection.enabled = connection->listening != 0;
        model.connection.connected = connection->connected != 0;
        __atomic_store_n(&network_enabled, model.connection.enabled, __ATOMIC_RELEASE);
        __atomic_store_n(&network_connected, model.connection.connected, __ATOMIC_RELEASE);
        model.connection.discoverable = connection->discovery != 0;
        model.connection.device_ip = device_ip;
        model.connection.port = 50005;
        model.connection.protocol = "VJFS / VJVP / VJTE + VJXZ";
        model.connection.stats_valid = connection->stats_valid != 0;
        model.connection.frame_hz = connection->frame_hz_milli / 1000.0f;
        model.connection.status = "CONNECTION SERVICE CONFIGURED BY THE MOD LOADER";
    }
    if(shell_path){
        /* The shell renders under its own mutex only: pads, keys, the encoder and touch never wait
           on ui_mutex for a frame. 0 = it gave the screen up during this call; draw the usual way. */
        struct xz_shell_runtime_frame_input in;shell_frame_input_locked(&in);
        shell_input_cache=in;shell_input_cached=1;
        struct xz_play_key key_frame=play_key;struct xz_play_key_view key_view;
        play_key_view_locked(&key_view);play_key_dirty=0;
        pthread_mutex_unlock(&ui_mutex);
        int drawn=xz_shell_runtime_frame(pixels,stride,&in);
        if(drawn){
            int valid=play_key_binding_valid(&key_frame);
            if(drawn>0&&key_frame.open){
                if(valid)xz_play_key_render(&key_frame,&key_view,pixels,stride);
                if(!pthread_mutex_trylock(&ui_mutex)){
                    if(valid&&play_key_binding_valid(&play_key))xz_play_key_published(&play_key,&key_frame);
                    else if(play_key.instance==key_frame.instance&&play_key.revision==key_frame.revision){
                        play_key.open=0;play_key_dirty=1;__atomic_store_n(&play_key_visible,0,__ATOMIC_RELEASE);
                    }
                    pthread_mutex_unlock(&ui_mutex);
                }
            }
            return drawn>0;
        }
        if(pthread_mutex_trylock(&ui_mutex)!=0)return 0;
    }
    int result,shell=0,classic=0;
    key_frame_cache.enabled=0;
    badge_frame_cache.valid=0;
    game_frame.count=0;
    struct xz_shell_runtime_frame_input shell_input;
    if(xz_shell_runtime_frame){
        shell_frame_input_locked(&shell_input);
        /* Tell the shell it lost the screen to MODS or a game, so it stops claiming it. */
        if(!shell_input.eligible)xz_shell_runtime_frame(pixels,stride,&shell_input);
    }
    if(xz_rhythm_lab_present())result=xz_rhythm_lab_draw(pixels,(size_t)stride*height,stride,xz_game_milliseconds(),&game_frame);
    else if(xz_wave_rider_present())result=xz_wave_rider_draw(pixels,(size_t)stride*height,stride,xz_game_milliseconds(),&game_frame);
    else if(arcade_present){
        arcade_tick();
        if(arcade_present)result=xz_arcade_render_targets(&arcade,pixels,(size_t)stride*height,stride,&game_frame);
        else {badge(pixels,stride);result=1;}
        xz_arcade_proof_v1.frames++;
        xz_arcade_proof_v1.phase=arcade.phase;xz_arcade_proof_v1.mode=arcade.mode;
        xz_arcade_proof_v1.live=arcade.live;xz_arcade_proof_v1.deck=arcade.clock_deck;
        xz_arcade_proof_v1.bpm=arcade.bpm;xz_arcade_proof_v1.x=arcade.x;xz_arcade_proof_v1.y=arcade.y;
    }
    else if (touch.visible) { model.clock_ms = xz_game_milliseconds(); counter_errors_locked(); result = xz_ui_render(&ui, &model, pixels, (size_t)stride * height, stride); }
    else if (xz_shell_runtime_frame&&(shell=xz_shell_runtime_frame(pixels,stride,&shell_input))!=0) result = shell>0;
    else {
        xz_key_frame_cache_store(&key_frame_cache,model.theme,model.key_feedback,model.fb_takeover,key_strip);
        badge_frame_cache.valid=!model.fb_takeover&&!xz_read_memory(0x3c6fc7c,&badge_frame_cache.scene,4);
        badge_frame_cache.ram_debug=model.ram_debug;badge_frame_cache.ram_valid=model.ram_debug_valid;
        badge_frame_cache.ram_free_mib=model.ram_free_mib;
        badge_frame_cache.metrics_enabled=model.metrics_enabled;badge_frame_cache.metrics=model.metrics;
        badge_frame_cache.theme=model.theme;badge_frame_cache.layout=xz_classic_stems_lamp(model.stems_overlay,xz_deck_layout_get()!=XZ_LAYOUT_TWO);
        badge_frame_cache.takeover=model.fb_takeover;
        badge_frame_cache.vj=model.connection.enabled&&model.connection.connected&&xz_native_play_visible();
        badge_frame_cache.shortcut=shortcut_visible();
        badge_frame_cache.selected=model.wave_mode==2?3:model.wave_mode==1?2:shortcut_stock_rgb();
        for(int i=0;i<3;i++)badge_frame_cache.colors[i]=xz_ui_model_stem_color(&model,i);
        badge(pixels,stride);result=1;
        classic=model.ui_interface==XZ_UI_INTERFACE_CLASSIC&&!model.fb_takeover;
        classic_mixer_present(pixels,stride,(size_t)stride*height,classic,model.theme,key_strip);
    }
    if(!classic&&xz_classic_mixer_runtime_hide)xz_classic_mixer_runtime_hide();
    if(result&&!touch.visible&&game_locked()==GAME_NONE&&!model.fb_takeover){
        if(!play_key_source_valid_locked()){play_key.open=0;__atomic_store_n(&play_key_visible,0,__ATOMIC_RELEASE);}
        struct xz_play_key_view view;play_key_view_locked(&view);
        xz_play_key_render(&play_key,&view,pixels,stride);
        xz_play_key_published(&play_key,&play_key);
    }
    if(game_locked()!=GAME_NONE&&result){
        xz_game_nav_frame(&game_nav,&game_frame);
        xz_game_nav_draw(&game_nav,pixels,stride,(int)width,(int)height);
    }
    pthread_mutex_unlock(&ui_mutex);
    return result;
}

/* The receiver supplies the stock backbuffer before any overlay/suppressed flip.
   A presenter frame can start from video pixels, so its native warning region
   must come from this capture, never from a previously composited mod frame. */
static pthread_mutex_t stock_counter_mutex=PTHREAD_MUTEX_INITIALIZER;
static uint16_t stock_counter_pixels[2][XZ_STOCK_COUNTER_W*XZ_STOCK_COUNTER_H];
static uint32_t stock_counter_cached_ids[2];
static unsigned stock_counter_valid;
__attribute__((visibility("default"))) unsigned xz_mods_stock_counter_mask_v1(void){return stock_counter_mask(NULL);}
__attribute__((visibility("default"))) void xz_mods_native_frame_v1(const uint16_t *pixels,uint32_t width,uint32_t height,uint32_t stride){
    if(!pixels||width!=800||height!=480||stride<800||stride>16384)return;
    uint32_t ids[2]={0};unsigned mask=stock_counter_mask(ids);
    pthread_mutex_lock(&stock_counter_mutex);
    stock_counter_valid=mask;
    for(unsigned d=0;d<2;d++)if(mask&(1u<<d)){
        for(unsigned y=0;y<XZ_STOCK_COUNTER_H;y++)memcpy(stock_counter_pixels[d]+y*400,
            pixels+(y+XZ_STOCK_COUNTER_Y)*(size_t)stride+d*400,400*sizeof *pixels);
        stock_counter_cached_ids[d]=ids[d];
    }
    pthread_mutex_unlock(&stock_counter_mutex);
}
__attribute__((visibility("default"))) int xz_mods_render_v1(uint16_t *pixels,uint32_t width,
        uint32_t height,uint32_t stride,const struct xz_vj_connection_v1 *connection){
    if(!__atomic_load_n(&started,__ATOMIC_ACQUIRE))return 0;
    if(!pixels||width!=800||height!=480||stride<800||stride>16384)return 0;
    uint32_t ids[2]={0};unsigned mask=stock_counter_mask(ids);
    int classic=__atomic_load_n(&stock_counter_classic,__ATOMIC_ACQUIRE);
    if(mask&&!pthread_mutex_trylock(&ui_mutex)){
        classic=model.ui_interface==XZ_UI_INTERFACE_CLASSIC;
        __atomic_store_n(&stock_counter_classic,classic,__ATOMIC_RELEASE);
        pthread_mutex_unlock(&ui_mutex);
    }
    static _Thread_local unsigned previous_mask;
    if(!mask&&previous_mask){
        pthread_mutex_lock(&stock_counter_mutex);stock_counter_valid=0;pthread_mutex_unlock(&stock_counter_mutex);
    }
    previous_mask=mask;
    if(!mask||!classic)return render_mods_frame(pixels,width,height,stride,connection);
    static _Thread_local uint16_t saved[2][XZ_STOCK_COUNTER_W*XZ_STOCK_COUNTER_H];
    pthread_mutex_lock(&stock_counter_mutex);
    for(unsigned d=0;d<2;d++)if(mask&(1u<<d)){
        if((stock_counter_valid&(1u<<d))&&stock_counter_cached_ids[d]==ids[d])memcpy(saved[d],stock_counter_pixels[d],sizeof saved[d]);
        else for(unsigned y=0;y<XZ_STOCK_COUNTER_H;y++)memcpy(saved[d]+y*400,
            pixels+(y+XZ_STOCK_COUNTER_Y)*(size_t)stride+d*400,400*sizeof *pixels);
    }
    pthread_mutex_unlock(&stock_counter_mutex);
    int drawn=render_mods_frame(pixels,width,height,stride,connection);
    /* Even an idle/busy renderer must publish freshly captured native error pixels. */
    for(unsigned d=0;d<2;d++)if(mask&(1u<<d))for(unsigned y=0;y<XZ_STOCK_COUNTER_H;y++)
        memcpy(pixels+(y+XZ_STOCK_COUNTER_Y)*(size_t)stride+d*400,saved[d]+y*400,400*sizeof *pixels);
    return stock_dialog_up()?0:drawn?drawn:1;
}

int xz_ui_runtime_start(int audio_ready, int key_ready, int stems_enabled) {
    /* Which computer made this loader, for ABOUT and REPORT AN ISSUE. Read once; older loaders have none. */
    if (xz_support_identity_load) xz_support_identity_load(getenv("XZ_MODS_USB"));
    static const unsigned char guard[8] = {0xf0,0x45,0x2d,0xe9,0x02,0x70,0xa0,0xe1};
    static const unsigned char link_guard[8] = {0x70,0x40,0x2d,0xe9,0x00,0x50,0xa0,0xe1};
    static const unsigned char rekordbox_guard[8] = {0x38,0x40,0x2d,0xe9,0x00,0x50,0xa0,0xe1};
    if (started) return 0;
    memset(&play_key,0,sizeof play_key);classic_keys.shown=0;play_key_dirty=play_key_back_contact=play_key_raw_down=0;
    __atomic_store_n(&play_key_visible,0,__ATOMIC_RELEASE);__atomic_store_n(&classic_key_visible,0,__ATOMIC_RELEASE);
    settings_usb[0] = 0; settings_pending = settings_created = 0; settings_flash_at = 0;
    memset(&settings_profile,0,sizeof(settings_profile)); settings_slot=-1;
    __atomic_store_n(&profile_slot,~0u,__ATOMIC_RELAXED);
    forward_touch = (void (*)(int,int,int))dlsym(RTLD_DEFAULT, "xz_vj_touch_v1");
    if (!forward_touch) { xz_log("UI unavailable: receiver does not expose the optional display bridge"); return -1; }
    if(xz_classic_mixer_runtime_start&&xz_classic_mixer_runtime_start())
        xz_log("Classic four-channel readouts: snapshot allocation failed");
    audio_available = audio_ready;
    key_available = key_ready;
    if(xz_key_browser_start())xz_log("Key browser index worker unavailable");
    if(xz_key_traffic_start())xz_log("Effective-key traffic-light adapter unavailable: firmware guard rejected");
    memset(&model, 0, sizeof(model));
    settings_owner_valid=settings_target_empty=0;
    model.jump=xz_jump_defaults();
    memset(&jump_state,0,sizeof(jump_state));
    model.pad_feedback = 1;
    model.shift_pages=model.shift_keysync=1;
    model.stems_overlay = 1;model.modern_stems = XZ_MODERN_STEMS_PADS;
    model.wave_mode = XZ_WAVE_THREE_BAND;
    model.fb_takeover = 0;
    model.takeover_assign = XZ_TAKEOVER_LINK;
    model.rekordbox_assign[0] = XZ_REKORDBOX_INTERFACE; model.rekordbox_assign[1] = XZ_REKORDBOX_STOCK;
    memset(&rekordbox_latch, 0, sizeof(rekordbox_latch));
    __atomic_store_n(&fb_takeover_active, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&network_enabled, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&network_connected, 0, __ATOMIC_RELEASE);
    requested_stems = !!stems_enabled;
    model.settings_status = xz_settings_internal_save ? "SETTINGS SAVE ON THIS XZ" : "SETTINGS NOT SAVED: START FROM USB";
    { struct xz_settings defaults; xz_settings_default(&defaults); browse_settings(&defaults);
      model.ui_interface = defaults.ui_interface; model.classic_theme = defaults.classic_theme; model.modern_theme = defaults.modern_theme;
      model.theme = xz_settings_look(model.ui_interface, model.classic_theme); }
    memset(key_sync, 0, sizeof(key_sync));
    __atomic_store_n(&key_sync_led_mask,0,__ATOMIC_RELEASE);
    master_tempo_down=master_tempo_owned=0;
    model.enabled = xz_runtime_cue_flags();
    if (audio_ready && stems_enabled) model.enabled |= XZ_UI_STEM;
    int restored = 0;
    if (xz_settings_internal_load) {
        struct xz_settings remembered, merged = settings_snapshot(); unsigned present = 0;
        if (xz_settings_internal_load(&remembered, &present) == XZ_INTERNAL_SETTINGS_OK &&
            !xz_settings_overlay(&merged, &remembered, present)) {
            profile_apply_locked(&merged,"boot-xz"); restored = 1;
            model.settings_status = "SETTINGS RESTORED ON THIS XZ";
        }
    }
    const char *usb = getenv("XZ_MODS_SETTINGS_USB");
    if(!usb||!usb[0])usb=getenv("XZ_MODS_USB");
    if(usb&&usb[0])settings_restore_usb_locked(usb,restored);
    settings_readonly = !xz_mods_may_write();
    if (settings_readonly) model.settings_status = "CHANGES LAST UNTIL REBOOT";
    performance_publish_locked();wave_configure();
    if(audio_ready)xz_audio_set_enabled(requested_stems);
    for(size_t i=0;i<sizeof pads.count/sizeof pads.count[0];i++)pads.count[i]=3;
    for(size_t i=0;i<sizeof model.deck/sizeof model.deck[0];i++) {
        model.deck[i].groove_active = model.deck[i].sample_active = -1;
        for(unsigned r=0;r<4;r++)model.deck[i].levels[r]=1;
        model.deck[i].stem_count=3;
        model.deck[i].sample_volume = 1;
        model.deck[i].track = i < 2 ? "NO TRACK CAPTURED" : "EXTERNAL USB AUDIO";
        model.deck[i].status = i < 2 ? "NATIVE ADAPTER EXPERIMENTAL" : "CONTROL IN DJ SOFTWARE";
    }
    pc_available_roles=0;
    xz_pc_stems_init(&pc_stems,xz_pc_stems_native_sink());
    pc_observing=xz_pc_stems_native_observe()==0;
    static const uint8_t midi_guard[8]={0x22,0x30,0xd0,0xe5,0xf0,0x47,0x2d,0xe9};
    if(xz_hook_arm(0x27a3f4,midi_guard,(void*)pc_midi_hook,(void**)&stock_pc_midi)){
        pc_observing=0;xz_log("PC STEMS: MIDI feedback hook unavailable");
    }
    xz_ui_init(&ui);
    xz_ui_init(&inline_ui);
    xz_native_touch_init(&touch, &ui, &model, apply, NULL);
    refresh();
    if (xz_hook_arm(0x2628b4, guard, (void *)touch_hook, (void **)&stock_touch) != 0) return -1;
    if(key_ready) {
        static const unsigned char mt_guard[8]={0x38,0x40,0x2d,0xe9,0x00,0x40,0xa0,0xe1};
        static const unsigned char mt_on_guard[8]={0x70,0x40,0x2d,0xe9,0x01,0x50,0xa0,0xe1};
        unsigned char actual[8];
        if(!xz_read_memory(0x289790,actual,sizeof(actual))&&!memcmp(actual,mt_on_guard,sizeof(actual))&&
           !xz_hook_arm(0x28bac4,mt_guard,(void*)master_tempo_key_hook,(void**)&stock_master_tempo_key))
            native_master_tempo_on=(int(*)(void*,int))(uintptr_t)0x289790;
        else xz_log("Master Tempo Key Sync reset unavailable: firmware guard rejected");
    }
    static const unsigned char back_guard[8]={0xf0,0x4f,0x2d,0xe9,0x01,0x40,0xa0,0xe1};
    if(xz_hook_arm(0x2eae50,back_guard,(void *)menu_back_key_hook,(void **)&stock_global_key))
        xz_log("Physical MODS Back hook unavailable");
    if (xz_hook_arm(0xdf994, link_guard, (void *)uikey_link_hook, (void **)&stock_uikey_link) != 0)
        xz_log("Source key LINK hook unavailable");
    if (xz_hook_arm(0xe0a50, rekordbox_guard, (void *)uikey_rekordbox_hook, (void **)&stock_uikey_rekordbox) != 0)
        xz_log("Source key REKORDBOX hook unavailable");
    settings_running = 1;
    if (pthread_create(&settings_thread,NULL,settings_writer,NULL)) {
        settings_running = 0; model.settings_status = "SETTINGS SAVE UNAVAILABLE";
    } else settings_created=1;
    if(xz_dj_runtime_start&&!xz_dj_runtime_start(&ui_mutex,&dj_view))model.dj=&dj_view;
    else xz_log("DJ Profile reader unavailable; the MODS header shows no profile");
    static const unsigned char encoder_guard[8]={0x08,0x40,0x2d,0xe9,0x02,0x30,0x80,0xe2};
    model.jump_encoder=xz_hook_arm(0xe15b0,encoder_guard,(void*)encoder_rotate_hook,(void**)&stock_encoder_rotate)==0;
    if(!model.jump_encoder)xz_log("MODS encoder turn unavailable; touch controls remain available");
    static const unsigned char encoder_push_guard[8]={0x70,0x40,0x2d,0xe9,0x00,0x40,0xa0,0xe1};
    if(xz_hook_arm(0xdec08,encoder_push_guard,(void*)encoder_push_hook,(void**)&stock_encoder_push))
        xz_log("MODS encoder push unavailable; touch controls remain available");
    __atomic_store_n(&started, 1, __ATOMIC_RELEASE);
    pthread_mutex_lock(&ui_mutex);shell_follow_interface_locked();pthread_mutex_unlock(&ui_mutex);
    sdk_start();
    if(!xz_metrics_start||xz_metrics_start())xz_log("Metrics sampler unavailable; top-bar setup remains available");
    if(model.metrics_enabled&&xz_metrics_enable)xz_metrics_enable(1);
    if (xz_native_inline_start(inline_enabled,render_inline,NULL)) xz_log("Native inline stems unavailable: window ABI guard failed");
    if (xz_native_led_start()) xz_log("Native pad LED feedback unavailable");
    if (xz_native_skin_start(model.theme)) xz_log("Native whole-interface theme adapter unavailable");
    xz_log("UI touch adapter installed; MODS badge opens the panel");
    /* Private RAM acceptance can open the menu without synthesizing a DJ key.
       Ordinary USB boot never sets this flag. */
    const char *arcade_trial=getenv("XZ_MODS_ARCADE_TRIAL");
    if(arcade_trial&&(!strcmp(arcade_trial,"1")||!strcmp(arcade_trial,"wave-rider")||!strcmp(arcade_trial,"rhythm-lab")||!strcmp(arcade_trial,"static-surfer"))){
        pthread_mutex_lock(&ui_mutex);
        struct xz_ui_action action={.kind=!strcmp(arcade_trial,"static-surfer")?XZ_UI_GLYPH_RUN_OPEN:!strcmp(arcade_trial,"rhythm-lab")?XZ_UI_RHYTHM_LAB_OPEN:!strcmp(arcade_trial,"wave-rider")?XZ_UI_WAVE_RIDER_OPEN:XZ_UI_ARCADE_OPEN};apply(NULL,&action,1);
        if(action.kind==XZ_UI_RHYTHM_LAB_OPEN||action.kind==XZ_UI_GLYPH_RUN_OPEN)xz_rhythm_lab_touch(0,0,0,xz_game_milliseconds());
        if(action.kind==XZ_UI_WAVE_RIDER_OPEN)xz_wave_rider_touch(0,0,0,xz_game_milliseconds());
        arcade_opening_touch=0;arcade.touch_down=0;
        pthread_mutex_unlock(&ui_mutex);
    }
    return 0;
}

void xz_ui_runtime_stop(void) {
    if(xz_shell_runtime_stop)xz_shell_runtime_stop();
    if(xz_metrics_stop)xz_metrics_stop();
    sdk_stop();
    xz_key_browser_stop();
    xz_native_skin_stop();
    xz_native_led_stop();
    __atomic_store_n(&started, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&visible, 0, __ATOMIC_RELEASE);
    if(xz_classic_mixer_runtime_stop)xz_classic_mixer_runtime_stop();
    pthread_mutex_lock(&ui_mutex);
    arcade_present=0;arcade_contact_count=0;memset(arcade_contacts,0,sizeof arcade_contacts);arcade_input_publish_locked();memset(&arcade_input,0,sizeof(arcade_input));
    xz_wave_rider_close();arcade_input_publish_locked();
    xz_rhythm_lab_close();arcade_input_publish_locked();
    int join = settings_created; settings_created=0;
    settings_running = 0; pthread_cond_signal(&settings_changed);
    int dj = model.dj!=NULL; model.dj=NULL; ui.profile_open=0;
    pthread_mutex_unlock(&ui_mutex);
    if (join) pthread_join(settings_thread,NULL);
    if (dj && xz_dj_runtime_stop) xz_dj_runtime_stop();
}


/* Diary worker only. All text is reduced to fixed codes before leaving this lock. */
int xz_ui_diary_snapshot(struct xz_diary_view *out) {
    if(!xz_diary_notice_code||!xz_diary_error_code||!__atomic_load_n(&started,__ATOMIC_ACQUIRE)||pthread_mutex_trylock(&ui_mutex))return 0;
    out->settings=settings_snapshot();out->have_settings=1;
    out->notice=xz_diary_notice_code(ui.notice);
    out->usb_error=stock_window_code(model.stock_window);
    for(unsigned i=0;i<4;i++) {
        static const unsigned states[]={XZ_DIARY_UNKNOWN,XZ_DIARY_EMPTY,XZ_DIARY_LOADING,XZ_DIARY_READY,XZ_DIARY_ERROR,XZ_DIARY_REMOVED};
        unsigned state=(unsigned)model.deck[i].ram.state;
        out->ram_state[i]=state<sizeof states/sizeof states[0]?states[state]:XZ_DIARY_UNKNOWN;
        switch(model.deck[i].ram.reason){
        case XZ_RAM_OK:out->ram_reason[i]=XZ_DIARY_NONE;break;
        case XZ_RAM_BUDGET_FULL:case XZ_RAM_FREE_LOW:out->ram_reason[i]=XZ_DIARY_SPACE_LOW;break;
        case XZ_RAM_SOURCE_CHANGED:out->ram_reason[i]=XZ_DIARY_SOURCE_CHANGED;break;
        default:out->ram_reason[i]=XZ_DIARY_OBSERVED;break;
        }
        if(i<2)out->error[i]=xz_diary_error_code(model.counter_error[i].code);
    }
    pthread_mutex_unlock(&ui_mutex);return 1;
}
