#!/bin/sh

cd $(dirname "$0")

# Low fixed frequency for simple UI
echo 600000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq 2>/dev/null

./settings.elf &> "$LOGS_PATH/settings.txt"
