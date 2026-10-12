#!/bin/sh

EMU_EXE=mednafen_vb
CORES_PATH=$(dirname "$0")

###############################

EMU_TAG=$(basename "$(dirname "$0")" .pak)
ROM="$1"
HOME="$USERDATA_PATH"
cd "$HOME"
# With a shader on, the GPU runs at performance (gpu-shader-boost.sh).
. "$SHARED_SYSTEM_PATH/bin/gpu-shader-boost.sh"
nx_gpu_shader_boost
minarch.elf "$CORES_PATH/${EMU_EXE}_libretro.so" "$ROM" &> "$LOGS_PATH/$EMU_TAG.txt"
