#!/bin/sh
# Cross-build the macOS CEF driver (stdlib/Gui/Component/CefBrowser) without a
# Mac and without an Apple SDK: `zig cc` carries the Darwin libc headers, and
# zan_cef.c only imports libSystem symbols (AppKit is reached through dlopen'd
# libobjc at run time), so the same trick scripts/build_macos_rt.sh uses works
# here too.
#
#   scripts/build_macos_cef.sh <cef-include-root> [path-to-zig]
#
# <cef-include-root> is a directory containing the CEF distribution's include/
# tree of the pinned current branch (cmake unpacks one into
# build/cef-headers/current when zan_cef is configured; the macOS archive
# CefRuntime.zan pins carries an identical one). The CEF API version comes from
# cmake/ZanCef.cmake so the driver keeps loading every runtime whose supported
# range covers it -- writing the number twice is exactly how the earlier
# "cef api hash mismatch" happened.
#
# Outputs, into stdlib/Gui/Component/CefBrowser/drivers/macos-{arm64,x64} -- the
# directories zanc bundles from (zan_driver_subdir() in src/compiler/main.c):
#   libzan_cef.dylib   the driver
#   zan_cef_helper     the Chromium subprocess executable of macOS bundles
#                      (native/zan_cef_helper.c: no CEF headers, just dlopen)
#
# CEF 109 is deliberately not built here: CefRuntime.zan pins no macOS 109
# archive (that branch exists for Windows 7/8), so a macOS zan_cef109 could
# never be selected.
set -eu

if [ $# -lt 1 ]; then
    echo "usage: $0 <cef-include-root> [path-to-zig]" >&2
    exit 2
fi

INC=$1
ZIG=${2:-zig}
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SRC="$ROOT/stdlib/Gui/Component/CefBrowser/native/zan_cef.c"
HELPER_SRC="$ROOT/stdlib/Gui/Component/CefBrowser/native/zan_cef_helper.c"

if [ ! -f "$INC/include/cef_api_hash.h" ]; then
    echo "$INC does not look like a CEF distribution (no include/cef_api_hash.h)" >&2
    exit 1
fi

API=$(sed -n 's/^set(ZAN_CEF_API_VERSION "\([0-9]*\)".*/\1/p' \
      "$ROOT/cmake/ZanCef.cmake" | head -1)
if [ -z "$API" ]; then
    echo "cannot read ZAN_CEF_API_VERSION from cmake/ZanCef.cmake" >&2
    exit 1
fi

for pair in arm64:aarch64 x64:x86_64; do
    sub=${pair%%:*}
    arch=${pair#*:}
    out="$ROOT/stdlib/Gui/Component/CefBrowser/drivers/macos-$sub"
    mkdir -p "$out"
    # .11.0 stamps LC_BUILD_VERSION minos 11.0, matching what zanc passes to
    # ld64.lld for macOS targets. -g0: DWARF would embed the build directory,
    # so the bytes would differ per machine for no benefit.
    "$ZIG" cc -target "$arch-macos.11.0" -dynamiclib -O2 -g0 \
        -fvisibility=hidden -DCEF_API_VERSION="$API" -I"$INC" \
        -Wl,-install_name,@rpath/libzan_cef.dylib \
        -o "$out/libzan_cef.dylib" "$SRC"
    echo "built $out/libzan_cef.dylib (CEF_API_VERSION=$API)"
    "$ZIG" cc -target "$arch-macos.11.0" -O2 -g0 \
        -o "$out/zan_cef_helper" "$HELPER_SRC"
    chmod 755 "$out/zan_cef_helper"
    echo "built $out/zan_cef_helper"
done
