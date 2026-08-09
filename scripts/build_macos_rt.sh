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
#
# Outputs toolchain/macos/{arm64,x64}/zanrt_{io,io_mt,sync,file,timer}.o. The objects are
# committed (they are our own code; *.o is gitignored, so `git add -f` them).
# -g0: DWARF in an object that is only ever statically linked is dead weight,
# and it embeds the build directory, so the bytes differ per machine and CI
# would recommit them on every run.
set -eu

ZIG=${1:-zig}
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
RT="$ROOT/src/runtime"

for pair in arm64:aarch64 x64:x86_64; do
    sub=${pair%%:*}
    arch=${pair#*:}
    out="$ROOT/toolchain/macos/$sub"
    mkdir -p "$out"
    # The .11.0 target suffix stamps LC_BUILD_VERSION minos 11.0, matching the
    # -platform_version zanc passes to ld64.lld; without it every link warns
    # that the object is newer than the target minimum.
    "$ZIG" cc -target "$arch-macos.11.0" -g0 -DZAN_IO_STACKLESS_ONLY -I "$RT" -O2 \
        -c "$RT/rt_io.c" -o "$out/zanrt_io.o"
    "$ZIG" cc -target "$arch-macos.11.0" -g0 -DZAN_IO_STACKLESS_ONLY -DZAN_CO_DRIVER \
        -I "$RT" -O2 -c "$RT/rt_io.c" -o "$out/zanrt_io_mt.o"
    "$ZIG" cc -target "$arch-macos.11.0" -g0 -std=c11 -I "$RT" -O2 \
        -c "$RT/rt_sync.c" -o "$out/zanrt_sync.o"
    # Its own command: this line used to be a dangling continuation of the
    # rt_sync one, so zanrt_file.o was silently never produced and every
    # cross-link of a program touching System.IO.File failed with
    # "cannot open .../zanrt_file.o".
    "$ZIG" cc -target "$arch-macos.11.0" -g0 -std=c11 -I "$RT" -O2 \
        -c "$RT/rt_file.c" -o "$out/zanrt_file.o"
    # Every emitted program calls zan_timer_* from its inline coroutine driver,
    # so a cross-link needs this object unconditionally (see main.c).
    "$ZIG" cc -target "$arch-macos.11.0" -g0 -std=c11 -I "$RT" -O2 \
        -c "$RT/rt_timer.c" -o "$out/zanrt_timer.o"
    echo "built toolchain/macos/$sub: zanrt_io.o zanrt_io_mt.o zanrt_sync.o zanrt_file.o zanrt_timer.o"
done
