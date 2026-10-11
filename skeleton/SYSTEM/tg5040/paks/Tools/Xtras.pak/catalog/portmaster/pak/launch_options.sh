#!/bin/sh
# NAME: Ports
# Launch Options: per-game CPU cores / CPU mode / GPU mode for ports. Its
# existence is the marker nxredux probes for the "Launch Options"
# context-menu entry; the Emulator Settings picker lists it too.
# Usage: launch_options.sh [rom-path]   (no arg: edit the console-wide default)
# ports_launch.sh applies the saved values (nx_launch_opts.sh). On "Launch"
# (options.elf exit 3) the ROM path goes to /tmp/nextui_launch and nextui
# starts the game when it comes back up.
PAK_DIR="$(dirname "$0")"
TAG="$(basename "$PAK_DIR" .pak)"
DIR="$USERDATA_PATH/$TAG-launch"
GLOBAL="$DIR/launch.ini"
mkdir -p "$DIR"
# options.elf's INI writer edits an existing file in place, so seed it.
[ -f "$GLOBAL" ] || printf '[launch]\ncores = auto\ncpu = auto\ngpu = auto\n' > "$GLOBAL"
if [ -n "${1:-}" ]; then
    ROM="$1"
    NAME="${ROM##*/}"
    options.elf --launch --json "$PAK_DIR/launch_options.json" --ini "$GLOBAL" \
        --override "$DIR/$NAME.ini" --game "${NAME%.*}" 2>> "$LOGS_PATH/launch-options.txt"
    if [ $? -eq 3 ]; then
        printf '%s' "$ROM" > /tmp/nextui_launch
        sync
    fi
else
    options.elf --json "$PAK_DIR/launch_options.json" --ini "$GLOBAL" \
        --game "Ports" 2>> "$LOGS_PATH/launch-options.txt"
fi
exit 0
