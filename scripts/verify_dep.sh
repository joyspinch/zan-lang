#!/bin/sh
# Verify a downloaded third-party archive against the pinned checksums in
# deps/checksums.txt. The download must be in the current directory and the
# script must run from the repository root (workflow steps do).
#
# Usage: verify_dep.sh <filename>
#
# The exact <sha256>  <filename> line is looked up (not the whole file, so
# unrelated archives can be absent), then re-checked with `sha256sum -c` so
# the hash comparison is done by coreutils. macOS has no sha256sum, so it
# falls back to `shasum -a 256 -c`, which reads the same format.
set -eu

FILE=$1
CHK=deps/checksums.txt

[ -f "$FILE" ] || { echo "verify_dep: $FILE not found in $(pwd)" >&2; exit 1; }
[ -f "$CHK" ]   || { echo "verify_dep: $CHK not found in $(pwd)" >&2; exit 1; }

ENTRY=$(awk -v f="$FILE" 'NF == 2 && $1 ~ /^[0-9a-f]{64}$/ && $2 == f { print $0 }' "$CHK")
if [ -z "$ENTRY" ]; then
  echo "verify_dep: no checksum entry for $FILE in $CHK" >&2
  exit 1
fi

if command -v sha256sum >/dev/null 2>&1; then
  printf '%s\n' "$ENTRY" | sha256sum -c -
else
  printf '%s\n' "$ENTRY" | shasum -a 256 -c -
fi
