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
for exe in zanc zan-lsp zan-dap zanfmt zandoc; do
    [ -f "build/$exe" ] && cp "build/$exe" "$stage/toolchain/"
done

# --emit-lib needs an indexed archive writer for this SDK's host, regardless
# of the archive's target. Prefer the LLVM installation used by the build.
llvm_bin="${LLVM_TOOLS_BINARY_DIR:-}"
llvm_dir="${LLVM_DIR:-}"
configured_cc=""
if [ -f build/CMakeCache.txt ]; then
    while IFS= read -r entry; do
        case "$entry" in
            LLVM_TOOLS_BINARY_DIR:*=*) llvm_bin="${entry#*=}" ;;
            LLVM_DIR:*=*) llvm_dir="${entry#*=}" ;;
            CMAKE_C_COMPILER:*=*) configured_cc="${entry#*=}" ;;
        esac
    done < build/CMakeCache.txt
fi
llvm_ar_candidates=("build/llvm-ar")
[ -z "$llvm_bin" ] || llvm_ar_candidates+=("$llvm_bin/llvm-ar")
if [ -n "$llvm_dir" ]; then
    llvm_ar_candidates+=("$llvm_dir/../bin/llvm-ar" "$llvm_dir/../../../bin/llvm-ar")
fi
[ -z "$configured_cc" ] || llvm_ar_candidates+=("${configured_cc%/*}/llvm-ar")
[ -z "${LLVM_ROOT:-}" ] || llvm_ar_candidates+=("$LLVM_ROOT/bin/llvm-ar")
[ -z "${LLVM_PATH:-}" ] || llvm_ar_candidates+=("$LLVM_PATH/bin/llvm-ar")
if command -v llvm-ar >/dev/null 2>&1; then
    llvm_ar_candidates+=("$(command -v llvm-ar)")
fi
llvm_ar=""
for candidate in "${llvm_ar_candidates[@]}"; do
    if [ -x "$candidate" ] && [ -f "$candidate" ]; then
        llvm_ar="$candidate"
        break
    fi
done
if [ -z "$llvm_ar" ]; then
    echo "package_bundle: host llvm-ar missing; install the configured LLVM tools" >&2
    exit 1
fi
cp "$llvm_ar" "$stage/toolchain/llvm-ar"

# cross sysroot + runtime objects that travel next to zanc
for sys in linux-musl linux-arm64 win-x64 win-arm64 wasm32 riscv64 macos ohos-x64 ohos-arm64; do
    [ -d "build/$sys" ] && cp -r "build/$sys" "$stage/toolchain/"
done
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
