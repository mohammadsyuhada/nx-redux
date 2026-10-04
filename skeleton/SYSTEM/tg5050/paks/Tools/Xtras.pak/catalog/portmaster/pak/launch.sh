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
#     first_run / self-update overwrite them): control.txt (plus the
#     exFAT `ln -s` fallback and curl stand-in port scripts get),
#     device_info.txt (TrimUI model, Brick Pro sticks and the trimui
#     capability on the 2026.09.19+ probe), platform.py
#     (paths + portmaster_install disabled), mod_TrimUI.txt (HOME)
#   - pugwash loop: honours .pugwash-reboot, retries once when a fresh
#     pylibs extraction crashed it before the patches landed
#   - pad map while pugwash runs is the opposite of the Button layout
#     setting (its XBOX FIXER swaps A/B and X/Y on TrimUI), the user's
#     setting restored afterwards
#   - post-run: fix installed port scripts, recreate
#     busybox wrappers, sync cover art to .media/, drop the launcher's list
#     caches
# Busybox sh + busybox/GNU sed only. Runs with the pak env from
# MinUI.pak/launch.sh (SDCARD_PATH, SYSTEM_PATH, LOGS_PATH, ...).

# --patch-only (ports_launch.sh, when PortMaster's install or self-update left its stock control.txt): apply the
# NxRedux patches and exit - no GUI, splash or CPU profile, the output going to the caller's log.
PATCH_ONLY=0
[ "${1:-}" = "--patch-only" ] && PATCH_ONLY=1
if [ "$PATCH_ONLY" = 0 ]; then
    rm -f "$LOGS_PATH/portmaster.txt"
    exec >"$LOGS_PATH/portmaster.txt" 2>&1
fi
echo "$0 $*"

PM_DIR="$SDCARD_PATH/Emus/shared/PortMaster"
PM_FILES="$PM_DIR/files"
PORTS_ROM_DIR="$SDCARD_PATH/Roms/Ports (PORTS)"
XTRAS_STATE_DIR="$SHARED_USERDATA_PATH/xtras"
PY="$PM_DIR/bin/python3"
PP="$PM_DIR/pylibs/harbourmaster/platform.py"
DI="$PM_DIR/device_info.txt"

splash() { # $1 = text, $2 = extra show2 args
    killall -9 show2.elf >/dev/null 2>&1
    # shellcheck disable=SC2086
    show2.elf --mode=progress --image=/dev/null --text="$1" --progress=-1 \
        --texty=45 --progressy=55 --fontsize=28 $2 &
}

if [ ! -f "$PM_DIR/pugwash" ]; then
    [ "$PATCH_ONLY" = 1 ] && exit 0
    echo "PortMaster runtime not found at $PM_DIR"
    splash "PortMaster is not installed. Install it from Xtras." "--timeout=4"
    wait
    exit 0
fi

if [ "$PATCH_ONLY" = 0 ]; then
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
fi

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
    cat >> "$PM_DIR/control.txt" <<'EOF'

# ---- NX Redux: exFAT/FAT32 compat for port scripts ----
# HOME and GAMEDIR live on the card, which can hold no symlinks. A port
# script's `ln -s` that fails there becomes what upstream's bind_directories
# does: a bind mount of the target onto the link path (folders and files),
# a copy when the mount fails. Links that work (/tmp, /dev) are untouched.
ln() {
  command ln "$@" 2>/dev/null && return 0
  local a sym=0 noderef=0 args=()
  for a in "$@"; do
    case "$a" in
      --symbolic) sym=1 ;;
      --no-dereference|--no-target-directory) noderef=1 ;;
      --*) ;;
      -*) case "$a" in *s*) sym=1 ;; esac; case "$a" in *[nT]*) noderef=1 ;; esac ;;
      *) args+=("$a") ;;
    esac
  done
  [ "$sym" = 1 ] && [ "${#args[@]}" -le 2 ] || { command ln "$@"; return; }
  local target="${args[0]}" link="${args[1]:-.}"
  if [ "$noderef" = 0 ] && [ -d "$link" ] && ! mountpoint -q "$link" 2>/dev/null; then
    link="${link%/}/${target##*/}"
  fi
  case "$target" in /*) ;; *) target="$(dirname -- "$link")/$target" ;; esac
  if [ -d "$target" ]; then
    rm -f "$link" 2>/dev/null
    mkdir -p "$link" && { umount "$link" 2>/dev/null; mount --bind "$target" "$link"; } && return 0
    cp -rf "$target/." "$link/" && return 0
  elif [ -f "$target" ]; then
    [ -d "$link" ] || { umount "$link" 2>/dev/null; touch "$link"; } && mount --bind "$target" "$link" && return 0
    cp -f "$target" "$link" && return 0
  fi
  echo "ln: cannot link $link -> ${args[0]} on this filesystem" >&2
  return 1
}
# curl is not on every firmware (the Brick has an old one in /usr/bin) or in
# PortMaster's bin; PortMaster's wget is (with HTTPS). Covers what port scripts
# use: -o FILE / -O, -I, -L, -s, -S, -f, -k, -C -, -H, -A, -m /
# --connect-timeout, --retry. Without -o or -O the body goes to stdout, as
# with curl; -I prints the response headers there instead.
if ! command -v curl >/dev/null 2>&1; then
curl() {
  # Verify TLS like curl does, whatever the wget build's default; -k turns it off.
  local a c rest out="" remote=0 head=0 next="" urls=() w=(-q --check-certificate=on) url rc=0
  for a in "$@"; do
    case "$next" in
      o) out="$a"; next=""; continue ;;
      H) w+=("--header=$a"); next=""; continue ;;
      A) w+=("--user-agent=$a"); next=""; continue ;;
      m) w+=("--timeout=$a"); next=""; continue ;;
      r) w+=("--tries=$((a + 1))"); next=""; continue ;;
      skip) next=""; continue ;;
    esac
    case "$a" in
      --output) next=o ;;
      --output=*) out="${a#*=}" ;;
      --remote-name) remote=1 ;;
      --head) head=1 ;;
      --header) next=H ;;
      --user-agent) next=A ;;
      --max-time|--connect-timeout) next=m ;;
      --retry) next=r ;;
      --insecure) w+=(--no-check-certificate) ;;
      --continue-at) w+=(-c); next=skip ;;
      --request|--referer|--user|--retry-delay|--retry-max-time) next=skip ;;
      --*) ;;
      -?*)
        rest="${a#-}"
        while [ -n "$rest" ]; do
          c="${rest:0:1}"; rest="${rest:1}"
          case "$c" in
            o) if [ -n "$rest" ]; then out="$rest"; else next=o; fi; rest="" ;;
            O) remote=1 ;;
            I) head=1 ;;
            k) w+=(--no-check-certificate) ;;
            C) w+=(-c); [ -n "$rest" ] || next=skip; rest="" ;;
            H) if [ -n "$rest" ]; then w+=("--header=$rest"); else next=H; fi; rest="" ;;
            A) if [ -n "$rest" ]; then w+=("--user-agent=$rest"); else next=A; fi; rest="" ;;
            m) if [ -n "$rest" ]; then w+=("--timeout=$rest"); else next=m; fi; rest="" ;;
            X|e|u) [ -n "$rest" ] || next=skip; rest="" ;;
          esac
        done ;;
      *) urls+=("$a") ;;
    esac
  done
  [ -n "${SSL_CERT_FILE:-}" ] && [ -f "$SSL_CERT_FILE" ] && w+=("--ca-certificate=$SSL_CERT_FILE")
  [ "${#urls[@]}" -gt 0 ] || { echo "curl (NX Redux stand-in): no URL" >&2; return 2; }
  for url in "${urls[@]}"; do
    if [ "$head" = 1 ]; then
      # wget -S writes the headers to stderr, two spaces in; -q would hide them.
      command wget "${w[@]:1}" -S --spider "$url" 2>&1 | sed -n 's/^  //p'
      a="${PIPESTATUS[0]}"; [ "$a" = 0 ] || rc="$a"
    elif [ -n "$out" ]; then
      command wget "${w[@]}" -O "$out" "$url" || rc=$?
    elif [ "$remote" = 1 ]; then
      a="${url%%[?#]*}"; command wget "${w[@]}" -O "${a##*/}" "$url" || rc=$?
    else
      command wget "${w[@]}" -O - "$url" || rc=$?
    fi
  done
  return "$rc"
}
fi
EOF
}


patch_platform_py() {
    [ -f "$PP" ] || return 0
    # NxRedux names the Ports folder differently from what PortMaster
    # hardcodes. PlatformTrimUI.portmaster_install (first run, self-update)
    # copies the stock TrimUI control.txt over ours and into /roms/ports/
    # PortMaster on the internal storage, drops its own launch.sh into
    # Emus/shared and empties tasksetter: none of that fits NX Redux. A
    # self-update still runs it once with the fresh code, before this patch
    # is back; ports_launch.sh repairs control.txt before the next port.
    sed -i "s|/mnt/SDCARD/Roms/PORTS|$PORTS_ROM_DIR|g;s|/mnt/SDCARD/Imgs/PORTS|$PORTS_ROM_DIR/.media|g" "$PP"
    "$PY" "$PM_DIR/disable_python_function.py" "$PP" portmaster_install 2>/dev/null
    rm -rf "$PM_DIR/pylibs/harbourmaster/__pycache__"
}

patch_mod_trimui() {
    [ -f "$PM_DIR/mod_TrimUI.txt" ] || return 0
    sed -i "s|/mnt/SDCARD/Data/home|$SHARED_USERDATA_PATH/PORTS-portmaster|g" "$PM_DIR/mod_TrimUI.txt"
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

# The 2026.09.19+ probe no longer lists the firmware among the capabilities,
# so a port its porter marked "!trimui" (known broken on TrimUI) showed up
# here anyway. Add "trimui" back right before the capability list is
# exported. The TrimUI probe cache is dropped only when the script is patched.
patch_device_info_trimui_cap() { # $1 = device_info.txt
    [ -d /usr/trimui ] || return 0
    grep -q 'NX Redux: TrimUI capability' "$1" 2>/dev/null && return 0
    awk '
        !done && /^export DEVICE_CAPABILITIES=/ {
            print "# NX Redux: TrimUI capability (ports opt out with !trimui)"
            print "[ \"$CFW_NAME\" = \"TrimUI\" ] && CAPS+=(\"trimui\")"
            done = 1
        }
        { print }
    ' "$1" >"$1.nxtmp" && mv -f "$1.nxtmp" "$1"
    grep -q 'NX Redux: TrimUI capability' "$1" && rm -f "${1%/*}"/device_info_trimui_*.env
}

# pugwash runs device_info.txt itself when no probe cache exists, but gives
# it 5 s; the 2026.09.19+ probe takes ~17 s on the Smart Pro S, so pugwash
# gave up and ran as "unknown" (no Xbox A/B fix). Write the cache here first;
# later runs read it in well under a second.
warm_device_info_cache() {
    [ -d /usr/trimui ] || return 0
    grep -q 'NX Redux: TrimUI firmware' "$DI" 2>/dev/null || return 0
    ls "$PM_DIR"/device_info_trimui_*.env >/dev/null 2>&1 && return 0
    (cd "$PM_DIR" && controlfolder="$PM_DIR" NO_SDL_RESOLUTION=1 \
        "$PM_DIR/bin/bash" "$DI" -f >/dev/null 2>&1)
}

# NX Redux used to ship replacement launch scripts in patchedScripts/ and copy
# them over installed ports after every PortMaster run. control.txt's ln
# fallback covers what they fixed, and the copies overwrote newer upstream
# scripts, so the folder goes.
retire_patched_scripts() {
    rm -rf "$PM_DIR/patchedScripts"
}

apply_patches() {
    patch_device_info_trimui "$DI"
    patch_device_info_trimui_cap "$DI"
    patch_control_txt
    patch_platform_py
    patch_mod_trimui
    warm_device_info_cache
    retire_patched_scripts
}

if [ "$PATCH_ONLY" = 1 ]; then
    apply_patches
    echo "NxRedux patches applied"
    exit 0
fi

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
        [ -e "$media/$base.png" ] && continue
        # Ports that ship no art (third-party sources such as NextOS): the
        # screenshot PortMaster cached for the catalog, config/images_<source>/
        # <zip name>.screenshot.<ext>.
        zip="$(grep -o '"name": *"[^"]*\.zip"' "$dir/port.json" | head -n 1 | sed 's/.*"\([^"]*\)\.zip"$/\1/')"
        [ -n "$zip" ] || zip="$(basename "$dir")"
        shot="$(find "$PM_DIR/config" -path '*/images_*/*' -iname "$zip.screenshot.*" 2>/dev/null | head -n 1)"
        [ -n "$shot" ] && cp -f "$shot" "$media/$base.png"
    done
}

# ---- run pugwash ----------------------------------------------------------

# pugwash's TrimUI XBOX FIXER swaps A/B and X/Y on top of the pad map, so it
# gets the opposite map: Xbox map -> right button confirms (Nintendo),
# Nintendo map -> bottom button confirms (Xbox).
. "$SYSTEM_PATH/bin/nx_button_layout.sh"
if [ "$NX_BUTTON_LAYOUT" = "xbox" ]; then
    PUGWASH_LAYOUT=nintendo
else
    PUGWASH_LAYOUT=xbox
fi

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
    # Patches and pad map before EVERY run: first_run (pylibs just
    # extracted) and a self-update replace platform.py, device_info.txt and
    # gamecontrollerdb.txt, and pugwash restarts itself straight after.
    apply_patches
    set_controller_layout "$PUGWASH_LAYOUT"
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
set_controller_layout "$NX_BUTTON_LAYOUT"
fix_port_scripts
create_busybox_wrappers
sync_port_artwork
rm -f "$USERDATA_PATH/emulist_cache.txt" "$USERDATA_PATH/romindex_cache.txt"
exit 0
