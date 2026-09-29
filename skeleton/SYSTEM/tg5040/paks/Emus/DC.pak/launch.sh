#!/bin/sh

EMU_EXE=flycast
CORES_PATH=$(dirname "$0")

###############################

EMU_TAG=$(basename "$(dirname "$0")" .pak)
ROM="$1"
HOME="$USERDATA_PATH"
cd "$HOME"

# First minarch launch of a game: carry the standalone flycast memory card,
# console settings and arcade saves over (copies, never moves).
. "$CORES_PATH/nx_dc_saves.sh"
nx_dc_seed_saves "$ROM"

# Netplay launch (Y / "Launch with Netplay"): the wizard, the host's saves and
# the GGPO session for the core; a no-op for a plain launch
. "$CORES_PATH/nx_dc_netplay.sh"
nx_dc_netplay

# Single cluster: cpu0-3 (Cortex-A53). minarch_cpu_speed = Performance sets the clock.
echo 1 >/sys/devices/system/cpu/cpu1/online 2>/dev/null
echo 1 >/sys/devices/system/cpu/cpu2/online 2>/dev/null
echo 1 >/sys/devices/system/cpu/cpu3/online 2>/dev/null

minarch.elf "$CORES_PATH/${EMU_EXE}_libretro.so" "$ROM" &> "$LOGS_PATH/$EMU_TAG.txt"

# the wizard's session teardown (hotspot/WiFi restore) and the session's
# copies; harmless on a plain launch
if [ -f "${NETPLAY_SESSION_FILE:-/tmp/netplay_session}" ]; then
	netplay.elf --cleanup >> "$LOGS_PATH/netplay-wizard.txt" 2>&1
	nx_dc_netplay_cleanup
fi
