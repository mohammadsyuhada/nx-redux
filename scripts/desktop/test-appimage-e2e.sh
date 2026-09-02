#!/bin/sh
# Headless E2E for the AppImage: first-run seed, non-black frame, launch a
# GB ROM from the UI, assert core+ROM load in minarch's log. Desktop keys
# (workspace/desktop/platform/platform.h): arrows=dpad, s=A, a=B, Return=Start,
# Space=MENU. Container-only (needs xvfb/xdotool/imagemagick from the
# `appimage` compose service).
set -eu
ROOT="$(pwd)"
IMG="$(ls -t releases/NXRedux-*-x86_64.AppImage | head -1)"
export HOME=/tmp/nxhome; rm -rf "$HOME"; mkdir -p "$HOME"
unset NXREDUX_SDCARD NXREDUX_SYSTEM_ROOT || true

# freely-licensed test ROM (MIT): renders a deterministic image
wget -q https://github.com/mattcurrie/dmg-acid2/releases/download/v1.0/dmg-acid2.gb \
	-O /tmp/dmg-acid2.gb

# --appimage-extract always unpacks into ./squashfs-root (cwd); $ROOT is
# bind-mounted from the host, so a stale dir from a prior interactive run
# would otherwise sit there as untracked worktree clutter (gitignored, but
# still confusing) -- start clean.
rm -rf "$ROOT/squashfs-root"

Xvfb :99 -screen 0 1280x720x24 & XPID=$!
export DISPLAY=:99
sleep 2

# AppRun.sh runs a `while` loop around nextui.elf (it relaunches nextui.elf
# whenever a pak/game returns -- see that script), so the pid we back up
# below is that outer shell, not nextui.elf itself, and signaling it alone
# won't reach a currently-running nextui.elf child. Target processes by
# name instead throughout.
# Known gotcha (see verify-macos.sh): an idle nextui.elf can enter a PWR
# sleep loop that ignores plain SIGTERM (battery stub always reports
# "charging", so suspend never powers off). Try SIGTERM first; escalate to
# SIGQUIT if it doesn't take within a couple seconds. Never SIGKILL -- it
# can wedge the process.
stop_nextui() {
	pgrep -x nextui.elf >/dev/null 2>&1 || return 0
	pkill -TERM nextui.elf 2>/dev/null || true
	sleep 2
	if pgrep -x nextui.elf >/dev/null 2>&1; then
		echo "note: SIGTERM did not stop nextui, escalating to SIGQUIT"
		pkill -QUIT nextui.elf 2>/dev/null || true
		sleep 1
	fi
}

# Runs on every exit path (success or any FAIL below) so a failed assertion
# never leaks a running nextui/minarch behind this script.
cleanup() {
	stop_nextui
	pkill -TERM -f squashfs-root/AppRun 2>/dev/null || true
	pkill -TERM minarch.elf 2>/dev/null || true
	kill "$XPID" 2>/dev/null || true
}
trap cleanup EXIT

# AppImages need FUSE; run extracted inside docker
"$IMG" --appimage-extract >/dev/null
squashfs-root/AppRun & APID=$!
sleep 10
kill -0 "$APID" || { echo "FAIL: launcher died"; cat "$HOME"/NXRedux/.userdata/desktop/logs/nextui.txt; exit 1; }
[ -d "$HOME/NXRedux/Roms/Game Boy (GB)" ] || { echo "FAIL: card not seeded"; exit 1; }

# drop the ROM in, restart so the list picks it up cold (simplest determinism)
cp /tmp/dmg-acid2.gb "$HOME/NXRedux/Roms/Game Boy (GB)/"
stop_nextui
# A plain SIGTERM/SIGQUIT quit writes no /tmp/next, so AppRun.sh's own loop
# sees nothing to relaunch and exits on its own -- give it a moment, then
# launch a fresh app instance cold.
sleep 1
squashfs-root/AppRun & APID=$!
sleep 10

import -window root /tmp/frame1.png
# non-black frame: mean channel value must exceed noise floor
MEAN="$(convert /tmp/frame1.png -format '%[fx:int(mean*255)]' info:)"
[ "$MEAN" -gt 5 ] || { echo "FAIL: black frame (mean=$MEAN)"; exit 1; }

# navigate: first console row -> A (s) -> first rom -> A (s)
xdotool key s; sleep 4; xdotool key s; sleep 10

LOG="$HOME/NXRedux/.userdata/desktop/logs/GB.txt"
grep -q "Plain ROM loaded" "$LOG" || { echo "FAIL: core/ROM did not load"; cat "$LOG"; exit 1; }
import -window root /tmp/frame2.png

echo "test-appimage-e2e: OK"
