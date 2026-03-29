if [ $# -eq 0 ] ; then
    value=`shmvar btswitch`
    mkdir -p /tmp/trimui_osd/toggle_bt/
    echo $value > /tmp/trimui_osd/toggle_bt/status
else
    value=`shmvar btswitch`
    if [ $value -eq 1 ] ; then
        #switch开，调用本脚本(set.sh)关闭
        if ! [ -f /tmp/system/bluetooth_turn_off ] ; then
            #关闭
            touch /tmp/system/bluetooth_turn_off
            mkdir -p /tmp/trimui_osd/toggle_bt/
            echo 0 > /tmp/trimui_osd/toggle_bt/status            
        else
            #关闭中，再次打开
            touch /tmp/system/bluetooth_turn_on
            mkdir -p /tmp/trimui_osd/toggle_bt/
            echo 1 > /tmp/trimui_osd/toggle_bt/status
        fi

    elif [ $value -eq 0 ] ; then
        #switch关，调用本脚本(set.sh)打开
        if ! [ -f /tmp/system/bluetooth_turn_on ] ; then
            #打开
            touch /tmp/system/bluetooth_turn_on
            mkdir -p /tmp/trimui_osd/toggle_bt/
            echo 1 > /tmp/trimui_osd/toggle_bt/status            
        else
            #打开中，再次关闭
            touch /tmp/system/bluetooth_turn_off
            mkdir -p /tmp/trimui_osd/toggle_bt/
            echo 0 > /tmp/trimui_osd/toggle_bt/status
        fi
        
    fi
fi
