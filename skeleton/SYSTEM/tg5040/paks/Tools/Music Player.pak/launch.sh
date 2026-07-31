#!/bin/sh
cd "$(dirname "$0")"

# Dynamic scaling for audio playback sessions
echo schedutil > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null
echo 1008000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq 2>/dev/null

mkdir -p /mnt/SDCARD/Music
./musicplayer.elf &> "$LOGS_PATH/music-player.txt"
