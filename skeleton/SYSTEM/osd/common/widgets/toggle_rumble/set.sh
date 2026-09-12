#!/bin/sh
# Master motor switch. The state lives in libmsettings shared memory (persisted
# to msettings.bin) via osdctl, so every app's vibration thread and the
# shutdown tap read the same flag.
export LD_LIBRARY_PATH="/mnt/SDCARD/.system/lib:/usr/trimui/lib:$LD_LIBRARY_PATH"
OSDCTL="/mnt/SDCARD/.system/bin/osdctl"
# The rumble motor hangs off a different GPIO per platform (gpio227 on tg5040).
# tg5050 has no rumble GPIO, so device/smartpros/ ships its own copy of this
# widget that buzzes through the PWM motor level instead.
RUMBLE_GPIO="/sys/class/gpio/gpio227/value"

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
        echo 1 > $RUMBLE_GPIO 2>/dev/null
        sleep 0.1
        echo 0 > $RUMBLE_GPIO 2>/dev/null
        echo 1 > /tmp/trimui_osd/toggle_rumble/status
    fi
fi
