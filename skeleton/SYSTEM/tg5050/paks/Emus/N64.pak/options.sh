#!/bin/sh
# NAME: Nintendo 64 (Mupen64Plus)
# Pre-launch options editor entry point. Its existence is also the capability
# marker nxredux probes for the "Emulator Options" context-menu entry (the
# same pattern as the `netplay` marker file).
# Usage: options.sh [rom-path]   (no arg: edit the device-global defaults)

PAK_DIR="$(dirname "$0")"
cd "$PAK_DIR"

# Config-dir resolution + mupen64plus.cfg seeding shared with launch.sh
. "$PAK_DIR/nx_paths.sh"

JSON="$SDCARD_PATH/Emus/shared/mupen64plus/overlay_settings.json"

if [ -n "$1" ]; then
    ROM="$1"
    NX_ROM_BASE="$(nx_rom_base "$ROM")"
    mkdir -p "$DEVICE_CONFIG_DIR/games"
    options.elf --json "$JSON" --ini "$EMU_CFG" \
        --override "$DEVICE_CONFIG_DIR/games/$NX_ROM_BASE.cfg" \
        --game "$NX_ROM_BASE" 2> "$LOGS_PATH/emu-options.txt"
else
    options.elf --json "$JSON" --ini "$EMU_CFG" \
        --game "Nintendo 64 (Mupen64Plus)" 2> "$LOGS_PATH/emu-options.txt"
fi
