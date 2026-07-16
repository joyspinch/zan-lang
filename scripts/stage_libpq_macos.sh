#!/usr/bin/env bash
# Stage the macOS libpq runtime closure into
# stdlib/System/Data/Postgres/drivers/<target>/.
#
# Walks the dependency graph from libpq.5.dylib across the Homebrew keg dirs
# (libpq, openssl@3, krb5), copies each non-system dylib, rewrites every
# install name to @rpath/<basename>, ad-hoc re-signs, and writes pq.bundle.
# bash 3.2 safe (macOS /bin/bash). Run on the matching native runner.
set -euo pipefail

TARGET="${1:?usage: stage_libpq_macos.sh <macos-arm64|macos-x64>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/stdlib/System/Data/Postgres/drivers/$TARGET"
BREW="$(brew --prefix)"
SRC_DIRS="$BREW/opt/libpq/lib $BREW/opt/openssl@3/lib $BREW/opt/krb5/lib"
mkdir -p "$DEST"

find_src() {
    local base="$1" d
    for d in $SRC_DIRS; do
        [ -f "$d/$base" ] && { echo "$d/$base"; return 0; }
    done
    return 1
}
hb_deps() {
    otool -L "$1" | tail -n +2 | awk '{print $1}' | while read -r dep; do
        case "$dep" in @@HOMEBREW*|*/opt/*|"$BREW"/*) basename "$dep" ;; esac
    done
}

SEEN=" "
QUEUE="libpq.5.dylib"
while :; do
    set -- $QUEUE
    [ $# -eq 0 ] && break
    cur="$1"; shift; QUEUE="$*"
    case "$SEEN" in *" $cur "*) continue ;; esac
    SEEN="$SEEN$cur "
    src="$(find_src "$cur")" || { echo "MISSING source for $cur" >&2; exit 3; }
    cp -f "$src" "$DEST/$cur"; chmod u+w "$DEST/$cur"
    for base in $(hb_deps "$DEST/$cur"); do
        find_src "$base" >/dev/null 2>&1 && QUEUE="$QUEUE $base"
    done
done

for f in $SEEN; do
    install_name_tool -id "@rpath/$f" "$DEST/$f"
    otool -L "$DEST/$f" | tail -n +2 | awk '{print $1}' | while read -r dep; do
        case "$dep" in
            @@HOMEBREW*|*/opt/*|"$BREW"/*)
                base="$(basename "$dep")"
                case "$SEEN" in *" $base "*)
                    install_name_tool -change "$dep" "@rpath/$base" "$DEST/$f" ;;
                esac ;;
        esac
    done
    codesign --force --sign - "$DEST/$f"
done

{ echo libpq.5.dylib; for f in $SEEN; do [ "$f" != libpq.5.dylib ] && echo "$f"; done | sort; } > "$DEST/pq.bundle"

echo "== staged $DEST =="; ls -la "$DEST"
echo "== pq.bundle =="; cat "$DEST/pq.bundle"
lipo -info "$DEST"/*.dylib
otool -L "$DEST/libpq.5.dylib"
