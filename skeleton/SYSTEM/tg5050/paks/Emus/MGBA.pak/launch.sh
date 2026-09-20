#!/bin/sh

EMU_EXE=mgba
# Runs on the little cluster (scripts/bench 2026-09-20: full speed with cpu4
# offline). The launcher hands cpu4 over online; taking it down here gates
# the big cluster. minarch's startup pin to cpu4-7 then fails (logged, harmless).
echo 0 > /sys/devices/system/cpu/cpu4/online 2>/dev/null
CORES_PATH=$(dirname "$0")

###############################

EMU_TAG=$(basename "$(dirname "$0")" .pak)
ROM="$1"
HOME="$USERDATA_PATH"
cd "$HOME"
minarch.elf "$CORES_PATH/${EMU_EXE}_libretro.so" "$ROM" &> "$LOGS_PATH/$EMU_TAG.txt"
