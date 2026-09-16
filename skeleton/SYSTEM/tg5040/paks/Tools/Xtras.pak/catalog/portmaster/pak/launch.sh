#!/bin/sh
# Tools/PortMaster.pak/launch.sh - opens PortMaster's GUI (pugwash).
#
# Installed to the flat Tools/ location by the Xtras catalog entry
# (catalog/portmaster/install.sh) and re-copied from the catalog by the
# platform install/update.sh on every system update, so a card never runs a
# stale copy. This script replaced the compiled ELF launcher (2026-09-16); everything
# the elf did around a pugwash run lives here:
#   - "not installed" pointer at Xtras when the runtime is missing
#   - "Launching PortMaster..." frame while Python starts
#   - NxRedux patches re-applied BEFORE and AFTER every run (pugwash's
#     first_run / self-update overwrite them): control.txt, device_info.txt
#     and hardware.py (Smart Pro S + Brick Pro detection), platform.py
#     (paths + portmaster_install disabled), mod_TrimUI.txt (HOME)
#   - pugwash loop: honours .pugwash-reboot, retries once when a fresh
#     pylibs extraction crashed it before the patches landed
#   - Xbox pad map while pugwash runs (its XBOX FIXER assumes it), the
#     user's Button layout setting restored afterwards
#   - post-run: fix installed port scripts, apply patchedScripts/, recreate
#     busybox wrappers, resync the Xtras version marker, sync cover art to
#     .media/, drop the launcher's list caches
# Busybox sh + busybox/GNU sed only. Runs with the pak env from
# MinUI.pak/launch.sh (SDCARD_PATH, SYSTEM_PATH, LOGS_PATH, ...).

rm -f "$LOGS_PATH/portmaster.txt"
exec >"$LOGS_PATH/portmaster.txt" 2>&1
echo "$0 $*"

PM_DIR="$SDCARD_PATH/Emus/shared/PortMaster"
PM_FILES="$PM_DIR/files"
PORTS_ROM_DIR="$SDCARD_PATH/Roms/Ports (PORTS)"
XTRAS_STATE_DIR="$SHARED_USERDATA_PATH/xtras"
PY="$PM_DIR/bin/python3"
PP="$PM_DIR/pylibs/harbourmaster/platform.py"
DI="$PM_DIR/device_info.txt"
HW="$PM_DIR/pylibs/harbourmaster/hardware.py"

splash() { # $1 = text, $2 = extra show2 args
    killall -9 show2.elf >/dev/null 2>&1
    # shellcheck disable=SC2086
    show2.elf --mode=progress --image=/dev/null --text="$1" --progress=-1 \
        --texty=45 --progressy=55 --fontsize=28 $2 &
}

if [ ! -f "$PM_DIR/pugwash" ]; then
    echo "PortMaster runtime not found at $PM_DIR"
    splash "PortMaster is not installed. Install it from Xtras." "--timeout=4"
    wait
    exit 0
fi

splash "Launching PortMaster..." ""

# Same CPU profile ports get (ports_launch.sh). The old 600 MHz / idle-core
# cap was for the elf's menu, which no longer exists; pugwash is a Python
# GUI that downloads and unpacks zips.
case "$PLATFORM" in
    tg5050)
        for i in 2 3 5 6 7; do echo 1 > /sys/devices/system/cpu/cpu$i/online 2>/dev/null; done
        echo schedutil > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null
        echo 408000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq 2>/dev/null
        echo 1416000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq 2>/dev/null
        echo schedutil > /sys/devices/system/cpu/cpu4/cpufreq/scaling_governor 2>/dev/null
        echo 408000 > /sys/devices/system/cpu/cpu4/cpufreq/scaling_min_freq 2>/dev/null
        echo 2160000 > /sys/devices/system/cpu/cpu4/cpufreq/scaling_max_freq 2>/dev/null
        ;;
    *)
        echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null
        echo 2000000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq 2>/dev/null
        echo 2000000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq 2>/dev/null
        ;;
esac

# Many port scripts use #!/bin/bash; the firmware has no /bin/bash.
[ -e /bin/bash ] || ln -sf "$PM_DIR/bin/bash" /bin/bash

# tg5050 ships newer lib versions than some bundled binaries expect
# (no symlinks on FAT, so copy)
[ ! -e "$PM_DIR/lib/libffi.so.7" ] && [ -e /usr/lib/libffi.so.8 ] && cp /usr/lib/libffi.so.8 "$PM_DIR/lib/libffi.so.7"
[ ! -e "$PM_DIR/lib/libncurses.so.5" ] && [ -e /usr/lib/libncurses.so.6 ] && cp /usr/lib/libncurses.so.6 "$PM_DIR/lib/libncurses.so.5"

# Default pugwash config: skip the disclaimer/theme first-run prompts.
if [ ! -f "$PM_DIR/config/config.json" ]; then
    mkdir -p "$PM_DIR/config"
    cat > "$PM_DIR/config/config.json" <<'EOF'
{
    "disclaimer": true,
    "show_experimental": false,
    "theme": "default_theme",
    "theme-scheme": "Darkest Mode"
}
EOF
fi

# ---- NxRedux patches ------------------------------------------------------

patch_control_txt() {
    cat > "$PM_DIR/control.txt" <<EOF
#!/bin/sh
#
# SPDX-License-Identifier: MIT
#
# Patched for NxRedux

CUR_TTY=/dev/tty0

export controlfolder="$PM_DIR"
export directory="mnt/SDCARD/.ports_temp"

PM_SCRIPTNAME="\$(basename "\${PM_SCRIPTNAME:-\$0}")"
PM_PORTNAME="\${PM_SCRIPTNAME%.sh}"

if [ -z "\$PM_PORTNAME" ]; then
  PM_PORTNAME="Port"
fi

export ESUDO=""
export ESUDOKILL="-1"
export SDL_GAMECONTROLLERCONFIG_FILE="\$controlfolder/gamecontrollerdb.txt"

get_controls() {
  sleep 0.5
}

. \$controlfolder/device_info.txt
. \$controlfolder/funcs.txt

export GPTOKEYB2="\$ESUDO env LD_PRELOAD=\$controlfolder/libinterpose.aarch64.so \$controlfolder/gptokeyb2 \$ESUDOKILL"
export GPTOKEYB="\$ESUDO \$controlfolder/gptokeyb \$ESUDOKILL"
EOF
}

patch_device_info() {
    # Smart Pro S (tg5050, sun55iw3) vs Smart Pro / Brick (tg5040): stock
    # PortMaster hardcodes "TrimUI Smart Pro". Patch 1 makes the CFW_NAME
    # block probe the device tree; patch 2 adds the FIXES case.
    if ! grep -q 'Smart Pro S' "$DI" 2>/dev/null; then
        sed -i '/\$CFW_NAME.*TrimUI/{n;s|DEVICE_NAME="TrimUI Smart Pro"|if grep -q sun55iw3 /proc/device-tree/model 2>/dev/null; then DEVICE_NAME="TrimUI Smart Pro S"; else DEVICE_NAME="TrimUI Smart Pro"; fi|;}' "$DI"
        sed -i '/"trimui smart pro"|"trimui-smart-pro")/i\    "trimui smart pro s"|"trimui-smart-pro-s")\n        DEVICE_CPU="t527"\n        DEVICE_NAME="TrimUI Smart Pro S"\n        ;;' "$DI"
    fi
    # hardware.py (pugwash's own detection): sun55iw3 -> trimui-smart-pro-s
    if ! grep -q 'smart-pro-s' "$HW" 2>/dev/null; then
        sed -i "s|('sun50iw10', 'trimui-smart-pro'),|('sun55iw3',  'trimui-smart-pro-s'),\n        ('sun50iw10', 'trimui-smart-pro'),|" "$HW"
        sed -i '/"TrimUI Smart Pro":.*trimui-smart-pro/a\    "TrimUI Smart Pro S": {"device": "trimui-smart-pro-s", "manufacturer": "TrimUI", "cfw": ["TrimUI"]},' "$HW"
        sed -i '/"trimui-smart-pro":.*a133plus/a\    "trimui-smart-pro-s": {"resolution": (1280, 720), "analogsticks": 2, "cpu": "t527", "capabilities": ["power"], "ram": 1024},' "$HW"
        sed -i '/"trimui-\*":/i\    "trimui-smart-pro-s*": "2.33",' "$HW"
        rm -rf "$PM_DIR/pylibs/harbourmaster/__pycache__"
    fi
    # Brick Pro: same resolution AND SoC as the Brick, so both detection
    # paths collapse it into trimui-brick (0 sticks). The redux SD marker
    # /mnt/SDCARD/tg5040-brickpro tells them apart.
    if ! grep -q 'TrimUI Brick Pro' "$DI" 2>/dev/null; then
        sed -i '/^# GLIBC$/i\if [ -e "/mnt/SDCARD/tg5040-brickpro" ] && [ "$DEVICE_NAME" = "TrimUI Brick" ]; then DEVICE_NAME="TrimUI Brick Pro"; ANALOG_STICKS=2; fi' "$DI"
    fi
    if ! grep -q 'trimui-brick-pro' "$HW" 2>/dev/null; then
        sed -i '/"TrimUI Brick": .*"trimui-brick",/a\    "TrimUI Brick Pro": {"device": "trimui-brick-pro", "manufacturer": "TrimUI", "cfw": ["TrimUI"]},' "$HW"
        sed -i '/"trimui-brick": .*"analogsticks": 0/a\    "trimui-brick-pro": {"resolution": (1024, 768), "analogsticks": 2, "cpu": "a133plus", "capabilities": ["power"], "ram": 1024},' "$HW"
        sed -i '/    expand_info(info, override_resolution, override_ram)/i\    if info["device"] == "trimui-brick" and Path("/mnt/SDCARD/tg5040-brickpro").exists(): info["device"] = "trimui-brick-pro"' "$HW"
        rm -rf "$PM_DIR/pylibs/harbourmaster/__pycache__"
    fi
}

patch_platform_py() {
    [ -f "$PP" ] || return 0
    # portmaster_install crashes on TrimUI; NxRedux names the Ports folder
    # differently from what PortMaster hardcodes.
    sed -i "s|/mnt/SDCARD/Roms/PORTS|$PORTS_ROM_DIR|g;s|/mnt/SDCARD/Imgs/PORTS|$PORTS_ROM_DIR/.media|g" "$PP"
    "$PY" "$PM_DIR/disable_python_function.py" "$PP" portmaster_install 2>/dev/null
    rm -rf "$PM_DIR/pylibs/harbourmaster/__pycache__"
}

patch_mod_trimui() {
    [ -f "$PM_DIR/mod_TrimUI.txt" ] || return 0
    sed -i "s|/mnt/SDCARD/Data/home|$SHARED_USERDATA_PATH/PORTS-portmaster|g" "$PM_DIR/mod_TrimUI.txt"
}

apply_patches() {
    patch_control_txt
    patch_device_info
    patch_platform_py
    patch_mod_trimui
}

set_controller_layout() { # $1 = nintendo|xbox
    [ -f "$PM_FILES/gamecontrollerdb_$1.txt" ] && cp -f "$PM_FILES/gamecontrollerdb_$1.txt" "$PM_DIR/gamecontrollerdb.txt"
}

# ---- post-run housekeeping ------------------------------------------------

fix_port_scripts() {
    [ -d "$PORTS_ROM_DIR" ] || return 0
    find "$PORTS_ROM_DIR" -maxdepth 1 -type f -name '*.sh' | while IFS= read -r f; do
        if grep -q '/roms/ports/PortMaster' "$f" 2>/dev/null; then
            sed -i "s|/roms/ports/PortMaster|$PM_DIR|g" "$f"
        fi
        if head -1 "$f" | grep -q '^#!/bin/bash'; then
            sed -i '1s|#!/bin/bash|#!/usr/bin/env bash|' "$f"
        fi
    done
}

apply_patched_scripts() {
    [ -d "$PM_DIR/patchedScripts" ] || return 0
    for f in "$PM_DIR/patchedScripts"/*.sh; do
        [ -f "$f" ] || continue
        [ -f "$PORTS_ROM_DIR/$(basename "$f")" ] && cp -f "$f" "$PORTS_ROM_DIR/$(basename "$f")"
    done
}

create_busybox_wrappers() {
    BB="$PM_DIR/bin/busybox"
    [ -f "$PM_DIR/bin/busybox_wrappers.done" ] && return 0
    [ -f "$BB" ] || return 0
    (cd "$PM_DIR/bin" && created='' && \
    for cmd in $("$BB" --list); do
        case "$cmd" in sh) continue ;; esac
        if [ ! -e "$cmd" ] || grep -q 'exec .*/busybox .*\$@' "$cmd" 2>/dev/null; then
            printf '#!/bin/sh\nexec %s %s "$@"\n' "$BB" "$cmd" > "$cmd"
            created="$created $cmd"
        fi
    done
    # shellcheck disable=SC2086
    [ -n "$created" ] && chmod +x $created
    touch busybox_wrappers.done) 2>/dev/null
}

sync_xtras_version_marker() {
    # Xtras flags "Update Available" from .userdata/shared/xtras/
    # portmaster.version; a pugwash self-update changes the runtime's own
    # version file without touching that marker. Resync after every run.
    tag="$(head -n 1 "$PM_DIR/version" 2>/dev/null | tr -d '\r\n' | cut -d' ' -f1)"
    [ -n "$tag" ] || return 0
    mkdir -p "$XTRAS_STATE_DIR"
    echo "$tag" > "$XTRAS_STATE_DIR/portmaster.version"
}

sync_port_artwork() {
    # cover/screenshot from .ports/<port>/ -> .media/<script-basename>.png
    ports="$PORTS_ROM_DIR/.ports"
    media="$PORTS_ROM_DIR/.media"
    mkdir -p "$media"
    [ -d "$ports" ] || return 0
    for dir in "$ports"/*/; do
        [ -f "$dir/port.json" ] || continue
        sh_name="$(grep -o '"[^"]*\.sh"' "$dir/port.json" | head -n 1 | tr -d '"')"
        [ -n "$sh_name" ] || continue
        base="${sh_name%.sh}"
        [ -e "$media/$base.png" ] && continue
        for c in cover.png cover.jpg screenshot.png screenshot.jpg; do
            if [ -f "$dir/$c" ]; then
                cp -f "$dir/$c" "$media/$base.png"
                break
            fi
        done
    done
}

# ---- run pugwash ----------------------------------------------------------

apply_patches
set_controller_layout xbox

export LD_LIBRARY_PATH="$SYSTEM_PATH/lib:$PM_DIR/lib:/usr/trimui/lib:/usr/lib:$LD_LIBRARY_PATH"
export PATH="$SYSTEM_PATH/bin:$PM_DIR/bin:$SHARED_SYSTEM_PATH/bin:/usr/trimui/bin:$PATH"
export PYSDL2_DLL_PATH="/usr/trimui/lib:/usr/lib"
export SSL_CERT_FILE="$PM_DIR/ssl/certs/ca-certificates.crt"
export HOME="$SHARED_USERDATA_PATH/PORTS-portmaster"
export XDG_DATA_HOME="$SDCARD_PATH/Emus/shared"
export HM_TOOLS_DIR="$SDCARD_PATH/Emus/shared"
export HM_PORTS_DIR="$PORTS_ROM_DIR/.ports"
export HM_SCRIPTS_DIR="$PORTS_ROM_DIR"
export SDL_GAMECONTROLLERCONFIG_FILE="$PM_DIR/gamecontrollerdb.txt"
mkdir -p "$HOME"

cd "$PM_DIR" || exit 1
rm -f .pugwash-reboot
RETRIES=0
while true; do
    # platform.py before EVERY run: covers first_run (pylibs just
    # extracted) and a self-update.
    patch_platform_py
    # The splash drew its frame; it must not keep painting over pugwash.
    # -9: show2 never drains SDL events, so SIGTERM is swallowed (see MinUI.pak/launch.sh)
    killall -9 show2.elf >/dev/null 2>&1
    "$PY" pugwash 2>&1 | tee "$LOGS_PATH/portmaster_pugwash.txt"
    if [ -f .pugwash-reboot ]; then
        rm -f .pugwash-reboot
        continue
    fi
    # Unpatched paths again = pugwash re-extracted pylibs and crashed on
    # them; one retry with the patches applied.
    if grep -q '/Roms/PORTS' "$PP" 2>/dev/null && [ "$RETRIES" -lt 1 ]; then
        RETRIES=$((RETRIES + 1))
        continue
    fi
    break
done

# ---- after pugwash --------------------------------------------------------

apply_patches
. "$SYSTEM_PATH/bin/nx_button_layout.sh"
set_controller_layout "$NX_BUTTON_LAYOUT"
fix_port_scripts
apply_patched_scripts
create_busybox_wrappers
sync_xtras_version_marker
sync_port_artwork
rm -f "$USERDATA_PATH/emulist_cache.txt" "$USERDATA_PATH/romindex_cache.txt"
exit 0
