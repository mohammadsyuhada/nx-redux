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
