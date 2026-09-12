#!/bin/sh
# Speaker mute lives in libmsettings shared memory (osdctl "mute"). A volume
# key press clears it outside the OSD, so refresh the icon from the flag
# instead of trusting the last toggle.
export LD_LIBRARY_PATH="/mnt/SDCARD/.system/lib:/usr/trimui/lib:$LD_LIBRARY_PATH"
OSDCTL="/mnt/SDCARD/.system/bin/osdctl"
mkdir -p /tmp/trimui_osd/toggle_mute/
value=$($OSDCTL get mute)
[ -z "$value" ] && value=0
echo $value > /tmp/trimui_osd/toggle_mute/status
