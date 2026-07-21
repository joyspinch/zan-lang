#!/usr/bin/env bash
# Assemble a self-contained Zan toolchain bundle for a POSIX target and produce
# out/<name>.tar.gz + .sha256. Layout mirrors docs/RELEASE.md section 3.
#
# Usage: scripts/package_bundle.sh <os> <arch>
#   <os>   : linux | macos
#   <arch> : x64 | arm64
#
# The graphical IDE (ZanIDE) is Windows-only for now (see docs/RELEASE.md 5.2),
# so this ships the compiler + CLIs + stdlib/templates/examples as an SDK.
set -euo pipefail

os="${1:?usage: package_bundle.sh <os> <arch>}"
arch="${2:?usage: package_bundle.sh <os> <arch>}"
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

ver="$(tr -d '[:space:]' < VERSION)"
name="zan-ide-${ver}-${os}-${arch}"
stage="out/${name}"
rm -rf "$stage"
mkdir -p "$stage/toolchain"

# compiler + companion CLIs
for exe in zanc zan-lsp zan-dap zanpkg zanfmt zandoc; do
    [ -f "build/$exe" ] && cp "build/$exe" "$stage/toolchain/"
done

# cross sysroot + runtime objects that travel next to zanc
[ -d build/linux-musl ] && cp -r build/linux-musl "$stage/toolchain/"
[ -d build/linux-arm64 ] && cp -r build/linux-arm64 "$stage/toolchain/"
for o in build/zanrt_*.o build/zanrt_*.obj; do
    [ -f "$o" ] && cp "$o" "$stage/toolchain/"
done

# platform-neutral sources
cp -r stdlib "$stage/stdlib"
cp -r templates "$stage/templates"
[ -d examples ] && cp -r examples "$stage/examples"
cp VERSION "$stage/VERSION"

cat > "$stage/README.txt" <<EOF
Zan toolchain ${ver} (${os}-${arch})
====================================
This bundle ships the Zan compiler and CLIs as an SDK:

  toolchain/zanc         the compiler        (toolchain/zanc --version)
  toolchain/zan-lsp      language server
  toolchain/zan-dap      debug adapter
  toolchain/zanpkg       package tool
  toolchain/zanfmt       formatter
  toolchain/zandoc       doc generator
  stdlib/ templates/ examples/

Build and run a program:
  toolchain/zanc hello.zan -o hello --auto-stdlib --stdlib-path stdlib
  ./hello

The graphical IDE (ZanIDE) is currently shipped on Windows only; the
cross-platform IDE build is tracked in docs/RELEASE.md (section 5.2).
EOF

tar -C out -czf "out/${name}.tar.gz" "${name}"
(
    cd out
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "${name}.tar.gz"
    else
        shasum -a 256 "${name}.tar.gz"
    fi > "${name}.tar.gz.sha256"
)
rm -rf "$stage"
echo "packaged out/${name}.tar.gz"
