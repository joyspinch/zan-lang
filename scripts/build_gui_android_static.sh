#!/usr/bin/env bash
# Build independent NativeActivity GUI, image, audio and game archives/shared
# libraries for Android. The GUI archive alone embeds the FreeType objects.
# Usage: scripts/build_gui_android_static.sh [arm64|x64|all] [ndk-root]
# Set FREETYPE_SRC and ANDROID_NDK_ROOT (or ANDROID_NDK) to override tool paths.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
FT="${FREETYPE_SRC:-/d/project/firefox/modules/freetype2}"
NDK="${2:-${ANDROID_NDK_ROOT:-${ANDROID_NDK:-${LOCALAPPDATA:-$HOME}/Android/Sdk/ndk/27.2.12479018}}}"
case "$(uname -s)" in
  Darwin) NDKHOST=darwin-x86_64 ;;
  MINGW*|MSYS*|CYGWIN*) NDKHOST=windows-x86_64 ;;
  *) NDKHOST=linux-x86_64 ;;
esac
NDKBIN="$NDK/toolchains/llvm/prebuilt/$NDKHOST/bin"
SYSROOT="$NDK/toolchains/llvm/prebuilt/$NDKHOST/sysroot"
WORK="$REPO/build/android_gui_drivers"
FT_CUSTOM_INC="$WORK/ft-inc"
mkdir -p "$FT_CUSTOM_INC/freetype/config"
# Match the module list below; always refresh when the FreeType checkout changes.
grep -vE "ft_sdf_renderer_class|ft_bitmap_sdf_renderer_class|ft_svg_renderer_class|ft_raster1_renderer_class|t1cid_driver_class|pfr_driver_class|t42_driver_class|winfnt_driver_class|pcf_driver_class|bdf_driver_class|t1_driver_class" \
  "$FT/include/freetype/config/ftmodule.h" > "$FT_CUSTOM_INC/freetype/config/ftmodule.h"
COMMON=(-O2 -g0 -DNDEBUG -fPIC -std=c11 -I"$REPO/src/runtime")
FT_FLAGS=(-DFT2_BUILD_LIBRARY -I"$FT_CUSTOM_INC" -I"$FT/include")
FT_SRC="autofit/autofit.c cff/cff.c base/ftbase.c base/ftbbox.c base/ftbitmap.c \
base/ftdebug.c base/ftgasp.c base/ftglyph.c gzip/ftgzip.c base/ftinit.c \
base/ftmm.c base/ftstroke.c base/ftsynth.c base/ftsystem.c psaux/psaux.c \
pshinter/pshinter.c psnames/psnames.c raster/raster.c sfnt/sfnt.c \
smooth/smooth.c truetype/truetype.c"

build() { # triple driver-target
  local triple="$1" target="$2" f m feature directory
  local work="$WORK/$target"
  local gui="$REPO/packages/Zan.Gui/src/Gui/drivers/$target"
  local image="$REPO/packages/Zan.Image/src/System/Drawing/Imaging/drivers/$target"
  local audio="$REPO/packages/Zan.Desktop/src/System/Media/drivers/$target"
  local game="$REPO/packages/Zan.Game/src/Game/Graphics/drivers/$target"
  local target_flags=(--sysroot="$SYSROOT" -target "$triple")
  local ft_objects=()
  mkdir -p "$work" "$gui/static" "$image/static" "$audio/static" "$game/static"
  "$NDKBIN/clang" "${target_flags[@]}" "${COMMON[@]}" "${FT_FLAGS[@]}" \
    -DZAN_GUI_ANDROID_NATIVE -DZAN_GUI_FREETYPE -DZAN_GUI_STATIC -I"$REPO/toolchain/$target" \
    -c "$REPO/src/runtime/gui_runtime.c" -o "$work/gui.o"
  for f in $FT_SRC; do
    m=$(basename "$f" .c)
    "$NDKBIN/clang" "${target_flags[@]}" "${COMMON[@]}" "${FT_FLAGS[@]}" \
      -c "$FT/src/$f" -o "$work/$m.o"
    ft_objects+=("$work/$m.o")
  done
  for feature in image audio game; do
    "$NDKBIN/clang" "${target_flags[@]}" "${COMMON[@]}" \
      -D"ZAN_$(printf '%s' "$feature" | tr '[:lower:]' '[:upper:]')_STATIC" \
      -c "$REPO/src/runtime/zan_$feature.c" -o "$work/$feature.o"
  done
  rm -f "$gui/static/libzan_gui.a" "$image/static/libzan_image.a" "$audio/static/libzan_audio.a" "$game/static/libzan_game.a"
  "$NDKBIN/llvm-ar" rcs "$gui/static/libzan_gui.a" "$work/gui.o" "${ft_objects[@]}"
  "$NDKBIN/llvm-ar" rcs "$image/static/libzan_image.a" "$work/image.o"
  "$NDKBIN/llvm-ar" rcs "$audio/static/libzan_audio.a" "$work/audio.o"
  "$NDKBIN/llvm-ar" rcs "$game/static/libzan_game.a" "$work/game.o"
  "$NDKBIN/clang" "${target_flags[@]}" -shared -Wl,-soname,libzan_gui.so \
    -o "$gui/libzan_gui.so" "$work/gui.o" "${ft_objects[@]}" -landroid -llog -lEGL -lGLESv3 -ldl -lm
  "$NDKBIN/clang" "${target_flags[@]}" -shared -Wl,-soname,libzan_image.so -o "$image/libzan_image.so" "$work/image.o" -lm
  "$NDKBIN/clang" "${target_flags[@]}" -shared -Wl,-soname,libzan_audio.so -o "$audio/libzan_audio.so" "$work/audio.o" -laaudio -ldl -lm
  "$NDKBIN/clang" "${target_flags[@]}" -shared -Wl,-soname,libzan_game.so -Wl,-rpath,'$ORIGIN' \
    -o "$game/libzan_game.so" "$work/game.o" -L"$gui" -L"$image" -lzan_gui -lzan_image -lm
  # pthread symbols are provided by Android libc; there is no libpthread.a.
  printf 'android\nlog\nEGL\nGLESv3\ndl\nm\n' > "$gui/static/zan_gui.libs"
  printf 'm\n' > "$image/static/zan_image.libs"
  printf 'aaudio\ndl\nm\n' > "$audio/static/zan_audio.libs"
  { printf 'zan_gui\nzan_image\n'; cat "$gui/static/zan_gui.libs"; } > "$game/static/zan_game.libs"
  printf 'libzan_gui.so\n' > "$gui/zan_gui.bundle"
  printf 'libzan_image.so\n' > "$image/zan_image.bundle"
  printf 'libzan_audio.so\n' > "$audio/zan_audio.bundle"
  printf '@driver/zan_gui\n@driver/zan_image\nlibzan_game.so\n' > "$game/zan_game.bundle"
  echo "staged GUI/image/audio/game shared and static drivers for $target"
}
case "${1:-all}" in
  arm64|android-arm64) build aarch64-linux-android28 android-arm64 ;;
  x64|x86_64|android-x64) build x86_64-linux-android28 android-x64 ;;
  all) build aarch64-linux-android28 android-arm64; build x86_64-linux-android28 android-x64 ;;
  *) echo "usage: $0 [arm64|x64|all] [ndk-root]" >&2; exit 2 ;;
esac
