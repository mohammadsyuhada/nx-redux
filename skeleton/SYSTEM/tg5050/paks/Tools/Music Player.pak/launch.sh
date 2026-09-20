#!/bin/sh
cd "$(dirname "$0")"

# Big core offline: this app runs on the little cores. MinUI.pak/launch.sh
# hands every pak over with cpu4 online (emulators need it); a 408 MHz big
# core adds nothing here, an offline one is power-gated. musicplayerd already decodes on the little cores while the launcher has cpu4 offline; capped below.
echo 0 > /sys/devices/system/cpu/cpu4/online 2>/dev/null
echo 1032000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq 2>/dev/null

mkdir -p /mnt/SDCARD/Music
./musicplayer.elf &> "$LOGS_PATH/music-player.txt"
