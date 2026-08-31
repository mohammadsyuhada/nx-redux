#!/bin/sh
# Smoke-test the packaged .app with an isolated HOME. Asserts first-run
# seeding + launcher liveness + runtime roots from the log.
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ZIP="$(ls -t "$ROOT"/releases/NXRedux-*-macos-arm64.zip | head -1)"
WORK="$(mktemp -d)"
PID=""

# Runs on every exit path (success or any FAIL below) so a failed assertion
# never leaks a running nextui.elf behind the WORK dir we're about to rm -rf.
# Known pre-existing gotcha: an idle nextui.elf can enter a PWR sleep loop
# that ignores plain SIGTERM (battery stub always reports "charging", so
# suspend never powers off). Try SIGTERM first; escalate to SIGQUIT if it
# doesn't take within a second. Never SIGKILL -- it can wedge the process.
cleanup() {
	if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
		kill "$PID" 2>/dev/null || true
		sleep 1
		if kill -0 "$PID" 2>/dev/null; then
			echo "note: SIGTERM did not stop nextui, escalating to SIGQUIT"
			kill -QUIT "$PID" 2>/dev/null || true
			sleep 1
		fi
	fi
	rm -rf "$WORK"
}
trap cleanup EXIT

ditto -x -k "$ZIP" "$WORK"
export HOME="$WORK/home"; mkdir -p "$HOME"
unset NXREDUX_SDCARD NXREDUX_SYSTEM_ROOT || true

"$WORK/NXRedux.app/Contents/MacOS/NXRedux" & PID=$!
sleep 5
kill -0 "$PID" || { echo "FAIL: nextui died"; cat "$HOME"/NXRedux/.userdata/desktop/logs/nextui.txt || true; exit 1; }
[ -d "$HOME/NXRedux/Roms/Game Boy (GB)" ] || { echo "FAIL: card not seeded"; exit 1; }
grep -q "hasRecents $HOME/NXRedux" "$HOME/NXRedux/.userdata/desktop/logs/nextui.txt" \
	|| { echo "FAIL: runtime root not honored"; exit 1; }
echo "verify-macos: OK (window render + game launch need one hands-on pass)"
