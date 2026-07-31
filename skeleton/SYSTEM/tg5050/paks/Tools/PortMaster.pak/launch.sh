#!/bin/sh

cd $(dirname "$0")

# Idle big core for launcher UI
echo 408000 > /sys/devices/system/cpu/cpu4/cpufreq/scaling_max_freq 2>/dev/null

./portmaster.elf &> "$LOGS_PATH/portmaster.txt"
