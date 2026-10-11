#!/bin/sh

EMU_EXE=mgba
CORES_PATH=$(dirname "$0")

###############################

EMU_TAG=$(basename "$(dirname "$0")" .pak)
ROM="$1"
HOME="$USERDATA_PATH"
cd "$HOME"
. "$SHARED_SYSTEM_PATH/bin/netplay-prelaunch.sh"
# All four little cores, uncapped, with the big cluster gated (scripts/bench
# 2026-10-11, FireRed mid-game): on cpu4 alone, schedutil left 49% of frames
# late (55.7 fps); on cpu0-3 the emulator, audio and frame threads spread out,
# 0.5% late. The launcher takes cpu2/3 offline and cpu4 back up on exit. minarch's
# startup pin to cpu4-7 then fails (logged, harmless).
# A USB Cable link runs there too (mGBA Sapphire<->Ruby, 2026-10-12: walking
# 0.5% of frames late against 3.2% with the big core, a trade 1.3% against 0.7%,
# no audio breakup); minarch runs at Performance while link traffic flows.
echo 1 > /sys/devices/system/cpu/cpu2/online 2>/dev/null
echo 1 > /sys/devices/system/cpu/cpu3/online 2>/dev/null
echo 0 > /sys/devices/system/cpu/cpu4/online 2>/dev/null
# While a shader is on, the GPU runs at performance (gpu-shader-boost.sh) and
# minarch's Auto holds the CPU at its top (1416 MHz on the little cluster): a
# shader adds CPU work too (lcd-perfect: 60.8 fps, 5.4% of frames late, against
# 58.2 fps, 34.5% late on schedutil).
export NX_SHADER_CPU_MAX=1
. "$SHARED_SYSTEM_PATH/bin/gpu-shader-boost.sh"
nx_gpu_shader_boost
minarch.elf "$CORES_PATH/${EMU_EXE}_libretro.so" "$ROM" &> "$LOGS_PATH/$EMU_TAG.txt"
if [ -f /tmp/netplay_session ]; then
	netplay.elf --cleanup >> "$LOGS_PATH/netplay-wizard.txt" 2>&1
fi
