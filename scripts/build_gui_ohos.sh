#!/bin/bash
# Rebuild the HarmonyOS GUI driver (ohos-x64, matching the DevEco emulator)
# and stage it into stdlib/Gui/drivers/ohos-x64/:
#
#   libzan_gui.so          -- standalone driver .so (runtime-loaded form)
#   static/libzan_gui.a    -- static archive zanc's --emit-lib link pulls in,
#                             so a HAP's libmain.so is self-contained
#
# gui_runtime.c is a single TU: the OHOS NativeWindow shell
# (gui_runtime_ohos.c) and the freetype font backend are #included into it,
# so one compile picks up everything. freetype is linked statically from the
# prebuilt libfreetype_x64.a (see _scratch/ft-ohos/compile.sh for the module
# list; single-TU module builds with DevEco's bundled LLVM).
#
# Usage: scripts/build_gui_ohos.sh
set -e
REPO=/d/project/zan-lang
DEVECO="C:/Program Files/Huawei/DevEco Studio"
CLANG="$DEVECO/sdk/default/openharmony/native/llvm/bin/clang.exe"
AR="$DEVECO/sdk/default/openharmony/native/llvm/bin/llvm-ar.exe"
SYS="$DEVECO/sdk/default/openharmony/native/sysroot"
FT=/d/project/firefox/modules/freetype2
OUT="$REPO/_scratch/ft-ohos"
DRV="$REPO/stdlib/Gui/drivers/ohos-x64"

COMMON="-O2 -fPIC -g0 -std=c11 -D_GNU_SOURCE -DZAN_GUI_OHOS -DZAN_GUI_FREETYPE -DFT2_BUILD_LIBRARY"
INC="-I$FT/include -I$REPO/src/runtime"

echo "== ohos-x64: compile gui_runtime.c"
"$CLANG" $COMMON $INC --target=x86_64-linux-ohos --sysroot="$SYS" \
  -c "$REPO/src/runtime/gui_runtime.c" -o "$OUT/gui_ohos.o"

echo "== ohos-x64: link libzan_gui.so"
"$CLANG" -fPIC -shared -g0 --target=x86_64-linux-ohos --sysroot="$SYS" \
  -o "$OUT/libzan_gui.so" "$OUT/gui_ohos.o" -L"$OUT" -lfreetype_x64 -lm \
  -lEGL -lGLESv3

echo "== ohos-x64: archive static/libzan_gui.a"
"$AR" rcs "$OUT/libzan_gui_static.a" "$OUT/gui_ohos.o" \
  $(ls "$OUT"/obj/x64/*.o)

mkdir -p "$DRV/static"
cp -f "$OUT/libzan_gui.so" "$DRV/libzan_gui.so"
cp -f "$OUT/libzan_gui_static.a" "$DRV/static/libzan_gui.a"
echo "== ohos-x64: staged -> $DRV/libzan_gui.so + $DRV/static/libzan_gui.a"
