#define _POSIX_C_SOURCE 200809L
#include "../usb_profile.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static void clean(const char *root) {
    char path[1100];snprintf(path,sizeof(path),"%s/VJ.Tools/XZ-Mods.cfg",root);unlink(path);
    snprintf(path,sizeof(path),"%s/VJ.Tools",root);rmdir(path);rmdir(root);
}
int main(void) {
    char a[]="/dev/shm/xz-profile-a-XXXXXX",b[]="/dev/shm/xz-profile-b-XXXXXX";
    assert(mkdtemp(a)&&mkdtemp(b));struct xz_usb_profile p={0};struct xz_settings s;
    assert(xz_usb_profile_load(&p,a,0,&s,NULL)==1);s.ui_interface=XZ_INTERFACE_CLASSIC;s.classic_theme=4;s.theme=4;s.key_format=2;s.browse_traffic=0;
    uint64_t a_generation=p.generation;assert(!xz_usb_profile_save(&p,a_generation,&s));
    assert(xz_usb_profile_load(&p,b,0,&s,NULL)==1&&s.theme==xz_settings_look(XZ_INTERFACE_MODERN,0)&&s.key_format==0&&s.browse_traffic);
    s.ui_interface=XZ_INTERFACE_CLASSIC;s.classic_theme=2;s.theme=2;assert(xz_usb_profile_save(&p,a_generation,&s)<0);
    assert(!xz_usb_profile_save(&p,p.generation,&s));
    assert(!xz_usb_profile_load(&p,a,0,&s,NULL)&&s.theme==4&&s.key_format==2&&!s.browse_traffic);
    uint64_t saved=p.generation;xz_usb_profile_removed(&p);assert(xz_usb_profile_save(&p,saved,&s)<0);
    char path[1100];snprintf(path,sizeof(path),"%s/VJ.Tools/XZ-Mods.cfg",b);
    FILE *f=fopen(path,"w");assert(f);fputs("bad record",f);assert(!fclose(f));
    assert(xz_usb_profile_load(&p,b,0,&s,NULL)<0&&s.theme==xz_settings_look(XZ_INTERFACE_MODERN,0)&&s.key_format==0&&s.browse_traffic);
    clean(a);clean(b);puts("PASS two user profiles, new-stick defaults, stale-save refusal, removal and invalid-profile isolation");
}
