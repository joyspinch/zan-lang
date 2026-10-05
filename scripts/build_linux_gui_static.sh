#!/usr/bin/env bash
# Build the self-contained static Linux GUI driver archive
#   packages/Zan.Gui/src/Gui/drivers/<target>/static/libzan_gui.a
# that zanc links into --target linux-{x64,arm64} GUI builds.
#
# The archive is: gui_runtime.c compiled as a single translation unit
# with -DZAN_GUI_STATIC -DZAN_GUI_FREETYPE, merged together with the
# static system X11 archives (libX11.a, libXau.a, libxcb.a) into a single .a archive.
#
# Usage: scripts/build_linux_gui_static.sh [linux-x64|linux-arm64]
set -euo pipefail

TARGET="${1:-linux-x64}"
case "$TARGET" in
    linux-x64|x64)
        TARGET="linux-x64"
        TRIPLET="x86_64-linux-gnu"
        ARCH_SUFFIX="x64"
        ;;
    linux-arm64|arm64)
        TARGET="linux-arm64"
        TRIPLET="aarch64-linux-gnu"
        ARCH_SUFFIX="arm64"
        ;;
    *)
        echo "usage: $0 [linux-x64|linux-arm64]" >&2
        exit 1
        ;;
esac

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/packages/Zan.Gui/src/Gui/drivers/$TARGET/static"
WORK="$ROOT/build/linux_gui_drivers_$ARCH_SUFFIX"
mkdir -p "$DEST" "$WORK"

echo "== [1/3] Compiling gui_runtime.c for $TARGET =="
CC="${CC:-gcc}"
AR="${AR:-ar}"

# FreeType include paths (system pkg-config or fallback)
FT_CFLAGS="$(pkg-config --cflags freetype2 2>/dev/null || echo "-I/usr/include/freetype2")"

# -DNDEBUG drops the vendored libs' assert() strings (their __FILE__ would
# leak build-machine paths into every static publish); -ffunction-sections
# pairs with the publish link's --gc-sections, which on ELF really prunes.
# -std=gnu11: the TU pulls in rt_crash.h + gui_runtime_x11.c, which need
# siginfo_t/SA_SIGINFO/CLOCK_MONOTONIC/strdup -- strict -std=c11 hides those
# declarations and the compile fails. -U_FORTIFY_SOURCE: distro gcc enables
# _FORTIFY_SOURCE by default at -O, emitting __memcpy_chk/__snprintf_chk
# references that the musl sysroot zanc links every linux publish with cannot
# resolve. -DZAN_NO_EXECINFO: same for backtrace() -- rt_crash.h falls back to
# its dladdr/frame-walk path (what musl builds use anyway), keeping the
# archive linkable on musl.
$CC -O2 -g0 -DNDEBUG -ffunction-sections -fdata-sections -fPIC -std=gnu11 \
    -U_FORTIFY_SOURCE -DZAN_NO_EXECINFO \
    -DZAN_GUI_STATIC -DZAN_GUI_FREETYPE \
    $FT_CFLAGS \
    -I"$ROOT/src/runtime" \
    -c "$ROOT/src/runtime/gui_runtime.c" -o "$WORK/zan_gui_${ARCH_SUFFIX}.o"

echo "== [1b] Compiling fortify/link shims =="
# -U_FORTIFY_SOURCE is mandatory here: with it, this file's own memcpy/... would
# be rewritten to the very __*_chk symbols it exists to provide.
$CC -O2 -g0 -ffunction-sections -fdata-sections -fPIC -std=gnu11 \
    -U_FORTIFY_SOURCE \
    -c "$ROOT/src/runtime/zan_fortify_compat.c" -o "$WORK/zan_fortify_compat.o"

echo "== [2/3] Extracting static X11 system objects =="
X_OBJS_DIR="$WORK/x_objs"
rm -rf "$X_OBJS_DIR"
mkdir -p "$X_OBJS_DIR"

# Locate static archives for X11, Xau, xcb
find_static_lib() {
    local name="$1"
    for d in "/tmp/arm64_x11/root/usr/lib/$TRIPLET" "/usr/lib/$TRIPLET" "/usr/lib" "/usr/local/lib"; do
        if [ -f "$d/$name" ]; then
            echo "$d/$name"
            return 0
        fi
    done
    return 1
}

# Extract into one subdirectory per archive: member names collide across them
# (libX11.a and libXdmcp.a both carry a Flush.o -- XFlush vs XdmcpFlush), and a
# shared directory silently overwrites one library's object with the other's.
i=0
for lib in libX11.a libXau.a libxcb.a libXdmcp.a; do
    LIBPATH="$(find_static_lib "$lib" || true)"
    if [ -n "$LIBPATH" ]; then
        i=$((i+1))
        LIBDIR="$X_OBJS_DIR/$i"
        mkdir -p "$LIBDIR"
        echo "Extracting $LIBPATH..."
        (cd "$LIBDIR" && $AR x "$LIBPATH")
    else
        echo "Notice: $lib not found under /usr/lib/$TRIPLET, skipping extraction"
    fi
done

echo "== [3/3] Creating merged static archive libzan_gui.a =="
rm -f "$DEST/libzan_gui.a"
$AR rcs "$DEST/libzan_gui.a" "$WORK/zan_gui_${ARCH_SUFFIX}.o" "$WORK/zan_fortify_compat.o" "$X_OBJS_DIR"/*/*.o 2>/dev/null || \
$AR rcs "$DEST/libzan_gui.a" "$WORK/zan_gui_${ARCH_SUFFIX}.o" "$WORK/zan_fortify_compat.o"

echo "Successfully generated $DEST/libzan_gui.a"
