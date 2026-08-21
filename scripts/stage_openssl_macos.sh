#!/usr/bin/env bash
# Stage the macOS OpenSSL 3 runtime closure into
# stdlib/System/Net/Tls/drivers/<target>/.
#
# The committed OpenSSL dylibs are the same Homebrew OpenSSL 3 pair taken from
# the Postgres driver closure. This script refreshes that pair from brew,
# rewrites Homebrew dependencies to @rpath, ad-hoc signs the result, and writes
# the ssl.bundle and crypto.bundle manifests.
# bash 3.2 safe (macOS /bin/bash). Run on the matching native runner.
set -euo pipefail

TARGET="${1:?usage: stage_openssl_macos.sh <macos-arm64|macos-x64>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/stdlib/System/Net/Tls/drivers/$TARGET"
case "$TARGET" in
    macos-arm64) ARCH=arm64 ;;
    macos-x64)   ARCH=x86_64 ;;
    *) echo "unknown target '$TARGET' (expected macos-arm64 or macos-x64)" >&2; exit 2 ;;
esac

BREW="$(brew --prefix)"
BREW_OPENSSL="$(brew --prefix openssl@3)"
SRC_DIRS="$BREW_OPENSSL/lib $BREW/opt/openssl@3/lib"
mkdir -p "$DEST"

find_src() {
    local base="$1" d
    for d in $SRC_DIRS; do
        [ -f "$d/$base" ] && { echo "$d/$base"; return 0; }
    done
    return 1
}

SEEN=" "
QUEUE="libssl.3.dylib libcrypto.3.dylib"
while :; do
    set -- $QUEUE
    [ $# -eq 0 ] && break
    cur="$1"; shift; QUEUE="$*"
    case "$SEEN" in *" $cur "*) continue ;; esac
    SEEN="$SEEN$cur "
    src="$(find_src "$cur")" || { echo "MISSING source for $cur" >&2; exit 3; }
    cp -f "$src" "$DEST/$cur"
    chmod u+w "$DEST/$cur"
    for base in $(otool -L "$DEST/$cur" | tail -n +2 | awk '{print $1}' |
        while read -r dep; do
            case "$dep" in
                @@HOMEBREW*|*/opt/*|"$BREW"/*) basename "$dep" ;;
            esac
        done); do
        find_src "$base" >/dev/null 2>&1 && QUEUE="$QUEUE $base"
    done
done

for f in $SEEN; do
    [ "$f" = " " ] && continue
    install_name_tool -id "@rpath/$f" "$DEST/$f"
    while read -r dep; do
        [ -n "$dep" ] || continue
        base="$(basename "$dep")"
        if [ -f "$DEST/$base" ]; then
            install_name_tool -change "$dep" "@rpath/$base" "$DEST/$f"
        fi
    done <<EOF
$(otool -L "$DEST/$f" | tail -n +2 | awk '{print $1}')
EOF
    codesign --force --sign - "$DEST/$f"
done

printf '%s\n' libssl.3.dylib libcrypto.3.dylib > "$DEST/ssl.bundle"
printf '%s\n' libcrypto.3.dylib > "$DEST/crypto.bundle"

echo "== staged $DEST =="; ls -la "$DEST"
echo "== architecture ($ARCH) =="; lipo -info "$DEST"/*.dylib
echo "== dylib dependencies =="; otool -L "$DEST/libssl.3.dylib"
otool -L "$DEST/libcrypto.3.dylib"
