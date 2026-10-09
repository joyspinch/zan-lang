#!/usr/bin/env bash
# Build/stage independent Linux GUI, image, audio and game shared/static drivers.
# GUI keeps its existing bundled X11 objects and font backend. Image decoding
# and audio are standalone archives; game links the GUI/image shared context.
# For cross builds set CC, AR and PKG_CONFIG_LIBDIR for the requested target.
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
GUI="$ROOT/packages/Zan.Gui/src/Gui/drivers/$TARGET"
IMAGE="$ROOT/packages/Zan.Image/src/System/Drawing/Imaging/drivers/$TARGET"
AUDIO="$ROOT/packages/Zan.Desktop/src/System/Media/drivers/$TARGET"
GAME="$ROOT/packages/Zan.Game/src/Game/Graphics/drivers/$TARGET"
DEST="$GUI/static"
WORK="$ROOT/build/linux_gui_drivers_$ARCH_SUFFIX"
mkdir -p "$DEST" "$IMAGE/static" "$AUDIO/static" "$GAME/static" "$WORK"

echo "== [1/3] Compiling independent native drivers for $TARGET =="
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

for feature in image audio game; do
    $CC -O2 -g0 -DNDEBUG -ffunction-sections -fdata-sections -fPIC -std=gnu11 \
        -U_FORTIFY_SOURCE -DZAN_NO_EXECINFO -D"ZAN_${feature^^}_STATIC" \
        -I"$ROOT/src/runtime" -c "$ROOT/src/runtime/zan_$feature.c" -o "$WORK/zan_$feature.o"
done

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
rm -f "$IMAGE/static/libzan_image.a" "$AUDIO/static/libzan_audio.a" "$GAME/static/libzan_game.a"
$AR rcs "$IMAGE/static/libzan_image.a" "$WORK/zan_image.o"
$AR rcs "$AUDIO/static/libzan_audio.a" "$WORK/zan_audio.o"
$AR rcs "$GAME/static/libzan_game.a" "$WORK/zan_game.o"
printf 'freetype\nfontconfig\ndl\npthread\nm\n' > "$DEST/zan_gui.libs"
printf 'm\n' > "$IMAGE/static/zan_image.libs"
printf 'dl\npthread\nm\n' > "$AUDIO/static/zan_audio.libs"
{ printf 'zan_gui\nzan_image\n'; cat "$DEST/zan_gui.libs"; } > "$GAME/static/zan_game.libs"

# Build the shared form from the same independent objects. $ORIGIN lets a
# published game driver find the two dependent libraries beside it.
$CC -shared -Wl,-soname,libzan_gui.so -o "$GUI/libzan_gui.so" \
    "$WORK/zan_gui_${ARCH_SUFFIX}.o" \
    -lX11 $(pkg-config --libs freetype2 fontconfig) -ldl -lpthread -lm
$CC -shared -Wl,-soname,libzan_image.so -o "$IMAGE/libzan_image.so" "$WORK/zan_image.o" -lm
$CC -shared -Wl,-soname,libzan_audio.so -o "$AUDIO/libzan_audio.so" "$WORK/zan_audio.o" -ldl -lpthread -lm
$CC -shared -Wl,-soname,libzan_game.so -Wl,-rpath,'$ORIGIN' \
    -o "$GAME/libzan_game.so" "$WORK/zan_game.o" -L"$GUI" -L"$IMAGE" -lzan_gui -lzan_image -lm
printf 'libzan_gui.so\n' > "$GUI/zan_gui.bundle"
printf 'libzan_image.so\n' > "$IMAGE/zan_image.bundle"
printf 'libzan_audio.so\n' > "$AUDIO/zan_audio.bundle"
printf '@driver/zan_gui\n@driver/zan_image\nlibzan_game.so\n' > "$GAME/zan_game.bundle"
echo "Successfully generated GUI/image/audio/game shared and static drivers for $TARGET"
