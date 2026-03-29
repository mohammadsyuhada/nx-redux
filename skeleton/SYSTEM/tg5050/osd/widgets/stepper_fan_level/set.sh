FAN_MAX_LEVEL=6
FAN_MIN_LEVEL=-1
COOLING_LEVELS=(0 20 22 24 26 28 30)
STOP_LEVEL=0

function set_fan_level() {  
    echo "set_fan_level, $1!"
    echo "$value" > /tmp/trimui_osd/stepper_fanlevel/status
    echo $value > /tmp/system/set_fanlevel
    if [ $1 -lt 0 ] ; then
        echo "set cooling level auto, do nothing."
    elif [ $1 -eq 0 ] ; then
        echo "set cooling level stop"
        echo -n "$STOP_LEVEL" > /tmp/trimui_osd/stepper_fanlevel/cooling_cur_state
    elif [ $1 -le $FAN_MAX_LEVEL ] ; then
        echo "set cooling level ${COOLING_LEVELS[$1]}"
        echo -n "${COOLING_LEVELS[$1]}" > /tmp/trimui_osd/stepper_fanlevel/cooling_cur_state
    fi
}

if [ $# -eq 0 ] ; then
    value=`shmvar fanlevel`
    mkdir -p /tmp/trimui_osd/stepper_fanlevel/
    echo "$value" > /tmp/trimui_osd/stepper_fanlevel/status
else
    if [ $1 -eq 0 ] ; then
        value=`shmvar fanlevel`
        value=$((value-1))
        if [ $value -lt $FAN_MIN_LEVEL ] ; then
            value=$FAN_MAX_LEVEL
        fi
        set_fan_level $value
    elif [ $1 -eq 1 ] ; then
        value=`shmvar fanlevel`
        value=$((value+1))
        if [ $value -gt $FAN_MAX_LEVEL ] ; then
            value=$FAN_MIN_LEVEL
        fi
        set_fan_level $value
    fi
fi
