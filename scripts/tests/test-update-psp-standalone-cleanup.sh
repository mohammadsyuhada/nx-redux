#!/usr/bin/env bash
# Host test for the psp-standalone-cleanup block in each platform's
# install/update.sh: PSP moved from the standalone PPSSPP pak (Xtras, ben16w
# minui-psp) to the PPSSPP libretro core in the system PSP.pak. The block
# removes the standalone (platform dirs, legacy flat install, Xtras-uninstall
# leftovers) and its Xtras record, carries texture packs over, and moves
# standalone saves (Saves/PSP/<ID>) into the libretro memory-stick layout
# (Saves/PSP/SAVEDATA/<ID>: PPSSPP maps ms0:/PSP straight onto a save dir named
# PSP) without ever overwriting. This extracts the
# marked block from each update.sh and runs it under sh against a fake card.
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT="$PWD"; TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
FAIL=0; fail() { echo "FAIL: $*" >&2; FAIL=1; }

standalone() { mkdir -p "$1/PPSSPP"; : > "$1/PPSSPP/PPSSPPSDL_tg5040"; : > "$1/launch.sh"; }
save() { mkdir -p "$1"; printf '%s' "$2" > "$1/PARAM.SFO"; printf 'data-%s' "$2" > "$1/DATA.BIN"; }

for PLAT in tg5040 tg5050; do
	UPDATE="$ROOT/workspace/$PLAT/install/update.sh"
	BLOCK="$(sed -n '/^# --- psp-standalone-cleanup-begin/,/^# --- psp-standalone-cleanup-end/p' "$UPDATE")"
	[ -n "$BLOCK" ] || { fail "$PLAT: psp-standalone-cleanup block not found in update.sh"; continue; }
	C="$TMP/$PLAT"; mkdir -p "$C"
	standalone "$C/Emus/tg5040/PSP.pak"; standalone "$C/Emus/tg5050/PSP.pak"; standalone "$C/Emus/PSP.pak"
	mkdir -p "$C/Emus/tg5040/PSP.pak/PPSSPP/.config/ppsspp/PSP/TEXTURES/ULUS10466"
	: > "$C/Emus/tg5040/PSP.pak/PPSSPP/.config/ppsspp/PSP/TEXTURES/ULUS10466/tex.png"
	MS=$C/Emus/tg5040/PSP.pak/PPSSPP/.config/ppsspp/PSP
	mkdir -p "$MS/GAME/NPUH10001" "$MS/SYSTEM" "$MS/Cheats" "$MS/TEXTURES/ULES00002" "$C/Saves/PSP/TEXTURES/ULES00002"
	echo dlc > "$MS/GAME/NPUH10001/EBOOT.PBP"; echo ini > "$MS/SYSTEM/ppsspp.ini"; echo cw > "$MS/Cheats/ULUS10466.ini"
	echo standalone > "$MS/TEXTURES/ULES00002/t.png"; echo live > "$C/Saves/PSP/TEXTURES/ULES00002/t.png"
	mkdir -p "$C/.userdata/shared/xtras"; echo v6 > "$C/.userdata/shared/xtras/psp.version"; echo v7 > "$C/.userdata/shared/xtras/psp.latest"
	echo keep > "$C/.userdata/shared/xtras/cheatdb.version"
	mkdir -p "$C/.userdata/shared/PSP-ppsspp"; : > "$C/.userdata/shared/PSP-ppsspp/state.ppst"
	save "$C/Saves/PSP/ULUS10466DATA" old            # plain standalone save -> moves
	save "$C/Saves/PSP/ULUS10123 SAVE" sp             # name with a space -> moves
	save "$C/Saves/PSP/ULES00001" standalone          # conflict: libretro copy exists
	save "$C/Saves/PSP/SAVEDATA/ULES00001" libretro
	mkdir -p "$C/Saves/PSP/notasave"; : > "$C/Saves/PSP/notasave/x"   # no PARAM.SFO -> stays
	: > "$C/Saves/PSP/readme.txt"                      # plain file -> stays
	mkdir -p "$C/Saves/PSP/NAND/flash0"                # libretro's own dir -> untouched
	SDCARD_PATH="$C" sh -c "$BLOCK" >/dev/null 2>&1 || fail "$PLAT: block exited non-zero"

	for p in Emus/tg5040/PSP.pak Emus/tg5050/PSP.pak Emus/PSP.pak Emus/tg5040 Emus/tg5050; do
		[ ! -e "$C/$p" ] || fail "$PLAT: $p not removed"
	done
	[ ! -e "$C/.userdata/shared/xtras/psp.version" ] || fail "$PLAT: xtras psp record kept"
	[ ! -e "$C/.userdata/shared/xtras/psp.latest" ] || fail "$PLAT: xtras psp update-check cache kept"
	[ -f "$C/.userdata/shared/xtras/cheatdb.version" ] || fail "$PLAT: other xtras record removed"
	[ -f "$C/.userdata/shared/PSP-ppsspp/state.ppst" ] || fail "$PLAT: standalone save states touched"
	[ -f "$C/Saves/PSP/TEXTURES/ULUS10466/tex.png" ] || fail "$PLAT: textures not carried over"
	[ "$(cat "$C/Saves/PSP/GAME/NPUH10001/EBOOT.PBP" 2>/dev/null)" = dlc ] || fail "$PLAT: GAME (DLC/homebrew) not carried over"
	BK="$C/Saves/PSP/standalone-backup/tg5040"
	[ "$(cat "$BK/SYSTEM/ppsspp.ini" 2>/dev/null)" = ini ] || fail "$PLAT: standalone settings not kept in backup"
	[ "$(cat "$BK/Cheats/ULUS10466.ini" 2>/dev/null)" = cw ] || fail "$PLAT: standalone cheats not kept in backup"
	[ "$(cat "$C/Saves/PSP/TEXTURES/ULES00002/t.png")" = live ] || fail "$PLAT: texture clash overwrote the live copy"
	[ "$(cat "$BK/TEXTURES/ULES00002/t.png" 2>/dev/null)" = standalone ] || fail "$PLAT: clashing standalone texture pack lost"
	[ "$(cat "$C/Saves/PSP/SAVEDATA/ULUS10466DATA/PARAM.SFO")" = old ] || fail "$PLAT: save not moved"
	[ "$(cat "$C/Saves/PSP/SAVEDATA/ULUS10466DATA/DATA.BIN")" = data-old ] || fail "$PLAT: save data not moved"
	[ ! -e "$C/Saves/PSP/ULUS10466DATA" ] || fail "$PLAT: moved save left behind"
	[ -f "$C/Saves/PSP/SAVEDATA/ULUS10123 SAVE/PARAM.SFO" ] || fail "$PLAT: spaced save not moved"
	[ "$(cat "$C/Saves/PSP/SAVEDATA/ULES00001/PARAM.SFO")" = libretro ] || fail "$PLAT: conflict overwrote libretro save"
	[ "$(cat "$C/Saves/PSP/ULES00001/PARAM.SFO")" = standalone ] || fail "$PLAT: conflict source removed"
	[ -f "$C/Saves/PSP/notasave/x" ] && [ -f "$C/Saves/PSP/readme.txt" ] || fail "$PLAT: non-save moved"
	[ -d "$C/Saves/PSP/NAND/flash0" ] && [ ! -e "$C/Saves/PSP/SAVEDATA/NAND" ] || fail "$PLAT: NAND touched"

	# a user PSP.pak that is not the standalone is not this block's to remove
	# (the shipped PSP.pak wins over it anyway); second run is a no-op
	mkdir -p "$C/Emus/$PLAT/PSP.pak"; echo mine > "$C/Emus/$PLAT/PSP.pak/launch.sh"
	SDCARD_PATH="$C" sh -c "$BLOCK" >/dev/null 2>&1 || fail "$PLAT: second run failed"
	[ "$(cat "$C/Emus/$PLAT/PSP.pak/launch.sh" 2>/dev/null)" = mine ] || fail "$PLAT: custom pak removed"
	[ "$(cat "$C/Saves/PSP/SAVEDATA/ULUS10466DATA/PARAM.SFO")" = old ] || fail "$PLAT: second run changed saves"

	# Xtras-uninstall leftover (config shell, no launcher/binary): textures carried, shell removed
	L="$TMP/leftover-$PLAT"
	mkdir -p "$L/Emus/$PLAT/PSP.pak/PPSSPP/.config/ppsspp/PSP/TEXTURES/NPJH50001" "$L/Emus/$PLAT/Other.pak"
	: > "$L/Emus/$PLAT/PSP.pak/PPSSPP/.config/ppsspp/PSP/TEXTURES/NPJH50001/t.png"
	SDCARD_PATH="$L" sh -c "$BLOCK" >/dev/null 2>&1 || fail "$PLAT: leftover card failed"
	[ ! -e "$L/Emus/$PLAT/PSP.pak" ] || fail "$PLAT: uninstall leftover not removed"
	[ -d "$L/Emus/$PLAT/Other.pak" ] || fail "$PLAT: non-empty platform dir removed"
	[ -f "$L/Saves/PSP/TEXTURES/NPJH50001/t.png" ] || fail "$PLAT: leftover textures not carried"
	[ ! -e "$L/Saves/PSP/standalone-backup" ] || fail "$PLAT: backup made with nothing left to keep"

	# empty card: no-op, succeeds, creates nothing
	E="$TMP/empty-$PLAT"; mkdir -p "$E"
	SDCARD_PATH="$E" sh -c "$BLOCK" >/dev/null 2>&1 || fail "$PLAT: empty card failed"
	[ -z "$(ls -A "$E")" ] || fail "$PLAT: empty card got files: $(ls -A "$E")"
done
[ "$FAIL" = 0 ] && echo "PASS: psp-standalone-cleanup" || exit 1
