#!/usr/bin/env bash
# Refresh the win-x64 MinGW bundle and the linux-x64 static GUI driver from a
# non-Windows/non-Linux host (macOS) so `zanc --target win-x64|linux-x64` links
# GUI programs. CI (drivers.yml) does not yet refresh toolchain/win-x64/mingw/lib
# or commit the Linux GUI archive; until it does, this is the repair path.
#
#   scripts/refresh_cross_bundles.sh [repo-root]
#
# What it does (background: docs/BUILD_TOOLCHAIN.md "win-x64 / linux-x64 交叉链接包"):
#   1. win-x64: replaces the mixed-generation MinGW bundle (TDM libsupc++ next
#      to an old CRT) with one consistent MSYS2 MINGW64 snapshot: GCC 16.2
#      runtime + winpthreads + mingw-w64 CRT, adds an __imp__set_errno thunk,
#      and builds the missing zanrt_crash.o
#   2. linux-x64: recompiles the stale gui_runtime / crash / fortify members of
#      drivers/linux-x64/static/libzan_gui.a for x86_64-linux-musl, keeping the
#      archive's X11/xcb system objects (replaced in place: libX11 and libXdmcp
#      both have a member named Flush.o, so never extract-and-rearchive)
# Afterwards rebuild zanc (ninja -C build zanc) so it re-stages toolchain/.
#
# Needs: zig (https://ziglang.org, 0.17+), llvm-ar/llvm-nm, curl, bsdtar with zstd.
set -euo pipefail

SDK=$(cd "${1:-$(dirname "$0")/..}" && pwd)
ZIG=${ZIG:-zig}
LLVM_BIN=${LLVM_BIN:-$(dirname "$(command -v llvm-ar || echo /usr/bin/llvm-ar)")}
AR="$LLVM_BIN/llvm-ar"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

fetch() { # url sha256 out
    curl -fsSL "$1" -o "$3"
    echo "$2  $3" | shasum -a 256 -c - >/dev/null || { echo "checksum mismatch: $1" >&2; exit 1; }
}

echo "== win-x64 MinGW bundle (MSYS2 MINGW64 snapshot)"
M=https://repo.msys2.org/mingw/mingw64
fetch $M/mingw-w64-x86_64-gcc-16.2.0-7-any.pkg.tar.zst \
    a804b652af7176620c99906e9e58d90f8447838a4489b6cedc451ad8492de201 "$WORK/gcc.zst"
fetch $M/mingw-w64-x86_64-winpthreads-14.0.0.r598.ge3d18511f-1-any.pkg.tar.zst \
    d875a6a969f01cc64caeebd337430d00528964123ce29c02873bb5c6c3c914b8 "$WORK/wp.zst"
fetch $M/mingw-w64-x86_64-crt-git-13.0.0.r380.gb83511db8-1-any.pkg.tar.zst \
    f2d7563f8684de1b8b65d22e6667570c1adc6a2fa63b91cdc7a94a2f0797ea35 "$WORK/crt.zst"
for p in gcc wp crt; do mkdir -p "$WORK/$p" && tar xf "$WORK/$p.zst" -C "$WORK/$p"; done
LIB="$SDK/toolchain/win-x64/mingw/lib"
for f in $(ls "$LIB"); do
    [ -f "$WORK/crt/mingw64/lib/$f" ] && cp "$WORK/crt/mingw64/lib/$f" "$LIB/$f"
done
G="$WORK/gcc/mingw64/lib/gcc/x86_64-w64-mingw32/16.2.0"
cp "$G/libgcc.a" "$G/libgcc_eh.a" "$G/crtbegin.o" "$G/crtend.o" "$LIB/"
cp "$WORK/gcc/mingw64/lib/libsupc++.a" "$LIB/"
cp "$WORK/wp/mingw64/lib/libwinpthread.a" "$LIB/"
"$ZIG" cc -target x86_64-windows-gnu -O2 -g0 -c "$SDK/toolchain/win-x64/mingw/zan_set_errno_imp.c" -o "$WORK/zan_set_errno_imp.o"
"$AR" q "$LIB/libwinpthread.a" "$WORK/zan_set_errno_imp.o" && "$AR" s "$LIB/libwinpthread.a"
"$ZIG" cc -target x86_64-windows-gnu -O2 -ffunction-sections -fdata-sections -g0 -std=c11 \
    -I "$SDK/src/runtime" -c "$SDK/src/runtime/rt_crash.c" -o "$SDK/toolchain/win-x64/zanrt_crash.o"

echo "== linux-x64 static GUI driver"
A=https://dl-cdn.alpinelinux.org/alpine/latest-stable/main/x86_64
mkdir -p "$WORK/alp"
fetch $A/libx11-dev-1.8.14-r0.apk e6f72a43c12c734fb76bee5e01856f42dbefc37cc4bacb52667fe31d858238fc "$WORK/x11.apk"
fetch $A/xorgproto-2025.1-r0.apk 8e43324c60aa227a1a335aef357dc2c0c26afcc536952f992b4a32293d230d76 "$WORK/proto.apk"
fetch $A/libxcb-dev-1.17.0-r2.apk 6658de0d8836c0e60744edf4e315bf006cd18d24f0539e58b3f70e2eae0b93c7 "$WORK/xcb.apk"
fetch $A/freetype-dev-2.14.3-r0.apk 423641ce2ebc18338c520ddabd64cd007d3f8a1e83b846d755e86f0a14addf3c "$WORK/ft.apk"
fetch $A/fontconfig-dev-2.17.1-r1.apk 5a2545628a11602590bc6131809d54cfa7d11ff6b746372869573c2f00c872c3 "$WORK/fc.apk"
for p in x11 proto xcb ft fc; do tar xzf "$WORK/$p.apk" -C "$WORK/alp" 2>/dev/null || true; done
INC="$WORK/alp/usr/include"
CF=(-target x86_64-linux-musl -O2 -g0 -DNDEBUG -ffunction-sections -fdata-sections -fPIC -std=gnu11
    -U_FORTIFY_SOURCE -DZAN_NO_EXECINFO)
"$ZIG" cc "${CF[@]}" -DZAN_GUI_STATIC -DZAN_GUI_FREETYPE -isystem "$INC" -isystem "$INC/freetype2" \
    -I"$SDK/src/runtime" -I"$SDK/src/drivers" -I"$SDK/src/third_party" \
    -c "$SDK/src/drivers/gui/gui_runtime.c" -o "$WORK/zan_gui_x64.o"
"$ZIG" cc "${CF[@]}" -I"$SDK/src/runtime" -c "$SDK/src/runtime/rt_crash.c" -o "$WORK/zan_crash_x64.o"
"$ZIG" cc -target x86_64-linux-musl -O2 -g0 -ffunction-sections -fdata-sections -fPIC -std=gnu11 -U_FORTIFY_SOURCE \
    -c "$SDK/src/common/zan_fortify_compat.c" -o "$WORK/zan_fortify_compat.o"
GUI_A="$SDK/packages/Zan.Gui/src/Gui/drivers/linux-x64/static/libzan_gui.a"
(cd "$WORK" && "$AR" r "$GUI_A" zan_gui_x64.o zan_crash_x64.o zan_fortify_compat.o && "$AR" s "$GUI_A")

echo "done. Now rebuild zanc in $SDK (e.g. ninja -C build zanc) to re-stage toolchain/."
