value=`shmvar btswitch`
mkdir -p /tmp/trimui_osd/toggle_bt/
if [ $value -eq 1 ] ; then
    set=0
    if [ -f /tmp/system/bluetooth_turn_off ] ; then
        echo 0 > /tmp/trimui_osd/toggle_bt/status
        set=1
    fi
    if [ -f /tmp/system/bluetooth_turn_on ] ; then
        echo 1 > /tmp/trimui_osd/toggle_bt/status
        set=1
    fi
    if [ $set -eq 0 ] ; then
        echo $value > /tmp/trimui_osd/toggle_bt/status
    fi
else
    set=0
    if [ -f /tmp/system/bluetooth_turn_off ] ; then
        echo 0 > /tmp/trimui_osd/toggle_bt/status
        set=1
    fi
    if [ -f /tmp/system/bluetooth_turn_on ] ; then
        echo 1 > /tmp/trimui_osd/toggle_bt/status
        set=1
    fi
    if [ $set -eq 0 ] ; then
        echo $value > /tmp/trimui_osd/toggle_bt/status
    fi
fi