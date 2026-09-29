#!/usr/bin/env bash
# Host tests for DC.pak's nx_dc_saves.sh: the one-time copy of standalone
# flycast saves into the libretro core's layout on a game's first launch.
# Runs the tg5040 copy under a fake card and checks the tg5050 copy is identical.
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT="$PWD"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
FAIL=0
fail() { echo "FAIL: $*"; FAIL=1; }

SEEDER="$ROOT/skeleton/SYSTEM/tg5040/paks/Emus/DC.pak/nx_dc_saves.sh"
cmp -s "$SEEDER" "$ROOT/skeleton/SYSTEM/tg5050/paks/Emus/DC.pak/nx_dc_saves.sh" \
    || fail "tg5040/tg5050 nx_dc_saves.sh differ"

# fresh card with standalone data: shared cards, console nvmem, arcade saves
card() {
    rm -rf "$TMP/sd"
    OLD="$TMP/sd/.userdata/shared/DC-flycast/data/flycast"
    mkdir -p "$OLD" "$TMP/sd/Saves" "$TMP/sd/Bios"
    echo A1 > "$OLD/vmu_save_A1.bin"
    echo A2 > "$OLD/vmu_save_A2.bin"
    echo NV > "$OLD/dc_nvmem.bin"
    echo MS6 > "$OLD/mslug6.zip.nvmem"
    echo MS6b > "$OLD/mslug6.zip.nvmem2"
    echo KOF > "$OLD/KOFXI.ZIP.nvmem"
    echo KOFe > "$OLD/KOFXI.ZIP.eeprom"
}
seed() { # $1 = ROM path
    SDCARD_PATH="$TMP/sd" SAVES_PATH="$TMP/sd/Saves" \
    SHARED_USERDATA_PATH="$TMP/sd/.userdata/shared" EMU_TAG=DC ROM="$1" \
    sh -c '. "$0"; nx_dc_seed_saves' "$SEEDER"
}
S="$TMP/sd/Saves/DC"
B="$TMP/sd/Bios/DC"

# console game: its own card, console-wide files to Bios/DC
card
seed "/mnt/SDCARD/Roms/Dreamcast (DC)/Soulcalibur (USA).chd"
[ "$(cat "$S/Soulcalibur (USA).A1.bin" 2>/dev/null)" = A1 ] || fail "console: card not seeded"
[ "$(cat "$B/vmu_save_A2.bin" 2>/dev/null)" = A2 ] || fail "console: A2 not seeded"
[ "$(cat "$B/dc_nvmem.bin" 2>/dev/null)" = NV ] || fail "console: dc_nvmem not seeded"
[ ! -e "$S/reicast" ] || fail "console: arcade dir created"

# multi-disc: "(Disc N)" / "(Disc N of M)" dropped, as flycast patch 0003 does
seed "/mnt/SDCARD/Roms/Dreamcast (DC)/Skies of Arcadia (USA) (Disc 2).chd"
[ -f "$S/Skies of Arcadia (USA).A1.bin" ] || fail "disc: card not named without (Disc N)"
seed "/mnt/SDCARD/Roms/Dreamcast (DC)/Shenmue/Shenmue (Disc 1 of 3) (Rev A).gdi"
[ -f "$S/Shenmue (Rev A).A1.bin" ] || fail "disc: (Disc N of M) not dropped"

# never overwrites: a card the player already uses wins
echo MINE > "$S/Soulcalibur (USA).A1.bin"
echo MINE > "$B/dc_nvmem.bin"
seed "/mnt/SDCARD/Roms/Dreamcast (DC)/Soulcalibur (USA).chd"
[ "$(cat "$S/Soulcalibur (USA).A1.bin")" = MINE ] || fail "overwrote an existing card"
[ "$(cat "$B/dc_nvmem.bin")" = MINE ] || fail "overwrote an existing dc_nvmem"

# arcade: saves under reicast/ with the full file name, no memory card
card
seed "/mnt/SDCARD/Roms/Dreamcast (DC)/mslug6.zip"
[ "$(cat "$S/reicast/mslug6.zip.nvmem" 2>/dev/null)" = MS6 ] || fail "arcade: nvmem not seeded"
[ "$(cat "$S/reicast/mslug6.zip.nvmem2" 2>/dev/null)" = MS6b ] || fail "arcade: nvmem2 not seeded"
[ ! -e "$S/mslug6.A1.bin" ] || fail "arcade: got a memory card"

# arcade set with an upper-case extension (FAT keeps whatever a copy tool wrote)
seed "/mnt/SDCARD/Roms/Dreamcast (DC)/KOFXI.ZIP"
[ "$(cat "$S/reicast/KOFXI.ZIP.nvmem" 2>/dev/null)" = KOF ] || fail "arcade .ZIP: nvmem not seeded"
[ "$(cat "$S/reicast/KOFXI.ZIP.eeprom" 2>/dev/null)" = KOFe ] || fail "arcade .ZIP: eeprom not seeded"
[ ! -e "$S/KOFXI.A1.bin" ] || fail "arcade .ZIP: got a memory card"

# RetroArch-style Bios/DC/dc/ takes the console-wide files
card
mkdir -p "$B/dc"
seed "/mnt/SDCARD/Roms/Dreamcast (DC)/Soulcalibur (USA).chd"
[ -f "$B/dc/dc_nvmem.bin" ] && [ ! -e "$B/dc_nvmem.bin" ] || fail "dc/: nvmem not seeded into Bios/DC/dc"

# no standalone data: nothing created
rm -rf "$TMP/sd"; mkdir -p "$TMP/sd"
seed "/mnt/SDCARD/Roms/Dreamcast (DC)/Soulcalibur (USA).chd"
[ ! -e "$S" ] && [ ! -e "$B" ] || fail "no standalone data: dirs created"

# once per game: a card the player deleted (to start fresh, or to free a full
# copy of the shared card) is not copied back on the next launch
card
seed "/mnt/SDCARD/Roms/Dreamcast (DC)/Soulcalibur (USA).chd"
seed "/mnt/SDCARD/Roms/Dreamcast (DC)/mslug6.zip"
rm -f "$S/Soulcalibur (USA).A1.bin" "$S/reicast/mslug6.zip.nvmem" "$B/dc_nvmem.bin"
seed "/mnt/SDCARD/Roms/Dreamcast (DC)/Soulcalibur (USA).chd"
seed "/mnt/SDCARD/Roms/Dreamcast (DC)/mslug6.zip"
[ ! -e "$S/Soulcalibur (USA).A1.bin" ] || fail "once: deleted card copied back"
[ ! -e "$S/reicast/mslug6.zip.nvmem" ] || fail "once: deleted arcade nvmem copied back"
[ ! -e "$B/dc_nvmem.bin" ] || fail "once: deleted dc_nvmem copied back"
# ...while a game never launched before still gets its copy
seed "/mnt/SDCARD/Roms/Dreamcast (DC)/Crazy Taxi 2 v1.004 (2001)(Sega)(US)[!].chd"
[ -f "$S/Crazy Taxi 2 v1.004 (2001)(Sega)(US)[!].A1.bin" ] || fail "once: new game (brackets in name) not seeded"
# a card that existed before the first launch (copied from a phone) is left
# alone and counts as done
card
mkdir -p "$S"; echo PHONE > "$S/Soulcalibur (USA).A1.bin"
seed "/mnt/SDCARD/Roms/Dreamcast (DC)/Soulcalibur (USA).chd"
rm -f "$S/Soulcalibur (USA).A1.bin"
seed "/mnt/SDCARD/Roms/Dreamcast (DC)/Soulcalibur (USA).chd"
[ ! -e "$S/Soulcalibur (USA).A1.bin" ] || fail "once: pre-existing card counted as not done"

# the standalone files themselves are never changed
card
seed "/mnt/SDCARD/Roms/Dreamcast (DC)/mslug6.zip"
seed "/mnt/SDCARD/Roms/Dreamcast (DC)/Soulcalibur (USA).chd"
[ "$(cat "$OLD/vmu_save_A1.bin")" = A1 ] && [ "$(cat "$OLD/mslug6.zip.nvmem")" = MS6 ] \
    || fail "standalone files changed"

[ "$FAIL" = 0 ] && echo "test-dc-saves-sh: OK"
exit "$FAIL"
