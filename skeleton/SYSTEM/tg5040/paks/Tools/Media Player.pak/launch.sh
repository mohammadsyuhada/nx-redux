#!/bin/sh
cd "$(dirname "$0")"

# Video decoding needs dynamic scaling
echo schedutil > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null
echo 408000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq 2>/dev/null
echo 2000000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq 2>/dev/null

export SDL_NOMOUSE=1
export HOME=/mnt/SDCARD
mkdir -p /mnt/SDCARD/Videos

./mediaplayer.elf &> "$LOGS_PATH/media-player.txt"
