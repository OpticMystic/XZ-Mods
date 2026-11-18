#!/usr/bin/env bash
# Ableton Link lab, run as root inside WSL Ubuntu (run_lab.py calls it the way harness/wsl_root.sh does).
#   bash run_lab.sh <out-dir> <seconds> [drift_ppm] [seek_s] [tempo_s] [tempo_pct] [obs_gaps 0|1]
# Three private network namespaces, all named xzk-*: xzk-h holds a bridge with multicast snooping off, xzk-a holds
# the player stand-in (the real player half of the tree) and the helper it starts, xzk-b holds an independent
# reference helper and the measuring program. No namespace has a default route or any interface other than its
# veth to the bridge, so nothing reaches the LAN or the XZ.
set -uo pipefail
LAB="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PEER="$(cd "$LAB/.." && pwd)"
MODS="$(cd "$PEER/../../mods" && pwd)"
OUT="$1"; SECS="$2"; DRIFT="${3:-40}"; SEEK="${4:-120}"; TEMPO_AT="${5:-210}"; TEMPO_PCT="${6:-2}"; OBS_GAPS="${7:-0}"
HELPER="$PEER/out/host/xz-link-peer"
PROBE="$PEER/out/host/xz-link-probe"
SHM=/dev/shm/xzk-lab
BIN=/tmp/xzk-lab-bin
NS=(xzk-a xzk-b)
mkdir -p "$OUT"
LOG="$OUT/run.log"; : > "$LOG"
say() { echo "[$(date +%T)] $*" | tee -a "$LOG"; }
check() { if [ "$2" = 1 ]; then echo "PASS $1" | tee -a "$LOG"; else echo "FAIL $1" | tee -a "$LOG"; fi; }

cleanup() {
  for n in "${NS[@]}"; do ip netns pids "$n" 2>/dev/null | xargs -r kill -TERM 2>/dev/null; done
  sleep 1
  for n in "${NS[@]}"; do ip netns pids "$n" 2>/dev/null | xargs -r kill -KILL 2>/dev/null; done
  for n in "${NS[@]}" xzk-h; do ip netns del "$n" 2>/dev/null; done
  rm -rf "$SHM"
}
trap cleanup EXIT

setup_net() {
  cleanup
  ip netns add xzk-h
  ip -n xzk-h link add br0 type bridge
  ip -n xzk-h link set br0 type bridge mcast_snooping 0
  ip -n xzk-h link set br0 up
  local i=1
  for n in "${NS[@]}"; do
    ip netns add "$n"
    ip link add "v$n" netns "$n" type veth peer name "h$n" netns xzk-h
    ip -n "$n" link set lo up
    ip -n "$n" addr add "10.78.0.$i/24" dev "v$n"
    ip -n "$n" link set "v$n" up
    ip -n "$n" route add 224.0.0.0/4 dev "v$n"
    ip -n xzk-h link set "h$n" master br0
    ip -n xzk-h link set "h$n" up
    i=$((i + 1))
  done
  { for n in "${NS[@]}" xzk-h; do echo "== $n"; ip -n "$n" -br link; ip -n "$n" -br addr; ip -n "$n" route; done; } > "$OUT/netns.txt"
  if grep -q default "$OUT/netns.txt"; then say "a namespace has a default route; stopping"; exit 3; fi
}

build() {
  mkdir -p "$BIN"
  gcc -std=c11 -O2 -Wall -Wextra -Werror -pthread -I"$PEER/src" -I"$MODS" -DXZ_LINK_DIR="\"$SHM/a\"" \
    "$LAB/standin.c" "$MODS/link_runtime.c" "$MODS/link_policy.c" "$MODS/link_clock.c" "$MODS/djlink_facts.c" \
    -lm -o "$BIN/standin" || exit 3
  gcc -std=c11 -O2 -Wall -Wextra -Werror -I"$PEER/src" "$LAB/measure.c" -lm -o "$BIN/measure" || exit 3
}

# ---- the helper by itself --------------------------------------------------------------------------------
helper_checks() {
  mkdir -p "$SHM/c"
  for bad in bad-magic bad-version bad-size; do
    "$PROBE" create "$SHM/c/state" "$bad"
    ip netns exec xzk-a "$HELPER" --shm "$SHM/c/state" --log "$OUT/helper-$bad.log" --seconds 3; rc=$?
    check "helper refuses a record with $bad (exit $rc)" "$([ $rc = 3 ] && grep -q '"refused"' "$OUT/helper-$bad.log" && echo 1)"
  done
  rm -f "$SHM/c/state"
  ip netns exec xzk-a "$HELPER" --shm "$SHM/c/state" --log "$OUT/helper-missing.log" --seconds 3; rc=$?
  check "helper refuses a missing record and creates none (exit $rc)" "$([ $rc = 3 ] && [ ! -e "$SHM/c/state" ] && echo 1)"

  "$PROBE" create "$SHM/c/state"
  ip netns exec xzk-a "$HELPER" --shm "$SHM/c/state" --cpus 0,2 --log "$OUT/helper-term.log" & local pid=$!
  sleep 2
  local hb1 hb2 kids threads cpus
  hb1=$(od -An -tu8 -j16 -N8 "$SHM/c/state" | tr -d ' ')
  sleep 0.5
  hb2=$(od -An -tu8 -j16 -N8 "$SHM/c/state" | tr -d ' ')
  check "helper heartbeat advances ($hb1 -> $hb2)" "$([ "${hb2:-0}" -gt "${hb1:-0}" ] && echo 1)"
  local child_seen=0
  for i in $(seq 1 30); do
    kids=$(pgrep -P "$pid" | wc -l); [ "$kids" -gt 0 ] && child_seen=1
    sleep 0.1
  done
  threads=$(ls /proc/$pid/task | wc -l)
  cpus=$(for t in /proc/$pid/task/*; do grep Cpus_allowed_list "$t/status" | awk '{print $2}'; done | sort -u | tr '\n' ' ')
  echo "helper threads=$threads cpus=$cpus" >> "$LOG"
  check "helper never had a child process over 3 s (threads $threads)" "$([ $child_seen = 0 ] && echo 1)"
  check "helper and Link threads pinned to 0,2 ($cpus)" "$([ "$cpus" = "0,2 " ] && echo 1)"
  kill -TERM "$pid"; wait "$pid"; rc=$?
  check "helper exits 0 on SIGTERM and logs it (exit $rc)" "$([ $rc = 0 ] && grep -q '"why":"signal"' "$OUT/helper-term.log" && echo 1)"

  "$PROBE" create "$SHM/c/state"
  ip netns exec xzk-a "$HELPER" --shm "$SHM/c/state" --log "$OUT/helper-removed.log" & pid=$!
  sleep 2; rm -f "$SHM/c/state"
  local gone=0
  for i in $(seq 1 30); do kill -0 "$pid" 2>/dev/null || { gone=1; break; }; sleep 0.1; done
  wait "$pid" 2>/dev/null
  check "helper leaves within 3 s once the record is removed" "$([ $gone = 1 ] && grep -q '"why":"record_removed"' "$OUT/helper-removed.log" && echo 1)"

  local imports
  imports=$(nm -D --undefined-only "$HELPER" | grep -Ew '(fork|vfork|execv|execve|execvp|execl|execlp|popen|system|posix_spawn|posix_spawnp|daemon)' | tr '\n' ' ')
  check "host helper imports no fork, exec, popen, system or spawn ($imports)" "$([ -z "$imports" ] && echo 1)"
  local arm="$PEER/out/arm-musl/xz-link-peer" sites
  sites=$(arm-linux-gnueabi-objdump -d "$arm" | python3 -I "$LAB/syscall_sites.py" 2>/dev/null)
  echo "$sites" > "$OUT/arm-musl-syscall-sites.txt"
  check "ARM static helper has no fork, vfork or execve system call site" "$(echo "$sites" | grep -Eqw '(fork|vfork|execve|execveat)' || echo 1)"
}

# ---- the five-minute run -----------------------------------------------------------------------------------
main_run() {
  mkdir -p "$SHM/a" "$SHM/b"
  cp "$HELPER" "$SHM/a/xz-link-peer"; chmod 700 "$SHM/a/xz-link-peer"
  "$PROBE" create "$SHM/b/state"
  say "reference helper in xzk-b"
  ip netns exec xzk-b "$HELPER" --shm "$SHM/b/state" --log "$OUT/helper-b.log" & local pb=$!
  sleep 3
  say "measure in xzk-b, player stand-in in xzk-a: $SECS s, drift $DRIFT ppm, seek at $SEEK s, tempo +$TEMPO_PCT % at $TEMPO_AT s, injected observation gaps $OBS_GAPS"
  ip netns exec xzk-b "$BIN/measure" "$SHM/b/state" "$SHM/truth" "$SECS" 10 5 "$OUT/measure.csv" "$OUT/measure.txt" & local pm=$!
  ip netns exec xzk-a "$BIN/standin" "$SECS" "$DRIFT" "$SEEK" "$TEMPO_AT" "$TEMPO_PCT" "$SHM/truth" "$OUT/standin.log" "$OUT/standin.txt" \
    "$OBS_GAPS" "$OUT/standin-trace.csv"
  local rs=$?
  wait "$pm"; local rm_=$?
  cp "$SHM/a/log" "$OUT/helper-a.log" 2>/dev/null
  local left
  left=$(pgrep -f "$SHM/a/xz-link-peer" | tr '\n' ' ')
  check "stand-in finished (exit $rs) and the measure finished (exit $rm_)" "$([ $rs = 0 ] && [ $rm_ = 0 ] && echo 1)"
  check "setting off: the helper started by the player is gone ($left) and its record removed" "$([ -z "$left" ] && [ ! -e "$SHM/a/state" ] && echo 1)"
  kill -TERM "$pb"; wait "$pb"
  say "done"
}

say "kernel $(uname -r); host load $(cut -d' ' -f1-3 /proc/loadavg)"
setup_net
build
helper_checks
main_run
