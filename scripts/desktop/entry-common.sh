# Shared desktop entry logic, sourced by Contents/MacOS/NXRedux (macOS) and
# AppRun (AppImage). Callers pass the bundle's system tree + base skeleton.
# Contract consumers: pak launch.sh needs PATH/CORES_PATH/USERDATA_PATH/
# LOGS_PATH; binaries read DEVICE, SHARED_USERDATA_PATH, NXREDUX_*.

entry_resolve_roots() { # $1 = system dir in bundle, $2 = base skeleton dir
	SYS="$1"; BASE_SKELETON="$2"
	CARD="${NXREDUX_SDCARD:-$HOME/NXRedux}"
}

entry_seed_card() {
	if [ ! -d "$CARD" ]; then
		mkdir -p "$CARD"
		cp -R "$BASE_SKELETON"/. "$CARD"/
	fi
	mkdir -p "$CARD/.userdata/desktop/logs" "$CARD/.userdata/shared" "$CARD/.shadercache"
}

entry_export_env() {
	export NXREDUX_SDCARD="$CARD"
	export NXREDUX_SYSTEM_ROOT="$SYS"
	export DEVICE=desktop
	export PATH="$SYS/bin:$PATH"
	export CORES_PATH="$SYS/cores"
	export USERDATA_PATH="$CARD/.userdata/desktop"
	export SHARED_USERDATA_PATH="$CARD/.userdata/shared"
	export LOGS_PATH="$CARD/.userdata/desktop/logs"
}

# Play-time tracking daemon (Game Tracker's gametimectl). Guarded so the
# entry still works if the binary isn't in the bundle (Task 11 copies it to
# $SYS/bin); backgrounded so it never blocks nextui.elf from starting.
entry_start_daemons() {
	[ -x "$SYS/bin/gametimectl.elf" ] && "$SYS/bin/gametimectl.elf" >> "$LOGS_PATH/gametimectl.txt" 2>&1 &
}
