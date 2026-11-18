/* xz-link-probe: plays the mod-library side of the xz-link-peer contract.
 *   xz-link-probe dump <shm>
 *   xz-link-probe watch <shm> <seconds>
 *   xz-link-probe tempo|request|force|play|enable <shm> <value>
 *   xz-link-probe compare <shmA> <shmB> <seconds> [csv]
 */
#define _GNU_SOURCE
#include "xz_link_shm.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static int64_t mono_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static void sleep_us(long us)
{
    struct timespec ts = {us / 1000000, (us % 1000000) * 1000};
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
    }
}

static struct xz_link_shm *open_shm(const char *path)
{
    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "open %s: %s\n", path, strerror(errno));
        exit(1);
    }
    void *p = mmap(NULL, sizeof(struct xz_link_shm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    struct xz_link_shm *shm = p;
    if (__atomic_load_n(&shm->magic, __ATOMIC_ACQUIRE) != XZ_LINK_SHM_MAGIC || shm->version != XZ_LINK_SHM_VERSION) {
        fprintf(stderr, "%s: bad magic/version\n", path);
        exit(1);
    }
    return shm;
}

static int create(const char *path, const char *variant)
{
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) { perror("open"); return 1; }
    size_t size = variant && !strcmp(variant, "bad-size") ? sizeof(struct xz_link_shm) - 8 : sizeof(struct xz_link_shm);
    if (ftruncate(fd, (off_t)size)) { perror("ftruncate"); return 1; }
    struct xz_link_shm shm;
    memset(&shm, 0, sizeof shm);
    shm.version = variant && !strcmp(variant, "bad-version") ? 1u : XZ_LINK_SHM_VERSION;
    shm.size = sizeof shm;
    shm.magic = variant && !strcmp(variant, "bad-magic") ? 0x12345678u : XZ_LINK_SHM_MAGIC;
    if (pwrite(fd, &shm, size < sizeof shm ? size : sizeof shm, 0) < 0) { perror("pwrite"); return 1; }
    close(fd);
    return 0;
}

static int read_state(struct xz_link_shm *shm, struct xz_link_state *st)
{
    return xz_link_seq_read(&shm->state, st, sizeof(*st), 1000);
}

static void print_state(const char *tag, const struct xz_link_state *s)
{
    printf("%s age_ms=%.1f peers=%u tempo=%.3f beat=%.4f phase=%.4f q=%.1f playing=%u enabled=%u host_us=%lld mono_us=%lld "
           "updates=%llu applied=%u\n",
           tag, (mono_us() - s->monotonic_us) / 1e3, s->peers, s->tempo_bpm, s->beat, s->phase, s->quantum, (s->flags & XZ_LINK_STATE_PLAYING) ? 1u : 0u,
           (s->flags & XZ_LINK_STATE_ENABLED) ? 1u : 0u, (long long)s->link_host_us, (long long)s->monotonic_us,
           (unsigned long long)s->updates, s->applied_serial);
}

static int command(struct xz_link_shm *shm, const char *op, double value)
{
    struct xz_link_cmd c;
    memcpy(&c, (const void *)&shm->cmd, sizeof(c));
    c.ops = 0;
    c.quantum = 0;
    c.at_monotonic_us = 0;
    if (!strcmp(op, "tempo")) { c.ops = XZ_LINK_OP_SET_TEMPO; c.tempo_bpm = value; }
    else if (!strcmp(op, "request")) { c.ops = XZ_LINK_OP_REQUEST_BEAT; c.beat = value; }
    else if (!strcmp(op, "force")) { c.ops = XZ_LINK_OP_FORCE_BEAT; c.beat = value; }
    else if (!strcmp(op, "play")) { c.ops = XZ_LINK_OP_SET_PLAYING; c.playing = value != 0; }
    else if (!strcmp(op, "enable")) { c.ops = XZ_LINK_OP_SET_ENABLED; c.enabled = value != 0; }
    else { fprintf(stderr, "unknown op %s\n", op); return 2; }
    c.serial = shm->cmd.serial + 1;

    xz_link_seq_write_begin(&shm->cmd.seq);
    c.seq = shm->cmd.seq;
    memcpy((void *)&shm->cmd, &c, sizeof(c));
    xz_link_seq_write_end(&shm->cmd.seq);

    int64_t sent = mono_us();
    for (int i = 0; i < 400; i++) {
        struct xz_link_state st;
        if (read_state(shm, &st) && st.applied_serial == c.serial) {
            printf("acked serial=%u after %.1f ms\n", c.serial, (mono_us() - sent) / 1e3);
            print_state("state", &st);
            return 0;
        }
        sleep_us(5000);
    }
    fprintf(stderr, "no ack for serial %u\n", c.serial);
    return 1;
}

static int compare(struct xz_link_shm *a, struct xz_link_shm *b, double seconds, const char *csv_path)
{
    FILE *csv = csv_path ? fopen(csv_path, "w") : NULL;
    if (csv) fprintf(csv, "t_s,peersA,peersB,tempoA,tempoB,diff_mbeat,diff_us,ageA_us,ageB_us\n");
    int64_t start = mono_us(), next = start;
    long n = 0, torn_retries = 0, skipped = 0;
    double sum = 0, sum_abs = 0, max_abs = 0, sum_us = 0, sum_abs_us = 0, max_abs_us = 0;
    while (mono_us() - start < (int64_t)(seconds * 1e6)) {
        struct xz_link_state sa, sb;
        if (!read_state(a, &sa) || !read_state(b, &sb)) { torn_retries++; continue; }
        int64_t now = mono_us();
        if (sa.peers == 0 || sb.peers == 0 || fabs(sa.tempo_bpm - sb.tempo_bpm) > 1e-3) {
            skipped++;
        } else {
            double q = sa.quantum;
            double d = xz_link_beat_at(&sa, now) - xz_link_beat_at(&sb, now);
            d = d - q * floor(d / q + 0.5);
            double dus = d * 60e6 / sa.tempo_bpm;
            n++;
            sum += d * 1e3; sum_abs += fabs(d) * 1e3; sum_us += dus; sum_abs_us += fabs(dus);
            if (fabs(d) * 1e3 > max_abs) max_abs = fabs(d) * 1e3;
            if (fabs(dus) > max_abs_us) max_abs_us = fabs(dus);
            if (csv)
                fprintf(csv, "%.3f,%u,%u,%.3f,%.3f,%.6f,%.2f,%lld,%lld\n", (now - start) / 1e6, sa.peers, sb.peers,
                        sa.tempo_bpm, sb.tempo_bpm, d * 1e3, dus, (long long)(now - sa.monotonic_us),
                        (long long)(now - sb.monotonic_us));
        }
        next += 10000;
        int64_t wait = next - mono_us();
        if (wait > 0) sleep_us(wait);
    }
    if (csv) fclose(csv);
    printf("compare samples=%ld skipped=%ld read_failures=%ld\n", n, skipped, torn_retries);
    if (n) {
        printf("phase diff A-B: mean=%.4f mbeat mean_abs=%.4f mbeat max_abs=%.4f mbeat\n", sum / n, sum_abs / n, max_abs);
        printf("phase diff A-B: mean=%.2f us mean_abs=%.2f us max_abs=%.2f us\n", sum_us / n, sum_abs_us / n, max_abs_us);
    }
    return n ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[1], "create"))
        return create(argv[2], argc >= 4 ? argv[3] : NULL);
    if (argc >= 3 && !strcmp(argv[1], "dump")) {
        struct xz_link_state st;
        if (!read_state(open_shm(argv[2]), &st)) return 1;
        print_state("state", &st);
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "watch")) {
        struct xz_link_shm *shm = open_shm(argv[2]);
        int64_t end = mono_us() + (int64_t)(atof(argv[3]) * 1e6);
        while (mono_us() < end) {
            struct xz_link_state st;
            if (read_state(shm, &st)) print_state("state", &st);
            sleep_us(500000);
        }
        return 0;
    }
    if (argc >= 5 && !strcmp(argv[1], "compare"))
        return compare(open_shm(argv[2]), open_shm(argv[3]), atof(argv[4]), argc >= 6 ? argv[5] : NULL);
    if (argc >= 4)
        return command(open_shm(argv[2]), argv[1], atof(argv[3]));
    fprintf(stderr, "usage: see source header\n");
    return 2;
}
