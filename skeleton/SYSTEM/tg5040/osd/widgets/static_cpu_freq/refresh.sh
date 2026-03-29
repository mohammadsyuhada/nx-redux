#!/bin/sh
value=`cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq`
freq=$((value / 1000))
mkdir -p /tmp/trimui_osd/toggle_cpu
echo -n $freq > /tmp/trimui_osd/toggle_cpu/curfreq
