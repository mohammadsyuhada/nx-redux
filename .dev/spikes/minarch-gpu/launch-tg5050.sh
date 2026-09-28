#!/bin/sh
# SPIKE (tg5050): run a ROM under the spike minarch build. Invoke via /tmp/next:
#   '/mnt/SDCARD/.spike/launch-tg5050.sh' '<core .so>' '<rom path>'
SPIKE=/mnt/SDCARD/.spike
CORE="$1"
ROM="$2"
# Same CPU/GPU setup as tg5050 DC.pak's launch.sh, so the comparison is fair:
# big cluster cpu4-5 at 1992-2160 MHz, GPU devfreq on performance.
echo 1 >/sys/devices/system/cpu/cpu5/online 2>/dev/null
echo performance >/sys/devices/system/cpu/cpu4/cpufreq/scaling_governor
echo 2160000 >/sys/devices/system/cpu/cpu4/cpufreq/scaling_max_freq
echo 1992000 >/sys/devices/system/cpu/cpu4/cpufreq/scaling_min_freq
echo performance >/sys/devices/platform/soc@3000000/1800000.gpu/devfreq/1800000.gpu/governor 2>/dev/null
cd "$USERDATA_PATH"
# DC.pak pins flycast's emu thread to cpu4 and its main/render thread to cpu5;
# keep the whole minarch process (main + flycast's emu thread) on cpu4-5.
taskset 0x30 "$SPIKE/minarch-spike.elf" "$CORE" "$ROM" >"$SPIKE/last-run.log" 2>&1
