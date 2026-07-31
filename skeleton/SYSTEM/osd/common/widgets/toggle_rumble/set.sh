#!/bin/sh
RUMBLE_STATE="/tmp/trimui_osd/toggle_rumble/enabled"
# The rumble motor hangs off a different GPIO per platform (gpio227 on tg5040).
# tg5050 wires it to gpio236, so device/smartpros/ ships its own copy of this
# widget with that value — everything else here is platform-independent.
RUMBLE_GPIO="/sys/class/gpio/gpio227/value"

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
        echo 1 > $RUMBLE_GPIO 2>/dev/null
        sleep 0.1
        echo 0 > $RUMBLE_GPIO 2>/dev/null
        echo 1 > /tmp/trimui_osd/toggle_rumble/status
    fi
fi
