#!/usr/bin/env bash
# Build the Lua runtime that System.Scripting.Lua dlopen()s and stage it into
# <repo>/stdlib/System/Scripting/drivers/<target>. Run on the matching native host
# (linux-x64 / linux-arm64 / macos-x64 / macos-arm64).
#
#   scripts/stage_lua.sh linux-x64
#
# Lua distributes source only; building it here keeps the 5.4 ABI identical to
# the Windows DLL produced by stage_lua.ps1.
set -euo pipefail

TARGET="${1:?usage: stage_lua.sh <linux-x64|linux-arm64|macos-x64|macos-arm64> [version]}"
VERSION="${2:-5.4.6}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/stdlib/System/Scripting/drivers/$TARGET"
WORK="${TMPDIR:-/tmp}/zan-lua-$VERSION"
SRC="$WORK/lua-$VERSION/src"

mkdir -p "$DEST" "$WORK"

if [ ! -f "$WORK/lua-$VERSION.tar.gz" ]; then
    curl -fsSL "https://www.lua.org/ftp/lua-$VERSION.tar.gz" \
        -o "$WORK/lua-$VERSION.tar.gz"
fi
[ -d "$SRC" ] || tar -xzf "$WORK/lua-$VERSION.tar.gz" -C "$WORK"

# lua.c / luac.c are the CLI drivers (each defines main()).
SOURCES=$(cd "$SRC" && ls *.c | grep -v -e '^lua\.c$' -e '^luac\.c$')

case "$TARGET" in
    macos-*)
        SONAME="liblua5.4.dylib"
        (cd "$SRC" && cc -O2 -fPIC -DLUA_USE_MACOSX -dynamiclib \
            -install_name "@rpath/$SONAME" -o "$DEST/$SONAME" $SOURCES)
        ;;
    linux-*)
        SONAME="liblua5.4.so.0"
        (cd "$SRC" && cc -O2 -fPIC -DLUA_USE_LINUX -shared \
            -Wl,-soname,"$SONAME" -o "$DEST/$SONAME" $SOURCES -lm -ldl)
        ln -sf "$SONAME" "$DEST/liblua5.4.so"
        ;;
    *)
        echo "unknown target: $TARGET" >&2
        exit 2
        ;;
esac

# The bundle manifest is what zanc copies next to a published executable.
printf '%s\n' "$SONAME" > "$DEST/lua.bundle"

# Dev convenience: a program looks beside its own exe, which for repo builds is
# build/.
mkdir -p "$ROOT/build"
cp "$DEST/$SONAME" "$ROOT/build/"

echo "STAGE_LUA_OK $DEST/$SONAME"
