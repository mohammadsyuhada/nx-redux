if [ $# -eq 0 ] ; then
    value=`shmvar wifiswitch`
    mkdir -p /tmp/trimui_osd/toggle_wifi/
    echo $value > /tmp/trimui_osd/toggle_wifi/status
else
    value=`shmvar wifiswitch`
    if [ $value -eq 1 ] ; then
        if ! [ -f /tmp/system/wifi_turn_off ] ; then
            touch /tmp/system/wifi_turn_off            
        fi
        mkdir -p /tmp/trimui_osd/toggle_wifi/
        echo 0 > /tmp/trimui_osd/toggle_wifi/status
    elif [ $value -eq 0 ] ; then
        if ! [ -f /tmp/system/wifi_turn_on ] ; then
            touch /tmp/system/wifi_turn_on            
        fi
        mkdir -p /tmp/trimui_osd/toggle_wifi/
        echo 1 > /tmp/trimui_osd/toggle_wifi/status
    fi
fi