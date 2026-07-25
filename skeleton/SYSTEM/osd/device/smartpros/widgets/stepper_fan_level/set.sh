#!/bin/sh
export LD_LIBRARY_PATH="/mnt/SDCARD/.system/__PLATFORM__/lib:/usr/trimui/lib:$LD_LIBRARY_PATH"
OSDCTL="/mnt/SDCARD/.system/__PLATFORM__/bin/osdctl"

FAN_MAX_LEVEL=6
FAN_MIN_LEVEL=-1

# Map stepper level to NextUI fan speed value
# -1=auto(normal), 0=off, 1-6=manual percentages
fan_level_to_speed() {
    case $1 in
        -1) echo "-2" ;;   # auto normal curve
        0)  echo "0" ;;    # fan off
        1)  echo "17" ;;
        2)  echo "33" ;;
        3)  echo "50" ;;
        4)  echo "67" ;;
        5)  echo "83" ;;
        6)  echo "100" ;;
        *)  echo "-2" ;;
    esac
}

# Map NextUI fan speed back to stepper level for display
speed_to_fan_level() {
    case $1 in
        -2|-1) echo "-1" ;;
        0)     echo "0" ;;
        *)
            if [ $1 -le 17 ]; then echo "1"
            elif [ $1 -le 33 ]; then echo "2"
            elif [ $1 -le 50 ]; then echo "3"
            elif [ $1 -le 67 ]; then echo "4"
            elif [ $1 -le 83 ]; then echo "5"
            else echo "6"
            fi
            ;;
    esac
}

if [ $# -eq 0 ] ; then
    speed=$($OSDCTL get fanspeed)
    value=$(speed_to_fan_level $speed)
    mkdir -p /tmp/trimui_osd/stepper_fanlevel/
    echo "$value" > /tmp/trimui_osd/stepper_fanlevel/status
else
    speed=$($OSDCTL get fanspeed)
    value=$(speed_to_fan_level $speed)
    if [ $1 -eq 0 ] ; then
        value=$((value-1))
        if [ $value -lt $FAN_MIN_LEVEL ] ; then
            value=$FAN_MAX_LEVEL
        fi
    elif [ $1 -eq 1 ] ; then
        value=$((value+1))
        if [ $value -gt $FAN_MAX_LEVEL ] ; then
            value=$FAN_MIN_LEVEL
        fi
    fi
    new_speed=$(fan_level_to_speed $value)
    $OSDCTL set fanspeed $new_speed
    mkdir -p /tmp/trimui_osd/stepper_fanlevel/
    echo "$value" > /tmp/trimui_osd/stepper_fanlevel/status
fi
