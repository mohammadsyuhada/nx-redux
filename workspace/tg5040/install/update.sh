#!/bin/sh

SDCARD_PATH=/mnt/SDCARD

# --------------------------------------
# Firmware validation: NX Redux targets the current TrimUI stock firmware -
# older firmware is missing rootfs libraries some features hard-depend on
# (field case 2026-08-10: a Brick on 1.0.6 has no /usr/trimui/lib/
# libmpg123.so.0, so the Xtras gen1recomp native runtime dies at load; 1.1.1
# ships it). The system itself still boots, so this must not block the
# install - but it IS the one moment the user is watching an install screen
# (tg5040.sh's show2 splash daemon is still up and this script only runs
# from its MinUI.zip branch), so say it loudly here instead of letting
# features degrade quietly later. Runs on every install AND update; a single
# system-level gate, not per-pak probes. README "Supported Devices" carries
# the same requirement.
MIN_FW="1.1.1"
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
# must run before the brick-migration block below, which can reboot without
# re-running install.sh; must never fail the update
sh ${SDCARD_PATH}/.system/shared/bin/migrate-paks.sh tg5040 || true

# --------------------------------------
# remove old brick system folder
BRICK_PATH=${SDCARD_PATH}/.system/tg3040
echo "check for $BRICK_PATH"
# this might always exist so we can pull up old cards
if [ -d $BRICK_PATH ]; then
	echo "deleting brick system folder $BRICK_PATH"
	rm -rf "$BRICK_PATH"
	
	# copy brick configs from userdata
	SRC_PATH=${SDCARD_PATH}/.userdata/tg3040
	if [ -d $SRC_PATH ]; then
		DST_PATH=${SDCARD_PATH}/.userdata/tg5040
		mkdir -p $DST_PATH # just in case
	
		for SUB_PATH in $SRC_PATH/*; do
			if [ -d $SUB_PATH ]; then
				SUB_NAME=$(basename $SUB_PATH)
				NEW_PATH=$DST_PATH/$SUB_NAME
			
				if [ ! -d $NEW_PATH ]; then
					echo "creating new path $NEW_PATH"
					mkdir -p $NEW_PATH
				fi
			
				for CFG_PATH in $SUB_PATH/*.cfg; do
					if [ -f $CFG_PATH ]; then
						CFG_NAME=$(basename $CFG_PATH .cfg)
						echo "copying $CFG_PATH to $NEW_PATH/$CFG_NAME-brick.cfg"
						cp $CFG_PATH $NEW_PATH/$CFG_NAME-brick.cfg
					fi
				done
			fi
		done
		echo "deleting brick userdata $SRC_PATH"
		rm -rf $SRC_PATH
		
		UPDATE_PATH=${SDCARD_PATH}/.tmp_update/tg3040
		rm -rf $UPDATE_PATH.sh
		rm -rf $UPDATE_PATH
		
		reboot
		# we need to sleep until reboot otherwise 
		# it will poweroff without rebooting
		while :; do
			sleep 1
		done
	fi
fi
# --------------------------------------

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
