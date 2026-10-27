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
        if (n <= 0) { close(fd); unlink(tmp_path); return -1; }
        p += (size_t)n;
        left -= (size_t)n;
    }
#if !defined(_WIN32)
    fsync(fd);
#endif
    close(fd);
#if defined(_WIN32)
    _unlink(final_path);
#endif
    if (rename(tmp_path, final_path) != 0) {
        unlink(tmp_path);
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
             "{\"app_version\":\"0.5.0-preview.22.8\",\"runtime_sha256\":%s%s%s,\"bridge_sha256\":%s%s%s,\"module_sha256\":%s%s%s}",
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
        "    \"app_version\": \"0.5.0-preview.22.8\",\n"
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

static int https_exchange(const char *state_dir, const char *origin_url, const char *method, const char *subpath,
                          const char *report_id, const char *expected_sha, const char *receipt_key,
                          const char *body, size_t body_len, int mark_in_flight,
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

    int64_t deadline = mbedtls_ms_time() + 15000;
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
            "X-Report-ID: %s\r\n"
            "X-Report-SHA256: %s\r\n"
            "X-Report-Consent: public-summary-private-logs/v1\r\n"
            "Authorization: Bearer %s\r\n"
            "Connection: close\r\n\r\n",
            method, base_path, subpath, host, (unsigned)body_len,
            report_id, expected_sha, receipt_key);
    } else {
        hlen = snprintf(req_head, sizeof(req_head),
            "%s %s%s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: xz-support/1\r\n"
            "Accept: application/json\r\n"
            "Authorization: Bearer %s\r\n"
            "Connection: close\r\n\r\n",
            method, base_path, subpath, host, receipt_key);
    }

    if (mark_in_flight) {
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
        write_atomic(state_dir, "outcome.txt", "FAILED\n", 7);
        exit_code = 1;
        goto cleanup;
    }

    const char *bstart = hdr_end + 4;
    size_t blen = rtot - (size_t)(bstart - raw_resp);
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
    char resp[16384] = {0};
    int rc = https_exchange(state_dir, origin_url ? origin_url : DEFAULT_ORIGIN,
                            "POST", "/v1/reports", report_id, expected_sha256, receipt_key,
                            draft, dlen, 1, &http_status, resp, sizeof(resp));
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
    char resp[16384] = {0};
    int rc = https_exchange(state_dir, origin_url ? origin_url : DEFAULT_ORIGIN,
                            "GET", subpath, report_id, expected_sha, receipt_key,
                            NULL, 0, 0, &http_status, resp, sizeof(resp));
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
    fprintf(stderr, "XZ_SUPPORT_ERROR UNKNOWN_CMD\n");
    return 1;
}
