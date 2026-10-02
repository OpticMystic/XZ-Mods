#define _GNU_SOURCE
#include "ui_runtime.h"
#include "runtime.h"
#include "audio/runtime.h"
#include "key/runtime.h"
#include "ui/native_touch.h"
#include "ui/stem_pads.h"
#include "ui/native_led.h"
#include "ui/native_wave_runtime.h"
#include "ui/layered_wave_runtime.h"
#include "ui/native_skin_runtime.h"
#include "ui/wave_viewport.h"
#include "settings.h"
#include "doom/native_bridge.h"
#include "ota/native_update.h"
#include "arcade/arcade.h"
#include "arcade/input.h"
#include "arcade/clock.h"
#include "arcade/native_clock.h"
#include "arcade/wave_rider_runtime.h"
#include "../vendor/tools/xz_runtime/mods_bridge.h"
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <math.h>

static pthread_mutex_t ui_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct xz_ui ui;
static struct xz_ui inline_ui;
static struct xz_ui_model model;
static struct xz_native_touch touch;
static struct xz_audio_status audio_status[2];
static xz_stock_touch stock_touch;
static void (*forward_touch)(int, int, int);
static int started, visible, audio_available, key_available, forwarded_down;
static int fb_takeover_active;
static int doom_phase,doom_previous_takeover;
static uint32_t doom_launched_at;
static struct xz_arcade arcade;
static struct xz_arcade_input arcade_input;
static int arcade_present,arcade_opening_touch,arcade_touch_owned,arcade_previous_takeover;
static uint32_t arcade_clock_sequence[2];
static uint32_t arcade_clock_poll;
static int arcade_native_clock_checked,arcade_native_clock_ready;
static int wave_rider_previous_takeover;
struct xz_arcade_proof {uint32_t version,frames,inputs,phase,mode,live,deck;float bpm,x,y;};
__attribute__((visibility("default"))) struct xz_arcade_proof xz_arcade_proof_v1={.version=1};
static int network_enabled, network_connected;
static char device_ip[16];
static struct xz_stem_pads pads;
static struct xz_jump_state jump_state;
static int deck_page[2] = {-1,-1};
static unsigned sync_down, sync_owned;
static pthread_cond_t settings_changed = PTHREAD_COND_INITIALIZER;
static pthread_t settings_thread;
static int settings_running, settings_pending, settings_readonly;
static int requested_stems;
static int native_focus=-1;
static uint32_t followed_generation;
static int inline_requested, inline_contact, inline_contact_deck, inline_wave_contact, inline_wave_deck, inline_cancelled;
static uint32_t displayed_generation[2], reported_generation[2];
static unsigned char overview[2][192];
struct prepared_proof_deck { uint32_t state,rate,frames,mixed,missed; int32_t lag; float correlation; };
struct prepared_proof { uint32_t version,sequence; struct prepared_proof_deck deck[2]; };
__attribute__((visibility("default"))) struct prepared_proof xz_prepared_proof_v1={1,0,{{0}}};
static char settings_usb[1024];
static struct stat settings_volume;

static int (*stock_uikey_link)(void *);
static int (*stock_uikey_rekordbox)(void *);

static void wave_rider_to_dj(void) {
    xz_wave_rider_close();
    xz_native_touch_visible(&touch,0);
    __atomic_store_n(&visible,0,__ATOMIC_RELEASE);
    model.fb_takeover=wave_rider_previous_takeover;
    __atomic_store_n(&fb_takeover_active,model.fb_takeover,__ATOMIC_RELEASE);
    ui.page=XZ_UI_GAMES;
}
static void arcade_to_dj(void) {
    arcade_present=0;
    xz_native_touch_visible(&touch,0);
    __atomic_store_n(&visible,0,__ATOMIC_RELEASE);
    model.fb_takeover=arcade_previous_takeover;
    __atomic_store_n(&fb_takeover_active,model.fb_takeover,__ATOMIC_RELEASE);
    ui.page=XZ_UI_EXTRAS;
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
    if(xz_wave_rider_input((int)channel-1,input,xz_game_milliseconds())) {
        if(xz_wave_rider_leave_requested())wave_rider_to_dj();
        pthread_mutex_unlock(&ui_mutex);return 1;
    }
    int consumed=xz_arcade_input_event(&arcade_input,&arcade,arcade_present,(int)channel-1,input,xz_game_milliseconds());
    if(consumed)xz_arcade_proof_v1.inputs++;
    if(arcade_present&&!was_music&&arcade.phase==XZ_ARCADE_MUSIC)arcade_to_dj();
    pthread_mutex_unlock(&ui_mutex);return consumed;
}

static int uikey_link_hook(void *arg0) {
    xz_ui_runtime_on_source_key(0);
    return stock_uikey_link ? stock_uikey_link(arg0) : 0;
}

static int uikey_rekordbox_hook(void *arg0) {
    xz_ui_runtime_on_source_key(1);
    return stock_uikey_rekordbox ? stock_uikey_rekordbox(arg0) : 0;
}

static struct xz_settings settings_snapshot(void) {
    return (struct xz_settings){requested_stems,
        !!(model.enabled & XZ_UI_GATE), !!(model.enabled & XZ_UI_SMART), model.theme,
        model.stem_page, model.shift_pages, model.pad_feedback, model.shift_keysync,
        model.fb_takeover, model.takeover_assign, 0, model.stem_bank, model.jump, model.stems_overlay, model.wave_mode};
}
/* Stem layers use the theme's pad colours: drums, harmonics, vocals. */
static void wave_configure(void) {
    uint32_t colors[3];
    for (int i = 0; i < 3; i++) colors[i] = xz_ui_stem_color(model.theme, i);
    xz_layered_wave_configure(model.wave_mode, colors);
}
static void settings_queue(void) {
    if (settings_readonly) { model.settings_status = "CHANGES LAST UNTIL REBOOT"; return; }
    if (!settings_running) { model.settings_status = "SETTINGS NOT SAVED: START FROM USB"; return; }
    model.settings_status = "SAVING SETTINGS TO USB";
    settings_pending = 1; pthread_cond_signal(&settings_changed);
}
static void *settings_writer(void *unused) {
    (void)unused;
    pthread_mutex_lock(&ui_mutex);
    while (settings_running || settings_pending) {
        while (settings_running && !settings_pending) pthread_cond_wait(&settings_changed,&ui_mutex);
        if (!settings_running && !settings_pending) break;
        struct xz_settings saved = settings_snapshot();
        settings_pending = 0;
        pthread_mutex_unlock(&ui_mutex);
        struct stat current;
        int same_volume = !stat(settings_usb,&current) && current.st_dev == settings_volume.st_dev && current.st_ino == settings_volume.st_ino;
        int rc = same_volume ? xz_settings_save(settings_usb,&saved) : -1;
        pthread_mutex_lock(&ui_mutex);
        if (!settings_pending) model.settings_status = rc ? "SETTINGS NOT SAVED: CHECK USB" : "SETTINGS SAVED TO USB";
    }
    pthread_mutex_unlock(&ui_mutex); return NULL;
}

static void levels(int deck) {
    if (deck < 0 || deck > 1) return;
    struct xz_ui_deck *d = &model.deck[deck];
    struct xz_stem_levels value = {1, 1, 1};
    if (!d->bypass) {
        value.drums = d->muted & 1 ? 0 : d->levels[0];
        value.harmonics = d->muted & 2 ? 0 : d->levels[1];
        value.vocals = d->muted & 4 ? 0 : d->levels[2];
    }
    xz_audio_set_levels(deck, value);
}

static void apply(void *context, const struct xz_ui_action *actions, size_t count) {
    (void)context;
    for (size_t i = 0; i < count; i++) {
        const struct xz_ui_action *a = &actions[i];
        switch (a->kind) {
        case XZ_UI_GAME_SELECT:
            if(a->index==0){ui.page=XZ_UI_DOOM_FILES;xz_doom_browser_scan();}
            else if(xz_doom_choose_game(1)==0)ui.page=XZ_UI_DOOM;
            else {ui.page=XZ_UI_DOOM_FILES;snprintf(ui.notice,sizeof(ui.notice),"DOWNLOAD %s IN THE WINDOWS APP, THEN COPY TO USB",a->index==1?"CHEX QUEST":"DOOM");}
            break;
        case XZ_UI_MYHOUSE_CHECK: {
            struct stat engine;
            int found=stat("/dev/shm/gzdoom-xz",&engine)==0&&S_ISREG(engine.st_mode);
            snprintf(ui.notice,sizeof(ui.notice),"%s",found?"NATIVE ENGINE FOUND / SELECT GAME DATA ON USB":"NATIVE GZDOOM ENGINE IS NOT INSTALLED YET");
            break;
        }
        case XZ_UI_WAVE_RIDER_OPEN:
            if(xz_doom_running()){snprintf(ui.notice,sizeof(ui.notice),"EXIT DOOM BEFORE STARTING WAVE RIDER");break;}
            wave_rider_previous_takeover=model.fb_takeover;model.fb_takeover=0;
            __atomic_store_n(&fb_takeover_active,0,__ATOMIC_RELEASE);
            xz_wave_rider_open(xz_game_milliseconds());
            xz_native_touch_visible(&touch,1);__atomic_store_n(&visible,1,__ATOMIC_RELEASE);
            break;
        case XZ_UI_ARCADE_OPEN:
            if(xz_doom_running()){snprintf(ui.notice,sizeof(ui.notice),"EXIT DOOM BEFORE STARTING BEAT ARCADE");break;}
            if(!arcade.initialized)xz_arcade_init(&arcade,xz_game_milliseconds());
            arcade.last_ms=xz_game_milliseconds();
            arcade_previous_takeover=model.fb_takeover;model.fb_takeover=0;
            __atomic_store_n(&fb_takeover_active,0,__ATOMIC_RELEASE);
            arcade_present=1;arcade_opening_touch=1;arcade.touch_down=1;
            xz_native_touch_visible(&touch,1);__atomic_store_n(&visible,1,__ATOMIC_RELEASE);
            break;
        case XZ_UI_PANEL:
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
            if(!model.update.ready || (ui.page!=XZ_UI_UPDATE_SETUP&&model.update.phase==XZ_UPDATE_ERROR)){ui.page=XZ_UI_UPDATE_SETUP;snprintf(ui.notice,sizeof(ui.notice),"DETECT THE UPDATE SERVER IN SETUP");}
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
            if(!xz_doom_ready()) { ui.page=XZ_UI_DOOM_SETUP; break; }
            if(xz_doom_launch()) { snprintf(ui.notice,sizeof(ui.notice),"DOOM COULD NOT START / CHECK USB"); break; }
            doom_previous_takeover=model.fb_takeover; doom_phase=1; doom_launched_at=xz_game_milliseconds();
            model.fb_takeover=1; __atomic_store_n(&fb_takeover_active,1,__ATOMIC_RELEASE);
            xz_native_touch_visible(&touch,0); __atomic_store_n(&visible,0,__ATOMIC_RELEASE);
            break;
        case XZ_UI_STEMS_OVERLAY:
            model.stems_overlay=!model.stems_overlay;
            if(!model.stems_overlay&&inline_contact){
                struct xz_ui_action ignored[XZ_UI_ACTIONS];xz_ui_cancel(&inline_ui,ignored);inline_cancelled=1;
            }
            __atomic_store_n(&inline_requested,requested_stems && model.stems_overlay && !touch.visible,__ATOMIC_RELEASE);
            break;
        case XZ_UI_CLOSE:
            xz_native_touch_visible(&touch, 0);
            __atomic_store_n(&visible, 0, __ATOMIC_RELEASE);
            break;
        case XZ_UI_DECK:
            if (a->index >= 0 && a->index < 2) { ui.deck = a->index; inline_ui.deck = a->index; }
            break;
        case XZ_UI_ENABLE:
            if (a->index == XZ_UI_GATE || a->index == XZ_UI_SMART ||
                (a->index == XZ_UI_STEM && audio_available)) {
                uint32_t next = a->value != 0 ? model.enabled | (uint32_t)a->index : model.enabled & ~(uint32_t)a->index;
                if (a->index == XZ_UI_STEM) {
                    requested_stems = a->value != 0;
                    xz_audio_set_enabled(a->value != 0); model.enabled = next;
                    if (a->value != 0) { levels(0); levels(1); }
                }
                else if (xz_runtime_set_cues(!!(next & XZ_UI_GATE), !!(next & XZ_UI_SMART)) == 0) model.enabled = next;
            }
            break;
        case XZ_UI_STEM_PAD_CONFIG:
            if(a->index>=0&&a->index<8){model.stem_bank=a->index/4;pads.bank=model.stem_bank;}break;
        case XZ_UI_STEM_BANK:
            if(a->index>=0&&a->index<=1){model.stem_bank=a->index;pads.bank=a->index;}break;
        case XZ_UI_LEVEL:
            if (a->deck >= 0 && a->deck < 2 && a->index >= 0 && a->index < 3) {
                if (a->value > 0) {
                    pads.muted[a->deck] &= ~(1u << a->index);
                    model.deck[a->deck].muted = pads.muted[a->deck];
                }
                model.deck[a->deck].levels[a->index] = a->value; levels(a->deck);
            }
            break;
        case XZ_UI_MUTE:
            if (a->deck >= 0 && a->deck < 2 && a->index >= 0 && a->index < 3) {
                if (a->phase != XZ_UI_PRESS) break;
                pads.muted[a->deck] ^= 1u << a->index;
                model.deck[a->deck].muted = pads.muted[a->deck];
                levels(a->deck);
            }
            break;
        case XZ_UI_BYPASS:
            if (a->deck >= 0 && a->deck < 2) { model.deck[a->deck].bypass = a->value != 0; levels(a->deck); }
            break;
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
        case XZ_UI_KEY_SHIFT:
            if (key_available && a->deck >= 0 && a->deck < 2) {
                int current;
                if (xz_key_get_desired_semitones(a->deck, &current) == 0) {
                    int target = a->index == 0 ? 0 : current + a->index;
                    if (xz_key_set_desired_semitones(a->deck, target) != 0)
                        snprintf(ui.notice, sizeof(ui.notice), "KEY RANGE IS -12 TO +12 SEMITONES");
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
        if (a->kind == XZ_UI_STEM_PAD_CONFIG || a->kind == XZ_UI_STEMS_OVERLAY || a->kind == XZ_UI_STEM_BANK || a->kind == XZ_UI_JUMP_STEP || a->kind == XZ_UI_JUMP_ENABLE || a->kind == XZ_UI_JUMP_SHIFT || a->kind == XZ_UI_JUMP_SIZE || a->kind == XZ_UI_ENABLE || a->kind == XZ_UI_SET_THEME || a->kind == XZ_UI_WAVE_MODE ||
            a->kind == XZ_UI_STEM_PAGE || a->kind == XZ_UI_SHIFT_PAGES ||
            a->kind == XZ_UI_PAD_FEEDBACK || a->kind == XZ_UI_SHIFT_KEYSYNC ||
            a->kind == XZ_UI_TAKEOVER_TOGGLE || a->kind == XZ_UI_TAKEOVER_ASSIGN) settings_queue();
    }
}

static int (*stock_encoder_rotate)(int);
static int encoder_rotate_hook(int delta) {
    if(__atomic_load_n(&started,__ATOMIC_ACQUIRE)){
        pthread_mutex_lock(&ui_mutex);
        int editing=touch.visible&&ui.page==XZ_UI_BEAT_JUMP;
        if(editing&&delta){struct xz_ui_action a={.kind=XZ_UI_JUMP_STEP,.index=delta};apply(NULL,&a,1);}
        pthread_mutex_unlock(&ui_mutex);
        if(editing)return 1;
    }
    return stock_encoder_rotate?stock_encoder_rotate(delta):0;
}

static void refresh(void) {
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
    __atomic_store_n(&inline_requested, requested_stems && model.stems_overlay && !touch.visible, __ATOMIC_RELEASE);
    __atomic_add_fetch(&xz_prepared_proof_v1.sequence,1,__ATOMIC_SEQ_CST);
    for (int deck = 0; deck < 2; deck++) {
        model.deck[deck].ready = XZ_UI_GATE | XZ_UI_SMART | XZ_UI_THEME;
        if (key_available) {
            model.deck[deck].ready |= XZ_UI_KEY;
            xz_key_get_desired_semitones(deck, &model.deck[deck].key_semitones);
        }
        if (audio_available) {
            model.deck[deck].ready |= XZ_UI_STEM;
            if (xz_audio_get_status(deck, &audio_status[deck]) == 0) {
                struct xz_audio_status *s = &audio_status[deck];
                if (displayed_generation[deck] != s->generation) {
                    displayed_generation[deck] = s->generation;
                    struct xz_ui_action cancelled[XZ_UI_ACTIONS];
                    if (inline_contact&&inline_contact_deck == deck) { xz_ui_cancel(&inline_ui,cancelled); inline_cancelled = 1; }
                    if (ui.deck == deck) { xz_ui_cancel(&ui,cancelled); if (touch.capture) touch.opening_contact = 1; }
                    pads.muted[deck] = model.deck[deck].muted = 0;
                    model.deck[deck].levels[0] = model.deck[deck].levels[1] = model.deck[deck].levels[2] = 1;
                    model.deck[deck].bypass = 0;
                }
                xz_prepared_proof_v1.deck[deck] = (struct prepared_proof_deck){s->state,s->reader_rate,s->reader_frames,s->mixed_blocks,s->skipped_blocks,s->alignment_frames,s->alignment_correlation};
                if (s->prepared && s->state == XZ_AUDIO_EXPERIMENTAL_READY && reported_generation[deck] != s->generation) {
                    char line[160];snprintf(line,sizeof(line),"PREPARED_STEMS_READY: deck=%d rate=%u frames=%u lag=%d correlation=%.6f",deck+1,s->reader_rate,s->reader_frames,s->alignment_frames,s->alignment_correlation);
                    xz_log(line);reported_generation[deck] = s->generation;
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
    int focus=xz_native_focus_deck();
    if(focus!=native_focus){native_focus=focus;followed_generation=0;}
    if(focus>=0&&focus<2&&audio_status[focus].prepared&&
       audio_status[focus].generation!=followed_generation){
        followed_generation=audio_status[focus].generation;
        if(inline_ui.deck!=focus){
            struct xz_ui_action cancelled[XZ_UI_ACTIONS];xz_ui_cancel(&inline_ui,cancelled);
            inline_cancelled=inline_contact;inline_ui.deck=focus;
            if(!touch.visible)ui.deck=focus;
        }
    }
    __atomic_add_fetch(&xz_prepared_proof_v1.sequence,1,__ATOMIC_SEQ_CST);
}

static int inline_enabled(void) { return __atomic_load_n(&started,__ATOMIC_ACQUIRE) && __atomic_load_n(&inline_requested,__ATOMIC_ACQUIRE); }
static int render_inline(void *context,uint16_t *pixels,size_t count,size_t stride,int width,int height) {
    (void)context;
    static _Thread_local uint16_t cached[536*XZ_WAVE_INLINE_HEIGHT];
    static _Thread_local int cached_valid;
    if (!pixels || width!=536 || height!=XZ_WAVE_INLINE_HEIGHT || stride<536 || stride>count/XZ_WAVE_INLINE_HEIGHT) return 0;
    if (pthread_mutex_trylock(&ui_mutex) != 0) {
        if (!cached_valid || !inline_enabled()) return 0;
        for (int y=0;y<XZ_WAVE_INLINE_HEIGHT;y++) memcpy(pixels+y*stride,cached+y*536,536*sizeof(*pixels));
        return 1;
    }
    refresh();
    if (touch.visible || !requested_stems || !model.stems_overlay) { pthread_mutex_unlock(&ui_mutex); return 0; }
    int result=xz_ui_inline_render(&inline_ui,&model,pixels,count,stride,width,height);
    struct xz_ui_widget widgets[XZ_UI_WIDGETS];
    size_t widget_count=xz_ui_inline_layout(&inline_ui,width,height,widgets);
    if(result)for(size_t column=0;column<widget_count;column++){
        struct xz_ui_widget widget=widgets[column];
        if(widget.kind!=XZ_UI_MUTE||model.deck[widget.deck].stem_loading)continue;
        unsigned role=(unsigned)widget.index;
        int left=widget.x+4,right=widget.x+widget.w-4;
        unsigned char peaks[256];float progress=0;
        size_t n=(size_t)(right-left);if(n>sizeof(peaks))n=sizeof(peaks);
        if(!xz_audio_waveform(widget.deck,role,peaks,n,&progress))continue;
        uint32_t rgb=xz_ui_stem_color(model.theme,(int)role);
        uint16_t color=(uint16_t)(((rgb>>19)&31)<<11|((rgb>>10)&63)<<5|((rgb>>3)&31));
        float gain=model.deck[widget.deck].muted&(1u<<role)?0:model.deck[widget.deck].levels[role];
        int center=widget.y+24;
        for(size_t x=0;x<n;x++){
            int amplitude=(int)(peaks[x]*gain*3/31);if(amplitude>3)amplitude=3;
            for(int y=center-4;y<=center+4;y++)pixels[(size_t)y*stride+(size_t)left+x]=xz_theme_rgb565(xz_theme_palette(model.theme)->bg);
            for(int y=center-amplitude;y<=center+amplitude;y++)pixels[(size_t)y*stride+(size_t)left+x]=gain?color:0x3186;
        }
        int needle=left+(int)(progress*(float)(n-1));
        for(int y=center-4;y<=center+4;y++)pixels[(size_t)y*stride+(size_t)needle]=0xffff;
    }
    if (result) {
        for (int y=0;y<XZ_WAVE_INLINE_HEIGHT;y++) memcpy(cached+y*536,pixels+y*stride,536*sizeof(*pixels));
        cached_valid=1;
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
    if (event->deck >= 0 && event->deck < 2) deck_page[event->deck] = event->pad_page;
    if (event->sync && event->deck >= 0 && event->deck < 2) {
        unsigned bit = 1u << event->deck;
        int owned = !!(sync_owned & bit);
        if (event->operation == 2 || event->operation == 3) {
            sync_down &= ~bit; sync_owned &= ~bit;
        } else if (event->operation == 0 && !(sync_down & bit)) {
            sync_down |= bit;
            if (event->shift && model.shift_keysync) {
                sync_owned |= bit; owned = 1;
                snprintf(ui.notice,sizeof(ui.notice),"KEY SYNC NOT READY: TRACK KEY / MASTER METADATA REQUIRED");
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
        if (toggle == 3) model.deck[deck].bypass = !model.deck[deck].bypass;
        levels(deck);
    }
    if (event->deck >= 0 && event->deck < 2) {
        if (model.deck[event->deck].bypass) *trace_flags |= 512u;
        *trace_flags |= (model.deck[event->deck].muted & 7u) << 10;
        if (audio_status[event->deck].state == XZ_AUDIO_EXPERIMENTAL_READY) *trace_flags |= 8192u;
    }
    pthread_mutex_unlock(&ui_mutex);
    return consumed||jump_consumed;
}

int xz_ui_runtime_pad_color(int deck,int pad,unsigned *rgb,int *lit) {
    if (!__atomic_load_n(&started,__ATOMIC_ACQUIRE) || deck < 0 || deck > 1 || pad < 0 || pad > 7) return 0;
    if (pthread_mutex_trylock(&ui_mutex)) return 0;
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
    if(model.jump.enabled&&model.jump.shift_page&&jump_state.page[deck]==1&&deck_page[deck]==3){
        *rgb=xz_jump_page2_color(pad);*lit=1;
        pthread_mutex_unlock(&ui_mutex);return XZ_LED_HUE;
    }
    int slot=pad-(model.stem_bank==1?4:0);
    int active = !(model.jump.enabled&&deck_page[deck]==3) && slot>=0&&slot<4&&stem_controls_active(deck) && model.pad_feedback && deck_page[deck] == model.stem_page;
    if (active) {
        *rgb = xz_stem_hardware_color(slot<3?xz_stem_for_pad(slot):3);
        *lit = slot < 3 ? !(model.deck[deck].muted & (1u << xz_stem_for_pad(slot))) &&
            model.deck[deck].levels[xz_stem_for_pad(slot)]>0 && !model.deck[deck].bypass : model.deck[deck].bypass;
    }
    pthread_mutex_unlock(&ui_mutex); return active;
}
void xz_ui_runtime_pad_native_page(int deck,int page) {
    if (deck < 0 || deck > 1) return;
    pthread_mutex_lock(&ui_mutex); deck_page[deck] = page; pthread_mutex_unlock(&ui_mutex);
}

static int shortcut_touch(void *self, const struct xz_touch_status *status, const void *mode, int previous_down);
static void touch_hook(void *self, const struct xz_touch_status *status, const void *mode) {
    if (!__atomic_load_n(&started, __ATOMIC_ACQUIRE)) { stock_touch(self, status, mode); return; }
    pthread_mutex_lock(&ui_mutex);
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
            if(!was_music&&arcade.phase==XZ_ARCADE_MUSIC)arcade_to_dj();
        }else if(!status->down)arcade_touch_owned=0;
        pthread_mutex_unlock(&ui_mutex);return;
    }
    refresh();
    touch.vj_btn_w=(model.fb_takeover||xz_native_play_visible())?112:0;
    int previous_down=((const unsigned char *)self)[4]!=0;
    if(shortcut_touch(self,status,mode,previous_down)){pthread_mutex_unlock(&ui_mutex);return;}
    if(!touch.visible&&!previous_down&&status->down&&status->x<115&&status->y>=24){
        if(status->y>=18&&status->y<159)ui.deck=inline_ui.deck=0;
        else if(status->y>=161&&status->y<302)ui.deck=inline_ui.deck=1;
    }
    int active=!touch.visible&&!(model.fb_takeover&&model.connection.connected)&&xz_native_inline_active();
    int pressed_deck=status->y>=XZ_WAVE_FIRST_CONTROL_Y&&status->y<XZ_WAVE_FIRST_CONTROL_Y+XZ_WAVE_CONTROL_HEIGHT?0:
        status->y>=XZ_WAVE_SECOND_CONTROL_Y&&status->y<XZ_WAVE_SECOND_CONTROL_Y+XZ_WAVE_CONTROL_HEIGHT?1:-1;
    if(active&&!inline_contact&&!previous_down&&status->down&&status->x>=131&&status->x<667&&pressed_deck>=0){
        inline_contact=1;inline_contact_deck=pressed_deck;inline_cancelled=0;
    }
    int owned;
    if(inline_contact){
        struct xz_ui_action actions[XZ_UI_ACTIONS];
        int row_y=(int)status->y-(inline_contact_deck?XZ_WAVE_SECOND_CONTROL_Y:XZ_WAVE_FIRST_CONTROL_Y);
        if(row_y<0)row_y=0;else if(row_y>=XZ_WAVE_CONTROL_HEIGHT)row_y=XZ_WAVE_CONTROL_HEIGHT-1;
        size_t n=inline_cancelled?0:xz_ui_inline_touch(&inline_ui,&model,536,XZ_WAVE_INLINE_HEIGHT,(int)status->x-131,row_y+inline_contact_deck*XZ_WAVE_CONTROL_HEIGHT,!!status->down,actions);
        if(n)apply(NULL,actions,n);
        if(!status->down){inline_contact=0;inline_cancelled=0;}
        owned=1;
    }else{
        if(!previous_down&&status->down){
            inline_wave_deck=status->y>=18&&status->y<XZ_WAVE_FIRST_CONTROL_Y?0:
                status->y>=18+XZ_WAVE_SECOND_LANE_Y&&status->y<XZ_WAVE_SECOND_CONTROL_Y?1:-1;
            inline_wave_contact=active&&status->x>=131&&status->x<667&&inline_wave_deck>=0;
        }
        struct xz_touch_status mapped=*status;
        if(inline_wave_contact){
            int row=(int)mapped.y-(inline_wave_deck?18+XZ_WAVE_SECOND_LANE_Y:18);
            if(row<0)row=0;else if(row>=XZ_WAVE_LANE_HEIGHT)row=XZ_WAVE_LANE_HEIGHT-1;
            mapped.y=18+xz_wave_source_row((unsigned)(row+(inline_wave_deck?XZ_WAVE_SECOND_LANE_Y:0)),XZ_WAVE_LANE_HEIGHT);
        }
        owned=xz_native_touch_dispatch(&touch,self,&mapped,mode,stock_touch);
        if(!status->down)inline_wave_contact=0;
    }
    __atomic_store_n(&visible, touch.visible, __ATOMIC_RELEASE);
    if ((owned || !model.fb_takeover || !model.connection.connected) && forwarded_down && forward_touch) {
        forward_touch(0, (int)status->x, (int)status->y); forwarded_down = 0;
    } else if (!owned && model.fb_takeover && model.connection.connected && forward_touch && !!status->down != forwarded_down) {
        forwarded_down = !!status->down;
        forward_touch(forwarded_down, (int)status->x, (int)status->y);
    }
    pthread_mutex_unlock(&ui_mutex);
}

__attribute__((visibility("default"))) int xz_mods_visible_v1(void) {
    return __atomic_load_n(&visible, __ATOMIC_ACQUIRE);
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
    for (int i = 0; i < 3; i++) colors[i] = xz_ui_stem_color(model.theme, i);
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
    }
    int segment = shortcut_contact;
    if (segment < 2) {
        struct xz_touch_status mapped = *status;
        mapped.x = segment ? 455 : 385; mapped.y = 252;
        stock_touch(self, &mapped, mode);
    }
    if (!status->down) {
        int wanted = segment >= 2 ? segment - 1 : XZ_WAVE_STOCK;
        if (wanted != model.wave_mode) {
            struct xz_ui_action action = { .kind = XZ_UI_WAVE_MODE, .index = wanted };
            apply(NULL, &action, 1);
        }
        shortcut_contact = -1;
    }
    return 1;
}
static void badge(uint16_t *pixels, uint32_t stride) {
    shortcut_wave(pixels, stride);
    int play=xz_native_play_visible();
    if(model.theme==XZ_THEME_LCARS&&play&&!model.fb_takeover)xz_ui_render_lcars_play_header(pixels,stride);
    xz_ui_render_native_buttons(pixels,stride,model.theme,model.stems_overlay,
        model.connection.enabled&&model.connection.connected&&(model.fb_takeover||play),model.fb_takeover);
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
    if (pthread_mutex_trylock(&ui_mutex) != 0) return 0;
    refresh();
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
    int result;
    if(xz_wave_rider_present())result=xz_wave_rider_draw(pixels,(size_t)stride*height,stride,xz_game_milliseconds());
    else if(arcade_present){
        arcade_tick();
        if(arcade_present)result=xz_arcade_render(&arcade,pixels,(size_t)stride*height,stride);
        else {badge(pixels,stride);result=1;}
        xz_arcade_proof_v1.frames++;
        xz_arcade_proof_v1.phase=arcade.phase;xz_arcade_proof_v1.mode=arcade.mode;
        xz_arcade_proof_v1.live=arcade.live;xz_arcade_proof_v1.deck=arcade.clock_deck;
        xz_arcade_proof_v1.bpm=arcade.bpm;xz_arcade_proof_v1.x=arcade.x;xz_arcade_proof_v1.y=arcade.y;
    }
    else if (touch.visible) result = xz_ui_render(&ui, &model, pixels, (size_t)stride * height, stride);
    else { badge(pixels, stride); result = 1; }
    pthread_mutex_unlock(&ui_mutex);
    return result;
}

int xz_ui_runtime_start(int audio_ready, int key_ready, int stems_enabled) {
    static const unsigned char guard[8] = {0xf0,0x45,0x2d,0xe9,0x02,0x70,0xa0,0xe1};
    static const unsigned char link_guard[8] = {0x70,0x40,0x2d,0xe9,0x00,0x50,0xa0,0xe1};
    static const unsigned char rekordbox_guard[8] = {0x38,0x40,0x2d,0xe9,0x00,0x50,0xa0,0xe1};
    if (started) return 0;
    settings_usb[0] = 0; settings_pending = 0;
    forward_touch = (void (*)(int,int,int))dlsym(RTLD_DEFAULT, "xz_vj_touch_v1");
    if (!forward_touch) { xz_log("UI unavailable: receiver does not expose the optional display bridge"); return -1; }
    audio_available = audio_ready;
    key_available = key_ready;
    memset(&model, 0, sizeof(model));
    model.jump=xz_jump_defaults();
    memset(&jump_state,0,sizeof(jump_state));
    model.pad_feedback = 1;
    model.stems_overlay = 1;
    model.wave_mode = XZ_WAVE_THREE_BAND;
    model.fb_takeover = 0;
    model.takeover_assign = XZ_TAKEOVER_LINK;
    __atomic_store_n(&fb_takeover_active, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&network_enabled, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&network_connected, 0, __ATOMIC_RELEASE);
    requested_stems = !!stems_enabled;
    model.settings_status = "SETTINGS NOT SAVED: START FROM USB";
    model.enabled = xz_runtime_cue_flags();
    if (audio_ready && stems_enabled) model.enabled |= XZ_UI_STEM;
    const char *usb = getenv("XZ_MODS_SETTINGS_USB");
    if(!usb||!usb[0])usb=getenv("XZ_MODS_USB");
    if (usb && strlen(usb) < sizeof(settings_usb) && !stat(usb,&settings_volume) && S_ISDIR(settings_volume.st_mode)) {
        strcpy(settings_usb,usb);
        struct xz_settings saved;
        int rc = xz_settings_load(usb,&saved);
        if (!rc) {
            model.jump=saved.jump;
            model.stems_overlay=saved.stems_overlay;
            model.wave_mode=saved.wave_mode;
            requested_stems = saved.stems;
            model.theme = saved.theme; model.stem_page = saved.stem_page;
            model.shift_pages = saved.shift_pages; model.pad_feedback = saved.pad_feedback;
            model.shift_keysync = saved.shift_keysync;
            model.fb_takeover = saved.fb_takeover;
            model.takeover_assign = saved.takeover_assign;model.stem_bank=saved.stem_bank;pads.bank=saved.stem_bank;
            __atomic_store_n(&fb_takeover_active, model.fb_takeover, __ATOMIC_RELEASE);
            xz_runtime_set_cues(saved.gate,saved.smart);
            model.enabled = xz_runtime_cue_flags();
            if (audio_ready) { xz_audio_set_enabled(saved.stems); if (saved.stems) model.enabled |= XZ_UI_STEM; }
        }
        model.settings_status = rc < 0 ? "INVALID USB SETTINGS: USING DEFAULTS" : rc ? "USB SETTINGS READY" : "SETTINGS RESTORED FROM USB";
    }
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
        model.deck[i].levels[0] = model.deck[i].levels[1] = model.deck[i].levels[2] = 1;
        model.deck[i].sample_volume = 1;
        model.deck[i].track = i < 2 ? "NO TRACK CAPTURED" : "EXTERNAL USB AUDIO";
        model.deck[i].status = i < 2 ? "NATIVE ADAPTER EXPERIMENTAL" : "CONTROL IN REKORDBOX OR SERATO";
    }
    xz_ui_init(&ui);
    xz_ui_init(&inline_ui);
    xz_native_touch_init(&touch, &ui, &model, apply, NULL);
    refresh();
    if (xz_hook_arm(0x2628b4, guard, (void *)touch_hook, (void **)&stock_touch) != 0) return -1;
    if (xz_hook_arm(0xdf994, link_guard, (void *)uikey_link_hook, (void **)&stock_uikey_link) != 0)
        xz_log("Source key LINK hook unavailable");
    if (xz_hook_arm(0xe0a50, rekordbox_guard, (void *)uikey_rekordbox_hook, (void **)&stock_uikey_rekordbox) != 0)
        xz_log("Source key REKORDBOX hook unavailable");
    if (settings_usb[0]) {
        settings_running = 1;
        if (pthread_create(&settings_thread,NULL,settings_writer,NULL)) {
            settings_running = 0; model.settings_status = "SETTINGS SAVE UNAVAILABLE";
        }
    }
    static const unsigned char encoder_guard[8]={0x08,0x40,0x2d,0xe9,0x02,0x30,0x80,0xe2};
    model.jump_encoder=xz_hook_arm(0xe15b0,encoder_guard,(void*)encoder_rotate_hook,(void**)&stock_encoder_rotate)==0;
    if(!model.jump_encoder)xz_log("Beat-jump encoder unavailable; touch controls remain available");
    __atomic_store_n(&started, 1, __ATOMIC_RELEASE);
    if (xz_native_inline_start(inline_enabled,render_inline,NULL)) xz_log("Native inline stems unavailable: window ABI guard failed");
    if (xz_native_led_start()) xz_log("Native pad LED feedback unavailable");
    if (xz_native_skin_start(model.theme)) xz_log("Native whole-interface theme adapter unavailable");
    xz_log("UI touch adapter installed; MODS badge opens the panel");
    /* Private RAM acceptance can open the menu without synthesizing a DJ key.
       Ordinary USB boot never sets this flag. */
    const char *arcade_trial=getenv("XZ_MODS_ARCADE_TRIAL");
    if(arcade_trial&&(!strcmp(arcade_trial,"1")||!strcmp(arcade_trial,"wave-rider"))){
        pthread_mutex_lock(&ui_mutex);
        struct xz_ui_action action={.kind=!strcmp(arcade_trial,"wave-rider")?XZ_UI_WAVE_RIDER_OPEN:XZ_UI_ARCADE_OPEN};apply(NULL,&action,1);
        if(action.kind==XZ_UI_WAVE_RIDER_OPEN)xz_wave_rider_touch(0,0,0,xz_game_milliseconds());
        arcade_opening_touch=0;arcade.touch_down=0;
        pthread_mutex_unlock(&ui_mutex);
    }
    return 0;
}

void xz_ui_runtime_stop(void) {
    xz_native_skin_stop();
    xz_native_led_stop();
    __atomic_store_n(&started, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&visible, 0, __ATOMIC_RELEASE);
    pthread_mutex_lock(&ui_mutex);
    arcade_present=0;memset(&arcade_input,0,sizeof(arcade_input));
    xz_wave_rider_close();
    int join = settings_running;
    settings_running = 0; pthread_cond_signal(&settings_changed);
    pthread_mutex_unlock(&ui_mutex);
    if (join) pthread_join(settings_thread,NULL);
}
