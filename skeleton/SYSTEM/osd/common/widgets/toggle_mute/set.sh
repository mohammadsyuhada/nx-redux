#!/bin/sh
export LD_LIBRARY_PATH="/mnt/SDCARD/.system/tg5040/lib:/usr/trimui/lib:$LD_LIBRARY_PATH"
OSDCTL="/mnt/SDCARD/.system/tg5040/bin/osdctl"

mkdir -p /tmp/trimui_osd/toggle_mute/

if [ $# -eq 0 ] ; then
    value=$($OSDCTL get mute)
    echo $value > /tmp/trimui_osd/toggle_mute/status
else
    value=$($OSDCTL get mute)
    if [ "$value" -eq 1 ] ; then
        $OSDCTL set mute 0
        echo 0 > /tmp/trimui_osd/toggle_mute/status
    else
        $OSDCTL set mute 1
        echo 1 > /tmp/trimui_osd/toggle_mute/status
    fi
fi
