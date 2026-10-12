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
    assert(mkdtemp(a)&&mkdtemp(b)&&mkdtemp(internal));xz_settings_internal_test_root(internal);struct xz_settings s;xz_settings_default(&s);
    s.theme=4;s.key_format=2;s.key_feedback=0;assert(!xz_settings_save(a,&s));
    s.theme=2;s.key_format=1;s.key_feedback=1;assert(!xz_settings_save(b,&s));
    snprintf(fixture_usb[0],1024,"%s",a);snprintf(fixture_usb[1],1024,"%s",b);
    memset(&model,0,sizeof(model));memset(&settings_profile,0,sizeof(settings_profile));
    settings_slot=-1;settings_pending=settings_readonly=0;settings_running=1;
    __atomic_store_n(&profile_slot,~0u,__ATOMIC_RELAXED);
    assert(!pthread_create(&settings_thread,NULL,settings_writer,NULL));
    wait_profile(a,4,2);
    pthread_mutex_lock(&ui_mutex);model.theme=3;settings_queue();pthread_mutex_unlock(&ui_mutex);
    wait_saved(a,3);
    struct xz_usb_profile old;
    pthread_mutex_lock(&ui_mutex);old=settings_profile;pthread_mutex_unlock(&ui_mutex);
    xz_ui_runtime_profile_usb(1);wait_profile(b,2,1);
    pthread_mutex_lock(&ui_mutex);assert(model.key_feedback==1);pthread_mutex_unlock(&ui_mutex);
    /* An already queued save remains bound to A, never to B. */
    pthread_mutex_lock(&ui_mutex);
    settings_request.profile=old;xz_settings_default(&settings_request.values);settings_request.values.theme=5;
    settings_pending=1;pthread_cond_signal(&settings_changed);pthread_mutex_unlock(&ui_mutex);
    profile_pause(100000000);assert(!xz_settings_load(a,&s)&&s.theme==3);
    assert(!xz_settings_load(b,&s)&&s.theme==2);
    pthread_mutex_lock(&ui_mutex);model.theme=6;settings_queue();pthread_mutex_unlock(&ui_mutex);wait_saved(b,6);
    wait_status("SAVED TO USB 2");
    pthread_mutex_lock(&ui_mutex);assert(settings_flash_at);pthread_mutex_unlock(&ui_mutex);
    wait_internal(6,1);
    xz_ui_runtime_profile_usb(0);wait_profile(a,3,2);
    pthread_mutex_lock(&ui_mutex);assert(model.key_feedback==0);pthread_mutex_unlock(&ui_mutex);
    wait_internal(3,2);
    /* Pulling the stick keeps what is applied, like the stock My Settings. */
    pthread_mutex_lock(&fixture_mutex);fixture_usb[0][0]=0;pthread_mutex_unlock(&fixture_mutex);
    pthread_cond_signal(&settings_changed);wait_profile("",3,2);wait_status("USB REMOVED: SETTINGS KEPT");
    pthread_mutex_lock(&ui_mutex);assert(model.key_feedback==0);model.theme=8;settings_queue();pthread_mutex_unlock(&ui_mutex);
    wait_status("SAVED ON THIS XZ / NO USB");wait_internal(8,2);
    /* A stick without settings keeps the current ones and takes the next change. */
    clean(a);assert(!mkdir(a,0755));
    pthread_mutex_lock(&fixture_mutex);snprintf(fixture_usb[0],1024,"%s",a);pthread_mutex_unlock(&fixture_mutex);
    pthread_cond_signal(&settings_changed);wait_profile(a,8,2);wait_status("USB 1 HAS NO SETTINGS: KEPT CURRENT");
    assert(xz_settings_load(a,&s)==1);
    pthread_mutex_lock(&ui_mutex);model.theme=9;settings_queue();pthread_mutex_unlock(&ui_mutex);wait_saved(a,9);
    wait_status("SAVED TO USB 1");
    /* An invalid record is never applied and never resets the XZ either. */
    xz_ui_runtime_profile_usb(1);wait_profile(b,6,1);
    char cfg[1100];snprintf(cfg,sizeof(cfg),"%s/VJ.Tools/XZ-Mods.cfg",a);
    FILE *broken=fopen(cfg,"w");assert(broken);fputs("XZ_MODS_SETTINGS 1\nstems=7\n",broken);fclose(broken);
    xz_ui_runtime_profile_usb(0);wait_status("USB 1 SETTINGS INVALID: KEPT CURRENT");
    pthread_mutex_lock(&ui_mutex);assert(model.theme==6&&model.key_format==1);pthread_mutex_unlock(&ui_mutex);
    /* An older record only sets what it wrote: explicit Off and Stems mode stay applied. */
    pthread_mutex_lock(&fixture_mutex);fixture_usb[0][0]=0;pthread_mutex_unlock(&fixture_mutex);
    pthread_cond_signal(&settings_changed);wait_status("USB REMOVED: SETTINGS KEPT");
    pthread_mutex_lock(&ui_mutex);
    struct xz_settings applied=settings_snapshot();applied.key_feedback=0;applied.wave_mode=2;profile_apply_locked(&applied);
    pthread_mutex_unlock(&ui_mutex);
    broken=fopen(cfg,"w");assert(broken);
    fputs("XZ_MODS_SETTINGS 1\nstems=1\ngate=0\nsmart=0\ntheme=3\nstem_page=0\nshift_pages=0\npad_feedback=1\nshift_keysync=0\n",broken);fclose(broken);
    pthread_mutex_lock(&fixture_mutex);snprintf(fixture_usb[0],1024,"%s",a);pthread_mutex_unlock(&fixture_mutex);
    pthread_cond_signal(&settings_changed);wait_profile(a,3,1);wait_status("USB 1 SETTINGS LOADED");
    pthread_mutex_lock(&ui_mutex);assert(model.key_feedback==0&&model.wave_mode==2);pthread_mutex_unlock(&ui_mutex);
    wait_internal(3,1);
    pthread_mutex_lock(&ui_mutex);settings_readonly=1;model.theme=7;settings_queue();assert(!settings_pending);pthread_mutex_unlock(&ui_mutex);
    assert(!xz_settings_load(a,&s)&&s.theme==3);wait_internal(3,1);
    pthread_mutex_lock(&ui_mutex);settings_running=0;pthread_cond_signal(&settings_changed);pthread_mutex_unlock(&ui_mutex);
    pthread_join(settings_thread,NULL);unlink(cfg);clean(a);clean(b);clean_internal(internal);
    puts("PASS actual profile worker: auto-discovery, USB selection, two users, save capture, stale-save rejection, removal keeps settings, new and invalid sticks keep settings, older records keep explicit Off and unset fields, XZ remembers last applied, save flash and read-only trial");
}
