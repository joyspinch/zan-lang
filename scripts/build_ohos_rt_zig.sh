#!/bin/sh
# Rebuild the OHOS runtime objects that `zanc --target ohos-arm64|ohos-x64`
# links into async/IO/file programs, without the OHOS NDK.
#
# Runs on any host: zig's bundled musl supplies the headers (OHOS userland is
# musl; same syscall ABI family), and the objects link clean against the
# committed OHOS musl libc.a subset in toolchain/ohos-<arch>/. zig 0.14 knows
# the -linux-ohos triples but ships no libc headers for them, so we compile
# against -linux-musl and add the OS define back by hand: -D__OHOS__ selects
# rt_sync.c's file-backed shm shim exactly as the NDK target predefines it.
# The NDK path in scripts/build_cross_rt.cmd stays authoritative when the SDK
# is available; keep the flag sets identical when either side changes.
#
#   scripts/build_ohos_rt_zig.sh [path-to-zig]
#
# Outputs toolchain/ohos-{arm64,x64}/zanrt_{io,sync,file,timer}.o,
# zan_embed_api.o, zan_inflate.o, zap_main.o. Committed after review (our own
# code; *.o is gitignored, so `git add -f` them).
# -g0: DWARF in an object that is only ever statically linked is dead weight,
# and it embeds the build directory, so the bytes differ per machine and CI
# would recommit them on every run.
# -fPIC throughout: the HAP shared-library link (zap_main.o head) rejects
# absolute relocations from non-PIC objects.
set -eu

ZIG=${1:-zig}
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
RT="$ROOT/src/runtime"

for pair in arm64:aarch64 x64:x86_64; do
    sub=${pair%%:*}
    arch=${pair#*:}
    out="$ROOT/toolchain/ohos-$sub"
    mkdir -p "$out"
    # arm64 timer: -mcpu=generic+sha2 turns on the TU-level crypto feature
    # macro so arm_neon.h declares the SHA-2 intrinsics (rt_hw_accel.c's
    # kernel) -- same role as the NDK recipe's -march=armv8-a+crypto. Runtime
    # dispatch is unaffected: the HWCAP_SHA2 gate plus KAT decide per CPU.
    march=""
    [ "$sub" = "arm64" ] && march="-mcpu=generic+sha2"
    "$ZIG" cc -target "$arch-linux-musl" -D__OHOS__ -g0 -DZAN_IO_STACKLESS_ONLY -fPIC \
        -I "$RT" -O2 -c "$RT/rt_io.c" -o "$out/zanrt_io.o"
    "$ZIG" cc -target "$arch-linux-musl" -D__OHOS__ -g0 -std=c11 -fPIC -I "$RT" -O2 \
        -c "$RT/rt_sync.c" -o "$out/zanrt_sync.o"
    "$ZIG" cc -target "$arch-linux-musl" -D__OHOS__ -g0 -std=c11 -fPIC -I "$RT" -O2 \
        -c "$RT/rt_file.c" -o "$out/zanrt_file.o"
    "$ZIG" cc -target "$arch-linux-musl" -D__OHOS__ -g0 -std=c11 -fPIC $march -I "$RT" -O2 \
        -c "$RT/rt_timer.c" -o "$out/zanrt_timer.o"
    "$ZIG" cc -target "$arch-linux-musl" -D__OHOS__ -g0 -std=c11 -fPIC -I "$RT" \
        -I "$ROOT/src/common" -O2 -c "$RT/zan_embed_api.c" -o "$out/zan_embed_api.o"
    "$ZIG" cc -target "$arch-linux-musl" -D__OHOS__ -g0 -std=c11 -fPIC \
        -DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_ZIP_APIS -DMINIZ_NO_STDIO -DMINIZ_NO_TIME \
        -I "$RT" -I "$ROOT/src/common" -O2 -c "$RT/zan_inflate.c" -o "$out/zan_inflate.o"
    # HAP XComponent shell adapter zanc puts at the head of every ohos -shared
    # link; the source is arch-independent and lives next to the x64 subset.
    "$ZIG" cc -target "$arch-linux-musl" -g0 -std=c11 -fPIC -O2 \
        -c "$ROOT/toolchain/ohos-x64/zap_main.c" -o "$out/zap_main.o"
    echo "built $out"
done
