#!/bin/sh
# Host-side unit tests for usblink's pure modules. Run from anywhere.
set -e
D=$(cd "$(dirname "$0")/.." && pwd)
OUT=${TMPDIR:-/tmp}/test_usblink
cc -std=gnu99 -Wall -Wextra -Werror -I"$D" \
	"$D/usblink_frame.c" "$D/usblink_link.c" "$D/usblink_state.c" "$D/usblink_sio.c" "$D/usblink_power.c" \
	"$D/tests/test_usblink.c" -o "$OUT"
"$OUT"
