#!/bin/sh

EMU_EXE=fbneo
CORES_PATH=$(dirname "$0")
###############################

EMU_TAG=$(basename "$(dirname "$0")" .pak)
ROM="$1"
HOME="$USERDATA_PATH"
cd "$HOME"
. "$SHARED_SYSTEM_PATH/bin/netplay-prelaunch.sh"
minarch.elf "$CORES_PATH/${EMU_EXE}_libretro.so" "$ROM" &> "$LOGS_PATH/$EMU_TAG.txt"
if [ -f /tmp/netplay_session ]; then
	netplay.elf --cleanup >> "$LOGS_PATH/netplay-wizard.txt" 2>&1
fi
