#!/bin/sh

EMU_EXE=swanstation
CORES_PATH=$(dirname "$0")

###############################

EMU_TAG=$(basename "$(dirname "$0")" .pak)
ROM="$1"
HOME="$USERDATA_PATH"
cd "$HOME"
. "$SHARED_SYSTEM_PATH/bin/netplay-prelaunch.sh"

# BIG cluster: bring cpu5 online (default.cfg: minarch_cpu_affinity = big)
echo 1 >/sys/devices/system/cpu/cpu5/online 2>/dev/null

# GPU: lock to performance for SwanStation's hardware (GLES) renderer
echo performance >/sys/devices/platform/soc@3000000/1800000.gpu/devfreq/1800000.gpu/governor 2>/dev/null

minarch.elf "$CORES_PATH/${EMU_EXE}_libretro.so" "$ROM" &> "$LOGS_PATH/$EMU_TAG.txt"

if [ -f /tmp/netplay_session ]; then
	netplay.elf --cleanup >> "$LOGS_PATH/netplay-wizard.txt" 2>&1
fi
