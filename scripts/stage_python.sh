#!/usr/bin/env bash
# Stage a self-contained CPython into
# <repo>/stdlib/System/Scripting/drivers/<target> so a published program using
# System.Scripting.Python runs without a system Python.
# Run on the matching native host:
#
#   scripts/stage_python.sh linux-x64            # uses `python3` on PATH
#   scripts/stage_python.sh macos-arm64 python3.12
#
# python.org publishes an embeddable package for Windows only, so on POSIX the
# bundle is assembled from an existing installation: libpythonX.Y.so/.dylib plus
# its standard library and extension modules (Py_Initialize needs the stdlib;
# the shared library alone cannot import anything).
set -euo pipefail

TARGET="${1:?usage: stage_python.sh <linux-x64|linux-arm64|macos-x64|macos-arm64> [python]}"
PY="${2:-python3}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/stdlib/System/Scripting/drivers/$TARGET"

command -v "$PY" >/dev/null || { echo "$PY not found" >&2; exit 2; }

VER=$("$PY" -c 'import sys; print("%d.%d" % sys.version_info[:2])')
LIBDIR=$("$PY" -c 'import sysconfig; print(sysconfig.get_config_var("LIBDIR") or "")')
STDLIB=$("$PY" -c 'import sysconfig; print(sysconfig.get_path("stdlib"))')
LDLIB=$("$PY" -c 'import sysconfig; print(sysconfig.get_config_var("LDLIBRARY") or "")')

if [ -z "$LDLIB" ] || [ ! -f "$LIBDIR/$LDLIB" ]; then
    echo "shared libpython not found (need a --enable-shared build; " \
         "e.g. apt install libpython$VER-dev / brew install python@$VER)" >&2
    exit 3
fi

mkdir -p "$DEST/lib/python$VER"
cp -L "$LIBDIR/$LDLIB" "$DEST/$LDLIB"

case "$TARGET" in
    linux-*)
        # Keep the SONAME copy Python.zan looks for first.
        [ -e "$DEST/libpython$VER.so.1.0" ] || \
            ln -sf "$LDLIB" "$DEST/libpython$VER.so.1.0"
        ;;
    macos-*)
        [ -e "$DEST/libpython$VER.dylib" ] || \
            ln -sf "$LDLIB" "$DEST/libpython$VER.dylib"
        ;;
esac

# PYTHONHOME=<dest> makes CPython look for the stdlib in <dest>/lib/pythonX.Y.
# Test trees and caches are dropped: they are large and never imported.
( cd "$STDLIB" && tar -cf - \
    --exclude 'test' --exclude 'tests' --exclude 'idlelib' \
    --exclude 'tkinter' --exclude '__pycache__' . ) | \
  ( cd "$DEST/lib/python$VER" && tar -xf - )

# zanc's bundle manifest copies bare filenames only, so it carries the shared
# library; the stdlib tree under lib/pythonX.Y has to travel with the published
# program as a directory (cp -R it next to the executable, as done here for dev
# builds), unlike Windows where the whole payload is flat.
printf '%s\n' "$LDLIB" > "$DEST/python.bundle"

mkdir -p "$ROOT/build"
cp -L "$DEST/$LDLIB" "$ROOT/build/"
cp -R "$DEST/lib" "$ROOT/build/"

echo "STAGE_PYTHON_OK $DEST (python $VER)"
