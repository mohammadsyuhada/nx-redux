#!/bin/sh
export LD_LIBRARY_PATH="/mnt/SDCARD/.system/__PLATFORM__/lib:/usr/trimui/lib:$LD_LIBRARY_PATH"
OSDCTL="/mnt/SDCARD/.system/__PLATFORM__/bin/osdctl"

if [ $# -eq 0 ] ; then
    value=$($OSDCTL get brightness)
    mkdir -p /tmp/trimui_osd/slider_backlight/
    echo "$value/10" > /tmp/trimui_osd/slider_backlight/status
else
    value=$($OSDCTL get brightness)
    if [ $1 -eq 0 ] ; then
        value=$((value-1))
        if [ $value -lt 0 ] ; then
            value=0
        fi
    elif [ $1 -eq 1 ] ; then
        value=$((value+1))
        if [ $value -gt 10 ] ; then
            value=10
        fi
    fi
    $OSDCTL set brightness $value
    mkdir -p /tmp/trimui_osd/slider_backlight/
    echo "$value/10" > /tmp/trimui_osd/slider_backlight/status
fi
