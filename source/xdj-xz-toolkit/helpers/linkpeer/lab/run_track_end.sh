#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Lab case for the unit's r29 failure: the stock master's track ends, the deck keeps its play flag and BPM, and
# its bar phase stands still. The REAL player half (link_runtime.c, link_policy.c, link_clock.c) runs in the
# stand-in against a simulated stock snapshot sampled every 33-40 ms with +-0.5 ms stamp jitter and a clock drift;
# the REAL helper (out/host/xz-link-peer, Ableton Link 4.1.1) publishes every 20 ms in a private network namespace
# with no route out (multicast stays on its own veth). Nothing reaches the LAN.
#
# Run as root in WSL Ubuntu after `python ../build.py`:
#   bash run_track_end.sh <out-dir> [policy.c]
# policy.c defaults to the tree's mods/link_policy.c; pass an older copy to see the old behaviour.
# Pass: one force at the first lock (before 5 s), none while the master stands still (20-50 s), exactly one within
# 2 s of the next track (50 s), and the helper's own log agrees (its "force" lines). Exit 0 on pass, 1 on fail.
set -uo pipefail
LAB="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PEER="$(cd "$LAB/.." && pwd)"
MODS="$(cd "$PEER/../../mods" && pwd)"
OUT="$1"; POLICY="${2:-$MODS/link_policy.c}"
HELPER="$PEER/out/host/xz-link-peer"
SHM=/dev/shm/xzk-te
BIN=/tmp/xzk-te-bin
NS=xzk-te
SECS=70 FREEZE=20 FREEZE_LEN=30 DRIFT=40
mkdir -p "$OUT" "$BIN"
cleanup() {
  ip netns pids "$NS" 2>/dev/null | xargs -r kill -KILL 2>/dev/null
  ip netns del "$NS" 2>/dev/null
  rm -rf "$SHM"
}
trap cleanup EXIT
cleanup
ip netns add "$NS"
ip -n "$NS" link set lo up
ip -n "$NS" route add 224.0.0.0/4 dev lo
if ip -n "$NS" route | grep -q default; then echo "the namespace has a default route; stopping"; exit 3; fi

gcc -std=c11 -O2 -Wall -Wextra -Werror -pthread -I"$PEER/src" -I"$MODS" -DXZ_LINK_DIR="\"$SHM/a\"" \
  "$LAB/standin.c" "$MODS/link_runtime.c" "$POLICY" "$MODS/link_clock.c" "$MODS/djlink_facts.c" \
  -lm -o "$BIN/standin" || exit 3
mkdir -p "$SHM/a"
cp "$HELPER" "$SHM/a/xz-link-peer"
chmod 700 "$SHM/a/xz-link-peer"

# The stand-in starts the helper itself (vfork + execve from XZ_LINK_DIR), as the player does.
XZ_LAB_FREEZE_S=$FREEZE XZ_LAB_FREEZE_LEN_S=$FREEZE_LEN ip netns exec "$NS" "$BIN/standin" "$SECS" "$DRIFT" 1000 1000 0 \
  "$OUT/truth.bin" "$OUT/standin.log" "$OUT/standin.txt" 0 "$OUT/trace.csv"
cp "$SHM/a/log" "$OUT/helper.log" 2>/dev/null

python3 - "$OUT" "$FREEZE" "$FREEZE_LEN" <<'PY'
import json, sys
out, freeze, length = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
result = dict(line.split(" ", 1) if " " in line else (line, "") for line in open(f"{out}/standin.txt").read().splitlines())
forces = [float(x) for x in result.get("force_times_s", "").split()]
helper = [json.loads(l) for l in open(f"{out}/helper.log") if l.startswith("{")]
helper_forces = [e for e in helper if e.get("ev") == "force"]
stats = [e for e in helper if e.get("ev") == "stats"]
resume = freeze + length
first = [t for t in forces if t < 5]
during = [t for t in forces if freeze <= t < resume]
after = [t for t in forces if resume <= t < resume + 2]
other = [t for t in forces if t >= 5 and not (freeze <= t < resume + 2)]
rows = [
    ("one force at the first lock", len(first) == 1, f"{first}"),
    ("no force while the master stands still and says it plays", not during, f"{len(during)} forces: {during[:6]}"),
    ("exactly one force for the next track", len(after) == 1, f"{after}"),
    ("no other force", not other, f"{other}"),
    ("helper log agrees (its force lines)", len(helper_forces) == len(forces), f"helper {len(helper_forces)}, player {len(forces)}"),
]
for name, ok, detail in rows:
    print(f"{'PASS' if ok else 'FAIL'} {name}: {detail}")
print("helper stats lines (tempo_ops, force_ops):", [(s.get('t'), s.get('tempo_ops'), s.get('force_ops')) for s in stats])
sys.exit(0 if all(ok for _, ok, _ in rows) else 1)
PY
