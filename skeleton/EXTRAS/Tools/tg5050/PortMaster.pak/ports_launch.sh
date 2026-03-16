#!/bin/sh
# Mute speaker to suppress pop during SDL audio init
echo 1 > /sys/class/speaker/mute 2>/dev/null || true

PAK_DIR="$(dirname "$0")"
PAK_NAME="$(basename "$PAK_DIR")"
PAK_NAME="${PAK_NAME%.*}"

rm -f "$LOGS_PATH/$PAK_NAME.txt"
exec >>"$LOGS_PATH/$PAK_NAME.txt"
exec 2>&1
[ -f "$USERDATA_PATH/PORTS-portmaster/debug" ] && set -x

echo "$0" "$*"
cd "$PAK_DIR" || exit 1
mkdir -p "$USERDATA_PATH/PORTS-portmaster"
mkdir -p "$SHARED_USERDATA_PATH/PORTS-portmaster"

EMU_DIR="$SDCARD_PATH/Emus/shared/PortMaster"
export PATH="$EMU_DIR/bin:$EMU_DIR:$SHARED_SYSTEM_PATH/bin:$PATH"

# tg5050 ships newer lib versions than what some bundled binaries expect
# (can't symlink on exFAT, so copy the actual files)
for lib_pair in "libffi.so.7 libffi.so.8" "libncurses.so.5 libncurses.so.6" "libncursesw.so.5 libncursesw.so.6"; do
    old="${lib_pair% *}" new="${lib_pair#* }"
    [ ! -e "$EMU_DIR/lib/$old" ] && [ -e "/usr/lib/$new" ] && \
        cp "/usr/lib/$new" "$EMU_DIR/lib/$old"
done

export LD_LIBRARY_PATH="$EMU_DIR/lib:/usr/trimui/lib:$LD_LIBRARY_PATH"
export SSL_CERT_FILE="$EMU_DIR/ssl/certs/ca-certificates.crt"
export SDL_GAMECONTROLLERCONFIG_FILE="$EMU_DIR/gamecontrollerdb.txt"
export PYSDL2_DLL_PATH="/usr/trimui/lib"
export HOME="$SHARED_USERDATA_PATH/PORTS-portmaster"
# Copy audio config so ALSA finds Bluetooth/USB DAC routing (audiomon writes to USERDATA_PATH)
[ -f "$USERDATA_PATH/.asoundrc" ] && cp "$USERDATA_PATH/.asoundrc" "$HOME/.asoundrc"
# Point XDG_DATA_HOME to PortMaster's parent so port scripts find it directly
# (port scripts check: elif [ -d "$XDG_DATA_HOME/PortMaster/" ])
export XDG_DATA_HOME="$SDCARD_PATH/Emus/shared"

[ -z "$1" ] && exit 1
ROM_PATH="$1"
ROM_DIR="$(dirname "$ROM_PATH")"
ROM_NAME="$(basename "$ROM_PATH")"
TEMP_DATA_DIR="$SDCARD_PATH/.ports_temp"
PORTS_DIR="$ROM_DIR/.ports"

export controlfolder="$EMU_DIR"
export PM_SCRIPTNAME="$ROM_NAME"

export HM_TOOLS_DIR="$SDCARD_PATH/Emus/shared"
export HM_PORTS_DIR="$TEMP_DATA_DIR/ports"
export HM_SCRIPTS_DIR="$TEMP_DATA_DIR/ports"

cleanup() {
    kill $SYNC_PID 2>/dev/null || true
    echo 0 > /sys/class/speaker/mute 2>/dev/null || true

    umount "$TEMP_DATA_DIR/ports" 2>/dev/null || umount -l "$TEMP_DATA_DIR/ports" 2>/dev/null || true
    # Use rmdir (not rm -rf) so a still-mounted bind mount can't delete .ports game data
    rmdir "$TEMP_DATA_DIR/ports" 2>/dev/null
    rmdir "$TEMP_DATA_DIR" 2>/dev/null
    rm -f "$HOME/.asoundrc" 2>/dev/null
}

set_controller_layout() {
    # TRIMUI Player1 GUID (Bus=0003 Vendor=045e Product=028e Version=0114)
    local TRIMUI_GUID="030000005e0400008e02000014010000"
    local dest="$EMU_DIR/gamecontrollerdb.txt"

    case "$1" in
        nintendo)
            local src="$EMU_DIR/gamecontrollerdb_nintendo.txt"
            if [ -f "$src" ]; then
                # Only copy if different
                cmp -s "$src" "$dest" || cp -f "$src" "$dest"
            fi
            # Set env var (highest priority) to swap A/B and X/Y
            export SDL_GAMECONTROLLERCONFIG="${TRIMUI_GUID},TRIMUI Player1,a:b1,b:b0,back:b6,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b8,leftshoulder:b4,leftstick:b9,lefttrigger:a2,leftx:a0,lefty:a1,rightshoulder:b5,rightstick:b10,righttrigger:a5,rightx:a3,righty:a4,start:b7,x:b3,y:b2,platform:Linux,"
            ;;
        xbox)
            local src="$EMU_DIR/gamecontrollerdb_xbox.txt"
            if [ -f "$src" ]; then
                cmp -s "$src" "$dest" || cp -f "$src" "$dest"
            fi
            # Clear any Nintendo override — default Xbox mapping is a:b0,b:b1
            unset SDL_GAMECONTROLLERCONFIG
            ;;
    esac
}

main() {
    echo "1" >/tmp/stay_awake
    trap "cleanup" EXIT INT TERM HUP QUIT

    # Bring all cores online for multi-threaded ports
    for i in 2 3 5 6 7; do
        echo 1 > /sys/devices/system/cpu/cpu$i/online 2>/dev/null
    done

    # Set CPU scaling — schedutil scales active cores to max, idle cores stay low
    echo schedutil >/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
    echo 408000 >/sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq
    echo 1416000 >/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq
    echo schedutil >/sys/devices/system/cpu/cpu4/cpufreq/scaling_governor
    echo 408000 >/sys/devices/system/cpu/cpu4/cpufreq/scaling_min_freq
    echo 2160000 >/sys/devices/system/cpu/cpu4/cpufreq/scaling_max_freq

    # GPU: lock to performance for port games (many use OpenGL/SDL2 rendering)
    echo performance >/sys/devices/platform/soc@3000000/1800000.gpu/devfreq/1800000.gpu/governor 2>/dev/null

    mkdir -p "$PORTS_DIR"

    # Bind mount .ports to temp dir so port scripts can find game data
    # Port scripts do: cd /$directory/ports/<game>
    # This maps /mnt/SDCARD/.ports_temp/ports -> $ROM_DIR/.ports
    umount "$TEMP_DATA_DIR/ports" 2>/dev/null || true
    mkdir -p "$TEMP_DATA_DIR/ports"
    if ! mount -o bind "$PORTS_DIR" "$TEMP_DATA_DIR/ports"; then
        echo "ERROR: Failed to bind mount $PORTS_DIR to $TEMP_DATA_DIR/ports"
        exit 1
    fi

    # Fix hardcoded paths and shebangs
    sed -i -e "s|/roms/ports/PortMaster|$EMU_DIR|g" \
           -e '1s|^#!/bin/bash|#!/usr/bin/env bash|' "$ROM_PATH"

    # Apply user's chosen button layout (default: nintendo)
    if [ -f "$SHARED_USERDATA_PATH/PORTS-portmaster/xbox_layout" ]; then
        set_controller_layout xbox
    else
        set_controller_layout nintendo
    fi

    echo "Starting port: $ROM_PATH"
    cd "$ROM_DIR"

    # Unmute speaker after game audio has initialized
    (sleep 5; echo 0 > /sys/class/speaker/mute 2>/dev/null; syncsettings.elf) &
    SYNC_PID=$!

    bash "$ROM_PATH"
}

main "$@"
