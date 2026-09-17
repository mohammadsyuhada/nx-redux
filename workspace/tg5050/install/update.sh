#!/bin/sh

SDCARD_PATH=/mnt/SDCARD

# --------------------------------------
# Firmware validation - same single system-level gate as tg5040's update.sh
# (see the comment there; field case was a Brick on 1.0.6 missing rootfs
# libs the Xtras gen1recomp native runtime hard-links). The Smart Pro S
# firmware line is versioned separately from the tg5040 family: 1.0.1
# (v1.0.1-20251218) is the latest official release per
# github.com/trimui/firmware_smartpro_s as of 2026-08-10.
MIN_FW="1.0.1"
FW="$(cat /etc/version 2>/dev/null)"
if [ -n "$MIN_FW" ] && [ -n "$FW" ] && [ "$FW" != "$MIN_FW" ]; then
	# dotted-decimal compare via numeric per-field sort; oldest sorts first
	OLDEST="$(printf '%s\n%s\n' "$FW" "$MIN_FW" | sort -t. -k1,1n -k2,2n -k3,3n | head -1)"
	if [ "$OLDEST" = "$FW" ]; then
		echo "firmware $FW older than supported $MIN_FW"
		if [ -p /tmp/show2.fifo ]; then
			echo "TEXT:TrimUI firmware $FW is too old (need $MIN_FW+). Some features will not work - update the stock firmware, then reinstall NX Redux." > /tmp/show2.fifo
			sleep 12
		fi
	fi
fi

# --------------------------------------
# clean shipped-name paks out of /Emus and /Tools (moved into .system
# 2026-07-31; removal of this hook is tracked in DEV_TODO.md);
# must never fail the update
sh ${SDCARD_PATH}/.system/shared/bin/migrate-paks.sh tg5050 || true

# --------------------------------------
# migration code here
# --------------------------------------

# Releases up to 2026-09 had the bluez upgrade (nextui.upgrade_bluez.pakz)
# tar the stock Bluetooth stack into the card root as btmgr_<date>.tar
# before replacing it. Nothing ever restored from it and the upgrade no
# longer writes it, so clear any leftover on the next update.
rm -f "${SDCARD_PATH}"/btmgr_*.tar

# --------------------------------------
# --- portmaster-refresh-begin
# PortMaster (an Xtras entry) installs its GUI pak and the Ports console
# runner as user-level copies (Tools/PortMaster.pak, Emus/PORTS.pak/launch.sh)
# that an update never touched, so they went stale: field case 2026-09-16, a
# Brick still ran the Aug-20 portmaster.elf (pre-RGBA colours — garbled the
# theme on exit) and the old ports runner (retired xbox_layout marker path,
# so ports ignored the button-layout setting). Re-sync them from the freshly
# unpacked catalog whenever PortMaster is installed. Since 2026-09-16 the pak
# is launch.sh only (the elf is gone, and the ports_launch.sh copy the elf
# built PORTS.pak from is unused): both are deleted, and a pak still at the pre-flat Tools/tg5050/ location
# is moved up. One-shot per update; must never fail the update. The platform
# tag is hard-coded on purpose: $PLATFORM is not set here, and an empty
# value would make the old path equal the flat pak.
PM_CATALOG="$SDCARD_PATH/.system/paks/Tools/Xtras.pak/catalog/portmaster/pak"
PM_TOOLS="$SDCARD_PATH/Tools/PortMaster.pak"
PM_OLD_TOOLS="$SDCARD_PATH/Tools/tg5050/PortMaster.pak"
PM_PORTS="$SDCARD_PATH/Emus/PORTS.pak"
if [ -d "$PM_CATALOG" ] && [ -f "$SDCARD_PATH/Emus/shared/PortMaster/version" ]; then
	if [ -d "$PM_OLD_TOOLS" ] && [ ! -d "$PM_TOOLS" ]; then
		mkdir -p "$PM_TOOLS" 2>/dev/null
	fi
	if [ -d "$PM_TOOLS" ]; then
		cp -f "$PM_CATALOG/launch.sh" "$PM_TOOLS/" 2>/dev/null \
			&& chmod +x "$PM_TOOLS/launch.sh" 2>/dev/null \
			&& rm -f "$PM_TOOLS/portmaster.elf" "$PM_TOOLS/ports_launch.sh" \
			&& echo "refreshed $PM_TOOLS from the Xtras catalog"
	fi
	if [ -d "$PM_OLD_TOOLS" ] && [ -x "$PM_TOOLS/launch.sh" ]; then
		rm -rf "$PM_OLD_TOOLS"
		rmdir "$SDCARD_PATH/Tools/tg5050" 2>/dev/null
		echo "moved the PortMaster pak from Tools/tg5050 to $PM_TOOLS"
	fi
	if [ -d "$PM_PORTS" ]; then
		cp -f "$PM_CATALOG/ports_launch.sh" "$PM_PORTS/launch.sh" 2>/dev/null \
			&& chmod +x "$PM_PORTS/launch.sh" 2>/dev/null \
			&& echo "refreshed $PM_PORTS/launch.sh from the Xtras catalog"
	fi
	# PortMaster is now version_source=internal; refresh the pak-code marker to
	# the catalog version so Xtras does not show a phantom "update available"
	# against an old upstream tag left by a pre-migration install (PM_CATALOG is
	# the .../portmaster/pak dir, so meta.txt is one level up).
	PM_VER="$(sed -n 's/^version=//p' "$SDCARD_PATH/.system/paks/Tools/Xtras.pak/catalog/portmaster/meta.txt" 2>/dev/null | head -1 | tr -d '\r')"
	[ -n "$PM_VER" ] && printf '%s\n' "$PM_VER" > "$SDCARD_PATH/.userdata/shared/xtras/portmaster.version" 2>/dev/null
fi
# --- portmaster-refresh-end

# --------------------------------------
# --- cheatdb-refresh-begin
# Cheat Database (an internal Xtras entry) installs its launchable pak as a
# user-level copy (Tools/Cheat Database.pak) that an update never touched, so
# it would go stale. Re-copy it from the freshly unpacked catalog whenever the
# Cheat Database is installed (the $XTRAS_STATE_DIR/cheatdb.version marker
# exists), and refresh the pak-code marker to the catalog version so Xtras does
# not show a phantom "update available" after the OTA already refreshed the
# code. One-shot per update; must never fail the update. Platform tag hard-coded
# ($PLATFORM is not set here).
CD_CATALOG="$SDCARD_PATH/.system/paks/Tools/Xtras.pak/catalog/cheatdb"
CD_TOOLS="$SDCARD_PATH/Tools/Cheat Database.pak"
CD_MARK="$SDCARD_PATH/.userdata/shared/xtras/cheatdb.version"
if [ -d "$CD_CATALOG/pak" ] && [ -f "$CD_MARK" ]; then
	if [ -d "$CD_TOOLS" ] || mkdir -p "$CD_TOOLS" 2>/dev/null; then
		cp -f "$CD_CATALOG/pak/launch.sh" "$CD_TOOLS/launch.sh" 2>/dev/null \
			&& chmod +x "$CD_TOOLS/launch.sh" 2>/dev/null \
			&& echo "refreshed $CD_TOOLS from the Xtras catalog"
	fi
	CD_VER="$(sed -n 's/^version=//p' "$CD_CATALOG/meta.txt" 2>/dev/null | head -1 | tr -d '\r')"
	[ -n "$CD_VER" ] && printf '%s\n' "$CD_VER" > "$CD_MARK" 2>/dev/null
fi
# --- cheatdb-refresh-end
