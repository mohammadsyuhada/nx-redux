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

# Single cluster: cpu0-3 (Cortex-A53). minarch_cpu_speed = Performance sets the clock.
echo 1 >/sys/devices/system/cpu/cpu1/online 2>/dev/null
echo 1 >/sys/devices/system/cpu/cpu2/online 2>/dev/null
echo 1 >/sys/devices/system/cpu/cpu3/online 2>/dev/null

minarch.elf "$CORES_PATH/${EMU_EXE}_libretro.so" "$ROM" &> "$LOGS_PATH/$EMU_TAG.txt"
