#!/bin/sh
SYSTEM_PATH="/mnt/SDCARD/.system"
SETTINGS_FILE="/mnt/SDCARD/.userdata/shared/minuisettings.txt"

wifi_is_on() {
    # Check if wlan0 interface exists and is up
    ip link show wlan0 2>/dev/null | grep -q "UP" && return 0
    return 1
}

update_config() {
    # Update wifi= line in minuisettings.txt for reboot persistence
    if [ -f "$SETTINGS_FILE" ]; then
        sed -i "s/^wifi=.*/wifi=$1/" "$SETTINGS_FILE"
    fi
}

mkdir -p /tmp/trimui_osd/toggle_wifi/

if [ $# -eq 0 ] ; then
    if wifi_is_on; then
        echo 1 > /tmp/trimui_osd/toggle_wifi/status
    else
        echo 0 > /tmp/trimui_osd/toggle_wifi/status
    fi
else
    if wifi_is_on; then
        # Currently on, turn off
        $SYSTEM_PATH/etc/wifi/wifi_init.sh stop > /dev/null 2>&1 &
        update_config 0
        echo 0 > /tmp/trimui_osd/toggle_wifi/status
    else
        # Currently off, turn on
        $SYSTEM_PATH/etc/wifi/wifi_init.sh start > /dev/null 2>&1 &
        update_config 1
        echo 1 > /tmp/trimui_osd/toggle_wifi/status
    fi
fi
