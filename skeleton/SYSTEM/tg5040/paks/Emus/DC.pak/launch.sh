#!/bin/sh

EMU_EXE=flycast
CORES_PATH=$(dirname "$0")

###############################

EMU_TAG=$(basename "$(dirname "$0")" .pak)
ROM="$1"
HOME="$USERDATA_PATH"
cd "$HOME"

# Netplay on the libretro core arrives in a later change; until then a netplay
# launch runs as a plain one and the flag must not outlive it.
rm -f /tmp/netplay_launch

# First minarch launch of a game: carry the standalone flycast memory card,
# console settings and arcade saves over (copies, never moves).
. "$CORES_PATH/nx_dc_saves.sh"
nx_dc_seed_saves "$ROM"

# Single cluster: cpu0-3 (Cortex-A53). minarch_cpu_speed = Performance sets the clock.
echo 1 >/sys/devices/system/cpu/cpu1/online 2>/dev/null
echo 1 >/sys/devices/system/cpu/cpu2/online 2>/dev/null
echo 1 >/sys/devices/system/cpu/cpu3/online 2>/dev/null

minarch.elf "$CORES_PATH/${EMU_EXE}_libretro.so" "$ROM" &> "$LOGS_PATH/$EMU_TAG.txt"
