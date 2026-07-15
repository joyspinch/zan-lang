#!/bin/bash
# Assemble a self-contained macos-arm64 libpq bundle: copy libpq + its Homebrew
# openssl/krb5 dependency closure, then rewrite every dylib id and inter-dep
# reference to @loader_path/<basename> so they load from beside the executable.
set -e
BREW=/Users/qq/.homebrew/opt
OUT=/tmp/pqbundle
rm -rf "$OUT"; mkdir -p "$OUT"
cd "$OUT"

# copy the closure (follow symlinks -> real files, keep referenced basenames)
cp -L "$BREW/libpq/lib/libpq.5.dylib"               libpq.5.dylib
cp -L "$BREW/openssl@3/lib/libssl.3.dylib"          libssl.3.dylib
cp -L "$BREW/openssl@3/lib/libcrypto.3.dylib"       libcrypto.3.dylib
cp -L "$BREW/krb5/lib/libgssapi_krb5.2.2.dylib"     libgssapi_krb5.2.2.dylib
cp -L "$BREW/krb5/lib/libkrb5.3.3.dylib"            libkrb5.3.3.dylib
cp -L "$BREW/krb5/lib/libk5crypto.3.1.dylib"        libk5crypto.3.1.dylib
cp -L "$BREW/krb5/lib/libcom_err.3.0.dylib"         libcom_err.3.0.dylib
cp -L "$BREW/krb5/lib/libkrb5support.1.1.dylib"     libkrb5support.1.1.dylib
chmod u+w *.dylib

for f in *.dylib; do
  install_name_tool -id "@loader_path/$f" "$f"
  # rewrite any dependency that lives under Homebrew to @loader_path/<basename>
  otool -L "$f" | tail -n +2 | awk '{print $1}' | while read dep; do
    case "$dep" in
      /Users/qq/.homebrew/*)
        base=$(basename "$dep")
        install_name_tool -change "$dep" "@loader_path/$base" "$f"
        ;;
    esac
  done
done

echo "=== resulting deps (should be @loader_path or /usr/lib only) ==="
for f in *.dylib; do echo "-- $f"; otool -L "$f" | tail -n +2; done
echo "=== files ==="; ls -la
