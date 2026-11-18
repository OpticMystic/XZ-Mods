/* USB STOP (or a pulled stick) and DECK 3/4: the real players, the real RAM copy service, a fake transport.
   A deck whose track plays from its attached RAM copy plays on and the stick's descriptors close; a deck whose track
   is on the stick and not in RAM is unloaded and its file closed; a deck on the other stick is not touched. The stick
   is a folder; its descriptors are counted through /proc/self/fd. Linux only.
   Usage: test-usb-stop-decks FIXTURE_DIR REFERENCE_WAV (FIXTURE_DIR receives usb1/ and usb2/). */
#define main retained_player_acceptance
#include "test_extra_player.c"
#undef main
#include "ram/budget.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <sys/stat.h>
#include <unistd.h>

static char stick[2][512], track_path[3][600];
static atomic_uint stick_generation[2];
static uint64_t gone_generation(void *unused, unsigned source) {
 (void)unused; assert(source == 1 || source == 2);
 return atomic_load(&stick_generation[source - 1]);
}
static uint64_t gone_cue_generation(unsigned source) { return gone_generation(NULL, source); }
/* Tracks 1 and 2 are on the stick in USB 1, track 3 on the one in USB 2. */
static int gone_resolve(int source, uint32_t id, uint64_t generation, struct xz_extra_track *out) {
 if (id < 1 || id > 3 || source != (id == 3 ? 2 : 1) || generation != gone_generation(NULL, (unsigned)source)) return -1;
 int rc = resolve(source, 1, out);
 out->source = source; out->track_id = id;
 snprintf(out->path, sizeof out->path, "%s", track_path[id - 1]);
 return rc;
}
static int no_cue_write(int deck, const struct xz_extra_track *t, uint64_t gen, int32_t length, const struct xz_saved_cues *s,
                        const struct xz_cue_edit *edit) {
 (void)deck; (void)t; (void)gen; (void)length; (void)s; (void)edit; return 1;
}

/* What each deck answered. */
static struct { int deck, slot; uint32_t stop; enum xz_xp_release what; } said[16];
static int says;
static void answer(void *ctx, int deck, int slot, uint32_t stop, enum xz_xp_release what) {
 assert(ctx == &says && says < 16);
 said[says].deck = deck; said[says].slot = slot; said[says].stop = stop; said[says].what = what; says++;
}
static int answered(int deck, int slot, uint32_t stop, enum xz_xp_release what) {
 for (int i = 0; i < says; i++)
  if (said[i].deck == deck && said[i].slot == slot && said[i].stop == stop && said[i].what == what) return 1;
 return 0;
}

/* Open descriptors of this process on one stick (a folder standing in for its mount point). */
static int stick_fds(int slot) {
 DIR *dir = opendir("/proc/self/fd");
 assert(dir);
 int n = 0; size_t len = strlen(stick[slot - 1]);
 struct dirent *entry;
 while ((entry = readdir(dir))) {
  char link[300], target[1024];
  if (entry->d_name[0] < '0' || entry->d_name[0] > '9') continue;
  snprintf(link, sizeof link, "/proc/self/fd/%s", entry->d_name);
  ssize_t got = readlink(link, target, sizeof target - 1);
  if (got <= 0) continue;
  target[got] = 0;
  if (!strncmp(target, stick[slot - 1], len) && target[len] == '/') n++;
 }
 closedir(dir);
 return n;
}
static uint64_t wall_ms(void) {
 struct timespec t; assert(!clock_gettime(CLOCK_MONOTONIC, &t));
 return (uint64_t)t.tv_sec * 1000u + (unsigned long)t.tv_nsec / 1000000u;
}
static void real_nap(void) { struct timespec t = {0, 2000000}; nanosleep(&t, NULL); }
/* Background workers (the RAM copy service) close on their own time: wait for it, bounded. */
static void wait_stick_fds(int slot, int want, int stepping) {
 uint64_t deadline = wall_ms() + 10000;
 while (stick_fds(slot) != want) {
  if (wall_ms() >= deadline) { fprintf(stderr, "USB %d: %d descriptor(s) open, expected %d\n", slot, stick_fds(slot), want); assert(0); }
  if (stepping) advance(10);
  real_nap();
 }
}
static struct xz_ram_status ram_of(struct xz_ram_extra *s, unsigned d) { struct xz_ram_status r; xz_ram_extra_status(s, d, &r); return r; }
static void copy_file(const char *from, const char *to) {
 FILE *in = fopen(from, "rb"), *out = fopen(to, "wb"); assert(in && out);
 unsigned char b[32768]; size_t n;
 while ((n = fread(b, 1, sizeof b, in))) assert(fwrite(b, 1, n, out) == n);
 assert(!ferror(in)); fclose(in); assert(!fclose(out));
}
static void load_from(int d, uint32_t id) {
 struct xz_extra_deck_event e = event(d, XZ_EXTRA_IN_LOAD, 1);
 e.load.source = id == 3 ? 2 : 1; e.load.track_id = id; e.load.media_generation = gone_generation(NULL, (unsigned)e.load.source);
 assert(input(e)); step();
 assert(view.deck[d].loaded);
}
static unsigned submitted(int d) { pthread_mutex_lock(&f.lock); unsigned n = f.submits[d]; pthread_mutex_unlock(&f.lock); return n; }
/* What the runtime's ask does (extra_decks.c decks_let_go): the copies first, then each deck's queue. The shell
   publishes the hidden stick as gone a little later, on its own worker (shell_runtime.c): the tests do that after
   the decks have stepped, the order that leaves a RAM release free to hand a deck back to the stick. */
static void stick_goes(struct xz_ram_extra *service, int slot, uint32_t stop) {
 if (service) xz_ram_extra_source_gone(service, (unsigned)slot);
 assert(xz_xp_release_source(p, slot, stop) == 2);
}

/* DECK 3 plays track 1 from its RAM copy, with a RAM release (a check of the stick file) under way when the stick
   goes; DECK 4 plays track 2 from the stick (its copy was cancelled). USB STOP on USB 1: DECK 3 plays on and the
   check's descriptor closes; DECK 4 is unloaded and its file closes. */
static void with_ram_service(void) {
 clock_ms = 1000; master = 12000;
 atomic_store(&stick_generation[0], 99); atomic_store(&stick_generation[1], 7); says = 0;
 struct xz_xp_env env = {{NULL, fake_link, fake_submit, fake_flush, fake_position, fake_transition}, now_ms, resolve, master_bpm, NULL,
                         gone_resolve, 0, 0};
 p = xz_xp_create(&env); assert(p); fake_route(12345); step();
 struct xz_ram_extra *service = xz_ram_extra_create(NULL, fake_link, gone_generation); assert(service);
 struct xz_ram_player_service ops = xz_ram_extra_player_service(service);
 xz_xp_set_ram_service(p, &ops); xz_xp_set_source_generation(p, gone_cue_generation); xz_xp_set_cue_store(p, NULL, no_cue_write);
 xz_xp_set_release_answer(p, answer, &says);
 load_from(0, 1); load_from(1, 2);
 struct xz_ram_status s1 = ram_of(service, 1);
 assert(xz_ram_extra_command(service, 1, XZ_RAM_CANCEL, &s1.key) == XZ_RAM_ACCEPTED);
 press(0, XZ_EXTRA_IN_PLAY); press(1, XZ_EXTRA_IN_PLAY);
 uint64_t deadline = wall_ms() + 10000;
 while (ram_of(service, 0).state != XZ_RAM_READY) { assert(wall_ms() < deadline && ram_of(service, 0).state != XZ_RAM_ERROR); advance(10); real_nap(); }
 assert(view.deck[0].playing && view.deck[1].playing);
 /* DECK 3 now reads its RAM copy: only DECK 4's file is open on the stick. */
 wait_stick_fds(1, 1, 1);
 /* RAM release on DECK 3: its worker opens the stick file to check it. The decks are not stepped, so the reader
    is not handed back yet; the stick goes now. */
 struct xz_ram_status s0 = ram_of(service, 0);
 assert(xz_ram_extra_command(service, 0, XZ_RAM_RELEASE, &s0.key) == XZ_RAM_ACCEPTED);
 wait_stick_fds(1, 2, 0);
 for (unsigned i = 0; i < 20; i++) real_nap(); /* let the check finish too: it is about to hand the reader back */
 stick_goes(service, 1, 41);
 unsigned before = submitted(0);
 advance(10);
 atomic_store(&stick_generation[0], 0);
 assert(answered(0, 1, 41, XZ_XP_RELEASE_KEPT) && answered(1, 1, 41, XZ_XP_RELEASE_UNLOADED) && says == 2);
 assert(debug(0).state == XZ_XP_PLAYING && view.deck[0].playing && view.deck[0].loaded);
 assert(debug(1).state == XZ_XP_EMPTY && !view.deck[1].loaded && !view.deck[1].playing);
 struct xz_ram_status kept = ram_of(service, 0);
 assert((kept.retained_assets & XZ_RAM_AUDIO) && kept.state != XZ_RAM_ERROR);
 /* Every descriptor of the stick closes: the check's, and DECK 4's track (retired to the RAM service). */
 wait_stick_fds(1, 0, 1);
 /* DECK 3 plays on from RAM; nothing opens the stick again. */
 advance(2000);
 assert(view.deck[0].playing && submitted(0) > before + 100 && !stick_fds(1));
 assert(debug(0).state == XZ_XP_PLAYING && (ram_of(service, 0).retained_assets & XZ_RAM_AUDIO));
 /* The other stick: nothing of either deck is on it. */
 assert(xz_xp_release_source(p, 2, 42) == 2);
 advance(10);
 assert(answered(0, 2, 42, XZ_XP_RELEASE_NOTHING) && answered(1, 2, 42, XZ_XP_RELEASE_NOTHING) && says == 4);
 assert(view.deck[0].playing);
 xz_xp_stop(p);
 xz_ram_extra_destroy(service);
 xz_xp_destroy(p); p = NULL;
 assert(!stick_fds(1));
 puts("PASS USB STOP with RAM copies: DECK 3 plays on from RAM and its pending stick check is closed; DECK 4 (not in RAM) is unloaded; no descriptor stays on the stick");
}

/* No RAM service at all, and the route down when the stick goes: DECK 3 (USB 1) is unloaded and its file closed at
   once; DECK 4 (USB 2) is not touched. The request waits behind a full control queue and still gets its turn. */
static void without_ram_service(void) {
 clock_ms = 1000; master = 12000;
 atomic_store(&stick_generation[0], 99); atomic_store(&stick_generation[1], 7); says = 0;
 struct xz_xp_env env = {{NULL, fake_link, fake_submit, fake_flush, fake_position, fake_transition}, now_ms, resolve, master_bpm, NULL,
                         gone_resolve, 0, 0};
 p = xz_xp_create(&env); assert(p); fake_route(12345); step();
 xz_xp_set_source_generation(p, gone_cue_generation); xz_xp_set_cue_store(p, NULL, no_cue_write);
 xz_xp_set_release_answer(p, answer, &says);
 load_from(0, 1); load_from(1, 3);
 press(0, XZ_EXTRA_IN_PLAY); press(1, XZ_EXTRA_IN_PLAY);
 advance(500);
 assert(view.deck[0].playing && view.deck[1].playing && stick_fds(1) == 1 && stick_fds(2) == 1);
 /* A full queue of ordinary controls on DECK 3: the request still finds its reserved place, behind them. */
 unsigned queued = 0;
 for (;; queued++) { struct xz_extra_deck_event e = event(0, XZ_EXTRA_IN_TEMPO, 1); e.tempo_position = (int32_t)(queued % 3); if (!input(e)) break; }
 assert(queued >= XZ_XP_QUEUE - 1);
 fake_route(0); /* the loopback route is down: the stick goes all the same */
 assert(xz_xp_release_source(p, 1, 51) == 2); /* DECK 3's queue is full for ordinary controls */
 atomic_store(&stick_generation[0], 0);
 for (unsigned i = 0; i < 4 && says < 2; i++) step();
 assert(answered(0, 1, 51, XZ_XP_RELEASE_UNLOADED) && answered(1, 1, 51, XZ_XP_RELEASE_NOTHING) && says == 2);
 assert(debug(0).state == XZ_XP_EMPTY && !view.deck[0].loaded && view.deck[1].loaded);
 assert(!stick_fds(1) && stick_fds(2) == 1); /* closed at once; the other stick's track stays open */
 fake_route(12346); step(); press(1, XZ_EXTRA_IN_PLAY); advance(200);
 assert(view.deck[1].playing && !view.deck[0].loaded);
 /* The unloaded deck takes a new track as before (from the stick that stays). */
 load_from(0, 3);
 assert(view.deck[0].loaded && stick_fds(2) == 2);
 xz_xp_stop(p); xz_xp_destroy(p); p = NULL;
 assert(!stick_fds(1) && !stick_fds(2));
 puts("PASS USB STOP without RAM copies: DECK 3 unloaded and its file closed at once, through a full queue and a down route; DECK 4 on the other stick untouched");
}

int main(int argc, char **argv) {
 assert(argc == 3); assert(!pthread_mutex_init(&f.lock, NULL));
 snprintf(fixtures, sizeof fixtures, "%s", argv[1]);
 snprintf(anlz_dir, sizeof anlz_dir, "%s/anlz", argv[1]);
 build_analysis();
 for (int slot = 1; slot <= 2; slot++) {
  assert(snprintf(stick[slot - 1], sizeof stick[0], "%s/usb%d", argv[1], slot) < (int)sizeof stick[0]);
  mkdir(stick[slot - 1], 0700);
 }
 const char *names[3] = {"usb1/a.wav", "usb1/b.wav", "usb2/c.wav"};
 for (int i = 0; i < 3; i++) {
  assert(snprintf(track_path[i], sizeof track_path[0], "%s/%s", argv[1], names[i]) < (int)sizeof track_path[0]);
  copy_file(argv[2], track_path[i]);
 }
 without_ram_service();
 with_ram_service();
 pthread_mutex_destroy(&f.lock);
 return 0;
}
