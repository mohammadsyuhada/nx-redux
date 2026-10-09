#!/bin/sh

EMU_EXE=mgba
CORES_PATH=$(dirname "$0")

###############################

EMU_TAG=$(basename "$(dirname "$0")" .pak)
ROM="$1"
HOME="$USERDATA_PATH"
cd "$HOME"
. "$SHARED_SYSTEM_PATH/bin/netplay-prelaunch.sh"
# Runs on the little cluster (scripts/bench 2026-09-20: full speed with cpu4
# offline). The launcher hands cpu4 over online; taking it down here gates
# the big cluster. minarch's startup pin to cpu4-7 then fails (logged, harmless).
# A USB Cable link keeps it: the real cable's waits (~6 ms a frame in a trade)
# don't fit on the little cluster, and the audio breaks up.
if [ "$NETPLAY_MODE" != "usb" ]; then
	echo 0 > /sys/devices/system/cpu/cpu4/online 2>/dev/null
fi
minarch.elf "$CORES_PATH/${EMU_EXE}_libretro.so" "$ROM" &> "$LOGS_PATH/$EMU_TAG.txt"
if [ -f /tmp/netplay_session ]; then
	netplay.elf --cleanup >> "$LOGS_PATH/netplay-wizard.txt" 2>&1
fi
