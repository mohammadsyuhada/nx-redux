#!/bin/sh
mkdir -p /tmp/trimui_osd/toggle_bt/
if hcitool dev 2>/dev/null | grep -q "hci0"; then
    echo 1 > /tmp/trimui_osd/toggle_bt/status
else
    echo 0 > /tmp/trimui_osd/toggle_bt/status
fi
