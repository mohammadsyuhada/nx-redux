#!/bin/sh
mkdir -p /tmp/trimui_osd/toggle_screenshot/
pid=$(cat /tmp/screenshot.pid 2>/dev/null)
if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
    echo 1 > /tmp/trimui_osd/toggle_screenshot/status
else
    echo 0 > /tmp/trimui_osd/toggle_screenshot/status
fi
