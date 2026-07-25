#!/bin/sh

cd $(dirname "$0")

# Low fixed frequency for file browsing
echo 1008000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq 2>/dev/null

HOME="$SDCARD_PATH"
if [ "$DEVICE" = "brick" ] || [ "$DEVICE" = "brickpro" ]; then
    CFG="tg3040.cfg"
else
    CFG="tg5040.cfg"
fi
./NextCommander --config $CFG &> "$LOGS_PATH/files.txt"