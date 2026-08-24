#!/usr/bin/env bash
# Build static PostgreSQL/libpq archives for a target supported by Zan.
#
# The resulting libpq archive is merged with PostgreSQL's frontend common and
# port archives because libpq.a alone leaves those symbols unresolved. Linux
# also builds the matching static OpenSSL archives. Set DRVBUILD to a working
# directory containing the unpacked OpenSSL and PostgreSQL sources, or pass
# OPENSSL_SRC/PG_SRC/OPENSSL_PREFIX explicitly.
set -euo pipefail

TARGET="${1:?usage: build_pq_static.sh <win-x64|linux-x64>}"
case "$TARGET" in
  win-x64|linux-x64) ;;
  *) echo "error: unsupported target '$TARGET' (use win-x64 or linux-x64)" >&2; exit 2 ;;
esac

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DRVBUILD="${DRVBUILD:-$HOME/drvbuild}"
OPENSSL_SRC="${OPENSSL_SRC:-$DRVBUILD/src/openssl-3.3.2}"
PG_SRC="${PG_SRC:-$DRVBUILD/src/postgresql-16.4}"
OPENSSL_PREFIX="${OPENSSL_PREFIX:-$DRVBUILD/out/$([[ "$TARGET" == win-x64 ]] && echo mingw-x64 || echo musl-x64)}"
WORK="${WORK:-$DRVBUILD/pq-$TARGET}"
AR="${AR:-}"

need_cmd() {
  command -v "$1" >/dev/null 2>&1 ||
    { echo "error: required tool '$1' was not found in PATH" >&2; exit 3; }
}
need_dir() {
  [ -d "$1" ] || { echo "error: required directory not found: $1" >&2; exit 3; }
}
need_file() {
  [ -f "$1" ] || { echo "error: required file not found: $1" >&2; exit 3; }
}

need_dir "$OPENSSL_SRC"
need_dir "$PG_SRC"
need_cmd make
need_cmd perl

if [[ "$TARGET" == win-x64 ]]; then
  HOST=x86_64-w64-mingw32
  CC="${CC:-$HOST-gcc}"
  AR="${AR:-$HOST-ar}"
  RANLIB="${RANLIB:-$HOST-ranlib}"
  need_cmd "$CC"; need_cmd "$AR"; need_cmd "$RANLIB"
  OPENSSL_LIBDIR="${OPENSSL_LIBDIR:-$OPENSSL_PREFIX/lib64}"
  PG_LIBS="${PG_LIBS:--lcrypt32 -lws2_32}"
  OPENSSL_CONFIG_TARGET=mingw64
else
  HOST=
  CC="${CC:-musl-gcc}"
  AR="${AR:-ar}"
  RANLIB="${RANLIB:-ranlib}"
  need_cmd "$CC"; need_cmd "$AR"; need_cmd "$RANLIB"
  OPENSSL_LIBDIR="${OPENSSL_LIBDIR:-$OPENSSL_PREFIX/lib64}"
  PG_LIBS="${PG_LIBS:-}"
  OPENSSL_CONFIG_TARGET=linux-x86_64
fi

mkdir -p "$WORK"
if [ ! -f "$OPENSSL_LIBDIR/libssl.a" ] || [ ! -f "$OPENSSL_LIBDIR/libcrypto.a" ]; then
  need_cmd make
  if [ ! -f "$OPENSSL_SRC/Makefile" ]; then
    openssl_args=("$OPENSSL_CONFIG_TARGET" no-shared no-dso no-tests no-apps no-docs
                   "--prefix=$OPENSSL_PREFIX")
    if [[ "$TARGET" == win-x64 ]]; then
      openssl_args+=("--cross-compile-prefix=$HOST-")
    fi
    (cd "$OPENSSL_SRC" && ./Configure "${openssl_args[@]}" )
  fi
  make -C "$OPENSSL_SRC" build_libs
  make -C "$OPENSSL_SRC" install_dev
fi
need_file "$OPENSSL_LIBDIR/libssl.a"
need_file "$OPENSSL_LIBDIR/libcrypto.a"

PG_BUILD="$WORK/postgresql"
mkdir -p "$PG_BUILD"
if [ ! -f "$PG_BUILD/config.status" ]; then
  configure_args=(
    --without-readline --without-zlib --without-icu
    --without-gssapi --without-ldap --without-gnutls --with-openssl
  )
  if [[ "$TARGET" == win-x64 ]]; then
    configure_args+=(--host="$HOST")
  fi
  (
    cd "$PG_BUILD"
    CC="$CC" CPPFLAGS="-I$OPENSSL_PREFIX/include" \
      LDFLAGS="-L$OPENSSL_LIBDIR" LIBS="$PG_LIBS" \
      "$PG_SRC/configure" "${configure_args[@]}"
  )
fi

make -C "$PG_BUILD/src/interfaces/libpq" libpq.a

DEST="$ROOT/stdlib/System/Data/Postgres/drivers/$TARGET/static"
TLS_DEST="$ROOT/stdlib/System/Net/Tls/drivers/$TARGET/static"
mkdir -p "$DEST"
if [[ "$TARGET" == linux-x64 ]]; then
  mkdir -p "$TLS_DEST"
  install -m 0644 "$OPENSSL_LIBDIR/libssl.a" "$TLS_DEST/libssl.a"
  install -m 0644 "$OPENSSL_LIBDIR/libcrypto.a" "$TLS_DEST/libcrypto.a"
fi

# Merge libpq with frontend common/port objects, prefixing object names to avoid
# collisions between the three input archives.
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/o"
i=0
for archive in \
  "$PG_BUILD/src/interfaces/libpq/libpq.a" \
  "$PG_BUILD/src/common/libpgcommon_shlib.a" \
  "$PG_BUILD/src/port/libpgport_shlib.a"; do
  need_file "$archive"
  i=$((i + 1))
  dir="$tmp/x$i"
  mkdir -p "$dir"
  (cd "$dir" && "$AR" x "$archive")
  for object in "$dir"/*.o; do
    [ -f "$object" ] || continue
    cp "$object" "$tmp/o/p${i}_$(basename "$object")"
  done
done
"$AR" crs "$DEST/libpq.a" "$tmp"/o/*.o
command -v "$RANLIB" >/dev/null 2>&1 && "$RANLIB" "$DEST/libpq.a" || true

if [[ "$TARGET" == win-x64 ]]; then
  cat > "$DEST/pq.libs" <<'EOF'
# PostgreSQL 16.4 libpq, mingw-w64 x86_64 static build.
# OpenSSL is supplied by the target's existing OpenSSL 3.x static bundle.
ssl
crypto
shell32
ws2_32
crypt32
EOF
else
  cat > "$DEST/pq.libs" <<'EOF'
# PostgreSQL 16.4 libpq, musl x86_64 static build.
# OpenSSL 3.3.2 was built no-shared/no-dso with gssapi/ldap/gnutls disabled.
ssl
crypto
EOF
  cat > "$TLS_DEST/ssl.libs" <<'EOF'
# OpenSSL 3.3.2, musl x86_64, no-shared/no-dso/no-tests/no-apps/no-docs.
crypto
EOF
  cat > "$TLS_DEST/crypto.libs" <<'EOF'
# OpenSSL 3.3.2, musl x86_64, no-shared/no-dso/no-tests/no-apps/no-docs.
# No additional native libraries were required by the static link probe.
EOF
fi

echo "== built $TARGET =="
ls -l "$DEST/libpq.a"
ls -l "$DEST/pq.libs"
if [[ "$TARGET" == linux-x64 ]]; then
  ls -l "$TLS_DEST/libssl.a" "$TLS_DEST/libcrypto.a" \
    "$TLS_DEST/ssl.libs" "$TLS_DEST/crypto.libs"
fi
