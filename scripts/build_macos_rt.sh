#!/bin/sh
# Rebuild the Mach-O runtime objects that `zanc --target macos-arm64|macos-x64`
# links into socket-async and AtomicInt/SharedTable programs.
#
# Runs on any host: `zig cc` carries the Darwin libc headers, so no Apple SDK
# and no Mac are needed. Only libSystem symbols are imported (verify with
# scripts/check_macos_rt.py), so the existing toolchain/macos/libSystem.tbd
# stub is enough to link them.
#
#   scripts/build_macos_rt.sh [path-to-zig]
#   scripts/build_macos_rt.sh path-to-zig --ios-entry-only [output-dir]
#
# 契约：工程辅助自动化与脚本执行规范
# committed (they are our own code; *.o is gitignored, so `git add -f` them).
# -g0: DWARF in an object that is only ever statically linked is dead weight,
# and it embeds the build directory, so the bytes differ per machine and CI
# would recommit them on every run.
set -eu

ZIG=${1:-zig}
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
RT="$ROOT/src/runtime"
ACCEL="$ROOT/src/accel"
EMBED="$ROOT/src/embed"
DRV="$ROOT/src/drivers"

macos_targets='arm64:aarch64 x64:x86_64'
ios_out="$ROOT/toolchain/ios/arm64"
if [ "${2:-}" = '--ios-entry-only' ]; then
    macos_targets=
    ios_out=${3:-$ios_out}
fi

for pair in $macos_targets; do
    sub=${pair%%:*}
    arch=${pair#*:}
    out="$ROOT/toolchain/macos/$sub"
    mkdir -p "$out"
    # The .11.0 target suffix stamps LC_BUILD_VERSION minos 11.0, matching the
    # -platform_version zanc passes to ld64.lld; without it every link warns
    # that the object is newer than the target minimum.
    # -fPIC throughout: these objects are linked into dylibs as well as into
    # executables, and Mach-O shared output rejects the absolute relocations a
    # non-PIC object emits.
    "$ZIG" cc -target "$arch-macos.11.0" -g0 -DZAN_IO_STACKLESS_ONLY -fPIC -I "$RT" -O2 -ffunction-sections -fdata-sections \
        -c "$RT/rt_io.c" -o "$out/zanrt_io.o"
    "$ZIG" cc -target "$arch-macos.11.0" -g0 -DZAN_IO_STACKLESS_ONLY -DZAN_CO_DRIVER -fPIC \
        -I "$RT" -O2 -ffunction-sections -fdata-sections -c "$RT/rt_io.c" -o "$out/zanrt_io_mt.o"
    "$ZIG" cc -target "$arch-macos.11.0" -g0 -std=c11 -fPIC -I "$RT" -O2 -ffunction-sections -fdata-sections \
        -c "$RT/rt_sync.c" -o "$out/zanrt_sync.o"
    # File metadata / stream IO runtime (zan_file_*), linked by any program
    # that touches System.IO.File.
    "$ZIG" cc -target "$arch-macos.11.0" -g0 -std=c11 -fPIC -I "$RT" -O2 -ffunction-sections -fdata-sections \
        -c "$RT/rt_file.c" -o "$out/zanrt_file.o"
    # Every emitted program calls zan_timer_* from its inline coroutine driver,
    # so a cross-link needs this object unconditionally (see main.c).
    "$ZIG" cc -target "$arch-macos.11.0" -g0 -std=c11 -fPIC -I "$RT" -O2 -ffunction-sections -fdata-sections \
        -c "$RT/rt_timer.c" -o "$out/zanrt_timer.o"

    # Crash reporter: rt_io.o references zan__crash_install (B-ID106,
    # rt_crash.h split), so the cross link needs this object next to
    # zanrt_timer.o.
    "$ZIG" cc -target "$arch-macos.11.0" -g0 -std=c11 -fPIC -I "$RT" -O2 -ffunction-sections -fdata-sections \
        -c "$RT/rt_crash.c" -o "$out/zanrt_crash.o"
    # 128-bit Decimal runtime: programs that reach System.Numerics (and so
    # NativeMemory) reference __zan_decimal_*, and the cross link injects this
    # object for them (main.c injects <toolchain>/macos/<arch>/zanrt_decimal.o).
    # Without it a macOS cross build of a decimal-using program dies with
    # "bundled macOS runtime objects not found" -- and the object is only
    # needed when the program actually uses decimal, so a plain console
    # program never notices it is missing.
    "$ZIG" cc -target "$arch-macos.11.0" -g0 -std=c11 -fPIC -I "$RT" -O2 -ffunction-sections -fdata-sections \
        -c "$RT/rt_decimal.c" -o "$out/zanrt_decimal.o"
    # Independent feature members keep JSON/CPU users from loading crypto.
    set --
    for crypto_src in "$ACCEL"/hw/rt_hw_*.c; do
        crypto_name=${crypto_src##*/}
        crypto_obj="$out/zan_crypto_${crypto_name%.c}.o"
        "$ZIG" cc -target "$arch-macos.11.0" -g0 -std=c11 -fPIC -I "$RT" -I "$ACCEL" -O2 -ffunction-sections -fdata-sections \
            -c "$crypto_src" -o "$crypto_obj"
        set -- "$@" "$crypto_obj"
    done
    rm -f "$out/libzancrypto.a"
    "$ZIG" ar rcs "$out/libzancrypto.a" "$@"
    rm -f "$@"
    # GUI compatibility stubs (event seq/flag, font ascent, full surface presentation).
    "$ZIG" cc -target "$arch-macos.11.0" -g0 -std=c11 -fPIC -I "$RT" -I "$DRV" -O2 -ffunction-sections -fdata-sections \
        -c "$DRV/gui/gui_compat_mac.c" -o "$out/zanrt_gui.o"
    # Embedded-resource API + compressed-resource decoder: GUI programs pull
    # skins/icons through the auto-embed path, so irgen sets uses_embed_api /
    # uses_inflate and the cross link injects these two objects (same flags as
    # the ZAN_EMBED_OBJ / ZAN_INFLATE_OBJ recipes in CMakeLists.txt).
    "$ZIG" cc -target "$arch-macos.11.0" -g0 -std=c11 -fPIC -I "$RT" -I "$EMBED" -O2 -ffunction-sections -fdata-sections \
        -c "$EMBED/zan_embed_api.c" -o "$out/zan_embed_api.o"
    "$ZIG" cc -target "$arch-macos.11.0" -g0 -std=c11 -fPIC -O2 -ffunction-sections -fdata-sections \
        -DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_ZIP_APIS -DMINIZ_NO_STDIO \
        -DMINIZ_NO_TIME -DMINIZ_NO_ARCHIVE_WRITERS \
        -I "$ROOT/src/common" \
        -c "$EMBED/zan_inflate.c" -o "$out/zan_inflate.o"
    echo "built toolchain/macos/$sub: zanrt_io.o zanrt_io_mt.o zanrt_sync.o zanrt_file.o zanrt_timer.o zanrt_crash.o zanrt_decimal.o libzancrypto.a zanrt_gui.o zan_embed_api.o zan_inflate.o"
done

# iOS uses the complete UIKit runtime, not the macOS compatibility shim.
# Zig's ios target needs its bundled Darwin libc headers explicitly.
ZIG_BIN=$(command -v "$ZIG")
IOSHDR="$(dirname -- "$ZIG_BIN")/lib/libc/include/any-macos-any"
if [ ! -f "$IOSHDR/pthread.h" ]; then
    echo "iOS Darwin headers not found next to Zig: $IOSHDR" >&2
    exit 1
fi
mkdir -p "$ios_out"
"$ZIG" cc -target aarch64-ios.14.0 -I "$IOSHDR" -g0 -std=c11 -fPIC \
    -I "$RT" -O2 -c "$RT/rt_crash.c" -o "$ios_out/zanrt_crash.o"
"$ZIG" cc -target aarch64-ios.14.0 -I "$IOSHDR" -g0 -std=c11 -fPIC \
    -DZAN_GUI_IOS -DZAN_GUI_STATIC -I "$RT" -I "$DRV" -O2 -ffunction-sections -fdata-sections \
    -c "$DRV/gui/gui_runtime.c" -o "$ios_out/zanrt_gui.o"
echo "built $ios_out: zanrt_gui.o zanrt_crash.o"

