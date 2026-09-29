# Sourced by DC.pak's launch.sh. Carries saves over from standalone flycast
# (before NX Redux ran Dreamcast on minarch) the first time they are needed:
# copies only, the standalone files under .userdata/shared/DC-flycast/ are
# never changed. Save states are not carried over (different emulator version
# and container).
#
# Each copy happens once: what was handled is listed in $DONE, so a card the
# player deletes (to start fresh, or to free a copy of a nearly full shared
# card) is not copied back on the next launch. A file that already exists when
# a game is first seen (e.g. a card copied from a phone) is kept and counts as
# handled too.
nx_dc_seed_saves() {
	OLD="$SHARED_USERDATA_PATH/DC-flycast/data/flycast"
	[ -d "$OLD" ] || return 0
	DONE="$SHARED_USERDATA_PATH/DC-flycast/nx-seeded.txt"
	ROMFILE=$(basename "$ROM")
	SAVES="$SAVES_PATH/$EMU_TAG"
	# flycast (patch 0005) uses Bios/DC/ itself unless a RetroArch-style dc/ exists
	SYS="$SDCARD_PATH/Bios/$EMU_TAG"
	[ -d "$SYS/dc" ] && SYS="$SYS/dc"
	mkdir -p "$SAVES" "$SYS"

	# arcade sets are .zip/.7z in any case (FAT keeps whatever a copy tool wrote)
	case "$(echo "${ROMFILE##*.}" | tr 'A-Z' 'a-z')" in
	zip | 7z)
		# Arcade (NAOMI/Atomiswave): settings and high scores, same names under
		# the core's reicast/ save folder
		for EXT in nvmem nvmem2 eeprom; do
			nx_dc_seed_once "$OLD/$ROMFILE.$EXT" "$SAVES/reicast/$ROMFILE.$EXT" "arcade:$ROMFILE.$EXT"
		done
		;;
	*)
		# Console: this game's own card (flycast patch 0003 names it after the
		# ROM without its extension or "(Disc N)" tag) starts as a copy of
		# standalone's shared card, which holds every game's saves
		NAME=$(echo "${ROMFILE%.*}" | sed 's/ (Disc [^)]*)//')
		nx_dc_seed_once "$OLD/vmu_save_A1.bin" "$SAVES/$NAME.A1.bin" "card:$NAME"
		;;
	esac

	# Shared across games: the second card slot, and the console's settings/clock
	for F in vmu_save_A2.bin dc_nvmem.bin; do
		nx_dc_seed_once "$OLD/$F" "$SYS/$F" "system:$F"
	done
}

# nx_dc_seed_once <standalone file> <destination> <key>: copy once per key
nx_dc_seed_once() {
	[ -f "$1" ] || return 0
	grep -Fxq "$3" "$DONE" 2>/dev/null && return 0
	if [ ! -f "$2" ]; then
		mkdir -p "$(dirname "$2")"
		cp "$1" "$2" || return 0
	fi
	echo "$3" >> "$DONE"
}
