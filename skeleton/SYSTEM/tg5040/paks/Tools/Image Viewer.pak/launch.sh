#!/bin/sh
cd "$(dirname "$0")"

export SDL_NOMOUSE=1
export HOME=/mnt/SDCARD
mkdir -p /mnt/SDCARD/Images

./imageviewer.elf &> "$LOGS_PATH/image-viewer.txt"
