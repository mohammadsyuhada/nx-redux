#!/usr/bin/env bash
# Host unit test for minarch's pure cheat matcher (ma_cheat_match.c).
# No device toolchain: compiles with the host cc and runs.
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/workspace/all/minarch"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cc -std=gnu99 -Wall -Wextra -I"$SRC" \
   "$SRC/ma_cheat_match.c" "$SRC/tests/test_cheat_match.c" \
   -o "$TMP/test_cheat_match"
"$TMP/test_cheat_match"
