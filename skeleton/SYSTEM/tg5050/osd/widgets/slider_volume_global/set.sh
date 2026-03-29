#!/bin/sh
export LD_LIBRARY_PATH="/mnt/SDCARD/.system/tg5050/lib:/usr/trimui/lib:$LD_LIBRARY_PATH"
OSDCTL="/mnt/SDCARD/.system/tg5050/bin/osdctl"

if [ $# -eq 0 ] ; then
    value=$($OSDCTL get volume)
    mkdir -p /tmp/trimui_osd/slider_volume/
    echo "$value/20" > /tmp/trimui_osd/slider_volume/status
else
    value=$($OSDCTL get volume)
    if [ $1 -eq 0 ] ; then
        value=$((value-1))
        if [ $value -lt 0 ] ; then
            value=0
        fi
    elif [ $1 -eq 1 ] ; then
        value=$((value+1))
        if [ $value -gt 20 ] ; then
            value=20
        fi
    fi
    $OSDCTL set volume $value
    mkdir -p /tmp/trimui_osd/slider_volume/
    echo "$value/20" > /tmp/trimui_osd/slider_volume/status
fi
