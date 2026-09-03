#!/bin/bash
# Cross-build the Linux CEF driver (libzan_cef.so, current 151 branch) from a
# Windows/macOS developer box with zig cc, and stage it into the directories
# zanc reads for --publish --target linux-{x64,arm64}.
#
# Why this exists: CefBrowser's committed driver dirs carry no Linux binaries
# (same policy as the win dlls / mac dylibs -- built per machine), and a native
# Linux build via cmake/ZanCef.cmake needs a Linux host. The driver itself is
# portable C that dlopens libcef and libX11 at run time, so a zig cc
# cross-compile against the pinned CEF headers is enough.
#
# The libcef RUNTIME is not built here: CefRuntime.zan downloads the pinned
# linux64/linuxarm64 minimal package per machine at first run (the SHA-1 in
# its pinned table is this script's header source of truth too).
#
# Usage: scripts/build_cef_linux.sh [x64|arm64|all]
set -e
REPO=/d/project/zan-lang
ZIG="${ZIG:-/c/Users/QQ/.mozbuild/zig/zig-x86_64-windows-0.14.1/zig.exe}"
SRC="$REPO/stdlib/Gui/Component/CefBrowser/native/zan_cef.c"
HEADERS_ROOT="$REPO/build/cef-headers"
mkdir -p "$HEADERS_ROOT"

# Single source of truth for version + sha1: the pinned table in CefRuntime.zan
# (same place cmake/ZanCef.cmake reads). API version comes from ZanCef.cmake.
CEF_API="$(grep -o 'ZAN_CEF_API_VERSION "[0-9]*"' "$REPO/cmake/ZanCef.cmake" |
           grep -o '[0-9]*')"
BRANCH=$(grep -o 'CurrentBranch() { return "[0-9]*\.' \
          "$REPO/stdlib/Gui/Component/CefBrowser/CefRuntime.zan" |
          grep -o '[0-9]*')
LINE=$(grep -B1 '"linux64"' "$REPO/stdlib/Gui/Component/CefBrowser/CefRuntime.zan" |
       grep "\"$BRANCH\." | head -1)
VERSION=$(echo "$LINE" | grep -o '"[^"]*"' | head -1 | tr -d '"')
SHA1=$(grep -A2 '"linux64"' "$REPO/stdlib/Gui/Component/CefBrowser/CefRuntime.zan" |
       grep -o '[0-9a-f]\{40\}' | head -1)
if [ -z "$VERSION" ] || [ -z "$SHA1" ] || [ -z "$CEF_API" ]; then
    echo "cannot parse pinned CEF version/sha1/api from CefRuntime.zan / ZanCef.cmake" >&2
    exit 2
fi
echo "pinned CEF $VERSION (api $CEF_API, sha1 $SHA1)"

VDIR="linux64-$(echo "$VERSION" | tr '+' '-')"
H="$HEADERS_ROOT/$VDIR"
if [ ! -f "$H/include/cef_version.h" ]; then
    ARCHIVE="$HEADERS_ROOT/cef_linux64.tar.bz2"
    URL="https://cef-builds.spotifycdn.com/cef_binary_$(echo "$VERSION" |
         sed 's/+/%2B/g')_linux64_minimal.tar.bz2"
    echo "downloading $URL"
    curl -sS -L -o "$ARCHIVE" "$URL"
    echo "$SHA1  $ARCHIVE" | sha1sum -c -
    mkdir -p "$H.staging"
    tar -xjf "$ARCHIVE" -C "$H.staging"
    mv "$H.staging"/cef_binary_* "$H"
    rmdir "$H.staging"
    rm -f "$ARCHIVE"
fi

build () { # arch triple outdir
    local arch=$1 triple=$2 outdir=$3
    mkdir -p "$REPO/stdlib/Gui/Component/CefBrowser/drivers/$outdir"
    "$ZIG" cc -target "$triple" -shared -fPIC -O2 -g0 -I "$H" \
        -DCEF_API_VERSION="$CEF_API" "$SRC" \
        -o "$REPO/stdlib/Gui/Component/CefBrowser/drivers/$outdir/libzan_cef.so" \
        -lpthread -ldl -lm
    echo "== $outdir staged"
}

case "${1:-all}" in
  x64)   build x86_64 x86_64-linux-gnu linux-x64   ;;
  arm64) build aarch64 aarch64-linux-gnu linux-arm64 ;;
  all)   build x86_64 x86_64-linux-gnu linux-x64
         build aarch64 aarch64-linux-gnu linux-arm64 ;;
  *) echo "usage: $0 [x64|arm64|all]" >&2; exit 2 ;;
esac
