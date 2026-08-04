#!/bin/sh
# Rebuild the PE runtime objects that `zanc --target win-arm64|win-x64` links
# into socket-async and AtomicInt/SharedTable programs when it cross-compiles.
#
# The objects zanc builds for itself are host-arch, so a cross-link needs the
# target's own copies; they sit next to the bundled mingw runtime, at
# toolchain/win-<arch>/zanrt_{io,io_mt,sync,timer}.o, and are staged beside zanc by
# CMake (build/win-<arch>/) and by scripts/publish_ide.ps1.
#
#   scripts/build_win_rt.sh <win-arm64|win-x64> [cc]
#
# Must run with a compiler that targets that arch's mingw ABI -- in CI this is
# the MSYS2 CLANGARM64 / MINGW64 clang of the matching drivers.yml job, which
# carries the mingw-w64 headers and matches the *-w64-windows-gnu objects zanc
# emits. -g0 keeps the committed objects byte-stable across machines.
set -eu

SUB=${1:?usage: build_win_rt.sh <win-arm64|win-x64> [cc]}
CC=${2:-cc}
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
RT="$ROOT/src/runtime"
OUT="$ROOT/toolchain/$SUB"

mkdir -p "$OUT"
"$CC" -O2 -g0 -DZAN_IO_STACKLESS_ONLY -I "$RT" -c "$RT/rt_io.c" \
    -o "$OUT/zanrt_io.o"
"$CC" -O2 -g0 -DZAN_IO_STACKLESS_ONLY -DZAN_CO_DRIVER -I "$RT" -c "$RT/rt_io.c" \
    -o "$OUT/zanrt_io_mt.o"
"$CC" -O2 -g0 -std=c11 -I "$RT" -c "$RT/rt_sync.c" -o "$OUT/zanrt_sync.o"
# Every emitted program calls zan_timer_* from its inline coroutine driver, so a
# cross-link needs this object unconditionally (see main.c).
"$CC" -O2 -g0 -std=c11 -I "$RT" -c "$RT/rt_timer.c" -o "$OUT/zanrt_timer.o"
echo "built toolchain/$SUB: zanrt_io.o zanrt_io_mt.o zanrt_sync.o zanrt_timer.o"
