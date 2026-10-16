#!/bin/sh

cd "$(dirname "$0")" || exit

# Simple UI: same 1008 MHz schedutil cap as the launcher menu (see PLAT_setCPUSpeed)
echo 1008000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq 2>/dev/null

./extras.elf > "$LOGS_PATH/Xtras.txt" 2>&1
