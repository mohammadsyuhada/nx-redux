#!/bin/sh

EMU_EXE=mgba

###############################

EMU_TAG=$(basename "$(dirname "$0")" .pak)
ROM="$1"
HOME="$USERDATA_PATH"
cd "$HOME"
. "$SHARED_SYSTEM_PATH/bin/netplay-prelaunch.sh"
# Netplay runs gpsp: the GBA link engine is gpsp's RFU (netpacket) and the
# other player's device runs gpsp too. mgba stays the core for normal play.
if [ -n "$NETPLAY_ROLE" ]; then
	EMU_EXE=gpsp
fi
minarch.elf "$CORES_PATH/${EMU_EXE}_libretro.so" "$ROM" > "$LOGS_PATH/$EMU_TAG.txt" 2>&1
if [ -f /tmp/netplay_session ]; then
	netplay.elf --cleanup >> "$LOGS_PATH/netplay-wizard.txt" 2>&1
fi
