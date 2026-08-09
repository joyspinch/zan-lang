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
# Outputs toolchain/{linux-musl,linux-arm64,linux-riscv64}/zanrt_{io,sync,file,timer}.o.
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
    "$ZIG" cc -target "$arch-linux-musl" -g0 -DZAN_IO_STACKLESS_ONLY \
        -I "$RT" -O2 -c "$RT/rt_io.c" -o "$out/zanrt_io.o"
    "$ZIG" cc -target "$arch-linux-musl" -g0 -std=c11 \
        -I "$RT" -O2 -c "$RT/rt_sync.c" -o "$out/zanrt_sync.o"
    # Its own command: this line used to be a dangling continuation of the
    # rt_sync one, so zanrt_file.o was silently never produced and every
    # cross-link of a program touching System.IO.File failed with
    # "cannot open .../zanrt_file.o".
    "$ZIG" cc -target "$arch-linux-musl" -g0 -std=c11 \
        -I "$RT" -O2 -c "$RT/rt_file.c" -o "$out/zanrt_file.o"
    # Every emitted program calls zan_timer_* from its inline coroutine driver,
    # so a cross-link needs this object unconditionally (see main.c) -- including
    # when the output is a .so, hence -fPIC (x86_64 otherwise emits absolute
    # 32-bit relocations that a shared object cannot take).
    "$ZIG" cc -target "$arch-linux-musl" -g0 -std=c11 -fPIC \
        -I "$RT" -O2 -c "$RT/rt_timer.c" -o "$out/zanrt_timer.o"
    echo "built toolchain/$sub: zanrt_io.o zanrt_sync.o zanrt_file.o zanrt_timer.o"
done
