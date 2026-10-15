/* SPDX-License-Identifier: MIT */
#define xz_settings_find_usb fixture_find_usb
#define xz_settings_mount_id fixture_mount_id
#define main existing_profile_controls_test
#include "../ui/test_runtime_controls.c"
#undef main
#undef xz_settings_find_usb
#undef xz_settings_mount_id
#include <unistd.h>
void xz_layered_wave_configure(int mode,const uint32_t rgb[3]){(void)mode;(void)rgb;}
int xz_read_memory(uint32_t address,void *out,size_t n){(void)address;(void)out;(void)n;return -1;}
static void profile_pause(long nanos) { struct timespec pause={0,nanos};nanosleep(&pause,NULL); }
static pthread_mutex_t fixture_mutex=PTHREAD_MUTEX_INITIALIZER;
static char fixture_usb[2][1024];
int fixture_find_usb(unsigned slot,char out[1024]) {
    pthread_mutex_lock(&fixture_mutex);
    snprintf(out,1024,"%s",fixture_usb[slot]);
    pthread_mutex_unlock(&fixture_mutex); return out[0]?0:1;
}
unsigned long fixture_mount_id(const char *root){(void)root;return 0;}
static void wait_profile(const char *root,int theme,int format) {
    for(int i=0;i<300;i++) {
        pthread_mutex_lock(&ui_mutex);
        int ok=!strcmp(settings_profile.root,root)&&model.theme==theme&&model.key_format==format;
        pthread_mutex_unlock(&ui_mutex); if(ok)return; profile_pause(10000000);
    }
    assert(!"profile worker failed to apply selected user");
}
static void wait_saved(const char *root,int theme) {
    for(int i=0;i<300;i++) {
        struct xz_settings s;
        if(!xz_settings_load(root,&s)&&s.theme==theme)return; profile_pause(10000000);
    }
    assert(!"profile worker failed to save selected user");
}
static void wait_status(const char *expected) {
    for(int i=0;i<300;i++) {
        pthread_mutex_lock(&ui_mutex);
        int ok=model.settings_status&&!strcmp(model.settings_status,expected);
        pthread_mutex_unlock(&ui_mutex); if(ok)return; profile_pause(10000000);
    }
    fprintf(stderr,"expected status %s, got %s\n",expected,model.settings_status?model.settings_status:"(null)");
    assert(!"profile worker did not report the expected status");
}
static void wait_internal(int theme,int format) {
    for(int i=0;i<300;i++) {
        struct xz_settings s;
        if(xz_settings_internal_load(&s,NULL)==XZ_INTERNAL_SETTINGS_OK&&s.theme==theme&&s.key_format==format)return;
        profile_pause(10000000);
    }
    assert(!"the XZ did not remember the applied settings");
}
static void clean_internal(const char *root) {
    char path[1100];
    static const char *names[]={"preferences.0","preferences.1","preferences.lock"};
    for(unsigned i=0;i<3;i++){snprintf(path,sizeof(path),"%s/VJ.Tools/%s",root,names[i]);unlink(path);}
    snprintf(path,sizeof(path),"%s/VJ.Tools",root);rmdir(path);rmdir(root);
}
static void clean(const char *root) {
    char path[1100];snprintf(path,sizeof(path),"%s/VJ.Tools/XZ-Mods.cfg",root);unlink(path);
    snprintf(path,sizeof(path),"%s/VJ.Tools",root);rmdir(path);rmdir(root);
}
int main(void) {
    char a[]="/dev/shm/xz-runtime-profile-a-XXXXXX",b[]="/dev/shm/xz-runtime-profile-b-XXXXXX";
    char internal[]="/dev/shm/xz-runtime-internal-XXXXXX";
    assert(mkdtemp(a)&&mkdtemp(b)&&mkdtemp(internal));xz_settings_internal_test_root(internal);
    struct xz_settings s;xz_settings_default(&s);
    s.theme=4;s.key_format=2;s.key_feedback=0;s.stems_overlay=XZ_STEM_LAYOUT_OUTER;s.metrics_enabled=1;
    assert(!xz_settings_save(a,&s));
    /* Boot restoration remains explicit; browsing must not repeat it. */
    memset(&model,0,sizeof(model));profile_apply_locked(&s,"explicit-profile");assert(xz_settings_internal_save(&s)==XZ_INTERNAL_SETTINGS_OK);
    s.theme=2;s.key_format=1;s.key_feedback=1;s.stems_overlay=XZ_STEM_LAYOUT_BELOW;s.metrics_enabled=0;
    assert(!xz_settings_save(b,&s));
    snprintf(fixture_usb[0],1024,"%s",a);snprintf(fixture_usb[1],1024,"%s",b);
    memset(&settings_profile,0,sizeof(settings_profile));settings_slot=-1;
    settings_pending=settings_readonly=0;settings_running=1;
    __atomic_store_n(&profile_slot,~0u,__ATOMIC_RELAXED);
    assert(!pthread_create(&settings_thread,NULL,settings_writer,NULL));wait_profile(a,4,2);
    uint32_t profile_count=settings_profile_applies;
    /* Engine absent, load transition, and either loaded deck all keep controls. */
    for(int mode=0;mode<4;mode++){
        pthread_mutex_lock(&ui_mutex);audio_available=mode!=0;fixture_loaded=mode<2?0:1<<(mode-2);pthread_mutex_unlock(&ui_mutex);
        for(int slot=1;slot>=0;slot--){
            xz_ui_runtime_profile_usb((unsigned)slot);wait_profile(slot?b:a,4,2);
            pthread_mutex_lock(&ui_mutex);
            int kept=model.stems_overlay==XZ_STEM_LAYOUT_OUTER&&model.metrics_enabled&&model.key_feedback==0&&settings_profile_applies==profile_count;
            pthread_mutex_unlock(&ui_mutex);
            if(!kept){fprintf(stderr,"FAIL browsing USB %d mode %d restored live settings\n",slot+1,mode);assert(kept);}
        }
    }
    /* A source change during a queued edit cannot lose the edit or write B. */
    pthread_mutex_lock(&ui_mutex);model.theme=7;settings_queue();xz_ui_runtime_profile_usb(1);pthread_mutex_unlock(&ui_mutex);
    wait_profile(b,7,2);wait_internal(7,2);wait_status("SAVED ON THIS XZ / USB CHANGED");
    assert(!xz_settings_load(a,&s)&&s.theme==4);assert(!xz_settings_load(b,&s)&&s.theme==2);
    pthread_mutex_lock(&ui_mutex);model.theme=8;settings_queue();pthread_mutex_unlock(&ui_mutex);
    wait_saved(b,8);wait_internal(8,2);wait_status("SAVED TO USB 2");
    struct xz_usb_profile old;pthread_mutex_lock(&ui_mutex);old=settings_profile;assert(settings_flash_at);pthread_mutex_unlock(&ui_mutex);
    xz_ui_runtime_profile_usb(0);wait_profile(a,8,2);
    pthread_mutex_lock(&ui_mutex);
    settings_request.profile=old;xz_settings_default(&settings_request.values);settings_request.values.theme=11;
    settings_request.revision=0;settings_pending=1;pthread_cond_signal(&settings_changed);pthread_mutex_unlock(&ui_mutex);
    profile_pause(100000000);assert(!xz_settings_load(a,&s)&&s.theme==4);assert(!xz_settings_load(b,&s)&&s.theme==8);wait_internal(8,2);
    pthread_mutex_lock(&fixture_mutex);fixture_usb[0][0]=0;pthread_mutex_unlock(&fixture_mutex);
    pthread_cond_signal(&settings_changed);wait_profile("",8,2);wait_status("USB REMOVED: SETTINGS KEPT");
    pthread_mutex_lock(&ui_mutex);model.theme=9;settings_queue();pthread_mutex_unlock(&ui_mutex);
    wait_status("SAVED ON THIS XZ / NO USB");wait_internal(9,2);
    clean(a);assert(!mkdir(a,0755));
    pthread_mutex_lock(&fixture_mutex);snprintf(fixture_usb[0],1024,"%s",a);pthread_mutex_unlock(&fixture_mutex);
    pthread_cond_signal(&settings_changed);wait_profile(a,9,2);wait_status("USB 1 HAS NO SETTINGS: KEPT CURRENT");
    assert(xz_settings_load(a,&s)==1);
    pthread_mutex_lock(&ui_mutex);model.theme=10;settings_queue();pthread_mutex_unlock(&ui_mutex);wait_saved(a,10);wait_internal(10,2);
    char cfg[1100];snprintf(cfg,sizeof(cfg),"%s/VJ.Tools/XZ-Mods.cfg",b);
    FILE *file=fopen(cfg,"w");assert(file);fputs("XZ_MODS_SETTINGS 1\nstems=7\n",file);fclose(file);
    xz_ui_runtime_profile_usb(1);wait_profile(b,10,2);wait_status("USB 2 SETTINGS INVALID: KEPT CURRENT");
    pthread_mutex_lock(&ui_mutex);model.theme=12;settings_queue();pthread_mutex_unlock(&ui_mutex);wait_saved(b,12);wait_internal(12,2);
    /* Explicit older profiles still apply provided fields and retain unset ones. */
    snprintf(cfg,sizeof(cfg),"%s/VJ.Tools/XZ-Mods.cfg",a);file=fopen(cfg,"w");assert(file);
    fputs("XZ_MODS_SETTINGS 1\nstems=1\ngate=0\nsmart=0\ntheme=3\nstem_page=0\nshift_pages=0\npad_feedback=1\nshift_keysync=0\n",file);fclose(file);
    xz_ui_runtime_profile_usb(0);wait_profile(a,12,2);wait_status("USB 1 SAVE TARGET READY");
    unsigned present=0;assert(!xz_settings_load_present(a,&s,&present));
    pthread_mutex_lock(&ui_mutex);struct xz_settings merged=settings_snapshot();merged.key_feedback=0;merged.wave_mode=2;
    assert(!xz_settings_overlay(&merged,&s,present));profile_apply_locked(&merged,"explicit-profile");
    assert(model.theme==3&&model.key_feedback==0&&model.wave_mode==2&&model.stems_overlay==XZ_STEM_LAYOUT_OUTER&&model.metrics_enabled);
    settings_queue();pthread_mutex_unlock(&ui_mutex);wait_saved(a,3);wait_internal(3,2);
    pthread_mutex_lock(&ui_mutex);settings_readonly=1;model.theme=7;settings_queue();assert(!settings_pending);pthread_mutex_unlock(&ui_mutex);
    assert(!xz_settings_load(a,&s)&&s.theme==3);wait_internal(3,2);
    pthread_mutex_lock(&ui_mutex);settings_running=0;pthread_cond_signal(&settings_changed);pthread_mutex_unlock(&ui_mutex);pthread_join(settings_thread,NULL);
    char diagnostics[8192];struct xz_ui_model unchanged=model;
    size_t size=xz_mods_stem_status_json_v1(diagnostics,sizeof(diagnostics));assert(size&&diagnostics[size]==0);
    assert(strstr(diagnostics,"\"layout\":3")&&strstr(diagnostics,"\"layout_origin\":\"explicit-profile\""));
    assert(strstr(diagnostics,"\"profile_apply_count\":")&&strstr(diagnostics,"\"alignment_correlation\":"));assert(!memcmp(&unchanged,&model,sizeof(model)));
    char too_small[16];memset(too_small,0x5a,sizeof(too_small));assert(!xz_mods_stem_status_json_v1(too_small,sizeof(too_small)));
    for(unsigned i=0;i<sizeof(too_small);i++)assert(too_small[i]==0x5a);
    clean(a);clean(b);clean_internal(internal);
    puts("PASS browsing selects save destination only: absent engine, load transition, either loaded deck, queued edit survives source change, stale saves rejected, internal and USB persistence, explicit profile restore, read-only diagnostics");
}
