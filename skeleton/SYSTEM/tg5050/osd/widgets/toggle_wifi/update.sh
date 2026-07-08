#!/bin/sh
mkdir -p /tmp/trimui_osd/toggle_wifi/
if ip link show wlan0 2>/dev/null | grep -q "UP"; then
    echo 1 > /tmp/trimui_osd/toggle_wifi/status
else
    echo 0 > /tmp/trimui_osd/toggle_wifi/status
fi
