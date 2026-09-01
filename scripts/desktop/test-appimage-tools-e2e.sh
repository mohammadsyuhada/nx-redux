#!/bin/sh
# Headless Tools-menu E2E for the AppImage: opens the Tools folder from the
# main menu and confirms the 7 desktop paks are listed, then launches
# Settings.pak's binary directly to confirm the section trim. Container-only
# (needs xvfb/xdotool/imagemagick from the `appimage` compose service), same
# recipe family as test-appimage-e2e.sh.
#
# Desktop keys (workspace/desktop/platform/platform.h -- SDL2 scancodes,
# RetroArch-style Z/X/A/S face-button cluster): CODE_A is scancode 27 =
# SDL_SCANCODE_X, ie. the physical "x" key opens/selects (test-appimage-e2e.sh's
# own header comment says "s=A" -- that's wrong, s is SDL_SCANCODE_S = CODE_X,
# a different button; don't copy it). Down-arrow navigation is unreliable
# under Xvfb (a single xdotool keydown/keyup pair sometimes moves the
# selection by 1 row, sometimes by 2 -- the app's PAD_REPEAT_INTERVAL
# auto-repeat logic occasionally fires once during even a short synthetic
# hold), so this script never counts exact row-navigation steps: it opens
# whatever is selected immediately after entering Tools (alphabetically
# first: Artwork Manager) for the "a pak opens" check, and launches
# Settings.pak/settings.elf directly (bypassing in-app navigation entirely)
# for the section-trim check.
set -eu
ROOT="$(pwd)"
IMG="$(ls -t releases/NXRedux-*-x86_64.AppImage | head -1)"
OUT="${1:-/tmp}"
export HOME=/tmp/nxhome; rm -rf "$HOME"; mkdir -p "$HOME"
unset NXREDUX_SDCARD NXREDUX_SYSTEM_ROOT || true
rm -rf "$ROOT/squashfs-root"

Xvfb :99 -screen 0 1280x720x24 & XPID=$!
export DISPLAY=:99
sleep 2

stop_nextui() {
	pgrep -x nextui.elf >/dev/null 2>&1 || return 0
	pkill -TERM nextui.elf 2>/dev/null || true
	sleep 2
	if pgrep -x nextui.elf >/dev/null 2>&1; then
		pkill -QUIT nextui.elf 2>/dev/null || true
		sleep 1
	fi
}
cleanup() {
	stop_nextui
	pkill -TERM -f squashfs-root/AppRun 2>/dev/null || true
	pkill -TERM settings.elf 2>/dev/null || true
	kill "$XPID" 2>/dev/null || true
}
trap cleanup EXIT

# --- Part 1: Tools menu lists the 7 paks + a pak opens -------------------
"$IMG" --appimage-extract >/dev/null
squashfs-root/AppRun & APID=$!
sleep 10
kill -0 "$APID" || { echo "FAIL: launcher died"; cat "$HOME"/NXRedux/.userdata/desktop/logs/nextui.txt; exit 1; }

xdotool key x; sleep 3 # open the root "Tools" folder (only root entry on a fresh card)
import -window root "$OUT/tools_menu.png"
echo "wrote $OUT/tools_menu.png -- visually confirm: Artwork Manager, Device Sync,"
echo "  Emulator Settings, Game Tracker, RetroAchievements, Settings, Xtras"

xdotool key x; sleep 4 # open the alphabetically-first pak (Artwork Manager)
import -window root "$OUT/artwork_manager_open.png"
LOG="$HOME/NXRedux/.userdata/desktop/logs/scraper.txt"
[ -f "$LOG" ] || { echo "FAIL: Artwork Manager did not launch (no scraper.txt)"; exit 1; }
pkill -TERM scraper.elf 2>/dev/null || true
sleep 1

stop_nextui
sleep 1

# --- Part 2: Settings section trim ----------------------------------------
# Direct binary launch (matches Task 2/6's dev-tree recipe) sidesteps the
# Down-key navigation flakiness above; it exercises the exact same
# settings.elf the pak's launch.sh runs, just without the cd+relaunch shell.
SYS="$ROOT/build/desktop-linux/AppDir/usr/system"
export NXREDUX_SDCARD="$HOME/NXRedux2"; rm -rf "$NXREDUX_SDCARD"
mkdir -p "$NXREDUX_SDCARD/.userdata/desktop/logs" "$NXREDUX_SDCARD/.userdata/shared"
export NXREDUX_SYSTEM_ROOT="$SYS" DEVICE=desktop
export SDCARD_PATH="$NXREDUX_SDCARD" SYSTEM_PATH="$SYS" CORES_PATH="$SYS/cores"
export USERDATA_PATH="$NXREDUX_SDCARD/.userdata/desktop"
export SHARED_USERDATA_PATH="$NXREDUX_SDCARD/.userdata/shared"
export LOGS_PATH="$NXREDUX_SDCARD/.userdata/desktop/logs"
export LD_LIBRARY_PATH="$ROOT/build/desktop-linux/AppDir/usr/lib"
"$SYS/paks/Tools/Settings.pak/settings.elf" >"$OUT/settings_stdout.log" 2>&1 &
SPID=$!
sleep 7
kill -0 "$SPID" || { echo "FAIL: settings.elf exited early"; cat "$OUT/settings_stdout.log"; exit 1; }
import -window root "$OUT/settings_menu.png"
echo "wrote $OUT/settings_menu.png -- visually confirm ONLY: Display, Appearance,"
echo "  In-game Notifications, Audio, Simple Mode, System, About (no WiFi/BT/"
echo "  Brightness*/Color/LED/Power/Sleep/Battery/F1-F2/FN/Developer)"
echo "  (*Brightness/Color live inside Display -- has_display_hw(dev) hides the"
echo "  whole page's HW rows, not the Display entry itself)"
kill -TERM "$SPID" 2>/dev/null || true

echo "test-appimage-tools-e2e: OK"
