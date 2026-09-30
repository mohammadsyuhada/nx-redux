#!/bin/sh

EMU_EXE=ppsspp
CORES_PATH=$(dirname "$0")

###############################

EMU_TAG=$(basename "$(dirname "$0")" .pak)
ROM="$1"
HOME="$USERDATA_PATH"
cd "$HOME"

# PPSSPP's runtime assets (fonts, flash0, vfpu tables, PPGe atlas) ship in the
# pak and update with the system; Bios/PSP stays the player's.
export NX_PPSSPP_ASSETS="$CORES_PATH/PPSSPP"

# Bring cpu5 (big cluster) online. PPSSPP's threads are left unpinned
# (default.cfg sets no minarch_cpu_affinity): pinning them to the big cluster
# measured slower.
echo 1 >/sys/devices/system/cpu/cpu5/online 2>/dev/null

# GPU: lock to performance for PPSSPP rendering
echo performance >/sys/devices/platform/soc@3000000/1800000.gpu/devfreq/1800000.gpu/governor 2>/dev/null

minarch.elf "$CORES_PATH/${EMU_EXE}_libretro.so" "$ROM" &> "$LOGS_PATH/$EMU_TAG.txt"
