#!/bin/sh
SYSTEM_PATH="/mnt/SDCARD/.system/__PLATFORM__"
SETTINGS_FILE="/mnt/SDCARD/.userdata/shared/minuisettings.txt"

bt_is_on() {
    # hcitool talks to the kernel directly (no bluetoothd/D-Bus), so this
    # matches the HCI_UP check the apps use and can't hang
    hcitool dev 2>/dev/null | grep -q "hci0" && return 0
    return 1
}

bt_daemon_alive() {
    pgrep bluetoothd > /dev/null 2>&1
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
        # Currently on, turn off. Power off the adapter but keep bluetoothd
        # running (mirrors the Settings app on tg5050): killing the daemon
        # while an app is mid-bluetoothctl call wedges that caller.
        if bt_daemon_alive; then
            bluetoothctl power off > /dev/null 2>&1 &
        else
            $SYSTEM_PATH/etc/bluetooth/bt_init.sh stop > /dev/null 2>&1 &
        fi
        update_config 0
        echo 0 > /tmp/trimui_osd/toggle_bt/status
    else
        # Currently off, turn on
        if bt_daemon_alive; then
            bluetoothctl power on > /dev/null 2>&1 &
        else
            $SYSTEM_PATH/etc/bluetooth/bt_init.sh start > /dev/null 2>&1 &
        fi
        update_config 1
        echo 1 > /tmp/trimui_osd/toggle_bt/status
    fi
fi
