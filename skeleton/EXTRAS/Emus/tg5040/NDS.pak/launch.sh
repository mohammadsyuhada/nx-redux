#!/bin/sh
set -eo pipefail
set -x

rm -f "$LOGS_PATH/NDS.txt"
exec >>"$LOGS_PATH/NDS.txt"
exec 2>&1

echo "$0" "$@"

EMU_DIR="$SDCARD_PATH/Emus/shared/drastic"
PAK_DIR="$(dirname "$0")"

# Hook code (compiled into SDL2) needs libjson-c.so.4 but device may ship libjson-c.so.5
if [ ! -f "$EMU_DIR/libs/libjson-c.so.4" ] && [ -f /usr/lib/libjson-c.so.5 ]; then
    mkdir -p "$EMU_DIR/libs"
    cp /usr/lib/libjson-c.so.5 "$EMU_DIR/libs/libjson-c.so.4"
fi

export PATH="$EMU_DIR:$PATH"
# NDS.pak libs first (hook-patched SDL2), then shared libs, then /usr/lib for libudev
export LD_LIBRARY_PATH="$PAK_DIR/libs:$EMU_DIR/libs:/usr/lib:$LD_LIBRARY_PATH"

cleanup() {
    rm -f /tmp/stay_awake
    umount "$EMU_DIR/config" 2>/dev/null || true
    umount "$EMU_DIR/backup" 2>/dev/null || true
    umount "$EMU_DIR/cheats" 2>/dev/null || true
    umount "$EMU_DIR/savestates" 2>/dev/null || true
}

main() {
    echo "1" >/tmp/stay_awake
    trap "cleanup" EXIT INT TERM HUP QUIT

    # Set performance mode for NDS emulation
    echo performance >/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
    echo 1608000 >/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq

    # Determine device-specific config directory
    if [ "$DEVICE" = "brick" ]; then
        DEVICE_CONFIG_PATH="$SHARED_USERDATA_PATH/NDS-advanced-drastic/config/tg5040-brick"
        DEVICE_PAK_DIR="$PAK_DIR/devices/trimui-brick"
    else
        DEVICE_CONFIG_PATH="$SHARED_USERDATA_PATH/NDS-advanced-drastic/config/tg5040-smart-pro"
        DEVICE_PAK_DIR="$PAK_DIR/devices/trimui-smart-pro"
    fi

    # Setup external directories
    mkdir -p "$SDCARD_PATH/Saves/NDS"
    mkdir -p "$SDCARD_PATH/Cheats/NDS"
    mkdir -p "$DEVICE_CONFIG_PATH"
    mkdir -p "$EMU_DIR/backup"
    mkdir -p "$EMU_DIR/cheats"
    mkdir -p "$EMU_DIR/config"
    mkdir -p "$EMU_DIR/savestates"

    # Apply device-specific config on first run only (don't overwrite user changes)
    if [ ! -f "$DEVICE_CONFIG_PATH/.initialized" ]; then
        if [ -d "$DEVICE_PAK_DIR/config" ]; then
            cp "$DEVICE_PAK_DIR/config/drastic.cfg" "$DEVICE_CONFIG_PATH/" 2>/dev/null || true
            cp "$DEVICE_PAK_DIR/config/drastic.cf2" "$DEVICE_CONFIG_PATH/" 2>/dev/null || true
        fi
        touch "$DEVICE_CONFIG_PATH/.initialized"
    fi

    # Move any leftover cheats to centralized location
    if [ -d "$EMU_DIR/cheats" ] && ls -A "$EMU_DIR/cheats" 2>/dev/null | grep -q .; then
        cd "$EMU_DIR/cheats"
        mv * "$SDCARD_PATH/Cheats/NDS/" || true
    fi

    # Bind-mount external locations into drastic directory
    mount -o bind "$DEVICE_CONFIG_PATH" "$EMU_DIR/config"
    mount -o bind "$SDCARD_PATH/Saves/NDS" "$EMU_DIR/backup"
    mount -o bind "$SDCARD_PATH/Cheats/NDS" "$EMU_DIR/cheats"
    mount -o bind "$SHARED_USERDATA_PATH/NDS-advanced-drastic" "$EMU_DIR/savestates"

    # Launch drastic — use dummy video driver.
    # Tell SDL where the gamepad is (udev not running, so SDL can't auto-detect).
    cd "$EMU_DIR"
    export HOME="$USERDATA_PATH"
    export SDL_VIDEODRIVER=dummy
    export SDL_AUDIODRIVER=alsa
    export SDL_AUDIO_BUFFER_SIZE=2048
    export SDL_JOYSTICK_DEVICE=/dev/input/event3
    export SDL_JOYSTICK_DISABLE_UDEV=1

    # Mute speaker before launch to prevent audio pop, then unmute after init
    echo 1 > /sys/class/speaker/mute 2>/dev/null || true
    (sleep 5; echo 0 > /sys/class/speaker/mute 2>/dev/null; syncsettings.elf) &
    SYNC_PID=$!

    # Start power button sleep/poweroff handler
    sleepmon.elf &

    "$EMU_DIR/drastic" "$*"

    killall sleepmon.elf 2>/dev/null || true
    kill $SYNC_PID 2>/dev/null || true
    echo 0 > /sys/class/speaker/mute 2>/dev/null || true
}

main "$@"
