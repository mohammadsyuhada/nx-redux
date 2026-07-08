#!/bin/sh
# Screen recording toggle (tg5040). No shm capture mirror on this platform,
# so record the framebuffer directly with ffmpeg (fbdev probes the resolution
# itself, which also covers the Brick's 1024x768). ffmpeg doesn't manage a
# PID file, so this script owns /tmp/screenrecorder.pid.
PID_FILE=/tmp/screenrecorder.pid
FFMPEG=/usr/bin/ffmpeg
OUTPUT_DIR=/mnt/SDCARD/Videos/Recordings
STATUS_DIR=/tmp/trimui_osd/toggle_screenrecord

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
    # SIGINT lets ffmpeg finalize the container; wait and clean up in the
    # background so the OSD isn't blocked meanwhile
    kill -INT "$pid" 2>/dev/null
    (
        i=0
        while [ $i -lt 20 ] && kill -0 "$pid" 2>/dev/null; do
            usleep 500000
            i=$((i + 1))
        done
        kill -9 "$pid" 2>/dev/null
        rm -f $PID_FILE
    ) &
    echo 0 > $STATUS_DIR/status
else
    [ -x $FFMPEG ] || exit 0
    mkdir -p $OUTPUT_DIR
    output=$OUTPUT_DIR/REC_$(date +%Y%m%d_%H%M%S).avi
    # plain & (no setsid — the Brick's busybox lacks it); survives fine under trimui_osdd
    $FFMPEG -nostdin -f fbdev -framerate 15 -i /dev/fb0 \
        -c:v mjpeg -q:v 10 -y "$output" >/dev/null 2>&1 &
    echo $! > $PID_FILE
    echo 1 > $STATUS_DIR/status
    # close the OSD so it doesn't sit in the recording
    touch /tmp/hide_osdd
fi
