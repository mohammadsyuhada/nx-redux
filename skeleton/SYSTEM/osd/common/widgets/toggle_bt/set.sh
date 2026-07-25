#!/bin/sh
# This is the tg5040 behaviour. tg5050 OVERRIDES this file entirely —
# see device/smartpros/widgets/toggle_bt/set.sh, and edit both if you change
# shared logic here. The two are deliberately opposite on toggle-off: tg5040
# must fully stop the BT stack, because on its xradio combo chip a live
# bluetoothd collapses WiFi throughput from ~330 KB/s to ~2 KB/s; tg5050 must
# keep bluetoothd alive, because killing it mid-call wedges the caller.
SYSTEM_PATH="/mnt/SDCARD/.system/__PLATFORM__"
SETTINGS_FILE="/mnt/SDCARD/.userdata/shared/minuisettings.txt"

bt_is_on() {
    hcitool dev 2>/dev/null | grep -q "hci0" && return 0
    return 1
}

update_config() {
    # Update bluetooth= line in minuisettings.txt for reboot persistence
    if [ -f "$SETTINGS_FILE" ]; then
        sed -i "s/^bluetooth=.*/bluetooth=$1/" "$SETTINGS_FILE"
    fi
}

mkdir -p /tmp/trimui_osd/toggle_bt/

if [ $# -eq 0 ] ; then
    if bt_is_on; then
        echo 1 > /tmp/trimui_osd/toggle_bt/status
    else
        echo 0 > /tmp/trimui_osd/toggle_bt/status
    fi
else
    if bt_is_on; then
        # Currently on, turn off. Unlike tg5050, this must fully stop the BT
        # stack (not just power off the adapter): on the xradio combo chip a
        # live bluetoothd drops WiFi throughput from ~330 KB/s to ~2 KB/s.
        # Apps survive losing bluetoothd mid-call via their command timeouts.
        $SYSTEM_PATH/etc/bluetooth/bt_init.sh stop > /dev/null 2>&1 &
        update_config 0
        echo 0 > /tmp/trimui_osd/toggle_bt/status
    else
        # Currently off, turn on
        $SYSTEM_PATH/etc/bluetooth/bt_init.sh start > /dev/null 2>&1 &
        update_config 1
        echo 1 > /tmp/trimui_osd/toggle_bt/status
    fi
fi
