// SPDX-License-Identifier: MIT (this file); the built binary is GPL-2.0-or-later because it contains Ableton Link.
// xz-link-peer: joins the Ableton Link session on the unit's network and mirrors it into the player's
// shared-memory record (xz_link_shm.h). It applies the tempo and beat commands the player writes there.
//
// Usage: xz-link-peer [--shm PATH] [--cpus LIST] [--log PATH] [--period-ms N] [--seconds N]
//   --shm        the record the player created (default /dev/shm/xz-link/state); never created here
//   --cpus       affinity for the helper and Link's two threads, e.g. 0,2
//   --log        bounded JSON-lines log; restarted from empty past 64 KiB
//   --period-ms  publish and command-poll period, 5..100 (default 20)
//   --seconds    exit after N seconds (tests only)
// Exit codes: 0 stopped (SIGTERM/SIGINT, --seconds, or the record was removed), 2 usage, 3 record refused,
// 4 Link could not start.

#include "xz_link_shm.h"

#include <ableton/Link.hpp>

#include <cerrno>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <fcntl.h>
#include <sched.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

using XzLink = ableton::BasicLink<ableton::platforms::linux_::ClockMonotonic>;

static volatile std::sig_atomic_t g_stop = 0;
static int g_log = -1;
static int64_t g_start_us = 0;

static void on_signal(int) { g_stop = 1; }

static int64_t monotonic_us()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return int64_t(ts.tv_sec) * 1000000 + ts.tv_nsec / 1000;
}

enum { LOG_LIMIT = 64 * 1024, LOG_LINE = 240 };

// One JSON object per line, at most LOG_LINE bytes; `fmt` supplies the fields after "t" and "ev".
static void log_line(const char *ev, const char *fmt = nullptr, ...)
{
    char line[LOG_LINE];
    int n = snprintf(line, sizeof line, "{\"t\":%.3f,\"ev\":\"%s\"", (monotonic_us() - g_start_us) / 1e6, ev);
    if (n < 0) return;
    if (fmt && size_t(n) < sizeof line) {
        line[n++] = ',';
        va_list ap;
        va_start(ap, fmt);
        int m = vsnprintf(line + n, sizeof line - size_t(n), fmt, ap);
        va_end(ap);
        if (m > 0) n += m;
    }
    if (size_t(n) > sizeof line - 3) n = int(sizeof line - 3);
    line[n++] = '}';
    line[n++] = '\n';
    int fd = g_log >= 0 ? g_log : 2;
    if (g_log >= 0) {
        off_t end = lseek(g_log, 0, SEEK_END);
        if (end > LOG_LIMIT && ftruncate(g_log, 0) == 0) {
            static const char again[] = "{\"ev\":\"log_restarted\"}\n";
            (void)!write(g_log, again, sizeof again - 1);
        }
    }
    (void)!write(fd, line, size_t(n));
}

static bool parse_cpus(const char *list, cpu_set_t *set)
{
    CPU_ZERO(set);
    int count = 0;
    for (const char *p = list; *p;) {
        char *end;
        long lo = strtol(p, &end, 10);
        if (end == p || lo < 0 || lo >= CPU_SETSIZE) return false;
        long hi = lo;
        p = end;
        if (*p == '-') {
            hi = strtol(p + 1, &end, 10);
            if (end == p + 1 || hi < lo || hi >= CPU_SETSIZE) return false;
            p = end;
        }
        for (long c = lo; c <= hi; ++c, ++count) CPU_SET(int(c), set);
        if (*p == ',') ++p;
        else if (*p) return false;
    }
    return count > 0;
}

// The player owns the layout: map only a complete record of the expected version.
static xz_link_shm *map_shm(const char *path, int *keep_fd)
{
    int fd = open(path, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        log_line("refused", "\"why\":\"open\",\"errno\":%d", errno);
        return nullptr;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size != off_t(sizeof(xz_link_shm))) {
        log_line("refused", "\"why\":\"size\",\"bytes\":%lld", (long long)st.st_size);
        close(fd);
        return nullptr;
    }
    void *p = mmap(nullptr, sizeof(xz_link_shm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        log_line("refused", "\"why\":\"mmap\",\"errno\":%d", errno);
        close(fd);
        return nullptr;
    }
    auto *shm = static_cast<xz_link_shm *>(p);
    const uint32_t magic = __atomic_load_n(&shm->magic, __ATOMIC_ACQUIRE);
    if (magic != XZ_LINK_SHM_MAGIC || shm->version != XZ_LINK_SHM_VERSION || shm->size != sizeof(xz_link_shm)) {
        log_line("refused", "\"why\":\"layout\",\"magic\":%u,\"version\":%u,\"size\":%u", magic, shm->version,
                 shm->size);
        munmap(p, sizeof(xz_link_shm));
        close(fd);
        return nullptr;
    }
    __atomic_store_n(&shm->helper_pid, int32_t(getpid()), __ATOMIC_RELEASE);
    *keep_fd = fd;
    return shm;
}

static void apply_command(XzLink &link, const xz_link_cmd &c)
{
    if (c.ops & XZ_LINK_OP_SET_ENABLED) link.enable(c.enabled != 0);
    const std::chrono::microseconds at(c.at_monotonic_us ? c.at_monotonic_us : link.clock().micros().count());
    const double q = c.quantum > 0 ? c.quantum : XZ_LINK_QUANTUM;
    auto s = link.captureAppSessionState();
    if ((c.ops & XZ_LINK_OP_SET_TEMPO) && c.tempo_bpm >= 20.0 && c.tempo_bpm <= 999.0) s.setTempo(c.tempo_bpm, at);
    if (c.ops & XZ_LINK_OP_REQUEST_BEAT) s.requestBeatAtTime(c.beat, at, q);
    if (c.ops & XZ_LINK_OP_FORCE_BEAT) s.forceBeatAtTime(c.beat, at, q);
    if (c.ops & XZ_LINK_OP_SET_PLAYING) s.setIsPlaying(c.playing != 0, at);
    link.commitAppSessionState(s);
}

static void publish(xz_link_shm *shm, XzLink &link, uint32_t applied)
{
    const auto s = link.captureAppSessionState();
    const int64_t before = monotonic_us();
    const auto host = link.clock().micros();
    const int64_t after = monotonic_us();
    xz_link_state st{};
    st.peers = uint32_t(link.numPeers());
    st.flags = (link.isEnabled() ? XZ_LINK_STATE_ENABLED : 0) | (s.isPlaying() ? XZ_LINK_STATE_PLAYING : 0) |
               (link.isStartStopSyncEnabled() ? XZ_LINK_STATE_START_STOP_SYNC : 0);
    st.applied_serial = applied;
    st.link_host_us = host.count();
    st.monotonic_us = before + (after - before) / 2;
    st.tempo_bpm = s.tempo();
    st.beat = s.beatAtTime(host, XZ_LINK_QUANTUM);
    st.phase = s.phaseAtTime(host, XZ_LINK_QUANTUM);
    st.quantum = XZ_LINK_QUANTUM;
    st.updates = shm->state.updates + 1;
    xz_link_seq_write(&shm->state, &st, sizeof st);
    __atomic_store_n(&shm->heartbeat_us, uint64_t(after), __ATOMIC_RELEASE);
}

int main(int argc, char **argv)
{
    g_start_us = monotonic_us();
    const char *path = "/dev/shm/xz-link/state", *cpus = nullptr, *log_path = nullptr;
    int period_ms = 20;
    double seconds = 0;
    for (int i = 1; i < argc; i += 2) {
        std::string k = argv[i];
        if (i + 1 >= argc) {
            fprintf(stderr, "%s needs a value\n", argv[i]);
            return 2;
        }
        const char *v = argv[i + 1];
        if (k == "--shm") path = v;
        else if (k == "--cpus") cpus = v;
        else if (k == "--log") log_path = v;
        else if (k == "--period-ms") period_ms = atoi(v);
        else if (k == "--seconds") seconds = atof(v);
        else {
            fprintf(stderr, "unknown option %s\n", argv[i]);
            return 2;
        }
    }
    if (period_ms < 5 || period_ms > 100) {
        fprintf(stderr, "--period-ms must be 5..100\n");
        return 2;
    }
    if (log_path) g_log = open(log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0644);

    log_line("start", "\"pid\":%d,\"shm\":\"%.96s\",\"period_ms\":%d,\"link\":\"4.1.1\"", int(getpid()), path,
             period_ms);
    if (cpus) {
        cpu_set_t set;
        if (!parse_cpus(cpus, &set) || sched_setaffinity(0, sizeof(set), &set) != 0) {
            log_line("affinity_failed", "\"cpus\":\"%.32s\",\"errno\":%d", cpus, errno);
            return 2;
        }
        log_line("affinity", "\"cpus\":\"%.32s\"", cpus);
    }
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
    signal(SIGPIPE, SIG_IGN);

    int shm_fd = -1;
    xz_link_shm *shm = map_shm(path, &shm_fd);
    if (!shm) return 3;

    // Link's threads inherit this thread's affinity and policy, so they are created after both are set.
    XzLink *link = nullptr;
    try {
        link = new XzLink(120.0);
        link->enableStartStopSync(false);
        link->enable(true);
    } catch (const std::exception &e) {
        log_line("link_failed", "\"what\":\"%.120s\"", e.what());
        return 4;
    }

    uint32_t applied = 0, commands = 0, tempo_ops = 0, force_ops = 0, last_peers = UINT32_MAX;
    {
        xz_link_cmd cmd;
        // A command written before this helper started belongs to an earlier one: acknowledge, do not apply.
        if (xz_link_seq_read(&shm->cmd, &cmd, sizeof cmd, 64)) applied = cmd.serial;
    }
    publish(shm, *link, applied);
    const int64_t period_us = int64_t(period_ms) * 1000;
    int64_t next_stats = monotonic_us() + 10000000, next_check = 0;
    const char *why = "signal";
    timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    while (!g_stop) {
        const int64_t now = monotonic_us();
        if (now >= next_check) {
            next_check = now + 1000000;
            struct stat st;
            if (fstat(shm_fd, &st) == 0 && st.st_nlink == 0) {
                why = "record_removed";
                break;
            }
        }
        xz_link_cmd cmd;
        if (xz_link_seq_read(&shm->cmd, &cmd, sizeof cmd, 8) && cmd.serial != applied) {
            applied = cmd.serial;
            apply_command(*link, cmd);
            ++commands;
            if (cmd.ops & XZ_LINK_OP_SET_TEMPO) ++tempo_ops;
            if (cmd.ops & XZ_LINK_OP_FORCE_BEAT) {
                ++force_ops;
                log_line("force", "\"serial\":%u,\"beat\":%.4f,\"tempo\":%.4f", cmd.serial, cmd.beat, cmd.tempo_bpm);
            }
        }
        publish(shm, *link, applied);
        const uint32_t peers = uint32_t(link->numPeers());
        if (peers != last_peers) {
            last_peers = peers;
            log_line("peers", "\"n\":%u", peers);
        }
        if (now >= next_stats) {
            next_stats = now + 10000000;
            log_line("stats", "\"peers\":%u,\"tempo\":%.4f,\"commands\":%u,\"tempo_ops\":%u,\"force_ops\":%u,\"applied\":%u",
                     peers, shm->state.tempo_bpm, commands, tempo_ops, force_ops, applied);
        }
        if (seconds > 0 && now - g_start_us >= int64_t(seconds * 1e6)) {
            why = "seconds";
            break;
        }
        next.tv_nsec += long(period_us % 1000000) * 1000;
        next.tv_sec += time_t(period_us / 1000000);
        if (next.tv_nsec >= 1000000000) {
            next.tv_nsec -= 1000000000;
            next.tv_sec += 1;
        }
        // After a long stall do not fire a burst of catch-up publishes.
        if (monotonic_us() - (int64_t(next.tv_sec) * 1000000 + next.tv_nsec / 1000) > period_us)
            clock_gettime(CLOCK_MONOTONIC, &next);
        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, nullptr) == EINTR && !g_stop) {
        }
    }
    link->enable(false);
    log_line("exit", "\"why\":\"%s\",\"commands\":%u,\"tempo_ops\":%u,\"force_ops\":%u", why, commands, tempo_ops,
             force_ops);
    delete link;
    return 0;
}
