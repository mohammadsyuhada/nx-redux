#!/bin/sh
# Host-side unit tests for Device Sync's pure protocol module. Run from anywhere.
set -e
D=$(cd "$(dirname "$0")/.." && pwd)
OUT=${TMPDIR:-/tmp}/test_sync_proto
cc -std=gnu99 -Wall -Wextra -Werror -I"$D" "$D/sync_proto.c" "$D/tests/test_sync_proto.c" -o "$OUT"
"$OUT"
