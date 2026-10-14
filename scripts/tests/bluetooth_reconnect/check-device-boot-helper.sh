#!/bin/sh
# Exercise persistence and read-only BlueZ queries in disposable directories.
# Never invokes start/stop/connect/disconnect or writes the real preference.
set -u
[ "$#" -ge 1 ] && [ "$#" -le 2 ] && [ -r "$1" ] || exit 2
helper=$1
expected=${2:-}
root=$(mktemp -d /tmp/nx-bt-boot-preflight.XXXXXX) || exit 1
cleanup() {
	exec 7>&- 8>&- 9>&-
	rm -f "$root/state/boot-lock" "$root/state/pref-lock" "$root/state/boot-cancel" "$root/userdata/bt-boot-audio"
	rmdir "$root/state" "$root/userdata" "$root" 2>/dev/null || :
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP
mkdir "$root/state" "$root/userdata" || exit 1
BT_RECONNECT_STATE=$root/state
USERDATA_PATH=$root/userdata
export BT_RECONNECT_STATE USERDATA_PATH
. "$helper"
if bt_boot_enabled; then echo 'Fresh preference unexpectedly enabled' >&2; exit 1; fi
bt_boot_set on && bt_boot_enabled || { echo 'Enable/readback failed' >&2; exit 1; }
if [ -n "$expected" ] && [ "$bt_boot_addr" != "$expected" ]; then
	echo 'Expected current audio target was not seeded' >&2; exit 1
fi
selected=$bt_boot_addr
bt_boot_set off && bt_boot_read || { echo 'Disable/readback failed' >&2; exit 1; }
[ "$bt_boot_on" = 0 ] && [ "$bt_boot_addr" = "$selected" ] || exit 1
[ -e "$BT_RECONNECT_STATE/boot-cancel" ] || exit 1
echo 'Boot helper preflight passed: default Off, seed, On/Off persistence, retained target, cancellation marker'
