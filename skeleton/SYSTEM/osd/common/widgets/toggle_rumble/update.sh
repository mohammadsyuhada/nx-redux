#!/bin/sh
export LD_LIBRARY_PATH="/mnt/SDCARD/.system/lib:/usr/trimui/lib:$LD_LIBRARY_PATH"
OSDCTL="/mnt/SDCARD/.system/bin/osdctl"
mkdir -p /tmp/trimui_osd/toggle_rumble/
value=$($OSDCTL get rumble)
[ -z "$value" ] && value=1
echo $value > /tmp/trimui_osd/toggle_rumble/status
