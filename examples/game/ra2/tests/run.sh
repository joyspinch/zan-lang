#!/usr/bin/env bash
# Regression tests for the RA2 asset parsers. Each tests/<name>.zan is a
# standalone program whose stdout must match tests/expected_<name>.out.
#
# The parsers live in this example rather than stdlib, so --auto-stdlib cannot
# find them; every test is compiled against the whole formats/ directory. That
# is cheap and saves maintaining a per-test dependency list.
#
# No test may read data/ -- that is the user's own game install. Tests use
# published vectors and synthetic buffers only.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../../../.." && pwd)"
cd "$root"

zanc=build/zanc
[ -x "$zanc" ] || zanc=build/zanc.exe
if [ ! -x "$zanc" ]; then
  echo "no zanc at build/zanc -- run: cmake --build build" >&2
  exit 1
fi

pass=0
fail=0
for src in examples/game/ra2/tests/*.zan; do
  name="$(basename "$src" .zan)"
  expected="examples/game/ra2/tests/expected_$name.out"
  exe="build/ra2_test_$name.exe"

  if [ ! -f "$expected" ]; then
    echo "FAIL $name: no golden file $expected"
    fail=$((fail + 1))
    continue
  fi

  if ! out="$("$zanc" "$src" examples/game/ra2/formats/*.zan \
      examples/game/ra2/assets/*.zan --auto-stdlib -o "$exe" 2>&1)"; then
    echo "FAIL $name: compile error"
    echo "$out" | sed 's/^/    /'
    fail=$((fail + 1))
    continue
  fi

  if ! out="$("$exe" 2>&1)"; then
    echo "FAIL $name: exited nonzero"
    echo "$out" | sed 's/^/    /'
    fail=$((fail + 1))
    continue
  fi

  if diff -u --strip-trailing-cr "$expected" <(printf '%s\n' "$out") > /dev/null; then
    echo "ok   $name"
    pass=$((pass + 1))
  else
    echo "FAIL $name: output differs"
    diff -u --strip-trailing-cr "$expected" <(printf '%s\n' "$out") | sed 's/^/    /'
    fail=$((fail + 1))
  fi
done

echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
