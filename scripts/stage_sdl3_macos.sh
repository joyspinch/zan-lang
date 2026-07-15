#!/usr/bin/env bash
# Stage the macOS SDL3 driver bundle into stdlib/SDL3/drivers/<target>/.
#
# Mirrors the libpq macOS bundle: the per-target directory holds the native
# dylibs (gitignored) plus a committed `zan_sdl3.bundle` manifest that
# `zanc --publish` reads to copy the runtime next to the published binary.
# All install names are rewritten to @rpath so the published bundle is
# relocatable (the publish step adds an @loader_path rpath).
#
# Usage:
#   scripts/stage_sdl3_macos.sh [macos-arm64|macos-x64]   (default: macos-arm64)
#
# macos-arm64 builds natively: the bridge via the CMake `zan_sdl3` target and
# the SDL3 runtime from the pkg-config (Homebrew) install.
#
# macos-x64 cross-compiles from an Apple Silicon host. Homebrew's SDL3 is
# arm64-only, so an x86_64 SDL3 runtime must be provided, either directly or
# built from source:
#   SDL3_X64_LIB=/path/to/x86_64/libSDL3.0.dylib   scripts/stage_sdl3_macos.sh macos-x64
#   SDL3_SRC=/path/to/SDL3-<ver>-source            scripts/stage_sdl3_macos.sh macos-x64
# With SDL3_SRC the script configures/builds SDL3 for x86_64 itself.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TARGET="${1:-macos-arm64}"
BUILD="${BUILD_DIR:-$ROOT/build}"
DEST="$ROOT/stdlib/SDL3/drivers/$TARGET"

realpath_py() { python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$1"; }

case "$TARGET" in
    macos-arm64) ARCH=arm64 ;;
    macos-x64)   ARCH=x86_64 ;;
    *) echo "unknown target '$TARGET' (expected macos-arm64 or macos-x64)" >&2; exit 2 ;;
esac

SDL_INCLUDE="${SDL3_INCLUDE:-$(pkg-config --variable=includedir sdl3)}"

if [ "$ARCH" = arm64 ]; then
    cmake --build "$BUILD" --target zan_sdl3
    BRIDGE="$BUILD/libzan_sdl3.dylib"
    SDL_SRC="$(realpath_py "$(pkg-config --variable=libdir sdl3)/libSDL3.0.dylib")"
else
    if [ -n "${SDL3_X64_LIB:-}" ]; then
        SDL_SRC="$(realpath_py "$SDL3_X64_LIB")"
    elif [ -n "${SDL3_SRC:-}" ]; then
        cmake -S "$SDL3_SRC" -B "$SDL3_SRC/build-x64" -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_OSX_ARCHITECTURES=x86_64 -DSDL_SHARED=ON -DSDL_STATIC=OFF \
            -DSDL_TEST_LIBRARY=OFF
        cmake --build "$SDL3_SRC/build-x64" -j
        SDL_SRC="$(realpath_py "$SDL3_SRC/build-x64/libSDL3.0.dylib")"
    else
        echo "macos-x64 needs SDL3_X64_LIB=<x86_64 libSDL3.0.dylib> or SDL3_SRC=<SDL3 source>" >&2
        exit 2
    fi
    BRIDGE="$BUILD/libzan_sdl3_x64.dylib"
    xcrun clang -arch x86_64 -O2 -dynamiclib -fPIC \
        -o "$BRIDGE" "$ROOT/stdlib/SDL3/native/zan_sdl3.c" \
        -I"$SDL_INCLUDE" "$SDL_SRC" -lm \
        -install_name @rpath/libzan_sdl3.dylib
fi

mkdir -p "$DEST"
cp -f "$BRIDGE" "$DEST/libzan_sdl3.dylib"
cp -f "$SDL_SRC" "$DEST/libSDL3.0.dylib"
chmod u+w "$DEST/libzan_sdl3.dylib" "$DEST/libSDL3.0.dylib"

# Relocatable install names.
install_name_tool -id @rpath/libzan_sdl3.dylib "$DEST/libzan_sdl3.dylib"
install_name_tool -id @rpath/libSDL3.0.dylib   "$DEST/libSDL3.0.dylib"

# Rewrite the bridge's dependency on SDL3 to @rpath (no-op if already @rpath).
OLD_SDL="$(otool -L "$DEST/libzan_sdl3.dylib" | awk '/libSDL3/{print $1; exit}')"
if [ -n "$OLD_SDL" ] && [ "$OLD_SDL" != "@rpath/libSDL3.0.dylib" ]; then
    install_name_tool -change "$OLD_SDL" @rpath/libSDL3.0.dylib "$DEST/libzan_sdl3.dylib"
fi

# Re-sign ad-hoc: install_name_tool invalidates the code signature, which
# the loader will otherwise reject.
codesign --force --sign - "$DEST/libSDL3.0.dylib"
codesign --force --sign - "$DEST/libzan_sdl3.dylib"

# Publish manifest: bridge first, then its runtime dependency.
cat > "$DEST/zan_sdl3.bundle" <<EOF
libzan_sdl3.dylib
libSDL3.0.dylib
EOF

echo "staged -> $DEST"
ls -la "$DEST"
echo "== lipo =="
lipo -info "$DEST"/*.dylib
echo "== otool libzan_sdl3.dylib =="
otool -L "$DEST/libzan_sdl3.dylib"
