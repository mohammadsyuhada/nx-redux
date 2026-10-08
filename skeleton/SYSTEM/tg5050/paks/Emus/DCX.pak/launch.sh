#!/bin/sh

EMU_EXE=flycast_legacy
CORES_PATH=$(dirname "$0")

###############################

EMU_TAG=$(basename "$(dirname "$0")" .pak)
ROM="$1"
HOME="$USERDATA_PATH"
cd "$HOME"

# BIG cluster: bring cpu5 online. Thread placement is minarch's job
# (default.cfg: minarch_cpu_affinity = big), as in DC.pak.
echo 1 >/sys/devices/system/cpu/cpu5/online 2>/dev/null
# GPU: lock to performance for flycast rendering
echo performance >/sys/devices/platform/soc@3000000/1800000.gpu/devfreq/1800000.gpu/governor 2>/dev/null

# This core runs emulation on its own thread and is paced by minarch blocking
# its audio push (ma_audio.c)
NX_AUDIO_BLOCK=1 minarch.elf "$CORES_PATH/${EMU_EXE}_libretro.so" "$ROM" &> "$LOGS_PATH/$EMU_TAG.txt"
