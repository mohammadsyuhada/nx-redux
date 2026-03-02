#!/bin/sh

cd "$(dirname "$0")"

# Idle big core, cap little cores for network I/O + compositing
echo 408000 > /sys/devices/system/cpu/cpu4/cpufreq/scaling_max_freq 2>/dev/null
echo 1008000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq 2>/dev/null

./scraper.elf &> "$LOGS_PATH/scraper.txt"
