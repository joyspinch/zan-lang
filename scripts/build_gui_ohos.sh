#!/bin/bash
# Rebuild independent HarmonyOS GUI, image, audio and game drivers (ohos-x64 =
# DevEco emulator, ohos-arm64 = real device), shared and static in owner dirs.
# GUI owns the OHOS NativeWindow/FreeType shell; game shares that drawing context
# through zan_gui and uses the separate zan_image decoder/cache.
#
# FreeType (2.14.x) is built per source file over the registered module set.
# Objects/cache live under build/ and are archived only into the GUI driver.
#
# The OpenHarmony native SDK (llvm/clang + sysroot with libEGL/libGLESv3)
# ships inside DevEco Studio; no separate NDK download is needed. drivers.yml
# has no ohos leg (GitHub runners cannot produce it), so this script is the
# only maintenance path for both ohos driver flavors.
#
# Usage: scripts/build_gui_ohos.sh [x64|arm64|all]   (default: all)
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)

# DevEco Studio location, most specific override first, then the standard
# install dirs of each host. Nothing is pinned to an author's drive letter.
find_deveco() {
  local c
  for c in "${ZAN_DEVECO_HOME:-}" "${DEVECO_ROOT:-}"; do
    [ -n "$c" ] && [ -d "$c/sdk/default/openharmony/native" ] && { printf '%s\n' "$c"; return 0; }
  done
  if [ -n "${DEVECO_SDK_HOME:-}" ]; then
    c="$DEVECO_SDK_HOME"
    case "$c" in */native) c="${c%/native}" ;; esac
    while [ -n "$c" ] && [ "$c" != "/" ] && [ ! -d "$c/default/openharmony/native" ]; do
      c=$(dirname "$c")
    done
    [ -d "$c/default/openharmony/native" ] && { dirname "$c"; return 0; }
  fi
  for c in "${PROGRAMFILES:-}/Huawei/DevEco Studio" \
           "${PROGRAMW6432:-}/Huawei/DevEco Studio" \
           "${LOCALAPPDATA:-}/Huawei/DevEcoStudio" \
           "$HOME/Huawei/DevEco-Studio" \
           "$HOME/Library/Huawei/DevEco-Studio" \
           "/Applications/DevEco-Studio.app/Contents" \
           "/opt/huawei/deveco-studio" "/opt/deveco-studio"; do
    [ -n "$c" ] && [ -d "$c/sdk/default/openharmony/native" ] && { printf '%s\n' "$c"; return 0; }
  done
  return 1
}
if ! DEVECO=$(find_deveco); then
  echo "DevEco Studio not found. Set ZAN_DEVECO_HOME (or DEVECO_ROOT / DEVECO_SDK_HOME)." >&2
  exit 1
fi
LLVM="$DEVECO/sdk/default/openharmony/native/llvm/bin"
CLANG="$LLVM/clang.exe"
AR="$LLVM/llvm-ar.exe"
SYS="$DEVECO/sdk/default/openharmony/native/sysroot"

# FreeType source: ZAN_FREETYPE_ROOT / FREETYPE_SRC (checkout root), a bare
# FREETYPE_INC (its include dir), then the usual user-level checkouts.
FT="${ZAN_FREETYPE_ROOT:-${FREETYPE_SRC:-}}"
if [ -z "$FT" ] && [ -n "${FREETYPE_INC:-}" ]; then FT=$(dirname "$FREETYPE_INC"); fi
if [ -z "$FT" ]; then
  for c in "$HOME/firefox/modules/freetype2" \
           "$HOME/.mozbuild/firefox/modules/freetype2" \
           "$HOME/src/firefox/modules/freetype2"; do
    if [ -f "$c/include/ft2build.h" ]; then FT="$c"; break; fi
  done
fi
if [ -z "$FT" ] || [ ! -f "$FT/include/ft2build.h" ]; then
  echo "FreeType source not found. Set ZAN_FREETYPE_ROOT (or FREETYPE_SRC) to a firefox/modules/freetype2 checkout." >&2
  exit 1
fi
OUT="$REPO/build/ohos_gui_drivers"

WANT="${1:-all}"

FT_FILES="src/base/ftsystem.c src/base/ftinit.c src/base/ftdebug.c \
src/base/ftbase.c src/base/ftbbox.c src/base/ftglyph.c src/base/ftbdf.c \
src/base/ftbitmap.c src/base/ftcid.c src/base/ftfstype.c src/base/ftgasp.c \
src/base/ftmm.c src/base/ftpatent.c src/base/ftpfr.c src/base/ftstroke.c \
src/base/ftsynth.c src/base/fttype1.c src/base/ftwinfnt.c \
src/autofit/autofit.c src/bdf/bdf.c src/cache/ftcache.c src/cff/cff.c \
src/cid/type1cid.c src/gzip/ftgzip.c src/lzw/ftlzw.c src/pcf/pcf.c \
src/pfr/pfr.c src/psaux/psaux.c src/pshinter/pshinter.c src/psnames/psnames.c \
src/raster/raster.c src/sdf/sdf.c src/svg/svg.c src/smooth/smooth.c \
src/sfnt/sfnt.c src/truetype/truetype.c src/type1/type1.c src/type42/type42.c \
src/winfonts/winfnt.c"

build_arch() {  # build_arch <arch>   (x64 | arm64)
  local a="$1" triple ftlib drv image audio game feature
  if [ "$a" = "x64" ]; then triple=x86_64-linux-ohos; else triple=aarch64-linux-ohos; fi
  ftlib="freetype_$a"
  drv="$REPO/packages/Zan.Gui/src/Gui/drivers/ohos-$a"
  image="$REPO/packages/Zan.Image/src/System/Drawing/Imaging/drivers/ohos-$a"
  audio="$REPO/packages/Zan.Desktop/src/System/Media/drivers/ohos-$a"
  game="$REPO/packages/Zan.Game/src/Game/Graphics/drivers/ohos-$a"
  mkdir -p "$OUT/obj/$a" "$drv/static" "$image/static" "$audio/static" "$game/static"

  if [ ! -f "$OUT/lib$ftlib.a" ]; then
    echo "== ohos-$a: freetype per-file (INSTALL.ANY) -> lib$ftlib.a"
    # Not one unity TU: the modules' private macros collide when concatenated
    # in arbitrary order (ftgrays/ftsdfcommon ONE_PIXEL, pfr macros) -- the
    # supported "single object" build relies on upstream's include order, so
    # we compile each component separately instead.
    local f objs=""
    for f in $FT_FILES; do
      local o="$OUT/obj/$a/$(echo "$f" | sed 's|/|_|g; s|\.c$|.o|')"
      "$CLANG" -O2 -fPIC -g0 -DNDEBUG -std=c11 -DFT2_BUILD_LIBRARY \
        -I"$FT/include" --target=$triple --sysroot="$SYS" \
        -c "$FT/$f" -o "$o"
      objs="$objs $o"
    done
    "$AR" rcs "$OUT/lib$ftlib.a" $objs
  fi

  echo "== ohos-$a: compile gui_runtime.c"
  "$CLANG" -O2 -fPIC -g0 -DNDEBUG -std=c11 -D_GNU_SOURCE -DZAN_GUI_OHOS \
    -DZAN_GUI_FREETYPE -DZAN_GUI_STATIC -I"$FT/include" -I"$REPO/src/runtime" \
    -I"$REPO/src/drivers" -I"$REPO/src/third_party" \
    --target=$triple --sysroot="$SYS" \
    -c "$REPO/src/drivers/gui/gui_runtime.c" -o "$OUT/gui_ohos_$a.o"

  for feature in image audio game; do
    "$CLANG" -O2 -fPIC -g0 -DNDEBUG -std=c11 -D_GNU_SOURCE \
      -D"ZAN_$(printf '%s' "$feature" | tr '[:lower:]' '[:upper:]')_STATIC" \
      -I"$REPO/src/runtime" -I"$REPO/src/drivers" -I"$REPO/src/third_party" --target=$triple --sysroot="$SYS" \
      -c "$REPO/src/drivers/$feature/zan_$feature.c" -o "$OUT/zan_${feature}_$a.o"
  done
  "$CLANG" -fPIC -shared -g0 --target=$triple --sysroot="$SYS" \
    -Wl,-soname,libzan_gui.so -o "$drv/libzan_gui.so" "$OUT/gui_ohos_$a.o" \
    -L"$OUT" -l"$ftlib" -lm -ldl -lEGL -lGLESv3
  "$CLANG" -fPIC -shared -g0 --target=$triple --sysroot="$SYS" \
    -Wl,-soname,libzan_image.so -o "$image/libzan_image.so" "$OUT/zan_image_$a.o" -lm
  "$CLANG" -fPIC -shared -g0 --target=$triple --sysroot="$SYS" \
    -Wl,-soname,libzan_audio.so -o "$audio/libzan_audio.so" "$OUT/zan_audio_$a.o" -ldl -pthread -lm
  "$CLANG" -fPIC -shared -g0 --target=$triple --sysroot="$SYS" \
    -Wl,-soname,libzan_game.so -Wl,-rpath,'$ORIGIN' -o "$game/libzan_game.so" \
    "$OUT/zan_game_$a.o" -L"$drv" -L"$image" -lzan_gui -lzan_image -lm
  rm -f "$drv/static/libzan_gui.a" "$image/static/libzan_image.a" "$audio/static/libzan_audio.a" "$game/static/libzan_game.a"
  "$AR" rcs "$drv/static/libzan_gui.a" "$OUT/gui_ohos_$a.o" "$OUT"/obj/"$a"/src_*.o
  "$AR" rcs "$image/static/libzan_image.a" "$OUT/zan_image_$a.o"
  "$AR" rcs "$audio/static/libzan_audio.a" "$OUT/zan_audio_$a.o"
  "$AR" rcs "$game/static/libzan_game.a" "$OUT/zan_game_$a.o"
  printf 'EGL\nGLESv3\ndl\nm\n' > "$drv/static/zan_gui.libs"
  printf 'm\n' > "$image/static/zan_image.libs"
  printf 'dl\npthread\nm\n' > "$audio/static/zan_audio.libs"
  { printf 'zan_gui\nzan_image\n'; cat "$drv/static/zan_gui.libs"; } > "$game/static/zan_game.libs"
  printf 'libzan_gui.so\n' > "$drv/zan_gui.bundle"
  printf 'libzan_image.so\n' > "$image/zan_image.bundle"
  printf 'libzan_audio.so\n' > "$audio/zan_audio.bundle"
  printf '@driver/zan_gui\n@driver/zan_image\nlibzan_game.so\n' > "$game/zan_game.bundle"
  echo "== ohos-$a: staged GUI/image/audio/game shared and static drivers"
}

case "$WANT" in
  x64) build_arch x64 ;;
  arm64) build_arch arm64 ;;
  all) build_arch x64; build_arch arm64 ;;
  *) echo "usage: $0 [x64|arm64|all]" >&2; exit 2 ;;
esac
