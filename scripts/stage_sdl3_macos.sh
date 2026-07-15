#!/usr/bin/env bash
# Stage the macOS SDL3 driver bundle into stdlib/SDL3/drivers/<target>/.
#
# Mirrors the libpq macOS bundle: the per-target directory holds the native
# dylibs (gitignored) plus a committed `zan_sdl3.bundle` manifest that
# `zanc --publish` reads to copy the runtime next to the published binary.
# All install names are rewritten to @rpath so the published bundle is
# relocatable (the publish step adds an @loader_path rpath).
#
# Usage: scripts/stage_sdl3_macos.sh [target]   (default: macos-arm64)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TARGET="${1:-macos-arm64}"
BUILD="${BUILD_DIR:-$ROOT/build}"
DEST="$ROOT/stdlib/SDL3/drivers/$TARGET"

realpath_py() { python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$1"; }

cmake --build "$BUILD" --target zan_sdl3

BRIDGE="$BUILD/libzan_sdl3.dylib"
SDL_LIBDIR="$(pkg-config --variable=libdir sdl3)"
SDL_SRC="$(realpath_py "$SDL_LIBDIR/libSDL3.0.dylib")"

mkdir -p "$DEST"
cp -f "$BRIDGE" "$DEST/libzan_sdl3.dylib"
cp -f "$SDL_SRC" "$DEST/libSDL3.0.dylib"
chmod u+w "$DEST/libzan_sdl3.dylib" "$DEST/libSDL3.0.dylib"

# Relocatable install names.
install_name_tool -id @rpath/libzan_sdl3.dylib "$DEST/libzan_sdl3.dylib"
install_name_tool -id @rpath/libSDL3.0.dylib   "$DEST/libSDL3.0.dylib"

# Rewrite the bridge's absolute dependency on SDL3 to @rpath.
OLD_SDL="$(otool -L "$DEST/libzan_sdl3.dylib" | awk '/libSDL3/{print $1; exit}')"
if [ -n "$OLD_SDL" ]; then
    install_name_tool -change "$OLD_SDL" @rpath/libSDL3.0.dylib "$DEST/libzan_sdl3.dylib"
fi

# Re-sign ad-hoc: install_name_tool invalidates the code signature, which
# Apple Silicon's loader will otherwise reject.
codesign --force --sign - "$DEST/libSDL3.0.dylib"
codesign --force --sign - "$DEST/libzan_sdl3.dylib"

# Publish manifest: bridge first, then its runtime dependency.
cat > "$DEST/zan_sdl3.bundle" <<EOF
libzan_sdl3.dylib
libSDL3.0.dylib
EOF

echo "staged -> $DEST"
ls -la "$DEST"
echo "== otool libzan_sdl3.dylib =="
otool -L "$DEST/libzan_sdl3.dylib"
