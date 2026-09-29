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

# BIG cluster: bring cpu5 online. Thread placement is minarch's job
# (default.cfg: minarch_cpu_affinity = big), as in PS.pak.
echo 1 >/sys/devices/system/cpu/cpu5/online 2>/dev/null

# GPU: lock to performance for flycast rendering
echo performance >/sys/devices/platform/soc@3000000/1800000.gpu/devfreq/1800000.gpu/governor 2>/dev/null

minarch.elf "$CORES_PATH/${EMU_EXE}_libretro.so" "$ROM" &> "$LOGS_PATH/$EMU_TAG.txt"
