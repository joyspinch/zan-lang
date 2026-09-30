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
$CC -O2 -g0 -DNDEBUG -ffunction-sections -fdata-sections -fPIC -std=c11 \
    -DZAN_GUI_STATIC -DZAN_GUI_FREETYPE \
    $FT_CFLAGS \
    -I"$ROOT/src/runtime" \
    -c "$ROOT/src/runtime/gui_runtime.c" -o "$WORK/zan_gui_${ARCH_SUFFIX}.o"

echo "== [2/3] Extracting static X11 system objects =="
X_OBJS_DIR="$WORK/x_objs"
rm -rf "$X_OBJS_DIR"
mkdir -p "$X_OBJS_DIR"

# Locate static archives for X11, Xau, xcb
find_static_lib() {
    local name="$1"
    for d in "/usr/lib/$TRIPLET" "/usr/lib" "/usr/local/lib"; do
        if [ -f "$d/$name" ]; then
            echo "$d/$name"
            return 0
        fi
    done
    return 1
}

for lib in libX11.a libXau.a libxcb.a; do
    LIBPATH="$(find_static_lib "$lib" || true)"
    if [ -n "$LIBPATH" ]; then
        echo "Extracting $LIBPATH..."
        (cd "$X_OBJS_DIR" && $AR x "$LIBPATH")
    else
        echo "Notice: $lib not found under /usr/lib/$TRIPLET, skipping extraction"
    fi
done

echo "== [3/3] Creating merged static archive libzan_gui.a =="
rm -f "$DEST/libzan_gui.a"
$AR rcs "$DEST/libzan_gui.a" "$WORK/zan_gui_${ARCH_SUFFIX}.o" "$X_OBJS_DIR"/*.o 2>/dev/null || \
$AR rcs "$DEST/libzan_gui.a" "$WORK/zan_gui_${ARCH_SUFFIX}.o"

echo "Successfully generated $DEST/libzan_gui.a"
