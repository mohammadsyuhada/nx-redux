#!/bin/sh
# shellcheck disable=SC1090,SC2034
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HELPER="$ROOT/skeleton/SYSTEM/shared/bin/device-info.sh"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

check_device() {
	platform=$1
	device_id=$2
	expected_device=$3
	expected_model=$4
	printf '%s\n' "$device_id" > "$TMP/.nx-device"
	(
		PLATFORM=$platform
		SDCARD_PATH=$TMP
		NX_DEVICE_FILE="$TMP/.nx-device"
		. "$HELPER"
		[ "$DEVICE" = "$expected_device" ]
		[ "$TRIMUI_MODEL" = "$expected_model" ]
		[ "$NX_DEVICE_ID" = "$device_id" ]
	) || {
		echo "FAIL: $device_id" >&2
		exit 1
	}
}

check_device tg5040 tg5040-brick brick "Trimui Brick"
check_device tg5040 tg5040-brickpro brickpro "Trimui Brick Pro"
check_device tg5040 tg5040-smartpro smartpro "Trimui Smart Pro"
check_device tg5050 tg5050-smartpros smartpros "Trimui Smart Pro S"

# Before the first extraction, firmware model detection remains available.
rm -f "$TMP/.nx-device"
(
	PLATFORM=tg5040
	SDCARD_PATH=$TMP
	NX_DEVICE_FILE="$TMP/.nx-device"
	NX_MAINUI_MODEL="Trimui Brick Pro"
	. "$HELPER"
	[ "$DEVICE" = "brickpro" ]
) || {
	echo "FAIL: first-boot model fallback" >&2
	exit 1
}

printf 'PASS: device-info.sh\n'
