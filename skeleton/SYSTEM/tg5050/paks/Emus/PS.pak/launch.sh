#!/bin/sh

EMU_EXE=pcsx_rearmed

###############################

EMU_TAG=$(basename "$(dirname "$0")" .pak)
ROM="$1"
HOME="$USERDATA_PATH"
cd "$HOME"
. "$SHARED_SYSTEM_PATH/bin/netplay-prelaunch.sh"

# BIG cluster: bring cpu5 online for dual-core emulation
echo 1 >/sys/devices/system/cpu/cpu5/online 2>/dev/null

# GPU: lock to performance for PS1 hardware-accelerated OpenGL rendering
echo performance >/sys/devices/platform/soc@3000000/1800000.gpu/devfreq/1800000.gpu/governor 2>/dev/null

# Thread placement is minarch's job now (default.cfg: minarch_cpu_affinity = big):
# the emulation thread and every pcsxr-* thread inherit the big cores (cpu4+cpu5),
# helpers and mali-* are pinned to cpu0-1. Measured 2026-09-21 in
# scripts/bench/results/tg5050/AFFINITY.md (big2 vs the old taskset script).
minarch.elf "$CORES_PATH/${EMU_EXE}_libretro.so" "$ROM" &> "$LOGS_PATH/$EMU_TAG.txt"

if [ -f /tmp/netplay_session ]; then
	netplay.elf --cleanup >> "$LOGS_PATH/netplay-wizard.txt" 2>&1
fi
