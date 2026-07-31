#!/bin/sh

cd "$(dirname "$0")"

# Moderate CPU for network I/O + image compositing
echo 1008000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq 2>/dev/null

./scraper.elf &> "$LOGS_PATH/scraper.txt"
