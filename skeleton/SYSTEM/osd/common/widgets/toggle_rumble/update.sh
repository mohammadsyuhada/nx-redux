#!/bin/sh
RUMBLE_STATE="/tmp/trimui_osd/toggle_rumble/enabled"
mkdir -p /tmp/trimui_osd/toggle_rumble/
if [ ! -f "$RUMBLE_STATE" ]; then
    echo 1 > "$RUMBLE_STATE"
fi
value=$(cat "$RUMBLE_STATE" 2>/dev/null)
[ -z "$value" ] && value=1
echo $value > /tmp/trimui_osd/toggle_rumble/status
