#!/bin/sh
cd "$(dirname "$0")"

# Idle big core, cap little cores for audio decoding
echo 408000 > /sys/devices/system/cpu/cpu4/cpufreq/scaling_max_freq 2>/dev/null
echo 1032000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq 2>/dev/null

mkdir -p /mnt/SDCARD/Music
./musicplayer.elf &> "$LOGS_PATH/music-player.txt"
