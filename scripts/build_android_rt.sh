#!/bin/sh
# Rebuild the bionic (Android) runtime objects that `zanc --target android-x64
# | android-arm64` links into async/IO/file/decimal programs.
#
#   scripts/build_android_rt.sh [path-to-ndk]
#
# zig has no bionic target, so this one needs the real Android NDK: its clang
# plus its sysroot (bionic headers). Set ANDROID_NDK to the NDK root (the
# directory that contains toolchains/), or pass it as the first argument; the
# script also looks in the usual SDK locations.
#
# API 28 is the floor: rt_sync.c uses glob(), which bionic added in 28.
# bionic has no shm_open -- the __ANDROID__ shim inside rt_sync.c backs shared
# tables with files under $ZAN_SHM_DIR (default /data/local/tmp).
#
# What it produces (toolchain/android-<arch>/):
#   - the source-derived objects: zanrt_io.o, zanrt_sync.o, zanrt_file.o,
#     zanrt_decimal.o, zanrt_timer.o, libzancrypto.a, zan_embed_api.o,
#     zan_inflate.o
#   - the NDK sysroot subset the cross link needs: crtbegin_static.o,
#     crtend_android.o, libc.a, libm.a, libdl.a, the dynamic libc/libm/libdl/
#     liblog/libandroid/libEGL/libGLESv2/libaaudio .so files and
#     libclang_rt.builtins.a
#
# The NDK's libc.a is compressed with zstd (llvm-objcopy --decompress-debug-
# sections) so that lld can link it; the script decompresses it the same way
# when the tool is available.
#
# 契约：工程辅助自动化与脚本执行规范
# The objects are committed (they are our own code; *.o is gitignored, so
# `git add -f` them). -g0 keeps them byte-stable across machines.
#
# NOT VERIFIED where it was written: the authoring machine had no NDK, so the
# first run belongs to a machine (or CI job) that does. Every step below is
# guarded -- a missing NDK file is reported, not silently skipped.
set -eu

NDK=${1:-${ANDROID_NDK:-}}
if [ -z "$NDK" ]; then
    for cand in \
        "$HOME/AppData/Local/Android/Sdk/ndk"/* \
        "$HOME/Library/Android/sdk/ndk"/* \
        "$HOME/Android/Sdk/ndk"/* \
        /usr/local/lib/android/sdk/ndk/* \
        /opt/android-sdk/ndk/*
    do
        [ -d "$cand" ] && NDK=$cand
    done
fi
if [ -z "$NDK" ] || [ ! -d "$NDK" ]; then
    echo "android NDK not found: pass the NDK root as the first argument" >&2
    echo "  (it is the directory that contains toolchains/)" >&2
    exit 1
fi

# The NDK ships one prebuilt toolchain per host OS; pick the one we run on.
case "$(uname -s)" in
    Linux)  HOST=linux-x86_64 ;;
    Darwin) HOST=darwin-x86_64 ;;
    *)      HOST=windows-x86_64 ;;
esac
PRE="$NDK/toolchains/llvm/prebuilt/$HOST"
[ -d "$PRE" ] || { echo "NDK prebuilt tree not found: $PRE" >&2; exit 1; }
CC="$PRE/bin/clang"
SYSROOT="$PRE/sysroot"
[ -x "$CC" ] || { echo "NDK clang not found: $CC" >&2; exit 1; }
AR="$PRE/bin/llvm-ar"
[ -x "$AR" ] || AR="$PRE/bin/llvm-ar.exe" || true

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
RT="$ROOT/src/runtime"

# The compiler-rt builtins live in a versioned directory; find the one that
# matches this NDK instead of hardcoding a version.
CLANG_VER=$(ls "$PRE/lib/clang" 2>/dev/null | sort -V | tail -1)
BUILTINS_DIR="$PRE/lib/clang/$CLANG_VER/lib/linux"

for pair in android-x64:x86_64 android-arm64:aarch64; do
    sub=${pair%%:*}
    arch=${pair#*:}
    triple="$arch-linux-android28"
    out="$ROOT/toolchain/$sub"
    mkdir -p "$out"

    "$CC" --sysroot="$SYSROOT" -target "$triple" -g0 -DZAN_IO_STACKLESS_ONLY \
        -fPIC -I "$RT" -O2 -ffunction-sections -fdata-sections -c "$RT/rt_io.c" -o "$out/zanrt_io.o"
    "$CC" --sysroot="$SYSROOT" -target "$triple" -g0 -std=c11 -fPIC \
        -I "$RT" -O2 -ffunction-sections -fdata-sections -c "$RT/rt_sync.c" -o "$out/zanrt_sync.o"
    "$CC" --sysroot="$SYSROOT" -target "$triple" -g0 -std=c11 -fPIC \
        -I "$RT" -O2 -ffunction-sections -fdata-sections -c "$RT/rt_file.c" -o "$out/zanrt_file.o"
    # 128-bit Decimal runtime: programs that reach System.Numerics reference
    # __zan_decimal_*, and the cross link injects this object for them.
    "$CC" --sysroot="$SYSROOT" -target "$triple" -g0 -std=c11 -fPIC \
        -I "$RT" -O2 -ffunction-sections -fdata-sections -c "$RT/rt_decimal.c" -o "$out/zanrt_decimal.o"
    "$CC" --sysroot="$SYSROOT" -target "$triple" -g0 -std=c11 -fPIC \
        -I "$RT" -O2 -ffunction-sections -fdata-sections -c "$RT/rt_timer.c" -o "$out/zanrt_timer.o"
    # Crypto as an archive member -- see scripts/build_linux_rt.sh for why a
    # bare .o on the link line is wrong here.
    "$CC" --sysroot="$SYSROOT" -target "$triple" -g0 -std=c11 -fPIC \
        -I "$RT" -O2 -ffunction-sections -fdata-sections -c "$RT/rt_hw_accel.c" -o "$out/zan_crypto.o"
    rm -f "$out/libzancrypto.a"
    "$AR" rcs "$out/libzancrypto.a" "$out/zan_crypto.o"
    "$CC" --sysroot="$SYSROOT" -target "$triple" -g0 -std=c11 -fPIC \
        -I "$RT" -I "$ROOT/src/common" -O2 -ffunction-sections -fdata-sections -c "$RT/zan_embed_api.c" \
        -o "$out/zan_embed_api.o"
    "$CC" --sysroot="$SYSROOT" -target "$triple" -g0 -std=c11 -fPIC \
        -DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_ZIP_APIS -DMINIZ_NO_STDIO \
        -DMINIZ_NO_TIME -DMINIZ_NO_ARCHIVE_WRITERS \
        -I "$RT" -I "$ROOT/src/common" -O2 -ffunction-sections -fdata-sections -c "$RT/zan_inflate.c" \
        -o "$out/zan_inflate.o"

    # ---- the NDK sysroot subset the cross link needs ----------------------
    LIBDIR="$SYSROOT/usr/lib/$triple"
    for f in crtbegin_static.o crtend_android.o libc.a libm.a libdl.a \
             libc.so libm.so libdl.so liblog.so libandroid.so libEGL.so \
             libGLESv2.so libaaudio.so
    do
        if [ -f "$LIBDIR/$f" ]; then
            cp -f "$LIBDIR/$f" "$out/$f"
        else
            echo "note: $LIBDIR/$f not in this NDK -- the $sub cross link may miss it" >&2
        fi
    done
    # libc.a in recent NDKs is zstd-compressed; lld cannot read it as is.
    if [ -f "$out/libc.a" ] && command -v llvm-objcopy >/dev/null 2>&1; then
        llvm-objcopy --decompress-debug-sections "$out/libc.a" || true
    fi
    # compiler-rt builtins (the NDK names them per arch; the link expects one
    # fixed name).
    for f in "$BUILTINS_DIR"/libclang_rt.builtins*"$arch"-android.a \
             "$BUILTINS_DIR"/libclang_rt.builtins.a
    do
        if [ -f "$f" ]; then
            cp -f "$f" "$out/libclang_rt.builtins.a"
            break
        fi
    done
    [ -f "$out/libclang_rt.builtins.a" ] || \
        echo "note: libclang_rt.builtins.a not found under $BUILTINS_DIR" >&2

    echo "built toolchain/$sub: zanrt_io.o zanrt_sync.o zanrt_file.o zanrt_decimal.o zanrt_timer.o libzancrypto.a zan_embed_api.o zan_inflate.o + NDK sysroot subset"
done
