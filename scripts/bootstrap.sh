#!/usr/bin/env bash
# Reproduce the self-hosting closure on POSIX systems.
#
#   gen0  = the C host compiler (build/zanc), already built via CMake
#   gen1  = gen0 compiling src/selfhost/*.zan into a native executable
#   g2.ll = gen1 compiling its own source to LLVM IR text
#   gen2  = clang linking g2.ll into a native executable
#   g3.ll = gen2 compiling the same source to LLVM IR text
#
# Success criterion: g2.ll and g3.ll are byte-identical (gen2 == gen3).
#
# Requires: a built build/zanc (gen0) and clang on PATH.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
ZANC="${ZANC:-$BUILD/zanc}"
CLANG="${CLANG:-clang}"

SRCS=(
  "$ROOT/src/selfhost/main.zan"
  "$ROOT/src/selfhost/irgen.zan"
  "$ROOT/src/selfhost/checker.zan"
  "$ROOT/src/selfhost/binder.zan"
  "$ROOT/src/selfhost/diag.zan"
  "$ROOT/src/selfhost/parser.zan"
  "$ROOT/src/selfhost/lexer.zan"
  "$ROOT/src/selfhost/ast.zan"
  "$ROOT/src/selfhost/token.zan"
)

echo "[1/5] gen0 -> gen1 (building the self-hosted compiler)"
"$ZANC" "${SRCS[@]}" -o "$BUILD/zanc1"

echo "[2/5] gen1 -> g2.ll (self-compile)"
"$BUILD/zanc1" "$BUILD/g2.ll" "${SRCS[@]}"

echo "[3/5] clang g2.ll -> gen2"
"$CLANG" "$BUILD/g2.ll" -o "$BUILD/zanc2"

echo "[4/5] gen2 -> g3.ll (self-compile)"
"$BUILD/zanc2" "$BUILD/g3.ll" "${SRCS[@]}"

echo "[5/5] compare g2.ll and g3.ll"
if cmp -s "$BUILD/g2.ll" "$BUILD/g3.ll"; then
  echo "SUCCESS: gen2 == gen3 (byte-identical, $(wc -c < "$BUILD/g2.ll") bytes)"
else
  echo "FAILURE: gen2 != gen3" >&2
  exit 1
fi
