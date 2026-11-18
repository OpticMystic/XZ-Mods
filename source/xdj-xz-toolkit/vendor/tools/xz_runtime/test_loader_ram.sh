#!/bin/sh
# Host test for orchestrator.sh's LOADER_RAM block: after the loader returns, the stock decryptor's single
# unmount of /mnt/iso must succeed although the loader leaves background subshells running.
# Linux, as root (it mounts a tmpfs): wsl -d Ubuntu -u root -- sh test_loader_ram.sh
# Each available shell (dash, bash, busybox sh) is tried. The control run without the block must leave the
# mount busy, which is the leak measured on the unit.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
LOADER="$HERE/orchestrator.sh"
WORK=$(mktemp -d /root/xz-loader-ram-test.XXXXXX) || exit 1
ISO="$WORK/iso"
FAILED=0
mkdir -p "$ISO" "$WORK/out"
trap 'umount "$ISO" 2>/dev/null; rm -rf "$WORK"' 0

block() { sed -n '/^# LOADER_RAM_BEGIN$/,/^# LOADER_RAM_END$/p' "$LOADER"; }
[ "$(block | wc -l)" -gt 5 ] || { echo "FAIL: LOADER_RAM block not found"; exit 1; }

# A stand-in for autoexec.sh: the block (or not), then what the loader leaves behind when it returns.
make_script() {
    {
        echo '#!/bin/sh'
        [ "$1" = with ] && block
        cat <<'EOF'
echo "$$ ${1:-none} $(pwd) $0" > "$OUT/args"
# usb_coldplug_start: a forked subshell that loops (never exec'd, so it keeps the script descriptor)
( i=0; while [ "$i" -lt 40 ]; do sleep 0.1; i=$((i + 1)); done ) </dev/null >/dev/null 2>&1 &
# the early-boot verifier: cd / first, still a fork of this shell
( cd / || exit; i=0; while [ "$i" -lt 40 ]; do sleep 0.1; i=$((i + 1)); done ) </dev/null >/dev/null 2>&1 &
exit 0
EOF
    } > "$ISO/autoexec.sh"
    chmod 755 "$ISO/autoexec.sh"
}

check() { # name condition message
    if eval "$2"; then echo "ok   $1: $3"; else echo "FAIL $1: $3"; FAILED=1; fi
}

run_case() { # shell variant
    sh_bin=$1 variant=$2 name="$1/$2"
    mount -t tmpfs -o size=1m xz-iso "$ISO" || { echo "FAIL: cannot mount tmpfs (root?)"; exit 1; }
    make_script "$variant"
    rm -f "$WORK/out/args"
    # As decrypt_autoexec.sh: run ./autoexec.sh from /mnt/iso, wait for it, then unmount once.
    ( cd "$ISO" && OUT="$WORK/out" exec "$sh_bin" ./autoexec.sh /media/usb2/sda2 ) &
    pid=$!
    wait "$pid"
    if umount "$ISO" 2>/dev/null; then unmounted=1; else unmounted=0; fi
    read -r got_pid got_arg got_cwd got_zero < "$WORK/out/args" 2>/dev/null || got_pid=""
    if [ "$variant" = with ]; then
        check "$name" '[ "$unmounted" = 1 ]' "the decryptor's single unmount succeeds with loader children alive"
        check "$name" '[ "$got_pid" = "$pid" ]' "same process (pid $pid) after the re-exec"
        check "$name" '[ "$got_arg" = /media/usb2/sda2 ]' "the USB argument passes through"
        check "$name" '[ "$got_cwd" = / ]' "runs in /"
        check "$name" '[ "$got_zero" = "/dev/shm/xz-loader-$pid/autoexec.sh" ]' "runs the RAM copy"
        check "$name" '[ ! -e "/dev/shm/xz-loader-$pid" ]' "the RAM copy is removed while it runs"
    else
        check "$name" '[ "$unmounted" = 0 ]' "control: without the block the unmount is refused (the leak)"
    fi
    # let the stand-in children finish, then make sure nothing is left mounted
    sleep 5
    umount "$ISO" 2>/dev/null
}

# A sourced copy (". ./autoexec.sh" from a script named decrypt_autoexec.sh) must never exec over its parent.
sourced_case() {
    sh_bin=$1
    mount -t tmpfs -o size=1m xz-iso "$ISO" || exit 1
    make_script with
    printf '%s\n' '#!/bin/sh' '. ./autoexec.sh' > "$ISO/decrypt_autoexec.sh"
    sed -i 's/^exit 0$/true/' "$ISO/autoexec.sh"
    printf '%s\n' 'echo parent-continued > "$OUT/parent"' >> "$ISO/decrypt_autoexec.sh"
    rm -f "$WORK/out/parent"
    ( cd "$ISO" && OUT="$WORK/out" exec "$sh_bin" ./decrypt_autoexec.sh )
    check "$sh_bin/sourced" '[ -f "$WORK/out/parent" ]' "a sourcing parent is not replaced"
    sleep 5
    umount "$ISO" 2>/dev/null
}

SHELLS=""
for candidate in dash bash "busybox sh"; do
    set -- $candidate
    command -v "$1" >/dev/null 2>&1 || continue
    if [ "$1" = busybox ]; then
        printf '#!/bin/sh\nexec busybox sh "$@"\n' > "$WORK/busybox-sh"; chmod 755 "$WORK/busybox-sh"
        SHELLS="$SHELLS $WORK/busybox-sh"
    else
        SHELLS="$SHELLS $(command -v "$1")"
    fi
done
for s in $SHELLS; do
    run_case "$s" without
    run_case "$s" with
    sourced_case "$s"
done
[ "$FAILED" = 0 ] && echo "test_loader_ram: all passed" || echo "test_loader_ram: FAILED"
exit "$FAILED"
