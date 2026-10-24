#!/bin/sh
# Rebuild the static-musl runtime objects that `zanc --target linux-x64 |
# linux-musl | linux-arm64 | riscv64` links into socket-async and
# AtomicInt/SharedTable programs.
#
# Runs on any host: `zig cc` carries the musl headers and libc, so no Linux
# machine and no cross toolchain are needed -- the same reason
# scripts/build_macos_rt.sh needs no Mac.
#
#   scripts/build_linux_rt.sh [path-to-zig]
#
# 契约：工程辅助自动化与脚本执行规范
# The objects are committed (they are our own code; *.o is gitignored, so
# `git add -f` them). -g0 keeps them the size the previously committed ones
# were: DWARF in an object that is only ever statically linked is dead weight.
set -eu

ZIG=${1:-zig}
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
RT="$ROOT/src/runtime"

for pair in linux-musl:x86_64 linux-arm64:aarch64 linux-riscv64:riscv64; do
    sub=${pair%%:*}
    arch=${pair#*:}
    out="$ROOT/toolchain/$sub"
    mkdir -p "$out"
    # -fPIC throughout: the same objects are linked both into static
    # executables (where ld.lld relaxes the PIC sequences) and into shared
    # libraries, which reject the absolute and static-TLS relocations a
    # non-PIC object emits.
    "$ZIG" cc -target "$arch-linux-musl" -g0 -DZAN_IO_STACKLESS_ONLY -fPIC \
        -I "$RT" -O2 -ffunction-sections -fdata-sections -c "$RT/rt_io.c" -o "$out/zanrt_io.o"
    "$ZIG" cc -target "$arch-linux-musl" -g0 -DZAN_IO_STACKLESS_ONLY -DZAN_CO_DRIVER -fPIC \
        -I "$RT" -O2 -ffunction-sections -fdata-sections -c "$RT/rt_io.c" -o "$out/zanrt_io_mt.o"
    "$ZIG" cc -target "$arch-linux-musl" -g0 -std=c11 -fPIC \
        -I "$RT" -O2 -ffunction-sections -fdata-sections -c "$RT/rt_sync.c" -o "$out/zanrt_sync.o"
    # File metadata / stream IO runtime (zan_file_*), linked by any program
    # that touches System.IO.File.
    "$ZIG" cc -target "$arch-linux-musl" -g0 -std=c11 -fPIC \
        -I "$RT" -O2 -ffunction-sections -fdata-sections -c "$RT/rt_file.c" -o "$out/zanrt_file.o"
    # 128-bit Decimal runtime: anything that reaches System.Numerics (and
    # therefore NativeMemory) references it, so the cross link injects it for
    # those programs -- without it the link dies on a missing object.
    "$ZIG" cc -target "$arch-linux-musl" -g0 -std=c11 -fPIC \
        -I "$RT" -O2 -ffunction-sections -fdata-sections -c "$RT/rt_decimal.c" -o "$out/zanrt_decimal.o"
    # Every emitted program calls zan_timer_* from its inline coroutine driver,
    # so a cross-link needs this object unconditionally (see main.c).
    "$ZIG" cc -target "$arch-linux-musl" -g0 -std=c11 -fPIC \
        -I "$RT" -O2 -ffunction-sections -fdata-sections -c "$RT/rt_timer.c" -o "$out/zanrt_timer.o"
    # Hardware-accelerated crypto, shipped as an ARCHIVE member and not as a
    # bare object: a bare .o on the link line is loaded whole, so every
    # program carried the entire SHA/AES/SM3/SM4 block even when it used none
    # of it. As an archive member the linker pulls it only when the program
    # really references a zan_hw_* symbol.
    # Rebuild this together with zanrt_timer.o: both derive from the same
    # rt_hw_accel.c, and mixing generations would define the same symbol
    # twice (TASKS.md A493).
    "$ZIG" cc -target "$arch-linux-musl" -g0 -std=c11 -fPIC \
        -I "$RT" -O2 -ffunction-sections -fdata-sections -c "$RT/rt_hw_accel.c" -o "$out/zan_crypto.o"
    rm -f "$out/libzancrypto.a"
    "$ZIG" ar rcs "$out/libzancrypto.a" "$out/zan_crypto.o"
    # Embedded-resource API + compressed-resource decoder: GUI programs pull
    # skins/icons through the auto-embed path, so the cross link injects these
    # two objects (same flags as the ZAN_INFLATE_OBJ recipe in CMakeLists.txt).
    "$ZIG" cc -target "$arch-linux-musl" -g0 -std=c11 -fPIC -I "$RT" -O2 -ffunction-sections -fdata-sections \
        -c "$RT/zan_embed_api.o" -o "$out/zan_embed_api.o"
    "$ZIG" cc -target "$arch-linux-musl" -g0 -std=c11 -fPIC -O2 -ffunction-sections -fdata-sections \
        -DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_ZIP_APIS -DMINIZ_NO_STDIO \
        -DMINIZ_NO_TIME -DMINIZ_NO_ARCHIVE_WRITERS \
        -I "$ROOT/src/common" \
        -c "$RT/zan_inflate.c" -o "$out/zan_inflate.o"
    # The small-object allocator's memory object: only linux-musl ships it (the
    # other arches use the platform allocator). It was missing here while the
    # stale-check table listed it -- the recipe and the check disagreed about
    # what exists, so the object could never be refreshed and the freshness
    # gate stayed red on it forever.
    if [ "$sub" = linux-musl ]; then
        "$ZIG" cc -target "$arch-linux-musl" -g0 -std=c11 -fPIC \
            -I "$RT" -O2 -ffunction-sections -fdata-sections -c "$RT/rt_mem.c" -o "$out/zanrt_mem.o"
    fi
    echo "built toolchain/$sub: zanrt_io.o zanrt_io_mt.o zanrt_sync.o zanrt_file.o zanrt_decimal.o zanrt_timer.o libzancrypto.a zan_embed_api.o zan_inflate.o"
done
