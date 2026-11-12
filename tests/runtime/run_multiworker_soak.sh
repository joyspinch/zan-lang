#!/usr/bin/env bash
# Build once, then execute each worker count in a separate bounded process.
set -euo pipefail
mode=${1:-short}
sanitizer=${2:-none}
case "$mode" in
  short) rounds=${ZAN_SOAK_ROUNDS:-8}; burst=${ZAN_SOAK_BURST:-128}; deadline=${ZAN_SOAK_DEADLINE:-45} ;;
  release) rounds=${ZAN_SOAK_ROUNDS:-1000}; burst=${ZAN_SOAK_BURST:-512}; deadline=${ZAN_SOAK_DEADLINE:-600} ;;
  *) printf 'unknown mode: %s\n' "$mode" >&2; exit 2 ;;
esac
# Validate before using the deadline in shell arithmetic or timeout.
for value in "$rounds" "$burst" "$deadline"; do
  if [[ ! $value =~ ^[1-9][0-9]{0,4}$ ]]; then
    printf 'invalid positive soak parameter: %s\n' "$value" >&2; exit 2
  fi
done
if (( rounds > 10000 || burst > 2048 || deadline > 1800 )); then
  printf 'soak parameters exceed resource limits\n' >&2; exit 2
fi
case "$sanitizer" in
  none) flags=(-O2) ;;
  asan) flags=(-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer) ;;
  tsan) flags=(-O1 -g -fsanitize=thread -fno-sanitize-recover=all -fno-omit-frame-pointer) ;;
  *) printf 'unknown sanitizer: %s\n' "$sanitizer" >&2; exit 2 ;;
esac
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
out="$repo/build/runtime-soak/$sanitizer"
mkdir -p "$out"
"${CC:-cc}" -std=c11 -D_GNU_SOURCE -DZAN_IO_STACKLESS_ONLY -DZAN_CO_DRIVER \
  -Wall -Wextra "${flags[@]}" -I"$repo" -I"$repo/src/runtime" \
  "$repo/tests/runtime/rt_multiworker_soak_test.c" \
  "$repo/src/runtime/rt_io.c" "$repo/src/runtime/rt_timer.c" \
  "$repo/src/runtime/rt_crash.c" -pthread -lrt -o "$out/rt_multiworker_soak"
"${CC:-cc}" -std=c11 -D_GNU_SOURCE -Wall -Wextra "${flags[@]}" \
  -I"$repo" -I"$repo/src/runtime" \
  "$repo/tests/runtime/rt_hash_salt_test.c" -pthread -lrt \
  -o "$out/rt_hash_salt_test"
# Bound native thread stacks and descriptor growth. ASan/TSan need large
# virtual address reservations, so no virtual-memory ulimit is imposed.
ulimit -s 2048
ulimit -n 1024
export ZAN_SOAK_ROUNDS="$rounds" ZAN_SOAK_BURST="$burst" ZAN_SOAK_DEADLINE="$deadline"
export ZAN_SOAK_PRODUCERS=${ZAN_SOAK_PRODUCERS:-3}
export ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=1:halt_on_error=1}
export UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}
export TSAN_OPTIONS=${TSAN_OPTIONS:-halt_on_error=1:exitcode=66}
timeout --signal=TERM --kill-after=5s 15s "$out/rt_hash_salt_test"
for workers in ${ZAN_SOAK_WORKERS:-2 4}; do
  printf 'RUN mode=%s sanitizer=%s workers=%s rounds=%s burst=%s deadline=%ss\n' \
    "$mode" "$sanitizer" "$workers" "$rounds" "$burst" "$deadline"
  ZAN_CO_WORKERS="$workers" timeout --signal=TERM --kill-after=5s \
    "$((deadline + 10))s" "$out/rt_multiworker_soak"
done
