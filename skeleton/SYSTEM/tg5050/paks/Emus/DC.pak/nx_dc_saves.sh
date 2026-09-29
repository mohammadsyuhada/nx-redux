# Sourced by DC.pak's launch.sh. Carries saves over from standalone flycast
# (before NX Redux ran Dreamcast on minarch) the first time they are needed:
# copies only, the standalone files under .userdata/shared/DC-flycast/ are
# never changed. Save states are not carried over (different emulator version
# and container).
nx_dc_seed_saves() {
	OLD="$SHARED_USERDATA_PATH/DC-flycast/data/flycast"
	[ -d "$OLD" ] || return 0
	ROMFILE=$(basename "$ROM")
	SAVES="$SAVES_PATH/$EMU_TAG"
	# flycast (patch 0005) uses Bios/DC/ itself unless a RetroArch-style dc/ exists
	SYS="$SDCARD_PATH/Bios/$EMU_TAG"
	[ -d "$SYS/dc" ] && SYS="$SYS/dc"
	mkdir -p "$SAVES" "$SYS"

	case "$ROMFILE" in
	*.zip | *.7z)
		# Arcade (NAOMI/Atomiswave): settings and high scores, same names under
		# the core's reicast/ save folder
		for EXT in nvmem nvmem2 eeprom; do
			if [ -f "$OLD/$ROMFILE.$EXT" ] && [ ! -f "$SAVES/reicast/$ROMFILE.$EXT" ]; then
				mkdir -p "$SAVES/reicast"
				cp "$OLD/$ROMFILE.$EXT" "$SAVES/reicast/$ROMFILE.$EXT"
			fi
		done
		;;
	*)
		# Console: this game's own card (flycast patch 0003 names it after the
		# ROM without its extension or "(Disc N)" tag) starts as a copy of
		# standalone's shared card, which holds every game's saves
		NAME=$(echo "${ROMFILE%.*}" | sed 's/ (Disc [^)]*)//')
		if [ -f "$OLD/vmu_save_A1.bin" ] && [ ! -f "$SAVES/$NAME.A1.bin" ]; then
			cp "$OLD/vmu_save_A1.bin" "$SAVES/$NAME.A1.bin"
		fi
		;;
	esac

	# Shared across games: the second card slot, and the console's settings/clock
	for F in vmu_save_A2.bin dc_nvmem.bin; do
		if [ -f "$OLD/$F" ] && [ ! -f "$SYS/$F" ]; then
			cp "$OLD/$F" "$SYS/$F"
		fi
	done
}
