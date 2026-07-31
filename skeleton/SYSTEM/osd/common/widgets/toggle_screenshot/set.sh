#!/bin/sh
# Screenshot daemon toggle. While the daemon runs, pressing L2+R2 captures
# a screenshot to /mnt/SDCARD/Images/Screenshots (see workspace/all/screenshot).
# The daemon writes/removes /tmp/screenshot.pid itself.
PID_FILE=/tmp/screenshot.pid
DAEMON=/mnt/SDCARD/.system/bin/screenshot.elf
STATUS_DIR=/tmp/trimui_osd/toggle_screenshot

daemon_running() {
    pid=$(cat $PID_FILE 2>/dev/null)
    [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null
}

# Tell the user how to trigger a capture (toast rendered by trimui_osdd).
# size:1 draws the 400px-wide bg_msg_w2.png; x centers it on the framebuffer
# (read from fb0, so correct at any panel width).
show_hint_toast() {
    fbw=$(cut -d, -f1 /sys/class/graphics/fb0/virtual_size 2>/dev/null)
    [ -n "$fbw" ] && [ "$fbw" -gt 0 ] 2>/dev/null || fbw=1280
    printf '{
    "type":"default",
    "id":"com.trimui.osd.msg.default",
    "duration":3000,
    "size":1,
    "x":'$(( (fbw - 400) / 2 ))',
    "y":500,
    "w":300,
    "h":80,
    "message":"Press L2+R2 to capture",
    "font":"",
    "bg":"",
    "icon":"",
    "fontsize":24,
    "fontcolor":"FFFFFFFF"
}\n' > /tmp/trimui_osd/osd_toast_msg
}

mkdir -p $STATUS_DIR

if [ $# -eq 0 ]; then
    if daemon_running; then
        echo 1 > $STATUS_DIR/status
    else
        echo 0 > $STATUS_DIR/status
    fi
elif daemon_running; then
    kill "$(cat $PID_FILE)" 2>/dev/null
    echo 0 > $STATUS_DIR/status
else
    # plain & (no setsid — busybox on these devices lacks it); survives fine under trimui_osdd
    "$DAEMON" >/dev/null 2>&1 &
    echo 1 > $STATUS_DIR/status
    # close the OSD so the user can line up the shot; the toast still renders
    touch /tmp/hide_osdd
    show_hint_toast
fi
