if [ $# -eq 0 ] ; then
    value=`shmvar ledswitch`
    mkdir -p /tmp/trimui_osd/toggle_led/
    echo $value > /tmp/trimui_osd/toggle_led/status
else
    value=`shmvar ledswitch`
    if [ $value -eq 1 ] ; then
        if ! [ -f /tmp/system/enable_led ] ; then
            echo 0 > /tmp/system/enable_led
        fi
        
        mkdir -p /tmp/trimui_osd/toggle_led/
        echo 0 > /tmp/trimui_osd/toggle_led/status
    elif [ $value -eq 0 ] ; then
        if ! [ -f /tmp/system/enable_led ] ; then
            echo 1 > /tmp/system/enable_led
        fi
        
        mkdir -p /tmp/trimui_osd/toggle_led/
        echo 1 > /tmp/trimui_osd/toggle_led/status
    fi
fi