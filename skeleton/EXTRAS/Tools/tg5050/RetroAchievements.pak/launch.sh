#!/bin/sh

cd "$(dirname "$0")"

# Fixed frequency for network I/O
echo 1008000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq 2>/dev/null

./ratools.elf &> "$LOGS_PATH/ratools.txt"
