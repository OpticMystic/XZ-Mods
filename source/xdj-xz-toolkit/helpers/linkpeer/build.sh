#!/usr/bin/env bash
# Compiles xz-link-peer and xz-link-probe. Called by build.py inside WSL Ubuntu, after build.py has checked
# the pinned Link commit, the asio archive and the Zig archive.
#   bash build.sh <zig-binary>
# Outputs (relative to this folder):
#   out/host/                 g++ build for the lab (not for the unit)
#   out/arm-musl/             zig c++ -target arm-linux-musleabi, fully static, stripped      (unit candidate 1)
#   out/arm-glibc213/         zig c++ -target arm-linux-gnueabi.2.13, dynamic, stripped     (unit candidate 2)
#   out/arm-musl-qemushim/    test-only: static musl build with the qemu setsockopt shim; never ship
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ZIG="$1"
OUT="$HERE/out"
LINK="$HERE/third_party/link"
SRC="$HERE/src"
export ZIG_GLOBAL_CACHE_DIR="${ZIG_GLOBAL_CACHE_DIR:-$HOME/xz-linkpeer/zig-cache}"
export ZIG_LOCAL_CACHE_DIR="$ZIG_GLOBAL_CACHE_DIR"

ASIO_NS=link_asio_1_38_2
DEFS=(-DLINK_PLATFORM_LINUX=1 -DLINK_PLATFORM_UNIX=1 -DASIO_STANDALONE=1 -DASIO_NO_TYPEID=1
      -DASIO_VERSION_NAMESPACE=$ASIO_NS)
INCS=(-I"$SRC" -I"$LINK/include" -isystem "$LINK/modules/asio-standalone/include")
CXXFLAGS=(-std=c++17 -O2 -Wall -Wextra -Werror -Wno-multichar -ffunction-sections -fdata-sections)
CFLAGS=(-std=c11 -O2 -Wall -Wextra -Werror)
ARM=(-mcpu=cortex_a9 -mfloat-abi=softfp -mfpu=vfpv3)
PEER="$SRC/xz_link_peer.cpp"
PROBE="$SRC/xz_link_probe.c"

mkdir -p "$OUT/host" "$OUT/arm-musl" "$OUT/arm-glibc213" "$OUT/arm-musl-qemushim"

echo "== host (g++)"
g++ "${CXXFLAGS[@]}" "${DEFS[@]}" "${INCS[@]}" "$PEER" -o "$OUT/host/xz-link-peer" -pthread -latomic
gcc "${CFLAGS[@]}" -I"$SRC" "$PROBE" -o "$OUT/host/xz-link-probe" -lm

echo "== arm-musl (static)"
"$ZIG" c++ -target arm-linux-musleabi "${ARM[@]}" "${CXXFLAGS[@]}" "${DEFS[@]}" "${INCS[@]}" "$PEER" \
  -static -Wl,--gc-sections -s -o "$OUT/arm-musl/xz-link-peer"
"$ZIG" cc -target arm-linux-musleabi "${ARM[@]}" "${CFLAGS[@]}" -I"$SRC" "$PROBE" -static -s -o "$OUT/arm-musl/xz-link-probe"

echo "== arm-glibc213 (dynamic)"
# asio 1.38 calls aligned_alloc, which glibc has only from 2.16.
"$ZIG" c++ -target arm-linux-gnueabi.2.13 "${ARM[@]}" "${CXXFLAGS[@]}" "${DEFS[@]}" -DASIO_DISABLE_STD_ALIGNED_ALLOC=1 \
  "${INCS[@]}" "$PEER" -Wl,--gc-sections -lrt -lpthread -s -o "$OUT/arm-glibc213/xz-link-peer"

echo "== arm-musl-qemushim (test only)"
"$ZIG" cc -target arm-linux-musleabi "${ARM[@]}" "${CFLAGS[@]}" -c "$SRC/qemu_setsockopt_shim.c" \
  -o "$OUT/arm-musl-qemushim/shim.o"
"$ZIG" c++ -target arm-linux-musleabi "${ARM[@]}" "${CXXFLAGS[@]}" "${DEFS[@]}" "${INCS[@]}" "$PEER" \
  "$OUT/arm-musl-qemushim/shim.o" -static -Wl,--gc-sections -s -o "$OUT/arm-musl-qemushim/xz-link-peer"
echo "== done"
