/* SPDX-License-Identifier: MIT */
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include "dj_runtime.h"
#include "dj_profile.h"
#include "dj_profile_write.h"
#include "dj_folders.h"
#include <fcntl.h>
#include <unistd.h>
#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>

/* What the worker last saw on a port; any change rereads the stick. */
enum { SEEN_TOP, SEEN_TOPS, SEEN_STICK_FILE, SEEN_INDEX, SEEN_CARD, SEEN_FILES };
struct seen { char root[1024]; unsigned long mount; struct stat file[SEEN_FILES]; int has[SEEN_FILES]; };

static pthread_mutex_t *lock;
static struct xz_ui_dj *view;
static pthread_t thread;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static int running, created, rescan;
static struct xz_dj_stick *stick[2], *scratch;
static int present[2], reading;
/* A pick waits here until the worker writes it: the profile reference (an id, or a level 0 folder name). */
static int pick_slot = -1, pick_entry = -1;
static char pick_ref[512];
/* Each stick's rekordbox colour as 0xRRGGBB, 0 when it has none. */
static uint32_t stick_color[2];
static uint16_t folder_tile[XZ_DJ_TILE_SIDE * XZ_DJ_TILE_SIDE];
static char status[160];
/* Requests for the DJ PROFILE page and editor, each served once by the worker. */
struct detail_job { int wanted, entry, usb; char id[XZ_DJ_ID_MAX], tile[1024], logo[1024]; };
static struct detail_job detail_request;
static uint16_t detail_tile[2][XZ_DJ_TILE_SIDE * XZ_DJ_TILE_SIDE];
static int detail_buffer;
static int images_wanted = -1;
static char images_list[2][XZ_DJ_IMAGE_LIST_MAX][XZ_DJ_IMAGE_PATH];
static int images_buffer;
struct preview_job { int wanted, index, usb; char path[XZ_DJ_IMAGE_PATH]; };
static struct preview_job preview_request;
static uint16_t preview_tile[2][XZ_DJ_TILE_SIDE * XZ_DJ_TILE_SIDE];
static int preview_buffer;
struct save_job { int wanted, entry_usb, logo_usb; struct xz_dj_draft draft; char logo[XZ_DJ_IMAGE_PATH]; };
static struct save_job save_request;
static uint32_t status_at;
static int status_set;

/* "#rrggbb" as profile.json stores an accent. */
static void hex_rgb(char out[8], uint32_t rgb) {
    static const char hex[] = "0123456789abcdef";
    out[0] = '#';
    for (int i = 0; i < 6; i++) out[1 + i] = hex[rgb >> (20 - 4 * i) & 15];
    out[7] = 0;
}
static uint32_t now_ms(void) {
    struct timeval tv; gettimeofday(&tv, NULL);
    return (uint32_t)((uint64_t)tv.tv_sec * 1000u + (uint64_t)tv.tv_usec / 1000u);
}
static void say(const char *text) {
    snprintf(status, sizeof(status), "%s", text);
    view->status = status; status_at = now_ms(); status_set = 1;
}
int xz_dj_runtime_usb_of(const char *path) {
    if (!path) return -1;
    if (!strncmp(path, "/media/usb1/", 12)) return 0;
    if (!strncmp(path, "/media/usb2/", 12)) return 1;
    return -1;
}
/* Rebuilds the published list from both sticks: USB 1's profiles, then USB 2's. */
static void publish_locked(void) {
    int n = 0;
    for (int slot = 0; slot < 2; slot++) {
        const struct xz_dj_stick *s = stick[slot];
        if (!present[slot] || !s) continue;
        for (int i = 0; i < s->count && n < XZ_UI_DJ_ENTRIES; i++, n++) {
            const struct xz_dj_profile *p = &s->profile[i];
            view->entry[n] = (struct xz_ui_dj_entry){ p->name, p->tagline, p->dj_id, slot, i == s->active, 0,
                p->damaged, p->accent, p->has_accent, p->has_thumb ? p->thumb : NULL, p->kind == XZ_DJ_LEVEL0 };
        }
    }
    view->count = n;
    /* Entries may have moved: the page asks for its profile again. */
    view->detail = -1; view->detail_tile = NULL;
    view->usb_present = (unsigned)(present[0] ? 1 : 0) | (unsigned)(present[1] ? 2 : 0);
    view->reading = reading;
    if (view->pending >= n) view->pending = -1;
}
/* The profile the stick in `slot` names as active, as a view entry, or -1. */
static int active_entry(int slot) {
    if (slot < 0 || slot > 1) return -1;
    for (int i = 0; i < view->count; i++) if (view->entry[i].usb == slot && view->entry[i].stick_active) return i;
    return -1;
}
void xz_dj_runtime_current(int master_usb, int playing_usb, int browse_usb) {
    if (!view) return;
    int order[3] = { master_usb, playing_usb, browse_usb }, current = -1;
    for (int k = 0; k < 3 && current < 0; k++) current = active_entry(order[k]);
    for (int slot = 0; slot < 2 && current < 0; slot++) current = active_entry(slot);
    view->current = current;
    view->flash = status_set && (uint32_t)(now_ms() - status_at) < 3000u;
}
void xz_dj_runtime_rescan(void) {
    if (!view) return;
    rescan = 1; pthread_cond_signal(&wake);
}
void xz_dj_runtime_pick(int entry) {
    if (!view || entry < 0 || entry >= view->count || view->pending >= 0) return;
    const struct xz_ui_dj_entry *e = &view->entry[entry];
    if (e->damaged) return;
    if (e->stick_active) { char line[112]; snprintf(line, sizeof(line), "%s IS ALREADY ACTIVE ON USB %d", e->name, e->usb + 1); say(line); return; }
    const struct xz_dj_stick *s = stick[e->usb];
    int index = -1;
    for (int i = 0, n = 0; i <= entry && n < XZ_UI_DJ_ENTRIES; i++) if (view->entry[i].usb == e->usb) { if (i == entry) index = n; n++; }
    if (!s || index < 0 || index >= s->count) return;
    const struct xz_dj_profile *p = &s->profile[index];
    snprintf(pick_ref, sizeof(pick_ref), "%s", xz_dj_profile_ref(p));
    pick_slot = e->usb; pick_entry = entry; view->pending = entry;
    pthread_cond_signal(&wake);
}
static const struct xz_dj_profile *profile_of(int entry) {
    if (!view || entry < 0 || entry >= view->count) return NULL;
    const struct xz_ui_dj_entry *e = &view->entry[entry];
    const struct xz_dj_stick *s = stick[e->usb];
    if (!s) return NULL;
    for (int i = 0; i < s->count; i++) if (!strcmp(s->profile[i].dj_id, e->dj_id)) return &s->profile[i];
    return NULL;
}
void xz_dj_runtime_detail(int entry) {
    const struct xz_dj_profile *p = profile_of(entry);
    if (!p || view->detail == entry || (detail_request.wanted && detail_request.entry == entry)) return;
    detail_request.wanted = 1; detail_request.entry = entry; detail_request.usb = view->entry[entry].usb;
    snprintf(detail_request.id, sizeof(detail_request.id), "%s", p->dj_id);
    snprintf(detail_request.tile, sizeof(detail_request.tile), "%s", p->tile_path);
    snprintf(detail_request.logo, sizeof(detail_request.logo), "%s", p->logo_path);
    pthread_cond_signal(&wake);
}
void xz_dj_runtime_images(int usb) {
    if (!view || usb < 0 || usb > 1) return;
    images_wanted = usb; view->images_reading = 1; view->images_usb = usb;
    view->preview = -1; view->preview_state = XZ_UI_PREVIEW_NONE;
    pthread_cond_signal(&wake);
}
void xz_dj_runtime_preview(int image) {
    if (!view || view->images_reading || image < 0 || image >= view->image_count) return;
    if (view->preview == image && view->preview_state != XZ_UI_PREVIEW_NONE) return;
    preview_request.wanted = 1; preview_request.index = image; preview_request.usb = view->images_usb;
    snprintf(preview_request.path, sizeof(preview_request.path), "%s", view->images[image]);
    view->preview = image; view->preview_state = XZ_UI_PREVIEW_LOADING; view->preview_error = NULL;
    pthread_cond_signal(&wake);
}
void xz_dj_runtime_save(const struct xz_ui *edit) {
    if (!view || !edit || view->saving || edit->edit_usb < 0 || edit->edit_usb > 1) return;
    memset(&save_request, 0, sizeof(save_request));
    struct xz_dj_draft *d = &save_request.draft;
    snprintf(d->dj_id, sizeof(d->dj_id), "%s", edit->edit_id);
    snprintf(d->name, sizeof(d->name), "%s", edit->edit_name);
    snprintf(d->tagline, sizeof(d->tagline), "%s", edit->edit_tagline);
    /* A picked image must be the one previewed from the list read now. */
    save_request.logo_usb = -1;
    if (edit->edit_logo >= 0) {
        if (view->images_reading || edit->edit_logo >= view->image_count) { say("THE LOGO LIST CHANGED: PICK THE LOGO AGAIN"); return; }
        snprintf(save_request.logo, sizeof(save_request.logo), "%s", view->images[edit->edit_logo]);
        save_request.logo_usb = view->images_usb;
    }
    /* A hand-made folder is written in place: it becomes a profile with an id, its logo kept. */
    const struct xz_dj_profile *p = edit->edit_id[0] ? NULL : profile_of(edit->edit_entry);
    if (p && p->kind == XZ_DJ_LEVEL0 && view->entry[edit->edit_entry].usb == edit->edit_usb) snprintf(d->folder, sizeof(d->folder), "%s", p->dir);
    /* A profile made on the XZ takes the stick's rekordbox colour (spec section 11). */
    if (!edit->edit_id[0] && stick_color[edit->edit_usb]) hex_rgb(d->accent, stick_color[edit->edit_usb]);
    d->remove_logo = edit->edit_logo == XZ_UI_LOGO_NONE;
    save_request.entry_usb = edit->edit_usb; save_request.wanted = 1; view->saving = 1;
    pthread_cond_signal(&wake);
}
/* Reads one stick-relative file of at most cap bytes; -1 when missing, a symlink or larger. */
static int read_file(const char *root, const char *relative, void *out, size_t cap, size_t *got) {
    char path[1400];
    if (!relative[0] || snprintf(path, sizeof(path), "%s/%s", root, relative) >= (int)sizeof(path)) return -1;
    int fd = open(path, O_RDONLY | O_NOFOLLOW); if (fd < 0) return -1;
    size_t n = 0; ssize_t r;
    while (n < cap && (r = read(fd, (char *)out + n, cap - n)) > 0) n += (size_t)r;
    char extra; int over = n == cap && read(fd, &extra, 1) > 0;
    close(fd); *got = n; return over ? -1 : 0;
}
static const char *image_error(enum xz_dj_image_result r) {
    switch (r) {
    case XZ_DJ_IMAGE_FILE_TOO_BIG: return "IMAGE FILE IS OVER 8 MB";
    case XZ_DJ_IMAGE_NOT_IMAGE: return "NOT A PNG OR JPEG IMAGE THE XZ CAN READ";
    case XZ_DJ_IMAGE_TOO_MANY_PIXELS: return "IMAGE IS LARGER THAN 4096 X 4096";
    case XZ_DJ_IMAGE_NO_MEMORY: return "NOT ENOUGH MEMORY FOR THIS IMAGE";
    default: return "IMAGE COULD NOT BE READ FROM THE USB";
    }
}
static void write_error(char *line, size_t cap, enum xz_dj_write_result r, int usb) {
    switch (r) {
    case XZ_DJ_WRITE_INVALID: snprintf(line, cap, "NAME OR TAGLINE NOT ALLOWED: NOTHING SAVED"); break;
    case XZ_DJ_WRITE_FULL: snprintf(line, cap, "USB %d ALREADY HOLDS 16 PROFILES: NOTHING SAVED", usb); break;
    case XZ_DJ_WRITE_IMAGE: snprintf(line, cap, "THE LOGO IMAGE COULD NOT BE READ: NOTHING SAVED"); break;
    case XZ_DJ_WRITE_CHANGED: snprintf(line, cap, "USB %d CHANGED WHILE SAVING: TRY AGAIN", usb); break;
    default: snprintf(line, cap, "COULD NOT WRITE USB %d: NOTHING CHANGED", usb); break;
    }
}
static void serve_save(void) {
    struct save_job job = save_request; save_request.wanted = 0;
    pthread_mutex_unlock(lock);
    char root[1024], logo_root[1024], line[160], id[XZ_DJ_ID_MAX] = "";
    enum xz_dj_write_result r = XZ_DJ_WRITE_IO;
    int found = !xz_settings_find_usb((unsigned)job.entry_usb, root);
    if (found && job.logo_usb >= 0) {
        found = !xz_settings_find_usb((unsigned)job.logo_usb, logo_root);
        /* A path that does not fit is refused rather than cut to another file. */
        if (found) found = snprintf(job.draft.logo, sizeof(job.draft.logo), "%s/%s", logo_root, job.logo) < (int)sizeof(job.draft.logo);
    }
    if (found) {
        r = xz_dj_profile_write(root, &job.draft, id);
        /* A profile made on the XZ is the one the DJ wants: it becomes active on its stick. */
        if (r == XZ_DJ_WRITE_OK && !job.draft.dj_id[0] && xz_dj_stick_set_active(root, id) < 0) r = XZ_DJ_WRITE_IO;
    }
    if (r == XZ_DJ_WRITE_OK) snprintf(line, sizeof(line), "SAVED %.100s TO USB %d", job.draft.name, job.entry_usb + 1);
    else write_error(line, sizeof(line), r, job.entry_usb + 1);
    pthread_mutex_lock(lock);
    rescan = 1; view->saving = 0; say(line);
}
static void serve_detail(void) {
    struct detail_job job = detail_request; detail_request.wanted = 0;
    int buffer = !detail_buffer;
    pthread_mutex_unlock(lock);
    char root[1024]; size_t got = 0; int tile_ok = 0;
    if (!xz_settings_find_usb((unsigned)job.usb, root)) {
        tile_ok = job.tile[0] && !read_file(root, job.tile, detail_tile[buffer], XZ_DJ_TILE_BYTES, &got) && got == XZ_DJ_TILE_BYTES;
        char path[1400];
        /* A profile without a tile rendition is drawn from its logo. */
        if (!tile_ok && job.logo[0] && snprintf(path, sizeof(path), "%s/%s", root, job.logo) < (int)sizeof(path))
            tile_ok = xz_dj_image_tile(path, detail_tile[buffer], NULL, NULL) == XZ_DJ_IMAGE_OK ? 2 : 0;
        /* The file is little-endian RGB565. */
        if (tile_ok == 1) for (int i = 0; i < XZ_DJ_TILE_SIDE * XZ_DJ_TILE_SIDE; i++) {
            const uint8_t *b = (const uint8_t *)&detail_tile[buffer][i]; detail_tile[buffer][i] = (uint16_t)(b[0] | b[1] << 8);
        }
    }
    pthread_mutex_lock(lock);
    /* Publish only if the entry still names the same profile. */
    if (job.entry < view->count && !strcmp(view->entry[job.entry].dj_id, job.id) && view->entry[job.entry].usb == job.usb) {
        detail_buffer = buffer; view->detail = job.entry;
        view->detail_tile = tile_ok ? detail_tile[buffer] : NULL;
    }
}
static void serve_images(void) {
    int usb = images_wanted, buffer = !images_buffer; images_wanted = -1;
    pthread_mutex_unlock(lock);
    char root[1024];
    int found = xz_settings_find_usb((unsigned)usb, root) ? 0 : xz_dj_image_list(root, images_list[buffer], XZ_DJ_IMAGE_LIST_MAX);
    pthread_mutex_lock(lock);
    if (images_wanted < 0) {
        images_buffer = buffer; view->images = (const char (*)[256])images_list[buffer];
        view->image_count = found > 0 ? found : 0; view->images_usb = usb; view->images_reading = 0;
    }
}
static void serve_preview(void) {
    struct preview_job job = preview_request; preview_request.wanted = 0;
    int buffer = !preview_buffer;
    pthread_mutex_unlock(lock);
    char root[1024], path[1400]; enum xz_dj_image_result r = XZ_DJ_IMAGE_UNREADABLE;
    if (!xz_settings_find_usb((unsigned)job.usb, root) && snprintf(path, sizeof(path), "%s/%s", root, job.path) < (int)sizeof(path))
        r = xz_dj_image_tile(path, preview_tile[buffer], NULL, NULL);
    pthread_mutex_lock(lock);
    if (!preview_request.wanted && view->preview == job.index && view->images_usb == job.usb) {
        preview_buffer = buffer;
        view->preview_state = r == XZ_DJ_IMAGE_OK ? XZ_UI_PREVIEW_READY : XZ_UI_PREVIEW_FAILED;
        view->preview_tile = r == XZ_DJ_IMAGE_OK ? preview_tile[buffer] : NULL;
        view->preview_error = r == XZ_DJ_IMAGE_OK ? NULL : image_error(r);
    }
}
/* Called and returns with the lock held; each job drops it for its I/O. */
static void serve_requests(void) {
    if (save_request.wanted) serve_save();
    if (detail_request.wanted) serve_detail();
    if (images_wanted >= 0) serve_images();
    if (preview_request.wanted) serve_preview();
}
static int work_waiting(void) {
    return rescan || pick_slot >= 0 || save_request.wanted || detail_request.wanted || images_wanted >= 0 || preview_request.wanted;
}
static int same_stat(const struct stat *a, const struct stat *b) {
    return a->st_ino == b->st_ino && a->st_size == b->st_size && a->st_mtime == b->st_mtime && a->st_dev == b->st_dev;
}
static void thumb_of(const uint16_t *tile, uint16_t *thumb) {
    const int scale = XZ_DJ_TILE_SIDE / XZ_DJ_THUMB;
    for (int y = 0; y < XZ_DJ_THUMB; y++) for (int x = 0; x < XZ_DJ_THUMB; x++) {
        unsigned r = 0, g = 0, b = 0;
        for (int dy = 0; dy < scale; dy++) for (int dx = 0; dx < scale; dx++) {
            unsigned v = tile[(y * scale + dy) * XZ_DJ_TILE_SIDE + x * scale + dx], r5 = v >> 11, g6 = v >> 5 & 63, b5 = v & 31;
            r += r5 << 3 | r5 >> 2; g += g6 << 2 | g6 >> 4; b += b5 << 3 | b5 >> 2;
        }
        unsigned cells = (unsigned)(scale * scale); r = (r + cells / 2) / cells; g = (g + cells / 2) / cells; b = (b + cells / 2) / cells;
        thumb[y * XZ_DJ_THUMB + x] = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | b >> 3);
    }
}
/* Renders the logo of every profile without a tile rendition and gives every profile without an
   accent the stick's rekordbox colour (spec section 11). Returns the colour, 0 for none. */
static uint32_t finish_stick(const char *root, struct xz_dj_stick *s) {
    uint32_t rgb = 0;
    if (xz_dj_stick_color(root, &rgb)) rgb = 0;
    for (int i = 0; i < s->count; i++) {
        struct xz_dj_profile *p = &s->profile[i]; char path[1400];
        if (!p->has_thumb && p->logo_path[0] && snprintf(path, sizeof(path), "%s/%s", root, p->logo_path) < (int)sizeof(path) &&
            xz_dj_image_tile(path, folder_tile, NULL, NULL) == XZ_DJ_IMAGE_OK) { thumb_of(folder_tile, p->thumb); p->has_thumb = 1; }
        if (!p->has_accent && rgb) { p->accent = rgb; p->has_accent = 1; }
    }
    return rgb;
}
/* Fills *now for a port and says whether it differs from *before. */
static int look(int slot, struct seen *now, const struct seen *before) {
    memset(now, 0, sizeof(*now));
    if (xz_settings_find_usb((unsigned)slot, now->root)) now->root[0] = 0;
    /* FAT looks names up without case, so the fixed spellings find any spelling on the stick. Folders
       added or removed change the top folder; edits inside a profile folder wait for CHECK. */
    static const char *const watched[SEEN_FILES] = { "DJPROFILE", "DJPROFILES", "DJPROFILE/DJPROFILE.json",
        "DJPROFILE/VJTOOLS/index.json", "VJ.Tools/Branding/profile.json" };
    int changed = strcmp(now->root, before->root) != 0;
    if (now->root[0]) {
        now->mount = xz_settings_mount_id(now->root);
        changed = changed || now->mount != before->mount;
        for (int k = 0; k < SEEN_FILES; k++) {
            char path[1100]; snprintf(path, sizeof(path), "%s/%s", now->root, watched[k]);
            now->has[k] = !lstat(path, &now->file[k]);
            changed = changed || now->has[k] != before->has[k] || (now->has[k] && !same_stat(&now->file[k], &before->file[k]));
        }
    }
    return changed;
}
static void *worker(void *unused) {
    (void)unused;
    static struct seen seen[2], now;
    pthread_mutex_lock(lock);
    while (running) {
        serve_requests();
        int force = rescan, slot_pick = pick_slot, entry_pick = pick_entry; rescan = 0;
        char ref[512]; snprintf(ref, sizeof(ref), "%s", pick_ref);
        pick_slot = -1; pick_entry = -1;
        pthread_mutex_unlock(lock);
        int wrote = 2;
        if (slot_pick >= 0) {
            char root[1024];
            wrote = xz_settings_find_usb((unsigned)slot_pick, root) ? -1 : xz_dj_stick_set_active(root, ref);
        }
        for (int slot = 0; slot < 2; slot++) {
            int changed = look(slot, &now, &seen[slot]) || force || slot == slot_pick;
            if (!changed) continue;
            int rc = 1;
            if (now.root[0]) {
                pthread_mutex_lock(lock); reading = 1; view->reading = 1; pthread_mutex_unlock(lock);
                rc = xz_dj_stick_read(now.root, scratch);
            }
            uint32_t rgb = rc >= 0 && now.root[0] ? finish_stick(now.root, scratch) : 0;
            pthread_mutex_lock(lock);
            /* A read error keeps what was shown and tries again next poll. */
            if (rc >= 0) {
                struct xz_dj_stick *old = stick[slot]; stick[slot] = scratch; scratch = old;
                present[slot] = now.root[0] != 0 && rc == 0 ? 1 : now.root[0] != 0 ? 2 : 0;
                if (present[slot] == 2) stick[slot]->count = 0;
                stick_color[slot] = now.root[0] ? rgb : 0;
                seen[slot] = now;
            }
            reading = 0;
            publish_locked();
            pthread_mutex_unlock(lock);
        }
        pthread_mutex_lock(lock);
        if (slot_pick >= 0) {
            char line[112];
            const char *name = entry_pick >= 0 && entry_pick < view->count ? view->entry[entry_pick].name : "PROFILE";
            for (int i = 0; i < view->count; i++) if (view->entry[i].usb == slot_pick && view->entry[i].stick_active) name = view->entry[i].name;
            if (wrote >= 0) snprintf(line, sizeof(line), "%s IS ACTIVE ON USB %d", name, slot_pick + 1);
            else snprintf(line, sizeof(line), "COULD NOT WRITE USB %d: PROFILE NOT CHANGED", slot_pick + 1);
            say(line);
            view->pending = -1;
        }
        if (running && !work_waiting()) {
            struct timeval tv; gettimeofday(&tv, NULL);
            struct timespec until = { tv.tv_sec + 1, tv.tv_usec * 1000 };
            pthread_cond_timedwait(&wake, lock, &until);
        }
    }
    pthread_mutex_unlock(lock);
    return NULL;
}
int xz_dj_runtime_start(pthread_mutex_t *ui_lock, struct xz_ui_dj *out) {
    if (!ui_lock || !out || created) return -1;
    stick[0] = calloc(1, sizeof(**stick)); stick[1] = calloc(1, sizeof(**stick)); scratch = calloc(1, sizeof(*scratch));
    if (!stick[0] || !stick[1] || !scratch) { free(stick[0]); free(stick[1]); free(scratch); stick[0] = stick[1] = scratch = NULL; return -1; }
    lock = ui_lock; view = out;
    memset(view, 0, sizeof(*view)); view->current = -1; view->pending = -1;
    view->detail = -1; view->images_usb = -1; view->preview = -1;
    detail_request.wanted = 0; images_wanted = -1; preview_request.wanted = 0; save_request.wanted = 0;
    present[0] = present[1] = 0; reading = 1; view->reading = 1; status_set = 0; rescan = 1;
    pthread_mutex_lock(lock); running = 1; pthread_mutex_unlock(lock);
    if (pthread_create(&thread, NULL, worker, NULL)) {
        running = 0; free(stick[0]); free(stick[1]); free(scratch); stick[0] = stick[1] = scratch = NULL; view = NULL; return -1;
    }
    created = 1; return 0;
}
void xz_dj_runtime_stop(void) {
    if (!created) return;
    pthread_mutex_lock(lock); running = 0; pthread_cond_signal(&wake); pthread_mutex_unlock(lock);
    pthread_join(thread, NULL); created = 0;
    pthread_mutex_lock(lock);
    if (view) { view->count = 0; view->current = -1; }
    free(stick[0]); free(stick[1]); free(scratch); stick[0] = stick[1] = scratch = NULL; view = NULL;
    pthread_mutex_unlock(lock);
}
