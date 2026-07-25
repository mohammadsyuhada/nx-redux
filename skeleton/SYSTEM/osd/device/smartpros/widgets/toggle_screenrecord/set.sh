#!/bin/sh
# Screen recording toggle (tg5050). screenrecorder.elf writes its own PID to
# /tmp/screenrecorder.pid; the foreground app's capture system (capture_check
# in generic_video.c) sees that file within ~1s and starts publishing RGBA
# frames to the /tmp/fb_mirror.raw shm that the recorder pipes into ffmpeg.
PID_FILE=/tmp/screenrecorder.pid
RECORDER=/mnt/SDCARD/.system/__PLATFORM__/bin/screenrecorder.elf
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
    # SIGTERM lets the recorder flush the encoder; wait for it and take cpu2
    # back offline in the background so the OSD isn't blocked meanwhile
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
        echo 0 > $CPU2
    ) &
    echo 0 > $STATUS_DIR/status
else
    # Bring cpu2 online as a dedicated encoding core
    echo 1 > $CPU2
    mkdir -p $OUTPUT_DIR
    output=$OUTPUT_DIR/REC_$(date +%Y%m%d_%H%M%S).avi
    # plain & (no setsid — tg5040's busybox lacks it; kept consistent here); survives fine under trimui_osdd
    "$RECORDER" "$output" 1280 720 >/dev/null 2>&1 &
    echo 1 > $STATUS_DIR/status
    # close the OSD so it doesn't sit in the recording
    touch /tmp/hide_osdd
fi
