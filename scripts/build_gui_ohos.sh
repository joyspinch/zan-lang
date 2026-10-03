#!/bin/bash
# Rebuild the HarmonyOS GUI driver (ohos-x64 = DevEco emulator, ohos-arm64 =
# real device) and stage it into packages/Zan.Gui/src/Gui/drivers/ohos-<arch>/:
#
#   libzan_gui.so          -- standalone driver .so (runtime-loaded form)
#   static/libzan_gui.a    -- static archive zanc's --emit-lib link pulls in,
#                             so a HAP's libmain.so is self-contained
#
# gui_runtime.c is a single TU: the OHOS NativeWindow shell
# (gui_runtime_ohos.c) and the freetype font backend are #included into it,
# so one compile picks up everything.
#
# FreeType (2.14.x) is built from source as one unity TU -- INSTALL.ANY's
# single-object recipe over the module set registered in
# include/freetype/config/ftmodule.h (autofit/tt/type1/cff/cid/pfr/t42/
# winfnt/pcf/bdf/psaux/psnames/pshinter/sfnt/smooth/raster1/sdf/svg; gzip and
# lzw use the bundled implementations, no external deps). The objects live in
# _scratch and are recreated on demand, then archived into the static driver
# and linked into the .so -- no prebuilt freetype artifact is stored anywhere.
#
# The OpenHarmony native SDK (llvm/clang + sysroot with libEGL/libGLESv3)
# ships inside DevEco Studio; no separate NDK download is needed. drivers.yml
# has no ohos leg (GitHub runners cannot produce it), so this script is the
# only maintenance path for both ohos driver flavors.
#
# Usage: scripts/build_gui_ohos.sh [x64|arm64|all]   (default: all)
set -e
REPO=D:/project/zan-lang
DEVECO="C:/Program Files/Huawei/DevEco Studio"
LLVM="$DEVECO/sdk/default/openharmony/native/llvm/bin"
CLANG="$LLVM/clang.exe"
AR="$LLVM/llvm-ar.exe"
SYS="$DEVECO/sdk/default/openharmony/native/sysroot"
FT=D:/project/firefox/modules/freetype2
OUT="$REPO/_scratch/ft-ohos"

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
  local a="$1" triple ftlib drv
  if [ "$a" = "x64" ]; then triple=x86_64-linux-ohos; else triple=aarch64-linux-ohos; fi
  ftlib="freetype_$a"
  drv="$REPO/packages/Zan.Gui/src/Gui/drivers/ohos-$a"
  mkdir -p "$OUT/obj/$a" "$drv/static"

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
    -DZAN_GUI_FREETYPE -I"$FT/include" -I"$REPO/src/runtime" \
    --target=$triple --sysroot="$SYS" \
    -c "$REPO/src/runtime/gui_runtime.c" -o "$OUT/gui_ohos_$a.o"

  echo "== ohos-$a: link libzan_gui.so"
  "$CLANG" -fPIC -shared -g0 --target=$triple --sysroot="$SYS" \
    -o "$OUT/libzan_gui_$a.so" "$OUT/gui_ohos_$a.o" -L"$OUT" -l"$ftlib" -lm \
    -lEGL -lGLESv3

  echo "== ohos-$a: archive static/libzan_gui.a"
  "$AR" rcs "$OUT/libzan_gui_static_$a.a" "$OUT/gui_ohos_$a.o" \
    $(ls "$OUT"/obj/$a/src_*.o)

  cp -f "$OUT/libzan_gui_$a.so" "$drv/libzan_gui.so"
  cp -f "$OUT/libzan_gui_static_$a.a" "$drv/static/libzan_gui.a"
  echo "== ohos-$a: staged -> $drv/libzan_gui.so + $drv/static/libzan_gui.a"
}

case "$WANT" in
  x64) build_arch x64 ;;
  arm64) build_arch arm64 ;;
  all) build_arch x64; build_arch arm64 ;;
  *) echo "usage: $0 [x64|arm64|all]" >&2; exit 2 ;;
esac
