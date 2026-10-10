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
# A failed or interrupted in-app update download used to leave a truncated
# .tmp_update.zip in the card root (the updater kept the partial file).
# Windows Explorer opens the root when the card is plugged in and asks for
# "the last disk of the multi-volume set" on that broken zip. A successful
# OTA deletes it before rebooting, so anything left here is stale.
rm -f "$SDCARD_PATH/.tmp_update.zip" "$SDCARD_PATH/.tmp_update.zip.done" "$SDCARD_PATH/.tmp_update.zip.headers" 2>/dev/null

# --------------------------------------
# --- psp-standalone-cleanup-begin
# PSP moved from the standalone PPSSPP pak (ben16w/minui-psp, installed from
# Xtras into Emus/<platform>/PSP.pak, earlier the flat Emus/PSP.pak) to the
# PPSSPP libretro core in the system PSP.pak. The standalone (identified by its
# PPSSPPSDL binary, or the PPSSPP/.config shell an Xtras uninstall leaves) has
# its memory stick carried to the libretro one (Saves/PSP: PPSSPP maps ms0:/PSP
# straight onto a save dir named PSP) and is removed. Any
# other user PSP.pak is left alone (a same-named user pak overrides the
# shipped one). Standalone saves (Saves/PSP/<ID>: the standalone bind-mounted
# Saves/PSP as SAVEDATA) move to Saves/PSP/SAVEDATA/<ID>; an existing
# destination is never overwritten.
# Standalone save states (.userdata/shared/PSP-ppsspp) are not portable and
# are left alone. One-shot per update; must never fail the update.
PSP_SAVES="$SDCARD_PATH/Saves/PSP"
for PSP_PAK in "$SDCARD_PATH/Emus/tg5040/PSP.pak" "$SDCARD_PATH/Emus/tg5050/PSP.pak" "$SDCARD_PATH/Emus/PSP.pak"; do
	if ! ls "$PSP_PAK"/PPSSPP/PPSSPPSDL* >/dev/null 2>&1; then
		[ ! -f "$PSP_PAK/launch.sh" ] && [ -d "$PSP_PAK/PPSSPP/.config" ] || continue
	fi
	# its memory stick: what the libretro one (Saves/PSP) uses moves over, never
	# overwriting; the rest (settings, its cheat files, name clashes) is kept in
	# Saves/PSP/standalone-backup/<tg5040|tg5050|flat> rather than deleted
	PSP_MS="$PSP_PAK/PPSSPP/.config/ppsspp/PSP"
	for PSP_D in GAME TEXTURES SAVEDATA PLUGINS SCREENSHOT; do
		for PSP_T in "$PSP_MS/$PSP_D"/*; do
			[ -e "$PSP_T" ] || continue
			[ -e "$PSP_SAVES/$PSP_D/${PSP_T##*/}" ] && continue
			mkdir -p "$PSP_SAVES/$PSP_D" 2>/dev/null
			mv "$PSP_T" "$PSP_SAVES/$PSP_D/" 2>/dev/null
		done
	done
	if [ -n "$(find "$PSP_MS" -type f 2>/dev/null | head -n 1)" ]; then
		PSP_B="${PSP_PAK%/PSP.pak}"
		PSP_B="${PSP_B##*/}"
		[ "$PSP_B" = Emus ] && PSP_B=flat
		mkdir -p "$PSP_SAVES/standalone-backup" 2>/dev/null
		[ -e "$PSP_SAVES/standalone-backup/$PSP_B" ] || mv "$PSP_MS" "$PSP_SAVES/standalone-backup/$PSP_B" 2>/dev/null
	fi
	rm -rf "$PSP_PAK" && echo "removed the standalone PPSSPP pak ${PSP_PAK#"$SDCARD_PATH"/}"
	rmdir "${PSP_PAK%/*}" 2>/dev/null
done
rm -f "$SDCARD_PATH/.userdata/shared/xtras/psp.version" "$SDCARD_PATH/.userdata/shared/xtras/psp.latest" 2>/dev/null
for PSP_S in "$PSP_SAVES"/*; do
	[ -f "$PSP_S/PARAM.SFO" ] || continue
	PSP_N="${PSP_S##*/}"
	[ -e "$PSP_SAVES/SAVEDATA/$PSP_N" ] && continue
	mkdir -p "$PSP_SAVES/SAVEDATA" 2>/dev/null
	mv "$PSP_S" "$PSP_SAVES/SAVEDATA/" 2>/dev/null && echo "moved PSP save $PSP_N"
done
true
# --- psp-standalone-cleanup-end

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
# built PORTS.pak from is unused): both are deleted, and a pak still at the pre-flat Tools/tg5040/ location
# is moved up. One-shot per update; must never fail the update. The platform
# tag is hard-coded on purpose: $PLATFORM is not set here, and an empty
# value would make the old path equal the flat pak.
PM_CATALOG="$SDCARD_PATH/.system/paks/Tools/Xtras.pak/catalog/portmaster/pak"
PM_TOOLS="$SDCARD_PATH/Tools/PortMaster.pak"
PM_OLD_TOOLS="$SDCARD_PATH/Tools/tg5040/PortMaster.pak"
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
		rmdir "$SDCARD_PATH/Tools/tg5040" 2>/dev/null
		echo "moved the PortMaster pak from Tools/tg5040 to $PM_TOOLS"
	fi
	if [ -d "$PM_PORTS" ]; then
		cp -f "$PM_CATALOG/ports_launch.sh" "$PM_PORTS/launch.sh" 2>/dev/null \
			&& chmod +x "$PM_PORTS/launch.sh" 2>/dev/null \
			&& echo "refreshed $PM_PORTS/launch.sh from the Xtras catalog"
	fi
	# gl4es's EGL shim in PortMaster/lib broke Xwayland for Weston ports (the
	# catalog install.sh no longer keeps it); remove it from existing installs.
	rm -f "$SDCARD_PATH/Emus/shared/PortMaster/lib/libEGL.so.1" 2>/dev/null
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
		cp -f "$CD_CATALOG/pak/launch.sh" "$CD_TOOLS/launch.sh" 2>/dev/null
		cp -f "$CD_CATALOG/pak/cheatdb.elf" "$CD_TOOLS/cheatdb.elf" 2>/dev/null
		chmod +x "$CD_TOOLS/launch.sh" "$CD_TOOLS/cheatdb.elf" 2>/dev/null \
			&& echo "refreshed $CD_TOOLS from the Xtras catalog"
	fi
	CD_VER="$(sed -n 's/^version=//p' "$CD_CATALOG/meta.txt" 2>/dev/null | head -1 | tr -d '\r')"
	[ -n "$CD_VER" ] && printf '%s\n' "$CD_VER" > "$CD_MARK" 2>/dev/null
fi
# --- cheatdb-refresh-end

# --------------------------------------
# --- dc-standalone-cleanup-begin
# Dreamcast moved from standalone flycast to the flycast libretro core on
# minarch. The install replaces .system/paks/Emus wholesale, so only the
# standalone OSD overlay file on the card root is left over. Players' flycast
# data (.userdata/shared/DC-flycast/, the source of the per-game memory card
# carried over by DC.pak) is never touched. One-shot per update; must never
# fail the update.
DC_OLD="$SDCARD_PATH/Emus/shared/flycast"
if [ -f "$DC_OLD/overlay_settings.json" ]; then
	rm -f "$DC_OLD/overlay_settings.json"
	rmdir "$DC_OLD" 2>/dev/null
	echo "removed the standalone flycast overlay settings"
fi
# --- dc-standalone-cleanup-end

# --------------------------------------
# --- n64-standalone-cleanup-begin
# N64 moved from the standalone mupen64plus (its own overlay, GLideN64 .so)
# to mupen64plus-next in minarch. The install replaces
# .system/paks/Emus wholesale; this removes what the standalone left on the
# card. Players' standalone saves and states (.userdata/shared/N64-mupen64plus)
# are never touched: N64.pak's core imports a game's saves from there on first
# launch. Hi-res texture packs stay in Roms/Nintendo 64 (N64)/.hires_texture.
# One-shot per update; must never fail the update.
N64_OLD="$SDCARD_PATH/Emus/shared/mupen64plus"
[ -d "$N64_OLD" ] && rm -rf "$N64_OLD" && echo "removed the standalone mupen64plus data"
true
# --- n64-standalone-cleanup-end

