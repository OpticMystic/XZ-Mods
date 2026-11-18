/* SPDX-License-Identifier: MIT
 * The XZ SDK binding inside the real ui_runtime.c, driven by a recording
 * manager. First the whole existing runtime control suite runs with an idle
 * manager present; then the binding's lifecycle, takeover and callbacks run.
 * Copied into mods/ui/test_sdk_ui_runtime.c; built by verify_sdk_runtime.py. */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#define main existing_runtime_controls
#include "test_runtime_controls.c"
#undef main
#include <stdio.h>
void xz_layered_wave_configure(int mode,const uint32_t rgb[3]){(void)mode;(void)rgb;}
int xz_read_memory(uint32_t address,void *out,size_t n){(void)address;(void)out;(void)n;return -1;}

/* Recording manager double: the binding's only collaborator. It owns no files or processes. */
static struct xz_sdk_host_config manager_config;
static const char *manager_roots[XZ_SDK_ROOTS];
/* manager_foreground is the reservation plus running app (admission); snapshot.foreground is a running app. */
static int manager_starts,manager_stops,manager_foreground,manager_busy,manager_refuse,manager_start_result;
static struct xz_sdk_snapshot manager_snapshot;
static struct xz_sdk_command manager_commands[32];
static unsigned manager_command_count;
int xz_sdk_host_start(const struct xz_sdk_host_config *config){
    manager_config=*config;memcpy(manager_roots,config->roots,sizeof(manager_roots));manager_starts++;return manager_start_result;
}
void xz_sdk_host_stop(void){manager_stops++;manager_foreground=0;manager_snapshot.foreground=0;}
int xz_sdk_host_read(struct xz_sdk_snapshot *out){if(manager_busy)return -1;*out=manager_snapshot;return 0;}
int xz_sdk_host_submit(const struct xz_sdk_command *command){
    if(manager_refuse)return -1;
    assert(manager_command_count<32);manager_commands[manager_command_count++]=*command;return 0;
}
int xz_sdk_host_foreground(void){return manager_foreground;}

static void locked_refresh(void){pthread_mutex_lock(&ui_mutex);refresh();pthread_mutex_unlock(&ui_mutex);}
static void catalog_item(unsigned i,const char *id,const char *name,unsigned kind,unsigned state){
    struct xz_sdk_item *item=&manager_snapshot.items[i];memset(item,0,sizeof(*item));
    snprintf(item->id,sizeof(item->id),"%s",id);snprintf(item->name,sizeof(item->name),"%s",name);
    snprintf(item->version,sizeof(item->version),"0.1.0");item->kind=kind;item->state=state;
    if(i>=manager_snapshot.count)manager_snapshot.count=i+1;
}
static struct xz_ui_widget panel_widget(int kind,int index){
    struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(&ui,&model,w);
    for(size_t i=0;i<n;i++)if((int)w[i].kind==kind&&w[i].index==index)return w[i];
    assert(!"missing SDK control");return w[0];
}
static void press(unsigned char *self,int kind,int index){
    struct xz_ui_widget w=panel_widget(kind,index);
    contact(self,(unsigned)(w.x+w.w/2),(unsigned)(w.y+w.h/2),1);contact(self,(unsigned)(w.x+w.w/2),(unsigned)(w.y+w.h/2),0);
}
/* The receiver resolves this export with dlsym; no header declares it. */
int xz_mods_game_active_v1(void);
static int display_owner_active(void){return xz_mods_game_active_v1();}

int main(void){
    unsigned char self[32]={0};
    unsetenv("XZ_MODS_USB");manager_start_result=0;
    sdk_start();
    assert(sdk_started&&manager_starts==1&&model.sdk.host==XZ_SDK_UI_RUNNING&&manager_config.root_count==2);
    /* An idle manager changes nothing the existing runtime suite checks. */
    assert(existing_runtime_controls()==0);
    assert(manager_command_count==0&&!sdk_session.active);

    /* Lifecycle and the configuration handed to the manager. */
    sdk_stop();assert(manager_stops==1&&!sdk_started&&model.sdk.host==XZ_SDK_UI_ABSENT);
    sdk_stop();assert(manager_stops==1);
    setenv("XZ_MODS_USB","/tmp/xz-sdk-dev-usb/",1);sdk_start();
    assert(manager_config.root_count==3&&!strcmp(manager_roots[0],"/media/usb1")&&!strcmp(manager_roots[1],"/media/usb2")&&
        !strcmp(manager_roots[2],"/tmp/xz-sdk-dev-usb")&&!manager_roots[3]&&model.sdk.roots==3&&model.sdk.extra_root);
    assert(!strcmp(manager_config.applications_folder,XZ_SDK_UI_APPLICATIONS_FOLDER)&&
        !strcmp(manager_config.autoload_folder,XZ_SDK_UI_AUTOLOAD_FOLDER)&&!strcmp(manager_config.stage_root,"/dev/shm/xz-sdk")&&
        manager_config.autoload_enabled==1&&manager_config.apply_profile==sdk_apply_profile&&
        manager_config.display_available==sdk_display_available);
    /* The loader USB is a partition beneath /media/usb1; only exact duplicates collapse. */
    sdk_stop();setenv("XZ_MODS_USB","/media/usb1/sda1",1);sdk_start();
    assert(manager_config.root_count==3&&!strcmp(manager_roots[2],"/media/usb1/sda1")&&model.sdk.extra_root&&sdk_started);
    sdk_stop();setenv("XZ_MODS_USB","/media/usb2/",1);sdk_start();
    assert(manager_config.root_count==2&&!model.sdk.extra_root&&sdk_started);

    /* A PC-only frame skips heavy local refresh but still tracks SDK app exit. */
    {
        struct xz_ui_model saved=model;struct xz_ui saved_inline=inline_ui;
        struct xz_pc_stem_source saved_sources[2]={fixture_pc_source[0],fixture_pc_source[1]};
        int saved_observing=pc_observing;uint16_t pixels[536*XZ_WAVE_INLINE_HEIGHT];
        for(unsigned d=0;d<2;d++)fixture_pc_source[d]=(struct xz_pc_stem_source){.pc=1,.slot=(int)d,.linked=1,.rekordbox=1,.loaded=1,.epoch=100+d};
        pc_observing=1;touch.visible=0;model.stems_overlay=1;model.fb_takeover=0;pc_refresh_locked();
        memset(&manager_snapshot,0,sizeof(manager_snapshot));manager_foreground=manager_snapshot.foreground=1;
        unsigned reads=fixture_audio_status_calls[0]+fixture_audio_status_calls[1];
        assert(render_inline(NULL,pixels,sizeof(pixels)/sizeof(*pixels),536,536,XZ_WAVE_INLINE_HEIGHT));
        assert(sdk_session.active&&model.fb_takeover==1);
        assert(fixture_audio_status_calls[0]+fixture_audio_status_calls[1]==reads);
        manager_foreground=manager_snapshot.foreground=0;
        assert(render_inline(NULL,pixels,sizeof(pixels)/sizeof(*pixels),536,536,XZ_WAVE_INLINE_HEIGHT));
        assert(!sdk_session.active&&model.fb_takeover==0);
        fixture_pc_source[0]=saved_sources[0];fixture_pc_source[1]=saved_sources[1];pc_refresh_locked();pc_observing=saved_observing;
        model=saved;inline_ui=saved_inline;
    }

    /* Display callback: games keep the display; a stopped UI grants nothing. */
    started=1;arcade_present=0;
    assert(sdk_display_available(NULL));
    arcade_present=1;assert(!sdk_display_available(NULL));arcade_present=0;
    started=0;assert(!sdk_display_available(NULL));started=1;

    /* A foreground app: the previous takeover is captured once, the display is
       forced once and the panel closes once so touches reach the app. */
    memset(&manager_snapshot,0,sizeof(manager_snapshot));manager_snapshot.generation=4;manager_snapshot.autoload_enabled=1;
    catalog_item(0,XZ_SDK_UI_TEST_ID,"Test card",XZ_SDK_APPLICATION,XZ_SDK_RUNNING);
    catalog_item(1,"com.example.club","Club",XZ_SDK_PROFILE,XZ_SDK_READY);
    xz_ui_init(&ui);ui.page=XZ_UI_GAMES;touch.visible=1;__atomic_store_n(&visible,1,__ATOMIC_RELEASE);
    model.fb_takeover=0;__atomic_store_n(&fb_takeover_active,0,__ATOMIC_RELEASE);
    model.connection.enabled=model.connection.connected=1;
    assert(!display_owner_active());
    /* A launch reservation alone guards games and admission but takes nothing yet. */
    manager_foreground=1;locked_refresh();
    assert(model.fb_takeover==0&&touch.visible&&!sdk_session.active&&display_owner_active());
    {struct xz_ui_action reserved={.kind=XZ_UI_ARCADE_OPEN};apply(NULL,&reserved,1);assert(!arcade_present&&ui.page==XZ_UI_APPS);ui.page=XZ_UI_GAMES;}
    manager_snapshot.foreground=1;locked_refresh();
    assert(model.fb_takeover==1&&__atomic_load_n(&fb_takeover_active,__ATOMIC_ACQUIRE)==1&&sdk_session.active&&!sdk_session.previous);
    assert(!touch.visible&&!__atomic_load_n(&visible,__ATOMIC_ACQUIRE)&&model.sdk.snapshot.generation==4&&model.sdk.foreground);
    assert(display_owner_active()); /* the receiver refuses remote clients now */
    assert(settings_snapshot().fb_takeover==0); /* the forced value is never saved */
    /* The user peeks at the DJ screen; refresh never forces it back. */
    model.fb_takeover=0;locked_refresh();assert(model.fb_takeover==0);
    model.fb_takeover=1;locked_refresh();assert(model.fb_takeover==1);
    /* The MODS badge reopens APPLICATIONS, and no later refresh closes it again. */
    contact(self,770,10,1);contact(self,770,10,0);
    assert(touch.visible&&ui.page==XZ_UI_APPS);
    locked_refresh();assert(touch.visible);
    /* STOP goes to the manager with the row's generation. */
    press(self,XZ_UI_SDK_STOP,0);
    assert(manager_command_count==1&&manager_commands[0].kind==XZ_SDK_STOP&&manager_commands[0].index==0&&manager_commands[0].generation==4);
    assert(strstr(ui.notice,"STOPPING Test card"));
    /* Game launches wait for STOP and point at it. */
    ui.page=XZ_UI_GAMES;
    struct xz_ui_action game={.kind=XZ_UI_ARCADE_OPEN};apply(NULL,&game,1);
    assert(!arcade_present&&ui.page==XZ_UI_APPS&&strstr(ui.notice,"STOP THE RUNNING APP"));
    game.kind=XZ_UI_WAVE_RIDER_OPEN;apply(NULL,&game,1);game.kind=XZ_UI_DOOM_START;apply(NULL,&game,1);
    assert(!doom_phase&&model.fb_takeover==1);
    /* Profiles from the manager: parsed, applied under ui_mutex, saved only on request,
       and never drop the app's display; the profile's takeover returns afterwards. */
    {
        struct xz_settings profile;char record[1024];xz_settings_default(&profile);profile.ui_interface=XZ_INTERFACE_CLASSIC;profile.classic_theme=5;profile.theme=5;profile.fb_takeover=1;
        assert(xz_settings_format(&profile,record,sizeof(record))>0);
        settings_running=0;settings_pending=0;settings_readonly=0;
        assert(sdk_apply_profile(NULL,record,0)==0);
        assert(model.theme==5&&model.fb_takeover==1&&sdk_session.previous==1&&!settings_pending&&
            !strcmp(model.settings_status,"PROFILE APPLIED / NOT SAVED"));
        settings_running=1;snprintf(settings_profile.root,sizeof(settings_profile.root),"/tmp/xz-sdk-profile");
        assert(sdk_apply_profile(NULL,record,1)==0&&settings_pending==1&&settings_request.values.theme==5&&settings_request.values.fb_takeover==1);
        settings_pending=0;settings_running=0;settings_profile.root[0]=0;
        assert(sdk_apply_profile(NULL,"XZ_MODS_SETTINGS 9\n",1)==-1&&sdk_apply_profile(NULL,NULL,0)==-1);
        profile.fb_takeover=0;assert(xz_settings_format(&profile,record,sizeof(record))>0);
        assert(sdk_apply_profile(NULL,record,0)==0&&model.fb_takeover==1&&sdk_session.previous==0);
        started=0;assert(sdk_apply_profile(NULL,record,0)==-1);started=1;
        {
            /* A legacy app profile sets only what it wrote; explicit Off and Stems mode stay. */
            struct xz_settings applied=settings_snapshot();applied.key_feedback=0;applied.wave_mode=2;profile_apply_locked(&applied,"explicit-profile");
            assert(sdk_apply_profile(NULL,"XZ_MODS_SETTINGS 1\nstems=1\ngate=0\nsmart=0\ntheme=7\nstem_page=0\nshift_pages=0\npad_feedback=1\nshift_keysync=0\n",0)==0);
            assert(model.theme==7&&model.key_feedback==0&&model.wave_mode==2);
        }
        model.theme=0;
    }
    /* The app exits: the previous takeover returns exactly once. */
    manager_foreground=0;manager_snapshot.foreground=0;manager_snapshot.items[0].state=XZ_SDK_READY;manager_snapshot.generation=5;locked_refresh();
    assert(model.fb_takeover==0&&!sdk_session.active&&!display_owner_active()&&model.sdk.snapshot.generation==5);
    model.fb_takeover=1;locked_refresh();assert(model.fb_takeover==1);model.fb_takeover=0;
    /* A busy manager lock keeps the last snapshot on screen. */
    manager_busy=1;manager_snapshot.generation=9;locked_refresh();assert(!model.sdk.fresh&&model.sdk.snapshot.generation==5);
    manager_busy=0;locked_refresh();assert(model.sdk.fresh&&model.sdk.snapshot.generation==9);
    /* A row pressed against an older list never reaches the manager. */
    touch.visible=1;ui.page=XZ_UI_APPS;
    {
        struct xz_ui_widget run=panel_widget(XZ_UI_SDK_RUN,0);struct xz_ui_action actions[XZ_UI_ACTIONS];
        assert(xz_ui_touch(&ui,&model,run.x+run.w/2,run.y+run.h/2,1,actions)==1&&actions[0].sdk_generation==9);
        struct xz_ui_action released[XZ_UI_ACTIONS];xz_ui_touch(&ui,&model,0,0,0,released);
        manager_snapshot.generation=10;locked_refresh();
        unsigned before=manager_command_count;apply(NULL,actions,1);
        assert(manager_command_count==before&&strstr(ui.notice,"APP LIST CHANGED"));
        /* A busy manager says so instead of failing silently. */
        manager_refuse=1;press(self,XZ_UI_SDK_RUN,0);
        assert(manager_command_count==before&&!strcmp(ui.notice,"APP MANAGER BUSY OR LIST CHANGED / TAP AGAIN"));
        manager_refuse=0;press(self,XZ_UI_SDK_SAVE,1);
        assert(manager_command_count==before+1&&manager_commands[before].kind==XZ_SDK_RUN&&manager_commands[before].index==1&&
            manager_commands[before].value==1&&manager_commands[before].generation==10&&strstr(ui.notice,"APPLYING AND SAVING"));
        /* Setup runs detection on arrival. */
        press(self,XZ_UI_SDK_SETUP,XZ_SDK_UI_DETECT);
        assert(ui.page==XZ_UI_APPS_SETUP&&manager_command_count==before+2&&manager_commands[before+1].kind==XZ_SDK_SCAN);
        ui.sdk_step=XZ_SDK_UI_FOLDERS;manager_snapshot.autoload_enabled=0;locked_refresh();
        press(self,XZ_UI_SDK_CHOICE,1);
        assert(manager_command_count==before+3&&manager_commands[before+2].kind==XZ_SDK_AUTOLOAD&&manager_commands[before+2].value==1);
        /* SDK actions never queue an XZ Mods settings save. */
        assert(!settings_pending);
    }
    /* Stopping during a session restores the takeover and unregisters the owner. */
    model.fb_takeover=0;manager_foreground=manager_snapshot.foreground=1;locked_refresh();assert(model.fb_takeover==1&&sdk_session.active&&display_owner_active());
    int stops=manager_stops;sdk_stop();
    assert(manager_stops==stops+1&&!sdk_started&&model.fb_takeover==0&&!sdk_session.active&&!display_owner_active()&&
        model.sdk.host==XZ_SDK_UI_ABSENT);
    manager_foreground=manager_snapshot.foreground=1;locked_refresh();assert(model.fb_takeover==0); /* no manager, no effect */
    /* A manager that fails to start leaves the panel in setup mode and games free. */
    manager_start_result=-1;sdk_start();assert(!sdk_started&&model.sdk.host==XZ_SDK_UI_ABSENT&&!display_owner_active());
    /* The session above closed the panel; the user reopens it on APPLICATIONS. */
    touch.visible=1;ui.page=XZ_UI_APPS;
    {struct xz_ui_widget setup=panel_widget(XZ_UI_SDK_SETUP,XZ_SDK_UI_DETECT);assert(!strcmp(setup.label,"SET UP APPS"));}
    unsigned before=manager_command_count;press(self,XZ_UI_SDK_SCAN,0);
    assert(manager_command_count==before&&ui.page==XZ_UI_APPS_SETUP&&strstr(ui.notice,"NOT RUNNING"));
    struct xz_ui_action game_open={.kind=XZ_UI_ARCADE_OPEN};apply(NULL,&game_open,1);
    assert(arcade_present);arcade_to_dj();
    puts("PASS SDK runtime binding: existing controls with an idle manager, config and roots, display callback, "
         "one takeover capture/force/close and one restore, badge reopens APPLICATIONS, game guard, profile apply/persist, "
         "stale and busy submits, setup detection, stop restores and unregisters, absent manager");
    return 0;
}
