#!/bin/sh
# Host-side unit tests for the gpSP lockstep link state machine. Run from anywhere.
set -e
D=$(cd "$(dirname "$0")/.." && pwd)
OUT=${TMPDIR:-/tmp}/test_sio_lockstep
cc -std=gnu99 -Wall -Wextra -Werror -I"$D" "$D/sio_lockstep.c" "$D/tests/test_sio_lockstep.c" -o "$OUT"
"$OUT"
