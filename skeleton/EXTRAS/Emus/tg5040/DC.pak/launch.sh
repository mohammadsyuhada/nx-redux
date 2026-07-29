#!/bin/sh
EMU_TAG=$(basename "$(dirname "$0")" .pak)
PAK_DIR="$(dirname "$0")"
EMU_DIR="$SDCARD_PATH/Emus/shared/flycast"
ROM="$1"

mkdir -p "$SAVES_PATH/$EMU_TAG"

# Single cluster: cpu0-3 (Cortex-A53, max 2000 MHz)
echo 1 >/sys/devices/system/cpu/cpu1/online 2>/dev/null
echo 1 >/sys/devices/system/cpu/cpu2/online 2>/dev/null
echo 1 >/sys/devices/system/cpu/cpu3/online 2>/dev/null
echo performance >/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
echo 2000000 >/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq
echo 1608000 >/sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq

# User data (config per device, VMU + save states shared across devices)
USERDATA_DIR="$SHARED_USERDATA_PATH/DC-flycast"
if [ "$DEVICE" = "brick" ]; then
    DEVICE_CONFIG_DIR="$USERDATA_DIR/config/tg5040-brick"
    DEVICE_DEFAULT_CFG="$PAK_DIR/default-brick.cfg"
elif [ "$DEVICE" = "brickpro" ]; then
    DEVICE_CONFIG_DIR="$USERDATA_DIR/config/tg5040-brickpro"
    DEVICE_DEFAULT_CFG="$PAK_DIR/default-brickpro.cfg"
else
    DEVICE_CONFIG_DIR="$USERDATA_DIR/config/tg5040-smart-pro"
    DEVICE_DEFAULT_CFG="$PAK_DIR/default-smartpro.cfg"
fi
mkdir -p "$DEVICE_CONFIG_DIR/flycast/mappings" "$USERDATA_DIR/data/flycast"

# First run: copy device-specific defaults
if [ ! -f "$DEVICE_CONFIG_DIR/.initialized" ]; then
    cp "$DEVICE_DEFAULT_CFG" "$DEVICE_CONFIG_DIR/flycast/emu.cfg"
    touch "$DEVICE_CONFIG_DIR/.initialized"
fi
EMU_CFG="$DEVICE_CONFIG_DIR/flycast/emu.cfg"

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
export LD_LIBRARY_PATH="$PAK_DIR:$EMU_DIR:$SDCARD_PATH/.system/tg5040/lib:/usr/trimui/lib:$LD_LIBRARY_PATH"

# BIOS: prefer real BIOS when present, HLE (Reios) otherwise. Never block launch.
# dc_flash.bin is not required -- flycast auto-creates it when missing
# (core/hw/flashrom/nvmem.cpp); only dc_boot.bin gates real-BIOS mode.
if [ -f "$SDCARD_PATH/Bios/DC/dc_boot.bin" ]; then
    sed -i 's/^UseReios =.*/UseReios = no/' "$EMU_CFG"
else
    sed -i 's/^UseReios =.*/UseReios = yes/' "$EMU_CFG"
fi

# RetroAchievements: sync credentials from the NextUI RA login (if any).
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

# --- Netplay auto-sync + auto-discovery (GGPO) -------------------------
# Runs only when [network] GGPO = yes in emu.cfg (the overlay's Netplay
# section). BOTH devices serve a role-tagged netplay-info on TCP 19714
# and BOTH scan the LAN for a peer running the same ROM in the OPPOSITE
# role, because GGPO is peer-to-peer: each side must know the other's
# address (ggpo.cpp:579-597 -- an empty server= makes flycast target
# loopback on localPort ^ 1: 19712 from the host, 19713 from the client,
# then wait forever on "Starting Network", which has no timeout, only a
# Cancel button). v1 had the host learn the client's IP
# passively from its own access log inside a pre-launch window; a client
# that was late, absent, or launched first left the host hung. Scanning
# on both sides makes launch order and timing irrelevant.
#   host   - plays on its REAL VMU/flash and also serves a tar of exactly
#            the GGPO-hashed files (vmu_save_*.bin + dc_nvmem.bin, see
#            ggpo.cpp:537), so whoever hosts contributes their own saves
#   client - pulls that tar into an isolated netplay-data dir and plays
#            on the borrowed copy; its own saves are never touched
# Every failure path falls through to a normal launch ("fail-open").
# Design: docs/superpowers/specs/2026-07-28-netplay-state-sync-design.md
NETPLAY_PORT=19714
NETPLAY_SRV=/tmp/nx_netplay
NETPLAY_LOG=/tmp/nx_netplay.log
NETPLAY_DEADLINE=90
NX_HTTPD_PID=""

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

# wget with a watchdog: busybox wget has no timeout flag, so kill it
# ourselves after $2 seconds. Prints fetched content (empty on failure).
nx_fetch() {
    # Each discovery pass runs a dozen-plus nx_fetch calls CONCURRENTLY,
    # and in POSIX sh `$$` keeps the PARENT shell's pid inside a subshell
    # -- so a pid-derived name is the SAME file in every probe. Measured
    # on-device: 16 concurrent probes against a network with exactly one
    # real peer returned ZERO hits, because one probe's trailing `rm -f`
    # deleted the payload another had just downloaded; the same race also
    # made a dead host report a live host's payload and win the `ls |
    # sort | head -1` selection. Both present as "stuck looking for
    # player 2", and this is the other half of the first pair test's
    # failure: fixing it ALONE would still have been swamped by the ~250
    # concurrent probes the un-deduplicated neighbour list produced (see
    # nx_find_peer). Both fixes are required.
    #
    # The name must be unique per invocation. mktemp is at
    # /bin/mktemp on both devices; the fallback derives uniqueness from
    # the URL, which is likewise distinct for every concurrent probe.
    NXF_OUT=$(mktemp /tmp/nx_netplay_fetch.XXXXXX 2>/dev/null)
    if [ -z "$NXF_OUT" ]; then
        NXF_OUT=/tmp/nx_netplay_fetch.$(printf '%s' "$1" | tr -c 'a-zA-Z0-9' '_')
        rm -f "$NXF_OUT"
    fi
    wget -q -O "$NXF_OUT" "$1" 2>/dev/null &
    NXF_PID=$!
    NXF_T=0
    while [ "$NXF_T" -lt "$2" ]; do
        kill -0 "$NXF_PID" 2>/dev/null || break
        # Abort promptly on cancel: without this a press during the
        # client's up-to-30s tar download would sit unnoticed until the
        # download finished. Callers already treat an empty result as a
        # failed fetch.
        nx_cancelled && break
        sleep 1
        NXF_T=$((NXF_T+1))
    done
    kill "$NXF_PID" 2>/dev/null
    wait "$NXF_PID" 2>/dev/null
    cat "$NXF_OUT" 2>/dev/null
    rm -f "$NXF_OUT"
}

# --- status display -----------------------------------------------------
# Drives show2.elf (already shipped in $SYSTEM_PATH/bin) in daemon mode so
# the long discovery phase is not a silent black screen. show2 renders via
# SDL2/DRM -- /dev/fb0 is NOT the display path on these devices, so a
# framebuffer-writing helper would draw nothing. Every call here is a
# guarded no-op when show2.elf is missing or its daemon died: discovery and
# launch must never block or fail because of the UI.
NX_UI_FIFO=/tmp/show2.fifo
NX_UI_ON=""

NX_CANCEL_FLAG=/tmp/nx_netplay_cancel
NX_CANCEL_BTN=00          # physical B. Measured on a Brick 2026-07-29:
                          # five presses, all "01 00 01 00" (value 0001,
                          # type 01, number 00). Agrees with this pak's own
                          # SDL_Xbox 360 Controller.cfg (bind0 = 0:btn_b).
NX_CANCEL_PID=""
NX_CANCELLED=""

nx_ui_start() {
    # Resolved via PATH ($SYSTEM_PATH/bin), exactly like this script's own
    # bare sleepmon.elf / syncsettings.elf / taskset calls. Do NOT build a
    # $SDCARD_PATH/.system/<platform>/bin path here: that differs per device
    # and this block must stay byte-identical across both launch.sh files.
    command -v show2.elf >/dev/null 2>&1 || return 0
    rm -f "$NX_UI_FIFO"
    # show2 needs libSDL2 from $SDCARD_PATH/.system/<platform>/lib, which
    # launch.sh already put on LD_LIBRARY_PATH above; without it show2
    # aborts with "libSDL2-2.0.so.0: cannot open shared object file".
    # --image is mandatory but we have no netplay artwork; /dev/null is the
    # repo's existing "no logo" idiom (see PortMaster's progressor).
    # --progress=-1 selects show2's indeterminate marquee (show2.cpp:424),
    # which is what the install boot.sh uses for exactly this kind of
    # unknown-length wait -- an empty 0% bar would just look stalled.
    if [ -n "$2" ]; then
        show2.elf --mode=daemon --image=/dev/null --bgcolor=0x000000 \
            --progress=-1 --text="$1" --hint="$2" >/dev/null 2>&1 &
    else
        show2.elf --mode=daemon --image=/dev/null --bgcolor=0x000000 \
            --progress=-1 --text="$1" >/dev/null 2>&1 &
    fi
    NX_UI_ON=$!
    # Daemon needs ~3-4s (SDL2 + DRM init) before it paints; do not wait
    # for it here -- the network work below overlaps that cost.
}

# nx_ui "<message>" [progress 0-100]
# Opening a FIFO for writing BLOCKS until a reader attaches, so a daemon
# that died while leaving its FIFO node behind would hang the launch
# forever on the next status update -- the exact failure this feature
# exists to prevent. Two defences: skip when the daemon PID is gone, and
# do the write in a background subshell so even a lost race can only
# strand that subshell, never the script.
nx_ui() {
    [ -n "$NX_UI_ON" ] || return 0
    kill -0 "$NX_UI_ON" 2>/dev/null || return 0
    # show2 creates the FIFO only after its SDL2/DRM init: mkfifo is the
    # first statement of runDaemonMode(), which runs after initialize()
    # returns (~3-4s). A plain [ -p ] test here would therefore DROP every
    # early message -- and those are the ones that matter, because the next
    # update can be up to NETPLAY_DEADLINE seconds later. So wait for the
    # node, but do it INSIDE the background subshell so the script itself
    # never stalls. Ordering between messages queued during that init
    # window is not guaranteed; they all land within a second of each
    # other and only the last is visible, which is acceptable here.
    ( NXU_T=0
      while [ ! -p "$NX_UI_FIFO" ] && [ "$NXU_T" -lt 10 ]; do
          kill -0 "$NX_UI_ON" 2>/dev/null || exit 0
          sleep 1
          NXU_T=$((NXU_T+1))
      done
      [ -p "$NX_UI_FIFO" ] || exit 0
      printf 'TEXT:%s\n' "$1" > "$NX_UI_FIFO" 2>/dev/null
      [ -n "$2" ] && printf 'PROGRESS:%s\n' "$2" > "$NX_UI_FIFO" 2>/dev/null
      : ) &
    return 0
}

nx_ui_stop() {
    [ -n "$NX_UI_ON" ] || return 0
    # Same blocking hazard as nx_ui, and worse here: this is the LAST
    # statement before flycast launches, so a dead daemon that left its
    # FIFO node behind would hang the launch at the finish line. Guard on
    # liveness and background the write. killall is the real cleanup --
    # it cannot block -- and the QUIT is only the graceful path.
    if kill -0 "$NX_UI_ON" 2>/dev/null && [ -p "$NX_UI_FIFO" ]; then
        ( printf 'QUIT\n' > "$NX_UI_FIFO" 2>/dev/null ) &
    fi
    # Belt-and-braces: nothing may hold the display against flycast.
    # MUST be -9. Measured on a Brick 2026-07-29: after a plain SIGTERM the
    # daemon was still alive 6 s later; SIGKILL ended it instantly. show2
    # installs a handler for SIGINT only (show2.cpp:623) and SDL_Init traps
    # SIGTERM into an SDL_QUIT event that show2 never pumps (it has no
    # SDL_PollEvent at all), so SIGTERM is swallowed. MinUI.pak's own
    # launch.sh:228 has always used -9 on this same binary.
    killall -9 show2.elf 2>/dev/null
    # Leave no stale node for the next launch to trip over.
    rm -f "$NX_UI_FIFO" 2>/dev/null
    NX_UI_ON=""
    return 0
}

# --- cancel (press B to skip netplay) ----------------------------------
# True once the user has asked to skip netplay.
nx_cancelled() { [ -f "$NX_CANCEL_FLAG" ]; }

# Watch the gamepad for a B press and drop a flag file.
#
# js0 delivers 8-byte events: u32 time, s16 value, u8 type, u8 number. A
# real press is type 01 with a non-zero value; opening the device emits an
# init burst (type 81/82, value 0) and releases carry value 0, so neither
# can trigger a cancel, and stick movement (type 02) cannot either.
#
# Two shapes were tried on-device and BOTH failed; do not "simplify" back
# to either:
#   dd if=js0 | hexdump | while read   -- hexdump BLOCK-BUFFERS to a pipe,
#     so presses sit unseen until ~4KB (~170 events) accumulates. A 45s
#     capture recorded zero presses while buttons were being pressed.
#   dd if=js0 count=1 inside a loop    -- re-opens the device every pass,
#     replaying the init burst each time, and spins.
# What works: open ONCE on fd 3, then one short-lived dd/hexdump per event
# so hexdump exits (and therefore flushes) every time. Verified: presses
# appear immediately and the loop blocks without spinning when idle.
#
# od is not on this firmware; hexdump is. Backgrounded so it can never
# delay the launch. If js0 is unreadable the subshell exits at once and
# cancel is simply unavailable -- every other path is unchanged.
nx_cancel_arm() {
    rm -f "$NX_CANCEL_FLAG"
    [ -r /dev/input/js0 ] || return 0
    (
        exec 3< /dev/input/js0 || exit 0
        while :; do
            NXB_EV=$(dd bs=8 count=1 <&3 2>/dev/null | hexdump -v -e '8/1 "%02x " "\n"')
            [ -n "$NXB_EV" ] || exit 0
            set -- $NXB_EV
            [ "$7" = "01" ] || continue
            [ "$8" = "$NX_CANCEL_BTN" ] || continue
            [ "$5$6" = "0000" ] && continue
            : > "$NX_CANCEL_FLAG"
            exit 0
        done
    ) &
    NX_CANCEL_PID=$!
    return 0
}

# Stop watching.
#
# Killing the subshell does NOT reap its children: the blocked dd, the
# command-substitution child and hexdump each keep js0 open. That leak is
# deliberately accepted rather than swept, and the reasoning is worth
# keeping because both obvious sweeps are wrong here:
#
#   - Matching /proc/<pid>/cmdline cannot work. The device is opened with
#     `exec 3< /dev/input/js0` and read with `dd bs=8 count=1 <&3`, so the
#     path is a redirection, never an argument -- busybox ps shows the dd
#     as "dd bs 8 count 1". Verified on-device: an argv sweep finds none of
#     the holders.
#   - Matching open descriptors (readlink /proc/<pid>/fd/*) DOES find them
#     all -- also verified on-device -- but "kill whatever holds js0" is
#     dangerous: keymon.elf opens input devices and runs throughout the
#     game, and killing the launcher is known to power this device off.
#     A cleanup that can kill the launcher is not worth a leak that clears
#     itself.
#
# And it does clear itself: dd was started with count=1, so the first
# controller event after flycast starts makes it read one event and exit,
# hexdump then sees EOF, and the substitution child follows. joydev gives
# each opener an independent event buffer, so nothing is taken from
# flycast in the meantime.
nx_cancel_disarm() {
    [ -n "$NX_CANCEL_PID" ] && kill "$NX_CANCEL_PID" 2>/dev/null
    NX_CANCEL_PID=""
    rm -f "$NX_CANCEL_FLAG"
    return 0
}

# Serve $NETPLAY_SRV on 19714 with whatever this firmware provides:
# busybox httpd (tg5040) or python3 http.server (tg5050, whose busybox
# 1.35 ships no httpd applet). Neither present -> no server, fail open.
# httpd runs without -v, so there is no request log at all: $NETPLAY_LOG
# only ever holds the server's own stdout/stderr (empty on tg5040). It
# still lives OUTSIDE the serve dir so it is never itself served.
# Must run in the parent shell, never inside $( ) -- NX_HTTPD_PID has to
# survive for the cleanup block after wait $EMU_PID.
nx_serve_start() {
    # The pidfile is only removed on a clean exit, so a power-off or a
    # hybrid-sleep leaves a stale one behind and the pid it names may have
    # been recycled onto an unrelated process by the next launch. Blind
    # SIGTERM to a recycled pid is a genuinely bad outcome on this device
    # family, so confirm the pid is actually one of our servers before
    # signalling it: /proc/<pid>/comm reads "httpd" or "python3" (the
    # python path execs, so the subshell is replaced and comm is the
    # interpreter, not sh). The pidfile goes either way -- a pid we
    # decline to kill is not ours to keep tracking.
    #
    # BOTH stages are needed; the digits test alone is not enough. It
    # accepts "0", and `kill 0` signals EVERY process in our process
    # group. That is unreachable only because /proc/0/comm does not
    # exist, so the comm test rejects it -- do not drop that test.
    if [ -f /tmp/nx_netplay.pid ]; then
        NXS_OLD=$(cat /tmp/nx_netplay.pid 2>/dev/null)
        case "$NXS_OLD" in
            ""|*[!0-9]*) ;;
            *)
                # A zero pid passes the all-digits test above, and
                # `kill 0` signals EVERY process in this script's process
                # group -- on this device family that reaches the
                # launcher, and killing the launcher powers the unit off.
                # /proc/0/comm not existing does block it today, but a
                # guard against that blast radius should not rest on one
                # accident. Test arithmetically rather than matching a
                # literal 0: "00" is also zero and slips past a pattern.
                if [ "$NXS_OLD" -gt 0 ]; then
                    case "$(cat "/proc/$NXS_OLD/comm" 2>/dev/null)" in
                        httpd|python3) kill "$NXS_OLD" 2>/dev/null ;;
                    esac
                fi
                ;;
        esac
        rm -f /tmp/nx_netplay.pid
    fi
    if command -v httpd >/dev/null 2>&1; then
        httpd -f -p "$NETPLAY_PORT" -h "$NETPLAY_SRV" > "$NETPLAY_LOG" 2>&1 &
        NX_HTTPD_PID=$!
    elif command -v python3 >/dev/null 2>&1; then
        ( cd "$NETPLAY_SRV" && exec python3 -u -m http.server "$NETPLAY_PORT" ) \
            > "$NETPLAY_LOG" 2>&1 &
        NX_HTTPD_PID=$!
    fi
    [ -n "$NX_HTTPD_PID" ] && echo "$NX_HTTPD_PID" > /tmp/nx_netplay.pid
}

# True when $1 serves a netplay-info for this ROM in role $2. The role
# line is what stops two hosts (or two clients) from pairing, and the
# ROM line stops cross-game pairing.
nx_probe() {
    NXI=$(nx_fetch "http://$1:$NETPLAY_PORT/netplay-info" 3)
    [ "$(printf '%s\n' "$NXI" | sed -n 1p)" = "$EMU_OVERLAY_GAME" ] && \
    [ "$(printf '%s\n' "$NXI" | sed -n 2p)" = "$2" ]
}

# Print the IP of a peer in role $1, or nothing. Tries the stored server=
# first, but only when it is in our current /24 -- after a relocation the
# stored address belongs to the old network and can never answer, so
# probing it just burns ~3s. Then ping-sweeps the /24 and probes every
# neighbour CONCURRENTLY: serial probes cost >=1s each (nx_fetch's poll
# floor), so a LAN with N reachable hosts used to cost ~N seconds per
# pass. Runs inside $( ) -- it must not set anything the caller needs.
nx_find_peer() {
    NXP_WANT=$1
    NXP_MYIP=$(ip -4 addr show wlan0 2>/dev/null | \
        sed -n 's|.*inet \([0-9.]*\)/24 .*|\1|p' | head -1)
    NXP_NET=${NXP_MYIP%.*}
    NXP_STORED=$(nx_cfg_get network server)
    if [ -n "$NXP_STORED" ] && [ -n "$NXP_NET" ] && \
       [ "${NXP_STORED%.*}" = "$NXP_NET" ] && \
       nx_probe "$NXP_STORED" "$NXP_WANT"; then
        echo "$NXP_STORED"
        return
    fi
    NXP_HITS=/tmp/nx_netplay_hits
    NXP_END=$(( $(date +%s) + NETPLAY_DEADLINE ))
    while [ "$(date +%s)" -lt "$NXP_END" ]; do
        nx_cancelled && return
        if [ -n "$NXP_NET" ]; then
            NXP_I=1
            while [ "$NXP_I" -le 254 ]; do
                ping -c1 -W1 "$NXP_NET.$NXP_I" >/dev/null 2>&1 &
                NXP_I=$((NXP_I+1))
            done
            sleep 3
        else
            # No /24 sweep to pace the loop; without this an empty
            # neighbour table would spin the deadline loop hot.
            sleep 1
        fi
        rm -rf "$NXP_HITS"
        mkdir -p "$NXP_HITS"
        # A /24 sweep leaves an ARP entry for EVERY address it probed, and
        # a router that answers ARP for unused addresses turns that into
        # hundreds of entries sharing a handful of MACs. Probing all of
        # them spawns hundreds of concurrent wgets on a 4-core handheld,
        # none finish inside the collect window below, and the peer is
        # never found -- one half of how the first real pair test failed.
        # The other half is nx_fetch's shared temp file (see there): dedup
        # ALONE cannot fix discovery, because that race destroys the
        # result at any concurrency.
        #
        # Deduplicate by MAC: one candidate per physical device. Measured
        # on-device, as one dataset through the two filter stages:
        #   raw `ip neigh`                     259 lines, ~19 MACs
        #   after the IPv4 + valid-lladdr sed  222 lines,  16 MACs
        # so 16 candidates survive, the real peer among them -- phantoms
        # share the router's MAC, real devices each keep their own.
        #
        # awk rather than `sort -u -k1,1` because a busybox built without
        # CONFIG_FEATURE_SORT_BIG has no `-k` at all; where `-k` exists,
        # `sort -u` does honour it (verified on tg5050).
        #
        # Both sed anchors are load-bearing, and they earn their keep on
        # different inputs:
        #   ^\([0-9.]*\)   excludes IPv6, which is checked before the
        #     lladdr group is even consulted. It matters: an IPv6 neighbour
        #     sharing the PEER's MAC would otherwise claim that MAC's
        #     seen[] slot and hide the peer behind an unprobeable candidate.
        #   \{17\}         excludes malformed lladdr values. Without it
        #     "192.168.1.20 dev wlan0 lladdr INCOMPLETE" yields an empty
        #     MAC key and awk then prints an empty candidate.
        for NXP_IP in $(ip neigh 2>/dev/null | \
                sed -n 's/^\([0-9.]*\) .*lladdr \([0-9a-f:]\{17\}\).*/\2 \1/p' | \
                awk '!seen[$1]++ {print $2}'); do
            nx_cancelled && return
            [ "$NXP_IP" = "$NXP_MYIP" ] && continue
            ( nx_probe "$NXP_IP" "$NXP_WANT" && : > "$NXP_HITS/$NXP_IP" ) &
        done
        # One probe's worth of wall clock rather than N probes' -- but note
        # this function runs inside $( ), and a command substitution does
        # not return until every background child it spawned has exited
        # (they inherit the capture pipe). So the caller actually waits
        # max(this sleep, slowest straggler probe). That is bounded: each
        # nx_probe goes through nx_fetch's 3s watchdog. Measure the
        # substitution, not this sleep, when timing a pass.
        sleep 4
        # Sorted so a re-run on an unchanged LAN picks the same peer.
        NXP_FOUND=$(ls "$NXP_HITS" 2>/dev/null | sort | head -1)
        rm -rf "$NXP_HITS"
        if [ -n "$NXP_FOUND" ]; then
            echo "$NXP_FOUND"
            return
        fi
    done
}

nx_netplay_host() {
    NXH_DATA="$USERDATA_DIR/data/flycast"
    # Deliberately no progress value. This call and the -1 one below are
    # both backgrounded and may still be waiting on show2's FIFO during
    # its ~3-4s init, so their arrival order is not guaranteed. A numeric
    # PROGRESS: landing after the -1 would cancel the marquee and freeze
    # the bar for the whole discovery wait. Sending none means the only
    # progress the daemon can see before that wait is -1, which makes this
    # path deterministic and matches the client path.
    nx_ui "Preparing saves..."
    # One-deep crash insurance: the REAL card is live during a netplay
    # session, so snapshot it first (~384 KB, overwritten each session).
    mkdir -p "$USERDATA_DIR/netplay-backup"
    cp -f "$NXH_DATA"/vmu_save_*.bin "$NXH_DATA/dc_nvmem.bin" \
        "$USERDATA_DIR/netplay-backup/" 2>/dev/null
    rm -rf "$NETPLAY_SRV"
    mkdir -p "$NETPLAY_SRV"
    printf '%s\nhost\n' "$EMU_OVERLAY_GAME" > "$NETPLAY_SRV/netplay-info"
    (cd "$NXH_DATA" 2>/dev/null && \
        tar cf "$NETPLAY_SRV/netplay-sync.tar" vmu_save_*.bin dc_nvmem.bin 2>/dev/null)
    nx_serve_start
    nx_cancelled && { NX_CANCELLED=1; return 0; }
    # Computed here in the parent shell: nx_find_peer's own copy lives in
    # a $( ) subshell and would always read back empty at this point.
    NXH_NET=$(ip -4 addr show wlan0 2>/dev/null | \
        sed -n 's|.*inet \([0-9.]*\)/24 .*|\1|p' | head -1)
    if [ -n "$NXH_NET" ]; then NXH_WHERE="${NXH_NET%.*}.x"; else NXH_WHERE="the network"; fi
    nx_ui "Looking for player 2 on $NXH_WHERE..." -1
    NXH_PEER=$(nx_find_peer client)
    if [ -n "$NXH_PEER" ]; then
        nx_cfg_set network server "$NXH_PEER"
        nx_ui "Player 2 found at $NXH_PEER" 90
    else
        nx_ui "No player 2 found - starting anyway" 100
    fi
    # No peer found: fail open on whatever server= already holds.
}

nx_netplay_client() {
    NXC_NPDATA="$USERDATA_DIR/netplay-data/flycast"
    mkdir -p "$NXC_NPDATA"
    nx_ui "Looking for the host..." -1
    # The client serves only its identity -- never any save data.
    rm -rf "$NETPLAY_SRV"
    mkdir -p "$NETPLAY_SRV"
    printf '%s\nclient\n' "$EMU_OVERLAY_GAME" > "$NETPLAY_SRV/netplay-info"
    nx_serve_start
    nx_cancelled && { NX_CANCELLED=1; return 0; }
    NXC_PEER=$(nx_find_peer host)
    if [ -n "$NXC_PEER" ]; then
        nx_cfg_set network server "$NXC_PEER"
        nx_ui "Host found at $NXC_PEER - copying saves..." 60
        nx_fetch "http://$NXC_PEER:$NETPLAY_PORT/netplay-sync.tar" 30 > /tmp/nx_netplay_sync.tar
        # Tar-slip guard: the peer is discovered, not authenticated, and
        # this script runs as root, so accept an archive only when ALL of
        # these hold -- it lists at least one entry (a non-tar payload
        # lists nothing, which would pass every negated test vacuously);
        # every name is a bare expected filename (no slashes, no "..",
        # no absolute paths); every mode column is "-"; and no entry is a
        # link. The explicit link test is NOT redundant with the mode
        # column: busybox tar prints a HARDLINK as "-" (only GNU tar uses
        # "h"), so on this firmware the mode column alone would accept
        # one. Both link kinds render their target as "name -> target".
        # Without this a link named vmu_save_A1.bin could land in
        # netplay-data and flycast would write VMU data through it.
        if [ -s /tmp/nx_netplay_sync.tar ] && \
           tar tf /tmp/nx_netplay_sync.tar 2>/dev/null | grep -q . && \
           ! tar tf /tmp/nx_netplay_sync.tar 2>/dev/null | \
             grep -qv '^vmu_save_[A-D][12]\.bin$\|^dc_nvmem\.bin$' && \
           ! tar tvf /tmp/nx_netplay_sync.tar 2>/dev/null | \
             grep -qv '^-' && \
           ! tar tvf /tmp/nx_netplay_sync.tar 2>/dev/null | \
             grep -q ' -> '; then
            (cd "$NXC_NPDATA" && tar xf /tmp/nx_netplay_sync.tar 2>/dev/null)
        fi
        rm -f /tmp/nx_netplay_sync.tar
    fi
    [ -n "$NXC_PEER" ] || nx_ui "No host found - starting anyway" 100
    # Borrowed-copy isolation: play on the synced copy; the client's real
    # saves under data/flycast are never read or written during netplay.
    #
    # Cancelled means "play this game normally", so do NOT redirect saves
    # to the borrowed netplay-data copy -- with GGPO off there is nothing
    # to isolate, and the user would otherwise be playing single-player on
    # the host's VMU instead of their own. (Their real saves are untouched
    # either way; this is about which set the session actually uses.)
    if nx_cancelled; then
        NX_CANCELLED=1
    else
        export XDG_DATA_HOME="$USERDATA_DIR/netplay-data"
    fi
}

if [ "$(nx_cfg_get network GGPO)" = "yes" ]; then
    nx_ui_start "Starting..." "B = skip netplay"
    nx_cancel_arm
    # Plug a controller into DC port B. GGPO always drives port B as
    # player 2 -- the REMOTE player on the host, the LOCAL player on the
    # client (ggpo.cpp:500,569 make the local player localPlayerNum + 1;
    # ggpo.cpp:643-650 writes inputState[player] straight to the maple
    # port index) -- so BOTH machines need a pad there; that the client's
    # own Start did nothing is exactly this. flycast leaves device2 =
    # MDT_None (10) by default (option.cpp:197-201), so with port B empty
    # those inputs reach no maple device and MvC2's VS mode stays greyed
    # out. Both peers must carry the identical value or the emulated
    # machines differ and desync -- which is why this is set here on both
    # sides, not by hand.
    nx_cfg_set input device2 0
    # Close flycast's router port punching. EnableUPnP defaults to TRUE
    # (option.cpp:172), and ggpo.cpp:801-804 runs miniupnp.Init() +
    # AddPortMapping(19713/UDP) BEFORE the ActAsServer branch, so BOTH
    # roles punch a mapping with an 86400 s lease (miniupnp.cpp). The
    # seeded EnableUPnP = no in the default*.cfg files only reaches FRESH
    # installs (it is gated behind .initialized), so no existing device
    # ever gets it -- setting it here closes it everywhere. No
    # restore-when-off branch is needed: the value is only consulted on
    # the GGPO path.
    nx_cfg_set network EnableUPnP no
    if [ "$(nx_cfg_get network ActAsServer)" = "yes" ]; then
        nx_netplay_host
    else
        nx_netplay_client
    fi
    if nx_cancelled; then
        NX_CANCELLED=1
        # nx_serve_start has already run in both role functions by the
        # time a cancel is noticed, and on the host path the tar holds
        # real VMU + dc_nvmem data. Without this the server keeps
        # offering it on TCP 19714 until `wait $EMU_PID` returns -- i.e.
        # for the whole session the user asked to spend NOT doing
        # netplay. Tear it down here rather than in each role function.
        if [ -n "$NX_HTTPD_PID" ]; then
            kill "$NX_HTTPD_PID" 2>/dev/null
            NX_HTTPD_PID=""
        fi
        rm -f /tmp/nx_netplay.pid
        rm -rf "$NETPLAY_SRV"
        nx_ui "Skipped - starting game..."
        # nx_ui only QUEUES its write in a background subshell, and
        # nx_ui_stop kills show2 immediately below, so without this pause
        # the confirmation is never drawn -- pressing B would look exactly
        # like nothing happening until flycast appears. One second is
        # enough for the subshell to be scheduled, open the FIFO and for
        # show2 to render a frame (it runs at 60 fps). This cost is paid
        # ONLY on the cancel path, never on a normal launch.
        sleep 1
    fi
    # Disarming deletes the flag file, so it MUST stay after the test
    # above -- do not reorder these two.
    nx_cancel_disarm
    nx_ui_stop
else
    # Netplay off: unplug port B again so single-player sessions see the
    # stock one-pad Dreamcast. It rewrites the key only when it currently
    # holds this block's own value (0), so a user who deliberately
    # configured another port-B device is not clobbered -- but a 0 set
    # through flycast's own Controls UI for local two-pad play is
    # indistinguishable from ours and does get reverted here.
    [ "$(nx_cfg_get input device2)" = "0" ] && nx_cfg_set input device2 10
fi
# --- end netplay -------------------------------------------------------

cd "$PAK_DIR"
if [ -n "$NX_CANCELLED" ]; then
    # Virtual config: flycast's get_entry() prefers virtual values and
    # ConfigFile::save() never writes them, so this disables netplay for
    # THIS RUN ONLY and emu.cfg is left byte-identical.
    ./flycast -config network:GGPO=no "$ROM" &> "$LOGS_PATH/$EMU_TAG.txt" &
else
    ./flycast "$ROM" &> "$LOGS_PATH/$EMU_TAG.txt" &
fi
EMU_PID=$!

# Thread pinning (cpu0-3 are all Cortex-A53 @ 2000 MHz). Evidence-based (see
# task-11 report): with all threads free-floating across the 4 shared cores,
# the SH4/emu thread ("Flycast-emu") measured only ~57% utime of a single
# core - looked like headroom - but jumped to ~96-100% of ITS OWN core the
# moment it was given a dedicated one, meaning it was actually being diluted/
# contended across cores rather than genuinely under 100%. Pinning also cut
# the real audio thread's ("SDLAudioP2") nonvoluntary context switches by
# roughly 4-8x (measured ~65-118 per 10s floating -> ~13-28 per 10s pinned),
# i.e. it is preempted far less once the emu/render threads stop migrating
# through its cores. That reduction in preemption/jitter for the thread that
# actually feeds the audio buffer is the most direct evidence pinning helps
# the gameplay-only distortion this task investigated.
#   Flycast-emu  (SH4 CPU + dynarec)       -> cpu0
#   Flycast-rend (main thread, GL present) -> cpu1
#   audio + helpers                        -> cpu2-3
# NOTE: taskset used to resolve (via PATH) to $SHARED_SYSTEM_PATH/bin/taskset
# (skeleton/SYSTEM/shared/bin/taskset), which crashed on this Brick unit
# ("FATAL: kernel too old" / SIGILL) - a statically-linked glibc binary's
# own startup code (_dl_non_dynamic_init(), only run for static executables)
# rejecting this device's real 4.9.191 kernel against a compile-time
# __LINUX_KERNEL_VERSION floor baked in from the toolchain's own kernel
# headers - NOT the .note.ABI-tag (this repo's own dynamic binaries carry a
# "for GNU/Linux 4.19.0" ABI-tag note and run fine on 4.9, so that note
# itself clearly isn't what's enforced). Confirmed by rebuilding with
# tg5040's own toolchain: still aborted identically. This silently no-op'd
# N64.pak's identical pinning too (masked by its own `2>/dev/null`). Fixed
# in this same task by dropping -static from workspace/all/taskset/Makefile
# (dynamic linking resolves libc.so.6 from the device's own /usr/lib64 at
# runtime instead, same resolution flycast itself relies on) and shipping
# the rebuilt binary at skeleton/SYSTEM/tg5040/bin/taskset, which now
# shadows the broken shared copy via this launch.sh's own PATH ordering
# ($SYSTEM_PATH/bin before $SHARED_SYSTEM_PATH/bin) - N64.pak's existing
# bare `taskset` calls pick this fix up automatically too, no edit needed
# there. Confirmed working on-device (task-11): no crash, affinity applied.
#
# Two passes: Flycast-emu (the SH4 thread) doesn't spawn until the game
# actually starts, and a real-BIOS boot can take longer than a single fixed
# sleep, so a one-shot scan at the same point every launch can miss it.
# Both calls below already tolerate a not-yet-spawned thread (no comm
# match) and a still-broken taskset (2>/dev/null) safely, so re-running the
# same scan is harmless if the first pass already caught everything.
pin_threads() {
    taskset -p 2 "$EMU_PID" 2>/dev/null   # mask 0x2 = cpu1 (main/render thread)
    for TID in $(ls /proc/$EMU_PID/task/ 2>/dev/null); do
        [ "$TID" = "$EMU_PID" ] && continue
        TNAME=$(cat /proc/$EMU_PID/task/$TID/comm 2>/dev/null)
        case "$TNAME" in
            Flycast-emu)
                taskset -p 1 "$TID" 2>/dev/null ;;    # mask 0x1 = cpu0
            Flycast-rend|SDLAudioP2|SDLHotplug*|SDLTimer|mali-*)
                taskset -p 0xc "$TID" 2>/dev/null ;;  # mask 0xc = cpu2-3
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
if [ -n "$NX_HTTPD_PID" ]; then
    kill "$NX_HTTPD_PID" 2>/dev/null
    rm -f /tmp/nx_netplay.pid
fi
echo 0 > /sys/class/speaker/mute 2>/dev/null || true
