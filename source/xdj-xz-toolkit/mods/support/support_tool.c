/* SPDX-License-Identifier: MIT */
#if !defined(_WIN32)
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#endif

#include "ca_bundle.h"
#define JSMN_STATIC
#define JSMN_STRICT
#include "../audio/vendor/jsmn/jsmn.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/platform.h"
#include "mbedtls/platform_time.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <direct.h>
#include <io.h>
typedef SOCKET sock_t;
#define INVALID_SOCK INVALID_SOCKET
#define close_sock closesocket
#define O_NOFOLLOW 0
#else
#include <arpa/inet.h>
#include <dirent.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int sock_t;
#define INVALID_SOCK (-1)
#define close_sock close
#endif

#define MIN_VALID_EPOCH 1767225600LL /* 2026-01-01T00:00:00Z */
#define MAX_VALID_EPOCH 4102444800LL /* 2100-01-01T00:00:00Z */
#define MAX_ITEM_RAW    16384u
#define MAX_DRAFT_BYTES 262144u
#define DEFAULT_ORIGIN  "https://xz-mods-support.will-026.workers.dev"

static mbedtls_time_t g_tls_epoch = 0;

static mbedtls_time_t xz_tls_time_cb(mbedtls_time_t *out) {
    mbedtls_time_t val = g_tls_epoch ? g_tls_epoch : (mbedtls_time_t)time(NULL);
    if (out) *out = val;
    return val;
}

mbedtls_ms_time_t mbedtls_ms_time(void) {
#if defined(_WIN32)
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | (uint64_t)ft.dwLowDateTime;
    return (mbedtls_ms_time_t)(t / 10000ULL);
#else
    struct timeval tv = {0};
    gettimeofday(&tv, NULL);
    return (mbedtls_ms_time_t)tv.tv_sec * 1000 + (mbedtls_ms_time_t)(tv.tv_usec / 1000);
#endif
}

struct tm *mbedtls_platform_gmtime_r(const mbedtls_time_t *tt, struct tm *tm_buf) {
    if (!tt || !tm_buf) return NULL;
    int64_t sec = (int64_t)(*tt);
    if (sec < 0) return NULL;
    int64_t days = sec / 86400;
    int64_t rem = sec % 86400;
    memset(tm_buf, 0, sizeof(*tm_buf));
    tm_buf->tm_hour = (int)(rem / 3600);
    rem %= 3600;
    tm_buf->tm_min = (int)(rem / 60);
    tm_buf->tm_sec = (int)(rem % 60);
    tm_buf->tm_wday = (int)((days + 4) % 7);

    int64_t z = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = (int64_t)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned d = doy - (153 * mp + 2) / 5 + 1;
    unsigned m = mp < 10 ? mp + 3 : mp - 9;
    y += (m <= 2);

    tm_buf->tm_year = (int)(y - 1900);
    tm_buf->tm_mon = (int)(m - 1);
    tm_buf->tm_mday = (int)d;
    return tm_buf;
}

static int random_bytes(unsigned char *buf, size_t len) {
#if defined(_WIN32)
    return BCryptGenRandom(NULL, buf, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0 ? 0 : -1;
#else
    int fd = open("/dev/urandom", O_RDONLY | O_NOFOLLOW);
    if (fd < 0) return -1;
    size_t off = 0;
    while (off < len) {
        ssize_t n = read(fd, buf + off, len - off);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { close(fd); return -1; }
        off += (size_t)n;
    }
    close(fd);
    return 0;
#endif
}

static int entropy_cb(void *data, unsigned char *output, size_t len, size_t *olen) {
    (void)data;
    if (random_bytes(output, len) != 0) return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    *olen = len;
    return 0;
}

static void to_hex(const unsigned char *in, size_t len, char *out) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = hex[in[i] >> 4];
        out[i * 2 + 1] = hex[in[i] & 0x0f];
    }
    out[len * 2] = '\0';
}

static int is_lower_hex(const char *s, size_t expected_len) {
    if (!s || strlen(s) != expected_len) return 0;
    for (size_t i = 0; i < expected_len; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return 0;
    }
    return 1;
}

static void sha256_hex(const void *data, size_t len, char out[65]) {
    unsigned char digest[32];
    mbedtls_sha256((const unsigned char *)data, len, digest, 0);
    to_hex(digest, 32, out);
}

static int test_mode_enabled(void) {
#ifdef XZ_SUPPORT_TEST
    const char *v = getenv("XZ_SUPPORT_TEST_MODE");
    return v && strcmp(v, "1") == 0;
#else
    return 0;
#endif
}

static int64_t current_system_epoch(void) {
    if (test_mode_enabled()) {
        const char *override = getenv("XZ_SUPPORT_TEST_SYSTEM_TIME");
        if (override && *override) return strtoll(override, NULL, 10);
    }
    return (int64_t)time(NULL);
}

static int ensure_dir_safe(const char *path) {
#if defined(_WIN32)
    struct _stat st;
    if (_stat(path, &st) == 0) return (st.st_mode & _S_IFDIR) ? 0 : -1;
    return _mkdir(path);
#else
    struct stat st;
    if (lstat(path, &st) == 0) {
        return (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) ? 0 : -1;
    }
    return mkdir(path, 0700);
#endif
}

static int read_file_bounded(const char *path, char *out, size_t cap, size_t *out_len, int *truncated) {
    *out_len = 0;
    if (truncated) *truncated = 0;
#if !defined(_WIN32)
    struct stat lst;
    if (lstat(path, &lst) != 0 || S_ISLNK(lst.st_mode) || !S_ISREG(lst.st_mode)) return -1;
#endif
    int flags = O_RDONLY | O_NOFOLLOW;
#if defined(_WIN32)
    flags |= _O_BINARY;
#endif
    int fd = open(path, flags);
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st) != 0 || (st.st_mode & S_IFMT) != S_IFREG) {
        close(fd);
        return -1;
    }
    size_t file_size = (size_t)st.st_size;
    if (file_size > cap - 1) {
        if (truncated) *truncated = 1;
        off_t skip = (off_t)(file_size - (cap - 1));
        lseek(fd, skip, SEEK_SET);
    }
    size_t total = 0;
    while (total < cap - 1) {
        ssize_t n = read(fd, out + total, cap - 1 - total);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        total += (size_t)n;
    }
    close(fd);
    out[total] = '\0';
    *out_len = total;
    return 0;
}

static int write_atomic(const char *dir, const char *name, const void *data, size_t len) {
    char final_path[1024], tmp_path[1024];
    snprintf(final_path, sizeof(final_path), "%s/%s", dir, name);
    snprintf(tmp_path, sizeof(tmp_path), "%s/.%s.tmp.%d", dir, name, (int)time(NULL));
#if !defined(_WIN32)
    struct stat lst;
    if (lstat(final_path, &lst) == 0 && S_ISLNK(lst.st_mode)) return -1;
    unlink(tmp_path);
    int fd = open(tmp_path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
#else
    _unlink(tmp_path);
    int fd = _open(tmp_path, _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE);
#endif
    if (fd < 0) return -1;
    const unsigned char *p = (const unsigned char *)data;
    size_t left = len;
    while (left > 0) {
        ssize_t n = write(fd, p, left);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { int e = n < 0 ? errno : ENOSPC; close(fd); unlink(tmp_path); errno = e; return -1; }
        p += (size_t)n;
        left -= (size_t)n;
    }
#if !defined(_WIN32)
    if (fsync(fd) != 0) { int e = errno; close(fd); unlink(tmp_path); errno = e; return -1; }
#endif
    if (close(fd) != 0) { int e = errno; unlink(tmp_path); errno = e; return -1; }
#if defined(_WIN32)
    _unlink(final_path);
#endif
    if (rename(tmp_path, final_path) != 0) {
        int e = errno;
        unlink(tmp_path);
        errno = e;
        return -1;
    }
    return 0;
}

static int hash_regular_file(const char *path, char out[65]) {
#if !defined(_WIN32)
    struct stat lst;
    if (lstat(path, &lst) != 0 || S_ISLNK(lst.st_mode) || !S_ISREG(lst.st_mode)) return -1;
#endif
    int flags = O_RDONLY | O_NOFOLLOW;
#if defined(_WIN32)
    flags |= _O_BINARY;
#endif
    int fd = open(path, flags);
    if (fd < 0) return -1;
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    unsigned char buf[16384];
    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { mbedtls_sha256_free(&ctx); close(fd); return -1; }
        if (n == 0) break;
        mbedtls_sha256_update(&ctx, buf, (size_t)n);
    }
    unsigned char digest[32];
    mbedtls_sha256_finish(&ctx, digest);
    mbedtls_sha256_free(&ctx);
    close(fd);
    to_hex(digest, 32, out);
    return 0;
}

static int contains_icase(const char *hay, const char *needle) {
    size_t nlen = strlen(needle);
    if (!nlen) return 0;
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < nlen && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == nlen) return 1;
    }
    return 0;
}

static int is_hex_pair(const char *p) {
    return isxdigit((unsigned char)p[0]) && isxdigit((unsigned char)p[1]);
}

static int match_mac_at(const char *p) {
    for (int i = 0; i < 6; i++) {
        if (!is_hex_pair(p + i * 3)) return 0;
        if (i < 5 && p[i * 3 + 2] != ':') return 0;
    }
    return 17;
}

static int match_ipv4_at(const char *s, const char *p) {
    if (p > s && (isalnum((unsigned char)p[-1]) || p[-1] == '.')) return 0;
    const char *cur = p;
    for (int part = 0; part < 4; part++) {
        if (!isdigit((unsigned char)*cur)) return 0;
        int val = 0, digits = 0;
        while (isdigit((unsigned char)*cur) && digits < 3) {
            val = val * 10 + (*cur - '0');
            cur++;
            digits++;
        }
        if (val > 255) return 0;
        if (part < 3) {
            if (*cur != '.') return 0;
            cur++;
        }
    }
    if (isalnum((unsigned char)*cur) || *cur == '.') return 0;
    return (int)(cur - p);
}

static int match_path_at(const char *p) {
    static const char *prefixes[] = {"/media/", "/mnt/", "/Users/", "/home/", "C:\\", "c:\\", "E:\\", "e:\\", NULL};
    for (int i = 0; prefixes[i]; i++) {
        size_t plen = strlen(prefixes[i]);
        if (strncmp(p, prefixes[i], plen) == 0) {
            size_t len = plen;
            while (p[len] && !isspace((unsigned char)p[len]) && p[len] != '"' && p[len] != '\'' && p[len] != ',' && p[len] != ';') len++;
            return (int)len;
        }
    }
    return 0;
}

static void sanitize_text(const char *raw, char *out, size_t out_cap, unsigned *redactions, unsigned *omitted) {
    size_t w = 0;
    const char *line = raw;
    while (*line && w + 64 < out_cap) {
        const char *eol = strchr(line, '\n');
        size_t len = eol ? (size_t)(eol - line) : strlen(line);
        if (len > 0 && line[len - 1] == '\r') len--;
        char buf[1024];
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        memcpy(buf, line, len);
        buf[len] = '\0';

        if (contains_icase(buf, "authorization:") || contains_icase(buf, "bearer ") ||
            contains_icase(buf, "owner-key") || contains_icase(buf, "receipt_key") ||
            contains_icase(buf, "admin_token") || contains_icase(buf, "aes256.key") ||
            contains_icase(buf, "cookie:")) {
            (*omitted)++;
            line = eol ? eol + 1 : line + strlen(line);
            continue;
        }

        for (size_t i = 0; i < len && w + 32 < out_cap; ) {
            int mlen = match_mac_at(buf + i);
            if (mlen > 0) {
                const char *rep = "<redacted-mac>";
                size_t rlen = strlen(rep);
                memcpy(out + w, rep, rlen);
                w += rlen;
                i += (size_t)mlen;
                (*redactions)++;
                continue;
            }
            if ((strncmp(buf + i, "serial=", 7) == 0 || strncmp(buf + i, "SERIAL=", 7) == 0)) {
                memcpy(out + w, buf + i, 7);
                w += 7;
                i += 7;
                while (i < len && !isspace((unsigned char)buf[i]) && buf[i] != ',' && buf[i] != '"' && buf[i] != ';') i++;
                const char *rep = "<redacted-serial>";
                size_t rlen = strlen(rep);
                memcpy(out + w, rep, rlen);
                w += rlen;
                (*redactions)++;
                continue;
            }
            int plen = match_path_at(buf + i);
            if (plen > 0) {
                const char *rep = "<redacted-path>";
                size_t rlen = strlen(rep);
                memcpy(out + w, rep, rlen);
                w += rlen;
                i += (size_t)plen;
                (*redactions)++;
                continue;
            }
            int iplen = match_ipv4_at(buf, buf + i);
            if (iplen > 0) {
                const char *rep = "<redacted-ip>";
                size_t rlen = strlen(rep);
                memcpy(out + w, rep, rlen);
                w += rlen;
                i += (size_t)iplen;
                (*redactions)++;
                continue;
            }
            unsigned char ch = (unsigned char)buf[i++];
            if (ch >= 32 && ch <= 126) {
                out[w++] = (char)ch;
            } else if (ch == '\t') {
                out[w++] = ' ';
            }
        }
        if (eol && w + 2 < out_cap) out[w++] = '\n';
        line = eol ? eol + 1 : line + strlen(line);
    }
    out[w] = '\0';
}

static size_t json_escape(const char *src, char *dst, size_t cap) {
    size_t w = 0;
    for (size_t i = 0; src[i] && w + 8 < cap; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '"') { dst[w++] = '\\'; dst[w++] = '"'; }
        else if (c == '\\') { dst[w++] = '\\'; dst[w++] = '\\'; }
        else if (c == '\n') { dst[w++] = '\\'; dst[w++] = 'n'; }
        else if (c == '\r') { dst[w++] = '\\'; dst[w++] = 'r'; }
        else if (c == '\t') { dst[w++] = '\\'; dst[w++] = 't'; }
        else if (c >= 32 && c <= 126) { dst[w++] = (char)c; }
    }
    dst[w] = '\0';
    return w;
}

static int read_clock_override(const char *state_dir, int64_t *out_epoch) {
    char path[1024], buf[64];
    size_t len = 0;
    snprintf(path, sizeof(path), "%s/clock.txt", state_dir);
    if (read_file_bounded(path, buf, sizeof(buf), &len, NULL) != 0 || !len) return -1;
    char *end = NULL, *next = NULL;
    errno = 0;
    long long val = strtoll(buf, &end, 10);
    if (errno || end == buf || *end != ' ') return -1;
    long long captured = strtoll(end + 1, &next, 10);
    if (errno || next == end + 1) return -1;
    while (*next && isspace((unsigned char)*next)) next++;
    if (*next) return -1;
    int64_t delta = current_system_epoch() - captured;
    if (delta < 0 || val < MIN_VALID_EPOCH || val > MAX_VALID_EPOCH ||
        delta > MAX_VALID_EPOCH - val) return -1;
    *out_epoch = val + delta;
    return 0;
}

static int resolve_effective_clock(const char *state_dir, int64_t *out_epoch, int *used_override) {
    *used_override = 0;
    int64_t sys_now = current_system_epoch();
    if (sys_now >= MIN_VALID_EPOCH && sys_now <= MAX_VALID_EPOCH) {
        *out_epoch = sys_now;
        return 0;
    }
    int64_t override = 0;
    if (read_clock_override(state_dir, &override) == 0) {
        *out_epoch = override;
        *used_override = 1;
        return 0;
    }
    return -1;
}

static void format_iso8601(int64_t epoch, char out[32]) {
    if (epoch < MIN_VALID_EPOCH || epoch > MAX_VALID_EPOCH) {
        strcpy(out, "unknown");
        return;
    }
    mbedtls_time_t t = (mbedtls_time_t)epoch;
    struct tm tm;
    if (!mbedtls_platform_gmtime_r(&t, &tm)) {
        strcpy(out, "unknown");
        return;
    }
    snprintf(out, 32, "%04d-%02d-%02dT%02d:%02d:%02dZ",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
}

struct item_buf {
    const char *label;
    char content[MAX_ITEM_RAW * 2];
    int truncated;
    int missing;
};

/* Only known diagnostic codes survive collection. Arbitrary log text never uploads. */
static void collect_facts(const char *raw, char *out, size_t cap, unsigned *redactions, unsigned *omitted) {
    static const char *const codes[] = {
    "ACTIVE",
    "ADMIT_WAIT",
    "CLEANUP_",
    "CLEANUP_MODULE_UNLOAD_FAILURE",
    "CLEANUP_NODE_REMOVE_FAILURE",
    "FATAL_BINDING_NOT_NEW",
    "FATAL_PROCESS_SET_CHANGE",
    "LD_PRELOAD",
    "MUSIC_READY",
    "NEW",
    "NONE",
    "QUALIFIED_EXACT_RUNTIME_AND_KERNEL",
    "RACE_MUSIC_CAPTURE",
    "RACE_MUSIC_DESCRIPTORS",
    "READY_CH3_CH4",
    "REFUSED_KERNEL_ABI",
    "REFUSED_MODULE_IDENTITY",
    "REFUSED_NATIVE_RUNTIME",
    "REFUSED_STOPPED_UNIT_MUSIC_BINDING",
    "RETIRED",
    "ROUTE_DISCONNECTED",
    "STOPPED",
    "STOPPED_WITH_ERROR",
    "STOP_PENDING",
    "TERMINATED_",
    "TERMINATED_ADMISSION_DEBOUNCE_FAILURE",
    "TERMINATED_APPLY_LOAD_FAILURE",
    "TERMINATED_BIND_DEBOUNCE_FAILURE",
    "TERMINATED_EVENT_SOCKET_FAILURE",
    "TERMINATED_FOREIGN_MODULE",
    "TERMINATED_FOREIGN_MODULE_OR_NODE",
    "TERMINATED_FOREIGN_NODE",
    "TERMINATED_MODULE_ADMISSION_FAILURE",
    "TERMINATED_MUSIC_IDENTITY",
    "TERMINATED_NATIVE_END_OR_IDENTITY_CHANGE",
    "TERMINATED_NODE_REMOVE_FAILURE",
    "TERMINATED_NODE_WRITE_FAILURE",
    "TERMINATED_PID_WRITE_FAILURE",
    "TERMINATED_PROBE_LOAD_FAILURE",
    "TERMINATED_PROBE_UNLOAD_FAILURE",
    "TERMINATED_PROCESS_SET_CHANGE",
    "TERMINATED_SCAN_FAILURE",
    "TERMINATED_SIGNAL",
    "TERMINATED_STOPPED_MEDIA_ADMISSION",
    "TERMINATED_STOP_MARKER",
    "TERMINATED_UNSPECIFIED",
    "UNSUPPORTED_MUSIC_FILESYSTEM",
    "UNSUPPORTED_MUSIC_MOUNT",
    "UNSUPPORTED_MUSIC_SERIAL_LENGTH",
    "UNSUPPORTED_UNIT_OR_USB2_TOPOLOGY",
    "UNSUPPORTED_USB2_BLOCK_ANCESTRY",
    "WAITING_MEDIA",
    "WAIT_AUDIO_DEVICE",
    "WAIT_LOOPBACK",
    "WAIT_MUSIC_DESCRIPTORS",
    "WAIT_MUSIC_MOUNT",
    "WAIT_MUSIC_SERIAL_READ",
    "WAIT_NATIVE_PAUSED",
    "WAIT_SAFE_LOOPBACK_DISCONNECT",
    "ready",
    "stopped",
    "stopping",
    "waiting"
    };
    size_t used = 0;
    const char *line = raw;
    while (*line) {
        const char *end = strchr(line, '\n');
        if (!end) end = line + strlen(line);
        int found = 0;
        const char *p = line;
        while (p < end) {
            if (!isalnum((unsigned char)*p) && *p != '_') { p++; continue; }
            const char *start = p++;
            while (p < end && (isalnum((unsigned char)*p) || *p == '_')) p++;
            size_t len = (size_t)(p - start);
            for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
                if (strlen(codes[i]) == len && !memcmp(start, codes[i], len)) {
                    if (used + len + 2 >= cap) { (*omitted)++; out[used] = 0; return; }
                    memcpy(out + used, codes[i], len); used += len; out[used++] = '\n';
                    found = 1; break;
                }
            }
        }
        (*redactions)++;
        if (!found) (*omitted)++;
        line = *end ? end + 1 : end;
    }
    out[used] = 0;
}

static void collect_one_file(const char *root_prefix, const char *rel_path, const char *label,
                             struct item_buf *item, unsigned *redactions, unsigned *omitted) {
    item->label = label;
    item->content[0] = '\0';
    item->truncated = 0;
    item->missing = 1;
    char full[1024];
    snprintf(full, sizeof(full), "%s%s", root_prefix, rel_path);
    char raw[MAX_ITEM_RAW];
    size_t len = 0;
    int trunc = 0;
    if (read_file_bounded(full, raw, sizeof(raw), &len, &trunc) == 0) {
        item->missing = 0;
        item->truncated = trunc;
        collect_facts(raw, item->content, sizeof(item->content), redactions, omitted);
    }
}

static int cmd_prepare(const char *state_dir, const char *category, const char *title_arg, const char *desc_arg) {
    if (ensure_dir_safe(state_dir) != 0) {
        fprintf(stderr, "XZ_SUPPORT_ERROR CANNOT_CREATE_STATE_DIR\n");
        return 1;
    }
    if (strcmp(category, "bridge") != 0 && strcmp(category, "audio") != 0 &&
        strcmp(category, "display") != 0 && strcmp(category, "other") != 0) {
        fprintf(stderr, "XZ_SUPPORT_ERROR INVALID_CATEGORY\n");
        return 1;
    }
    unsigned char rand_id[16], rand_key[32];
    if (random_bytes(rand_id, sizeof(rand_id)) != 0 || random_bytes(rand_key, sizeof(rand_key)) != 0) {
        fprintf(stderr, "XZ_SUPPORT_ERROR ENTROPY_UNAVAILABLE\n");
        return 1;
    }
    char report_id[33], receipt_key[65];
    to_hex(rand_id, sizeof(rand_id), report_id);
    to_hex(rand_key, sizeof(rand_key), receipt_key);

    const char *src_root = "";
    if (test_mode_enabled()) {
        const char *env_root = getenv("XZ_SUPPORT_SOURCE_ROOT");
        if (env_root) src_root = env_root;
    }

    char runtime_sha[65] = {0}, bridge_sha[65] = {0}, module_sha[65] = {0};
    char pbuf[1024];
    snprintf(pbuf, sizeof(pbuf), "%s/dev/shm/libxz-mods.so", src_root);
    int has_rt = (hash_regular_file(pbuf, runtime_sha) == 0);
    snprintf(pbuf, sizeof(pbuf), "%s/dev/shm/xz-auto-4bb9db00a2ab/xz-auto-bridge", src_root);
    int has_br = (hash_regular_file(pbuf, bridge_sha) == 0);
    snprintf(pbuf, sizeof(pbuf), "%s/dev/shm/xz-auto-4bb9db00a2ab/xz_hub_depth.ko", src_root);
    int has_ko = (hash_regular_file(pbuf, module_sha) == 0);

    unsigned redactions = 0, omitted = 0;
    struct item_buf items[5];
    memset(items, 0, sizeof(items));

    items[0].label = "build.json";
    items[0].missing = 0;
    items[0].truncated = 0;
    snprintf(items[0].content, sizeof(items[0].content),
             "{\"app_version\":\"0.5.0-preview.22.7\",\"runtime_sha256\":%s%s%s,\"bridge_sha256\":%s%s%s,\"module_sha256\":%s%s%s}",
             has_rt ? "\"" : "", has_rt ? runtime_sha : "null", has_rt ? "\"" : "",
             has_br ? "\"" : "", has_br ? bridge_sha : "null", has_br ? "\"" : "",
             has_ko ? "\"" : "", has_ko ? module_sha : "null", has_ko ? "\"" : "");

    collect_one_file(src_root, "/dev/shm/xz-four-deck/status.json", "bridge.json", &items[1], &redactions, &omitted);
    if (items[1].missing) {
        collect_one_file(src_root, "/dev/shm/xz-four-deck/status", "bridge.json", &items[1], &redactions, &omitted);
    }
    collect_one_file(src_root, "/dev/shm/xz-four-deck/bridge.log", "bridge.log", &items[2], &redactions, &omitted);
    collect_one_file(src_root, "/dev/shm/xz-usb-coldplug.log", "bootstrap.log", &items[3], &redactions, &omitted);
    collect_one_file(src_root, "/dev/shm/xz-mods.log", "runtime.log", &items[4], &redactions, &omitted);

    int64_t eff_epoch = 0;
    int used_override = 0;
    int clock_ok = (resolve_effective_clock(state_dir, &eff_epoch, &used_override) == 0);
    char created_at[32];
    format_iso8601(clock_ok ? eff_epoch : current_system_epoch(), created_at);

    char clean_title[256] = {0}, clean_desc[4096] = {0};
    sanitize_text(title_arg && *title_arg ? title_arg : "XZ Mods controller report", clean_title, sizeof(clean_title), &redactions, &omitted);
    if (!clean_title[0]) strcpy(clean_title, "XZ Mods controller report");
    if (strlen(clean_title) > 160) clean_title[160] = '\0';

    sanitize_text(desc_arg ? desc_arg : "", clean_desc, sizeof(clean_desc), &redactions, &omitted);

    char esc_title[512], esc_desc[8192];
    json_escape(clean_title, esc_title, sizeof(esc_title));
    json_escape(clean_desc, esc_desc, sizeof(esc_desc));

    char *json = calloc(1, MAX_DRAFT_BYTES);
    char *esc_item = calloc(1, MAX_ITEM_RAW * 4);
    if (!json || !esc_item) {
        free(json);
        free(esc_item);
        return 1;
    }

    size_t pos = 0;
    pos += (size_t)snprintf(json + pos, MAX_DRAFT_BYTES - pos,
        "{\n"
        "  \"schema\": \"xz-support-report/1\",\n"
        "  \"report_id\": \"%s\",\n"
        "  \"created_at\": \"%s\",\n"
        "  \"origin\": \"controller\",\n"
        "  \"category\": \"%s\",\n"
        "  \"build\": {\n"
        "    \"app_version\": \"0.5.0-preview.22.7\",\n"
        "    \"runtime_sha256\": %s%s%s,\n"
        "    \"bridge_sha256\": %s%s%s,\n"
        "    \"module_sha256\": %s%s%s\n"
        "  },\n"
        "  \"public_issue\": {\n"
        "    \"title\": \"%s\",\n"
        "    \"description\": \"%s\"\n"
        "  },\n"
        "  \"private\": {\n"
        "    \"contact\": null,\n"
        "    \"items\": [\n",
        report_id, created_at, category,
        has_rt ? "\"" : "", has_rt ? runtime_sha : "null", has_rt ? "\"" : "",
        has_br ? "\"" : "", has_br ? bridge_sha : "null", has_br ? "\"" : "",
        has_ko ? "\"" : "", has_ko ? module_sha : "null", has_ko ? "\"" : "",
        esc_title, esc_desc);

    for (size_t i = 0; i < 5; i++) {
        json_escape(items[i].content, esc_item, MAX_ITEM_RAW * 4);
        pos += (size_t)snprintf(json + pos, MAX_DRAFT_BYTES - pos,
            "      {\"label\": \"%s\", \"content\": \"%s\", \"truncated\": %s, \"missing\": %s}%s\n",
            items[i].label, esc_item,
            items[i].truncated ? "true" : "false",
            items[i].missing ? "true" : "false",
            i + 1 < 5 ? "," : "");
    }

    pos += (size_t)snprintf(json + pos, MAX_DRAFT_BYTES - pos,
        "    ]\n"
        "  },\n"
        "  \"redactions\": {\"count\": %u, \"omitted_lines\": %u}\n"
        "}\n",
        redactions, omitted);

    char payload_sha256[65];
    sha256_hex(json, pos, payload_sha256);

    if (write_atomic(state_dir, "draft.json", json, pos) != 0) {
        free(json);
        free(esc_item);
        fprintf(stderr, "XZ_SUPPORT_ERROR WRITE_DRAFT_FAILED\n");
        return 1;
    }

    char meta[2048];
    int mlen = snprintf(meta, sizeof(meta),
        "report_id=%s\n"
        "receipt_key=%s\n"
        "payload_sha256=%s\n"
        "payload_bytes=%u\n"
        "category=%s\n"
        "created_at=%s\n"
        "redactions=%u\n"
        "omitted_lines=%u\n"
        "title=%s\n",
        report_id, receipt_key, payload_sha256, (unsigned)pos,
        category, created_at, redactions, omitted, clean_title);
    if (write_atomic(state_dir, "draft.meta", meta, (size_t)mlen) != 0) {
        free(json);
        free(esc_item);
        fprintf(stderr, "XZ_SUPPORT_ERROR WRITE_META_FAILED\n");
        return 1;
    }

    char old_file[1024];
    snprintf(old_file, sizeof(old_file), "%s/receipt.json", state_dir);
    unlink(old_file);
    snprintf(old_file, sizeof(old_file), "%s/outcome.txt", state_dir);
    unlink(old_file);

    printf("PREPARED %s %s %u %u %u %d\n",
           report_id, payload_sha256, (unsigned)pos, redactions, omitted, clock_ok);
    free(json);
    free(esc_item);
    return 0;
}

static int parse_meta_field(const char *meta, const char *key, char *out, size_t cap) {
    size_t klen = strlen(key);
    const char *p = meta;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        if (len > klen + 1 && strncmp(p, key, klen) == 0 && p[klen] == '=') {
            size_t vlen = len - (klen + 1);
            if (vlen > 0 && p[klen + 1 + vlen - 1] == '\r') vlen--;
            if (vlen >= cap) vlen = cap - 1;
            memcpy(out, p + klen + 1, vlen);
            out[vlen] = '\0';
            return 0;
        }
        p = eol ? eol + 1 : p + len;
    }
    return -1;
}

struct net_ctx {
    sock_t fd;
    int64_t deadline_ms;
};

static int set_nonblocking(sock_t fd) {
#if defined(_WIN32)
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode);
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

static int wait_socket(sock_t fd, int for_write, int64_t deadline_ms) {
    for (;;) {
        int64_t now = mbedtls_ms_time();
        if (now >= deadline_ms) return -1;
        int timeout = (int)(deadline_ms - now);
        if (timeout > 1000) timeout = 1000;
#if defined(_WIN32)
        WSAPOLLFD pfd;
        pfd.fd = fd;
        pfd.events = for_write ? POLLWRNORM : POLLRDNORM;
        pfd.revents = 0;
        int r = WSAPoll(&pfd, 1, timeout);
        if (r > 0) {
            if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) return -1;
            return 0;
        }
        if (r < 0) return -1;
#else
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = for_write ? POLLOUT : POLLIN;
        pfd.revents = 0;
        int r = poll(&pfd, 1, timeout);
        if (r > 0) {
            if (pfd.revents & (POLLERR | POLLNVAL)) return -1;
            return 0;
        }
        if (r < 0 && errno != EINTR) return -1;
#endif
    }
}

static int bio_send(void *ctx, const unsigned char *buf, size_t len) {
    struct net_ctx *c = (struct net_ctx *)ctx;
    if (mbedtls_ms_time() >= c->deadline_ms) return MBEDTLS_ERR_SSL_TIMEOUT;
#if defined(_WIN32)
    int n = send(c->fd, (const char *)buf, (int)len, 0);
    if (n < 0) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
            if (wait_socket(c->fd, 1, c->deadline_ms) != 0) return MBEDTLS_ERR_SSL_TIMEOUT;
            return MBEDTLS_ERR_SSL_WANT_WRITE;
        }
        return -1;
    }
    return n;
#else
    ssize_t n = send(c->fd, buf, len, MSG_NOSIGNAL);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            if (wait_socket(c->fd, 1, c->deadline_ms) != 0) return MBEDTLS_ERR_SSL_TIMEOUT;
            return MBEDTLS_ERR_SSL_WANT_WRITE;
        }
        return -1;
    }
    return (int)n;
#endif
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len) {
    struct net_ctx *c = (struct net_ctx *)ctx;
    if (mbedtls_ms_time() >= c->deadline_ms) return MBEDTLS_ERR_SSL_TIMEOUT;
#if defined(_WIN32)
    int n = recv(c->fd, (char *)buf, (int)len, 0);
    if (n < 0) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
            if (wait_socket(c->fd, 0, c->deadline_ms) != 0) return MBEDTLS_ERR_SSL_TIMEOUT;
            return MBEDTLS_ERR_SSL_WANT_READ;
        }
        return -1;
    }
    return n;
#else
    ssize_t n = recv(c->fd, buf, len, 0);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            if (wait_socket(c->fd, 0, c->deadline_ms) != 0) return MBEDTLS_ERR_SSL_TIMEOUT;
            return MBEDTLS_ERR_SSL_WANT_READ;
        }
        return -1;
    }
    return (int)n;
#endif
}

static sock_t connect_tcp(const char *host, const char *port, int64_t deadline_ms) {
    struct addrinfo hints, *res = NULL, *rp = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) != 0 || !res) return INVALID_SOCK;
    sock_t fd = INVALID_SOCK;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == INVALID_SOCK) continue;
        set_nonblocking(fd);
        int rc = connect(fd, rp->ai_addr, (int)rp->ai_addrlen);
        if (rc == 0) break;
#if defined(_WIN32)
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS) {
#else
        if (errno == EINPROGRESS) {
#endif
            if (wait_socket(fd, 1, deadline_ms) == 0) {
                int so_err = 0;
                socklen_t slen = sizeof(so_err);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&so_err, &slen) == 0 && so_err == 0) {
                    break;
                }
            }
        }
        close_sock(fd);
        fd = INVALID_SOCK;
    }
    freeaddrinfo(res);
    return fd;
}

static int parse_https_url(const char *url, char *host, size_t host_cap, char *port, size_t port_cap, char *base_path, size_t path_cap) {
    if (strncmp(url, "https://", 8) != 0) return -1;
    const char *p = url + 8;
    const char *slash = strchr(p, '/');
    const char *host_end = slash ? slash : p + strlen(p);
    const char *colon = memchr(p, ':', (size_t)(host_end - p));
    if (colon) {
        size_t hlen = (size_t)(colon - p);
        size_t plen = (size_t)(host_end - (colon + 1));
        if (!hlen || hlen >= host_cap || !plen || plen >= port_cap) return -1;
        memcpy(host, p, hlen);
        host[hlen] = '\0';
        memcpy(port, colon + 1, plen);
        port[plen] = '\0';
    } else {
        size_t hlen = (size_t)(host_end - p);
        if (!hlen || hlen >= host_cap) return -1;
        memcpy(host, p, hlen);
        host[hlen] = '\0';
        snprintf(port, port_cap, "443");
    }
    if (slash) {
        snprintf(base_path, path_cap, "%s", slash);
        size_t blen = strlen(base_path);
        if (blen > 1 && base_path[blen - 1] == '/') base_path[blen - 1] = '\0';
    } else {
        base_path[0] = '\0';
    }
    return 0;
}

/* Undo HTTP/1.1 chunked transfer coding in place; returns the body length or -1 when malformed. */
static long dechunk(char *body, size_t len) {
    size_t in = 0, out = 0;
    for (;;) {
        char *end = NULL;
        if (in >= len || !isxdigit((unsigned char)body[in])) return -1;
        unsigned long size = strtoul(body + in, &end, 16);
        if (!end || end == body + in || size > len) return -1;
        char *line = strstr(end, "\r\n");
        if (!line || (size_t)(line - body) >= len) return -1;
        in = (size_t)(line - body) + 2;
        if (!size) return (long)out;
        if (size > len - in || len - in - size < 2 || body[in + size] != '\r' || body[in + size + 1] != '\n') return -1;
        memmove(body + out, body + in, size);
        out += size;
        in += size + 2;
    }
}

/* headers: extra request header lines, each ending in CRLF (authorization and identities).
   mark_in_flight: 0 a read; 1 the v1 upload, which records outcome.txt; 2 an upload that records nothing.
   An upload whose request went out without an answer returns 4 (outcome unknown). */
static int https_exchange(const char *state_dir, const char *origin_url, const char *method, const char *subpath,
                          const char *headers, const char *body, size_t body_len, int mark_in_flight,
                          int *http_status, char *resp_body, size_t resp_cap) {
    int64_t eff_epoch = 0;
    int used_override = 0;
    if (resolve_effective_clock(state_dir, &eff_epoch, &used_override) != 0) {
        fprintf(stderr, "XZ_SUPPORT_ERROR NEEDS_CLOCK SYSTEM CLOCK IS BEFORE 2026 / SET REPORT DATE FOR HTTPS\n");
        return 3;
    }
    g_tls_epoch = (mbedtls_time_t)eff_epoch;
    mbedtls_platform_set_time(xz_tls_time_cb);

    char host[256], port[16], base_path[256];
    if (parse_https_url(origin_url, host, sizeof(host), port, sizeof(port), base_path, sizeof(base_path)) != 0) {
        fprintf(stderr, "XZ_SUPPORT_ERROR INVALID_HTTPS_ORIGIN\n");
        return 1;
    }

#if defined(_WIN32)
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    int64_t budget_ms = 15000;
    if (test_mode_enabled()) {
        const char *override_ms = getenv("XZ_SUPPORT_TEST_DEADLINE_MS");
        if (override_ms && *override_ms) budget_ms = strtoll(override_ms, NULL, 10);
    }
    int64_t deadline = mbedtls_ms_time() + budget_ms;
    const char *connect_host = host;
    if (test_mode_enabled()) {
        const char *override_connect = getenv("XZ_SUPPORT_TEST_CONNECT_HOST");
        if (override_connect && *override_connect) connect_host = override_connect;
    }
    sock_t fd = connect_tcp(connect_host, port, deadline);
    if (fd == INVALID_SOCK) {
        fprintf(stderr, "XZ_SUPPORT_ERROR CONNECT_FAILED\n");
        return 1;
    }

    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt cacert;

    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_x509_crt_init(&cacert);

    int exit_code = 1;
    if (mbedtls_entropy_add_source(&entropy, entropy_cb, NULL, 32, MBEDTLS_ENTROPY_SOURCE_STRONG) != 0 ||
        mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)"xz-support", 10) != 0) {
        fprintf(stderr, "XZ_SUPPORT_ERROR RNG_INIT_FAILED\n");
        goto cleanup;
    }

    int loaded_ca = 0;
    if (test_mode_enabled()) {
        const char *ca_file = getenv("XZ_SUPPORT_CA_FILE");
        if (ca_file && *ca_file) {
            char pem_buf[32768];
            size_t pem_len = 0;
            if (read_file_bounded(ca_file, pem_buf, sizeof(pem_buf), &pem_len, NULL) == 0 && pem_len > 0) {
                if (mbedtls_x509_crt_parse(&cacert, (const unsigned char *)pem_buf, pem_len + 1) == 0) {
                    loaded_ca = 1;
                }
            }
        }
    }
    if (!loaded_ca) {
        for (size_t i = 0; i < XZ_CA_BUNDLE_COUNT; i++) {
            if (mbedtls_x509_crt_parse_der(&cacert, xz_ca_bundle[i].der, xz_ca_bundle[i].len) == 0) {
                loaded_ca++;
            }
        }
    }
    if (!loaded_ca) {
        fprintf(stderr, "XZ_SUPPORT_ERROR CA_BUNDLE_LOAD_FAILED\n");
        goto cleanup;
    }

    if (mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
        fprintf(stderr, "XZ_SUPPORT_ERROR SSL_CONFIG_FAILED\n");
        goto cleanup;
    }
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);

    if (mbedtls_ssl_setup(&ssl, &conf) != 0 || mbedtls_ssl_set_hostname(&ssl, host) != 0) {
        fprintf(stderr, "XZ_SUPPORT_ERROR SSL_SETUP_FAILED\n");
        goto cleanup;
    }

    struct net_ctx nctx = {fd, deadline};
    mbedtls_ssl_set_bio(&ssl, &nctx, bio_send, bio_recv, NULL);

    int ret;
    while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            uint32_t vflags = mbedtls_ssl_get_verify_result(&ssl);
            if (vflags == 0xffffffffu) vflags = 0; /* no certificate was checked: a timeout or a dropped connection */
            if (vflags & MBEDTLS_X509_BADCERT_EXPIRED) {
                fprintf(stderr, "XZ_SUPPORT_ERROR TLS_CERT_EXPIRED\n");
            } else if (vflags & MBEDTLS_X509_BADCERT_FUTURE) {
                fprintf(stderr, "XZ_SUPPORT_ERROR TLS_CERT_FUTURE_CHECK_REPORT_DATE\n");
            } else if (vflags & MBEDTLS_X509_BADCERT_CN_MISMATCH) {
                fprintf(stderr, "XZ_SUPPORT_ERROR TLS_HOSTNAME_MISMATCH\n");
            } else if (vflags != 0) {
                fprintf(stderr, "XZ_SUPPORT_ERROR TLS_VERIFY_FAILED_0x%x\n", (unsigned)vflags);
            } else {
                fprintf(stderr, "XZ_SUPPORT_ERROR TLS_HANDSHAKE_FAILED_-0x%x\n", (unsigned)(-ret));
            }
            goto cleanup;
        }
    }

    char req_head[2048];
    int hlen;
    if (body && body_len > 0) {
        hlen = snprintf(req_head, sizeof(req_head),
            "%s %s%s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: xz-support/1\r\n"
            "Accept: application/json\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %u\r\n"
            "%s"
            "Connection: close\r\n\r\n",
            method, base_path, subpath, host, (unsigned)body_len, headers);
    } else {
        hlen = snprintf(req_head, sizeof(req_head),
            "%s %s%s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: xz-support/1\r\n"
            "Accept: application/json\r\n"
            "%s"
            "Connection: close\r\n\r\n",
            method, base_path, subpath, host, headers);
    }
    if (hlen < 0 || (size_t)hlen >= sizeof(req_head)) {
        fprintf(stderr, "XZ_SUPPORT_ERROR REQUEST_HEADER_TOO_LONG\n");
        goto cleanup;
    }

    if (mark_in_flight == 1) {
        write_atomic(state_dir, "outcome.txt", "UNKNOWN\n", 8);
    }

    size_t sent = 0;
    while (sent < (size_t)hlen) {
        ret = mbedtls_ssl_write(&ssl, (const unsigned char *)req_head + sent, (size_t)hlen - sent);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (ret <= 0) {
            fprintf(stderr, "XZ_SUPPORT_ERROR SEND_HEADER_FAILED\n");
            exit_code = mark_in_flight ? 4 : 1;
            goto cleanup;
        }
        sent += (size_t)ret;
    }

    sent = 0;
    while (body && sent < body_len) {
        ret = mbedtls_ssl_write(&ssl, (const unsigned char *)body + sent, body_len - sent);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (ret <= 0) {
            fprintf(stderr, "XZ_SUPPORT_ERROR OUTCOME_UNKNOWN SEND_INTERRUPTED\n");
            exit_code = 4;
            goto cleanup;
        }
        sent += (size_t)ret;
    }

    if (test_mode_enabled()) {
        const char *abort_after_send = getenv("XZ_SUPPORT_TEST_ABORT_AFTER_SEND");
        if (abort_after_send && strcmp(abort_after_send, "1") == 0) {
            fprintf(stderr, "XZ_SUPPORT_ERROR OUTCOME_UNKNOWN SIMULATED_ABORT_AFTER_SEND\n");
            exit_code = 4;
            goto cleanup;
        }
    }

    char raw_resp[32768];
    size_t rtot = 0;
    while (rtot < sizeof(raw_resp) - 1) {
        ret = mbedtls_ssl_read(&ssl, (unsigned char *)raw_resp + rtot, sizeof(raw_resp) - 1 - rtot);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || ret == 0) break;
        if (ret < 0) {
            if (rtot == 0) {
                fprintf(stderr, "XZ_SUPPORT_ERROR OUTCOME_UNKNOWN NO_RESPONSE\n");
                exit_code = mark_in_flight ? 4 : 1;
                goto cleanup;
            }
            break;
        }
        rtot += (size_t)ret;
    }
    raw_resp[rtot] = '\0';

    char *hdr_end = strstr(raw_resp, "\r\n\r\n");
    if (!hdr_end || strncmp(raw_resp, "HTTP/1.", 7) != 0) {
        fprintf(stderr, "XZ_SUPPORT_ERROR OUTCOME_UNKNOWN INVALID_HTTP_RESPONSE\n");
        exit_code = mark_in_flight ? 4 : 1;
        goto cleanup;
    }
    *http_status = atoi(raw_resp + 9);
    if (*http_status >= 300 && *http_status < 400) {
        fprintf(stderr, "XZ_SUPPORT_ERROR REDIRECT_REFUSED_%d\n", *http_status);
        if (mark_in_flight == 1) write_atomic(state_dir, "outcome.txt", "FAILED\n", 7);
        exit_code = 1;
        goto cleanup;
    }

    char *bstart = hdr_end + 4;
    size_t blen = rtot - (size_t)(bstart - raw_resp);
    *hdr_end = '\0';
    if (contains_icase(raw_resp, "\ntransfer-encoding: chunked")) {
        long plain = dechunk(bstart, blen);
        if (plain < 0) {
            fprintf(stderr, "XZ_SUPPORT_ERROR OUTCOME_UNKNOWN INVALID_CHUNKED_RESPONSE\n");
            exit_code = mark_in_flight ? 4 : 1;
            goto cleanup;
        }
        blen = (size_t)plain;
    }
    if (blen >= resp_cap) blen = resp_cap - 1;
    memcpy(resp_body, bstart, blen);
    resp_body[blen] = '\0';
    exit_code = 0;

cleanup:
    mbedtls_ssl_close_notify(&ssl);
    close_sock(fd);
    mbedtls_x509_crt_free(&cacert);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    return exit_code;
}

static int receipt_equal(const char *json, const jsmntok_t *t, const char *expected) {
    return t->type == JSMN_STRING && (size_t)(t->end-t->start) == strlen(expected) &&
        !memcmp(json+t->start,expected,strlen(expected));
}
static int validate_receipt_json(const char *json, const char *expected_id, const char *expected_sha) {
    jsmn_parser p; jsmntok_t t[64]; jsmn_init(&p);
    size_t len=strlen(json);
    int count=jsmn_parse(&p,json,len,t,64);
    if(count<1 || t[0].type!=JSMN_OBJECT || count!=1+2*t[0].size) return -1;
    for(size_t n=(size_t)t[0].end;n<len;n++) if(!isspace((unsigned char)json[n])) return -1;
    unsigned seen=0;
    for(int i=1;i<count;i+=2) {
        if(t[i].type!=JSMN_STRING || t[i+1].type==JSMN_OBJECT || t[i+1].type==JSMN_ARRAY) return -1;
        for(int j=1;j<i;j+=2) if(t[i].end-t[i].start==t[j].end-t[j].start &&
            !memcmp(json+t[i].start,json+t[j].start,(size_t)(t[i].end-t[i].start))) return -1;
        const char *expect=NULL; unsigned bit=0;
        if(receipt_equal(json,&t[i],"schema")) {expect="xz-support-receipt/1";bit=1;}
        else if(receipt_equal(json,&t[i],"report_id")) {expect=expected_id;bit=2;}
        else if(receipt_equal(json,&t[i],"payload_sha256")) {expect=expected_sha;bit=4;}
        else if(receipt_equal(json,&t[i],"private_logs")) {expect="stored";bit=8;}
        else if(receipt_equal(json,&t[i],"public_issue")) {
            if(!receipt_equal(json,&t[i+1],"pending") && !receipt_equal(json,&t[i+1],"user_provided") &&
                !receipt_equal(json,&t[i+1],"verified")) return -1;
            bit=16;
        }
        if(expect && !receipt_equal(json,&t[i+1],expect)) return -1;
        seen|=bit;
    }
    return seen==31 ? 0 : -1;
}

static int load_verified_draft(const char *state_dir, const char *expected_sha256,
                               char report_id[33], char receipt_key[65],
                               char **out_bytes, size_t *out_len) {
    if (!is_lower_hex(expected_sha256, 64)) {
        fprintf(stderr, "XZ_SUPPORT_ERROR INVALID_EXPECTED_SHA256\n");
        return -1;
    }
    char meta_path[1024], meta_buf[2048];
    size_t meta_len = 0;
    snprintf(meta_path, sizeof(meta_path), "%s/draft.meta", state_dir);
    if (read_file_bounded(meta_path, meta_buf, sizeof(meta_buf), &meta_len, NULL) != 0) {
        fprintf(stderr, "XZ_SUPPORT_ERROR MISSING_DRAFT_META\n");
        return -1;
    }
    char meta_sha[65] = {0};
    if (parse_meta_field(meta_buf, "report_id", report_id, 33) != 0 ||
        parse_meta_field(meta_buf, "receipt_key", receipt_key, 65) != 0 ||
        parse_meta_field(meta_buf, "payload_sha256", meta_sha, 65) != 0) {
        fprintf(stderr, "XZ_SUPPORT_ERROR CORRUPT_DRAFT_META\n");
        return -1;
    }
    if (strcmp(meta_sha, expected_sha256) != 0) {
        fprintf(stderr, "XZ_SUPPORT_ERROR STALE_PREVIEW_SHA256\n");
        return -1;
    }
    char draft_path[1024];
    snprintf(draft_path, sizeof(draft_path), "%s/draft.json", state_dir);
    char *draft = calloc(1, MAX_DRAFT_BYTES);
    if (!draft) return -1;
    size_t dlen = 0;
    int trunc = 0;
    if (read_file_bounded(draft_path, draft, MAX_DRAFT_BYTES, &dlen, &trunc) != 0 || trunc || dlen == 0) {
        free(draft);
        fprintf(stderr, "XZ_SUPPORT_ERROR MISSING_DRAFT_JSON\n");
        return -1;
    }
    char actual_sha[65];
    sha256_hex(draft, dlen, actual_sha);
    if (strcmp(actual_sha, expected_sha256) != 0) {
        free(draft);
        fprintf(stderr, "XZ_SUPPORT_ERROR DRAFT_BYTES_CHANGED\n");
        return -1;
    }
    *out_bytes = draft;
    *out_len = dlen;
    return 0;
}

static int cmd_send(const char *state_dir, const char *expected_sha256, const char *origin_url) {
    char report_id[33] = {0}, receipt_key[65] = {0};
    char *draft = NULL;
    size_t dlen = 0;
    if (load_verified_draft(state_dir, expected_sha256, report_id, receipt_key, &draft, &dlen) != 0) {
        return 2;
    }
    int http_status = 0;
    char resp[16384] = {0}, headers[512];
    snprintf(headers, sizeof(headers),
             "X-Report-ID: %s\r\nX-Report-SHA256: %s\r\nX-Report-Consent: public-summary-private-logs/v1\r\n"
             "Authorization: Bearer %s\r\n", report_id, expected_sha256, receipt_key);
    int rc = https_exchange(state_dir, origin_url ? origin_url : DEFAULT_ORIGIN,
                            "POST", "/v1/reports", headers, draft, dlen, 1, &http_status, resp, sizeof(resp));
    free(draft);
    if (rc != 0) return rc;
    if ((http_status == 200 || http_status == 201) &&
        validate_receipt_json(resp, report_id, expected_sha256) == 0) {
        write_atomic(state_dir, "receipt.json", resp, strlen(resp));
        write_atomic(state_dir, "outcome.txt", "STORED\n", 7);
        printf("STORED %s %s\n", report_id, expected_sha256);
        return 0;
    }
    write_atomic(state_dir, "outcome.txt", "FAILED\n", 7);
    fprintf(stderr, "XZ_SUPPORT_ERROR HTTP_%d\n", http_status);
    return 1;
}

static int cmd_check(const char *state_dir, const char *origin_url) {
    char meta_path[1024], meta_buf[2048];
    size_t meta_len = 0;
    snprintf(meta_path, sizeof(meta_path), "%s/draft.meta", state_dir);
    if (read_file_bounded(meta_path, meta_buf, sizeof(meta_buf), &meta_len, NULL) != 0) {
        fprintf(stderr, "XZ_SUPPORT_ERROR MISSING_DRAFT_META\n");
        return 1;
    }
    char report_id[33] = {0}, receipt_key[65] = {0}, expected_sha[65] = {0};
    if (parse_meta_field(meta_buf, "report_id", report_id, 33) != 0 ||
        parse_meta_field(meta_buf, "receipt_key", receipt_key, 65) != 0 ||
        parse_meta_field(meta_buf, "payload_sha256", expected_sha, 65) != 0) {
        fprintf(stderr, "XZ_SUPPORT_ERROR CORRUPT_DRAFT_META\n");
        return 1;
    }
    char subpath[128];
    snprintf(subpath, sizeof(subpath), "/v1/reports/%s/receipt", report_id);
    int http_status = 0;
    char resp[16384] = {0}, headers[128];
    snprintf(headers, sizeof(headers), "Authorization: Bearer %s\r\n", receipt_key);
    int rc = https_exchange(state_dir, origin_url ? origin_url : DEFAULT_ORIGIN,
                            "GET", subpath, headers, NULL, 0, 0, &http_status, resp, sizeof(resp));
    if (rc != 0) return rc;
    if (http_status == 200 && validate_receipt_json(resp, report_id, expected_sha) == 0) {
        write_atomic(state_dir, "receipt.json", resp, strlen(resp));
        write_atomic(state_dir, "outcome.txt", "STORED\n", 7);
        printf("STORED %s %s\n", report_id, expected_sha);
        return 0;
    }
    if (http_status == 404) {
        write_atomic(state_dir, "outcome.txt", "NOT_FOUND\n", 10);
        printf("NOT_FOUND %s\n", report_id);
        return 0;
    }
    if (http_status == 410) {
        write_atomic(state_dir, "outcome.txt", "GONE\n", 5);
        printf("GONE %s\n", report_id);
        return 0;
    }
    fprintf(stderr, "XZ_SUPPORT_ERROR CHECK_HTTP_%d\n", http_status);
    return 1;
}

static int cmd_export(const char *state_dir, const char *expected_sha256, const char *usb_root) {
    if (!usb_root || !*usb_root || strstr(usb_root, "..")) {
        fprintf(stderr, "XZ_SUPPORT_ERROR INVALID_USB_ROOT\n");
        return 1;
    }
    char report_id[33] = {0}, receipt_key[65] = {0};
    char *draft = NULL;
    size_t dlen = 0;
    if (load_verified_draft(state_dir, expected_sha256, report_id, receipt_key, &draft, &dlen) != 0) {
        return 2;
    }
    char vj_dir[1024], rep_dir[1024], fname[128];
    snprintf(vj_dir, sizeof(vj_dir), "%s/VJTOOLS", usb_root);
    snprintf(rep_dir, sizeof(rep_dir), "%s/VJTOOLS/support-reports", usb_root);
    if (ensure_dir_safe(vj_dir) != 0 || ensure_dir_safe(rep_dir) != 0) {
        free(draft);
        fprintf(stderr, "XZ_SUPPORT_ERROR USB_DIR_UNAVAILABLE\n");
        return 1;
    }
    snprintf(fname, sizeof(fname), "xz-support-report-%s.json", report_id);
    if (write_atomic(rep_dir, fname, draft, dlen) != 0) {
        free(draft);
        fprintf(stderr, "XZ_SUPPORT_ERROR USB_WRITE_FAILED\n");
        return 1;
    }
    free(draft);
    write_atomic(state_dir, "outcome.txt", "EXPORTED\n", 9);
    printf("EXPORTED %s %s\n", report_id, expected_sha256);
    return 0;
}

static int cmd_set_clock(const char *state_dir, const char *epoch_str) {
    if (ensure_dir_safe(state_dir) != 0) return 1;
    int64_t val = strtoll(epoch_str, NULL, 10);
    if (val < MIN_VALID_EPOCH || val > MAX_VALID_EPOCH) {
        fprintf(stderr, "XZ_SUPPORT_ERROR INVALID_REPORT_DATE\n");
        return 1;
    }
    char buf[64];
    int len = snprintf(buf, sizeof(buf), "%lld %lld\n", (long long)val, (long long)current_system_epoch());
    if (write_atomic(state_dir, "clock.txt", buf, (size_t)len) != 0) return 1;
    printf("CLOCK_SET %lld\n", (long long)val);
    return 0;
}

/* ---- Pending reports from the XZ (XZ-REPORTS.md sections 2, 4 and 5) ----
   `pending <state_dir>` reads one bounded request from stdin (the runtime writes it; the loader key travels this
   way so it never appears in argv or a file), collects the allowlisted diagnostics, saves the report to the
   loader stick first, then uploads it with the loader key. The stick copy is removed once the Worker accepts it,
   so a report is never lost to a hang, a power cut or a missing network. Output, one line each, stdout:
     SAVED_FIRST <id>                    the stick copy exists (flushed before the upload starts)
     SENT <id> <R-XXXX-XXXX> <url>       the Worker accepted it
     SAVED <id> <why>                    on the stick only; why: NO_IDENTITY NEEDS_CLOCK OFFLINE REJECTED UNKNOWN
     UNSAVED <stick> <why>               neither; stick: NO_STICK READ_ONLY FULL LIMIT FAILED */
#include "crockford.h"

#define PENDING_REQUEST_BYTES 4096u
#define PENDING_TEXT_CHARS 280u
#define PENDING_MAX_STICK_REPORTS 32u
#define PENDING_FACTS 64u
#define PENDING_DIARY_TAIL 32768u
#define PENDING_DIARY_HEADER 2048u
#define PENDING_ORIGIN DEFAULT_ORIGIN

struct pending_request {
    char report_id[33], topic[24], text[PENDING_TEXT_CHARS + 1], version[65];
    char iface[8], before[16], entry[8];
    int deck_view;
    unsigned alert[3]; /* deck 1, deck 2, the USB port (E-8307) */
    int settings[6], have_settings;
    char settings_sha[65], loader_id[12], loader_key[65], usb[1024];
};

/* The report v2 contract's enums (spec/support-contract.json). tests/verify_support_report.py checks they match. */
static const char *const v2_codes[] = {
    "ACTIVE_MISMATCH","CANCELLED","CLEANUP_MODULE_UNLOAD_FAILURE","CLEANUP_NODE_REMOVE_FAILURE","FAILED",
    "FATAL_BINDING_NOT_NEW","FATAL_PROCESS_SET_CHANGE","MOUNT_AMBIGUOUS","MUSIC_READY","NONE","NOT_READY",
    "OTA_ACCEPTED","OTA_BASE_OR_RECOVERY","OTA_ROLLBACK","OTA_TRIAL_READY","OWNER_MISMATCH","RACE_MUSIC_CAPTURE",
    "RACE_MUSIC_DESCRIPTORS","READY","READY_CH3_CH4","STOPPED_WITH_ERROR","STOP_PENDING",
    "TERMINATED_ADMISSION_DEBOUNCE_FAILURE","TERMINATED_APPLY_LOAD_FAILURE","TERMINATED_BIND_DEBOUNCE_FAILURE",
    "TERMINATED_EVENT_SOCKET_FAILURE","TERMINATED_FOREIGN_MODULE","TERMINATED_FOREIGN_MODULE_OR_NODE",
    "TERMINATED_FOREIGN_NODE","TERMINATED_MODULE_ADMISSION_FAILURE","TERMINATED_MUSIC_IDENTITY",
    "TERMINATED_NATIVE_END_OR_IDENTITY_CHANGE","TERMINATED_NODE_REMOVE_FAILURE","TERMINATED_NODE_WRITE_FAILURE",
    "TERMINATED_PID_WRITE_FAILURE","TERMINATED_PROBE_LOAD_FAILURE","TERMINATED_PROBE_UNLOAD_FAILURE",
    "TERMINATED_PROCESS_SET_CHANGE","TERMINATED_SCAN_FAILURE","TERMINATED_SIGNAL","TERMINATED_STOPPED_MEDIA_ADMISSION",
    "TERMINATED_STOP_MARKER","TERMINATED_UNSPECIFIED","UNSUPPORTED_KERNEL","UNSUPPORTED_MODEL",
    "UNSUPPORTED_MUSIC_FILESYSTEM","UNSUPPORTED_MUSIC_MOUNT","UNSUPPORTED_MUSIC_SERIAL_LENGTH",
    "UNSUPPORTED_UNIT_OR_USB2_TOPOLOGY","UNSUPPORTED_USB2_BLOCK_ANCESTRY","USB_GADGET_PROBE_COMPLETE",
    "VJ_NETWORK_READY","WAIT_AUDIO_DEVICE","WAIT_LOOPBACK","WAIT_MEDIA","WAIT_MOUNT","WAIT_MUSIC_DESCRIPTORS",
    "WAIT_MUSIC_MOUNT","WAIT_MUSIC_SERIAL_READ","WAIT_NATIVE_PAUSED","WAIT_SAFE_LOOPBACK_DISCONNECT"
};
static const char *const v2_states[] = {
    "ACTIVE","FAILED","NEW","READY","RETIRED","STOPPED","WAITING","ready","retryable","route_ready","stopped",
    "stopping","waiting","unknown","started","present","removed","changed","empty","loading","playing","paused",
    "error","off","following","locked","shown"
};
/* The diary's own words (diary/format.c), each also a v2 fact value. */
static const char *const diary_components[] = {"runtime","usb","settings","deck","sync","ram","ui"};
static const char *const diary_states[] = {"unknown","started","present","removed","changed","empty","loading","ready",
    "playing","paused","error","off","following","locked","shown"};
static const char *const diary_reasons[] = {"none","observed","track_load","ram_copy","read_busy","space_low","read_only",
    "io_error","queue_full","source_changed","no_grid","manual","clock_lost","range_limit","no_master",
    "phase_unavailable","discontinuity","loop","aligning","external_master","no_bpm","tempo_only"};
static const char *const diary_notices[] = {"none","other","track_missing","track_read","source_changed","route_changed",
    "key_busy","key_pending","jump_limit","cues_session","cues_invalid","scratch_truncated"};
static const char *const topics[] = {"GOT_WORSE","NOT_WORKING","WORK_DIFFERENTLY","NEW_IDEA","HOW_DO_I"};
static const char *const topic_kinds[] = {"REGRESSION","BUG","EXPECTATION","FEATURE","QUESTION"};
static const char *const places[] = {"UNKNOWN","PLAY","BROWSE","PLAYLIST","TAG_LIST","SEARCH","INFO","SHORTCUT",
    "UTILITY","SOURCE","STOCK_MENU","TRACK_LIST","GRID_ADJUST","TAKEOVER","GAME","OTHER"};
static const char *const entries[] = {"EXTRAS","ALERT","UPDATES"};
static const char *const deck_views[] = {"TWO_DECKS","FOUR_DECKS_1234","FOUR_DECKS_3124"};
static const char *const setting_names[6] = {"stems","gate","interface","classic_theme","deck_view","browse_preview"};
static const unsigned alert_codes[] = {7206, 8302, 8304, 8305, 8306, 8307};

static int in_list(const char *const *list, size_t n, const char *s, size_t len) {
    for (size_t i = 0; i < n; i++) if (strlen(list[i]) == len && !memcmp(list[i], s, len)) return (int)i;
    return -1;
}
#define LIST(a) (a), (sizeof(a) / sizeof((a)[0]))

static int parse_uint(const char *s, unsigned long max, unsigned long *out) {
    char *end = NULL;
    if (!s || *s < '0' || *s > '9') return -1;
    errno = 0;
    unsigned long v = strtoul(s, &end, 10);
    if (errno || !end || *end || v > max) return -1;
    *out = v;
    return 0;
}

static int known_alert(unsigned long code) {
    if (!code) return 1;
    for (size_t i = 0; i < sizeof(alert_codes) / sizeof(alert_codes[0]); i++) if (alert_codes[i] == code) return 1;
    return 0;
}

/* Every line is key=value; every key is known and appears at most once; every value is allowlisted. */
static int parse_pending_request(char *text, struct pending_request *r) {
    static const char *const keys[] = {"report_id","topic","text","version","interface","before","entry","deck_view",
        "alert","settings","settings_sha256","loader_id","loader_key","usb"};
    unsigned seen = 0;
    memset(r, 0, sizeof(*r));
    char *line = text;
    if (strncmp(line, "xz-pending-request/1\n", 21) != 0) return -1;
    line += 21;
    while (*line) {
        char *eol = strchr(line, '\n');
        if (!eol) return -1;
        *eol = 0;
        if (eol > line && eol[-1] == '\r') eol[-1] = 0;
        char *eq = strchr(line, '=');
        if (!eq) return -1;
        *eq = 0;
        const char *v = eq + 1;
        size_t vlen = strlen(v);
        int k = in_list(LIST(keys), line, strlen(line));
        if (k < 0 || (seen & (1u << k))) return -1;
        seen |= 1u << k;
        unsigned long n = 0;
        switch (k) {
        case 0: if (!is_lower_hex(v, 32)) return -1; strcpy(r->report_id, v); break;
        case 1: if (vlen && in_list(LIST(topics), v, vlen) < 0) return -1; strcpy(r->topic, v); break;
        case 2:
            if (vlen > PENDING_TEXT_CHARS) return -1;
            for (size_t i = 0; i < vlen; i++) if ((unsigned char)v[i] < 32 || (unsigned char)v[i] > 126) return -1;
            strcpy(r->text, v);
            break;
        case 3:
            if (vlen >= sizeof(r->version)) return -1;
            for (size_t i = 0; i < vlen; i++) {
                char c = v[i];
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || c == '.' || c == '-' || c == '+')) return -1;
            }
            strcpy(r->version, v);
            break;
        case 4: if (strcmp(v, "MODERN") && strcmp(v, "CLASSIC")) return -1; strcpy(r->iface, v); break;
        case 5: if (in_list(LIST(places), v, vlen) < 0) return -1; strcpy(r->before, v); break;
        case 6: if (in_list(LIST(entries), v, vlen) < 0) return -1; strcpy(r->entry, v); break;
        case 7: if (parse_uint(v, 2, &n)) return -1; r->deck_view = (int)n; break;
        case 8:
        case 9: {
            char copy[128];
            if (vlen >= sizeof(copy)) return -1;
            strcpy(copy, v);
            unsigned want = k == 8 ? 3u : 6u, got = 0;
            for (char *part = copy, *next; part; part = next) {
                next = strchr(part, ',');
                if (next) *next++ = 0;
                if (got == want || parse_uint(part, k == 8 ? 9999u : 1000u, &n)) return -1;
                if (k == 8) { if (!known_alert(n)) return -1; r->alert[got] = (unsigned)n; }
                else r->settings[got] = (int)n;
                got++;
            }
            if (got != want) return -1;
            if (k == 9) r->have_settings = 1;
            break;
        }
        case 10: if (!is_lower_hex(v, 64)) return -1; strcpy(r->settings_sha, v); break;
        case 11: if (vlen > 32 || xz_crockford_ref(v, vlen, 'L', r->loader_id)) return -1; break;
        case 12: if (!is_lower_hex(v, 64)) return -1; strcpy(r->loader_key, v); break;
        case 13:
            if (vlen >= sizeof(r->usb) || (vlen && (v[0] != '/' || strstr(v, "..")))) return -1;
            strcpy(r->usb, v);
            break;
        }
        line = eol + 1;
    }
    /* report_id, interface, before, entry and deck_view are required. A key needs its ID. */
    if ((seen & 0xf1u) != 0xf1u) return -1;
    if (!r->loader_id[0] != !r->loader_key[0]) return -1;
    return 0;
}

static int read_stdin_request(char *buf, size_t cap) {
    size_t used = 0;
    for (;;) {
#if defined(_WIN32)
        int n = _read(0, buf + used, (unsigned)(cap - 1 - used));
#else
        ssize_t n = read(0, buf + used, cap - 1 - used);
#endif
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) return -1;
        if (n == 0) break;
        used += (size_t)n;
        if (used == cap - 1) return -1; /* larger than any valid request */
    }
    buf[used] = 0;
    return memchr(buf, 0, used) ? -1 : 0;
}

/* A bounded JSON writer: once anything does not fit, the whole report is refused rather than cut. */
struct out { char *p; size_t len, cap; int full; };
static void put(struct out *o, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void put(struct out *o, const char *fmt, ...) {
    if (o->full) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(o->p + o->len, o->cap - o->len, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= o->cap - o->len) { o->full = 1; return; }
    o->len += (size_t)n;
}
static void put_string(struct out *o, const char *s) {
    char esc[2 * PENDING_TEXT_CHARS * 4 + 64];
    json_escape(s, esc, sizeof(esc));
    put(o, "\"%s\"", esc);
}

/* One fact as the v2 schema has it: its own fields, then when it was. */
struct fact { char body[320]; };
struct facts { struct fact f[PENDING_FACTS]; unsigned n, start; unsigned omitted; };
static void fact_add(struct facts *fs, const char *body) {
    if (fs->n) {
        const struct fact *last = &fs->f[(fs->start + fs->n - 1) % PENDING_FACTS];
        if (!strcmp(last->body, body)) return; /* the same fact again */
    }
    if (strlen(body) >= sizeof(fs->f[0].body)) { fs->omitted++; return; }
    if (fs->n == PENDING_FACTS) { fs->start = (fs->start + 1) % PENDING_FACTS; fs->n--; fs->omitted++; }
    strcpy(fs->f[(fs->start + fs->n) % PENDING_FACTS].body, body);
    fs->n++;
}

/* Known codes and states in raw log text; nothing else survives. */
static void scan_codes(const char *raw, const char *timing, struct facts *fs, unsigned *redactions) {
    const char *line = raw;
    while (*line) {
        const char *end = strchr(line, '\n');
        if (!end) end = line + strlen(line);
        int found = 0;
        for (const char *p = line; p < end;) {
            if (!isalnum((unsigned char)*p) && *p != '_') { p++; continue; }
            const char *start = p++;
            while (p < end && (isalnum((unsigned char)*p) || *p == '_')) p++;
            size_t len = (size_t)(p - start);
            char body[320];
            int c = in_list(LIST(v2_codes), start, len), s = c < 0 ? in_list(LIST(v2_states), start, len) : -1;
            if (c >= 0) snprintf(body, sizeof(body), "\"code\":\"%s\",%s", v2_codes[c], timing);
            else if (s >= 0) snprintf(body, sizeof(body), "\"state\":\"%s\",%s", v2_states[s], timing);
            else continue;
            fact_add(fs, body);
            found = 1;
        }
        if (!found && end > line) { fs->omitted++; (*redactions)++; }
        line = *end ? end + 1 : end;
    }
}

static const char *const unknown_time = "\"relative_seconds\":null,\"clock_basis\":\"unknown\",\"window_match\":\"unknown\"";
static const char *const current_time = "\"relative_seconds\":null,\"clock_basis\":\"current\",\"window_match\":\"unknown\"";

static void put_item(struct out *o, int *first, const char *source, const char *slot, int parser, const char *clock,
                     const struct facts *fs, const char *missing, int truncated) {
    unsigned char id[16];
    char hex[33];
    if (random_bytes(id, sizeof(id)) != 0) memset(id, 0, sizeof(id));
    to_hex(id, sizeof(id), hex);
    put(o, "%s{\"item_id\":\"%s\",\"source_id\":\"%s\",\"source_slot\":\"%s\",\"parser_version\":%d,"
           "\"capture\":\"at_collection\",\"clock_confidence\":\"%s\",\"selected\":true,\"facts\":[",
        *first ? "" : ",", hex, source, slot, parser, clock);
    for (unsigned i = 0; fs && i < fs->n; i++) put(o, "%s{%s}", i ? "," : "", fs->f[(fs->start + i) % PENDING_FACTS].body);
    if (missing) put(o, "],\"missing_reason\":\"%s\"", missing);
    else put(o, "],\"missing_reason\":null");
    put(o, ",\"truncated\":%s,\"omission_count\":%u}", truncated ? "true" : "false",
        fs ? (fs->omitted > 1000000u ? 1000000u : fs->omitted) : 0u);
    *first = 0;
}

static void collect_log_item(struct out *o, int *first, const char *root, const char *rel, const char *source,
                             const char *clock, const char *timing, unsigned *redactions) {
    char path[1024], raw[MAX_ITEM_RAW];
    size_t len = 0;
    int truncated = 0;
    static struct facts fs;
    memset(&fs, 0, sizeof(fs));
    snprintf(path, sizeof(path), "%s%s", root, rel);
    if (read_file_bounded(path, raw, sizeof(raw), &len, &truncated) != 0) {
        put_item(o, first, source, "xz_a", 1, "unknown", NULL, "missing", 0);
        return;
    }
    scan_codes(raw, timing, &fs, redactions);
    put_item(o, first, source, "xz_a", 1, clock, &fs, NULL, truncated);
}

/* One integer token in [lo,hi] (or null when nullable); -2 for null, -1 when invalid. */
static long long token_int(const char *json, const jsmntok_t *t, long long lo, long long hi, int nullable, int *is_null) {
    char text[24];
    size_t n = (size_t)(t->end - t->start);
    *is_null = 0;
    if (t->type != JSMN_PRIMITIVE || !n || n >= sizeof(text)) return -1;
    memcpy(text, json + t->start, n);
    text[n] = 0;
    if (!strcmp(text, "null")) { *is_null = 1; return nullable ? 0 : -1; }
    char *end = NULL;
    errno = 0;
    long long v = strtoll(text, &end, 10);
    if (errno || !end || *end || v < lo || v > hi) { *is_null = -1; return 0; }
    return v;
}
static int token_is(const char *json, const jsmntok_t *t, const char *s) {
    size_t n = strlen(s);
    return t->type == JSMN_STRING && (size_t)(t->end - t->start) == n && !memcmp(json + t->start, s, n);
}
static int token_word(const char *json, const jsmntok_t *t, const char *const *list, size_t count) {
    if (t->type != JSMN_STRING) return -1;
    return in_list(list, count, json + t->start, (size_t)(t->end - t->start));
}

struct diary_clock { int known; int64_t epoch, now; };
static void diary_timing(char *out, size_t cap, const struct diary_clock *c, long long mono) {
    if (!c->known) { snprintf(out, cap, "%s", unknown_time); return; }
    int64_t at = c->epoch + mono / 1000 - c->now;
    if (at < -3600 || at > 3600) {
        snprintf(out, cap, "\"relative_seconds\":null,\"clock_basis\":\"device_utc\",\"window_match\":\"outside\"");
        return;
    }
    snprintf(out, cap, "\"relative_seconds\":%lld,\"clock_basis\":\"device_utc\",\"window_match\":\"%s\"",
             (long long)at, at >= -900 && at <= 300 ? "inside" : "outside");
}

/* One diary line (diary/format.c) as one fact, or -1. Every key is known and in range. */
static int diary_fact(const char *json, size_t len, const struct diary_clock *clock, char *body, size_t cap) {
    jsmn_parser p;
    jsmntok_t t[48];
    jsmn_init(&p);
    int count = jsmn_parse(&p, json, len, t, 48);
    if (count < 1 || t[0].type != JSMN_OBJECT) return -1;
    int type = -1;
    long long mono = -1;
    char fields[256] = "";
    size_t used = 0;
    for (int i = 1; i < count;) {
        const jsmntok_t *k = &t[i], *v = &t[i + 1];
        if (k->type != JSMN_STRING || i + 1 >= count) return -1;
        int is_null = 0;
        char piece[96] = "";
        if (token_is(json, k, "type")) {
            type = token_is(json, v, "event") ? 0 : token_is(json, v, "metrics") ? 1 : -1;
            if (type < 0) return -1;
        } else if (token_is(json, k, "sequence")) {
            token_int(json, v, 0, 2147483647LL, 0, &is_null);
            if (is_null) return -1;
        } else if (token_is(json, k, "monotonic_ms")) {
            mono = token_int(json, v, 0, 604800000LL, 0, &is_null);
            if (is_null) return -1;
        } else if (token_is(json, k, "component") || token_is(json, k, "state") || token_is(json, k, "reason") ||
                   token_is(json, k, "notice")) {
            const char *const *list = token_is(json, k, "component") ? diary_components : token_is(json, k, "state") ? diary_states :
                token_is(json, k, "reason") ? diary_reasons : diary_notices;
            size_t n = list == diary_components ? sizeof(diary_components) / sizeof(*list) :
                list == diary_states ? sizeof(diary_states) / sizeof(*list) :
                list == diary_reasons ? sizeof(diary_reasons) / sizeof(*list) : sizeof(diary_notices) / sizeof(*list);
            int w = token_word(json, v, list, n);
            if (w < 0) return -1;
            snprintf(piece, sizeof(piece), "\"%.*s\":\"%s\",", k->end - k->start, json + k->start, list[w]);
        } else if (token_is(json, k, "deck") || token_is(json, k, "slot")) {
            long long n = token_int(json, v, 0, token_is(json, k, "deck") ? 4 : 2, 0, &is_null);
            if (is_null) return -1;
            snprintf(piece, sizeof(piece), "\"%.*s\":%lld,", k->end - k->start, json + k->start, n);
        } else if (token_is(json, k, "detail_code")) {
            long long n = token_int(json, v, 0, 9999, 0, &is_null);
            if (is_null || !known_alert((unsigned long)n)) return -1;
            snprintf(piece, sizeof(piece), "\"detail_code\":%lld,", n);
        } else if (token_is(json, k, "cpu_percent") || token_is(json, k, "free_ram_kib") ||
                   token_is(json, k, "usb_read_latency_us") || token_is(json, k, "dropped_records")) {
            long long hi = token_is(json, k, "cpu_percent") ? 100 : token_is(json, k, "free_ram_kib") ? 1048576 : 1000000;
            int nullable = !token_is(json, k, "dropped_records");
            long long n = token_int(json, v, 0, hi, nullable, &is_null);
            if (is_null < 0 || (is_null && !nullable)) return -1;
            if (is_null) snprintf(piece, sizeof(piece), "\"%.*s\":null,", k->end - k->start, json + k->start);
            else snprintf(piece, sizeof(piece), "\"%.*s\":%lld,", k->end - k->start, json + k->start, n);
        } else if (token_is(json, k, "sync_phase_error_ms") || token_is(json, k, "key_shift_max_us") ||
                   token_is(json, k, "audio_underrun_frames")) {
            long long lo = token_is(json, k, "sync_phase_error_ms") ? -10000 : 0;
            long long hi = token_is(json, k, "sync_phase_error_ms") ? 10000 : token_is(json, k, "key_shift_max_us") ? 1000000 : 2147483647LL;
            if (v->type != JSMN_ARRAY || v->size != 4) return -1;
            size_t w = (size_t)snprintf(piece, sizeof(piece), "\"%.*s\":[", k->end - k->start, json + k->start);
            for (int e = 0; e < 4; e++) {
                if (i + 2 + e >= count) return -1;
                long long n = token_int(json, &t[i + 2 + e], lo, hi, 1, &is_null);
                if (is_null < 0) return -1;
                if (is_null) w += (size_t)snprintf(piece + w, sizeof(piece) - w, "%snull", e ? "," : "");
                else w += (size_t)snprintf(piece + w, sizeof(piece) - w, "%s%lld", e ? "," : "", n);
            }
            snprintf(piece + w, sizeof(piece) - w, "],");
            i += 4; /* the array's elements */
        } else return -1;
        size_t pn = strlen(piece);
        if (used + pn >= sizeof(fields)) return -1;
        memcpy(fields + used, piece, pn + 1);
        used += pn;
        i += 2;
    }
    if (type < 0 || mono < 0 || !used) return -1;
    char timing[128];
    diary_timing(timing, sizeof(timing), clock, mono);
    int n = snprintf(body, cap, "%s%s", fields, timing);
    return n < 0 || (size_t)n >= cap ? -1 : 0;
}

/* The loader stick's current diary segment: the newest generation's tail, as allowlisted facts. */
static void collect_diary_item(struct out *o, int *first, const char *usb, int64_t now, int now_known) {
    static struct facts fs;
    static char raw[PENDING_DIARY_TAIL + 1];
    memset(&fs, 0, sizeof(fs));
    if (!usb[0]) { put_item(o, first, "usb_diary", "usb_a", 2, "unknown", NULL, "missing", 0); return; }
    char dir[1100], path[1200], best[1200] = "";
    unsigned long best_gen = 0;
    snprintf(dir, sizeof(dir), "%s/VJ.Tools/XZ-Mods-diary", usb);
    for (unsigned i = 0; i < 8; i++) {
        char head[PENDING_DIARY_HEADER];
        size_t len = 0;
        int truncated = 0;
        snprintf(path, sizeof(path), "%s/segment-%02u.jsonl", dir, i);
        if (read_file_bounded(path, head, sizeof(head), &len, &truncated) != 0 || truncated) continue;
        char *key = strstr(head, "\"segment_generation\":");
        if (!key || strncmp(head, "{\"type\":\"session\",\"schema\":\"xz-mods-diary/2\"", 44) != 0) continue;
        char *end = NULL;
        unsigned long gen = strtoul(key + 21, &end, 10);
        if (!end || end == key + 21 || gen > 2147483647UL) continue;
        if (!best[0] || gen > best_gen) { best_gen = gen; strcpy(best, path); }
    }
    if (!best[0]) { put_item(o, first, "usb_diary", "usb_a", 2, "unknown", NULL, "missing", 0); return; }
    /* Header first: the session's start epoch maps monotonic times to the device clock. */
    char head[PENDING_DIARY_HEADER];
    size_t hlen = 0;
    int truncated = 0;
    struct diary_clock clock = {0, 0, now};
    if (read_file_bounded(best, head, sizeof(head), &hlen, &truncated) == 0) {
        char *nl = strchr(head, '\n');
        if (nl) *nl = 0;
        jsmn_parser p;
        jsmntok_t t[40];
        jsmn_init(&p);
        int count = jsmn_parse(&p, head, strlen(head), t, 40);
        char version[65] = "", iface[8] = "";
        long long deck_view = 0, epoch = -1;
        for (int i = 1; count > 0 && t[0].type == JSMN_OBJECT && i + 1 < count; i++) {
            if (t[i].type != JSMN_STRING || t[i].size != 1) continue;
            const jsmntok_t *v = &t[i + 1];
            int is_null = 0;
            size_t n = (size_t)(v->end - v->start);
            if (token_is(head, &t[i], "epoch")) { long long e = token_int(head, v, 1767225600LL, 4102444799LL, 1, &is_null); if (!is_null) epoch = e; }
            else if (token_is(head, &t[i], "version") && v->type == JSMN_STRING && n < sizeof(version)) {
                int ok = 1;
                for (size_t c = 0; c < n; c++) {
                    char ch = head[v->start + c];
                    if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || ch == '.' || ch == '-' || ch == '+')) ok = 0;
                }
                if (ok) { memcpy(version, head + v->start, n); version[n] = 0; }
            } else if (token_is(head, &t[i], "interface") && (token_is(head, v, "MODERN") || token_is(head, v, "CLASSIC")))
                snprintf(iface, sizeof(iface), "%.*s", (int)n, head + v->start);
            else if (token_is(head, &t[i], "deck_view")) { long long d = token_int(head, v, 2, 4, 0, &is_null); if (!is_null && (d == 2 || d == 4)) deck_view = d; }
        }
        if (epoch > 0 && now_known) { clock.known = 1; clock.epoch = epoch; }
        if (version[0] && iface[0] && deck_view) {
            char body[320];
            snprintf(body, sizeof(body), "\"component\":\"runtime\",\"version\":\"%s\",\"provenance\":\"usb_record\","
                     "\"interface\":\"%s\",\"deck_view\":%lld,%s", version, iface, deck_view, unknown_time);
            fact_add(&fs, body);
        }
    }
    size_t len = 0;
    if (read_file_bounded(best, raw, sizeof(raw), &len, &truncated) != 0) {
        put_item(o, first, "usb_diary", "usb_a", 2, "unknown", NULL, "invalid", 0);
        return;
    }
    char *line = raw;
    if (truncated) { char *nl = strchr(raw, '\n'); line = nl ? nl + 1 : raw + len; } /* the first line is cut */
    while (*line) {
        char *nl = strchr(line, '\n');
        if (!nl) { fs.omitted++; break; } /* still being written */
        *nl = 0;
        char body[320];
        if (strncmp(line, "{\"type\":\"session\"", 17) != 0) {
            if (diary_fact(line, strlen(line), &clock, body, sizeof(body)) == 0) fact_add(&fs, body);
            else fs.omitted++;
        }
        line = nl + 1;
    }
    put_item(o, first, "usb_diary", "usb_a", 2, clock.known ? "known" : "unknown", &fs, NULL, truncated);
}

static void collect_settings_item(struct out *o, int *first, const struct pending_request *r) {
    static struct facts fs;
    memset(&fs, 0, sizeof(fs));
    if (!r->have_settings) { put_item(o, first, "usb_settings", "xz_a", 1, "unknown", NULL, "not_available", 0); return; }
    char body[320];
    for (int i = 0; i < 6; i++) {
        snprintf(body, sizeof(body), "\"setting\":\"%s\",\"value\":%d,\"valid\":true,%s", setting_names[i], r->settings[i], current_time);
        fact_add(&fs, body);
    }
    if (r->settings_sha[0]) {
        snprintf(body, sizeof(body), "\"component\":\"settings\",\"sha256\":\"%s\",%s", r->settings_sha, current_time);
        fact_add(&fs, body);
    }
    put_item(o, first, "usb_settings", "xz_a", 1, "current", &fs, NULL, 0);
}

/* The upload body and the stick copy: one envelope around a report v2. NULL when it would not fit. */
static char *build_pending_report(const struct pending_request *r, const char *state_dir, const char *src_root, size_t *out_len) {
    struct out o = { calloc(1, MAX_DRAFT_BYTES), 0, MAX_DRAFT_BYTES, 0 };
    if (!o.p) return NULL;
    unsigned redactions = 0, omitted = 0;
    int topic = r->topic[0] ? in_list(LIST(topics), r->topic, strlen(r->topic)) : -1;
    char clean[PENDING_TEXT_CHARS * 2 + 64] = "";
    if (r->text[0]) sanitize_text(r->text, clean, sizeof(clean), &redactions, &omitted);
    for (size_t i = strlen(clean); i > 0 && (clean[i - 1] == ' ' || clean[i - 1] == '\n'); i--) clean[i - 1] = 0;
    int typed = clean[0] != 0;
    const char *category = r->alert[2] == 8307 ? "bridge" : (r->alert[0] && r->alert[0] != 7206) || (r->alert[1] && r->alert[1] != 7206) ? "app_usb" : "other";
    int64_t now = 0;
    int used_override = 0;
    int now_known = resolve_effective_clock(state_dir, &now, &used_override) == 0;

    put(&o, "{\"schema\":\"xz-pending-report/1\",\"report_id\":\"%s\",\"loader_id\":", r->report_id);
    if (r->loader_id[0]) put(&o, "\"%s\"", r->loader_id); else put(&o, "null");
    put(&o, ",\"description_from_xz\":%s,\"screen\":{\"interface\":\"%s\",\"before\":\"%s\",\"entry\":\"%s\",\"deck_view\":\"%s\",\"alerts\":[",
        typed ? "true" : "false", r->iface, r->before, r->entry, deck_views[r->deck_view]);
    int any = 0;
    for (int i = 0; i < 3; i++) if (r->alert[i]) { put(&o, "%s{\"deck\":%d,\"code\":%u}", any ? "," : "", i < 2 ? i + 1 : 0, r->alert[i]); any = 1; }
    put(&o, "]},\"report\":{\"schema\":\"xz-support-report/2\",\"report_id\":\"%s\",\"origin\":\"controller\",\"kind_hint\":", r->report_id);
    if (topic >= 0) put(&o, "\"%s\"", topic_kinds[topic]); else put(&o, "null");
    put(&o, ",\"description\":");
    put_string(&o, typed ? clean : "Sent from the XZ without a description. The DJ adds what happened on their computer.");
    put(&o, ",\"category\":\"%s\",\"build\":{\"desktop_version\":\"unknown\",\"desktop_provenance\":\"unknown\","
            "\"runtime_version\":\"unknown\",\"runtime_provenance\":\"unknown\"},"
            "\"incident\":{\"before_seconds\":900,\"after_seconds\":300,\"clock_confidence\":\"%s\"},"
            "\"private\":{\"contact\":null,\"contact_consent\":false,\"items\":[", category, now_known ? "known" : "unknown");
    int first = 1;
    /* The running runtime: its version and hash, then its log's known events. */
    {
        static struct facts fs;
        memset(&fs, 0, sizeof(fs));
        char body[320], sha[65] = "", path[1024], raw[MAX_ITEM_RAW];
        snprintf(path, sizeof(path), "%s/dev/shm/libxz-mods.so", src_root);
        int has_sha = hash_regular_file(path, sha) == 0;
        snprintf(body, sizeof(body), "\"component\":\"runtime\",\"version\":\"%s\",\"provenance\":\"running_runtime\"%s%s%s,%s",
                 r->version[0] && strlen(r->version) <= 64 ? r->version : "unknown",
                 has_sha ? ",\"sha256\":\"" : "", has_sha ? sha : "", has_sha ? "\"" : "", current_time);
        fact_add(&fs, body);
        size_t len = 0;
        int truncated = 0;
        snprintf(path, sizeof(path), "%s/dev/shm/xz-mods.log", src_root);
        if (read_file_bounded(path, raw, sizeof(raw), &len, &truncated) == 0) scan_codes(raw, unknown_time, &fs, &redactions);
        put_item(&o, &first, "runtime_events", "xz_a", 1, "unknown", &fs, NULL, truncated);
    }
    {
        char path[1024];
        struct stat st;
        snprintf(path, sizeof(path), "%s/dev/shm/xz-four-deck/status.json", src_root);
        if (stat(path, &st) == 0) collect_log_item(&o, &first, src_root, "/dev/shm/xz-four-deck/status.json", "bridge_status", "current", current_time, &redactions);
        else collect_log_item(&o, &first, src_root, "/dev/shm/xz-four-deck/status", "bridge_fallback", "current", current_time, &redactions);
    }
    collect_log_item(&o, &first, src_root, "/dev/shm/xz-four-deck/bridge.log", "bridge_history", "unknown", unknown_time, &redactions);
    collect_log_item(&o, &first, src_root, "/dev/shm/xz-usb-coldplug.log", "coldplug", "unknown", unknown_time, &redactions);
    collect_settings_item(&o, &first, r);
    collect_diary_item(&o, &first, r->usb, now, now_known);
    unsigned char gen[16];
    char gen_hex[33];
    if (random_bytes(gen, sizeof(gen)) != 0) memset(gen, 0, sizeof(gen));
    to_hex(gen, sizeof(gen), gen_hex);
    put(&o, "]},\"coverage\":{\"omitted_items\":0,\"unfinished_sources\":0,\"scan_generation\":\"%s\"},"
            "\"redactions\":{\"count\":%u,\"omitted_lines\":%u},\"reporter_hints\":{\"topic\":",
        gen_hex, redactions > 1000000u ? 1000000u : redactions, omitted > 1000000u ? 1000000u : omitted);
    if (topic >= 0) put(&o, "\"%s\"", topics[topic]); else put(&o, "null");
    put(&o, "}}}\n");
    if (o.full) { free(o.p); return NULL; }
    *out_len = o.len;
    return o.p;
}

/* How the stick refused a write, in the words the XZ shows. */
static const char *stick_error(int err) {
    if (err == EROFS || err == EACCES || err == EPERM) return "READ_ONLY";
    if (err == ENOSPC) return "FULL";
#ifdef EDQUOT
    if (err == EDQUOT) return "FULL";
#endif
    if (err == ENOENT || err == ENODEV || err == ENXIO || err == EIO) return "NO_STICK";
    return "FAILED";
}

static unsigned count_pending(const char *dir, const char *except) {
    unsigned n = 0;
#if defined(_WIN32)
    char pattern[1200];
    struct _finddata_t found;
    snprintf(pattern, sizeof(pattern), "%s/xz-report-*.json", dir);
    intptr_t h = _findfirst(pattern, &found);
    if (h == -1) return 0;
    do { if (strcmp(found.name, except) && ++n > 4096) break; } while (_findnext(h, &found) == 0);
    _findclose(h);
#else
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    unsigned seen = 0;
    while ((e = readdir(d)) && ++seen <= 4096) {
        size_t len = strlen(e->d_name);
        if (len > 15 && !strncmp(e->d_name, "xz-report-", 10) && !strcmp(e->d_name + len - 5, ".json") && strcmp(e->d_name, except)) n++;
    }
    closedir(d);
#endif
    return n;
}

/* The stick copy: <usb>/VJ.Tools/XZ-Mods-reports/pending/xz-report-<id>.json, written atomically. */
static const char *save_pending(const char *usb, const char *id, const char *body, size_t len, char *path_out, size_t path_cap) {
    struct stat st;
    if (!usb || !usb[0] || stat(usb, &st) != 0 || !(st.st_mode & S_IFDIR)) return "NO_STICK";
    char a[1100], b[1200], c[1300], name[64];
    snprintf(a, sizeof(a), "%s/VJ.Tools", usb);
    snprintf(b, sizeof(b), "%s/XZ-Mods-reports", a);
    snprintf(c, sizeof(c), "%s/pending", b);
    if (ensure_dir_safe(a) != 0 || ensure_dir_safe(b) != 0 || ensure_dir_safe(c) != 0) return stick_error(errno);
    snprintf(name, sizeof(name), "xz-report-%s.json", id);
    if (count_pending(c, name) >= PENDING_MAX_STICK_REPORTS) return "LIMIT";
    if (write_atomic(c, name, body, len) != 0) return stick_error(errno);
    snprintf(path_out, path_cap, "%s/%s", c, name);
    return NULL;
}

static int valid_tracking_url(const char *s, size_t n) {
    static const char *const prefixes[] = {DEFAULT_ORIGIN "/", "https://xzmods.com/"};
    if (n < 12 || n > 160) return 0;
    int ok = 0;
    for (size_t i = 0; i < 2; i++) if (n > strlen(prefixes[i]) && !strncmp(s, prefixes[i], strlen(prefixes[i]))) ok = 1;
    for (size_t i = 0; ok && i < n; i++) {
        char ch = s[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || strchr(":/._-", ch))) ok = 0;
    }
    return ok;
}

/* The Worker's answer: {"report_id":"<the same id>","reference":"R-XXXX-XXXX"[,"tracking_url":"https://..."]}.
   Other keys are ignored; the ones read must be exactly right. */
static int parse_pending_receipt(const char *json, const char *report_id, char reference[12], char *url, size_t url_cap) {
    jsmn_parser p;
    jsmntok_t t[64];
    jsmn_init(&p);
    size_t len = strlen(json);
    int count = jsmn_parse(&p, json, len, t, 64);
    if (count < 1 || t[0].type != JSMN_OBJECT) return -1;
    for (size_t n = (size_t)t[0].end; n < len; n++) if (!isspace((unsigned char)json[n])) return -1;
    unsigned seen = 0;
    url[0] = 0;
    for (int i = 1; i < count; i++) {
        if (t[i].type != JSMN_STRING || t[i].size != 1 || i + 1 >= count) continue; /* only top-level keys own a value */
        const jsmntok_t *v = &t[i + 1];
        size_t n = (size_t)(v->end - v->start);
        if (token_is(json, &t[i], "report_id")) {
            if (v->type != JSMN_STRING || n != 32 || memcmp(json + v->start, report_id, 32)) return -1;
            seen |= 1;
        } else if (token_is(json, &t[i], "reference")) {
            if (v->type != JSMN_STRING || n > 16 || xz_crockford_ref(json + v->start, n, 'R', reference)) return -1;
            seen |= 2;
        } else if (token_is(json, &t[i], "tracking_url")) {
            if (v->type != JSMN_STRING || !valid_tracking_url(json + v->start, n) || n >= url_cap) return -1;
            memcpy(url, json + v->start, n);
            url[n] = 0;
        }
    }
    if (seen != 3) return -1;
    if (!url[0]) snprintf(url, url_cap, "%s/r/%s", DEFAULT_ORIGIN, reference);
    return 0;
}

static int cmd_pending(const char *state_dir) {
    static char request[PENDING_REQUEST_BYTES];
    struct pending_request r;
    if (read_stdin_request(request, sizeof(request)) != 0 || parse_pending_request(request, &r) != 0) {
        fprintf(stderr, "XZ_SUPPORT_ERROR INVALID_PENDING_REQUEST\n");
        return 2;
    }
    if (ensure_dir_safe(state_dir) != 0) {
        fprintf(stderr, "XZ_SUPPORT_ERROR CANNOT_CREATE_STATE_DIR\n");
        return 2;
    }
    const char *src_root = "";
    if (test_mode_enabled()) {
        const char *env_root = getenv("XZ_SUPPORT_SOURCE_ROOT");
        if (env_root) src_root = env_root;
    }
    size_t len = 0;
    char *body = build_pending_report(&r, state_dir, src_root, &len);
    if (!body) {
        fprintf(stderr, "XZ_SUPPORT_ERROR PENDING_REPORT_TOO_LARGE\n");
        return 2;
    }
    char saved_path[1400] = "";
    const char *stick = save_pending(r.usb, r.report_id, body, len, saved_path, sizeof(saved_path));
    if (!stick) { printf("SAVED_FIRST %s\n", r.report_id); fflush(stdout); }
    const char *why = "NO_IDENTITY";
    if (r.loader_id[0]) {
        const char *origin = PENDING_ORIGIN;
        if (test_mode_enabled()) {
            const char *test_origin = getenv("XZ_SUPPORT_TEST_ORIGIN");
            if (test_origin && *test_origin) origin = test_origin;
        }
        char sha[65], headers[512], resp[16384] = {0}, reference[12] = "", url[192] = "";
        sha256_hex(body, len, sha);
        snprintf(headers, sizeof(headers), "Authorization: Bearer %s\r\nX-Loader-ID: %s\r\nX-Report-ID: %s\r\nX-Report-SHA256: %s\r\n",
                 r.loader_key, r.loader_id, r.report_id, sha);
        int status = 0;
        int rc = https_exchange(state_dir, origin, "POST", "/v2/xz/pending", headers, body, len, 2, &status, resp, sizeof(resp));
        mbedtls_platform_zeroize(headers, sizeof(headers));
        if (rc == 0 && (status == 200 || status == 201) && parse_pending_receipt(resp, r.report_id, reference, url, sizeof(url)) == 0) {
            if (saved_path[0]) unlink(saved_path); /* the Worker has it; the app finds it in the queue */
            free(body);
            char id[33];
            strcpy(id, r.report_id);
            mbedtls_platform_zeroize(&r, sizeof(r));
            printf("SENT %s %s %s\n", id, reference, url);
            return 0;
        }
        why = rc == 3 ? "NEEDS_CLOCK" : rc == 4 || (rc == 0 && status >= 200 && status < 300) ? "UNKNOWN" :
              rc == 0 ? "REJECTED" : "OFFLINE";
        if (rc == 0 && !(status >= 200 && status < 300)) fprintf(stderr, "XZ_SUPPORT_ERROR PENDING_HTTP_%d\n", status);
    }
    free(body);
    char id[33];
    strcpy(id, r.report_id);
    mbedtls_platform_zeroize(&r, sizeof(r));
    if (!stick) { printf("SAVED %s %s\n", id, why); return 0; }
    printf("UNSAVED %s %s\n", stick, why);
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "XZ_SUPPORT_ERROR USAGE\n");
        return 1;
    }
    const char *cmd = argv[1];
    const char *state_dir = argv[2];
    if (strcmp(cmd, "prepare") == 0) {
        const char *cat = argc > 3 ? argv[3] : "bridge";
        const char *title = argc > 4 ? argv[4] : "Four-deck bridge did not reach READY_CH3_CH4";
        const char *desc = argc > 5 ? argv[5] : "";
        return cmd_prepare(state_dir, cat, title, desc);
    }
    if (strcmp(cmd, "set-clock") == 0 && argc >= 4) {
        return cmd_set_clock(state_dir, argv[3]);
    }
    if (strcmp(cmd, "send") == 0 && argc >= 4) {
        return cmd_send(state_dir, argv[3], argc > 4 ? argv[4] : NULL);
    }
    if (strcmp(cmd, "check") == 0) {
        return cmd_check(state_dir, argc > 3 ? argv[3] : NULL);
    }
    if (strcmp(cmd, "export") == 0 && argc >= 5) {
        return cmd_export(state_dir, argv[3], argv[4]);
    }
    if (strcmp(cmd, "pending") == 0 && argc == 3) {
        return cmd_pending(state_dir);
    }
    fprintf(stderr, "XZ_SUPPORT_ERROR UNKNOWN_CMD\n");
    return 1;
}
