#define _GNU_SOURCE
#include "ui_runtime.h"
#include "metrics_runtime.h"
/* Portable UI harnesses omit the OS sampler. */
#pragma weak xz_metrics_start
#pragma weak xz_metrics_stop
#pragma weak xz_metrics_enable
#pragma weak xz_metrics_snapshot
#include "runtime.h"
#include "audio/runtime.h"
#include "key/runtime.h"
#include "key/native.h"
#include "key/display.h"
#include "key/browser.h"
#include "key/traffic.h"
#include "key/native_browse.h"
#include "ui/key_strip.h"
#include "ui/key_frame_cache.h"
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
#include "ui/native_touch.h"
#include "ui/stem_pads.h"
#include "pc_control/stems.h"
#include "pc_control/wave.h"
#include "ui/native_led.h"
#include "ui/native_wave_runtime.h"
#include "ui/layered_wave_runtime.h"
#include "ui/native_skin_runtime.h"
#include "ui/wave_viewport.h"
#include "settings.h"
#include "settings_internal.h"
#include "usb_profile.h"
#include "dj_runtime.h"
#include "shell_runtime.h"
/* FLAGSHIP shell. Weak so UI harnesses link without it; the device build links it. */
#pragma weak xz_shell_runtime_start
#pragma weak xz_shell_runtime_stop
#pragma weak xz_shell_runtime_owns
#pragma weak xz_shell_runtime_idle
#pragma weak xz_shell_runtime_frame
#pragma weak xz_shell_runtime_touch
#pragma weak xz_shell_runtime_key
#pragma weak xz_shell_runtime_encoder_turn
#pragma weak xz_shell_runtime_encoder_push
#pragma weak xz_shell_runtime_take_ui_requests
#pragma weak xz_shell_runtime_notice
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
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>

static pthread_mutex_t ui_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct xz_ui ui;
static struct xz_ui inline_ui;
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
static struct { int valid, cents; uint32_t epoch; } key_sync[2];
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
static int menu_key_toggle(void);

static void rhythm_lab_to_dj(void) {
    xz_rhythm_lab_close();xz_native_touch_visible(&touch,0);
    __atomic_store_n(&visible,0,__ATOMIC_RELEASE);model.fb_takeover=lab_previous_takeover;
    __atomic_store_n(&fb_takeover_active,model.fb_takeover,__ATOMIC_RELEASE);xz_ui_navigate(&ui,XZ_UI_GAMES);
}
static void wave_rider_to_dj(void) {
    xz_wave_rider_close();
    xz_native_touch_visible(&touch,0);
    __atomic_store_n(&visible,0,__ATOMIC_RELEASE);
    model.fb_takeover=wave_rider_previous_takeover;
    __atomic_store_n(&fb_takeover_active,model.fb_takeover,__ATOMIC_RELEASE);
    xz_ui_navigate(&ui,XZ_UI_GAMES);
}
static void arcade_to_dj(void) {
    arcade_present=0;arcade.leave=0;
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
    unsigned channel=((const unsigned char *)self)[0x26];
    if(channel<1||channel>2)return 0;
    pthread_mutex_lock(&ui_mutex);
    int was_music=arcade.phase==XZ_ARCADE_MUSIC;
    if(xz_rhythm_lab_input((int)channel-1,input,xz_game_milliseconds())) {
        if(xz_rhythm_lab_leave_requested())rhythm_lab_to_dj();
        pthread_mutex_unlock(&ui_mutex);return 1;
    }
    if(xz_wave_rider_input((int)channel-1,input,xz_game_milliseconds())) {
        if(xz_wave_rider_leave_requested())wave_rider_to_dj();
        pthread_mutex_unlock(&ui_mutex);return 1;
    }
    int consumed=xz_arcade_input_event(&arcade_input,&arcade,arcade_present,(int)channel-1,input,xz_game_milliseconds());
    if(consumed)xz_arcade_proof_v1.inputs++;
    if(arcade_leaving(was_music))arcade_to_dj();
    pthread_mutex_unlock(&ui_mutex);return consumed;
}

static int uikey_link_hook(void *arg0) {
    xz_ui_runtime_on_source_key(0);
    return stock_uikey_link ? stock_uikey_link(arg0) : 0;
}

static int uikey_rekordbox_hook(void *arg0,void *arg1,void *arg2,void *arg3) {
    /* By default REKORDBOX is the hardware MODS key, so a broken touchscreen still
       reaches every setting. The ADVANCED page hands it back to the stock action. */
    if (menu_key_toggle()) return 1;
    xz_ui_runtime_on_source_key(1);
    return stock_uikey_rekordbox ? stock_uikey_rekordbox(arg0,arg1,arg2,arg3) : 0;
}

static struct xz_settings settings_snapshot(void) {
    return (struct xz_settings){.stems=requested_stems,
        .gate=!!(model.enabled & XZ_UI_GATE), .smart=!!(model.enabled & XZ_UI_SMART), .theme=model.theme,
        .stem_page=model.stem_page, .shift_pages=model.shift_pages, .pad_feedback=model.pad_feedback, .shift_keysync=model.shift_keysync,
        .fb_takeover=xz_sdk_ui_saved_takeover(&sdk_session,model.fb_takeover), .takeover_assign=model.takeover_assign, .stem_bank=model.stem_bank, .jump=model.jump,
        .stems_overlay=model.stems_overlay, .wave_mode=model.wave_mode,
        .browse_columns={model.browse_columns[0],model.browse_columns[1],model.browse_columns[2]},
        .browse_key_prefix=model.browse_key_prefix, .browse_traffic=model.browse_traffic, .key_format=model.key_format,
        .stem_colors={model.stem_colors[0],model.stem_colors[1],model.stem_colors[2]},.stem_order=model.stem_order,.key_feedback=model.key_feedback,.ram_debug=model.ram_debug,.rekordbox_menu=model.rekordbox_menu,.metrics_enabled=model.metrics_enabled};
}
/* Profile values that only the UI owns. The backend already rejects invalid profiles. */
static void browse_settings(const struct xz_settings *saved) {
    if (xz_key_browser_columns_valid(saved->browse_columns))
        memcpy(model.browse_columns, saved->browse_columns, sizeof(model.browse_columns));
    model.browse_key_prefix = !!saved->browse_key_prefix; model.browse_traffic = !!saved->browse_traffic;
    model.key_format = saved->key_format >= 0 && saved->key_format <= 2 ? saved->key_format : 0;
    model.metrics_enabled=!!saved->metrics_enabled;
    if(xz_metrics_enable)xz_metrics_enable(model.metrics_enabled);
    model.ram_debug=!!saved->ram_debug;
    __atomic_store_n(&xz_mods_ram_debug_v1.enabled,model.ram_debug,__ATOMIC_RELEASE);
    model.key_feedback = !!saved->key_feedback;
    __atomic_store_n(&xz_mods_key_feedback_proof_v1.enabled,model.key_feedback,__ATOMIC_RELEASE);
}
/* Stem layers use the saved pad colours: drums, harmonics, vocals. */
static void wave_configure(void) {
    uint32_t colors[3];
    for (int i = 0; i < 3; i++) colors[i] = xz_ui_model_stem_color(&model, i);
    xz_layered_wave_configure(model.wave_mode, colors);
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
static void settings_queue(void) {
    settings_revision++;
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
    int layout=(unsigned)model.stems_overlay<XZ_STEM_LAYOUT_COUNT?model.stems_overlay:XZ_STEM_LAYOUT_HIDDEN;
    __atomic_store_n(&inline_requested,(requested_stems||pc_present)&&!touch.visible?layout:XZ_STEM_LAYOUT_HIDDEN,__ATOMIC_RELEASE);
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
        else if(kind==XZ_UI_SOURCE_PC&&!source[d].rekordbox)out->reason="OPEN REKORDBOX IN PERFORMANCE MODE";
        else if(kind==XZ_UI_SOURCE_PC&&!source[d].loaded)out->reason="LOAD AN ANALYZED TRACK IN REKORDBOX";
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
        xz_native_touch_visible(&touch,1);__atomic_store_n(&visible,1,__ATOMIC_RELEASE);
        pc_setup_step[d]=XZ_UI_PC_STEP_DETECT;pc_refresh_locked();return;
    }
    if(a->kind==XZ_UI_PC_CHECK){
        pc_refresh_locked();
        struct xz_ui_pc_stems *state=&model.deck[d].pc_stems;
        if(state->source!=XZ_UI_SOURCE_PC||!state->connected){snprintf(ui.notice,sizeof(ui.notice),"%s",state->reason?state->reason:"SELECT A REKORDBOX DECK");return;}
        if(a->index==XZ_UI_PC_STEP_DONE)pc_setup_step[d]=XZ_UI_PC_STEP_DETECT;
        else if(pc_setup_step[d]==XZ_UI_PC_STEP_DETECT)pc_setup_step[d]=XZ_UI_PC_STEP_MAPPING;
        else if(pc_setup_step[d]==XZ_UI_PC_STEP_MAPPING)pc_setup_step[d]=XZ_UI_PC_STEP_TEST;
        pc_refresh_locked();return;
    }
    if(a->kind==XZ_UI_PC_TOGGLE&&a->phase==XZ_UI_PRESS){
        pc_refresh_locked();
        if(a->index<0||a->index>=XZ_PC_ROLES||!(pc_available_roles&(1u<<a->index))){snprintf(ui.notice,sizeof(ui.notice),"BASS IS PART OF INST / ENABLE 4 STEMS IN REKORDBOX");return;}
        if(!pc_action_current(a,&source)){snprintf(ui.notice,sizeof(ui.notice),"PC SOURCE OR TRACK CHANGED / CHECK AGAIN");return;}
        pthread_mutex_lock(&pc_mutex);
        int sent=xz_pc_stems_toggle(&pc_stems,(unsigned)source.slot,(unsigned)a->index,xz_game_milliseconds());
        pthread_mutex_unlock(&pc_mutex);
        snprintf(ui.notice,sizeof(ui.notice),"%s",sent?"STEM COMMAND SENT / WAITING FOR REKORDBOX":"STEM COMMAND NOT SENT / CHECK THE PC LINK");
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
        pc_setup_step[d]=XZ_UI_PC_STEP_CONFIRM;snprintf(ui.notice,sizeof(ui.notice),"TEST SENT / CHECK DRUMS IN REKORDBOX");
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
        snprintf(ui.notice,sizeof(ui.notice),"ONSCREEN STEMS CONTROL REKORDBOX");pc_refresh_locked();
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
} badge_frame_cache;
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
static int sync_key(int deck) {
    struct xz_key_snapshot source,master;struct xz_playing_key target;int master_shift,cents;
    if(deck<0||deck>1)return 0;
    if(!key_available){
        xz_ui_navigate(&ui,XZ_UI_KEY_SETUP);snprintf(ui.notice,sizeof(ui.notice),"KEY ENGINE UNAVAILABLE / REBUILD THE LOADER WITH THE CURRENT XZ MODS APP");
        return key_sync_fail(deck,XZ_KEY_SYNC_NO_ENGINE);
    }
    /* Each failure says on that deck's own key strip what to do; nothing pops up. */
    if(!xz_key_native_snapshot((unsigned)deck,&source))return key_sync_fail(deck,XZ_KEY_SYNC_NO_KEY);
    if(!xz_key_native_snapshot((unsigned)(1-deck),&master))return key_sync_fail(deck,XZ_KEY_SYNC_NO_MASTER_KEY);
    if(source.master)return key_sync_fail(deck,XZ_KEY_SYNC_IS_MASTER);
    if(!master.master)return key_sync_fail(deck,XZ_KEY_SYNC_NEED_MASTER);
    if(xz_key_get_applied_cents(1-deck,master.epoch,&master_shift)||!xz_key_playing(&master,master_shift,&target))
        return key_sync_fail(deck,XZ_KEY_SYNC_NO_MASTER_KEY);
    if(!xz_key_sync_cents(&source,&target,&cents)||xz_key_set_desired_cents(deck,source.epoch,cents))
        return key_sync_fail(deck,XZ_KEY_SYNC_RANGE);
    strip_error_until[deck]=0;
    __atomic_add_fetch(&xz_mods_key_proof_v1.requests,1,__ATOMIC_RELAXED);
    key_sync[deck].valid=1;key_sync[deck].epoch=source.epoch;key_sync[deck].cents=cents;model.deck[deck].key_synced=1;
    char after[48];xz_key_readout(&source,cents,after,sizeof(after));
    snprintf(ui.notice,sizeof(ui.notice),"D%d KEY %s / MASTER D%d%s",deck+1,after,2-deck,
        source.original.minor!=master.original.minor?" RELATIVE KEY":"");
    xz_log(ui.notice);return 1;
}

static int (*stock_master_tempo_key)(void *,const void *);
static int (*native_master_tempo_on)(void *,int);
static int master_tempo_key_hook(void *self,const void *input) {
    const unsigned char *player=self,*key=input;
    int deck=(int)player[0x26]-1,operation=key[0x0b]&15,consumed=0;
    if(deck<0||deck>1)return stock_master_tempo_key?stock_master_tempo_key(self,input):0;
    unsigned bit=1u<<deck;
    pthread_mutex_lock(&ui_mutex);
    if(operation==0&&!(master_tempo_down&bit)) {
        master_tempo_down|=bit;
        if(started&&key_available&&!player[0x34]&&key_sync[deck].valid&&native_master_tempo_on) {
            struct xz_key_snapshot current;
            uint32_t epoch=key_sync[deck].epoch;
            if(xz_key_native_snapshot((unsigned)deck,&current)&&current.epoch==epoch&&
               !xz_key_set_desired_cents(deck,epoch,0)) {
                key_sync[deck].valid=0;
                model.deck[deck].key_synced=key_strip[deck].synced=0;
                strip_error_until[deck]=0;
                master_tempo_owned|=bit;
                /* The native setter preserves its LED/settings and is a no-op if already ON. */
                native_master_tempo_on(self,1);
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
    xz_ram_status(deck,&s);
    struct xz_ui_ram *r=&model.deck[deck].ram;
    switch(s.state){
    case XZ_RAM_EMPTY:r->state=XZ_UI_RAM_EMPTY;break;
    case XZ_RAM_LOADING:r->state=XZ_UI_RAM_LOADING;break;
    case XZ_RAM_READY:r->state=XZ_UI_RAM_READY;break;
    case XZ_RAM_ERROR:r->state=XZ_UI_RAM_ERROR;break;
    case XZ_RAM_DETACHED:r->state=XZ_UI_RAM_DETACHED;break;
    default:r->state=XZ_UI_RAM_UNAVAILABLE;break;}
    r->copied=s.copied;r->bytes=s.bytes;
    s.message[sizeof(s.message)-1]=0;snprintf(r->message,sizeof(r->message),"%s",s.message);
    if(s.state==XZ_RAM_READY||s.state==XZ_RAM_DETACHED){
        struct xz_audio_status audio={0};xz_audio_get_status(deck,&audio);
        snprintf(r->message,sizeof(r->message),"ORIGINAL IN RAM / %s",audio.retained_stems_bytes?"PREPARED STEMS IN RAM":"STEMS STILL NEED USB");
    }
}

/* The worker copies asynchronously; these only start, stop or free it and report the next step. */
static void ram_action(const struct xz_ui_action *a) {
    int deck=a->deck;
    if(deck<0||deck>1)return;
    ram_refresh(deck);
    if(a->kind==XZ_UI_RAM_REQUEST){
        if(xz_ram_request(deck)==0)snprintf(ui.notice,sizeof(ui.notice),"DECK %d: COPYING ORIGINAL AUDIO TO RAM",deck+1);
        else{
            ram_refresh(deck);
            if(model.deck[deck].ram.state==XZ_UI_RAM_UNAVAILABLE)
                snprintf(ui.notice,sizeof(ui.notice),"RAM ENGINE NOT IN THIS LOADER / REBUILD THE USB WITH THE XZ MODS APP");
            else snprintf(ui.notice,sizeof(ui.notice),"DECK %d: RAM COPY DID NOT START / SEE STATUS ABOVE",deck+1);
        }
    }else if(a->kind==XZ_UI_RAM_CANCEL){
        snprintf(ui.notice,sizeof(ui.notice),xz_ram_cancel(deck)==0?"DECK %d: RAM COPY CANCELLED":"DECK %d: NO RAM COPY TO CANCEL",deck+1);
    }else if(model.deck[deck].ram.state!=XZ_UI_RAM_READY){
        snprintf(ui.notice,sizeof(ui.notice),"DECK %d: RELEASE NEEDS THE USB SOURCE CONNECTED",deck+1);
    }else{
        snprintf(ui.notice,sizeof(ui.notice),xz_ram_release(deck)==0?"DECK %d: RAM RELEASE REQUESTED":"DECK %d: RAM COPY STILL HELD / TRY AGAIN",deck+1);
    }
    ram_refresh(deck);
}

static void apply(void *context, const struct xz_ui_action *actions, size_t count) {
    (void)context;
    for (size_t i = 0; i < count; i++) {
        const struct xz_ui_action *a = &actions[i];
        if (xz_sdk_ui_kind(a->kind)) { sdk_apply(a); continue; }
        if (sdk_refuses_game(a)) continue;
        switch (a->kind) {
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
            model.metrics_enabled=!model.metrics_enabled;
            memset(&model.metrics,0,sizeof(model.metrics));
            if(xz_metrics_enable)xz_metrics_enable(model.metrics_enabled);
            break;
        case XZ_UI_METRICS_SETUP_OPEN:
            xz_ui_navigate(&ui,XZ_UI_METRICS_SETUP);ui.metrics_step=0;
            xz_native_touch_visible(&touch,1);__atomic_store_n(&visible,1,__ATOMIC_RELEASE);break;
        case XZ_UI_METRICS_CHECK:
            if(!xz_metrics_start||xz_metrics_start()){
                snprintf(ui.notice,sizeof(ui.notice),"SAMPLER MISSING: REBUILD USB LOADER WITH CURRENT XZ MODS");break;
            }
            model.metrics_enabled=1;if(xz_metrics_enable)xz_metrics_enable(1);
            if(ui.metrics_step<3){ui.metrics_step++;break;}
            if(xz_metrics_snapshot)xz_metrics_snapshot(&model.metrics);
            if(xz_metrics_fresh(&model.metrics,xz_game_milliseconds())&&(model.metrics.valid&3)==3){
                snprintf(ui.notice,sizeof(ui.notice),model.metrics.valid&4?"CPU / RAM / SOC SENSOR LIVE":"CPU / RAM LIVE; SOC SENSOR NOT EXPOSED");
                xz_native_touch_visible(&touch,0);__atomic_store_n(&visible,0,__ATOMIC_RELEASE);
            }else snprintf(ui.notice,sizeof(ui.notice),"WAIT ONE SECOND, THEN LIVE VERIFY AGAIN");
            break;
        case XZ_UI_LOCAL_STEM_SETUP_OPEN:
            xz_ui_navigate(&ui,XZ_UI_LOCAL_STEM_SETUP);ui.deck=a->deck>=0&&a->deck<2?a->deck:0;
            refresh_local();ui.stem_setup_step=!audio_available?0:(model.enabled&XZ_UI_STEM)?2:1;
            xz_native_touch_visible(&touch,1);__atomic_store_n(&visible,1,__ATOMIC_RELEASE);inline_layout_publish();break;
        case XZ_UI_LOCAL_STEM_SETUP_STEP: {
            int deck=ui.deck>=0&&ui.deck<2?ui.deck:0;
            if(model.deck[deck].pc_stems.source!=XZ_UI_SOURCE_LOCAL){xz_ui_navigate(&ui,XZ_UI_PC_STEMS);break;}
            refresh_local();
            if(!audio_available){snprintf(ui.notice,sizeof(ui.notice),"STEM ENGINE MISSING / UPDATE USB LOADER IN XZ MODS");break;}
            if(a->index==0){ui.stem_setup_step=(model.enabled&XZ_UI_STEM)?2:1;snprintf(ui.notice,sizeof(ui.notice),"%s",xz_ui_usb_next_step(xz_ui_usb_blocked(&model,deck)));}
            else if(a->index==1){requested_stems=1;model.enabled|=XZ_UI_STEM;xz_audio_set_enabled(1);levels(0);levels(1);ui.stem_setup_step=2;settings_queue();refresh_local();}
            /* Live verify means a prepared track is actually playing stems, not just an engine present. */
            else if((model.deck[deck].ready&XZ_UI_STEM)&&audio_status[deck].prepared&&audio_status[deck].state==XZ_AUDIO_EXPERIMENTAL_READY){xz_native_touch_visible(&touch,0);__atomic_store_n(&visible,0,__ATOMIC_RELEASE);inline_redraw_pending=1;}
            else snprintf(ui.notice,sizeof(ui.notice),"%s",model.deck[deck].stem_error&&*model.deck[deck].stem_error?
                model.deck[deck].stem_error:xz_ui_usb_next_step(xz_ui_usb_blocked(&model,deck)));
            inline_layout_publish();break;
        }
        case XZ_UI_UNAVAILABLE:
            if(a->index==XZ_UI_STEM){xz_ui_navigate(&ui,XZ_UI_LOCAL_STEM_SETUP);ui.stem_setup_step=0;xz_native_touch_visible(&touch,1);__atomic_store_n(&visible,1,__ATOMIC_RELEASE);inline_layout_publish();}
            if(a->index&(XZ_UI_KEY|XZ_UI_KEYSYNC))xz_ui_navigate(&ui,XZ_UI_KEY_SETUP);
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
            xz_native_touch_visible(&touch,1);__atomic_store_n(&visible,1,__ATOMIC_RELEASE);break;
        case XZ_UI_WAVE_RIDER_OPEN:
            if(xz_doom_running()){snprintf(ui.notice,sizeof(ui.notice),"EXIT DOOM BEFORE STARTING WAVE RIDER");break;}
            wave_rider_previous_takeover=model.fb_takeover;model.fb_takeover=0;
            __atomic_store_n(&fb_takeover_active,0,__ATOMIC_RELEASE);
            xz_game_nav_reset(&game_nav);xz_wave_rider_open(xz_game_milliseconds());
            xz_native_touch_visible(&touch,1);__atomic_store_n(&visible,1,__ATOMIC_RELEASE);
            break;
        case XZ_UI_ARCADE_OPEN:
            if(xz_doom_running()){snprintf(ui.notice,sizeof(ui.notice),"EXIT DOOM BEFORE STARTING BEAT ARCADE");break;}
            if(!arcade.initialized)xz_arcade_init(&arcade,xz_game_milliseconds());
            arcade.last_ms=xz_game_milliseconds();
            arcade_previous_takeover=model.fb_takeover;model.fb_takeover=0;
            __atomic_store_n(&fb_takeover_active,0,__ATOMIC_RELEASE);
            arcade_present=1;arcade_opening_touch=1;arcade.touch_down=1;xz_game_nav_reset(&game_nav);
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
            if(!model.update.ready || (ui.page!=XZ_UI_UPDATE_SETUP&&model.update.phase==XZ_UPDATE_ERROR)){xz_ui_navigate(&ui,XZ_UI_UPDATE_SETUP);snprintf(ui.notice,sizeof(ui.notice),"DETECT THE UPDATE SERVER IN SETUP");}
            else xz_update_request(0);
            break;
        case XZ_UI_UPDATE_INSTALL:
            xz_update_read(&model.update);
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
            ui.history_count=0;
            xz_native_skin_request_repaint();
            xz_native_touch_visible(&touch, 0);
            __atomic_store_n(&visible, 0, __ATOMIC_RELEASE);
            break;
        case XZ_UI_DECK:
        case XZ_UI_RAM_DECK:
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
        case XZ_UI_STEM_COLOR:
            if(a->index>=0&&a->index<3&&a->value>=0&&a->value<XZ_STEM_COLOR_CHOICES){
                model.stem_colors[a->index]=(int)a->value;wave_configure();
            }
            break;
        case XZ_UI_STEM_COLORS_RESET:
            memset(model.stem_colors,0,sizeof(model.stem_colors));wave_configure();break;
        case XZ_UI_SET_THEME:
            if (a->index >= 0 && a->index < XZ_THEME_COUNT) {model.theme = a->index;xz_native_skin_request(a->index);wave_configure();}
            break;
        case XZ_UI_WAVE_MODE:
            if (a->index >= 0 && a->index < XZ_WAVE_MODE_COUNT) {
                model.wave_mode = a->index; wave_configure();
                snprintf(ui.notice, sizeof(ui.notice), "WAVEFORM: %s", a->index == 2 ? "STEMS" : a->index == 1 ? "3-BAND" : "STOCK");
            }
            break;
        case XZ_UI_JUMP_ENABLE:
            model.jump.enabled=a->value!=0;
            jump_state.page[0]=jump_state.page[1]=0;
            break;
        case XZ_UI_JUMP_SHIFT: model.jump.shift_page=a->value!=0; break;
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
        case XZ_UI_SHIFT_PAGES: model.shift_pages = a->value != 0; break;
        case XZ_UI_PAD_FEEDBACK: model.pad_feedback = a->value != 0; break;
        case XZ_UI_SHIFT_KEYSYNC: model.shift_keysync = a->value != 0; break;
        case XZ_UI_KEY_CHECK:
            ui.notice[0]=0;
            if(key_available&&xz_key_native_snapshot(0,&key_snapshot[0])&&xz_key_native_snapshot(1,&key_snapshot[1])&&
                key_snapshot[0].master!=key_snapshot[1].master){
                model.shift_keysync=1;
                snprintf(ui.notice,sizeof(ui.notice),"TWO KEYS + MASTER VERIFIED / SHIFT + SYNC ENABLED");
            }else snprintf(ui.notice,sizeof(ui.notice),"LOAD ANALYZED USB TRACKS + CHOOSE MASTER, THEN RETRY");
            break;
        case XZ_UI_KEY_SYNC: sync_key(a->deck); break;
        case XZ_UI_BROWSE_COLUMN:
            if (!xz_key_browser_column_choose(model.browse_columns, a->index/4, a->index%4))
                snprintf(ui.notice, sizeof(ui.notice), "BROWSE COLUMNS UNCHANGED");
            break;
        case XZ_UI_BROWSE_KEY_PREFIX: model.browse_key_prefix = a->value != 0; break;
        case XZ_UI_BROWSE_TRAFFIC: model.browse_traffic = a->value != 0; break;
        case XZ_UI_KEY_FORMAT: if (a->index >= 0 && a->index <= 2) model.key_format = a->index; break;
        case XZ_UI_RAM_DEBUG:
            model.ram_debug=a->value!=0;
            __atomic_store_n(&xz_mods_ram_debug_v1.enabled,model.ram_debug,__ATOMIC_RELEASE);
            xz_native_skin_request_repaint();
            break;
        case XZ_UI_REKORDBOX_KEY:
            model.rekordbox_menu = a->value != 0;
            snprintf(ui.notice, sizeof(ui.notice), "%s", model.rekordbox_menu ?
                "REKORDBOX KEY OPENS MODS" : "REKORDBOX KEY: STOCK / OPEN MODS FROM THE SCREEN");
            break;
        case XZ_UI_KEY_FEEDBACK:
            model.key_feedback = a->value != 0;
            __atomic_store_n(&xz_mods_key_feedback_proof_v1.enabled,model.key_feedback,__ATOMIC_RELEASE);
            break;
        case XZ_UI_KEY_SHIFT:
            if (key_available && a->deck >= 0 && a->deck < 2) {
                struct xz_key_snapshot snapshot;int current;
                if (xz_key_native_snapshot((unsigned)a->deck,&snapshot)&&
                    xz_key_get_desired_cents(a->deck,snapshot.epoch,&current) == 0) {
                    int target = a->index == 0 ? 0 : current + a->index*100;
                    if (xz_key_set_desired_cents(a->deck,snapshot.epoch,target) != 0)
                        snprintf(ui.notice, sizeof(ui.notice), "KEY RANGE IS -12 TO +12 SEMITONES");
                    else { key_sync[a->deck].valid = 0; model.deck[a->deck].key_synced = 0; }
                }
            }
            break;
        case XZ_UI_TAKEOVER_TOGGLE:
            model.fb_takeover = !model.fb_takeover;
            __atomic_store_n(&fb_takeover_active, model.fb_takeover, __ATOMIC_RELEASE);
            snprintf(ui.notice, sizeof(ui.notice), "VJ.TOOLS VIEW %s", model.fb_takeover ? "ENABLED" : "DISABLED");
            break;
        case XZ_UI_TAKEOVER_ASSIGN:
            if (a->index >= 0 && a->index <= 2) {
                model.takeover_assign = a->index;
                snprintf(ui.notice, sizeof(ui.notice), "TAKEOVER KEY: %s",
                    model.takeover_assign == 0 ? "LINK" : (model.takeover_assign == 1 ? "REKORDBOX" : "ONSCREEN"));
            }
            break;
        default:
            break; /* Unconnected capabilities remain disabled by the model. */
        }
        if (a->kind == XZ_UI_METRICS_TOGGLE || a->kind == XZ_UI_METRICS_CHECK || a->kind == XZ_UI_STEM_ORDER || a->kind == XZ_UI_STEM_COLOR || a->kind == XZ_UI_STEM_COLORS_RESET || a->kind == XZ_UI_STEM_PAD_CONFIG || a->kind == XZ_UI_STEMS_OVERLAY || a->kind == XZ_UI_STEM_BANK || a->kind == XZ_UI_JUMP_STEP || a->kind == XZ_UI_JUMP_ENABLE || a->kind == XZ_UI_JUMP_SHIFT || a->kind == XZ_UI_JUMP_SIZE || a->kind == XZ_UI_ENABLE || a->kind == XZ_UI_SET_THEME || a->kind == XZ_UI_WAVE_MODE ||
            a->kind == XZ_UI_STEM_PAGE || a->kind == XZ_UI_SHIFT_PAGES ||
            a->kind == XZ_UI_PAD_FEEDBACK || a->kind == XZ_UI_SHIFT_KEYSYNC || a->kind == XZ_UI_KEY_CHECK ||
            a->kind == XZ_UI_TAKEOVER_TOGGLE || a->kind == XZ_UI_TAKEOVER_ASSIGN ||
            a->kind == XZ_UI_BROWSE_COLUMN || a->kind == XZ_UI_BROWSE_KEY_PREFIX || a->kind == XZ_UI_BROWSE_TRAFFIC ||
            a->kind == XZ_UI_KEY_FORMAT || a->kind == XZ_UI_KEY_FEEDBACK || a->kind == XZ_UI_RAM_DEBUG ||
            a->kind == XZ_UI_REKORDBOX_KEY) settings_queue();
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
/* The shell may own the screen only while MODS, a game, Doom and the VJ takeover leave it free. */
static int shell_eligible_locked(void){
    return !touch.visible&&game_locked()==GAME_NONE&&!xz_doom_running()&&!__atomic_load_n(&fb_takeover_active,__ATOMIC_ACQUIRE);
}
static int shell_owns_locked(void){return xz_shell_runtime_owns&&shell_eligible_locked()&&xz_shell_runtime_owns();}
static void shell_frame_input_locked(struct xz_shell_runtime_frame_input *in){
    memset(in,0,sizeof(*in));in->eligible=shell_eligible_locked();in->theme=model.theme;
    in->stems_known=1;in->stems_on=model.stems_overlay!=XZ_STEM_LAYOUT_HIDDEN;
    for(int d=0;d<2;d++){
        /* 3-stem local playback only: with four stems or PC stems the bits mean something else. */
        in->stem_muted_known[d]=(model.deck[d].ready&XZ_UI_STEM)&&model.deck[d].pc_stems.source==XZ_UI_SOURCE_LOCAL&&model.deck[d].stem_count!=4;
        in->stem_muted[d]=model.deck[d].muted;
    }
    in->stem_rgb_known=1;for(int i=0;i<3;i++)in->stem_rgb[i]=xz_ui_model_stem_color(&model,i);
    for(int deck=0;deck<2;deck++){
        if(!key_valid[deck])continue;
        struct xz_shell_runtime_key *k=&in->key[deck];int other=1-deck,cents=0,other_cents=0;
        int format=model.key_format==1?XZ_KEY_CLASSIC:model.key_format==2?XZ_KEY_CAMELOT:key_snapshot[deck].format;
        xz_music_key_format(key_snapshot[deck].original,format,k->text);k->valid=1;
        if(key_available)xz_key_get_applied_cents(deck,key_snapshot[deck].epoch,&cents);
        k->shift=(int)round(cents*.01);
        struct xz_playing_key mine,master;
        if(key_valid[other]&&key_snapshot[other].master&&!key_snapshot[deck].master){
            if(key_available)xz_key_get_applied_cents(other,key_snapshot[other].epoch,&other_cents);
            k->matches_master=xz_key_playing(&key_snapshot[deck],cents,&mine)&&
                xz_key_playing(&key_snapshot[other],other_cents,&master)&&xz_key_match(mine,master)==XZ_KEY_COMPATIBLE;
        }
    }
}
/* The shell's MODS and STEMS header buttons, run on the input thread under ui_mutex exactly as the
   stock-mode badge (native_touch.c: visible=1) and STEMS button (XZ_UI_STEMS_OVERLAY) do. The rest of
   an opening contact stays with the shell's touch owner, so the panel never sees it, as the badge's
   opening_contact capture does. */
static void shell_ui_requests_locked(void){
    unsigned requests=xz_shell_runtime_take_ui_requests?xz_shell_runtime_take_ui_requests():0;
    if(requests&XZ_SHELL_UI_TOGGLE_STEMS){
        struct xz_ui_action act={.kind=XZ_UI_STEMS_OVERLAY};apply(NULL,&act,1);
        /* The layout changes as on the stock button, but rows only appear once stems are on. */
        if(!requested_stems&&!pc_present&&xz_shell_runtime_notice)xz_shell_runtime_notice("STEM ROWS SHOW ONCE STEMS ARE ON IN MODS");
    }
    if((requests&XZ_SHELL_UI_OPEN_MODS)&&touch.visible&&xz_shell_runtime_notice)xz_shell_runtime_notice("THE MODS PANEL IS ALREADY OPEN");
    if((requests&XZ_SHELL_UI_OPEN_MODS)&&!touch.visible){
        touch.visible=1;
        if(sdk_session.active&&ui.page!=XZ_UI_APPS_SETUP)xz_ui_navigate(&ui,XZ_UI_APPS);
        __atomic_store_n(&visible,1,__ATOMIC_RELEASE);
        inline_layout_publish();
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
static int menu_back_contact;
static void menu_back_key_hook(void *self,const void *input){
    uint16_t key=0;unsigned operation=0;
    if(input){memcpy(&key,(const unsigned char *)input+8,2);operation=((const unsigned char *)input)[11]&15;}
    int consumed=0;
    /* Every key passes here (jog included), so only keys in the shell's table take the lock. */
    unsigned channel=input?((const unsigned char *)input)[10]:0;
    if(input&&xz_shell_runtime_key&&xz_shrt_key_lookup&&xz_shrt_key_lookup(key,channel)&&__atomic_load_n(&started,__ATOMIC_ACQUIRE)){
        pthread_mutex_lock(&ui_mutex);
        consumed=xz_shell_runtime_key(shell_eligible_locked(),model.theme,key,channel,operation);
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
static int menu_key_toggle(void){
    if(!__atomic_load_n(&started,__ATOMIC_ACQUIRE))return 0;
    pthread_mutex_lock(&ui_mutex);
    int owned=model.rekordbox_menu;
    enum game game=game_locked();
    /* Doom exits on its own; MODS opens on the next press. */
    if(owned&&!touch.visible&&game==GAME_NONE&&xz_doom_nav(XZ_GAME_NAV_QUIT)){}
    else if(owned){
        if(game!=GAME_NONE){
            game_to_menu_locked(game);refresh_local();sdk_refresh();
            xz_native_touch_visible(&touch,1);__atomic_store_n(&visible,1,__ATOMIC_RELEASE);
            inline_layout_publish();
        }
        else if(touch.visible){struct xz_ui_action close={.kind=XZ_UI_CLOSE};apply(NULL,&close,1);}
        else{
            refresh_local();sdk_refresh();
            xz_native_touch_visible(&touch,1);__atomic_store_n(&visible,1,__ATOMIC_RELEASE);
            if(sdk_session.active&&ui.page!=XZ_UI_APPS_SETUP)xz_ui_navigate(&ui,XZ_UI_APPS);
            inline_layout_publish();
        }
        xz_native_skin_request_repaint();
    }
    pthread_mutex_unlock(&ui_mutex);
    return owned;
}
static int (*stock_encoder_rotate)(int);
static int encoder_rotate_hook(int delta) {
    if(__atomic_load_n(&started,__ATOMIC_ACQUIRE)){
        pthread_mutex_lock(&ui_mutex);
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
    if(__atomic_load_n(&started,__ATOMIC_ACQUIRE)){
        pthread_mutex_lock(&ui_mutex);
        int owned=menu_owns_keys_locked(),shell=0;
        if(owned){
            struct xz_ui_action actions[XZ_UI_ACTIONS];
            size_t n=xz_ui_encoder_push(&ui,&model,actions);
            if(n)apply(NULL,actions,n);
            inline_layout_publish();
        }
        else if(game_locked()!=GAME_NONE){owned=1;game_push_locked(game_locked());}
        else if(!touch.visible&&arg0&&xz_shell_runtime_encoder_push&&shell_eligible_locked()){
            /* pushKey entry {flags, 0, state, handler}: the shell reads the press and hold phases from it. */
            uint32_t entry[3];memcpy(entry,arg0,sizeof(entry));
            shell=xz_shell_runtime_encoder_push(1,model.theme,entry[0],entry[2]);
            if(!shell)owned=xz_doom_nav(XZ_GAME_NAV_ENTER);
        }
        else if(!touch.visible)owned=xz_doom_nav(XZ_GAME_NAV_ENTER);
        if(shell)shell_ui_requests_locked();
        pthread_mutex_unlock(&ui_mutex);
        if(shell)return 1;
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
    model.settings_flash=settings_flash_at&&(uint32_t)(xz_game_milliseconds()-settings_flash_at)<3000u;
    model.doom_ready=xz_doom_ready();
    xz_doom_browser_read(ui.wad_offset,&model.doom);ui.wad_offset=model.doom.offset;
    xz_update_read(&model.update);
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
    for (int deck = 0; deck < 2; deck++) {
        ram_refresh(deck);
        if(model.deck[deck].pc_stems.source!=XZ_UI_SOURCE_LOCAL){key_valid[deck]=0;continue;}
        model.deck[deck].ready = XZ_UI_GATE | XZ_UI_SMART | XZ_UI_THEME;
        model.deck[deck].usb_reason = XZ_UI_USB_WHY_ENGINE;
        key_valid[deck]=xz_key_native_snapshot((unsigned)deck,&key_snapshot[deck]);
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
        if(key_sync[deck].valid&&(!valid||key_snapshot[deck].epoch!=key_sync[deck].epoch||cents!=key_sync[deck].cents))key_sync[deck].valid=0;
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
    for(int deck=0;deck<2;deck++)if(key_valid[deck]&&key_valid[1-deck]){
        int shift[2]={0,0};struct xz_playing_key playing[2];
        xz_key_get_applied_cents(0,key_snapshot[0].epoch,&shift[0]);
        xz_key_get_applied_cents(1,key_snapshot[1].epoch,&shift[1]);
        if(key_snapshot[1-deck].master&&!key_snapshot[deck].master&&key_available)
            model.deck[deck].ready|=XZ_UI_KEYSYNC;
        if(xz_key_playing(&key_snapshot[deck],shift[deck],&playing[deck])&&
            xz_key_playing(&key_snapshot[1-deck],shift[1-deck],&playing[1-deck])){
            enum xz_key_match match=xz_key_match(playing[deck],playing[1-deck]);
            model.deck[deck].key_color=key_snapshot[deck].master?0x52d794:
                key_snapshot[1-deck].master?(match==XZ_KEY_COMPATIBLE?0x52d794:match==XZ_KEY_DETUNED?0xffbd59:0xa8b2bf):0xa8b2bf;
        }
    }
    for(int deck=0;deck<2;deck++)key_strip[deck].sync_master=key_strip[deck].valid&&key_strip[1-deck].valid&&
        key_snapshot[deck].master&&!key_snapshot[1-deck].master&&key_strip[1-deck].synced;
    struct xz_playing_key master_key;int have_master=0,master_format=XZ_KEY_CLASSIC;
    for(int deck=0;deck<2;deck++)if(key_valid[deck]&&key_snapshot[deck].master){
        int cents=0;xz_key_get_applied_cents(deck,key_snapshot[deck].epoch,&cents);
        have_master=xz_key_playing(&key_snapshot[deck],cents,&master_key);master_format=key_snapshot[deck].format;
    }
    xz_key_browser_master(have_master?&master_key:NULL,master_format);
    xz_key_traffic_master(have_master?&master_key:NULL);
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
static void refresh(void){sdk_refresh();pc_refresh_locked();refresh_local();ram_debug_refresh();if(model.metrics_enabled&&xz_metrics_snapshot)xz_metrics_snapshot(&model.metrics);}

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
                int newly_enabled=!model.shift_keysync;
                model.shift_keysync=1;
                int done = sync_key(event->deck);
                if(newly_enabled&&done)settings_queue();
                /* A failure is shown inline on the deck strip. Only a missing key engine,
                   which no gesture can fix, opens its setup page. */
                if (!done && !key_available) { xz_native_touch_visible(&touch,1); __atomic_store_n(&visible,1,__ATOMIC_RELEASE); }
            }
        }
        pthread_mutex_unlock(&ui_mutex); return owned;
    }
    *trace_flags = 1u | (touch.visible ? 2u : 0) | (ui.page == XZ_UI_STEMS ? 4u : 0) |
        (ui.deck == event->deck ? 8u : 0) | (model.enabled & XZ_UI_STEM ? 16u : 0) |
        (audio_available ? 32u : 0) | (event->hotcue_mode ? 64u : 0);
    int toggle,beats=0;
    int active=stem_controls_active(event->deck);
    int jump_mode=model.jump.enabled&&model.jump.shift_page&&event->mode_button==3;
    if(jump_mode||(model.jump.enabled&&event->pad_page==3&&event->pad>=0))active=0;
    /* Both handlers see releases, including after settings change while held. */
    int consumed=xz_stem_control_event(&pads,event,active,model.stem_page,model.shift_pages,&toggle);
    int stem_mode_press=consumed&&event->mode_button>=0&&event->operation==0;
    int jump_consumed=stem_mode_press?0:xz_jump_event(&jump_state,&model.jump,event,&beats);
    if(jump_consumed){
        *trace_flags|=16384u;
        if(beats&&!xz_runtime_beat_jump(event->deck,beats))
            snprintf(ui.notice,sizeof(ui.notice),"JUMP UNAVAILABLE: TRACK EDGE, BUFFER OR DECK STATE");
    }
    if (consumed) *trace_flags |= 128u;
    if (toggle >= 0) *trace_flags |= 256u;
    if (toggle >= 0) {
        int deck = event->deck;
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
    if(model.jump.enabled&&model.jump.shift_page&&jump_state.page[deck]==1&&deck_page[deck]==3){
        *rgb=xz_jump_page2_color(pad);*lit=1;
        pthread_mutex_unlock(&ui_mutex);return XZ_LED_HUE;
    }
    int slot=pad-(model.stem_bank==1?4:0);
    int active = !(model.jump.enabled&&deck_page[deck]==3) && slot>=0&&slot<4&&stem_controls_active(deck) && model.pad_feedback && deck_page[deck] == model.stem_page;
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
        if(status->y<24&&((status->x>=XZ_METRICS_X&&status->x<XZ_METRICS_X+XZ_METRICS_W)||
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
static void touch_hook(void *self, const struct xz_touch_status *status, const void *mode) {
    if (!__atomic_load_n(&started, __ATOMIC_ACQUIRE)) { stock_touch(self, status, mode); return; }
    pthread_mutex_lock(&ui_mutex);
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
    if(shell_touch_locked(self,status,mode)){pthread_mutex_unlock(&ui_mutex);return;}
    sdk_refresh();pc_refresh_locked();
    if(touch.visible||model.deck[0].pc_stems.source!=XZ_UI_SOURCE_PC||model.deck[1].pc_stems.source!=XZ_UI_SOURCE_PC)refresh_local();
    touch.vj_btn_w=(model.fb_takeover||xz_native_play_visible())?112:0;
    int previous_down=((const unsigned char *)self)[4]!=0;
    if(shortcut_touch(self,status,mode,previous_down)){pthread_mutex_unlock(&ui_mutex);return;}
    if(browse_head_touch(status,previous_down)){pthread_mutex_unlock(&ui_mutex);return;}
    if(!touch.visible&&!previous_down&&status->down&&status->x<115&&status->y>=24){
        if(status->y>=18&&status->y<159)ui.deck=inline_ui.deck=0;
        else if(status->y>=161&&status->y<302)ui.deck=inline_ui.deck=1;
    }
    int active=!touch.visible&&!(model.fb_takeover&&model.connection.connected)&&xz_native_inline_active();
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
    int pressed_deck=xz_wave_layout_control_at(&geometry,(int)status->y-18);
    if(active&&!inline_contact&&!previous_down&&status->down&&status->x>=131&&status->x<667&&pressed_deck>=0){
        inline_contact=1;inline_contact_deck=pressed_deck;inline_cancelled=0;inline_gesture_layout=model.stems_overlay;
    }
    int owned,pc_redraw=inline_contact&&model.deck[inline_contact_deck].pc_stems.source==XZ_UI_SOURCE_PC;
    if(inline_contact){
        struct xz_ui_action actions[XZ_UI_ACTIONS];
        struct xz_wave_layout captured;xz_wave_layout_get(inline_gesture_layout,&captured);
        if(model.stems_overlay!=inline_gesture_layout&&!inline_cancelled){
            xz_ui_cancel(&inline_ui,actions);inline_cancelled=1;
        }
        int row_y=(int)status->y-18-(int)captured.control_y[inline_contact_deck];
        if(row_y<0)row_y=0;else if(row_y>=(int)captured.control_height)row_y=(int)captured.control_height-1;
        size_t n=inline_cancelled?0:xz_ui_inline_touch(&inline_ui,&model,536,(int)captured.control_height*2,(int)status->x-131,row_y+inline_contact_deck*(int)captured.control_height,!!status->down,actions);
        if(n)apply(NULL,actions,n);
        if(!status->down){inline_contact=0;inline_cancelled=0;}
        owned=1;
    }else{
        if(!previous_down&&status->down){
            inline_wave_deck=xz_wave_layout_lane_at(&geometry,(int)status->y-18);
            inline_gesture_layout=model.stems_overlay;
            inline_wave_contact=active&&status->x>=131&&status->x<667&&inline_wave_deck>=0;
        }
        struct xz_touch_status mapped=*status;
        if(inline_wave_contact&&(status->down||status->x||status->y)){
            struct xz_wave_layout captured;xz_wave_layout_get(inline_gesture_layout,&captured);
            int row=(int)mapped.y-18-(int)captured.lane_y[inline_wave_deck];
            if(row<0)row=0;else if(row>=(int)captured.lane_height)row=(int)captured.lane_height-1;
            mapped.y=18+xz_wave_layout_source_row(&captured,(unsigned)row+captured.lane_y[inline_wave_deck]);
        }
        int was_visible=touch.visible;
        owned=xz_native_touch_dispatch(&touch,self,&mapped,mode,stock_touch);
        /* Keep SDK app-session navigation; table editing stays in the browse header. */
        if(!was_visible&&touch.visible&&sdk_session.active&&ui.page!=XZ_UI_APPS_SETUP)
            xz_ui_navigate(&ui,XZ_UI_APPS);
        if(!status->down)inline_wave_contact=0;
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
    if(pc_redraw)xz_native_skin_request_repaint();
}

/* The receiver treats a shell-owned screen exactly like the open panel. */
__attribute__((visibility("default"))) int xz_mods_visible_v1(void) {
    return __atomic_load_n(&visible, __ATOMIC_ACQUIRE)||(xz_shell_runtime_owns&&xz_shell_runtime_owns());
}
/* Lock-free: the next xz_mods_render_v1 that draws writes every pixel (the shell owns the screen), so
   the receiver can skip copying its base frame under it. */
__attribute__((visibility("default"))) int xz_mods_full_frame_v1(void) {
    return !__atomic_load_n(&visible, __ATOMIC_ACQUIRE)&&!__atomic_load_n(&fb_takeover_active,__ATOMIC_ACQUIRE)&&
        xz_shell_runtime_owns&&xz_shell_runtime_owns();
}

__attribute__((visibility("default"))) int xz_mods_native_touch_v1(void) {
    return __atomic_load_n(&started, __ATOMIC_ACQUIRE);
}

__attribute__((visibility("default"))) int xz_mods_takeover_v1(void) {
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
    xz_ui_render_native_buttons(pixels,stride,model.theme,model.stems_overlay,
        model.connection.enabled&&model.connection.connected&&(model.fb_takeover||play),model.fb_takeover);
    xz_metrics_draw(pixels,stride,model.theme,model.metrics_enabled,&model.metrics,xz_game_milliseconds());
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
    model.jump = saved->jump; model.stems_overlay = saved->stems_overlay; model.wave_mode = saved->wave_mode;
    requested_stems = saved->stems; model.theme = saved->theme; model.stem_page = saved->stem_page;
    model.shift_pages = saved->shift_pages; model.pad_feedback = saved->pad_feedback;
    model.shift_keysync = saved->shift_keysync; model.takeover_assign = saved->takeover_assign;
    model.rekordbox_menu = saved->rekordbox_menu;
    model.stem_bank = saved->stem_bank; pads.bank = saved->stem_bank;
    model.stem_order = (unsigned)saved->stem_order < XZ_STEM_ORDER_COUNT ? saved->stem_order : 0; pads.order = model.stem_order;
    browse_settings(saved);
    memcpy(model.stem_colors,saved->stem_colors,sizeof(model.stem_colors));
    xz_runtime_set_cues(saved->gate, saved->smart);
    model.enabled = xz_runtime_cue_flags();
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

__attribute__((visibility("default"))) int xz_mods_render_v1(uint16_t *pixels, uint32_t width,
        uint32_t height, uint32_t stride, const struct xz_vj_connection_v1 *connection) {
    if (!__atomic_load_n(&started, __ATOMIC_ACQUIRE) || !pixels || width != 800 || height != 480 || stride < 800 || stride > 16384)
        return 0;
    if (pthread_mutex_trylock(&ui_mutex) != 0) {
        __atomic_add_fetch(&xz_mods_key_present_proof_v1.busy,1,__ATOMIC_RELAXED);
        if(!__atomic_load_n(&visible,__ATOMIC_ACQUIRE)&&
           !__atomic_load_n(&fb_takeover_active,__ATOMIC_ACQUIRE)&&!(xz_shell_runtime_owns&&xz_shell_runtime_owns())) {
            int drawn=0;
            uint32_t scene;
            if(badge_frame_cache.valid&&!xz_read_memory(0x3c6fc7c,&scene,4)&&scene==badge_frame_cache.scene) {
                if(badge_frame_cache.shortcut)
                    xz_ui_render_shortcut_wave(pixels,stride,badge_frame_cache.selected,badge_frame_cache.colors);
                xz_ui_render_native_buttons(pixels,stride,badge_frame_cache.theme,badge_frame_cache.layout,
                    badge_frame_cache.vj,badge_frame_cache.takeover);
                xz_metrics_draw(pixels,stride,badge_frame_cache.theme,badge_frame_cache.metrics_enabled,&badge_frame_cache.metrics,xz_game_milliseconds());
                if(__atomic_load_n(&xz_mods_ram_debug_v1.enabled,__ATOMIC_ACQUIRE))
                    xz_ui_render_ram_debug(pixels,stride,badge_frame_cache.theme,badge_frame_cache.ram_debug&&!badge_frame_cache.metrics_enabled,badge_frame_cache.ram_valid,badge_frame_cache.ram_free_mib);
                drawn=1;
            }
            if(__atomic_load_n(&xz_mods_key_feedback_proof_v1.enabled,__ATOMIC_ACQUIRE))
                drawn|=xz_key_frame_cache_draw(&key_frame_cache,pixels,stride);
            if(drawn){__atomic_add_fetch(&xz_mods_key_present_proof_v1.cached,1,__ATOMIC_RELAXED);return 1;}
        }
        return 0;
    }
    /* Between shell frames there is nothing to refresh or draw; the receiver keeps the last frame. */
    int shell_path=xz_shell_runtime_frame&&shell_owns_locked();
    if(shell_path&&xz_shell_runtime_idle&&xz_shell_runtime_idle()){pthread_mutex_unlock(&ui_mutex);return 0;}
    /* The shell draws from its own model and reads only the key snapshots from this one, so while it
       owns the screen the UI model refreshes at 10 Hz instead of on every 60 Hz frame. */
    {
        uint32_t now=xz_game_milliseconds();
        if(!shell_path||!shell_ui_refreshed_ms||(uint32_t)(now-shell_ui_refreshed_ms)>=100u){refresh();shell_ui_refreshed_ms=now?now:1;}
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
        pthread_mutex_unlock(&ui_mutex);
        int drawn=xz_shell_runtime_frame(pixels,stride,&in);
        if(drawn)return drawn>0;
        if(pthread_mutex_trylock(&ui_mutex)!=0)return 0;
    }
    int result,shell=0;
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
    else if (touch.visible) result = xz_ui_render(&ui, &model, pixels, (size_t)stride * height, stride);
    else if (xz_shell_runtime_frame&&(shell=xz_shell_runtime_frame(pixels,stride,&shell_input))!=0) result = shell>0;
    else {
        xz_key_frame_cache_store(&key_frame_cache,model.theme,model.key_feedback,model.fb_takeover,key_strip);
        badge_frame_cache.valid=!model.fb_takeover&&!xz_read_memory(0x3c6fc7c,&badge_frame_cache.scene,4);
        badge_frame_cache.ram_debug=model.ram_debug;badge_frame_cache.ram_valid=model.ram_debug_valid;
        badge_frame_cache.ram_free_mib=model.ram_free_mib;
        badge_frame_cache.metrics_enabled=model.metrics_enabled;badge_frame_cache.metrics=model.metrics;
        badge_frame_cache.theme=model.theme;badge_frame_cache.layout=model.stems_overlay;
        badge_frame_cache.takeover=model.fb_takeover;
        badge_frame_cache.vj=model.connection.enabled&&model.connection.connected&&xz_native_play_visible();
        badge_frame_cache.shortcut=shortcut_visible();
        badge_frame_cache.selected=model.wave_mode==2?3:model.wave_mode==1?2:shortcut_stock_rgb();
        for(int i=0;i<3;i++)badge_frame_cache.colors[i]=xz_ui_model_stem_color(&model,i);
        badge(pixels,stride);result=1;
    }
    if(game_locked()!=GAME_NONE&&result){
        xz_game_nav_frame(&game_nav,&game_frame);
        xz_game_nav_draw(&game_nav,pixels,stride,(int)width,(int)height);
    }
    pthread_mutex_unlock(&ui_mutex);
    return result;
}

int xz_ui_runtime_start(int audio_ready, int key_ready, int stems_enabled) {
    static const unsigned char guard[8] = {0xf0,0x45,0x2d,0xe9,0x02,0x70,0xa0,0xe1};
    static const unsigned char link_guard[8] = {0x70,0x40,0x2d,0xe9,0x00,0x50,0xa0,0xe1};
    static const unsigned char rekordbox_guard[8] = {0x38,0x40,0x2d,0xe9,0x00,0x50,0xa0,0xe1};
    if (started) return 0;
    settings_usb[0] = 0; settings_pending = settings_created = 0; settings_flash_at = 0;
    memset(&settings_profile,0,sizeof(settings_profile)); settings_slot=-1;
    __atomic_store_n(&profile_slot,~0u,__ATOMIC_RELAXED);
    forward_touch = (void (*)(int,int,int))dlsym(RTLD_DEFAULT, "xz_vj_touch_v1");
    if (!forward_touch) { xz_log("UI unavailable: receiver does not expose the optional display bridge"); return -1; }
    audio_available = audio_ready;
    key_available = key_ready;
    if(xz_key_browser_start())xz_log("Key browser index worker unavailable");
    if(xz_key_traffic_start())xz_log("Effective-key traffic-light adapter unavailable: firmware guard rejected");
    memset(&model, 0, sizeof(model));
    settings_owner_valid=settings_target_empty=0;
    model.jump=xz_jump_defaults();
    memset(&jump_state,0,sizeof(jump_state));
    model.pad_feedback = 1;
    model.stems_overlay = 1;
    model.wave_mode = XZ_WAVE_THREE_BAND;
    model.fb_takeover = 0;
    model.takeover_assign = XZ_TAKEOVER_LINK;
    model.rekordbox_menu = 1;
    __atomic_store_n(&fb_takeover_active, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&network_enabled, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&network_connected, 0, __ATOMIC_RELEASE);
    requested_stems = !!stems_enabled;
    model.settings_status = xz_settings_internal_save ? "SETTINGS SAVE ON THIS XZ" : "SETTINGS NOT SAVED: START FROM USB";
    { struct xz_settings defaults; xz_settings_default(&defaults); browse_settings(&defaults); }
    memset(key_sync, 0, sizeof(key_sync));
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
    const char *force_stems = getenv("XZ_MODS_STEMS_FORCE");
    const char *ram_settings = getenv("XZ_MODS_SETTINGS_READONLY");
    settings_readonly = ram_settings && !strcmp(ram_settings,"1");
    if (settings_readonly) model.settings_status = "CHANGES LAST UNTIL REBOOT";
    const char *forced_theme=getenv("XZ_MODS_THEME");
    if(forced_theme&&forced_theme[0]>='0'&&forced_theme[0]<='9'&&
       (!forced_theme[1]||(forced_theme[1]>='0'&&forced_theme[1]<='9'&&!forced_theme[2]))){
        int chosen=forced_theme[0]-'0';if(forced_theme[1])chosen=chosen*10+forced_theme[1]-'0';
        if(chosen<XZ_THEME_COUNT)model.theme=chosen;
    }
    const char *forced_wave=getenv("XZ_MODS_WAVE_MODE");
    if(forced_wave&&forced_wave[0]>='0'&&forced_wave[0]<'0'+XZ_WAVE_MODE_COUNT&&!forced_wave[1])model.wave_mode=forced_wave[0]-'0';
    wave_configure();
    const char *native_view = getenv("XZ_MODS_NATIVE_VIEW");
    if (native_view && !strcmp(native_view,"1")) { model.fb_takeover=0; __atomic_store_n(&fb_takeover_active,0,__ATOMIC_RELEASE); }
    if (audio_ready && force_stems && !strcmp(force_stems,"1")) {
        requested_stems=1;model.enabled|=XZ_UI_STEM;xz_audio_set_enabled(1);
    }
    for (int i = 0; i < 4; i++) {
        model.deck[i].groove_active = model.deck[i].sample_active = -1;
        for(unsigned r=0;r<4;r++)model.deck[i].levels[r]=1;
        model.deck[i].stem_count=pads.count[i]=3;
        model.deck[i].sample_volume = 1;
        model.deck[i].track = i < 2 ? "NO TRACK CAPTURED" : "EXTERNAL USB AUDIO";
        model.deck[i].status = i < 2 ? "NATIVE ADAPTER EXPERIMENTAL" : "CONTROL IN REKORDBOX OR SERATO";
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
    if(xz_shell_runtime_start&&xz_shell_runtime_start())xz_log("FLAGSHIP shell unavailable; the restyled stock screen stays");
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
    pthread_mutex_lock(&ui_mutex);
    arcade_present=0;memset(&arcade_input,0,sizeof(arcade_input));
    xz_wave_rider_close();
    xz_rhythm_lab_close();
    int join = settings_created; settings_created=0;
    settings_running = 0; pthread_cond_signal(&settings_changed);
    int dj = model.dj!=NULL; model.dj=NULL; ui.profile_open=0;
    pthread_mutex_unlock(&ui_mutex);
    if (join) pthread_join(settings_thread,NULL);
    if (dj && xz_dj_runtime_stop) xz_dj_runtime_stop();
}
