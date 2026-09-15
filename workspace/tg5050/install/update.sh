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
# unpacked catalog whenever PortMaster is installed. One-shot per update;
# must never fail the update.
PM_CATALOG="$SDCARD_PATH/.system/paks/Tools/Xtras.pak/catalog/portmaster/pak"
PM_TOOLS="$SDCARD_PATH/Tools/PortMaster.pak"
PM_PORTS="$SDCARD_PATH/Emus/PORTS.pak"
if [ -d "$PM_CATALOG" ] && [ -f "$SDCARD_PATH/Emus/shared/PortMaster/version" ]; then
	if [ -d "$PM_TOOLS" ]; then
		cp -f "$PM_CATALOG/launch.sh" "$PM_CATALOG/ports_launch.sh" "$PM_CATALOG/portmaster.elf" "$PM_TOOLS/" 2>/dev/null \
			&& chmod +x "$PM_TOOLS/launch.sh" "$PM_TOOLS/ports_launch.sh" "$PM_TOOLS/portmaster.elf" 2>/dev/null \
			&& echo "refreshed $PM_TOOLS from the Xtras catalog"
	fi
	if [ -d "$PM_PORTS" ]; then
		cp -f "$PM_CATALOG/ports_launch.sh" "$PM_PORTS/launch.sh" 2>/dev/null \
			&& chmod +x "$PM_PORTS/launch.sh" 2>/dev/null \
			&& echo "refreshed $PM_PORTS/launch.sh from the Xtras catalog"
	fi
fi
# --- portmaster-refresh-end
