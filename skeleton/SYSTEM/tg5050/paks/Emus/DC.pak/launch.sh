#!/bin/sh
EMU_TAG=$(basename "$(dirname "$0")" .pak)
PAK_DIR="$(dirname "$0")"
EMU_DIR="$SDCARD_PATH/Emus/shared/flycast"
ROM="$1"

mkdir -p "$SAVES_PATH/$EMU_TAG"

# BIG cluster: cpu4-5 (Cortex-A55, 2160 MHz)
echo 1 >/sys/devices/system/cpu/cpu5/online 2>/dev/null
echo performance >/sys/devices/system/cpu/cpu4/cpufreq/scaling_governor
echo 2160000 >/sys/devices/system/cpu/cpu4/cpufreq/scaling_max_freq
echo 1992000 >/sys/devices/system/cpu/cpu4/cpufreq/scaling_min_freq

# GPU: lock to performance for flycast rendering
echo performance >/sys/devices/platform/soc@3000000/1800000.gpu/devfreq/1800000.gpu/governor 2>/dev/null

# Config-dir resolution + emu.cfg seeding shared with options.sh
. "$PAK_DIR/nx_paths.sh"

# Install the curated controller mapping if not already present. Deliberately
# NOT gated by .initialized: that marker only tracks the emu.cfg seed, so an
# existing install upgraded from an older pak version (already .initialized,
# but never had a mapping shipped) would otherwise never receive it. Install
# unconditionally-if-absent instead, every launch, and never overwrite a
# mapping the user has since customized via flycast's own Controls UI.
if [ ! -f "$DEVICE_CONFIG_DIR/flycast/mappings/SDL_Xbox 360 Controller.cfg" ]; then
    cp "$PAK_DIR/SDL_Xbox 360 Controller.cfg" "$DEVICE_CONFIG_DIR/flycast/mappings/" 2>/dev/null || true
fi

# Flycast resolves config to $XDG_CONFIG_HOME/flycast/ and data (BIOS search,
# VMUs, save states) to $XDG_DATA_HOME/flycast/ (core/linux-dist/main.cpp).
export HOME="$USERDATA_PATH"
export XDG_CONFIG_HOME="$DEVICE_CONFIG_DIR"
export XDG_DATA_HOME="$USERDATA_DIR/data"
export FLYCAST_BIOS_PATH="$SDCARD_PATH/Bios/DC"
export LD_LIBRARY_PATH="$PAK_DIR:$EMU_DIR:$SDCARD_PATH/.system/lib:/usr/trimui/lib:$LD_LIBRARY_PATH"

# BIOS: prefer real BIOS when present, HLE (Reios) otherwise. Never block launch.
# dc_flash.bin is not required -- flycast auto-creates it when missing
# (core/hw/flashrom/nvmem.cpp); only dc_boot.bin gates real-BIOS mode.
if [ -f "$SDCARD_PATH/Bios/DC/dc_boot.bin" ]; then
    sed -i 's/^UseReios =.*/UseReios = no/' "$EMU_CFG"
else
    sed -i 's/^UseReios =.*/UseReios = yes/' "$EMU_CFG"
fi

# RetroAchievements: sync credentials from the NXRedux RA login (if any).
RA_USER=$(sed -n 's/^raUsername=//p' "$SHARED_USERDATA_PATH/minuisettings.txt" 2>/dev/null)
RA_TOKEN=$(sed -n 's/^raToken=//p' "$SHARED_USERDATA_PATH/minuisettings.txt" 2>/dev/null)
if [ -n "$RA_USER" ] && [ -n "$RA_TOKEN" ]; then
    sed -i "/^\[achievements\]/,/^\[/{s/^UserName =.*/UserName = $RA_USER/;s/^Token =.*/Token = $RA_TOKEN/;}" "$EMU_CFG"
else
    sed -i "/^\[achievements\]/,/^\[/{s/^Enabled =.*/Enabled = no/;}" "$EMU_CFG"
fi

# Overlay menu config
export EMU_OVERLAY_JSON="$EMU_DIR/overlay_settings.json"
export EMU_OVERLAY_INI="$EMU_CFG"
export EMU_OVERLAY_GAME="$(basename "$ROM" | sed 's/\.[^.]*$//')"
FONT_FILE=$(ls "$SDCARD_PATH/.system/res/"*.ttf 2>/dev/null | head -1)
export EMU_OVERLAY_FONT="${FONT_FILE:-$SDCARD_PATH/.system/res/font.ttf}"
export EMU_OVERLAY_RES="$SDCARD_PATH/.system/res"
MINUI_DIR="$SHARED_USERDATA_PATH/.minui/$EMU_TAG"
mkdir -p "$MINUI_DIR"
export EMU_OVERLAY_SCREENSHOT_DIR="$MINUI_DIR"
export EMU_OVERLAY_ROMFILE="$(basename "$ROM")"

# Mute speaker before launch to prevent audio pop, then unmute after init
echo 1 > /sys/class/speaker/mute 2>/dev/null || true
(sleep 5; echo 0 > /sys/class/speaker/mute 2>/dev/null; syncsettings.elf) &
SYNC_PID=$!

# Start power button sleep/poweroff handler
sleepmon.elf &

# Pick the device-open rate for the current audio sink (audiomon publishes
# /tmp/nx_audio_sink). Exact match on the native rate wins; otherwise the
# sink's preferred (first-listed) rate; 48000 when the file is absent.
nx_pick_audio_rate() {
    NATIVE="$1"
    RATE=48000
    if [ -f /tmp/nx_audio_sink ]; then
        RATES=$(sed -n 's/^rates=//p' /tmp/nx_audio_sink)
        for R in $RATES; do
            if [ "$R" = "$NATIVE" ]; then
                echo "$NATIVE"
                return
            fi
        done
        FIRST=${RATES%% *}
        [ -n "$FIRST" ] && RATE=$FIRST
    fi
    echo "$RATE"
}

# Dreamcast AICA is natively 44.1 kHz
export NX_AUDIO_RATE=$(nx_pick_audio_rate 44100)

# --- Netplay (pre-launch wizard) ---------------------------------------
# Netplay runs only when NXRedux wrote /tmp/netplay_launch (Y / "Launch
# with Netplay" on a capable ROM). netplay.elf owns all setup UI: role,
# hotspot/WiFi, peer discovery, and the rsync save sync (host serves its
# VMU/flash read-only on TCP 18731; the client pulls into netplay-data).
# Every netplay value below is passed as a VIRTUAL -config: flycast's
# get_entry() prefers virtual values and ConfigFile::save() never writes
# them, so emu.cfg is untouched by a netplay run. GGPODelay remains a
# real overlay setting that flycast reads from emu.cfg itself; DCNet is
# forced off among the virtual values below, so its overlay/per-game
# value only ever reaches a normal launch.
# Design: docs/superpowers/specs/2026-07-29-netplay-prelaunch-wizard-design.md

# Read one key from one section of emu.cfg (empty when absent).
nx_cfg_get() {
    sed -n "/^\[$1\]/,/^\[/s/^$2 = *//p" "$EMU_CFG" | head -1
}

# Set one key in one section of emu.cfg, creating the section and/or the
# key when missing. flycast drops keys left at their default when it
# rewrites the file on quit, so [input] and server= are routinely absent
# from a live cfg -- never assume either exists.
nx_cfg_set() {
    grep -q "^\[$1\]$" "$EMU_CFG" || printf '\n[%s]\n' "$1" >> "$EMU_CFG"
    sed -n "/^\[$1\]/,/^\[/p" "$EMU_CFG" | grep -q "^$2 =" || \
        sed -i "s/^\[$1\]\$/[$1]\n$2 =/" "$EMU_CFG"
    sed -i "/^\[$1\]/,/^\[/s|^$2 =.*|$2 = $3|" "$EMU_CFG"
}

# Migration guard (idempotent): revert the two keys the old overlay-toggle
# flow persisted that would still CHANGE a plain launch on an upgraded
# card -- GGPO (would start netplay unasked) and device2 (would plug a
# phantom pad into DC port B). This is deliberately NOT a full netplay-state
# wipe: a stale `server = <ip>` (and `EnableUPnP = no`) written by a previous
# real session is left in emu.cfg untouched. Both are harmless -- flycast
# reads them only on the GGPO path, and on that path the virtual -config set
# below overrides them anyway, since get_entry() prefers virtual values.
# device2: a hand-set 0 (local two-pad) is indistinguishable from the old
# block's and gets reverted ONCE; the new flow never writes it to disk.
NX_G="$(nx_cfg_get network GGPO)"
if [ "$NX_G" = "yes" ] || [ "$NX_G" = "True" ]; then
    nx_cfg_set network GGPO no
fi
[ "$(nx_cfg_get input device2)" = "0" ] && nx_cfg_set input device2 10

# Both early exits below leave the script BEFORE `wait $EMU_PID`, skipping
# the teardown at the bottom -- and sleepmon.elf, the unmute subshell
# ($SYNC_PID) and the pre-launch mute are all started ABOVE this block. An
# orphaned sleepmon.elf left running in the game list keeps its own
# long-press power handler armed (killall -TERM nextui.elf, then an
# unconditional poweroff), so an early exit MUST undo them itself. The old
# netplay block never exited early -- even its cancel arm fell through to
# flycast, so the teardown at the bottom always ran.
nx_netplay_bail() {
    killall sleepmon.elf 2>/dev/null || true
    kill $SYNC_PID 2>/dev/null || true
    echo 0 > /sys/class/speaker/mute 2>/dev/null || true
    exit 0
}

NETPLAY_ARGS=""
if [ -f /tmp/netplay_launch ]; then
    rm -f /tmp/netplay_launch
    netplay.elf --game "$EMU_OVERLAY_GAME" \
        --serve-dir "$USERDATA_DIR/data/flycast" \
        --fetch-to "$USERDATA_DIR/netplay-data/flycast" \
        --fetch-files "vmu_save_*.bin,dc_nvmem.bin" \
        &> "$LOGS_PATH/netplay-wizard.txt"
    if [ $? -ne 0 ]; then
        # cancelled or failed: back to the game list, never a peerless
        # GGPO launch (single-player is one A press away)
        nx_netplay_bail
    fi
    # Defensive: exit 0 is the wizard's "start with netplay" contract and it
    # unlinks a half-written session before failing, so a missing file here
    # is an anomaly -- but sourcing an absent file is FATAL in ash, which
    # would take the whole launch down. Bail to the game list instead.
    [ -f /tmp/netplay_session ] || nx_netplay_bail
    . /tmp/netplay_session
    if [ "$NETPLAY_ROLE" = "host" ]; then
        # host plays on its REAL VMU/flash ("host brings the memory
        # card"); keep a one-deep pre-session copy for manual recovery,
        # overwritten every hosted session
        mkdir -p "$USERDATA_DIR/netplay-backup"
        cp -f "$USERDATA_DIR/data/flycast"/vmu_save_*.bin \
              "$USERDATA_DIR/data/flycast/dc_nvmem.bin" \
              "$USERDATA_DIR/netplay-backup/" 2>/dev/null
        NX_AS_SERVER=yes
    else
        # client plays on the borrowed copy; its own saves are untouched
        mkdir -p "$USERDATA_DIR/netplay-data/flycast"
        export XDG_DATA_HOME="$USERDATA_DIR/netplay-data"
        NX_AS_SERVER=no
    fi
    # device2=0 plugs a pad into DC port B on BOTH sides (GGPO drives
    # port B as player 2 -- remote on the host, LOCAL on the client);
    # EnableUPnP=no stops ggpo.cpp:801-804 punching a router mapping.
    # DCNet=no is forced: DCNet routes the emulated modem to an external
    # cloud service, an input the GGPO lockstep never synchronizes, so a
    # peer's modem traffic desyncs the session. A per-game or global
    # DCNet=yes IS a repeated key here and NETPLAY_ARGS comes last, so it
    # loses on a netplay launch and still applies to a normal one.
    NETPLAY_ARGS="-config network:GGPO=yes -config network:ActAsServer=$NX_AS_SERVER -config network:server=$NETPLAY_PEER_IP -config network:EnableUPnP=no -config input:device2=0 -config network:DCNet=no"
fi
# --- end netplay -------------------------------------------------------

# --- Per-game option overrides ------------------------------------------
# Written by options.elf ("Emulator Options" in the game list's context
# menu). Each entry becomes a VIRTUAL -config (never persisted by flycast),
# so emu.cfg stays the device-global config and flycast's wholesale config
# rewriting can never bake per-game values into it. Values are ints/bools
# and never contain whitespace, so word-splitting GAME_ARGS is safe.
# A malformed or unreadable file yields no args: the game still launches.
GAME_ARGS=""
NX_ROM_BASE="$(nx_rom_base "$ROM")"
NX_GAME_CFG="$DEVICE_CONFIG_DIR/games/$NX_ROM_BASE.cfg"
if [ -f "$NX_GAME_CFG" ]; then
    GAME_ARGS=$(awk -F' = ' '
        /^\[.*\]$/ { sec = substr($0, 2, length($0) - 2); next }
        NF == 2 && sec != "" { printf "-config %s:%s=%s ", sec, $1, $2 }
    ' "$NX_GAME_CFG" 2>/dev/null)
fi
# --- end per-game overrides ---------------------------------------------

cd "$PAK_DIR"
# $GAME_ARGS / $NETPLAY_ARGS are deliberately UNQUOTED: word-split argument
# lists (-config pairs) whose values contain no whitespace, empty on normal
# launches. NETPLAY_ARGS comes last so its network:* values win over any
# per-game override on a netplay launch: flycast's LAST -config wins for a
# repeated section:key -- ParseCommandLine() walks argv left-to-right
# (core/cfg/cl.cpp) and every -config lands in cfgSetVirtual() ->
# ConfigSection::set(), a plain std::map assignment that overwrites
# (core/cfg/ini.cpp).
./flycast $GAME_ARGS $NETPLAY_ARGS "$ROM" &> "$LOGS_PATH/$EMU_TAG.txt" &
EMU_PID=$!

# Thread pinning (dual cluster). Evidence-based (see task-11 report): tg5040
# testing showed the SH4/emu thread ("Flycast-emu") measured only ~57% utime
# of a single floating core (spread across 4 shared cores by the scheduler)
# but jumped to ~96-100% of a dedicated core once pinned - it was being
# diluted/contended, not genuinely idle - and pinning cut the real audio
# thread's ("SDLAudioP2") nonvoluntary context switches by ~4-8x (measured
# ~65-118 per 10s floating -> ~13-28 per 10s pinned). Later confirmed on a
# real Smart Pro S (tg5050) too - dual-cluster pinning applies cleanly there
# as well, matching the tg5040 finding and the identical N64.pak precedent
# below.
#   Flycast-emu  (SH4 CPU + dynarec)       -> BIG cpu4
#   Flycast-rend (main thread, GL present) -> BIG cpu5
#   audio + helpers                        -> LITTLE cpu0-1
# NOTE: the shared $SHARED_SYSTEM_PATH/bin/taskset this used to fall back to
# was found (task-11, tg5040 testing) to crash with "FATAL: kernel too old"
# - a statically-linked glibc binary's own startup code
# (_dl_non_dynamic_init(), only run for static executables) rejecting the
# real device kernel against a compile-time __LINUX_KERNEL_VERSION floor
# baked in from the toolchain's own kernel headers, not a toolchain-target
# mismatch and NOT the .note.ABI-tag (this repo's own dynamic binaries
# carry a "for GNU/Linux 4.19.0" ABI-tag note and run fine on the Brick's
# 4.9 kernel, so that note itself isn't what's enforced) - a rebuild with
# tg5040's own toolchain still aborted identically. Fixed by dropping
# -static in workspace/all/taskset/Makefile and shipping a rebuilt tg5050
# binary at skeleton/SYSTEM/tg5050/bin/taskset, which this launch.sh's own
# PATH ordering ($SYSTEM_PATH/bin before $SHARED_SYSTEM_PATH/bin) picks up
# automatically. Confirmed working on real Smart Pro S (tg5050) hardware:
# no "kernel too old" crash, and pinning applies as expected.
#
# Two passes: Flycast-emu (the SH4 thread) doesn't spawn until the game
# actually starts, and a real-BIOS boot can take longer than a single fixed
# sleep, so a one-shot scan at the same point every launch can miss it.
# Both calls below already tolerate a not-yet-spawned thread (no comm
# match) and a still-broken taskset (2>/dev/null) safely, so re-running the
# same scan is harmless if the first pass already caught everything.
pin_threads() {
    taskset -p 0x20 "$EMU_PID" 2>/dev/null   # mask 0x20 = cpu5 (main/render thread)
    for TID in $(ls /proc/$EMU_PID/task/ 2>/dev/null); do
        [ "$TID" = "$EMU_PID" ] && continue
        TNAME=$(cat /proc/$EMU_PID/task/$TID/comm 2>/dev/null)
        case "$TNAME" in
            Flycast-emu)
                taskset -p 0x10 "$TID" 2>/dev/null ;;  # mask 0x10 = cpu4
            Flycast-rend|SDLAudioP2|SDLHotplug*|SDLTimer|mali-*)
                taskset -p 0x3 "$TID" 2>/dev/null ;;   # mask 0x3 = cpu0-1
        esac
    done
}

# First pass: catches Flycast-rend and most helpers.
sleep 4
pin_threads

# Second pass: catches Flycast-emu and any other late-spawning thread that
# missed the first pass (e.g. a slow real-BIOS boot). Wait up to 8 more
# seconds for it to show up, but poll liveness every second and stop early
# (skipping the second pass entirely) if flycast has already exited - an
# unconditional sleep here would otherwise delay cleanup by up to ~12s on
# a quick quit or crash.
for _ in 1 2 3 4 5 6 7 8; do
    kill -0 "$EMU_PID" 2>/dev/null || break
    sleep 1
done
kill -0 "$EMU_PID" 2>/dev/null && pin_threads

wait $EMU_PID
killall sleepmon.elf 2>/dev/null || true
kill $SYNC_PID 2>/dev/null || true
# Netplay teardown: the session file is the only marker that this run went
# through the wizard, so its presence gates the whole thing. --cleanup stops
# the host's rsyncd, tears down a hotspot AP, restores the previous WiFi
# network, and removes /tmp/netplay_session. Harmless on a plain launch.
[ -f /tmp/netplay_session ] && netplay.elf --cleanup >> "$LOGS_PATH/netplay-wizard.txt" 2>&1
echo 0 > /sys/class/speaker/mute 2>/dev/null || true
