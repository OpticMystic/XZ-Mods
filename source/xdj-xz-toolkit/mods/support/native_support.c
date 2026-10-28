/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include "native_support.h"
#include "loader_identity.h"
#include "../clock_compat.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SUPPORT_BIN "/dev/shm/xz-support"
#define SUPPORT_STATE_DIR "/dev/shm/xz-support-state"
#define MIN_VALID_EPOCH 1767225600LL

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static struct xz_support_model state = {
    .phase = XZ_SUPPORT_IDLE,
    .status = "PREPARE A SANITIZED DIAGNOSTIC DRAFT TO PREVIEW"
};
static char cached_draft[262144];
#ifndef XZ_DIARY_VERSION
#define XZ_DIARY_VERSION "unknown"
#endif
/* The loader's identity, read once at start. Its key never leaves this module except on the helper's stdin. */
static struct xz_loader_identity identity;
/* The report being sent; a retry keeps it so the Worker and the app see one report, not two. */
static char report_id[33];
static size_t cached_draft_len = 0;
static pid_t pending_helper;
#ifndef XZ_SUPPORT_WORKER_TIMEOUT_MS
#define XZ_SUPPORT_WORKER_TIMEOUT_MS 45000u
#endif
static uint64_t worker_millis(void) {
    struct timespec t;
    if (xz_clock_gettime_compat(CLOCK_MONOTONIC, &t)) return 0;
    return (uint64_t)t.tv_sec * 1000u + (uint32_t)t.tv_nsec / 1000000u;
}
static void reap_helper_locked(void) {
    if (!pending_helper) return;
    int status;
    pid_t got = waitpid(pending_helper, &status, WNOHANG);
    if (got == pending_helper || (got < 0 && errno == ECHILD)) pending_helper = 0;
}

static const char *category_slug(int idx) {
    static const char *const slugs[4] = {"bridge", "other", "audio", "display"};
    return slugs[idx >= 0 && idx < 4 ? idx : 0];
}

static const char *summary_title(int idx) {
    static const char *const titles[4] = {
        "Four-deck bridge or jog scrub issue on CH3/CH4",
        "Deck transport or jog unresponsive after deck switch",
        "USB media or library not recognized on device",
        "Display, menu, or runtime issue on XDJ-XZ"
    };
    return titles[idx >= 0 && idx < 4 ? idx : 0];
}

static const char *summary_body(int idx) {
    static const char *const bodies[4] = {
        "Prepared on-device via XZ Mods Support wizard (category: four-deck bridge / jog).",
        "Prepared on-device via XZ Mods Support wizard (category: deck switch / transport).",
        "Prepared on-device via XZ Mods Support wizard (category: USB media / library).",
        "Prepared on-device via XZ Mods Support wizard (category: display / menu / runtime)."
    };
    return bodies[idx >= 0 && idx < 4 ? idx : 0];
}

static int helper_ready(void) {
    const char *override_bin = getenv("XZ_SUPPORT_BIN");
    const char *bin = (override_bin && override_bin[0] == '/') ? override_bin : SUPPORT_BIN;
    return access(bin, X_OK) == 0;
}

static const char *state_dir_path(void) {
    const char *override_dir = getenv("XZ_SUPPORT_STATE_DIR");
    return (override_dir && override_dir[0] == '/') ? override_dir : SUPPORT_STATE_DIR;
}

static int usb_ready(void) {
    const char *usb = getenv("XZ_MODS_USB");
    return usb && usb[0] == '/' && access(usb, W_OK) == 0;
}

static int read_small_file(const char *path, char *buf, size_t cap) {
    if (!buf || cap == 0) return -1;
    buf[0] = 0;
    int fd = open(path, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) return -1;
    size_t total = 0;
    while (total + 1 < cap) {
        ssize_t n = read(fd, buf + total, cap - 1 - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            close(fd);
            buf[0] = 0;
            return -1;
        }
        if (n == 0) break;
        total += (size_t)n;
    }
    close(fd);
    buf[total] = 0;
    return (int)total;
}

static int check_clock_ready(void) {
    time_t now = time(NULL);
    if ((long long)now >= MIN_VALID_EPOCH) return 1;
    char path[512], buf[64];
    snprintf(path, sizeof(path), "%s/clock.txt", state_dir_path());
    if (read_small_file(path, buf, sizeof(buf)) > 0) {
        long long v = strtoll(buf, NULL, 10);
        if (v >= MIN_VALID_EPOCH) return 1;
    }
    return 0;
}

static int parse_meta_line(const char *meta, const char *key, char *out, size_t cap) {
    size_t klen = strlen(key);
    const char *p = meta;
    while (*p) {
        const char *eol = strpbrk(p, "\r\n");
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        if (len > klen + 1 && strncmp(p, key, klen) == 0 && p[klen] == '=') {
            size_t vlen = len - (klen + 1);
            if (vlen >= cap) vlen = cap - 1;
            memcpy(out, p + klen + 1, vlen);
            out[vlen] = 0;
            return 0;
        }
        if (!eol) break;
        p = eol + 1;
    }
    return -1;
}

static uint32_t count_preview_pages_locked(void) {
    if (cached_draft_len == 0) return 1;
    uint32_t lines = 0;
    size_t pos = 0;
    const size_t width = XZ_SUPPORT_LINE_CHARS - 4;
    while (pos < cached_draft_len) {
        const char *nl = memchr(cached_draft + pos, '\n', cached_draft_len - pos);
        size_t seg = nl ? (size_t)(nl - (cached_draft + pos)) : (cached_draft_len - pos);
        uint32_t chunks = (uint32_t)((seg + width - 1) / width);
        if (chunks == 0) chunks = 1;
        lines += chunks;
        pos += seg + (nl ? 1 : 0);
    }
    uint32_t pages = (lines + XZ_SUPPORT_PREVIEW_LINES - 1) / XZ_SUPPORT_PREVIEW_LINES;
    return pages ? pages : 1;
}

static void fill_preview_page_locked(struct xz_support_model *out, int requested_page) {
    for (int i = 0; i < XZ_SUPPORT_PREVIEW_LINES; i++) out->preview[i][0] = 0;
    uint32_t pages = count_preview_pages_locked();
    out->page_count = pages;
    int page = requested_page < 0 ? 0 : requested_page;
    if ((uint32_t)page >= pages) page = (int)pages - 1;
    out->page_index = (uint32_t)page;
    if (cached_draft_len == 0) {
        snprintf(out->preview[0], XZ_SUPPORT_LINE_CHARS, "NO REPORT PREPARED YET.");
        return;
    }
    uint32_t start_line = (uint32_t)page * XZ_SUPPORT_PREVIEW_LINES;
    uint32_t cur_line = 0;
    uint32_t written = 0;
    size_t pos = 0;
    const size_t width = XZ_SUPPORT_LINE_CHARS - 4;
    while (pos < cached_draft_len && written < XZ_SUPPORT_PREVIEW_LINES) {
        const char *nl = memchr(cached_draft + pos, '\n', cached_draft_len - pos);
        size_t seg = nl ? (size_t)(nl - (cached_draft + pos)) : (cached_draft_len - pos);
        if (seg > 0 && cached_draft[pos + seg - 1] == '\r') seg--;
        size_t off = 0;
        do {
            size_t take = seg - off;
            if (take > width) take = width;
            if (cur_line >= start_line && written < XZ_SUPPORT_PREVIEW_LINES) {
                memcpy(out->preview[written], cached_draft + pos + off, take);
                out->preview[written][take] = 0;
                for (size_t k = 0; k < take; k++) {
                    unsigned char ch = (unsigned char)out->preview[written][k];
                    if (ch < 32 || ch > 126) out->preview[written][k] = ' ';
                }
                written++;
            }
            cur_line++;
            off += take;
        } while (off < seg && written < XZ_SUPPORT_PREVIEW_LINES);
        pos += (nl ? (size_t)(nl - (cached_draft + pos)) + 1 : cached_draft_len - pos);
    }
}

static void reload_draft_locked(int bump_revision) {
    char path[512], meta[2048];
    const char *sdir = state_dir_path();
    snprintf(path, sizeof(path), "%s/draft.meta", sdir);
    if (read_small_file(path, meta, sizeof(meta)) <= 0) return;
    char id[33] = {0}, sha[65] = {0}, bytes_s[32] = {0}, red_s[32] = {0};
    if (parse_meta_line(meta, "report_id", id, sizeof(id)) == 0 &&
        parse_meta_line(meta, "payload_sha256", sha, sizeof(sha)) == 0) {
        parse_meta_line(meta, "payload_bytes", bytes_s, sizeof(bytes_s));
        parse_meta_line(meta, "redactions", red_s, sizeof(red_s));
        snprintf(state.report_id, sizeof(state.report_id), "%s", id);
        snprintf(state.payload_sha256, sizeof(state.payload_sha256), "%s", sha);
        state.payload_bytes = (uint32_t)strtoul(bytes_s, NULL, 10);
        state.redactions = (uint32_t)strtoul(red_s, NULL, 10);
        snprintf(state.github_url, sizeof(state.github_url),
                 "https://xz-mods-support.will-026.workers.dev/r/%s/github", id);
        snprintf(state.export_path, sizeof(state.export_path),
                 "VJTOOLS/support-reports/xz-support-report-%s.json", id);
        if (bump_revision || state.draft_revision == 0) state.draft_revision++;
    }
    snprintf(path, sizeof(path), "%s/draft.json", sdir);
    int n = read_small_file(path, cached_draft, sizeof(cached_draft));
    cached_draft_len = n > 0 && (uint32_t)n == state.payload_bytes ? (size_t)n : 0;
    if (!cached_draft_len) {
        state.report_id[0] = 0;
        state.payload_sha256[0] = 0;
    }
}

void xz_support_identity_load(const char *usb_root) {
    struct xz_loader_identity next;
    xz_loader_identity_read(usb_root, &next);
    pthread_mutex_lock(&mutex);
    identity = next;
    state.loader_state = next.state;
    snprintf(state.loader_id, sizeof(state.loader_id), "%s", next.state == XZ_LOADER_OK ? next.id : "");
    pthread_mutex_unlock(&mutex);
    xz_loader_identity_clear(&next);
}

static int fresh_report_id(char out[33]) {
    unsigned char bytes[16];
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t n = read(fd, bytes, sizeof(bytes));
    close(fd);
    if (n != (ssize_t)sizeof(bytes)) return -1;
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(bytes); i++) { out[2 * i] = hex[bytes[i] >> 4]; out[2 * i + 1] = hex[bytes[i] & 15]; }
    out[32] = 0;
    return 0;
}

static const char *const topic_words[XZ_SUPPORT_TOPICS] = {"GOT_WORSE", "NOT_WORKING", "WORK_DIFFERENTLY", "NEW_IDEA", "HOW_DO_I"};
static const char *const entry_words[XZ_SUPPORT_ENTRIES] = {"EXTRAS", "ALERT", "UPDATES"};
static const char *const place_words[XZ_SUPPORT_PLACES] = {"UNKNOWN", "PLAY", "BROWSE", "PLAYLIST", "TAG_LIST", "SEARCH",
    "INFO", "SHORTCUT", "UTILITY", "SOURCE", "STOCK_MENU", "TRACK_LIST", "GRID_ADJUST", "TAKEOVER", "GAME", "OTHER"};

/* The helper's request (support_tool.c cmd_pending). Values are allowlisted again on the helper's side. */
static int report_request(const struct xz_support_report *r, const char *id, const char *usb, char *out, size_t cap) {
    char text[XZ_SUPPORT_TEXT_CHARS + 1], key[65] = "";
    size_t n = 0;
    for (; n < XZ_SUPPORT_TEXT_CHARS && r->text[n]; n++) {
        unsigned char c = (unsigned char)r->text[n];
        text[n] = c >= 32 && c <= 126 ? (char)c : ' ';
    }
    text[n] = 0;
    static const char hex[] = "0123456789abcdef";
    int keyed = identity.state == XZ_LOADER_OK;
    if (keyed) {
        for (size_t i = 0; i < XZ_LOADER_KEY_BYTES; i++) { key[2 * i] = hex[identity.key[i] >> 4]; key[2 * i + 1] = hex[identity.key[i] & 15]; }
        key[64] = 0;
    }
    unsigned alert[3];
    for (int i = 0; i < 3; i++) {
        unsigned a = r->alert[i];
        alert[i] = a == 7206 || a == 8302 || a == 8304 || a == 8305 || a == 8306 || a == 8307 ? a : 0;
    }
    int w = snprintf(out, cap,
        "xz-pending-request/1\nreport_id=%s\ntopic=%s\ntext=%s\nversion=%s\ninterface=%s\nbefore=%s\nentry=%s\n"
        "deck_view=%d\nalert=%u,%u,%u\n",
        id, r->topic >= 0 && r->topic < XZ_SUPPORT_TOPICS ? topic_words[r->topic] : "", text, XZ_DIARY_VERSION,
        r->ui_interface ? "MODERN" : "CLASSIC", place_words[r->before >= 0 && r->before < XZ_SUPPORT_PLACES ? r->before : 0],
        entry_words[r->entry >= 0 && r->entry < XZ_SUPPORT_ENTRIES ? r->entry : 0],
        r->deck_view >= 0 && r->deck_view <= 2 ? r->deck_view : 0, alert[0], alert[1], alert[2]);
    if (w > 0 && (size_t)w < cap && r->have_settings) {
        int s[6];
        for (int i = 0; i < 6; i++) s[i] = r->settings[i] >= 0 && r->settings[i] <= 1000 ? r->settings[i] : 0;
        w += snprintf(out + w, cap - (size_t)w, "settings=%d,%d,%d,%d,%d,%d\n", s[0], s[1], s[2], s[3], s[4], s[5]);
        int sha_ok = strlen(r->settings_sha256) == 64;
        for (int i = 0; sha_ok && i < 64; i++) sha_ok = strchr(hex, r->settings_sha256[i]) != NULL;
        if (w > 0 && (size_t)w < cap && sha_ok) w += snprintf(out + w, cap - (size_t)w, "settings_sha256=%s\n", r->settings_sha256);
    }
    if (w > 0 && (size_t)w < cap && keyed) w += snprintf(out + w, cap - (size_t)w, "loader_id=%s\nloader_key=%s\n", identity.id, key);
    if (w > 0 && (size_t)w < cap && usb && usb[0] == '/') w += snprintf(out + w, cap - (size_t)w, "usb=%s\n", usb);
    volatile char *wipe = key;
    for (size_t i = 0; i < sizeof(key); i++) wipe[i] = 0;
    return w > 0 && (size_t)w < cap ? w : -1;
}

/* The helper's answer: the last SENT, SAVED or UNSAVED line; SAVED_FIRST says the stick copy exists. */
static void report_result_locked(const char *text, int timed_out) {
    char line[256];
    int saved_first = 0, done = 0;
    for (const char *p = text; *p;) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = 0;
        char *word[5] = {0};
        int words = 0;
        for (char *t = line; *t && words < 5;) {
            while (*t == ' ') *t++ = 0;
            if (!*t) break;
            word[words++] = t;
            while (*t && *t != ' ') t++;
        }
        if (words == 2 && !strcmp(word[0], "SAVED_FIRST")) saved_first = 1;
        else if (words == 4 && !strcmp(word[0], "SENT") && strlen(word[2]) == 11 && !strncmp(word[3], "https://", 8)) {
            state.phase = XZ_SUPPORT_SENT;
            snprintf(state.reference, sizeof(state.reference), "%s", word[2]);
            snprintf(state.tracking_url, sizeof(state.tracking_url), "%s", word[3]);
            state.result[0] = state.detail[0] = 0;
            done = 1;
        } else if (words == 3 && !strcmp(word[0], "SAVED")) {
            state.phase = XZ_SUPPORT_SAVED;
            snprintf(state.result, sizeof(state.result), "%s", word[2]);
            state.detail[0] = 0;
            done = 1;
        } else if (words == 3 && !strcmp(word[0], "UNSAVED")) {
            state.phase = XZ_SUPPORT_UNSAVED;
            snprintf(state.result, sizeof(state.result), "%s", word[1]);
            snprintf(state.detail, sizeof(state.detail), "%s", word[2]);
            done = 1;
        }
        p = eol ? eol + 1 : p + strlen(p);
    }
    if (!done && saved_first) {
        state.phase = XZ_SUPPORT_SAVED;
        snprintf(state.result, sizeof(state.result), "UNKNOWN");
        state.detail[0] = 0;
    } else if (!done) {
        state.phase = XZ_SUPPORT_UNSAVED;
        snprintf(state.result, sizeof(state.result), "%s", timed_out ? "TIMEOUT" : "FAILED");
        snprintf(state.detail, sizeof(state.detail), "UNKNOWN");
    }
    if (state.phase != XZ_SUPPORT_UNSAVED) report_id[0] = 0; /* the next report is a new one */
    snprintf(state.status, sizeof(state.status), "%s",
             state.phase == XZ_SUPPORT_SENT ? "REPORT SENT" : state.phase == XZ_SUPPORT_SAVED ? "REPORT SAVED ON THIS STICK" :
             "REPORT NOT SENT YET / TRY AGAIN");
}

void xz_support_read(struct xz_support_model *out, int requested_page) {
    if (!out) return;
    pthread_mutex_lock(&mutex);
    reap_helper_locked();
    state.ready = helper_ready();
    state.usb_writable = usb_ready();
    state.clock_ready = check_clock_ready();
    fill_preview_page_locked(&state, requested_page);
    *out = state;
    pthread_mutex_unlock(&mutex);
}

static void read_origin_url(char *out, size_t cap) {
    out[0] = 0;
    const char *env_url = getenv("XZ_SUPPORT_ORIGIN");
    if (env_url && strncmp(env_url, "https://", 8) == 0) {
        snprintf(out, cap, "%s", env_url);
        return;
    }
    const char *usb = getenv("XZ_MODS_USB");
    if (!usb || usb[0] != '/') return;
    char path[1024];
    snprintf(path, sizeof(path), "%s/VJTOOLS/support-server.txt", usb);
    if (read_small_file(path, out, cap) > 0) {
        out[strcspn(out, "\r\n")] = 0;
    }
}

static void *worker_run(void *arg) {
    struct xz_support_request_args req = *(struct xz_support_request_args *)arg;
    free(arg);

    const char *override_bin = getenv("XZ_SUPPORT_BIN");
    const char *bin = (override_bin && override_bin[0] == '/') ? override_bin : SUPPORT_BIN;
    const char *sdir = state_dir_path();
    const char *usb = getenv("XZ_MODS_USB");
    char origin[320] = {0};
    read_origin_url(origin, sizeof(origin));

    char epoch[32];
    snprintf(epoch, sizeof(epoch), "%llu", (unsigned long long)req.report_epoch);
    char *argv[8] = {0};
    argv[0] = (char *)bin;
    char request[2048];
    int request_len = -1;
    if (req.op == XZ_SUPPORT_OP_REPORT) {
        char id[33];
        pthread_mutex_lock(&mutex);
        snprintf(id, sizeof(id), "%s", report_id);
        request_len = report_request(&req.report, id, usb, request, sizeof(request));
        pthread_mutex_unlock(&mutex);
        argv[1] = "pending";
        argv[2] = (char *)sdir;
    } else if (req.op == XZ_SUPPORT_OP_PREPARE) {
        argv[1] = "prepare";
        argv[2] = (char *)sdir;
        argv[3] = (char *)category_slug(req.category);
        argv[4] = (char *)summary_title(req.summary);
        argv[5] = (char *)summary_body(req.summary);
    } else if (req.op == XZ_SUPPORT_OP_SET_CLOCK) {
        argv[1] = "set-clock";
        argv[2] = (char *)sdir;
        argv[3] = epoch;
    } else if (req.op == XZ_SUPPORT_OP_SEND) {
        argv[1] = "send";
        argv[2] = (char *)sdir;
        argv[3] = req.expected_sha256;
        if (origin[0]) argv[4] = origin;
    } else if (req.op == XZ_SUPPORT_OP_CHECK) {
        argv[1] = "check";
        argv[2] = (char *)sdir;
        if (origin[0]) argv[3] = origin;
    } else if (req.op == XZ_SUPPORT_OP_EXPORT) {
        argv[1] = "export";
        argv[2] = (char *)sdir;
        argv[3] = req.expected_sha256;
        argv[4] = (char *)(usb ? usb : "");
    }

    extern char **environ;
    size_t count = 0;
    while (environ && environ[count]) count++;
    char **env = calloc(count + 2, sizeof(*env));
    size_t out_env = 0;
    if (env) {
        for (size_t i = 0; i < count; i++) {
            if (strncmp(environ[i], "LD_PRELOAD=", 11) != 0) env[out_env++] = environ[i];
        }
        env[out_env] = "LD_PRELOAD=";
    }

    int pipefd[2] = {-1, -1}, spawn_rc = -1, wstatus = 0, timed_out = 0, child_done = 0;
    /* REPORT's request goes in on stdin over a socket pair: send() never raises SIGPIPE in the player. */
    int input[2] = {-1, -1};
    char text[4096] = {0};
    pid_t pid = 0;
    int input_ready = req.op != XZ_SUPPORT_OP_REPORT || (request_len > 0 && socketpair(AF_UNIX, SOCK_STREAM, 0, input) == 0);
    if (env && input_ready && pipe(pipefd) == 0) {
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, pipefd[1], 1);
        posix_spawn_file_actions_adddup2(&actions, pipefd[1], 2);
        posix_spawn_file_actions_addclose(&actions, pipefd[0]);
        posix_spawn_file_actions_addclose(&actions, pipefd[1]);
        if (input[1] >= 0) {
            posix_spawn_file_actions_adddup2(&actions, input[1], 0);
            posix_spawn_file_actions_addclose(&actions, input[0]);
            posix_spawn_file_actions_addclose(&actions, input[1]);
        }
        spawn_rc = posix_spawn(&pid, argv[0], &actions, NULL, argv, env);
        posix_spawn_file_actions_destroy(&actions);
        close(pipefd[1]);
        pipefd[1] = -1;
        if (input[1] >= 0) { close(input[1]); input[1] = -1; }
        if (spawn_rc == 0 && input[0] >= 0) {
            /* At most 2 KiB into an empty socket buffer: this never waits on the helper. */
            size_t sent = 0;
            while (sent < (size_t)request_len) {
                ssize_t n = send(input[0], request + sent, (size_t)request_len - sent, MSG_NOSIGNAL | MSG_DONTWAIT);
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) break;
                sent += (size_t)n;
            }
        }
        if (input[0] >= 0) { close(input[0]); input[0] = -1; }
        if (spawn_rc == 0) {
            size_t used = 0;
            char buf[512];
            int output_open = 1;
            uint64_t begun = worker_millis();
            for (;;) {
                if (!child_done) {
                    pid_t got = waitpid(pid, &wstatus, WNOHANG);
                    if (got == pid) child_done = 1;
                    else if (got < 0 && errno != EINTR) { child_done = 1; wstatus = -1; }
                }
                if (child_done && !output_open) break;
                uint64_t now = worker_millis();
                if (!begun || !now || now < begun || now-begun >= XZ_SUPPORT_WORKER_TIMEOUT_MS) {
                    timed_out = 1;
                    if (!child_done) {
                        kill(pid, SIGKILL);
                        pid_t got = waitpid(pid, &wstatus, WNOHANG);
                        if (got == pid || (got < 0 && errno == ECHILD)) child_done = 1;
                    }
                    break;
                }
                unsigned remaining = XZ_SUPPORT_WORKER_TIMEOUT_MS-(unsigned)(now-begun);
                int delay = remaining < 50 ? (int)remaining : 50;
                struct pollfd ready = { .fd=pipefd[0], .events=POLLIN };
                int polled=poll(output_open ? &ready : NULL, output_open ? 1 : 0, delay);
                if(polled<0 && errno==EINTR)continue;
                if(polled<0) { output_open=0;continue; }
                if(!polled || !output_open)continue;
                ssize_t n=read(pipefd[0],buf,sizeof(buf));
                if(n<0 && errno==EINTR)continue;
                if(n<=0){output_open=0;continue;}
                size_t keep = (size_t)n;
                if (keep > sizeof(text) - 1 - used) keep = sizeof(text) - 1 - used;
                memcpy(text + used, buf, keep);
                used += keep;
            }
        }
    }
    if (pipefd[0] >= 0) close(pipefd[0]);
    if (pipefd[1] >= 0) close(pipefd[1]);
    if (input[0] >= 0) close(input[0]);
    if (input[1] >= 0) close(input[1]);
    {
        volatile char *wipe = request;
        for (size_t i = 0; i < sizeof(request); i++) wipe[i] = 0;
    }
    free(env);

    int exit_code = (spawn_rc == 0 && child_done && wstatus != -1 && !timed_out && WIFEXITED(wstatus)) ? WEXITSTATUS(wstatus) : -1;
    if(timed_out && (req.op==XZ_SUPPORT_OP_SEND || req.op==XZ_SUPPORT_OP_CHECK)) exit_code=4;
    /* The first line, for the older operations' status; REPORT reads every line of text. */
    char msg[160];
    const char *first = strncmp(text, "XZ_SUPPORT_ERROR ", 17) == 0 ? text + 17 : text;
    snprintf(msg, sizeof(msg), "%.*s", (int)strcspn(first, "\r\n"), first);

    pthread_mutex_lock(&mutex);
    if(spawn_rc == 0 && !child_done) pending_helper=pid;
    if (req.op == XZ_SUPPORT_OP_REPORT) {
        if (spawn_rc == 0) report_result_locked(text, timed_out);
        else {
            state.phase = XZ_SUPPORT_UNSAVED;
            snprintf(state.result, sizeof(state.result), "FAILED");
            snprintf(state.detail, sizeof(state.detail), "NOT_STARTED");
            snprintf(state.status, sizeof(state.status), "COULD NOT START THE REPORT HELPER / TRY AGAIN");
        }
    } else if (req.op == XZ_SUPPORT_OP_PREPARE && exit_code == 0) {
        reload_draft_locked(1);
        state.prepared_selection = cached_draft_len ? 16u | (unsigned)(req.category & 3) | (unsigned)((req.summary & 3) << 2) : 0;
        state.phase = cached_draft_len ? XZ_SUPPORT_PREPARED : XZ_SUPPORT_ERROR;
        snprintf(state.status, sizeof(state.status), "DRAFT READY (%u BYTES, %u REDACTIONS) / REVIEW SHA256",
                 state.payload_bytes, state.redactions);
    } else if (req.op == XZ_SUPPORT_OP_SET_CLOCK && exit_code == 0) {
        state.clock_ready = 1;
        state.phase = state.report_id[0] ? XZ_SUPPORT_PREPARED : XZ_SUPPORT_IDLE;
        snprintf(state.status, sizeof(state.status), "REPORTING UTC DATE SET / PLAYBACK CLOCK UNCHANGED");
    } else if ((req.op == XZ_SUPPORT_OP_SEND || req.op == XZ_SUPPORT_OP_CHECK) && exit_code == 0 &&
               strncmp(text, "STORED ", 7) == 0) {
        reload_draft_locked(0);
        state.phase = XZ_SUPPORT_STORED;
        snprintf(state.status, sizeof(state.status), "PRIVATE LOGS STORED / SCAN QR TO OPEN GITHUB ISSUE");
    } else if (req.op == XZ_SUPPORT_OP_CHECK && exit_code == 0 && strncmp(text, "NOT_FOUND", 9) == 0) {
        state.phase = XZ_SUPPORT_PREPARED;
        snprintf(state.status, sizeof(state.status), "NO RECEIPT ON SERVER YET / RETRY SEND OR EXPORT TO USB");
    } else if (req.op == XZ_SUPPORT_OP_EXPORT && exit_code == 0) {
        reload_draft_locked(0);
        state.phase = XZ_SUPPORT_EXPORTED;
        snprintf(state.status, sizeof(state.status), "EXPORTED TO USB: %.96s", state.export_path);
    } else if (exit_code == 3) {
        state.phase = XZ_SUPPORT_NEEDS_CLOCK;
        snprintf(state.status, sizeof(state.status), "SET THE CURRENT UTC DATE AND TIME FOR REPORTING TLS");
    } else if (exit_code == 4) {
        state.phase = XZ_SUPPORT_OUTCOME_UNKNOWN;
        snprintf(state.status, sizeof(state.status), "UPLOAD OUTCOME UNKNOWN / TAP CHECK RECEIPT BEFORE RETRYING");
    } else {
        state.phase = XZ_SUPPORT_ERROR;
        snprintf(state.status, sizeof(state.status), "%.127s",
                 *msg ? msg : "SUPPORT HELPER COULD NOT COMPLETE REQUEST");
    }
    pthread_mutex_unlock(&mutex);
    return NULL;
}

int xz_support_request(const struct xz_support_request_args *req) {
    if (!req) return -1;
    pthread_mutex_lock(&mutex);
    reap_helper_locked();
    if (state.phase == XZ_SUPPORT_WORKING || pending_helper) {
        pthread_mutex_unlock(&mutex);
        return -1;
    }
    if (!helper_ready()) {
        state.phase = XZ_SUPPORT_ERROR;
        snprintf(state.status, sizeof(state.status), "SUPPORT HELPER MISSING / UPDATE USB WITH XZ MODS APP");
        pthread_mutex_unlock(&mutex);
        return -1;
    }
    if ((req->op == XZ_SUPPORT_OP_SEND || req->op == XZ_SUPPORT_OP_EXPORT) &&
        (!state.report_id[0] || req->draft_revision != state.draft_revision ||
         strcmp(req->expected_sha256, state.payload_sha256) != 0)) {
        state.phase = XZ_SUPPORT_ERROR;
        snprintf(state.status, sizeof(state.status), "DRAFT SHA256 CHANGED / REVIEW AND APPROVE PREVIEW AGAIN");
        pthread_mutex_unlock(&mutex);
        return -1;
    }
    if (req->op == XZ_SUPPORT_OP_SET_CLOCK &&
        (req->report_epoch < 1767225600ULL || req->report_epoch > 2145916799ULL)) {
        pthread_mutex_unlock(&mutex);
        return -1;
    }
    if (req->op == XZ_SUPPORT_OP_REPORT) {
        if (!req->report.retry || !report_id[0]) {
            if (fresh_report_id(report_id) != 0) {
                state.phase = XZ_SUPPORT_UNSAVED;
                snprintf(state.result, sizeof(state.result), "FAILED");
                snprintf(state.detail, sizeof(state.detail), "NO_RANDOM");
                pthread_mutex_unlock(&mutex);
                return -1;
            }
        }
        state.reference[0] = state.tracking_url[0] = state.result[0] = state.detail[0] = 0;
    }
    if (req->op == XZ_SUPPORT_OP_EXPORT && !usb_ready()) {
        state.phase = XZ_SUPPORT_ERROR;
        snprintf(state.status, sizeof(state.status), "USB NOT WRITABLE / INSERT WRITABLE XZ MODS USB");
        pthread_mutex_unlock(&mutex);
        return -1;
    }
    struct xz_support_request_args *copy = calloc(1, sizeof(*copy));
    if (!copy) {
        pthread_mutex_unlock(&mutex);
        return -1;
    }
    *copy = *req;
    state.phase = XZ_SUPPORT_WORKING;
    snprintf(state.status, sizeof(state.status), "%s",
             req->op == XZ_SUPPORT_OP_PREPARE ? "COLLECTING AND REDACTING DIAGNOSTIC SNAPSHOT..." :
             req->op == XZ_SUPPORT_OP_SET_CLOCK ? "SETTING REPORTING TLS DATE (PLAYBACK CLOCK UNCHANGED)..." :
             req->op == XZ_SUPPORT_OP_SEND ? "UPLOADING PRIVATE LOGS OVER VERIFIED TLS 1.2..." :
             req->op == XZ_SUPPORT_OP_CHECK ? "CHECKING RECEIPT WITH SUPPORT SERVICE..." :
             req->op == XZ_SUPPORT_OP_REPORT ? "SENDING YOUR REPORT..." :
             "WRITING SANITIZED REPORT BUNDLE TO USB...");
    pthread_t th;
    if (pthread_create(&th, NULL, worker_run, copy) == 0) {
        pthread_detach(th);
        pthread_mutex_unlock(&mutex);
        return 0;
    }
    free(copy);
    state.phase = XZ_SUPPORT_ERROR;
    snprintf(state.status, sizeof(state.status), "COULD NOT START SUPPORT WORKER THREAD");
    pthread_mutex_unlock(&mutex);
    return -1;
}
