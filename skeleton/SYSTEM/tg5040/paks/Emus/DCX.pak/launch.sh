#!/bin/sh

EMU_EXE=flycast_legacy
CORES_PATH=$(dirname "$0")

###############################

EMU_TAG=$(basename "$(dirname "$0")" .pak)
ROM="$1"
HOME="$USERDATA_PATH"
cd "$HOME"

# Single cluster: cpu0-3 (Cortex-A53). minarch_cpu_speed = Performance sets the clock.
echo 1 >/sys/devices/system/cpu/cpu1/online 2>/dev/null
echo 1 >/sys/devices/system/cpu/cpu2/online 2>/dev/null
echo 1 >/sys/devices/system/cpu/cpu3/online 2>/dev/null

# This core runs emulation on its own thread and is paced by minarch blocking
# its audio push (ma_audio.c)
NX_AUDIO_BLOCK=1 minarch.elf "$CORES_PATH/${EMU_EXE}_libretro.so" "$ROM" &> "$LOGS_PATH/$EMU_TAG.txt"
