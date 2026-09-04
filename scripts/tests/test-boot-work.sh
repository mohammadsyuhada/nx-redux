#!/bin/sh
# shellcheck disable=SC1090
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HELPER="$ROOT/skeleton/SYSTEM/shared/bin/boot-work.sh"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

USERDATA_PATH="$TMP/userdata"
SHARED_USERDATA_PATH="$TMP/shared"
NX_POWEROFF_PATH="$TMP/poweroff"
NX_REBOOT_PATH="$TMP/reboot"
export NX_POWEROFF_PATH NX_REBOOT_PATH
mkdir -p "$USERDATA_PATH" "$SHARED_USERDATA_PATH/.minui"

worker() {
	echo ran >> "$TMP/work"
}

# Normal boots run work asynchronously.
. "$HELPER"
nx_run_boot_work 0 worker
wait
[ "$(cat "$TMP/work")" = "ran" ] || {
	echo "FAIL: deferred worker did not run" >&2
	exit 1
}

# A pending shutdown suppresses delayed work.
: > "$NX_POWEROFF_PATH"
rm -f "$TMP/work"
nx_run_boot_work 0 worker
wait
[ ! -e "$TMP/work" ] || {
	echo "FAIL: worker ran after poweroff" >&2
	exit 1
}
rm -f "$NX_POWEROFF_PATH"

# auto.sh keeps the historical synchronous ordering.
: > "$USERDATA_PATH/auto.sh"
. "$HELPER"
rm -f "$TMP/work"
nx_run_boot_work 30 worker
[ "$(cat "$TMP/work")" = "ran" ] || {
	echo "FAIL: auto.sh worker was not synchronous" >&2
	exit 1
}

# Auto-resume has the same ordering requirement.
rm -f "$USERDATA_PATH/auto.sh"
: > "$SHARED_USERDATA_PATH/.minui/auto_resume.txt"
. "$HELPER"
rm -f "$TMP/work"
nx_run_boot_work 30 worker
[ "$(cat "$TMP/work")" = "ran" ] || {
	echo "FAIL: auto-resume worker was not synchronous" >&2
	exit 1
}

printf 'PASS: boot-work.sh\n'
