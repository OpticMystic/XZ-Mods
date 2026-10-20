/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "../settings_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[]="/dev/shm/xz-internal-settings-XXXXXX";
static char slot0[1100],slot1[1100];

static unsigned long long generation(const char *path) {
    FILE *f=fopen(path,"rb");
    if(!f)return 0;
    unsigned char header[16];
    size_t got=fread(header,1,sizeof(header),f);fclose(f);
    if(got!=sizeof(header))return 0;
    unsigned long long value=0;
    for(int i=15;i>=8;i--)value=value<<8|header[i];
    return value;
}
static void flip_last_byte(const char *path) {
    FILE *f=fopen(path,"r+b");assert(f);
    assert(!fseek(f,-2,SEEK_END));int c=fgetc(f);assert(c!=EOF);
    assert(!fseek(f,-2,SEEK_END));fputc(c=='1'?'2':'1',f);fclose(f);
}
int main(void) {
    assert(mkdtemp(root));
    xz_settings_internal_test_root(root);
    snprintf(slot0,sizeof(slot0),"%s/VJ.Tools/preferences.0",root);
    snprintf(slot1,sizeof(slot1),"%s/VJ.Tools/preferences.1",root);
    struct xz_settings s,loaded;xz_settings_default(&s);

    assert(xz_settings_internal_load(&loaded,NULL)==XZ_INTERNAL_SETTINGS_MISSING);
    s.ui_interface=XZ_INTERFACE_CLASSIC;s.classic_theme=5;s.theme=5;assert(xz_settings_internal_save(&s)==XZ_INTERNAL_SETTINGS_OK);
    unsigned present=0;
    assert(xz_settings_internal_load(&loaded,&present)==XZ_INTERNAL_SETTINGS_OK&&loaded.theme==5&&present==43);
    assert(generation(slot0)==1&&access(slot1,F_OK));

    s.ui_interface=XZ_INTERFACE_CLASSIC;s.classic_theme=6;s.theme=6;assert(xz_settings_internal_save(&s)==XZ_INTERNAL_SETTINGS_OK);
    assert(generation(slot0)==1&&generation(slot1)==2);
    assert(xz_settings_internal_load(&loaded,NULL)==XZ_INTERNAL_SETTINGS_OK&&loaded.theme==6);

    assert(xz_settings_internal_save(&s)==XZ_INTERNAL_SETTINGS_OK);
    assert(generation(slot1)==2&&generation(slot0)==1);

    flip_last_byte(slot1);
    assert(xz_settings_internal_load(&loaded,NULL)==XZ_INTERNAL_SETTINGS_OK&&loaded.theme==5);
    s.ui_interface=XZ_INTERFACE_CLASSIC;s.classic_theme=7;s.theme=7;assert(xz_settings_internal_save(&s)==XZ_INTERNAL_SETTINGS_OK);
    assert(generation(slot1)==2&&generation(slot0)==1);
    assert(xz_settings_internal_load(&loaded,NULL)==XZ_INTERNAL_SETTINGS_OK&&loaded.theme==7);

    flip_last_byte(slot0);flip_last_byte(slot1);
    assert(xz_settings_internal_load(&loaded,NULL)==XZ_INTERNAL_SETTINGS_CORRUPT);
    assert(xz_settings_internal_save(&s)==XZ_INTERNAL_SETTINGS_OK&&generation(slot0)==1);
    assert(xz_settings_internal_load(&loaded,NULL)==XZ_INTERNAL_SETTINGS_OK&&loaded.theme==7);
    unlink(slot0);unlink(slot1);

    xz_settings_internal_test_low_space(1);
    assert(xz_settings_internal_save(&s)==XZ_INTERNAL_SETTINGS_LOW_SPACE);
    xz_settings_internal_test_low_space(0);

    assert(!setenv("XZ_MODS_SETTINGS_READONLY","1",1));
    assert(xz_settings_internal_save(&s)==XZ_INTERNAL_SETTINGS_UNAVAILABLE);
    assert(!unsetenv("XZ_MODS_SETTINGS_READONLY"));
    assert(xz_settings_internal_load(&loaded,NULL)==XZ_INTERNAL_SETTINGS_MISSING);

    char missing[1200];snprintf(missing,sizeof(missing),"%s/absent",root);
    xz_settings_internal_test_root(missing);
    assert(xz_settings_internal_load(&loaded,NULL)==XZ_INTERNAL_SETTINGS_UNAVAILABLE);
    assert(xz_settings_internal_save(&s)==XZ_INTERNAL_SETTINGS_UNAVAILABLE);

    char path[1200];
    snprintf(path,sizeof(path),"%s/VJ.Tools/preferences.lock",root);unlink(path);
    snprintf(path,sizeof(path),"%s/VJ.Tools",root);rmdir(path);rmdir(root);
    puts("PASS internal settings: missing, round-trip, alternating slots, unchanged-write skip, torn-slot fallback, both-corrupt recovery, low space, read-only trial, absent volume");
}
