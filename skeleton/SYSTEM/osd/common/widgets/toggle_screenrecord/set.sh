#!/bin/sh
# Screen recording toggle (all platforms). screenrecorder.elf picks its own
# capture source — DRM scanout on tg5050, fbdev on tg5040, GPU mirror as a
# last resort — so it records ANY app, follows the display across app
# switches, and writes wallclock-stamped VFR mp4 (correct speed at whatever
# frame rate the device sustains). It writes its own PID to
# /tmp/screenrecorder.pid; frames are captured at the panel's native size
# (the width/height args are only a fallback for the mirror source).
PID_FILE=/tmp/screenrecorder.pid
RECORDER=/mnt/SDCARD/.system/bin/screenrecorder.elf
OUTPUT_DIR=/mnt/SDCARD/Videos/Recordings
STATUS_DIR=/tmp/trimui_osd/toggle_screenrecord
CPU2=/sys/devices/system/cpu/cpu2/online

recording() {
    pid=$(cat $PID_FILE 2>/dev/null)
    [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null
}

mkdir -p $STATUS_DIR

if [ $# -eq 0 ]; then
    if recording; then
        echo 1 > $STATUS_DIR/status
    else
        echo 0 > $STATUS_DIR/status
    fi
elif recording; then
    pid=$(cat $PID_FILE)
    # SIGTERM lets the recorder flush the encoder; wait for it and undo the
    # cpu2 boost in the background so the OSD isn't blocked meanwhile
    kill "$pid" 2>/dev/null
    (
        i=0
        while [ $i -lt 20 ] && kill -0 "$pid" 2>/dev/null; do
            usleep 500000
            i=$((i + 1))
        done
        if kill -0 "$pid" 2>/dev/null; then
            kill -9 "$pid" 2>/dev/null
            rm -f $PID_FILE
        fi
        # only take cpu2 back offline if the start path brought it up
        # (tg5050 parks it when idle; tg5040 keeps all cores online)
        if [ -f $STATUS_DIR/cpu2_onlined ]; then
            echo 0 > $CPU2 2>/dev/null
            rm -f $STATUS_DIR/cpu2_onlined
        fi
    ) &
    echo 0 > $STATUS_DIR/status
else
    # Give the encoder a dedicated core where one is parked (tg5050)
    if [ "$(cat $CPU2 2>/dev/null)" = "0" ]; then
        echo 1 > $CPU2 2>/dev/null
        touch $STATUS_DIR/cpu2_onlined
    fi
    mkdir -p $OUTPUT_DIR
    # mp4: the recorder writes variable-frame-rate (wallclock-stamped) video
    output=$OUTPUT_DIR/REC_$(date +%Y%m%d_%H%M%S).mp4
    # plain & (no setsid — tg5040's busybox lacks it); survives fine under trimui_osdd
    "$RECORDER" "$output" 1280 720 >/dev/null 2>&1 &
    echo 1 > $STATUS_DIR/status
    # close the OSD so it doesn't sit in the recording
    touch /tmp/hide_osdd
fi
