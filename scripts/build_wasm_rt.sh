#!/bin/sh
# wasm32 (WASI) console runtime objects -- the CI-owned subset of
# build_cross_rt.cmd's wasm32 block (toolchain.yml wasm32-objects job).
#
# Flag source of truth: scripts/check_toolchain_stale.py rebuild_cmd()'s
# wasm32 branch (byte-verified against the committed objects on a zig
# 0.15 machine). Keep all three -- this script, the .cmd block and the
# checker table -- in sync when touching flags.
#
# Deliberately NOT built here (same policy as the native feature drivers,
# which stay report-only in the staleness table):
#   - zanrt_gui.o / zanrt_image.o / zanrt_audio.o / zanrt_game.o: the .cmd
#     builds them; the gui one additionally wants an external freetype
#     header checkout plus the prebuilt libfreetype.a, so the pair stays
#     manual (see build_cross_rt.cmd lines ~125-154).
#   - zanrt_ehtag.o: needs mozbuild clang's wasm EH backend; zig's clang
#     fails the same compile (recipe in build_cross_rt.cmd).
#   - crt1.o / libc.a / libm.a / libzigc.a / libclang_rt.builtins: wasi
#     sysroot subsets, no repo source drives them.
#
# Single-threaded target: no rt_io / rt_sync -- the wasm link rejects
# programs that reference them before any object would be needed (main.c's
# wasm_obj_refs_any gates).
set -e

ZIG="$1"
if [ -z "$ZIG" ]; then
    echo "usage: $0 <path-to-zig>" >&2
    exit 2
fi

RT=src/runtime
OUT=toolchain/wasm32
mkdir -p "$OUT"

build() {
    # build <output-name> <source.c> [extra flags...]
    name="$1"; src="$2"; shift 2
    echo "  $name"
    "$ZIG" cc -target wasm32-wasi -g0 "$@" -c "$RT/$src" -o "$OUT/$name"
}

# file/timer/wasm: -std=c11 (the .cmd's wasm block; a gnu dialect shifts
# every byte vs the committed objects).
build zanrt_wasm.o    rt_wasm.c    -std=c11  -I "$RT" -O2
build zanrt_file.o    rt_file.c    -std=c11  -I "$RT" -O2
build zanrt_decimal.o rt_decimal.c -std=c11  -I "$RT" -O2
build zanrt_timer.o   rt_timer.c   -std=c11  -I "$RT" -O2
# Crypto as an archive member -- a plain zanrt_crypto.o would pull the
# whole archive into every wasm link; the member form lets the linker
# drop it for programs without native crypto references (same reason the
# linux/macos blocks use an archive).
build zan_crypto.o    rt_hw_accel.c -std=c11 -I "$RT" -O2
rm -f "$OUT/libzancrypto.a"
"$ZIG" ar rcs "$OUT/libzancrypto.a" "$OUT/zan_crypto.o"
# Compressed embedded icons/skins need the same decoder as native targets:
# no -I "$RT", miniz trim defines + NO_ARCHIVE_WRITERS, -I src/common.
"$ZIG" cc -target wasm32-wasi -g0 -std=c11 -O2 \
    -DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_ZIP_APIS -DMINIZ_NO_STDIO \
    -DMINIZ_NO_TIME -DMINIZ_NO_ARCHIVE_WRITERS \
    -I src/common -c "$RT/zan_inflate.c" -o "$OUT/zan_inflate.o"
# Single-threaded sync equivalents (threads run bodies synchronously,
# atomics are plain cells): gnu11 exposes what the wasm shells need.
build zanrt_syncw.o   rt_sync_wasm.c -std=gnu11 -I "$RT" -O2

echo "built $OUT (console bundle)"
