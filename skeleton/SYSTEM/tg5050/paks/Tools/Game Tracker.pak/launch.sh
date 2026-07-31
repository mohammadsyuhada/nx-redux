#!/bin/sh

cd "$(dirname "$0")"

# Idle big core at minimum, little cores auto-scale via schedutil
echo 408000 > /sys/devices/system/cpu/cpu4/cpufreq/scaling_max_freq 2>/dev/null

./gametime.elf &> "$LOGS_PATH/gametime.txt"
