#!/bin/sh
# Every model has several brightness nodes that must all be controlled
# together: Smart Pro/S and Brick have max_scale/_lr/_f1f2, Brick Pro adds
# _rear. Writing a node the device doesn't have is harmless (the file simply
# isn't there), and the settings-file probe below covers every model's name,
# so one script serves all four devices.
LED_MAIN="/sys/class/led_anim/max_scale"
LED_LR="/sys/class/led_anim/max_scale_lr"
LED_F1F2="/sys/class/led_anim/max_scale_f1f2"
LED_REAR="/sys/class/led_anim/max_scale_rear"
# Pick THIS model's settings file, don't probe for whichever exists. All three
# live in .userdata/shared, which is shared across platforms — sync.c excludes
# all three precisely because they coexist — so a card that has been in a Brick
# still carries ledsettings_brick.txt on a Smart Pro. Probing in a fixed order
# would then read the wrong model's brightness, permanently: Settings writes
# ledsettings.txt on both Smart Pro and Smart Pro S, and it would stay shadowed.
# Brick Pro is the only model with a _rear zone; Brick is the only other model
# at 1024 px wide; everything else uses the plain file.
if [ -e "$LED_REAR" ]; then
    LED_SETTINGS="/mnt/SDCARD/.userdata/shared/ledsettings_brickpro.txt"
elif [ "$(cut -d, -f1 /sys/class/graphics/fb0/virtual_size 2>/dev/null)" = "1024" ]; then
    LED_SETTINGS="/mnt/SDCARD/.userdata/shared/ledsettings_brick.txt"
else
    LED_SETTINGS="/mnt/SDCARD/.userdata/shared/ledsettings.txt"
fi

# Read configured brightness from this model's ledsettings (first zone's value)
get_configured_brightness() {
    if [ -f "$LED_SETTINGS" ]; then
        val=$(grep -m1 "^brightness=" "$LED_SETTINGS" | cut -d= -f2)
        [ -n "$val" ] && [ "$val" -gt 0 ] 2>/dev/null && echo "$val" && return
    fi
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
    echo $1 > $LED_REAR 2>/dev/null
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
