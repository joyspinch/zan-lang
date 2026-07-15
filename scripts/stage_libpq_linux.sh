#!/usr/bin/env bash
# Stage the Linux libpq runtime closure into
# stdlib/System/Data/Postgres/drivers/<target>/.
#
# Resolves libpq.so.5 (from libpq-dev), walks its shared-library closure with
# ldd, copies every non-glibc/system dependency, sets each copy's rpath to
# $ORIGIN with patchelf, and writes pq.bundle. Run on the matching native
# runner (ubuntu-latest = x64, ubuntu-24.04-arm = arm64).
set -euo pipefail

TARGET="${1:?usage: stage_libpq_linux.sh <linux-x64|linux-arm64>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/stdlib/System/Data/Postgres/drivers/$TARGET"
mkdir -p "$DEST"

command -v patchelf >/dev/null || { echo "patchelf required" >&2; exit 2; }

# Locate libpq.so.5.
LIBPQ="$(ldconfig -p 2>/dev/null | awk '/libpq\.so\.5/{print $NF; exit}')"
[ -n "${LIBPQ:-}" ] || LIBPQ="$(find /usr/lib /usr/lib/* -name 'libpq.so.5*' 2>/dev/null | head -1)"
[ -n "${LIBPQ:-}" ] || { echo "libpq.so.5 not found (install libpq-dev)" >&2; exit 3; }

# System libraries that are always present on the target and must NOT be bundled.
is_system() {
    case "$1" in
        libc.so.*|libm.so.*|libdl.so.*|libpthread.so.*|librt.so.*|\
        libresolv.so.*|libutil.so.*|ld-linux*.so.*|linux-vdso.so.*|\
        libgcc_s.so.*|libstdc++.so.*|libnsl.so.*|libanl.so.*) return 0 ;;
    esac
    return 1
}

# BFS the closure with ldd.
SEEN=" "
QUEUE="$(basename "$LIBPQ")"
cp -L "$LIBPQ" "$DEST/$(basename "$LIBPQ")"
declare_paths() { ldd "$1" 2>/dev/null | awk '/=>/{print $3}'; }

while :; do
    set -- $QUEUE
    [ $# -eq 0 ] && break
    cur="$1"; shift; QUEUE="$*"
    case "$SEEN" in *" $cur "*) continue ;; esac
    SEEN="$SEEN$cur "
    [ -f "$DEST/$cur" ] || continue
    for p in $(declare_paths "$DEST/$cur"); do
        [ -e "$p" ] || continue
        b="$(basename "$p")"
        is_system "$b" && continue
        [ -f "$DEST/$b" ] || cp -L "$p" "$DEST/$b"
        QUEUE="$QUEUE $b"
    done
done

# Relocatable: rpath $ORIGIN so each lib finds its siblings next to the exe.
for f in $SEEN; do
    [ -f "$DEST/$f" ] || continue
    chmod u+w "$DEST/$f"
    patchelf --set-rpath '$ORIGIN' "$DEST/$f" 2>/dev/null || true
done

PQ="$(basename "$LIBPQ")"
{ echo "$PQ"; for f in $SEEN; do [ "$f" != "$PQ" ] && [ -f "$DEST/$f" ] && echo "$f"; done | sort; } > "$DEST/pq.bundle"

echo "== staged $DEST =="; ls -la "$DEST"
echo "== pq.bundle =="; cat "$DEST/pq.bundle"
echo "== ldd libpq =="; ldd "$DEST/$PQ" || true
