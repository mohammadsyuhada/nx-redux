#!/bin/sh
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
[ ! -f /bin/bash ] && ln -sf "$EMU_DIR/bin/bash" /bin/bash

# tg5050 ships newer lib versions than what some bundled binaries expect
# (can't symlink on exFAT, so copy the actual files)
for lib_pair in "libffi.so.7 libffi.so.8" "libncurses.so.5 libncurses.so.6" "libncursesw.so.5 libncursesw.so.6"; do
    old="${lib_pair% *}" new="${lib_pair#* }"
    [ ! -e "$EMU_DIR/lib/$old" ] && [ -e "/usr/lib/$new" ] && \
        cp "/usr/lib/$new" "$EMU_DIR/lib/$old"
done

export LD_LIBRARY_PATH="$EMU_DIR/lib/compat:$EMU_DIR/lib:/usr/trimui/lib:$LD_LIBRARY_PATH"
export SSL_CERT_FILE="$EMU_DIR/ssl/certs/ca-certificates.crt"
export SDL_GAMECONTROLLERCONFIG_FILE="$EMU_DIR/gamecontrollerdb.txt"
export PYSDL2_DLL_PATH="/usr/trimui/lib"
export HOME="$SHARED_USERDATA_PATH/PORTS-portmaster"
# Copy audio config so ALSA finds Bluetooth/USB DAC routing (audiomon writes to USERDATA_PATH)
[ -f "$USERDATA_PATH/.asoundrc" ] && cp "$USERDATA_PATH/.asoundrc" "$HOME/.asoundrc"
# The standard XDG data home, as on other PortMaster platforms: games inherit it
# and keep their data under ~/.local/share/<game>, which is where port scripts
# bind_directories their save/config folders. Port scripts find PortMaster via
# their fallback path, rewritten to $EMU_DIR before launch (see main).
export XDG_DATA_HOME="$HOME/.local/share"
# Port scripts take $XDG_DATA_HOME/PortMaster over their fallback whenever
# that directory exists, so a stray one (some cards carry an empty leftover)
# hides the real install and the port dies sourcing control.txt. Remove it
# when empty; a non-empty one without control.txt is left alone and logged.
if [ -d "$XDG_DATA_HOME/PortMaster" ] && [ ! -f "$XDG_DATA_HOME/PortMaster/control.txt" ]; then
    rmdir "$XDG_DATA_HOME/PortMaster" 2>/dev/null \
        || echo "warning: $XDG_DATA_HOME/PortMaster has no control.txt; port scripts will use it over $EMU_DIR"
fi

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
    killall sleepmon.elf 2>/dev/null || true
    killall show2.elf 2>/dev/null || true
    kill $SYNC_PID 2>/dev/null || true

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

    # The layout variants ship in files/ (BASE Emus/shared/PortMaster/files/,
    # same dir portmaster.c copies from via PM_FILES_DIR); fall back to the
    # runtime root for older layouts that kept them there.
    local src="$EMU_DIR/files/gamecontrollerdb_$1.txt"
    [ -f "$src" ] || src="$EMU_DIR/gamecontrollerdb_$1.txt"
    if [ -f "$src" ]; then
        # Only copy if different
        cmp -s "$src" "$dest" || cp -f "$src" "$dest"
    else
        echo "ports_launch: gamecontrollerdb_$1.txt not found"
    fi

    # Always export the TrimUI pad mapping (highest SDL priority) so a port
    # that ships its own gamecontrollerdb still follows the chosen face layout.
    case "$1" in
        nintendo)
            # Nintendo positions: a:b1,b:b0 and x:b3,y:b2
            export SDL_GAMECONTROLLERCONFIG="${TRIMUI_GUID},TRIMUI Player1,a:b1,b:b0,back:b6,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b8,leftshoulder:b4,leftstick:b9,lefttrigger:a2,leftx:a0,lefty:a1,rightshoulder:b5,rightstick:b10,righttrigger:a5,rightx:a3,righty:a4,start:b7,x:b3,y:b2,platform:Linux,"
            ;;
        xbox)
            # Xbox positions: a:b0,b:b1 and x:b2,y:b3
            export SDL_GAMECONTROLLERCONFIG="${TRIMUI_GUID},TRIMUI Player1,a:b0,b:b1,back:b6,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b8,leftshoulder:b4,leftstick:b9,lefttrigger:a2,leftx:a0,lefty:a1,rightshoulder:b5,rightstick:b10,righttrigger:a5,rightx:a3,righty:a4,start:b7,x:b2,y:b3,platform:Linux,"
            ;;
    esac
}

# Ports on PortMaster's Weston runtime (weston_pkg) need udev to label input
# devices (ID_INPUT): libinput skips unlabelled devices and Weston exits with no
# input, so the game gets no X display (black screen, "GLFW library is not
# initialized"). The stock firmware's udev ships no input_id rule, so add one in
# udev's runtime rules dir (RAM, gone at reboot) and re-scan the input devices.
# Once per boot; virtual pads gptokeyb creates later are labelled by the rule.
add_input_udev_rule() {
    command -v udevadm >/dev/null 2>&1 || return 0
    for d in /run/udev /tmp/run/udev; do
        [ -d "$d/data" ] || continue
        [ -f "$d/rules.d/60-nx-input-id.rules" ] && return 0
        mkdir -p "$d/rules.d" || return 0
        printf '%s\n' \
            'ACTION=="remove", GOTO="nx_input_id_end"' \
            'SUBSYSTEM=="input", ENV{ID_INPUT}=="", IMPORT{builtin}="input_id"' \
            'LABEL="nx_input_id_end"' >"$d/rules.d/60-nx-input-id.rules"
        udevadm control --reload 2>/dev/null
        udevadm trigger --action=add --subsystem-match=input 2>/dev/null
        udevadm settle -t 3 2>/dev/null
        return 0
    done
}

# PortMaster 2026.09.19+ rewrote device_info.txt's host probe and dropped the
# TrimUI firmware branch (`[ -d /usr/trimui ]` -> CFW_NAME=TrimUI). On TrimUI
# CFW_NAME stays "Unknown": pugwash falls back to its default platform (no
# Xbox A/B fix, so the PortMaster app's buttons come out inverted) and port
# scripts skip mod_TrimUI.txt. Re-add it ahead of the os-release fallback,
# naming the model from the launcher's $DEVICE and taking the firmware
# version from /etc/version (as the old upstream probe did). No-op on
# device_info files without those fallbacks (the older ones still detect
# TrimUI themselves).
# PortMaster caches the probe as device_info_<cfw>_<device>.env and reads a
# cache before re-running the script, so a probe cached as "unknown" is
# dropped, and the TrimUI caches are dropped whenever the script is patched.
patch_device_info_trimui() { # $1 = device_info.txt
    [ -d /usr/trimui ] || return 0
    rm -f "${1%/*}"/device_info_unknown_*.env
    grep -q 'NX Redux: TrimUI version' "$1" 2>/dev/null && return 0
    grep -q '^if \[ "\$CFW_NAME" = "Unknown" \] && {' "$1" 2>/dev/null || return 0
    grep -q '^if \[ "\$CFW_VERSION" = "Unknown" \] && {' "$1" 2>/dev/null || return 0
    _nxname=0
    grep -q 'NX Redux: TrimUI firmware' "$1" && _nxname=1
    awk -v name="$_nxname" '
        !name && /^if \[ "\$CFW_NAME" = "Unknown" \] && \{/ {
            print "# NX Redux: TrimUI firmware (dropped from the upstream probe)"
            print "if [ \"$CFW_NAME\" = \"Unknown\" ] && [ -d \"/usr/trimui\" ]; then"
            print "    export CFW_NAME=\"TrimUI\""
            print "    case \"$DEVICE\" in"
            print "        brickpro) export DEVICE_NAME=\"TrimUI Brick Pro\" ;;"
            print "        brick) export DEVICE_NAME=\"TrimUI Brick\" ;;"
            print "        smartpros) export DEVICE_NAME=\"TrimUI Smart Pro S\" ;;"
            print "        *) export DEVICE_NAME=\"TrimUI Smart Pro\" ;;"
            print "    esac"
            print "fi"
            print ""
            name = 1
        }
        !ver && /^if \[ "\$CFW_VERSION" = "Unknown" \] && \{/ {
            print "# NX Redux: TrimUI version"
            print "if [ \"$CFW_NAME\" = \"TrimUI\" ] && [ \"$CFW_VERSION\" = \"Unknown\" ] && [ -f /etc/version ]; then"
            print "    export CFW_VERSION=\"$(tr -d \x27\\r\\n\x27 < /etc/version)\""
            print "fi"
            print ""
            ver = 1
        }
        { print }
    ' "$1" >"$1.nxtmp" && mv -f "$1.nxtmp" "$1"
    rm -f "${1%/*}"/device_info_trimui_*.env
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
           -e "s|/mnt/SDCARD/Emus/tg50[45]0/PORTS.pak/PortMaster|$EMU_DIR|g" \
           -e '1s|^#!/bin/bash|#!/usr/bin/env bash|' "$ROM_PATH"

    # Apply the global button layout (Settings > System > Button layout).
    # Replaces the old per-runtime xbox_layout marker; the marker is ignored.
    . "$SYSTEM_PATH/bin/nx_button_layout.sh"
    set_controller_layout "$NX_BUTTON_LAYOUT"

    add_input_udev_rule
    patch_device_info_trimui "$EMU_DIR/device_info.txt"

    # Start power button sleep/poweroff handler
    sleepmon.elf &

    echo "Starting port: $ROM_PATH"
    cd "$ROM_DIR"

    # Re-apply the saved audio sink, volume and brightness once the emulator has
    # finished its own SDL/ALSA init, which clobbers the mixer. The old pre-launch
    # codec mute stays out: it would silence background music.
    (sleep 5; syncsettings.elf) &
    SYNC_PID=$!

    bash "$ROM_PATH"
}

main "$@"
