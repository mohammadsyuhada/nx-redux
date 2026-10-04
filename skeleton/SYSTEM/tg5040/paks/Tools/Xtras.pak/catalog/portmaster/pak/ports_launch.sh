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

# PortMaster's install (Xtras) and its self-update put back the stock control.txt, whose paths point at
# /roms/ports/PortMaster: a port then can't load device_info.txt and dies ("Game files are not installed
# correctly"), and the stock device_info.txt, which no longer recognises TrimUI. The PortMaster tool re-applies the
# NxRedux patches each time it opens, so a port launched before the tool's next run used the stock files; apply
# them here too (the tool's launch.sh keeps the one copy of every patch). Checked by each file's newest NX Redux
# marker, so a card patched by an older build is refreshed too; device_info.txt only when it has the
# 2026.09.19+ probe's capability line the patches anchor on. Only a tool with --patch-only is called: an older
# one would ignore the flag and open the PortMaster GUI.
PM_TOOL="$SDCARD_PATH/Tools/PortMaster.pak/launch.sh"
DI="$EMU_DIR/device_info.txt"
if { { [ -f "$EMU_DIR/control.txt" ] && ! grep -q 'NX Redux: exFAT/FAT32 compat' "$EMU_DIR/control.txt"; } \
     || { grep -q '^export DEVICE_CAPABILITIES=' "$DI" 2>/dev/null && ! grep -q 'NX Redux: TrimUI capability' "$DI"; }; } \
    && [ -f "$PM_TOOL" ] && grep -q -- '--patch-only' "$PM_TOOL"; then
    echo "PortMaster files are unpatched: applying the NxRedux patches"
    sh "$PM_TOOL" --patch-only
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

    # Mounts the port left under HOME or the ports dir (bind_directories,
    # control.txt's ln fallback, runtime squashfs), deepest first. Left mounted, the next run's
    # `rm -rf ~/.config/<game>` would empty the save folder through them.
    awk -v h="$HOME/" -v t="$TEMP_DATA_DIR/ports/" '
        { gsub(/\\040/, " ", $2) }
        index($2, h) == 1 || index($2, t) == 1 { print $2 }
    ' /proc/mounts 2>/dev/null | sort -r | while IFS= read -r m; do
        umount "$m" 2>/dev/null || umount -l "$m" 2>/dev/null
    done

    umount "$TEMP_DATA_DIR/ports" 2>/dev/null || umount -l "$TEMP_DATA_DIR/ports" 2>/dev/null || true
    # Use rmdir (not rm -rf) so a still-mounted bind mount can't delete .ports game data
    rmdir "$TEMP_DATA_DIR/ports" 2>/dev/null
    rmdir "$TEMP_DATA_DIR" 2>/dev/null
    rm -f "$HOME/.asoundrc" 2>/dev/null
    # The audio routing bound over /etc/asound.conf in main()
    umount "$NX_ASOUND_DEST" 2>/dev/null
    rm -f "$NX_ASOUND"
}

# ALSA reads ~/.asoundrc, and some ports move HOME into their game folder
# (every NextOS port, a few official ones), which lost the Bluetooth/USB DAC
# routing and the Game Volume control. ALSA_CONFIG_PATH can't add it (this
# alsa-lib, 1.1.8, ignores definitions in extra files listed there), but
# /etc/asound.conf is always read: for the port session, bind a copy with the
# routing appended over it. cleanup() unmounts it; a reboot drops it too.
NX_ASOUND="/tmp/nx_ports_asound.conf"
NX_ASOUND_DEST="/etc/asound.conf"
bind_audio_routing() {
    [ -f "$HOME/.asoundrc" ] && [ -f "$NX_ASOUND_DEST" ] || return 0
    umount "$NX_ASOUND_DEST" 2>/dev/null
    cat "$NX_ASOUND_DEST" "$HOME/.asoundrc" > "$NX_ASOUND" \
        && mount --bind "$NX_ASOUND" "$NX_ASOUND_DEST"
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

main() {
    echo "1" >/tmp/stay_awake
    trap "cleanup" EXIT INT TERM HUP QUIT
    bind_audio_routing

    # Set performance mode for ports (set max before min to avoid min > max rejection)
    echo performance >/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
    echo 2000000 >/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq
    echo 2000000 >/sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq

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
    # /roms/ports/<dir> (other firmwares' ports folder, which some third-party
    # wrappers look in for their game folder) -> the .ports bind mount above.
    sed -i -e "s|/roms/ports/PortMaster|$EMU_DIR|g" \
           -e "s|/roms/ports/|$TEMP_DATA_DIR/ports/|g" \
           -e "s|/mnt/SDCARD/Emus/tg50[45]0/PORTS.pak/PortMaster|$EMU_DIR|g" \
           -e '1s|^#!/bin/bash|#!/usr/bin/env bash|' "$ROM_PATH"

    # Re-apply the saved audio sink, volume and brightness once the emulator has
    # finished its own SDL/ALSA init, which clobbers the mixer. The old pre-launch
    # codec mute stays out: it would silence background music.
    (sleep 5; syncsettings.elf) &
    SYNC_PID=$!

    # Apply the global button layout (Settings > System > Button layout).
    # Replaces the old per-runtime xbox_layout marker; the marker is ignored.
    . "$SYSTEM_PATH/bin/nx_button_layout.sh"
    set_controller_layout "$NX_BUTTON_LAYOUT"

    add_input_udev_rule

    # Start power button sleep/poweroff handler
    sleepmon.elf &

    echo "Starting port: $ROM_PATH"
    cd "$ROM_DIR"
    bash "$ROM_PATH"
}

main "$@"
