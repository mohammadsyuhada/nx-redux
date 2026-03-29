#!/bin/sh
LED_MAIN="/sys/class/led_anim/max_scale"
mkdir -p /tmp/trimui_osd/toggle_led/
value=$(cat $LED_MAIN 2>/dev/null)
if [ "$value" != "0" ] && [ -n "$value" ]; then
    echo 1 > /tmp/trimui_osd/toggle_led/status
else
    echo 0 > /tmp/trimui_osd/toggle_led/status
fi
