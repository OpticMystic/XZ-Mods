/* SPDX-License-Identifier: MIT */
/* The DJ Profile worker against real fixture sticks on two fake USB ports:
   discovery, the master-deck rule, switching on disk, removal and the
   status flash. Usage: test_dj_runtime <usb1 copy> <usb2 copy> */
#define xz_settings_find_usb fixture_find_usb
#define xz_settings_mount_id fixture_mount_id
#include "../dj_runtime.c"
#undef xz_settings_find_usb
#undef xz_settings_mount_id
#include <assert.h>
#include <time.h>
#include <unistd.h>

#ifdef NDEBUG
#error "DJ runtime acceptance requires assertions; compile with -UNDEBUG"
#endif

static pthread_mutex_t fixture_mutex = PTHREAD_MUTEX_INITIALIZER;
static char fixture_usb[2][1024];
int fixture_find_usb(unsigned slot, char out[1024]) {
    pthread_mutex_lock(&fixture_mutex);
    snprintf(out, 1024, "%s", slot < 2 ? fixture_usb[slot] : "");
    pthread_mutex_unlock(&fixture_mutex);
    return out[0] ? 0 : 1;
}
unsigned long fixture_mount_id(const char *root) { (void)root; return 0; }
static void plug(int slot, const char *root) {
    pthread_mutex_lock(&fixture_mutex); snprintf(fixture_usb[slot], 1024, "%s", root ? root : ""); pthread_mutex_unlock(&fixture_mutex);
}

static pthread_mutex_t ui_lock = PTHREAD_MUTEX_INITIALIZER;
static struct xz_ui_dj dj;
/* Waits up to 5 s for cond, holding the UI lock while it is checked. */
#define WAIT(cond) do { int ok_ = 0; for (int t_ = 0; t_ < 500 && !ok_; t_++) { \
    pthread_mutex_lock(&ui_lock); ok_ = (cond); pthread_mutex_unlock(&ui_lock); \
    if (!ok_) { struct timespec p_ = { 0, 10000000 }; nanosleep(&p_, NULL); } } \
    assert(ok_ && #cond); } while (0)
static int named(int entry, const char *name) { return entry >= 0 && entry < dj.count && !strcmp(dj.entry[entry].name, name); }
static int find_name(const char *name) { for (int i = 0; i < dj.count; i++) if (!strcmp(dj.entry[i].name, name)) return i; return -1; }
static long file_size(const char *root, const char *relative) {
    char path[1200]; snprintf(path, sizeof(path), "%s/%s", root, relative);
    struct stat st; return stat(path, &st) ? -1 : (long)st.st_size;
}
static int contains(const char *root, const char *relative, const char *needle) {
    char path[1200], text[8192] = {0}; snprintf(path, sizeof(path), "%s/%s", root, relative);
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    size_t n = fread(text, 1, sizeof(text) - 1, f); fclose(f); text[n] = 0;
    return strstr(text, needle) != NULL;
}

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    const char *usb1 = argv[1], *usb2 = argv[2];
    long pdb = file_size(usb1, "PIONEER/rekordbox/export.pdb");
    assert(pdb > 0);

    /* Nothing plugged: the reader starts, finishes its first look, and lists nothing. */
    assert(!xz_dj_runtime_start(&ui_lock, &dj));
    WAIT(!dj.reading);
    pthread_mutex_lock(&ui_lock);
    assert(dj.count == 0 && dj.usb_present == 0 && dj.current == -1 && dj.pending == -1);
    xz_dj_runtime_current(-1, -1, -1); assert(dj.current == -1);
    pthread_mutex_unlock(&ui_lock);

    /* USB 1 carries the two-profile fixture, USB 2 the same stick's copy with DJ Kai active. */
    plug(0, usb1);
    WAIT(dj.count == 2);
    pthread_mutex_lock(&ui_lock);
    assert(dj.usb_present == 1 && named(0, "DJ Kai") && named(1, "Lena B"));
    assert(!dj.entry[0].stick_active && dj.entry[1].stick_active && dj.entry[0].usb == 0);
    assert(dj.entry[0].thumb && dj.entry[0].has_accent && dj.entry[0].accent == 0xff2a6d);
    assert(!strcmp(dj.entry[0].tagline, "Deep tech, Berlin") && !dj.entry[0].damaged && !dj.entry[0].locked);
    /* With no deck reporting, the header falls back to the first stick's active profile. */
    xz_dj_runtime_current(-1, -1, -1); assert(dj.current == 1);
    pthread_mutex_unlock(&ui_lock);

    assert(!xz_dj_stick_set_active(usb2, "djp_kai"));
    plug(1, usb2);
    WAIT(dj.count == 4);
    pthread_mutex_lock(&ui_lock);
    assert(dj.usb_present == 3 && dj.entry[2].usb == 1 && dj.entry[2].stick_active && named(2, "DJ Kai"));
    /* The master deck plays from USB 2, so its active profile is the one shown. */
    xz_dj_runtime_current(1, 0, 0); assert(dj.current == 2);
    xz_dj_runtime_current(0, 1, -1); assert(dj.current == 1);
    /* No master: a deck playing from a stick wins over the browser's choice. */
    xz_dj_runtime_current(-1, 1, 0); assert(dj.current == 2);
    xz_dj_runtime_current(-1, -1, 1); assert(dj.current == 2);
    /* Picking the already active profile writes nothing and says so. */
    xz_dj_runtime_pick(1); assert(dj.pending == -1 && dj.status && strstr(dj.status, "ALREADY ACTIVE"));
    xz_dj_runtime_current(-1, -1, -1); assert(dj.flash);
    /* Picking DJ Kai on USB 1 rewrites only that stick's index. */
    xz_dj_runtime_pick(0); assert(dj.pending == 0);
    /* A second pick while the first is written is refused. */
    xz_dj_runtime_pick(3); assert(dj.pending == 0);
    pthread_mutex_unlock(&ui_lock);
    WAIT(dj.pending == -1);
    pthread_mutex_lock(&ui_lock);
    assert(dj.status && !strcmp(dj.status, "DJ Kai IS ACTIVE ON USB 1"));
    assert(dj.entry[0].stick_active && !dj.entry[1].stick_active && named(find_name("DJ Kai"), "DJ Kai"));
    xz_dj_runtime_current(0, -1, -1); assert(dj.current == 0);
    pthread_mutex_unlock(&ui_lock);
    /* The pick moved USB 1 to DJ Profile 1.0 first; an older runtime still reads the old index. */
    assert(contains(usb1, "DJPROFILE/DJPROFILE.json", "\"active\": \"djp_kai\""));
    assert(contains(usb1, "DJPROFILE/VJTOOLS/index.json", "\"active\": \"djp_kai\""));
    assert(file_size(usb1, "DJPROFILE/DJ Kai/profile.json") > 0 && file_size(usb1, "DJPROFILE/VJTOOLS/profiles/djp_kai/profile.json") > 0);
    assert(file_size(usb1, "PIONEER/rekordbox/export.pdb") == pdb);
    assert(file_size(usb1, "DJPROFILE/VJTOOLS/index.json.tmp") < 0);

    /* Another tool changes USB 2 behind our back: the next poll picks it up. */
    assert(!xz_dj_stick_set_active(usb2, "djp_lena"));
    pthread_mutex_lock(&ui_lock); xz_dj_runtime_rescan(); pthread_mutex_unlock(&ui_lock);
    WAIT(dj.count == 4 && dj.entry[3].stick_active);

    /* A damaged profile.json with no profile.previous.json: the old store, still on the stick, lists Lena again
       (contract 0.1) and her hand-made-looking folder is not listed twice. */
    char path[1200]; snprintf(path, sizeof(path), "%s/DJPROFILE/Lena B/profile.json", usb2);
    FILE *f = fopen(path, "wb"); assert(f); fputs("{not json", f); fclose(f);
    pthread_mutex_lock(&ui_lock); xz_dj_runtime_rescan(); pthread_mutex_unlock(&ui_lock);
    WAIT(dj.count == 4 && !dj.reading && !strcmp(dj.entry[3].dj_id, "djp_lena"));
    pthread_mutex_lock(&ui_lock);
    assert(named(3, "Lena B") && dj.entry[3].thumb && !dj.entry[3].damaged && !dj.entry[3].folder);
    pthread_mutex_unlock(&ui_lock);

    /* Pulling USB 1 drops its profiles; the header follows what is left. */
    plug(0, NULL);
    WAIT(dj.count == 2 && dj.usb_present == 2);
    pthread_mutex_lock(&ui_lock);
    assert(dj.entry[0].usb == 1);
    xz_dj_runtime_current(0, -1, -1); assert(dj.current >= 0 && dj.entry[dj.current].usb == 1);
    pthread_mutex_unlock(&ui_lock);

    /* A stick with no profile shows as present with nothing listed. */
    char away[1200]; snprintf(path, sizeof(path), "%s/DJPROFILE", usb2); snprintf(away, sizeof(away), "%s/PUTAWAY", usb2); assert(!rename(path, away));
    pthread_mutex_lock(&ui_lock); xz_dj_runtime_rescan(); pthread_mutex_unlock(&ui_lock);
    WAIT(dj.count == 0 && dj.usb_present == 2 && !dj.reading);

    /* USB 1 back with its two profiles and a logo image the verify script put on it. */
    plug(0, usb1);
    WAIT(dj.count == 2 && dj.entry[0].usb == 0);
    /* The DJ PROFILE page: the full logo for one profile. */
    pthread_mutex_lock(&ui_lock); int kai = find_name("DJ Kai"); xz_dj_runtime_detail(kai); pthread_mutex_unlock(&ui_lock);
    WAIT(dj.detail == kai);
    pthread_mutex_lock(&ui_lock);
    assert(dj.detail_tile && dj.detail_tile[0] == (uint16_t)((255 >> 3) << 11 | (42 >> 2) << 5 | 109 >> 3));
    int lena = find_name("Lena B"); xz_dj_runtime_detail(lena);
    pthread_mutex_unlock(&ui_lock);
    WAIT(dj.detail == lena);
    pthread_mutex_lock(&ui_lock);
    assert(dj.detail_tile);
    /* The logo picker lists the image, and a pick renders it. */
    xz_dj_runtime_images(0); assert(dj.images_reading);
    pthread_mutex_unlock(&ui_lock);
    WAIT(!dj.images_reading);
    pthread_mutex_lock(&ui_lock);
    /* The profile folders' own logos come first. */
    assert(dj.images_usb == 0 && dj.image_count == 4 && !strcmp(dj.images[0], "DJPROFILE/DJ Kai/logo.png") &&
        !strcmp(dj.images[2], "Logos/me.png") && !strcmp(dj.images[3], "not-a-logo.jpg"));
    xz_dj_runtime_preview(3); assert(dj.preview_state == XZ_UI_PREVIEW_LOADING);
    pthread_mutex_unlock(&ui_lock);
    WAIT(dj.preview_state != XZ_UI_PREVIEW_LOADING);
    pthread_mutex_lock(&ui_lock);
    assert(dj.preview_state == XZ_UI_PREVIEW_FAILED && dj.preview_error && strstr(dj.preview_error, "PNG OR JPEG"));
    xz_dj_runtime_preview(2);
    pthread_mutex_unlock(&ui_lock);
    WAIT(dj.preview == 2 && dj.preview_state == XZ_UI_PREVIEW_READY);
    /* A new profile made on the XZ with that logo becomes active on USB 1. */
    static struct xz_ui edit; memset(&edit, 0, sizeof(edit));
    snprintf(edit.edit_name, sizeof(edit.edit_name), "NEW DJ"); snprintf(edit.edit_tagline, sizeof(edit.edit_tagline), "Made on the XZ");
    edit.edit_logo = 2; edit.edit_usb = 0; edit.edit_entry = -1;
    pthread_mutex_lock(&ui_lock); xz_dj_runtime_save(&edit); assert(dj.saving); pthread_mutex_unlock(&ui_lock);
    WAIT(!dj.saving && dj.count == 3);
    pthread_mutex_lock(&ui_lock);
    assert(dj.status && !strcmp(dj.status, "SAVED NEW DJ TO USB 1"));
    int made = find_name("NEW DJ");
    assert(made >= 0 && dj.entry[made].usb == 0 && dj.entry[made].stick_active && dj.entry[made].thumb && !strcmp(dj.entry[made].tagline, "Made on the XZ"));
    assert(dj.entry[made].thumb[0] == (uint16_t)((10 >> 3) << 11 | (200 >> 2) << 5 | 90 >> 3));
    /* Editing DJ Kai keeps its logo and links; only the name changes. */
    memset(&edit, 0, sizeof(edit));
    snprintf(edit.edit_id, sizeof(edit.edit_id), "djp_kai"); snprintf(edit.edit_name, sizeof(edit.edit_name), "DJ Kai Live");
    snprintf(edit.edit_tagline, sizeof(edit.edit_tagline), "Deep tech, Berlin"); edit.edit_logo = XZ_UI_LOGO_KEEP; edit.edit_usb = 0;
    xz_dj_runtime_save(&edit);
    pthread_mutex_unlock(&ui_lock);
    WAIT(!dj.saving && find_name("DJ Kai Live") >= 0);
    pthread_mutex_lock(&ui_lock);
    int live = find_name("DJ Kai Live");
    assert(dj.count == 3 && dj.entry[live].thumb && !dj.entry[live].stick_active && find_name("DJ Kai") < 0);
    xz_dj_runtime_detail(live);
    pthread_mutex_unlock(&ui_lock);
    WAIT(dj.detail == live);
    assert(contains(usb1, "DJPROFILE/DJ Kai Live/profile.json", "https://example.com/kai/press.zip"));
    assert(contains(usb1, "DJPROFILE/DJPROFILE.json", "\"DJ Kai Live\""));
    assert(file_size(usb1, "PIONEER/rekordbox/export.pdb") == pdb);
    /* A blank name is refused by the writer and nothing changes. */
    pthread_mutex_lock(&ui_lock);
    memset(&edit, 0, sizeof(edit)); snprintf(edit.edit_name, sizeof(edit.edit_name), "   "); edit.edit_logo = XZ_UI_LOGO_NONE; edit.edit_entry = -1;
    xz_dj_runtime_save(&edit);
    pthread_mutex_unlock(&ui_lock);
    WAIT(!dj.saving && dj.status && strstr(dj.status, "NOT ALLOWED"));
    pthread_mutex_lock(&ui_lock); assert(dj.count == 3); pthread_mutex_unlock(&ui_lock);

    /* Hand-made folders on USB 2, set to Blue in rekordbox: each is a profile in that colour. */
    static unsigned char logo[1 << 16]; long logo_n = file_size(usb1, "Logos/me.png");
    { char p[1200]; snprintf(p, sizeof(p), "%s/Logos/me.png", usb1); FILE *in = fopen(p, "rb"); assert(in && logo_n > 0 && logo_n < (long)sizeof(logo));
      assert(fread(logo, 1, (size_t)logo_n, in) == (size_t)logo_n); fclose(in); }
    const char *const folders[] = { "DJPROFILES", "DJPROFILES/Mia Moon", "DJPROFILES/Zed" };
    for (int i = 0; i < 3; i++) { snprintf(path, sizeof(path), "%s/%s", usb2, folders[i]); assert(!mkdir(path, 0755)); }
    const char *const logos[] = { "DJPROFILES/Mia Moon/logo.png", "DJPROFILES/Zed/zed.png" };
    for (int i = 0; i < 2; i++) { snprintf(path, sizeof(path), "%s/%s", usb2, logos[i]); f = fopen(path, "wb"); assert(f); fwrite(logo, 1, (size_t)logo_n, f); fclose(f); }
    {   static unsigned char pdb_bytes[3 * 4096]; memset(pdb_bytes, 0, sizeof(pdb_bytes));
        pdb_bytes[5] = 0x10; pdb_bytes[8] = 1; pdb_bytes[0x1c] = 19; pdb_bytes[0x1c + 8] = 1; pdb_bytes[0x1c + 12] = 2;
        pdb_bytes[4096 + 4] = 1; pdb_bytes[4096 + 8] = 19; pdb_bytes[4096 + 12] = 2; pdb_bytes[4096 + 0x1b] = 0x64;
        pdb_bytes[8192 + 4] = 2; pdb_bytes[8192 + 8] = 19; pdb_bytes[8192 + 0x1b] = 0x34;
        /* A stale row (Green) then the live one (Blue); only the live row's present bit is set. */
        pdb_bytes[8192 + 0x18] = 2; pdb_bytes[8192 + 4096 - 4] = 2; pdb_bytes[8192 + 4096 - 6] = 0; pdb_bytes[8192 + 4096 - 8] = 12;
        pdb_bytes[8192 + 0x28] = 0x80; pdb_bytes[8192 + 0x29] = 0x02; pdb_bytes[8192 + 0x28 + 9] = 5;
        pdb_bytes[8192 + 0x28 + 12] = 0x80; pdb_bytes[8192 + 0x28 + 13] = 0x02; pdb_bytes[8192 + 0x28 + 12 + 9] = 7;
        snprintf(path, sizeof(path), "%s/PIONEER/rekordbox/export.pdb", usb2); f = fopen(path, "wb"); assert(f);
        fwrite(pdb_bytes, 1, sizeof(pdb_bytes), f); fclose(f); }
    pthread_mutex_lock(&ui_lock); xz_dj_runtime_rescan(); pthread_mutex_unlock(&ui_lock);
    WAIT(dj.count == 5);
    pthread_mutex_lock(&ui_lock);
    int mia = find_name("Mia Moon"), zed = find_name("Zed");
    assert(mia >= 0 && zed == mia + 1 && dj.entry[mia].usb == 1 && dj.entry[mia].folder && !dj.entry[mia].locked);
    assert(dj.entry[mia].stick_active && !dj.entry[zed].stick_active && dj.entry[mia].thumb);
    assert(dj.entry[mia].thumb[0] == (uint16_t)((10 >> 3) << 11 | (200 >> 2) << 5 | 90 >> 3));
    assert(dj.entry[mia].has_accent && dj.entry[mia].accent == 0x1e50f0 && dj.entry[zed].accent == 0x1e50f0);
    /* USB 1 has no colour, so its profiles keep their own. */
    assert(dj.entry[find_name("DJ Kai Live")].accent == 0xff2a6d);
    xz_dj_runtime_detail(mia);
    pthread_mutex_unlock(&ui_lock);
    WAIT(dj.detail == mia && dj.detail_tile);
    /* Picking a folder makes it active by its folder name; it stays a folder. */
    pthread_mutex_lock(&ui_lock); xz_dj_runtime_pick(zed); assert(dj.pending == zed); pthread_mutex_unlock(&ui_lock);
    WAIT(dj.pending == -1);
    pthread_mutex_lock(&ui_lock);
    assert(dj.status && !strcmp(dj.status, "Zed IS ACTIVE ON USB 2"));
    zed = find_name("Zed"); mia = find_name("Mia Moon");
    assert(dj.count == 5 && dj.entry[zed].folder && dj.entry[zed].stick_active && dj.entry[zed].thumb && dj.entry[mia].folder && !dj.entry[mia].stick_active);
    pthread_mutex_unlock(&ui_lock);
    assert(contains(usb2, "DJPROFILE/DJPROFILE.json", "\"active\": \"Zed\""));
    /* Editing a folder profile saves a new profile with the folder's logo; the folder then steps aside. */
    pthread_mutex_lock(&ui_lock);
    memset(&edit, 0, sizeof(edit)); snprintf(edit.edit_name, sizeof(edit.edit_name), "Mia Moon");
    snprintf(edit.edit_tagline, sizeof(edit.edit_tagline), "Disco"); edit.edit_entry = mia; edit.edit_logo = XZ_UI_LOGO_KEEP; edit.edit_usb = 1;
    xz_dj_runtime_save(&edit);
    pthread_mutex_unlock(&ui_lock);
    WAIT(!dj.saving && find_name("Mia Moon") >= 0 && !dj.entry[find_name("Mia Moon")].folder);
    pthread_mutex_lock(&ui_lock);
    mia = find_name("Mia Moon");
    assert(dj.count == 5 && !strcmp(dj.entry[mia].tagline, "Disco") && dj.entry[mia].thumb && dj.entry[mia].accent == 0x1e50f0);
    pthread_mutex_unlock(&ui_lock);
    /* The DJ's folder became the profile in place, with the stick colour stored as its accent. */
    assert(contains(usb2, "DJPROFILES/Mia Moon/profile.json", "\"accent\": \"#1e50f0\"") && file_size(usb2, "DJPROFILES/Mia Moon/logo.png") > 0);
    /* No logo: the initial on the accent colour replaces it. */
    pthread_mutex_lock(&ui_lock);
    memset(&edit, 0, sizeof(edit)); snprintf(edit.edit_id, sizeof(edit.edit_id), "%s", dj.entry[mia].dj_id); snprintf(edit.edit_name, sizeof(edit.edit_name), "Mia Moon");
    edit.edit_entry = mia; edit.edit_logo = XZ_UI_LOGO_NONE; edit.edit_usb = 1;
    long before_logo = file_size(usb2, "DJPROFILES/Mia Moon/logo.png");
    xz_dj_runtime_save(&edit);
    pthread_mutex_unlock(&ui_lock);
    WAIT(!dj.saving);
    assert(file_size(usb2, "DJPROFILES/Mia Moon/logo.png") != before_logo);

    xz_dj_runtime_stop();
    assert(dj.count == 0 && dj.current == -1);
    puts("PASS DJ runtime: discovery on two ports, master-deck rule, pick moves the old store to 1.0 and writes only that stick, refusal while busy, external change, damaged profile.json falls back to the old store, removal, empty stick, full logo, image list + preview, create with a logo, edit keeping links, blank name refused, folder profiles in the live-row stick colour, pick a folder by name, edit adopts the folder, no logo draws the initial");
    return 0;
}
