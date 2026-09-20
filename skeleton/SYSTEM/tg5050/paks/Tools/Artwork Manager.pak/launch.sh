#!/bin/sh

cd "$(dirname "$0")"

# Big core offline: this app runs on the little cores. MinUI.pak/launch.sh
# hands every pak over with cpu4 online (emulators need it); a 408 MHz big
# core adds nothing here, an offline one is power-gated. Little cores capped below for network I/O + compositing.
echo 0 > /sys/devices/system/cpu/cpu4/online 2>/dev/null
echo 1008000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq 2>/dev/null

./scraper.elf &> "$LOGS_PATH/scraper.txt"
