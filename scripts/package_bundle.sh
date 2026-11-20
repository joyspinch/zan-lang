#!/usr/bin/env bash
# Assemble a self-contained Zan toolchain bundle for a POSIX target and produce
# out/<name>.tar.gz + .sha256.
#
# Usage: scripts/package_bundle.sh <os> <arch> [build-dir]
#   <os>       : linux | macos
#   <arch>     : x64 | arm64
#   [build-dir]: existing CMake build, relative to the worktree or absolute (default build)
#
# Run from a release Git worktree with the built toolchain.
# This ships the compiler + CLIs + stdlib/templates/examples as an SDK.
set -euo pipefail

os="${1:?usage: package_bundle.sh <os> <arch>}"
arch="${2:?usage: package_bundle.sh <os> <arch>}"
build="${3:-build}"
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

ver="$(tr -d '[:space:]' < VERSION)"
if [[ ! "$ver" =~ ^[0-9A-Za-z][0-9A-Za-z.+-]*$ ||
      ! "$os" =~ ^(linux|macos)$ || ! "$arch" =~ ^(x64|arm64)$ ]]; then
    echo "package_bundle: invalid version or platform: $ver $os $arch" >&2
    exit 1
fi
name="zan-ide-${ver}-${os}-${arch}"
umask 022
mkdir -p out
staging="$(mktemp -d out/.bundle.XXXXXXXX)"
trap 'rm -rf "$staging"' EXIT
stage="$staging/$name"
mkdir -p "$stage/toolchain"

# compiler + companion CLIs
for exe in zanc zan-mvc zan-lsp zan-dap zanfmt zandoc; do
    cp "$build/$exe" "$stage/toolchain/"
done

# --emit-lib needs an indexed archive writer for this SDK's host, regardless
# of the archive's target. Prefer the LLVM installation used by the build.
llvm_bin="${LLVM_TOOLS_BINARY_DIR:-}"
llvm_dir="${LLVM_DIR:-}"
configured_cc=""
if [ -f "$build/CMakeCache.txt" ]; then
    while IFS= read -r entry; do
        case "$entry" in
            LLVM_TOOLS_BINARY_DIR:*=*) llvm_bin="${entry#*=}" ;;
            LLVM_DIR:*=*) llvm_dir="${entry#*=}" ;;
            CMAKE_C_COMPILER:*=*) configured_cc="${entry#*=}" ;;
        esac
    done < "$build/CMakeCache.txt"
fi
llvm_ar_candidates=("$build/llvm-ar")
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

if [ "$os" = macos ]; then
    macos_args=(relocate-macos --sdk "$stage" --build "$build" --archiver "$llvm_ar")
    if [ -n "$llvm_dir" ] && [ -d "$llvm_dir/../.." ]; then
        macos_args+=(--native-dir "$llvm_dir/../..")
    fi
    if command -v brew >/dev/null 2>&1 && zstd_prefix="$(brew --prefix zstd)" && [ -d "$zstd_prefix/lib" ]; then
        macos_args+=(--native-dir "$zstd_prefix/lib")
    fi
    python3 -I -B scripts/sdk_stage_manifest.py "${macos_args[@]}"
fi

# cross sysroot + runtime objects that travel next to zanc
for sys in linux-musl linux-arm64 win-x64 win-arm64 wasm32 linux-riscv64 macos ohos-x64 ohos-arm64; do
    if [ -d "$build/$sys" ]; then
        cp -R "$build/$sys" "$stage/toolchain/"
    fi
done
cp "$build"/zanrt_*.o "$build/zan_embed_api.o" "$build/zan_inflate.o" "$build/libzancrypto.a" "$stage/toolchain/"

# Stage platform-neutral sources from the release Git worktree inventory.
python3 -I -B scripts/sdk_stage_manifest.py stage-posix-sources --repo "$root" --sdk "$stage"

cat > "$stage/README.txt" <<EOF
Zan toolchain ${ver} (${os}-${arch})
====================================
This bundle ships the Zan compiler and CLIs as an SDK:

  toolchain/zanc         the compiler        (toolchain/zanc --version)
  toolchain/zan-mvc      MVC command-line tooling
  toolchain/zan-lsp      language server
  toolchain/zan-dap      debug adapter
  toolchain/zanfmt       formatter
  toolchain/zandoc       doc generator
  stdlib/ templates/ examples/

Build and run a program with the host C toolchain (cc on PATH):
  /path/to/${name}/toolchain/zanc hello.zan -o hello --auto-stdlib
  ./hello

The graphical IDE (ZanIDE) ships on Windows.
EOF

archive="${name}.tar.gz"
python3 -I -B scripts/sdk_stage_manifest.py write-archive --sdk "$stage" --archive "$staging/$archive"
mv -f "$staging/$archive" "$staging/$archive.sha256" out/
echo "packaged out/$archive"
