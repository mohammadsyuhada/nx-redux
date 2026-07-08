#!/bin/sh
# TG5040 (Brick) has 3 LED zones that must all be controlled together
LED_MAIN="/sys/class/led_anim/max_scale"
LED_LR="/sys/class/led_anim/max_scale_lr"
LED_F1F2="/sys/class/led_anim/max_scale_f1f2"
LED_SETTINGS="/mnt/SDCARD/.userdata/shared/ledsettings_brick.txt"
LED_SETTINGS_ALT="/mnt/SDCARD/.userdata/shared/ledsettings.txt"

# Read configured brightness from ledsettings (first zone's value)
get_configured_brightness() {
    for f in "$LED_SETTINGS" "$LED_SETTINGS_ALT"; do
        if [ -f "$f" ]; then
            val=$(grep -m1 "^brightness=" "$f" | cut -d= -f2)
            [ -n "$val" ] && [ "$val" -gt 0 ] 2>/dev/null && echo "$val" && return
        fi
    done
    echo "50"
}

led_is_on() {
    value=$(cat $LED_MAIN 2>/dev/null)
    [ "$value" != "0" ] && [ -n "$value" ] && return 0
    return 1
}

set_all_leds() {
    echo $1 > $LED_MAIN 2>/dev/null
    echo $1 > $LED_LR 2>/dev/null
    echo $1 > $LED_F1F2 2>/dev/null
}

mkdir -p /tmp/trimui_osd/toggle_led/

if [ $# -eq 0 ] ; then
    if led_is_on; then
        echo 1 > /tmp/trimui_osd/toggle_led/status
    else
        echo 0 > /tmp/trimui_osd/toggle_led/status
    fi
else
    if led_is_on; then
        # Currently on, turn off. The flag file tells the apps' LED profile
        # engine (LEDS_setProfile, see defines.h LEDS_DISABLED_PATH) to stay
        # off — otherwise the next app launch or charging/sleep profile
        # change would reapply ledsettings and relight the LEDs.
        touch /tmp/leds_disabled
        set_all_leds 0
        echo 0 > /tmp/trimui_osd/toggle_led/status
    else
        # Currently off, turn on at configured brightness
        rm -f /tmp/leds_disabled
        brightness=$(get_configured_brightness)
        set_all_leds $brightness
        echo 1 > /tmp/trimui_osd/toggle_led/status
    fi
fi
