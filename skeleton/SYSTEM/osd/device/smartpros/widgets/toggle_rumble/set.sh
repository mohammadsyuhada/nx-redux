#!/bin/sh
# Master motor switch. The state lives in libmsettings shared memory (persisted
# to msettings.bin) via osdctl, so every app's vibration thread and the
# shutdown tap read the same flag.
export LD_LIBRARY_PATH="/mnt/SDCARD/.system/lib:/usr/trimui/lib:$LD_LIBRARY_PATH"
OSDCTL="/mnt/SDCARD/.system/bin/osdctl"
# tg5050 override of the common toggle_rumble widget: the Smart Pro S has no
# rumble GPIO, its motor is the PWM vibrator behind /sys/class/motor/level,
# 0-65535 (PLAT_setRumble drives the same file). Everything else matches
# common/ — keep the two in sync when editing.
RUMBLE_LEVEL="/sys/class/motor/level"
RUMBLE_ON=65535

mkdir -p /tmp/trimui_osd/toggle_rumble/

if [ $# -eq 0 ] ; then
    value=$($OSDCTL get rumble)
    echo $value > /tmp/trimui_osd/toggle_rumble/status
else
    value=$($OSDCTL get rumble)
    if [ "$value" -eq 1 ] ; then
        $OSDCTL set rumble 0
        echo 0 > /tmp/trimui_osd/toggle_rumble/status
    else
        $OSDCTL set rumble 1
        # brief buzz so the user feels the motor come back
        echo $RUMBLE_ON > $RUMBLE_LEVEL 2>/dev/null
        sleep 0.15
        echo 0 > $RUMBLE_LEVEL 2>/dev/null
        echo 1 > /tmp/trimui_osd/toggle_rumble/status
    fi
fi
