#!/bin/sh
# SPIKE: run a ROM under the spike minarch build. Invoke via /tmp/next:
#   '/mnt/SDCARD/.spike/launch.sh' '<core .so>' '<rom path>'
SPIKE=/mnt/SDCARD/.spike
CORE="$1"
ROM="$2"
# Same CPU setup as DC.pak's launch.sh, so the comparison is fair.
echo 1 >/sys/devices/system/cpu/cpu1/online 2>/dev/null
echo 1 >/sys/devices/system/cpu/cpu2/online 2>/dev/null
echo 1 >/sys/devices/system/cpu/cpu3/online 2>/dev/null
echo performance >/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
echo 2000000 >/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq
echo 1608000 >/sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq
cd "$USERDATA_PATH"
export NX_HWR_STATS=1 # [HWR] 5 s stats line (measurements)
"$SPIKE/minarch-spike.elf" "$CORE" "$ROM" >"$SPIKE/last-run.log" 2>&1
