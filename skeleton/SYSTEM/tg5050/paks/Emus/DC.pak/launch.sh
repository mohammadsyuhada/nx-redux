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

# BIG cluster: bring cpu5 online. Thread placement is minarch's job
# (default.cfg: minarch_cpu_affinity = big), as in PS.pak.
echo 1 >/sys/devices/system/cpu/cpu5/online 2>/dev/null

# GPU: lock to performance for flycast rendering
echo performance >/sys/devices/platform/soc@3000000/1800000.gpu/devfreq/1800000.gpu/governor 2>/dev/null

minarch.elf "$CORES_PATH/${EMU_EXE}_libretro.so" "$ROM" &> "$LOGS_PATH/$EMU_TAG.txt"

# the wizard's session teardown (hotspot/WiFi restore) and the session's
# copies; harmless on a plain launch
if [ -f "${NETPLAY_SESSION_FILE:-/tmp/netplay_session}" ]; then
	netplay.elf --cleanup >> "$LOGS_PATH/netplay-wizard.txt" 2>&1
	nx_dc_netplay_cleanup
fi
