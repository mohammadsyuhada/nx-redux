if [ $# -eq 0 ] ; then
    value=`shmvar ledswitch`
    mkdir -p /tmp/trimui_osd/toggle_led/
    echo $value > /tmp/trimui_osd/toggle_led/status
else
    value=`shmvar ledswitch`
    if [ $value -eq 1 ] ; then
        if ! [ -f /tmp/system/led_turn_off ] ; then
            touch /tmp/system/led_turn_off
        fi
        mkdir -p /tmp/trimui_osd/toggle_led/
        echo 0 > /tmp/trimui_osd/toggle_led/status
    elif [ $value -eq 0 ] ; then
        if ! [ -f /tmp/system/led_turn_on ] ; then
            touch /tmp/system/led_turn_on            
        fi
        mkdir -p /tmp/trimui_osd/toggle_led/
        echo 1 > /tmp/trimui_osd/toggle_led/status
    fi
fi