#!/usr/bin/env bash
# Fixture test for the GBA pak's Classic NES Series switch: a bare .gba whose
# header has the fixed 0x96 at 0xB2 and a game code at 0xAC of four letters or
# digits starting with F runs MGBA.pak's mgba core instead of gpSP; anything
# else stays on gpSP. Runs each platform's real launch.sh with stubbed
# minarch.elf / netplay / shader helpers, under sh and (if present) dash. The
# device runs busybox ash: dd, printf and case are the same there.
set -euo pipefail
cd "$(dirname "$0")/../.."
REPO="$PWD"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
fail() { echo "FAIL: $1" >&2; exit 1; }

BIN="$TMP/bin"
SHARED="$TMP/shared"
mkdir -p "$BIN" "$SHARED/bin" "$TMP/logs" "$TMP/userdata" "$TMP/cores" "$TMP/roms"
printf '#!/bin/sh\necho "core=$1"\n' > "$BIN/minarch.elf"
printf '#!/bin/sh\n' > "$BIN/netplay.elf"
chmod +x "$BIN/minarch.elf" "$BIN/netplay.elf"
# the netplay step sees the core the pak chose
printf 'echo "$EMU_EXE" > "$LOGS_PATH/prelaunch-exe"\n' > "$SHARED/bin/netplay-prelaunch.sh"
printf 'nx_gpu_shader_boost() { :; }\n' > "$SHARED/bin/gpu-shader-boost.sh"

# rom <name> <game code> <byte at 0xB2, octal escape>
rom() {
	{
		head -c 172 /dev/zero
		printf '%s01' "$2"
		printf "$3"
		head -c 1000 /dev/zero
	} > "$TMP/roms/$1"
}
rom "Pac-Man.gba" FP7E '\226'
rom "Pac-Man.GBA" FP7E '\226'
rom "Pac-Man.zip" FP7E '\226'
rom "FireRed.gba" BPRE '\226'
rom "NoFixed.gba" FP7E '\000'
rom "Lower.gba" fp7e '\226'
rom "Punct.gba" "FP-E" '\226'
: > "$TMP/roms/Empty.gba"

for plat in tg5040 tg5050; do
	for shell in sh dash; do
		command -v "$shell" > /dev/null || continue
		EMUS="$TMP/$plat-$shell/Emus"
		mkdir -p "$EMUS/GBA.pak" "$EMUS/MGBA.pak"
		cp "$REPO/skeleton/SYSTEM/$plat/paks/Emus/GBA.pak/launch.sh" "$EMUS/GBA.pak/"
		: > "$EMUS/MGBA.pak/mgba_libretro.so"

		# expect <rom> <want core basename> <want note: 1/0>
		expect() {
			PATH="$BIN:$PATH" USERDATA_PATH="$TMP/userdata" LOGS_PATH="$TMP/logs" CORES_PATH="$TMP/cores" \
				SHARED_SYSTEM_PATH="$SHARED" "$shell" "$EMUS/GBA.pak/launch.sh" "$TMP/roms/$1" 2> /dev/null
			log="$TMP/logs/GBA.txt"
			core=$(sed -n 's/^core=//p' "$log")
			[ "$(basename "$core")" = "$2" ] || fail "$plat/$shell $1: core '$core', want $2"
			[ -f "$core" ] || [ "$2" = gpsp_libretro.so ] || fail "$plat/$shell $1: core path '$core' missing"
			want_exe=${2%_libretro.so}
			[ "$(cat "$TMP/logs/prelaunch-exe")" = "$want_exe" ] || fail "$plat/$shell $1: netplay saw $(cat "$TMP/logs/prelaunch-exe")"
			if grep -q "Classic NES Series cart" "$log"; then note=1; else note=0; fi
			[ "$note" = "$3" ] || fail "$plat/$shell $1: log note $note, want $3"
		}
		expect "Pac-Man.gba" mgba_libretro.so 1
		expect "Pac-Man.GBA" mgba_libretro.so 1
		expect "Pac-Man.zip" gpsp_libretro.so 0
		expect "FireRed.gba" gpsp_libretro.so 0
		expect "NoFixed.gba" gpsp_libretro.so 0
		expect "Lower.gba" gpsp_libretro.so 0
		expect "Punct.gba" gpsp_libretro.so 0
		expect "Empty.gba" gpsp_libretro.so 0
		rm "$EMUS/MGBA.pak/mgba_libretro.so"
		expect "Pac-Man.gba" gpsp_libretro.so 0 # no MGBA.pak core: stay on gpSP
	done
done
echo "PASS: GBA pak runs Classic NES Series carts on mgba"
