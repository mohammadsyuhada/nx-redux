#!/bin/sh
# Migration to the .system pak layout (2026-07-31, platform-less): shipped
# Emus and Tools paks live in .system/paks/, and /Emus,/Tools hold user paks
# only (no platform subfolder — cards are per-device). Any SD pak whose
# <TAG>.pak name exists among the system paks is a redux-shipped leftover:
# delete it (tag match only — no content check; owner-accepted that
# customized copies of shipped-name paks are removed too). Unknown tags are
# never touched. Legacy /Emus/<plat> and /Tools/<plat> dirs from pre-merge
# cards get the same cleanup, surviving entries hoisted one level up, .media
# merged, and the platform dir removed. Cards are per-device now, but a
# formerly-shared card (common under old NxRedux) can carry MORE than one
# platform's legacy dirs, so every known platform is swept, not just this
# device's. /Emus/shared is never examined
# (refreshed by unzip -o; holds user state). The pre-flatten legacy
# .system/<plat> tree is removed too: wholesale on a normal (new-boot) run, or
# pruned around the two Task-11 compat shims (bin/install.sh,
# paks/MinUI.pak/launch.sh) when this update was started by the old boot.sh —
# the still-running old updater execs the launch shim after install.sh returns.
# Runs on every update; scheduled for removal after ~2 releases — see
# DEV_TODO.md.
#
# Usage: migrate-paks.sh PLATFORM   (busybox sh; must never fail the update)

PLATFORM="$1"
[ -n "$PLATFORM" ] || exit 0

# Every platform this repo ships. A formerly-shared card can hold legacy dirs
# for any of them, and all are cleaned. MUST track the Makefile PLATFORMS list
# (same precedent as boot.sh's per-device marker list).
KNOWN_PLATFORMS="tg5040 tg5050"

SDCARD_PATH="${SDCARD_PATH:-/mnt/SDCARD}"
SYSTEM_PATH="$SDCARD_PATH/.system"
REPORT_DIR="$SDCARD_PATH/.userdata/$PLATFORM"
REPORT="$REPORT_DIR/migration-report.txt"

header_written=0
report() {
	mkdir -p "$REPORT_DIR"
	if [ "$header_written" = 0 ]; then
		echo "== pak cleanup $(date '+%Y-%m-%d %H:%M:%S') ==" >> "$REPORT"
		header_written=1
	fi
	echo "$1" >> "$REPORT"
}

# user pak layers must exist (fresh cards ship without them)
for dir in "$SDCARD_PATH/Emus" "$SDCARD_PATH/Tools"; do
	mkdir -p "$dir"
	[ -f "$dir/README.txt" ] || cat > "$dir/README.txt" <<'RM_EOF'
Put your own paks here (e.g. PSP.pak). The paks NX Redux ships live in
/.system/paks/ and are replaced wholesale on every update — do not edit them
there, and do not place a pak here with the same name as a shipped one:
same-named paks are treated as NX Redux leftovers and are currently removed on
every update.
RM_EOF
done

# 1) current layout: delete shipped-name paks
for kind in Emus Tools; do
	for pak in "$SDCARD_PATH/$kind"/*.pak; do
		[ -d "$pak" ] || continue
		tag="$(basename "$pak")"
		if [ -d "$SYSTEM_PATH/paks/$kind/$tag" ]; then
			rm -rf "$pak"
			report "DELETED $pak — shipped pak name, now lives in .system/paks/$kind/"
		fi
	done
done

# 2) legacy platform dirs (pre-merge layout), for every known platform
for kind in Emus Tools; do
	for legacy_plat in $KNOWN_PLATFORMS; do
		legacy="$SDCARD_PATH/$kind/$legacy_plat"
		[ -d "$legacy" ] || continue

		for pak in "$legacy"/*.pak; do
			[ -d "$pak" ] || continue
			tag="$(basename "$pak")"
			if [ -d "$SYSTEM_PATH/paks/$kind/$tag" ]; then
				rm -rf "$pak"
				report "DELETED $pak — shipped pak name (legacy layout)"
			fi
		done

		# merge legacy .media entry-by-entry (freshly-shipped copy wins), drop rest
		if [ -d "$legacy/.media" ]; then
			mkdir -p "$SDCARD_PATH/$kind/.media"
			for item in "$legacy/.media"/* "$legacy/.media"/.[!.]*; do
				[ -e "$item" ] || continue
				name="$(basename "$item")"
				if [ ! -e "$SDCARD_PATH/$kind/.media/$name" ]; then
					mv "$item" "$SDCARD_PATH/$kind/.media/$name"
					report "MOVED   $item -> $SDCARD_PATH/$kind/.media/$name"
				fi
			done
			rm -rf "$legacy/.media"
			report "REMOVED $legacy/.media — merged into $SDCARD_PATH/$kind/.media"
		fi

		# a README this script wrote into the legacy dir on an earlier run is ours
		rm -f "$legacy/README.txt"

		# hoist surviving entries (user paks, stray files) one level up
		for item in "$legacy"/* "$legacy"/.[!.]*; do
			[ -e "$item" ] || continue
			name="$(basename "$item")"
			if [ -e "$SDCARD_PATH/$kind/$name" ]; then
				report "SKIPPED $item — $SDCARD_PATH/$kind/$name already exists, left in place"
			else
				mv "$item" "$SDCARD_PATH/$kind/$name"
				report "MOVED   $item -> $SDCARD_PATH/$kind/$name"
			fi
		done

		rmdir "$legacy" 2>/dev/null && report "REMOVED $legacy — legacy platform folder"
	done
done

# 3) legacy .system/<plat> trees (pre-flatten layout), for every known
# platform. Foreign-platform trees are dead weight (their shims/tree are never
# invoked on this device — the updater dispatches by /proc/cpuinfo) and go
# wholesale regardless of the flag. Only THIS device's tree may need its two
# compat shims kept: if the update was started by a legacy boot.sh (shim sets
# the flag), the still-running old updater execs the launch shim after we
# return — so prune around them.
NX_LEGACY_FLAG="${NX_LEGACY_FLAG:-/tmp/nx_legacy_boot}"
for legacy_plat in $KNOWN_PLATFORMS; do
	legacy_sys="$SYSTEM_PATH/$legacy_plat"
	[ -d "$legacy_sys" ] || continue
	if [ "$legacy_plat" = "$PLATFORM" ] && [ -f "$NX_LEGACY_FLAG" ]; then
		for item in "$legacy_sys"/* "$legacy_sys"/.[!.]*; do
			[ -e "$item" ] || continue
			case "$item" in
				"$legacy_sys/bin"|"$legacy_sys/paks") continue ;;
			esac
			rm -rf "$item"
		done
		find "$legacy_sys/bin" -mindepth 1 ! -name 'install.sh' -exec rm -rf {} + 2>/dev/null
		find "$legacy_sys/paks" -mindepth 1 -maxdepth 1 ! -name 'MinUI.pak' -exec rm -rf {} + 2>/dev/null
		find "$legacy_sys/paks/MinUI.pak" -mindepth 1 ! -name 'launch.sh' -exec rm -rf {} + 2>/dev/null
		report "PRUNED  $legacy_sys — legacy system tree (compat shims kept for this boot)"
	else
		rm -rf "$legacy_sys"
		report "REMOVED $legacy_sys — legacy system tree"
	fi
done

exit 0
