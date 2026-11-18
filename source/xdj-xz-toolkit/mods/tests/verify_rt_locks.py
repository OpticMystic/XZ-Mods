#!/usr/bin/env python3
"""Guard against priority inversion through locks without priority inheritance.

Lists every condition-variable call, every rwlock call, every plain (non-inheriting) mutex and every spinlock in
the code loaded into the player (mods/ and the display bridge), by file and enclosing function, and compares them
with ALLOWED below. A new site fails until someone decides, and writes down, why no real-time thread can wait on
it behind a lower-priority thread. A listed site that no longer exists fails too, so the list stays true.

The rule (mods/xz_wake.h): a thread at real-time priority never signals or waits on a condition variable, and never
touches an rwlock or a plain mutex, that a lower-priority thread also uses. Wake workers with xz_wake; exclude with
the priority-inheriting mutexes of mods/xz_mutex.h.

Usage: python verify_rt_locks.py [--list]   (exit 0 when the sites match the list)
"""
import pathlib
import re
import sys

MODS = pathlib.Path(__file__).resolve().parents[1]
BRIDGE = MODS.parent / "vendor" / "tools" / "xz_runtime"
SITE = re.compile(r"\b(pthread_cond_(?:signal|broadcast|wait|timedwait)|pthread_rwlock_[a-z]+|pthread_spin_[a-z]+)\s*\("
                  r"|(PTHREAD_MUTEX_INITIALIZER)|\b(pthread_mutex_init)\s*\([^,]*,\s*(?:NULL|0)\s*\)")
FUNCTION = re.compile(r"^(?:static\s+|inline\s+|extern\s+)*[A-Za-z_][\w\s\*]*?\b([A-Za-z_]\w*)\s*\([^;]*$")
# Not linked into the player: tests, third-party code without threads of its own, the pinned historical bridge,
# and the offline navigation fixture (it includes test_extra_player.c).
SKIP = re.compile(r"(^|/)(tests?|vendor)/|(^|/)test_[^/]*$|_test\.c$|^integration/pinned/|^shell/nav_adapter_")

# (path relative to mods/ or "bridge/<file>", enclosing function, call) -> why no real-time thread can wait behind a
# lower-priority thread here. Keep the reason specific: who touches the lock, at what priority.
ALLOWED = {
    ("xz_mutex.h", "?", "PTHREAD_MUTEX_INITIALIZER"): "fallback definition for non-glibc host builds only",
    ("xz_mutex.h", "xz_mutex_init", "pthread_mutex_init"):
        "fallback when the C library refuses priority inheritance; xz_mutex_self_check proves it at start",
    ("audio/overcue_stream.c", "?", "PTHREAD_MUTEX_INITIALIZER"):
        "pin_turn: only ever pthread_mutex_trylock'ed, by stem workers (normal priority); nobody waits on it",
    ("audio/usb_transport/usb_transport.c", "allocate_transport", "pthread_mutex_init"):
        "snapshot_lock: readers (DECK 3/4 RR 59, connector RR 60) only trylock; the USB worker (RR 60) trylocks, and "
        "locks only at its own exit",
    ("extra_decks.c", "sink_submit", "pthread_rwlock_tryrdlock"):
        "route.lock: DECK 3/4 threads (RR 59) and the connector (RR 60) only, plus the diagnostic capture below",
    ("extra_decks.c", "sink_submit", "pthread_rwlock_unlock"): "route.lock, as sink_submit",
    ("extra_decks.c", "sink_flush", "pthread_rwlock_tryrdlock"): "route.lock, as sink_submit",
    ("extra_decks.c", "sink_flush", "pthread_rwlock_unlock"): "route.lock, as sink_submit",
    ("extra_decks.c", "sink_transition", "pthread_rwlock_tryrdlock"): "route.lock, as sink_submit",
    ("extra_decks.c", "sink_transition", "pthread_rwlock_unlock"): "route.lock, as sink_submit",
    ("extra_decks.c", "sink_position", "pthread_rwlock_tryrdlock"): "route.lock, as sink_submit",
    ("extra_decks.c", "sink_position", "pthread_rwlock_unlock"): "route.lock, as sink_submit",
    ("extra_decks.c", "set_link", "pthread_rwlock_wrlock"): "route.lock: connector (RR 60) only",
    ("extra_decks.c", "set_link", "pthread_rwlock_unlock"): "route.lock: connector (RR 60) only",
    ("extra_decks.c", "close_route", "pthread_rwlock_wrlock"): "route.lock: connector (RR 60) only",
    ("extra_decks.c", "close_route", "pthread_rwlock_unlock"): "route.lock: connector (RR 60) only",
    ("extra_decks.c", "connector", "pthread_rwlock_wrlock"): "route.lock: connector (RR 60) only",
    ("extra_decks.c", "connector", "pthread_rwlock_unlock"): "route.lock: connector (RR 60) only",
    ("extra_decks.c", "xz_extra_decks_usb_capture", "pthread_rwlock_tryrdlock"):
        "KNOWN EXCEPTION: bench op 10-12 (bench worker, normal priority; diagnostic loader only, "
        "XZ_MODS_TEST_CONTROL). See the inversion REPORT.md",
    ("extra_decks.c", "xz_extra_decks_usb_capture", "pthread_rwlock_unlock"): "KNOWN EXCEPTION, as above",
    ("network/service.c", "publish", "pthread_cond_broadcast"):
        "s->changed carries worker-to-stopper news only; the worker itself is woken through xz_wake",
    ("network/service.c", "worker", "pthread_cond_broadcast"): "as publish",
    ("network/service.c", "xz_net_service_stop", "pthread_cond_timedwait"): "the stopper at shutdown (normal priority)",
    ("key/browser.c", "worker", "pthread_cond_timedwait"): "key browser worker; only xz_key_browser_stop signals, at shutdown",
    ("key/browser.c", "xz_key_browser_stop", "pthread_cond_signal"): "shutdown only",
    ("ram/budget.c", "xz_ram_copy_lock", "pthread_cond_wait"):
        "one-copy turn between RAM-copy and stem workers (idle and normal priority); no real-time caller",
    ("ram/budget.c", "xz_ram_copy_unlock", "pthread_cond_broadcast"): "as xz_ram_copy_lock",
    ("bridge/xz_directfb_hook.c", "vsync_waiter", "pthread_cond_broadcast"):
        "vsync_cond: xz-vsync (FIFO 27) to xz-present (FIFO 26) only",
    ("bridge/xz_directfb_hook.c", "presenter_wait", "pthread_cond_timedwait"): "vsync_cond: xz-present (FIFO 26) only",
}


def strip_comments(text):
    """Blank out comments, keeping line numbers, so prose about the rule is not counted as a site."""
    out, i, n = [], 0, len(text)
    while i < n:
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join(c if c == "\n" else " " for c in text[i:j]))
            i = j
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif text[i] == '"':
            j = i + 1
            while j < n and text[j] not in '"\n':
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def sites():
    for root, prefix in ((MODS, ""), (BRIDGE, "bridge/")):
        for path in sorted(list(root.rglob("*.c")) + list(root.rglob("*.h"))):
            rel = path.relative_to(root).as_posix()
            if SKIP.search(rel) or (prefix and not rel.startswith("xz_")):
                continue
            function = "?"
            text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
            for number, line in enumerate(text.splitlines(), 1):
                if line and not line[0].isspace() and line[0] not in "#}":
                    m = FUNCTION.match(line)
                    if m and m.group(1) not in ("if", "for", "while", "switch", "return", "sizeof"):
                        function = m.group(1)
                elif line.startswith("}"):
                    function = "?"
                for m in SITE.finditer(line):
                    yield (prefix + rel, function, next(g for g in m.groups() if g)), number


def main():
    found = {}
    for key, number in sites():
        found.setdefault(key, []).append(number)
    if "--list" in sys.argv:
        for (path, function, call), numbers in sorted(found.items()):
            print(f"{path}:{','.join(map(str, numbers))}: {function}: {call}")
        return 0
    new = sorted(set(found) - set(ALLOWED))
    stale = sorted(set(ALLOWED) - set(found))
    for path, function, call in new:
        print(f"NEW  {path}:{found[(path, function, call)][0]}: {call} in {function}: a real-time thread must not wait "
              f"behind a lower-priority one here; use xz_wake or xz_mutex.h, or add it to ALLOWED with the reason")
    for path, function, call in stale:
        print(f"GONE {path}: {call} in {function}: remove it from ALLOWED")
    print(f"{len(found)} lock sites, {len(new)} new, {len(stale)} gone")
    return 1 if new or stale else 0


if __name__ == "__main__":
    sys.exit(main())
