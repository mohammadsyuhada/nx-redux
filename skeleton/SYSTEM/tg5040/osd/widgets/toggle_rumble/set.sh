if [ $# -eq 0 ] ; then
    value=`shmvar rumbleswitch`
    mkdir -p /tmp/trimui_osd/toggle_rumble/
    echo $value > /tmp/trimui_osd/toggle_rumble/status
else
    value=`shmvar rumbleswitch`
    if [ $value -eq 1 ] ; then
        if ! [ -f /tmp/system/rumble_turn_off ] ; then
            touch /tmp/system/rumble_turn_off
        fi
        mkdir -p /tmp/trimui_osd/toggle_rumble/
        echo 0 > /tmp/trimui_osd/toggle_rumble/status
    elif [ $value -eq 0 ] ; then
        if ! [ -f /tmp/system/rumble_turn_on ] ; then
            touch /tmp/system/rumble_turn_on            
        fi
        mkdir -p /tmp/trimui_osd/toggle_rumble/
        echo 1 > /tmp/trimui_osd/toggle_rumble/status
    fi
fi