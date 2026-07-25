#!/bin/sh
temper=`cat /sys/class/power_supply/axp2202-battery/temp`
temperc=$((temper / 10))
temperc2=$((temper % 10))
str=$temperc.$temperc2
mkdir -p /tmp/trimui_osd/toggle_temperature
echo -n $str > /tmp/trimui_osd/toggle_temperature/temperature
