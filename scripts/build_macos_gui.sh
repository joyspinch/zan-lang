#!/usr/bin/env bash
# Build the Cocoa GUI driver (libzan_gui.dylib) on a Mac.
#
# This is the one driver that can only be built on macOS: it is Objective-C
# against Cocoa/WebKit. `zanc --target macos-*` cross links GUI programs
# against the copy committed under stdlib/Gui/drivers/, so any newly added
# zan_gui_* export only becomes usable after this script has refreshed it --
# it is the same command the drivers workflow runs (.github/workflows/
# drivers.yml, "Stage zan_gui driver"), kept here so it can be run by hand.
#
# Usage: scripts/build_macos_gui.sh [macos-arm64|macos-x64|both]   (default both)
set -euo pipefail

cd "$(dirname "$0")/.."

if [ "$(uname -s)" != "Darwin" ]; then
    echo "build_macos_gui.sh must run on macOS (needs Cocoa/WebKit headers)" >&2
    exit 1
fi

want="${1:-both}"

build() {
    target="$1"
    arch_flag="$2"
    dest="stdlib/Gui/drivers/$target"
    mkdir -p "$dest"
    # shellcheck disable=SC2086
    xcrun clang -O2 -fPIC -dynamiclib $arch_flag -mmacosx-version-min=11.0 \
        -install_name @rpath/libzan_gui.dylib -o "$dest/libzan_gui.dylib" \
        -DZAN_GUI_COCOA src/runtime/gui_runtime.c src/runtime/gui_runtime_mac.m \
        -lm -framework Cocoa -framework CoreText -framework QuartzCore \
        -framework IOSurface -framework WebKit
    codesign --force --sign - "$dest/libzan_gui.dylib"
    printf 'libzan_gui.dylib\n' > "$dest/zan_gui.bundle"
    echo "built $dest/libzan_gui.dylib"
    # A missing export is the usual cause of "cross link cannot find
    # zan_gui_*", so show what the WebView bridge got.
    nm -gU "$dest/libzan_gui.dylib" | grep _zan_gui_webview_ || true
}

case "$want" in
    macos-arm64) build macos-arm64 "-arch arm64" ;;
    macos-x64)   build macos-x64 "-arch x86_64" ;;
    both)
        build macos-arm64 "-arch arm64"
        build macos-x64 "-arch x86_64"
        ;;
    *)
        echo "usage: $0 [macos-arm64|macos-x64|both]" >&2
        exit 2
        ;;
esac
