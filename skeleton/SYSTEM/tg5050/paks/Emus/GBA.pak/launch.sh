#!/bin/sh

EMU_EXE=gpsp

###############################

EMU_TAG=$(basename "$(dirname "$0")" .pak)
ROM="$1"
HOME="$USERDATA_PATH"
cd "$HOME"
. "$SHARED_SYSTEM_PATH/bin/netplay-prelaunch.sh"
# All four little cores, uncapped, with the big cluster gated (scripts/bench
# 2026-10-11, FireRed mid-game): on cpu4 alone 9.8% of frames were late, on
# cpu0-3 none. The launcher takes cpu2/3 offline and cpu4 back up on exit.
# minarch's startup pin to cpu4-7 then fails (logged, harmless).
# A USB Cable link runs there too: measured with mGBA (MGBA.pak), which is
# heavier than gpSP; minarch runs at Performance while link traffic flows.
echo 1 > /sys/devices/system/cpu/cpu2/online 2>/dev/null
echo 1 > /sys/devices/system/cpu/cpu3/online 2>/dev/null
echo 0 > /sys/devices/system/cpu/cpu4/online 2>/dev/null
# While a shader is on, the GPU runs at performance (gpu-shader-boost.sh) and
# minarch's Auto holds the CPU at its top (1416 MHz on the little cluster):
# lcd-perfect was 50.3 fps with 72.8% of frames late on v2.1.0, 0.1% late so.
export NX_SHADER_CPU_MAX=1
. "$SHARED_SYSTEM_PATH/bin/gpu-shader-boost.sh"
nx_gpu_shader_boost
minarch.elf "$CORES_PATH/${EMU_EXE}_libretro.so" "$ROM" &> "$LOGS_PATH/$EMU_TAG.txt"
if [ -f /tmp/netplay_session ]; then
	netplay.elf --cleanup >> "$LOGS_PATH/netplay-wizard.txt" 2>&1
fi
