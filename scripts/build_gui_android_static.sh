#!/bin/bash
# Rebuild the committed Android static GUI driver archives
#   packages/Zan.Gui/src/Gui/drivers/android-{arm64,x64}/static/libzan_gui.a
# that zanc links into --target android-* --emit-apk libmain.so builds.
#
# The archive is: gui_runtime.c compiled as ONE translation unit with
# ZAN_GUI_ANDROID_NATIVE + ZAN_GUI_FREETYPE (the NativeActivity shell, EGL
# present, FreeType text, libwebp/stb_image all live in that TU) plus the
# FreeType modules (FT2_BUILD_LIBRARY) baked in as separate members.
#
# Re-run this whenever src/runtime/gui_runtime*.c, src/runtime/libwebp/ or the
# FreeType checkout change AND packages/Zan.Gui/src/Gui/Rendering/Render.zan (or any stdlib surface)
# gains a new `static extern` -- a stale archive does NOT fail the link
# (-shared keeps unresolved symbols), it crashes the app at dlopen with
# "cannot locate symbol" (see 2026-09-16 NewProject.apk incident).
# scripts/check_toolchain_stale.py does not cover this archive.
#
# Usage: scripts/build_gui_android_static.sh [arm64|x64|all] [ndk-root]
set -e
REPO=$(cd "$(dirname "$0")/.." && pwd)
FT=/d/project/firefox/modules/freetype2
NDK="${2:-${ANDROID_NDK:-$LOCALAPPDATA/Android/Sdk/ndk/27.2.12479018}}"
NDKBIN="$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin"
SYSROOT="$NDK/toolchains/llvm/prebuilt/windows-x86_64/sysroot"
WORK="$REPO/build/android_gui_drivers"

# Trimmed ftmodule.h: registers only the modules we compile (drops
# raster1/sdf/svg renderers from the upstream header, matching what the
# committed archives have always carried -- the upstream header registers
# classes whose sources we do not compile, which would fail the link).
FT_CUSTOM_INC="$REPO/build/android_gui_drivers/ft-inc"
mkdir -p "$FT_CUSTOM_INC/freetype/config"
if [ ! -f "$FT_CUSTOM_INC/freetype/config/ftmodule.h" ]; then
  grep -vE "ft_sdf_renderer_class|ft_bitmap_sdf_renderer_class|ft_svg_renderer_class|ft_raster1_renderer_class|t1cid_driver_class|pfr_driver_class|t42_driver_class|winfnt_driver_class|pcf_driver_class|bdf_driver_class|t1_driver_class" \
    "$FT/include/freetype/config/ftmodule.h" > "$FT_CUSTOM_INC/freetype/config/ftmodule.h"
fi

# -DNDEBUG drops the vendored libs' assert() strings (their __FILE__ would
# leak build-machine paths into every static publish).
COMMON="-O2 -g0 -DNDEBUG -fPIC -std=c11 -DFT2_BUILD_LIBRARY -I$FT_CUSTOM_INC -I$FT/include -I$REPO/src/runtime"

# FreeType modules: the member list the committed archives carry (base set +
# gzip + synth + mm + stroke; no bzip2/lzw/png/zlib, no cache).
FT_SRC="autofit/autofit.c cff/cff.c base/ftbase.c base/ftbbox.c base/ftbitmap.c \
base/ftdebug.c base/ftgasp.c base/ftglyph.c gzip/ftgzip.c base/ftinit.c \
base/ftmm.c base/ftstroke.c base/ftsynth.c base/ftsystem.c psaux/psaux.c \
pshinter/pshinter.c psnames/psnames.c raster/raster.c sfnt/sfnt.c \
smooth/smooth.c truetype/truetype.c"

build () { # triple outdir
  local triple=$1 out=$2
  local arch=${triple%%-*}
  local glue=android-arm64
  [ "$arch" = x86_64 ] && glue=android-x64
  mkdir -p "$out"
  echo "== compile gui_runtime.c ($triple)"
  "$NDKBIN/clang" --sysroot="$SYSROOT" -target "$triple" \
    -DZAN_GUI_ANDROID_NATIVE -DZAN_GUI_FREETYPE -I"$REPO/toolchain/$glue" $COMMON \
    -c "$REPO/src/runtime/gui_runtime.c" -o "$out/gui_${arch}.o"
  echo "== compile freetype modules ($triple)"
  for f in $FT_SRC; do
    local m
    m=$(basename "$f" .c)
    "$NDKBIN/clang" --sysroot="$SYSROOT" -target "$triple" $COMMON \
      -c "$FT/src/$f" -o "$out/$m.o"
  done
  echo "== archive"
  rm -f "$out/libzan_gui.a"
  "$NDKBIN/llvm-ar" rcs "$out/libzan_gui.a" "$out"/gui_*.o "$out"/[acfghprst]*.o
  rm -f "$out"/gui_*.o "$out"/[acfghprst]*.o
  ls -la "$out/libzan_gui.a"
}

case "${1:-all}" in
  arm64) build aarch64-linux-android28 "$REPO/packages/Zan.Gui/src/Gui/drivers/android-arm64/static" ;;
  x64)   build x86_64-linux-android28  "$REPO/packages/Zan.Gui/src/Gui/drivers/android-x64/static" ;;
  all)   build aarch64-linux-android28 "$REPO/packages/Zan.Gui/src/Gui/drivers/android-arm64/static"
         build x86_64-linux-android28  "$REPO/packages/Zan.Gui/src/Gui/drivers/android-x64/static" ;;
  *) echo "usage: $0 [arm64|x64|all] [ndk-root]" >&2; exit 2 ;;
esac
echo done
