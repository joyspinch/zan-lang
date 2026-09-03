#!/bin/bash
# Rebuild libzan_gui.so for both Android ABIs and stage it into the driver
# directories zanc bundles for --target android-* (--emit-apk).
#
# gui_runtime.c is a single TU: the SDL backend, the Android system-WebView
# bridge (gui_runtime_android.c) and the shims are all #included into it, so
# one compile per ABI picks up everything. freetype is linked statically from
# a prebuilt libfreetype_<arch>.a (see _scratch/ft-android for how it was
# configured); SDL3 comes from the AAR's prefab libs, matching what the APK
# shell packages.
#
# Usage: scripts/build_gui_android.sh          # both ABIs
#        scripts/build_gui_android.sh arm64    # one ABI (arm64|x86_64)
set -e
REPO=/d/project/zan-lang
NDK="${ANDROID_NDK_ROOT:-$LOCALAPPDATA/Android/Sdk/ndk/27.2.12479018}/toolchains/llvm/prebuilt/windows-x86_64/bin"
FT=/d/project/firefox/modules/freetype2
OUT="$REPO/_scratch/ft-android"
SDL_INC="$REPO/_scratch/sdl3-android/aar/prefab/modules/SDL3-Headers/include"
SDL_LIB="$REPO/_scratch/sdl3-android/aar/prefab/modules/SDL3-shared/libs"
COMMON="-O2 -fPIC -g0 -std=c11 -DZAN_GUI_SDL -DZAN_GUI_FREETYPE -DFT2_BUILD_LIBRARY"
INC="-I$FT/include -I$OUT/include -I$REPO/src/runtime"

build () { # arch triple sdllib drvdir
  local arch=$1 triple=$2 sdllib=$3 drvdir=$4
  echo "== $arch: compile"
  "$NDK/${triple}-clang" $COMMON $INC -I"$SDL_INC" \
    -c "$REPO/src/runtime/gui_runtime.c" -o "$OUT/gui_${arch}.o"
  echo "== $arch: link"
  "$NDK/${triple}-clang" -fPIC -shared -g0 -o "$OUT/libzan_gui_${arch}.so" \
    "$OUT/gui_${arch}.o" \
    -L"$sdllib" -lSDL3 -L"$OUT" -lfreetype_${arch} -llog -lm
  mkdir -p "$REPO/stdlib/Gui/drivers/$drvdir"
  cp -f "$OUT/libzan_gui_${arch}.so" "$REPO/stdlib/Gui/drivers/$drvdir/libzan_gui.so"
  echo "== $arch: staged -> stdlib/Gui/drivers/$drvdir/libzan_gui.so"
}

case "${1:-all}" in
  arm64)   build aarch64 aarch64-linux-android28   "$SDL_LIB/android.arm64-v8a" android-arm64 ;;
  x86_64)  build x86_64  x86_64-linux-android28    "$SDL_LIB/android.x86_64"    android-x64  ;;
  all)     build aarch64 aarch64-linux-android28   "$SDL_LIB/android.arm64-v8a" android-arm64
           build x86_64  x86_64-linux-android28    "$SDL_LIB/android.x86_64"    android-x64  ;;
  *) echo "usage: $0 [arm64|x86_64|all]" >&2; exit 2 ;;
esac
