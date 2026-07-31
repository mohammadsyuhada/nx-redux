#!/bin/sh

cd "$(dirname "$0")"

# Idle big core, cap little cores for network I/O
echo 408000 > /sys/devices/system/cpu/cpu4/cpufreq/scaling_max_freq 2>/dev/null
echo 1032000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq 2>/dev/null

./sync.elf &> "$LOGS_PATH/sync.txt"
