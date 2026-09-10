#!/bin/sh
RUMBLE_STATE="/tmp/trimui_osd/toggle_rumble/enabled"
# tg5050 override of the common toggle_rumble widget: the Smart Pro S has no
# rumble GPIO (gpio236 is never exported; the pin the old script wrote does not
# exist), its motor is the PWM vibrator behind /sys/class/motor/level, 0-65535
# (launch.sh parks it at 0 at boot, PLAT_setRumble drives the same file).
# Everything else matches common/ — keep the two in sync when editing.
RUMBLE_LEVEL="/sys/class/motor/level"
RUMBLE_ON=65535

mkdir -p /tmp/trimui_osd/toggle_rumble/

# Initialize state file from current setting if it doesn't exist
if [ ! -f "$RUMBLE_STATE" ]; then
    echo 1 > "$RUMBLE_STATE"
fi

if [ $# -eq 0 ] ; then
    value=$(cat "$RUMBLE_STATE" 2>/dev/null)
    [ -z "$value" ] && value=1
    echo $value > /tmp/trimui_osd/toggle_rumble/status
else
    value=$(cat "$RUMBLE_STATE" 2>/dev/null)
    [ -z "$value" ] && value=1
    if [ "$value" -eq 1 ] ; then
        # Currently on, turn off
        echo 0 > "$RUMBLE_STATE"
        echo 0 > /tmp/trimui_osd/toggle_rumble/status
    else
        # Currently off, turn on — give brief haptic feedback
        echo 1 > "$RUMBLE_STATE"
        echo $RUMBLE_ON > $RUMBLE_LEVEL 2>/dev/null
        sleep 0.15
        echo 0 > $RUMBLE_LEVEL 2>/dev/null
        echo 1 > /tmp/trimui_osd/toggle_rumble/status
    fi
fi
