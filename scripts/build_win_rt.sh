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
"$CC" -O2 -g0 -std=c11 -I "$RT" -c "$RT/rt_file.c" -o "$OUT/zanrt_file.o"
# Every emitted program calls zan_timer_* from its inline coroutine driver, so a
# cross-link needs this object unconditionally (see main.c).
"$CC" -O2 -g0 -std=c11 -I "$RT" -c "$RT/rt_timer.c" -o "$OUT/zanrt_timer.o"
# Hardware-accelerated crypto, shipped as an ARCHIVE member and not as a bare
# object: a bare .o on the link line is loaded whole, so every program carried
# the entire SHA/AES/SM3/SM4 block even when it used none of it. As an archive
# member the linker pulls it only when the program really references a
# zan_hw_* symbol.
# Rebuild this together with zanrt_timer.o: both derive from the same
# rt_hw_accel.c, and mixing generations would define the same symbol twice
# (TASKS.md A493).
"$CC" -O2 -g0 -std=c11 -I "$RT" -c "$RT/rt_hw_accel.c" -o "$OUT/zan_crypto.o"
rm -f "$OUT/libzancrypto.a"
"${AR:-ar}" rcs "$OUT/libzancrypto.a" "$OUT/zan_crypto.o"
# Embedded-resource API + compressed-resource decoder: GUI programs pull
# skins/icons through the auto-embed path, so the cross link injects these two
# objects (same flags as the ZAN_INFLATE_OBJ recipe in CMakeLists.txt).
"$CC" -O2 -g0 -std=c11 -I "$RT" -c "$RT/zan_embed_api.c" -o "$OUT/zan_embed_api.o"
"$CC" -O2 -g0 -std=c11 \
    -DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_ZIP_APIS -DMINIZ_NO_STDIO \
    -DMINIZ_NO_TIME -DMINIZ_NO_ARCHIVE_WRITERS \
    -I "$ROOT/src/common" -c "$RT/zan_inflate.c" -o "$OUT/zan_inflate.o"
echo "built toolchain/$SUB: zanrt_io.o zanrt_io_mt.o zanrt_sync.o zanrt_file.o zanrt_timer.o libzancrypto.a zan_embed_api.o zan_inflate.o"
