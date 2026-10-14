#!/bin/sh
# Run on the device against a staged helper before activation. Read-only BlueZ
# queries use disposable state; this never starts/stops/connects Bluetooth.
set -u
if [ "$#" -lt 1 ] || [ "$#" -gt 2 ] || [ ! -r "$1" ]; then
	echo "Usage: $0 STAGED_HELPER [EXPECTED_CONNECTED_AUDIO_MAC]" >&2
	exit 2
fi
helper=$1
expected=${2:-}
BT_RECONNECT_STATE=$(mktemp -d /tmp/nx-bt-preflight.XXXXXX) || exit 1
export BT_RECONNECT_STATE
cleanup() {
	exec 9>&-
	case "$BT_RECONNECT_STATE" in
		/tmp/nx-bt-preflight.*)
			rm -f "$BT_RECONNECT_STATE/audio" "$BT_RECONNECT_STATE/token" \
				"$BT_RECONNECT_STATE/lock" "$BT_RECONNECT_STATE/audio.$$" "$BT_RECONNECT_STATE/token.$$"
			rmdir "$BT_RECONNECT_STATE" 2>/dev/null || :
			;;
	esac
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP
. "$helper"
if [ -n "$expected" ] && ! bt_valid_addr "$expected"; then
	echo "Expected address is invalid" >&2
	exit 2
fi
if ! bt_toggle_begin; then
	echo "Helper capability/state initialization failed" >&2
	exit 1
fi
bt_capture_audio
selected=$(cat "$BT_RECONNECT_STATE/audio" 2>/dev/null || :)
if [ -n "$expected" ] && [ "$selected" != "$expected" ]; then
	echo "Expected connected bonded/trusted audio selection was not captured" >&2
	exit 1
fi
if [ -n "$selected" ]; then
	echo "Helper preflight passed; connected audio selection captured"
else
	echo "Helper preflight passed; no eligible connected audio selection"
fi
