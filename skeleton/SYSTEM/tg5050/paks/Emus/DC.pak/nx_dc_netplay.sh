# Sourced by DC.pak's launch.sh. Dreamcast netplay is flycast's GGPO rollback,
# run inside the core; this sets it up when nextui asked for a netplay launch
# (Y / "Launch with Netplay" wrote the launch flag):
#   - the pre-launch wizard (netplay.elf) finds the other player and, as for
#     every system, the host brings the save: it serves its memory card,
#     console settings (dc_nvmem) and second card, or an arcade game's saves;
#     the client plays on copies in /tmp, its own files are never touched;
#   - both sides trade a BIOS fingerprint (--caps) and use the real BIOS only
#     when they have the same one, HLE otherwise (a mismatch would desync);
#   - the session goes to the core in NX_GGPO_* and to minarch in
#     NX_CORE_NETPLAY, never NETPLAY_ROLE (that starts minarch's own link
#     engine, which is not what runs here).
# A cancelled or failed wizard exits the sourcing launch.sh (back to the game
# list), never falling through to a one-player launch. The flag and session
# paths and NX_DC_NETPLAY_TMP (default /tmp) are overridable for tests only.
nx_dc_netplay() {
	NP_FLAG="${NETPLAY_LAUNCH_FLAG:-/tmp/netplay_launch}"
	[ -f "$NP_FLAG" ] || return 0
	rm -f "$NP_FLAG"
	NP_SESSION="${NETPLAY_SESSION_FILE:-/tmp/netplay_session}"
	NP_TMP="${NX_DC_NETPLAY_TMP:-/tmp}"
	NP_STAGE="$NP_TMP/netplay-serve"
	NP_FETCH="$NP_TMP/netplay-fetch"
	NP_SAVES="$NP_TMP/netplay-saves"
	NP_SYSTEM="$NP_TMP/netplay-system"

	NP_ROMFILE=$(basename "$ROM")
	NP_STEM="${NP_ROMFILE%.*}"
	# the core's names: patch 0003 (card) and the arcade save names
	NP_CARD="$(echo "$NP_STEM" | sed 's/ (Disc [^)]*)//').A1.bin"
	NP_SAVES_REAL="$SAVES_PATH/$EMU_TAG"
	NP_SYS_REAL="$SDCARD_PATH/Bios/$EMU_TAG"
	[ -d "$NP_SYS_REAL/dc" ] && NP_SYS_REAL="$NP_SYS_REAL/dc"
	case "$(echo "${NP_ROMFILE##*.}" | tr 'A-Z' 'a-z')" in
	zip | 7z) NP_ARCADE=1 ;;
	*) NP_ARCADE=0 ;;
	esac

	NP_BIOS=none
	if [ -f "$NP_SYS_REAL/dc_boot.bin" ]; then
		NP_BIOS=$(md5sum "$NP_SYS_REAL/dc_boot.bin" | cut -c1-12)
	fi

	# Stage what a host serves, under wire-safe names (the wizard only moves
	# [A-Za-z0-9._-] names). Rebuilt every session so nothing stale leaks in.
	rm -rf "$NP_STAGE" "$NP_FETCH" "$NP_SAVES" "$NP_SYSTEM"
	mkdir -p "$NP_STAGE" "$NP_FETCH"
	if [ "$NP_ARCADE" = 1 ]; then
		for NP_EXT in nvmem nvmem2 eeprom; do
			[ -f "$NP_SAVES_REAL/reicast/$NP_ROMFILE.$NP_EXT" ] &&
				cp "$NP_SAVES_REAL/reicast/$NP_ROMFILE.$NP_EXT" "$NP_STAGE/nxarc.$NP_EXT"
		done
	else
		[ -f "$NP_SAVES_REAL/$NP_CARD" ] && cp "$NP_SAVES_REAL/$NP_CARD" "$NP_STAGE/nxcard.bin"
	fi
	[ -f "$NP_SYS_REAL/dc_nvmem.bin" ] && cp "$NP_SYS_REAL/dc_nvmem.bin" "$NP_STAGE/nxnvmem.bin"
	[ -f "$NP_SYS_REAL/vmu_save_A2.bin" ] && cp "$NP_SYS_REAL/vmu_save_A2.bin" "$NP_STAGE/nxa2.bin"

	netplay.elf --game "$NP_STEM" --session-file "$NP_SESSION" --caps "dcbios=$NP_BIOS" \
		--serve-dir "$NP_STAGE" --fetch-to "$NP_FETCH" \
		--fetch-files "nxcard.bin,nxnvmem.bin,nxa2.bin,nxarc.nvmem,nxarc.nvmem2,nxarc.eeprom" \
		> "$LOGS_PATH/netplay-wizard.txt" 2>&1 || { nx_dc_netplay_cleanup; exit 0; }
	[ -f "$NP_SESSION" ] || { nx_dc_netplay_cleanup; exit 0; }
	. "$NP_SESSION"
	NP_ROLE="$NETPLAY_ROLE"
	NP_PEER="$NETPLAY_PEER_IP"
	NP_PEER_BIOS=$(echo "${NETPLAY_PEER_CAPS:-}" | sed -n 's/.*dcbios=\([^,]*\).*/\1/p')
	unset NETPLAY_ROLE NETPLAY_PEER_IP NETPLAY_MODE NETPLAY_PLAYER NETPLAY_NUM_PLAYERS NETPLAY_PEER_CAPS

	NP_HLE=1
	[ "$NP_BIOS" != none ] && [ "$NP_BIOS" = "$NP_PEER_BIOS" ] && NP_HLE=0

	if [ "$NP_ROLE" = host ]; then
		NP_HOST=1
		# the host plays on its real files; keep a one-deep copy of what it
		# brought to the session, for manual recovery
		NP_BACKUP="$SHARED_USERDATA_PATH/DC-flycast/netplay-backup"
		rm -rf "$NP_BACKUP"
		mkdir -p "$NP_BACKUP"
		cp "$NP_STAGE"/* "$NP_BACKUP/" 2>/dev/null
	else
		NP_HOST=0
		# the client plays on the host's files; missing ones stay missing (the
		# core starts a blank card) rather than falling back to its own
		mkdir -p "$NP_SAVES" "$NP_SYSTEM"
		[ -f "$NP_FETCH/nxcard.bin" ] && mv "$NP_FETCH/nxcard.bin" "$NP_SAVES/$NP_CARD"
		for NP_EXT in nvmem nvmem2 eeprom; do
			if [ -f "$NP_FETCH/nxarc.$NP_EXT" ]; then
				mkdir -p "$NP_SAVES/reicast"
				mv "$NP_FETCH/nxarc.$NP_EXT" "$NP_SAVES/reicast/$NP_ROMFILE.$NP_EXT"
			fi
		done
		[ -f "$NP_FETCH/nxnvmem.bin" ] && mv "$NP_FETCH/nxnvmem.bin" "$NP_SYSTEM/dc_nvmem.bin"
		[ -f "$NP_FETCH/nxa2.bin" ] && mv "$NP_FETCH/nxa2.bin" "$NP_SYSTEM/vmu_save_A2.bin"
		# the BIOS files the session uses: arcade sets always, the Dreamcast BIOS
		# only when both sides agreed on it
		for NP_F in "$NP_SYS_REAL"/*.zip; do
			[ -f "$NP_F" ] && cp "$NP_F" "$NP_SYSTEM/"
		done
		[ "$NP_HLE" = 0 ] && cp "$NP_SYS_REAL/dc_boot.bin" "$NP_SYSTEM/"
		export NETPLAY_SAVES_DIR="$NP_SAVES" NETPLAY_SYSTEM_DIR="$NP_SYSTEM"
	fi

	export NX_CORE_NETPLAY=1 NX_GGPO=1 NX_GGPO_HOST="$NP_HOST" NX_GGPO_SERVER="$NP_PEER" NX_GGPO_HLE="$NP_HLE"
}

# After the game (or a cancelled wizard): the session's copies in /tmp go -- the
# host's staged files and the client's working saves, settings and BIOS copies.
# The host's netplay-backup on the card stays, for recovery.
nx_dc_netplay_cleanup() {
	NP_TMP="${NX_DC_NETPLAY_TMP:-/tmp}"
	rm -rf "$NP_TMP/netplay-serve" "$NP_TMP/netplay-fetch" "$NP_TMP/netplay-saves" "$NP_TMP/netplay-system"
}
