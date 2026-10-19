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
#
# The source list is read from scripts/selfhost_sources.txt -- the same file the
# ctest harnesses use -- so the closure always covers exactly the sources the
# project ships (a hand-copied list here used to lag behind and silently
# covered only a 9-file subset).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
ZANC="${ZANC:-$BUILD/zanc}"
CLANG="${CLANG:-clang}"
LIST="$ROOT/scripts/selfhost_sources.txt"

SRCS=()
while IFS= read -r line; do
  line="${line%%#*}"
  line="$(echo "$line" | tr -d '[:space:]')"
  [ -z "$line" ] && continue
  SRCS+=("$ROOT/src/${line}.zan")
done < "$LIST"

if [ "${#SRCS[@]}" -eq 0 ]; then
  echo "no sources listed in $LIST" >&2
  exit 1
fi

# gen2 links the same runtime the host links for an ordinary program.
RT_OBJS=()
for rt in rt_file rt_io rt_sched rt_timer rt_decimal rt_sync; do
  obj="$BUILD/${rt}.o"
  if [ ! -f "$obj" ]; then
    ${CC:-clang} -O2 -c "$ROOT/src/runtime/${rt}.c" -o "$obj"
  fi
  RT_OBJS+=("$obj")
done
if [ ! -f "$BUILD/zan_embed_api.o" ]; then
  ${CC:-clang} -O2 -c "$ROOT/src/runtime/zan_embed_api.c" -o "$BUILD/zan_embed_api.o"
fi
RT_OBJS+=("$BUILD/zan_embed_api.o")

echo "[1/5] gen0 -> gen1 (building the self-hosted compiler, ${#SRCS[@]} sources)"
"$ZANC" "${SRCS[@]}" -o "$BUILD/zanc1"

echo "[2/5] gen1 -> g2.ll (self-compile)"
"$BUILD/zanc1" "$BUILD/g2.ll" "${SRCS[@]}"

echo "[3/5] clang g2.ll -> gen2"
"$CLANG" "$BUILD/g2.ll" "${RT_OBJS[@]}" -ldl -lpthread -lm -o "$BUILD/zanc2"

echo "[4/5] gen2 -> g3.ll (self-compile)"
"$BUILD/zanc2" "$BUILD/g3.ll" "${SRCS[@]}"

echo "[5/5] compare g2.ll and g3.ll"
if cmp -s "$BUILD/g2.ll" "$BUILD/g3.ll"; then
  echo "SUCCESS: gen2 == g3 (byte-identical, $(wc -c < "$BUILD/g2.ll") bytes)"
else
  echo "FAILURE: gen2 != gen3" >&2
  exit 1
fi
