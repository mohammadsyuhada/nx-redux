# Shared desktop entry logic, sourced by Contents/MacOS/NXRedux (macOS) and
# AppRun (AppImage). Callers pass the bundle's system tree + base skeleton.
# Contract consumers: pak launch.sh needs PATH/SDCARD_PATH/SYSTEM_PATH/
# SHARED_SYSTEM_PATH/CORES_PATH/USERDATA_PATH/LOGS_PATH; binaries read DEVICE,
# SHARED_USERDATA_PATH, NXREDUX_*.

entry_resolve_roots() { # $1 = system dir in bundle, $2 = base skeleton dir
	SYS="$1"; BASE_SKELETON="$2"
	CARD="${NXREDUX_SDCARD:-$HOME/NXRedux}"
}

entry_seed_card() {
	if [ ! -d "$CARD" ]; then
		mkdir -p "$CARD"
		cp -R "$BASE_SKELETON"/. "$CARD"/
	fi
	# .minui mirrors the device boot chain (MinUI.pak/launch.sh mkdir -p's it):
	# minarch/nextui write resume-slot markers, save-state screenshots, and
	# recent.txt under it with a single-level mkdir that can't create the
	# parent — without this the Game Switcher stays empty and save states
	# have no thumbnails.
	mkdir -p "$CARD/.userdata/desktop/logs" "$CARD/.userdata/shared/.minui" "$CARD/.shadercache"
}

entry_export_env() {
	export NXREDUX_SDCARD="$CARD"
	export NXREDUX_SYSTEM_ROOT="$SYS"
	# PID of the entry script (the relaunch loop): the in-app updater kills it
	# after a successful self-update so the swapped-in build's relaunch never
	# races a stale instance. $$ is the sourcing script's shell here.
	export NXREDUX_ENTRY_PID="$$"
	export DEVICE=desktop
	export PATH="$SYS/bin:$PATH"
	# SDCARD_PATH/SYSTEM_PATH mirror the device boot chain's exports (eg.
	# skeleton/SYSTEM/tg5040/paks/MinUI.pak/launch.sh): pak launch.sh scripts
	# ported from device (eg. Emulator Settings.pak) reference them directly
	# in shell glob/path logic, distinct from the C-side PATHS_* globals that
	# binaries resolve from NXREDUX_SDCARD/NXREDUX_SYSTEM_ROOT via paths.c.
	export SDCARD_PATH="$CARD"
	export SYSTEM_PATH="$SYS"
	# Device launch.sh scripts source shared helpers off this (eg.
	# netplay-prelaunch.sh); on desktop the shared skeleton lives inside the
	# bundle's system tree rather than on the card.
	export SHARED_SYSTEM_PATH="$SYS/shared"
	export CORES_PATH="$SYS/cores"
	export USERDATA_PATH="$CARD/.userdata/desktop"
	export SHARED_USERDATA_PATH="$CARD/.userdata/shared"
	export LOGS_PATH="$CARD/.userdata/desktop/logs"
}
