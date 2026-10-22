#!/bin/sh
# Rebuild the PE runtime objects that `zanc --target win-arm64|win-x64` links
# into socket-async and AtomicInt/SharedTable programs when it cross-compiles.
#
# The objects zanc builds for itself are host-arch, so a cross-link needs the
# target's own copies; they sit next to the bundled mingw runtime, at
# toolchain/win-<arch>/zanrt_{io,io_mt,sync,file,decimal,timer}.o, and are staged
# beside zanc by CMake (build/win-<arch>/) and by scripts/publish_ide.ps1.
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

# Build into a staging directory and swap it in only when every object is
# ready. A rebuild that dies half-way (a missing MSYS2 package, a toolchain
# hiccup) must not leave a MIXED bundle behind: the CI job commits whatever
# sits in toolchain/, so "new zanrt_io.o next to old zanrt_timer.o" would be
# committed as if it were current -- and the objects' generation is exactly
# what the cross link silently depends on (TASKS.md A493). Same reason the
# crypto archive is rebuilt with the timer: mixing generations defines the
# same symbol twice.
STAGE="$OUT/.staging.$$"
trap 'rm -rf "$STAGE"' EXIT INT TERM
rm -rf "$STAGE"
mkdir -p "$STAGE"

"$CC" -O2 -g0 -DZAN_IO_STACKLESS_ONLY -I "$RT" -c "$RT/rt_io.c" \
    -o "$STAGE/zanrt_io.o"
"$CC" -O2 -g0 -DZAN_IO_STACKLESS_ONLY -DZAN_CO_DRIVER -I "$RT" -c "$RT/rt_io.c" \
    -o "$STAGE/zanrt_io_mt.o"
"$CC" -O2 -g0 -std=c11 -I "$RT" -c "$RT/rt_sync.c" -o "$STAGE/zanrt_sync.o"
"$CC" -O2 -g0 -std=c11 -I "$RT" -c "$RT/rt_file.c" -o "$STAGE/zanrt_file.o"
# 128-bit Decimal runtime: programs that reach System.Numerics reference
# __zan_decimal_* and the cross link injects this object for them.
"$CC" -O2 -g0 -std=c11 -I "$RT" -c "$RT/rt_decimal.c" -o "$STAGE/zanrt_decimal.o"
# Every emitted program calls zan_timer_* from its inline coroutine driver, so a
# cross-link needs this object unconditionally (see main.c).
"$CC" -O2 -g0 -std=c11 -I "$RT" -c "$RT/rt_timer.c" -o "$STAGE/zanrt_timer.o"
# Hardware-accelerated crypto, shipped as an ARCHIVE member and not as a bare
# object: a bare .o on the link line is loaded whole, so every program carried
# the entire SHA/AES/SM3/SM4 block even when it used none of it. As an archive
# member the linker pulls it only when the program really references a
# zan_hw_* symbol.
# Rebuild this together with zanrt_timer.o: both derive from the same
# rt_hw_accel.c, and mixing generations would define the same symbol twice
# (TASKS.md A493).
"$CC" -O2 -g0 -std=c11 -I "$RT" -c "$RT/rt_hw_accel.c" -o "$STAGE/zan_crypto.o"
rm -f "$STAGE/libzancrypto.a"
"${AR:-ar}" rcs "$STAGE/libzancrypto.a" "$STAGE/zan_crypto.o"
# Embedded-resource API + compressed-resource decoder: GUI programs pull
# skins/icons through the auto-embed path, so the cross link injects these two
# objects (same flags as the ZAN_INFLATE_OBJ recipe in CMakeLists.txt).
"$CC" -O2 -g0 -std=c11 -I "$RT" -c "$RT/zan_embed_api.c" -o "$STAGE/zan_embed_api.o"
"$CC" -O2 -g0 -std=c11 \
    -DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_ZIP_APIS -DMINIZ_NO_STDIO \
    -DMINIZ_NO_TIME -DMINIZ_NO_ARCHIVE_WRITERS \
    -I "$ROOT/src/common" -c "$RT/zan_inflate.c" -o "$STAGE/zan_inflate.o"

# Self-check (when nm is around): the size invariant of A493 -- the crypto
# block must live in the ARCHIVE, not in the timer object. A timer that
# carries zan_hw_* again means rt_timer.c text-includes rt_hw_accel.c once
# more, and every cross product grows by ~53 KB while the build stays green.
if command -v nm >/dev/null 2>&1; then
    if nm --defined-only "$STAGE/zanrt_timer.o" 2>/dev/null | grep -q 'zan_hw_'; then
        echo "build_win_rt.sh: zanrt_timer.o still carries the crypto block" >&2
        echo "  (rt_timer.c must not text-include rt_hw_accel.c -- see TASKS.md A493)" >&2
        exit 1
    fi
    if ! nm --defined-only "$STAGE/libzancrypto.a" 2>/dev/null | grep -q 'zan_hw_'; then
        echo "build_win_rt.sh: libzancrypto.a carries no crypto symbol" >&2
        exit 1
    fi
fi

# Swap in: every artifact is ready, so the bundle changes generation in one
# step.
for f in zanrt_io.o zanrt_io_mt.o zanrt_sync.o zanrt_file.o zanrt_decimal.o \
         zanrt_timer.o zan_embed_api.o zan_inflate.o libzancrypto.a zan_crypto.o; do
    rm -f "$OUT/$f"
    mv "$STAGE/$f" "$OUT/$f"
done
echo "built toolchain/$SUB: zanrt_io.o zanrt_io_mt.o zanrt_sync.o zanrt_file.o zanrt_decimal.o zanrt_timer.o libzancrypto.a zan_embed_api.o zan_inflate.o"
