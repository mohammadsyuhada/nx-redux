#!/bin/sh
# N64: mupen64plus-next (GLideN64 + Rice) in minarch. Rice is the default on
# these PowerVR devices (default*.cfg); Emulator Options switches per game.

EMU_EXE=mupen64plus_next
CORES_PATH=$(dirname "$0")

###############################

EMU_TAG=$(basename "$(dirname "$0")" .pak)
ROM="$1"
HOME="$USERDATA_PATH"
cd "$HOME"
SYS="${NX_N64_SYSFS_ROOT:-/sys}"

# Saves from the standalone N64.pak: the core copies a game's files in on its
# first launch here (patch 0004); an existing .srm wins, the old files stay.
export NX_M64P_LEGACY_SAVE_DIR="$SHARED_USERDATA_PATH/N64-mupen64plus/data/mupen64plus/save"
# GLideN64 hi-res texture packs and their cache stay where the standalone
# kept them (core patch 0007; the core's own default is Bios/N64/Mupen64plus).
export NX_M64P_TX_PATH="$SDCARD_PATH/Roms/Nintendo 64 (N64)/.hires_texture"
export NX_M64P_TX_CACHE_PATH="$SDCARD_PATH/Roms/Nintendo 64 (N64)/.cache"

# Hi-res texture packs (off by default; Emulator Options turns them on):
# GLideN64 keeps up to 200 MB of loaded textures (default*.cfg), more than
# the free RAM with a game running, so the standalone's 512 MB swap file is
# on for the session -- only for players who have packs or caches.
SWAPFILE="${NX_UDISK:-/mnt/UDISK}/n64_swap"
NX_HAS_TX=0
for NX_TXDIR in "$NX_M64P_TX_PATH" "$NX_M64P_TX_CACHE_PATH"; do
	[ -n "$(ls -A "$NX_TXDIR" 2>/dev/null)" ] && NX_HAS_TX=1
done
if [ "$NX_HAS_TX" = 1 ]; then
	if [ ! -f "$SWAPFILE" ]; then
		dd if=/dev/zero of="$SWAPFILE" bs=1M count="${NX_N64_SWAP_MB:-512}" 2>/dev/null
		mkswap "$SWAPFILE" >/dev/null 2>&1
	fi
	swapon "$SWAPFILE" 2>/dev/null
fi

# Netplay launch (Y / "Launch with Netplay"): wizard + m64p-server; a no-op
# for a plain launch
. "$CORES_PATH/nx_n64_netplay.sh"
nx_n64_netplay

# All four A53 cores: emulation, the GLideN64/Rice GL thread and audio.
echo 1 >"$SYS/devices/system/cpu/cpu1/online" 2>/dev/null
echo 1 >"$SYS/devices/system/cpu/cpu2/online" 2>/dev/null
echo 1 >"$SYS/devices/system/cpu/cpu3/online" 2>/dev/null

minarch.elf "$CORES_PATH/${EMU_EXE}_libretro.so" "$ROM" &> "$LOGS_PATH/$EMU_TAG.txt"

nx_n64_netplay_cleanup
swapoff "$SWAPFILE" 2>/dev/null
