/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L

#include "xz_sdk_host_internal.h"
#include "../kart/xz_kart_runtime.h"

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <time.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

#define XZ_COMMANDS 32u
#define XZ_CHILDREN 3u
#define XZ_EXTENSIONS 2u
#define XZ_AUTOLOAD_KEYS 64u
#define XZ_ENVIRONMENT 256u
#define XZ_CLOSE_DESCRIPTORS 8192u
#define XZ_WORKER_WAIT_MS 50u
#define XZ_PROBE_INTERVAL_MS 500u
#define XZ_MEDIA_CHECK_MS 200u
#define XZ_TERMINATE_GRACE_MS 1500u

extern char **environ;

enum xz_stop_reason {
    XZ_STOP_NONE = 0,
    XZ_STOP_MANUAL,
    XZ_STOP_MEDIA,
    XZ_STOP_SHUTDOWN
};

struct xz_child {
    int active;
    pid_t process;
    uint32_t kind;
    char id[XZ_SDK_ID];
    char name[XZ_SDK_NAME];
    char version[32];
    uint8_t content_digest[XZ_SHA256_BYTES];
    struct xz_sdk_source source;
    struct xz_sdk_stage stage;
    enum xz_stop_reason stop_reason;
    uint64_t terminate_deadline;
    int kill_sent;
};

struct xz_autoload_key {
    int used;
    int present;
    int next_present;
    int attempted;
    uint8_t content_digest[XZ_SHA256_BYTES];
};

struct xz_manager {
    int started;
    int worker_joined;
    atomic_int stopping;
    pthread_t worker;

    char roots[XZ_SDK_ROOTS][XZ_SDK_PATH];
    int root_present[XZ_SDK_ROOTS];
    char applications_folder[XZ_SDK_PATH];
    char autoload_folder[XZ_SDK_PATH];
    char stage_root[XZ_SDK_PATH];
    struct xz_sdk_scan_spec scan_spec;

    int autoload_enabled;
    int (*apply_profile)(void *context, const char *record, int persist);
    int (*display_available)(void *context);
    void *context;

    struct xz_sdk_package_record catalog[XZ_SDK_PACKAGES];
    uint32_t catalog_count;
    struct xz_sdk_scan_result scan_scratch;
    struct xz_child children[XZ_CHILDREN];
    struct xz_autoload_key autoload[XZ_AUTOLOAD_KEYS];

    struct xz_sdk_command commands[XZ_COMMANDS];
    uint32_t command_head;
    uint32_t command_count;
    struct xz_sdk_snapshot snapshot;
    uint8_t published_catalog_digest[XZ_SHA256_BYTES];

    int scanning;
    int discovery_limited;
    char message[XZ_SDK_SCAN_MESSAGE];
    uint64_t stage_nonce;
    uint64_t probe_signature;
    int probe_valid;
    uint64_t next_probe;
    uint64_t next_media_check;
};

static pthread_mutex_t manager_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct xz_manager manager;
static atomic_int foreground_reserved = ATOMIC_VAR_INIT(0);

static int worker_should_stop(void);

static uint64_t milliseconds_now(void)
{
    struct timespec value;
    if (syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &value) != 0) {
        return 0;
    }
    return (uint64_t)value.tv_sec * 1000u + (uint64_t)value.tv_nsec / 1000000u;
}

static void wait_milliseconds(unsigned milliseconds)
{
    struct timeval timeout;
    timeout.tv_sec = (time_t)(milliseconds / 1000u);
    timeout.tv_usec = (suseconds_t)((milliseconds % 1000u) * 1000u);
    while (select(0, NULL, NULL, NULL, &timeout) < 0 && errno == EINTR) {
        timeout.tv_sec = 0;
        timeout.tv_usec = 1000;
    }
}

static int copy_text(char *output, size_t capacity, const char *input)
{
    size_t length;
    if (!output || !capacity || !input) {
        return -1;
    }
    length = strlen(input);
    if (length >= capacity) {
        return -1;
    }
    memcpy(output, input, length + 1u);
    return 0;
}

static int relative_config_path_valid(const char *path)
{
    const char *component;
    const char *cursor;
    if (!path || !path[0] || path[0] == '/' || strlen(path) >= XZ_SDK_PATH) {
        return 0;
    }
    component = path;
    for (cursor = path;; ++cursor) {
        unsigned char value = (unsigned char)*cursor;
        if (value == '/' || !value) {
            size_t length = (size_t)(cursor - component);
            if (!length || length >= 256u ||
                (length == 1u && component[0] == '.') ||
                (length == 2u && component[0] == '.' && component[1] == '.')) {
                return 0;
            }
            if (!value) {
                return 1;
            }
            component = cursor + 1;
        } else if (value < 0x20u || value > 0x7eu || value == '\\') {
            return 0;
        }
    }
}

static int digest_equal(const uint8_t first[XZ_SHA256_BYTES],
                        const uint8_t second[XZ_SHA256_BYTES])
{
    return memcmp(first, second, XZ_SHA256_BYTES) == 0;
}

static void set_message(const char *message)
{
    if (!message) {
        manager.message[0] = '\0';
        return;
    }
    (void)copy_text(manager.message, sizeof(manager.message), message);
}

static void build_status(char output[96])
{
    uint32_t ready = 0;
    uint32_t running = 0;
    uint32_t failed = 0;
    uint32_t blocked = 0;
    uint32_t index;
    if (manager.message[0]) {
        copy_text(output, 96u, manager.message);
        return;
    }
    if (manager.discovery_limited) {
        copy_text(output, 96u, "Discovery capped: 32 packages, 512 folder entries, 8 roots");
        return;
    }
    for (index = 0; index < manager.catalog_count; ++index) {
        switch (manager.catalog[index].view.state) {
        case XZ_SDK_READY:
            ++ready;
            break;
        case XZ_SDK_RUNNING:
            ++running;
            break;
        case XZ_SDK_FAILED:
            ++failed;
            break;
        case XZ_SDK_BLOCKED:
            ++blocked;
            break;
        default:
            break;
        }
    }
    (void)snprintf(output, 96u, "%u packages: %u ready, %u running, %u failed, %u blocked",
                   manager.catalog_count, ready, running, failed, blocked);
}

static void publish_snapshot(void)
{
    struct xz_sdk_snapshot next;
    struct xz_sha256_context hash;
    uint8_t catalog_digest[XZ_SHA256_BYTES];
    uint32_t index;
    xz_sha256_init(&hash);
    xz_sha256_update(&hash, &manager.catalog_count, sizeof(manager.catalog_count));
    memset(&next, 0, sizeof(next));
    next.count = manager.catalog_count;
    next.scanning = (uint32_t)manager.scanning;
    next.autoload_enabled = (uint32_t)manager.autoload_enabled;
    for (index = 0; index < XZ_CHILDREN; ++index) {
        if (manager.children[index].active && manager.children[index].kind == XZ_SDK_APPLICATION) next.foreground = 1;
    }
    for (index = 0; index < manager.catalog_count; ++index) {
        const struct xz_sdk_package_record *package = &manager.catalog[index];
        next.items[index] = package->view;
        xz_sha256_update(&hash, package->view.id, sizeof(package->view.id));
        xz_sha256_update(&hash, package->content_digest, XZ_SHA256_BYTES);
        xz_sha256_update(&hash, &package->source, sizeof(package->source));
    }
    xz_sha256_final(&hash, catalog_digest);
    build_status(next.status);

    pthread_mutex_lock(&manager_mutex);
    next.generation = manager.snapshot.generation;
    if (!digest_equal(catalog_digest, manager.published_catalog_digest)) {
        ++next.generation;
        if (!next.generation) {
            next.generation = 1u;
        }
        memcpy(manager.published_catalog_digest, catalog_digest, XZ_SHA256_BYTES);
    }
    manager.snapshot = next;
    pthread_mutex_unlock(&manager_mutex);
}

static struct xz_autoload_key *autoload_key_for(
    const uint8_t content_digest[XZ_SHA256_BYTES], int create)
{
    uint32_t index;
    struct xz_autoload_key *unused = NULL;
    struct xz_autoload_key *absent = NULL;
    for (index = 0; index < XZ_AUTOLOAD_KEYS; ++index) {
        if (manager.autoload[index].used &&
            digest_equal(manager.autoload[index].content_digest,
                         content_digest)) {
            return &manager.autoload[index];
        }
        if (!manager.autoload[index].used && !unused) {
            unused = &manager.autoload[index];
        } else if (manager.autoload[index].used &&
                   !manager.autoload[index].present && !absent) {
            absent = &manager.autoload[index];
        }
    }
    if (!unused) {
        unused = absent;
    }
    if (create && unused) {
        memset(unused, 0, sizeof(*unused));
        unused->used = 1;
        memcpy(unused->content_digest, content_digest, XZ_SHA256_BYTES);
        return unused;
    }
    return NULL;
}

static int content_was_present(const uint8_t digest[XZ_SHA256_BYTES])
{
    struct xz_autoload_key *key = autoload_key_for(digest, 0);
    return key && key->present;
}

static void update_presence_epochs(void)
{
    uint32_t index;
    for (index = 0; index < XZ_AUTOLOAD_KEYS; ++index) {
        manager.autoload[index].next_present = 0;
    }
    for (index = 0; index < manager.catalog_count; ++index) {
        struct xz_sdk_package_record *package = &manager.catalog[index];
        struct xz_autoload_key *key;
        if (!package->valid) {
            continue;
        }
        key = autoload_key_for(package->content_digest, 1);
        if (key) {
            if (!key->present) {
                key->attempted = 0;
            }
            key->next_present = 1;
        }
    }
    for (index = 0; index < XZ_AUTOLOAD_KEYS; ++index) {
        if (!manager.autoload[index].used) {
            continue;
        }
        if (!manager.autoload[index].next_present) {
            manager.autoload[index].attempted = 0;
        }
        manager.autoload[index].present = manager.autoload[index].next_present;
    }
}

static int find_child(const char *id,
                      const uint8_t content_digest[XZ_SHA256_BYTES])
{
    uint32_t index;
    for (index = 0; index < XZ_CHILDREN; ++index) {
        if (manager.children[index].active &&
            strcmp(manager.children[index].id, id) == 0 &&
            digest_equal(manager.children[index].content_digest,
                         content_digest)) {
            return (int)index;
        }
    }
    return -1;
}

static int find_package(const char *id,
                        const uint8_t content_digest[XZ_SHA256_BYTES])
{
    uint32_t index;
    for (index = 0; index < manager.catalog_count; ++index) {
        if (manager.catalog[index].valid &&
            strcmp(manager.catalog[index].manifest.id, id) == 0 &&
            digest_equal(manager.catalog[index].content_digest,
                         content_digest)) {
            return (int)index;
        }
    }
    return -1;
}

static int find_child_record(const struct xz_child *child)
{
    uint32_t index;
    for (index = 0; index < manager.catalog_count; ++index) {
        if (strcmp(manager.catalog[index].view.id, child->id) == 0 &&
            digest_equal(manager.catalog[index].content_digest, child->content_digest) &&
            (manager.catalog[index].valid ||
             memcmp(&manager.catalog[index].source, &child->source,
                    sizeof(child->source)) == 0)) {
            return (int)index;
        }
    }
    return -1;
}

static void set_package_state(uint32_t index, uint32_t state,
                              const char *detail)
{
    if (index >= manager.catalog_count) {
        return;
    }
    manager.catalog[index].view.state = state;
    if (detail) {
        (void)copy_text(manager.catalog[index].view.detail,
                        sizeof(manager.catalog[index].view.detail), detail);
    }
}

static void request_child_stop(struct xz_child *child,
                               enum xz_stop_reason reason,
                               const char *detail)
{
    int package_index;
    if (!child || !child->active || child->stop_reason != XZ_STOP_NONE) {
        return;
    }
    child->stop_reason = reason;
    child->terminate_deadline = milliseconds_now() + XZ_TERMINATE_GRACE_MS;
    xz_kart_runtime_release((uint32_t)child->process);
    (void)kill(xz_kart_runtime_signal_target(child->process), SIGTERM);
    package_index = find_child_record(child);
    if (package_index >= 0) {
        set_package_state((uint32_t)package_index,
                          XZ_SDK_RUNNING,
                          manager.catalog[package_index].conflicted ?
                          "stopping; conflicting ID has different content" : detail);
    }
}

static void append_child_tombstone(const struct xz_child *child,
                                   const char *detail)
{
    struct xz_sdk_package_record *package;
    uint32_t index;
    for (index = 0; index < manager.catalog_count; ++index) {
        if (strcmp(manager.catalog[index].view.id, child->id) == 0 &&
            digest_equal(manager.catalog[index].content_digest, child->content_digest)) {
            return;
        }
    }
    if (manager.catalog_count >= XZ_SDK_PACKAGES) {
        return;
    }
    package = &manager.catalog[manager.catalog_count++];
    memset(package, 0, sizeof(*package));
    copy_text(package->view.id, sizeof(package->view.id), child->id);
    copy_text(package->view.name, sizeof(package->view.name), child->name);
    copy_text(package->view.version, sizeof(package->view.version),
              child->version);
    copy_text(package->view.detail, sizeof(package->view.detail), detail);
    package->view.kind = child->kind;
    package->view.state = XZ_SDK_RUNNING;
    memcpy(package->content_digest, child->content_digest, XZ_SHA256_BYTES);
    package->source = child->source;
}

static void reconcile_catalog(struct xz_sdk_scan_result *scan)
{
    uint32_t old_count = manager.catalog_count;
    uint32_t next_count = 0;
    uint32_t index;

    for (index = 0; index < scan->count && next_count < XZ_SDK_PACKAGES;
         ++index) {
        const struct xz_sdk_package_record *candidate = &scan->packages[index];
        int identical = -1;
        uint32_t existing;
        if (candidate->valid) {
            for (existing = 0; existing < next_count; ++existing) {
                if (scan->packages[existing].valid &&
                    strcmp(scan->packages[existing].manifest.id,
                           candidate->manifest.id) == 0 &&
                    digest_equal(scan->packages[existing].content_digest,
                                 candidate->content_digest)) {
                    identical = (int)existing;
                    break;
                }
            }
        }
        if (identical >= 0) {
            struct xz_sdk_package_record *winner = &scan->packages[identical];
            int winner_eligible = winner->automatic &&
                                  winner->root_autoload_enabled;
            int candidate_eligible = candidate->automatic &&
                                     candidate->root_autoload_enabled;
            winner->automatic = winner->automatic || candidate->automatic;
            winner->view.autoload = (uint32_t)winner->automatic;
            winner->root_autoload_enabled = winner->root_autoload_enabled ||
                                            candidate->root_autoload_enabled;
            if (!winner_eligible && candidate_eligible) {
                winner->source = candidate->source;
            }
            continue;
        }
        if (next_count != index) {
            scan->packages[next_count] = *candidate;
        }
        ++next_count;
    }

    for (index = 0; index < next_count; ++index) {
        uint32_t other;
        if (!scan->packages[index].valid) {
            continue;
        }
        for (other = index + 1u; other < next_count; ++other) {
            if (scan->packages[other].valid &&
                strcmp(scan->packages[index].manifest.id,
                       scan->packages[other].manifest.id) == 0 &&
                !digest_equal(scan->packages[index].content_digest,
                              scan->packages[other].content_digest)) {
                scan->packages[index].view.state = XZ_SDK_BLOCKED;
                scan->packages[other].view.state = XZ_SDK_BLOCKED;
                scan->packages[index].conflicted = 1;
                scan->packages[other].conflicted = 1;
                copy_text(scan->packages[index].view.detail,
                          sizeof(scan->packages[index].view.detail),
                          "conflicting ID has different content");
                copy_text(scan->packages[other].view.detail,
                          sizeof(scan->packages[other].view.detail),
                          "conflicting ID has different content");
            }
        }
    }

    for (index = 0; index < next_count; ++index) {
        uint32_t previous;
        int child;
        if (!scan->packages[index].valid) {
            continue;
        }
        child = find_child(scan->packages[index].manifest.id,
                           scan->packages[index].content_digest);
        if (child >= 0) {
            scan->packages[index].view.state = XZ_SDK_RUNNING;
            copy_text(scan->packages[index].view.detail,
                      sizeof(scan->packages[index].view.detail),
                      scan->packages[index].conflicted ?
                      "running; conflicting ID has different content" : "running");
            continue;
        }
        if (scan->packages[index].conflicted) {
            continue;
        }
        if (!content_was_present(scan->packages[index].content_digest)) {
            continue;
        }
        for (previous = 0; previous < old_count; ++previous) {
            if (manager.catalog[previous].valid &&
                strcmp(manager.catalog[previous].manifest.id,
                       scan->packages[index].manifest.id) == 0 &&
                digest_equal(manager.catalog[previous].content_digest,
                             scan->packages[index].content_digest) &&
                manager.catalog[previous].view.state == XZ_SDK_FAILED) {
                scan->packages[index].view.state = XZ_SDK_FAILED;
                copy_text(scan->packages[index].view.detail,
                          sizeof(scan->packages[index].view.detail),
                          manager.catalog[previous].view.detail);
                break;
            }
        }
    }

    memset(manager.catalog, 0, sizeof(manager.catalog));
    memcpy(manager.catalog, scan->packages,
           (size_t)next_count * sizeof(manager.catalog[0]));
    manager.catalog_count = next_count;
    update_presence_epochs();
}

static int child_slot_available(uint32_t kind)
{
    uint32_t index;
    unsigned extensions = 0;
    for (index = 0; index < XZ_CHILDREN; ++index) {
        if (!manager.children[index].active) {
            continue;
        }
        if (manager.children[index].kind == XZ_SDK_EXTENSION) {
            ++extensions;
        }
    }
    if (kind == XZ_SDK_APPLICATION &&
        atomic_load_explicit(&foreground_reserved, memory_order_acquire)) {
        return 0;
    }
    if (kind == XZ_SDK_EXTENSION && extensions >= XZ_EXTENSIONS) {
        return 0;
    }
    for (index = 0; index < XZ_CHILDREN; ++index) {
        if (!manager.children[index].active) {
            return (int)index + 1;
        }
    }
    return 0;
}

static int build_environment(const char *stage_path, const char *kind,
                             char *output[XZ_ENVIRONMENT + 3u],
                             char package_variable[XZ_SDK_PATH + 32u],
                             char kind_variable[64])
{
    size_t count = 0;
    size_t index;
    if (snprintf(package_variable, XZ_SDK_PATH + 32u,
                 "XZ_SDK_PACKAGE_ROOT=%s", stage_path) >=
            (int)(XZ_SDK_PATH + 32u) ||
        snprintf(kind_variable, 64u, "XZ_SDK_KIND=%s", kind) >= 64) {
        return -1;
    }
    for (index = 0; environ && environ[index]; ++index) {
        const char *entry = environ[index];
        if (strncmp(entry, "LD_PRELOAD=", 11u) == 0 ||
            strncmp(entry, "XZ_SDK_PACKAGE_ROOT=", 20u) == 0 ||
            strncmp(entry, "XZ_SDK_KIND=", 12u) == 0 ||
            strncmp(entry, "XZ_KART_INPUT_", 14u) == 0 ||
            strncmp(entry, "XZ_KART_SDK_DISPLAY=", 20u) == 0) {
            continue;
        }
        if (count >= XZ_ENVIRONMENT) {
            return -1;
        }
        output[count++] = environ[index];
    }
    output[count++] = package_variable;
    output[count++] = kind_variable;
    output[count] = NULL;
    return 0;
}

static int spawn_staged(const struct xz_sdk_stage *stage, uint32_t kind, const char *package_id,
                        pid_t *process)
{
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    sigset_t empty;
    sigset_t defaults;
    char *arguments[2];
    char *environment[XZ_ENVIRONMENT + 6u];
    struct xz_kart_launch kart_launch;
    char package_variable[XZ_SDK_PATH + 32u];
    char kind_variable[64];
    DIR *descriptors;
    struct dirent *entry;
    unsigned visited = 0;
    short flags = 0;
    int actions_ready = 0;
    int attributes_ready = 0;
    int result;

    if (xz_kart_runtime_prepare(package_id, kind, &kart_launch) < 0) return -1;
    if (build_environment(stage->path,
                          kind == XZ_SDK_APPLICATION ? "application" :
                                                       "extension",
                          environment, package_variable, kind_variable) != 0) {
        return -1;
    }
    if (kart_launch.display_env[0]) {
        size_t n = 0;
        while (environment[n]) ++n;
        environment[n++] = kart_launch.display_env;
        if (kart_launch.epoch) {
            environment[n++] = kart_launch.epoch_env;
            environment[n++] = kart_launch.mask_env;
        }
        environment[n] = NULL;
    }
    if (posix_spawn_file_actions_init(&actions) != 0) {
        return -1;
    }
    actions_ready = 1;
    descriptors = opendir("/proc/self/fd");
    if (!descriptors) {
        posix_spawn_file_actions_destroy(&actions);
        return -1;
    }
    while ((entry = readdir(descriptors)) != NULL) {
        char *end;
        long descriptor = strtol(entry->d_name, &end, 10);
        if (++visited > XZ_CLOSE_DESCRIPTORS ||
            (!*end && descriptor >= 3 && descriptor <= INT_MAX &&
             descriptor != dirfd(descriptors) &&
             posix_spawn_file_actions_addclose(&actions, (int)descriptor) != 0)) {
            closedir(descriptors);
            posix_spawn_file_actions_destroy(&actions);
            return -1;
        }
    }
    closedir(descriptors);
    if (posix_spawnattr_init(&attributes) != 0) {
        posix_spawn_file_actions_destroy(&actions);
        return -1;
    }
    attributes_ready = 1;
    sigemptyset(&empty);
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGTERM);
    sigaddset(&defaults, SIGINT);
    sigaddset(&defaults, SIGCHLD);
    if (posix_spawnattr_setsigmask(&attributes, &empty) == 0 &&
        posix_spawnattr_setsigdefault(&attributes, &defaults) == 0) {
        flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
        (void)posix_spawnattr_setflags(&attributes, flags);
    }
    if (kart_launch.epoch) {
        short group_flags;
        if (posix_spawnattr_getflags(&attributes, &group_flags) != 0 ||
            posix_spawnattr_setpgroup(&attributes, 0) != 0 ||
            posix_spawnattr_setflags(&attributes, group_flags | POSIX_SPAWN_SETPGROUP) != 0) {
            posix_spawnattr_destroy(&attributes);
            posix_spawn_file_actions_destroy(&actions);
            return -1;
        }
    }
    arguments[0] = (char *)stage->entry_path;
    arguments[1] = NULL;
    result = posix_spawn(process, stage->entry_path, &actions, &attributes,
                         arguments, environment);
    if (result == 0 && xz_kart_runtime_attach((uint32_t)*process, &kart_launch) != 0) {
        int status;
        (void)kill(-*process, SIGKILL);
        while (waitpid(*process, &status, 0) < 0 && errno == EINTR) {}
        xz_kart_runtime_cleanup_profile((uint32_t)*process, kart_launch.epoch);
        result = EBUSY;
    }
    if (attributes_ready) {
        posix_spawnattr_destroy(&attributes);
    }
    if (actions_ready) {
        posix_spawn_file_actions_destroy(&actions);
    }
    return result == 0 ? 0 : -1;
}

static int read_profile_record(const struct xz_sdk_stage *stage,
                               char record[XZ_SDK_PROFILE_BYTES + 1u])
{
    int descriptor;
    size_t used = 0;
    descriptor = open(stage->entry_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) {
        return -1;
    }
    for (;;) {
        ssize_t count;
        if (used == XZ_SDK_PROFILE_BYTES) {
            char extra;
            do {
                count = read(descriptor, &extra, 1u);
            } while (count < 0 && errno == EINTR);
            if (count != 0) {
                close(descriptor);
                return -1;
            }
            break;
        }
        do {
            count = read(descriptor, record + used,
                         XZ_SDK_PROFILE_BYTES - used);
        } while (count < 0 && errno == EINTR);
        if (count < 0) {
            close(descriptor);
            return -1;
        }
        if (!count) {
            break;
        }
        used += (size_t)count;
    }
    close(descriptor);
    if (memchr(record, '\0', used) != NULL) {
        return -1;
    }
    record[used] = '\0';
    return 0;
}

static void fail_launch(uint32_t index, const char *detail)
{
    set_package_state(index, XZ_SDK_FAILED, detail);
    set_message(detail);
    publish_snapshot();
}

static int run_package(uint32_t index, int automatic, int persist)
{
    struct xz_sdk_package_record *package;
    struct xz_sdk_stage stage;
    char error[XZ_SDK_SCAN_MESSAGE] = {0};
    int slot = 0;
    int reserved = 0;
    if (index >= manager.catalog_count) {
        return -1;
    }
    if (worker_should_stop()) return -1;
    package = &manager.catalog[index];
    if (package->view.state == XZ_SDK_BLOCKED) {
        set_message("Conflicting package ID remains blocked");
        publish_snapshot();
        return -1;
    }
    if (!package->valid) {
        fail_launch(index, "package is not launchable");
        return -1;
    }
    if (find_child(package->manifest.id, package->content_digest) >= 0) {
        set_package_state(index, XZ_SDK_RUNNING, package->conflicted ?
                          "running; conflicting ID has different content" : "already running");
        publish_snapshot();
        return 0;
    }
    if (package->manifest.kind != XZ_SDK_PROFILE &&
        !(slot = child_slot_available(package->manifest.kind))) {
        if (package->manifest.kind == XZ_SDK_APPLICATION) {
            fail_launch(index, automatic ?
                        "automatic launch refused: foreground is busy" :
                        "foreground is busy; retry later");
        } else {
            fail_launch(index, automatic ?
                        "automatic launch refused: process limit" :
                        "process limit reached; retry later");
        }
        return -1;
    }
    memset(&stage, 0, sizeof(stage));
    ++manager.stage_nonce;
    if (xz_sdk_internal_stage(package, manager.stage_root,
                              manager.stage_nonce, &manager.scan_spec, &stage, error) != 0) {
        fail_launch(index, error[0] ? error : "package staging failed");
        return -1;
    }
    if (worker_should_stop()) {
        xz_sdk_internal_stage_cleanup(&stage);
        return -1;
    }
    if (package->manifest.kind == XZ_SDK_PROFILE) {
        char record[XZ_SDK_PROFILE_BYTES + 1u];
        int callback_result;
        if (read_profile_record(&stage, record) != 0 ||
            !manager.apply_profile) {
            xz_sdk_internal_stage_cleanup(&stage);
            fail_launch(index, "profile adapter unavailable or record unreadable");
            return -1;
        }
        /* No manager mutex is held across this host-owned callback. */
        callback_result = manager.apply_profile(manager.context, record, automatic ? 0 : persist);
        xz_sdk_internal_stage_cleanup(&stage);
        if (callback_result != 0) {
            fail_launch(index, "profile was rejected by host adapter");
            return -1;
        }
        set_package_state(index, XZ_SDK_READY, persist && !automatic ?
                          "profile applied; save requested" : "profile applied for this session; not persisted");
        set_message(persist && !automatic ? "Profile save requested; check settings status" : "Profile applied for this session only");
        publish_snapshot();
        return 0;
    }

    if (package->manifest.kind == XZ_SDK_APPLICATION) {
        int available = 1;
        int expected = 0;
        /* Reserve before arbitration, so game actions see the reservation while
         * the host checks ownership under its UI mutex. Publish takeover only
         * after a child actually starts. */
        if (!atomic_compare_exchange_strong_explicit(&foreground_reserved, &expected, 1,
                                                     memory_order_acq_rel, memory_order_acquire)) available = 0;
        if (manager.display_available) {
            if (available) available = manager.display_available(manager.context) > 0;
        }
        if (!available) {
            if (!expected) atomic_store_explicit(&foreground_reserved, 0, memory_order_release);
            xz_sdk_internal_stage_cleanup(&stage);
            fail_launch(index, automatic ?
                        "automatic launch refused: display unavailable" :
                        "display unavailable; retry when free");
            return -1;
        }
        reserved = 1;
        publish_snapshot();
    }
    {
        struct xz_child *child = &manager.children[(uint32_t)(slot - 1)];
        pid_t process;
        if (worker_should_stop()) {
            if (reserved) atomic_store_explicit(&foreground_reserved, 0, memory_order_release);
            xz_sdk_internal_stage_cleanup(&stage);
            return -1;
        }
        if (spawn_staged(&stage, package->manifest.kind, package->manifest.id, &process) != 0) {
            if (reserved) {
                atomic_store_explicit(&foreground_reserved, 0,
                                      memory_order_release);
            }
            xz_sdk_internal_stage_cleanup(&stage);
            fail_launch(index, "posix_spawn failed; package remains retryable");
            return -1;
        }
        if (xz_sdk_internal_stage_child(&stage, process) != 0) {
            int status;
            xz_kart_runtime_release((uint32_t)process);
            (void)kill(xz_kart_runtime_signal_target(process), SIGKILL);
            xz_kart_runtime_forget((uint32_t)process);
            while (waitpid(process, &status, 0) < 0 && errno == EINTR) {}
            if (reserved) atomic_store_explicit(&foreground_reserved, 0, memory_order_release);
            xz_sdk_internal_stage_cleanup(&stage);
            fail_launch(index, "could not journal child ownership");
            return -1;
        }
        memset(child, 0, sizeof(*child));
        child->active = 1;
        child->process = process;
        child->kind = package->manifest.kind;
        copy_text(child->id, sizeof(child->id), package->manifest.id);
        copy_text(child->name, sizeof(child->name), package->manifest.name);
        copy_text(child->version, sizeof(child->version),
                  package->manifest.version);
        memcpy(child->content_digest, package->content_digest,
               XZ_SHA256_BYTES);
        child->source = package->source;
        child->stage = stage;
    }
    set_package_state(index, XZ_SDK_RUNNING, "running");
    set_message(NULL);
    publish_snapshot();
    return 0;
}

static void run_autoload(void)
{
    uint32_t index;
    if (!manager.autoload_enabled || worker_should_stop()) {
        return;
    }
    for (index = 0; index < manager.catalog_count && !worker_should_stop(); ++index) {
        struct xz_sdk_package_record *package = &manager.catalog[index];
        struct xz_autoload_key *key;
        if (!package->valid || !package->automatic ||
            !package->root_autoload_enabled ||
            package->view.state == XZ_SDK_BLOCKED) {
            continue;
        }
        key = autoload_key_for(package->content_digest, 0);
        if (!key || key->attempted) {
            continue;
        }
        key->attempted = 1;
        (void)run_package(index, 1, 0);
    }
}

static void perform_scan(void)
{
    struct xz_sdk_scan_result *scan = &manager.scan_scratch;
    uint32_t child_index;
    manager.scanning = 1;
    set_message("Scanning configured application folders");
    publish_snapshot();
    memset(scan, 0, sizeof(*scan));
    if (xz_sdk_internal_scan(&manager.scan_spec, scan) != 0) {
        manager.scanning = 0;
        set_message("Scan configuration is invalid");
        publish_snapshot();
        return;
    }
    if (worker_should_stop()) {
        return;
    }
    reconcile_catalog(scan);
    manager.discovery_limited = (int)scan->limit_reached;
    manager.scanning = 0;
    set_message(scan->message[0] ? scan->message : NULL);

    for (child_index = 0; child_index < XZ_CHILDREN; ++child_index) {
        struct xz_child *child = &manager.children[child_index];
        if (child->active &&
            find_package(child->id, child->content_digest) < 0) {
            append_child_tombstone(child,
                                   "package unavailable; stopping child");
            request_child_stop(child, XZ_STOP_MEDIA,
                               "package unavailable; stopping");
        }
    }
    if (xz_sdk_internal_probe(&manager.scan_spec,
                              &manager.probe_signature) == 0) {
        manager.probe_valid = 1;
    }
    manager.next_probe = milliseconds_now() + XZ_PROBE_INTERVAL_MS;
    publish_snapshot();
    run_autoload();
}

static void finish_child(struct xz_child *child, int wait_status,
                         int wait_failed)
{
    if (xz_kart_runtime_drain((uint32_t)child->process) != 0) {
        set_message("Kart process group still stopping; stage retained");
        return;
    }
    xz_kart_runtime_forget((uint32_t)child->process);
    int visible_index = find_child_record(child);
    char detail[96];
    uint32_t state = XZ_SDK_READY;
    if (child->kind == XZ_SDK_APPLICATION) {
        atomic_store_explicit(&foreground_reserved, 0, memory_order_release);
    }
    if (child->stop_reason == XZ_STOP_MANUAL) {
        copy_text(detail, sizeof(detail), "stopped by user");
    } else if (child->stop_reason == XZ_STOP_MEDIA) {
        state = XZ_SDK_FAILED;
        copy_text(detail, sizeof(detail), "stopped after media or folder removal");
    } else if (child->stop_reason == XZ_STOP_SHUTDOWN) {
        copy_text(detail, sizeof(detail), "manager stopped");
    } else if (wait_failed) {
        state = XZ_SDK_FAILED;
        copy_text(detail, sizeof(detail), "child could not be reaped normally");
    } else if (WIFEXITED(wait_status) && WEXITSTATUS(wait_status) == 0) {
        copy_text(detail, sizeof(detail), "completed");
    } else if (WIFEXITED(wait_status)) {
        state = XZ_SDK_FAILED;
        (void)snprintf(detail, sizeof(detail), "child exited with status %d",
                       WEXITSTATUS(wait_status));
    } else if (WIFSIGNALED(wait_status)) {
        state = XZ_SDK_FAILED;
        (void)snprintf(detail, sizeof(detail), "child terminated by signal %d",
                       WTERMSIG(wait_status));
    } else {
        state = XZ_SDK_FAILED;
        copy_text(detail, sizeof(detail), "child ended unexpectedly");
    }
    xz_sdk_internal_stage_cleanup(&child->stage);
    memset(child, 0, sizeof(*child));
    if (visible_index >= 0 && !worker_should_stop()) {
        if (manager.catalog[visible_index].conflicted) {
            state = XZ_SDK_BLOCKED;
            copy_text(detail, sizeof(detail), "conflicting ID has different content");
        }
        set_package_state((uint32_t)visible_index, state, detail);
        set_message(state == XZ_SDK_FAILED ? detail : NULL);
        publish_snapshot();
    }
}

static void reap_children(void)
{
    uint32_t index;
    uint64_t now = milliseconds_now();
    for (index = 0; index < XZ_CHILDREN; ++index) {
        struct xz_child *child = &manager.children[index];
        int status = 0;
        pid_t result;
        if (!child->active) {
            continue;
        }
        do {
            result = waitpid(child->process, &status, WNOHANG);
        } while (result < 0 && errno == EINTR);
        if (result == child->process) {
            finish_child(child, status, 0);
            continue;
        }
        if (result < 0 && errno == ECHILD) {
            finish_child(child, status, 1);
            continue;
        }
        if (child->stop_reason != XZ_STOP_NONE && !child->kill_sent &&
            now >= child->terminate_deadline) {
            (void)kill(xz_kart_runtime_signal_target(child->process), SIGKILL);
            child->kill_sent = 1;
        }
    }
}

static void check_child_media(void)
{
    uint32_t index;
    for (index = 0; index < XZ_CHILDREN; ++index) {
        struct xz_child *child = &manager.children[index];
        if (child->active && child->stop_reason == XZ_STOP_NONE &&
            !xz_sdk_internal_source_present(&child->source)) {
            request_child_stop(child, XZ_STOP_MEDIA,
                               "media or configured folder removed; stopping");
        }
    }
    publish_snapshot();
}

static int take_command(struct xz_sdk_command *command,
                        uint32_t *current_generation)
{
    int available = 0;
    pthread_mutex_lock(&manager_mutex);
    *current_generation = manager.snapshot.generation;
    if (manager.command_count) {
        *command = manager.commands[manager.command_head];
        manager.command_head = (manager.command_head + 1u) % XZ_COMMANDS;
        --manager.command_count;
        available = 1;
    }
    pthread_mutex_unlock(&manager_mutex);
    return available;
}

static void process_command(const struct xz_sdk_command *command,
                            uint32_t current_generation)
{
    if (command->generation != current_generation) {
        set_message("Stale command ignored; refresh the application list");
        publish_snapshot();
        return;
    }
    switch (command->kind) {
    case XZ_SDK_RUN:
        if (command->index < manager.catalog_count) {
            (void)run_package(command->index, 0, (int)command->value);
        }
        break;
    case XZ_SDK_STOP:
        if (command->index < manager.catalog_count) {
            struct xz_sdk_package_record *package =
                &manager.catalog[command->index];
            int child = find_child(package->view.id, package->content_digest);
            if (child >= 0) {
                request_child_stop(&manager.children[child], XZ_STOP_MANUAL,
                                   "stopping by user request");
                set_message("Stopping package");
            } else {
                set_message("Package is not running");
            }
            publish_snapshot();
        }
        break;
    case XZ_SDK_SCAN:
        perform_scan();
        break;
    case XZ_SDK_AUTOLOAD:
        manager.autoload_enabled = command->value ? 1 : 0;
        set_message(manager.autoload_enabled ? "Autoload enabled" :
                                               "Autoload disabled");
        publish_snapshot();
        if (manager.autoload_enabled) {
            run_autoload();
        }
        break;
    default:
        break;
    }
}

static int children_active(void)
{
    uint32_t index;
    for (index = 0; index < XZ_CHILDREN; ++index) {
        if (manager.children[index].active) {
            return 1;
        }
    }
    return 0;
}

static void terminate_all_children(void)
{
    uint32_t index;
    uint64_t hard_deadline;
    for (index = 0; index < XZ_CHILDREN; ++index) {
        if (manager.children[index].active) {
            request_child_stop(&manager.children[index], XZ_STOP_SHUTDOWN,
                               "manager stopping");
        }
    }
    hard_deadline = milliseconds_now() + XZ_TERMINATE_GRACE_MS + 500u;
    while (children_active() && milliseconds_now() < hard_deadline) {
        reap_children();
        wait_milliseconds(XZ_WORKER_WAIT_MS);
    }
    for (index = 0; index < XZ_CHILDREN; ++index) {
        struct xz_child *child = &manager.children[index];
        int status = 0;
        pid_t result;
        if (!child->active) {
            continue;
        }
        (void)kill(xz_kart_runtime_signal_target(child->process), SIGKILL);
        do {
            result = waitpid(child->process, &status, 0);
        } while (result < 0 && errno == EINTR);
        finish_child(child, status, result != child->process);
    }
}

static int worker_should_stop(void)
{
    return atomic_load_explicit(&manager.stopping, memory_order_acquire);
}

static int scan_cancelled(void *context)
{
    (void)context;
    return worker_should_stop();
}

static void *manager_worker(void *unused)
{
    (void)unused;
    (void)xz_sdk_internal_recover(manager.stage_root, &manager.scan_spec);
    perform_scan();
    manager.next_media_check = milliseconds_now() + XZ_MEDIA_CHECK_MS;
    while (!worker_should_stop()) {
        struct xz_sdk_command command;
        uint32_t generation;
        uint64_t now;
        while (take_command(&command, &generation)) {
            process_command(&command, generation);
            if (worker_should_stop()) {
                break;
            }
        }
        reap_children();
        now = milliseconds_now();
        if (now >= manager.next_media_check) {
            check_child_media();
            manager.next_media_check = now + XZ_MEDIA_CHECK_MS;
        }
        if (now >= manager.next_probe) {
            uint64_t signature;
            if (xz_sdk_internal_probe(&manager.scan_spec, &signature) == 0 &&
                (!manager.probe_valid || signature != manager.probe_signature)) {
                manager.probe_signature = signature;
                manager.probe_valid = 1;
                perform_scan();
            } else {
                manager.next_probe = now + XZ_PROBE_INTERVAL_MS;
            }
        }
        wait_milliseconds(XZ_WORKER_WAIT_MS);
    }
    terminate_all_children();
    if (!xz_kart_runtime_group_pending())
        atomic_store_explicit(&foreground_reserved, 0, memory_order_release);
    else publish_snapshot();
    return NULL;
}

int xz_sdk_host_start(const struct xz_sdk_host_config *config)
{
    const char *applications = "VJ.Tools/Applications";
    const char *autoload = "VJ.Tools/Autoload";
    const char *stage_root = "/dev/shm/xz-sdk";
    unsigned root_count = 0;
    unsigned index;
    int create_result;

    pthread_mutex_lock(&manager_mutex);
    if (manager.started) {
        pthread_mutex_unlock(&manager_mutex);
        return -1;
    }
    if (config) {
        if (config->root_count > XZ_SDK_ROOTS) {
            pthread_mutex_unlock(&manager_mutex);
            return -1;
        }
        root_count = config->root_count;
        if (config->applications_folder) {
            applications = config->applications_folder;
        }
        if (config->autoload_folder) {
            autoload = config->autoload_folder;
        }
        if (config->stage_root) {
            stage_root = config->stage_root;
        }
    }
    if (!relative_config_path_valid(applications) ||
        !relative_config_path_valid(autoload) || !stage_root ||
        stage_root[0] != '/' || strlen(stage_root) >= XZ_SDK_PATH) {
        pthread_mutex_unlock(&manager_mutex);
        return -1;
    }
    memset(&manager, 0, sizeof(manager));
    atomic_init(&manager.stopping, 0);
    atomic_store_explicit(&foreground_reserved, 0, memory_order_release);
    if (copy_text(manager.applications_folder,
                  sizeof(manager.applications_folder), applications) != 0 ||
        copy_text(manager.autoload_folder, sizeof(manager.autoload_folder),
                  autoload) != 0 ||
        copy_text(manager.stage_root, sizeof(manager.stage_root), stage_root) != 0) {
        pthread_mutex_unlock(&manager_mutex);
        return -1;
    }
    manager.scan_spec.root_count = root_count;
    manager.scan_spec.applications_folder = manager.applications_folder;
    manager.scan_spec.autoload_folder = manager.autoload_folder;
    manager.scan_spec.cancelled = scan_cancelled;
    for (index = 0; index < root_count; ++index) {
        if (!config->roots[index]) {
            manager.scan_spec.roots[index] = NULL;
            continue;
        }
        if (config->roots[index][0] != '/' ||
            copy_text(manager.roots[index], sizeof(manager.roots[index]),
                      config->roots[index]) != 0) {
            memset(&manager, 0, sizeof(manager));
            pthread_mutex_unlock(&manager_mutex);
            return -1;
        }
        manager.root_present[index] = 1;
        manager.scan_spec.roots[index] = manager.roots[index];
    }
    manager.autoload_enabled = config ? !!config->autoload_enabled : 1;
    manager.apply_profile = config ? config->apply_profile : NULL;
    manager.display_available = config ? config->display_available : NULL;
    manager.context = config ? config->context : NULL;
    manager.started = 1;
    manager.scanning = 1;
    manager.snapshot.generation = 1u;
    manager.snapshot.scanning = 1u;
    manager.snapshot.autoload_enabled = (uint32_t)manager.autoload_enabled;
    copy_text(manager.snapshot.status, sizeof(manager.snapshot.status),
              "Starting SDK host manager");
    manager.stage_nonce = milliseconds_now() << 12;
    create_result = pthread_create(&manager.worker, NULL, manager_worker, NULL);
    if (create_result != 0) {
        memset(&manager, 0, sizeof(manager));
        pthread_mutex_unlock(&manager_mutex);
        return -1;
    }
    pthread_mutex_unlock(&manager_mutex);
    return 0;
}

static void *retry_stop_worker(void *unused)
{
    (void)unused;
    terminate_all_children();
    return NULL;
}

void xz_sdk_host_stop(void)
{
    pthread_t worker;
    int joined;
    pthread_mutex_lock(&manager_mutex);
    if (!manager.started) {
        pthread_mutex_unlock(&manager_mutex);
        return;
    }
    atomic_store_explicit(&manager.stopping, 1, memory_order_release);
    worker = manager.worker;
    joined = manager.worker_joined;
    pthread_mutex_unlock(&manager_mutex);

    if (!pthread_equal(pthread_self(), worker)) {
        if (!joined) (void)pthread_join(worker, NULL);
        else {
            pthread_t retry;
            if (pthread_create(&retry, NULL, retry_stop_worker, NULL) != 0) return;
            (void)pthread_join(retry, NULL);
        }
        pthread_mutex_lock(&manager_mutex);
        manager.worker_joined = 1;
        if (xz_kart_runtime_group_pending()) {
            pthread_mutex_unlock(&manager_mutex);
            return;
        }
        memset(&manager, 0, sizeof(manager));
        atomic_store_explicit(&foreground_reserved, 0, memory_order_release);
        pthread_mutex_unlock(&manager_mutex);
    }
}

int xz_sdk_host_read(struct xz_sdk_snapshot *output)
{
    if (!output || pthread_mutex_trylock(&manager_mutex) != 0) {
        return -1;
    }
    if (!manager.started) {
        pthread_mutex_unlock(&manager_mutex);
        return -1;
    }
    *output = manager.snapshot;
    pthread_mutex_unlock(&manager_mutex);
    return 0;
}

int xz_sdk_host_submit(const struct xz_sdk_command *command)
{
    uint32_t tail;
    if (!command || pthread_mutex_trylock(&manager_mutex) != 0) {
        return -1;
    }
    if (!manager.started || manager.stopping ||
        command->kind < XZ_SDK_RUN || command->kind > XZ_SDK_AUTOLOAD ||
        command->generation != manager.snapshot.generation ||
        ((command->kind == XZ_SDK_RUN || command->kind == XZ_SDK_STOP) &&
         command->index >= manager.snapshot.count) ||
        ((command->kind == XZ_SDK_AUTOLOAD || command->kind == XZ_SDK_RUN) && command->value > 1u) ||
        manager.command_count >= XZ_COMMANDS) {
        pthread_mutex_unlock(&manager_mutex);
        return -1;
    }
    tail = (manager.command_head + manager.command_count) % XZ_COMMANDS;
    manager.commands[tail] = *command;
    ++manager.command_count;
    pthread_mutex_unlock(&manager_mutex);
    return 0;
}

int xz_sdk_host_foreground(void)
{
    return atomic_load_explicit(&foreground_reserved, memory_order_acquire);
}
