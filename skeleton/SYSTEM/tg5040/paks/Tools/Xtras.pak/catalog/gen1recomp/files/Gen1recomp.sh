#!/bin/sh
# NX_RUNTIME: native
# gen1recomp launcher — self-contained native runtime entry (2026-08-10
# conversion; extras_games_launch.sh execs this directly, no PortMaster).
#
# Why native works: the bundled LOVE 11.5 runtime resolves every shared-lib
# dependency without PortMaster - love.aarch64 needs only libc + the bundled
# liblove/libluajit (libs.aarch64/), and liblove's own deps come from the
# firmware: SDL2 + mpg123 from /usr/trimui/lib, freetype/openal/theoradec/
# vorbisfile/z/stdc++ from /usr/lib (device-verified on tg5040 2026-08-10).
# POSIX sh on purpose: /bin/bash on these cards is a symlink ports_launch.sh
# creates INTO PortMaster's vendored bin - a bash shebang would quietly
# reintroduce the dependency this entry just dropped.
#
# What this script owns (native entries own their ENTIRE runtime):
#   - LD_LIBRARY_PATH (bundled libs first, then the firmware SDL)
#   - Nintendo-layout SDL controller override (physical A = confirm), same
#     xbox_layout user toggle PortMaster ports honor
#   - audio routing: copy audiomon's .asoundrc so ALSA follows the chosen
#     output sink (BT/USB DAC), plus the anti-pop speaker mute dance
#   - /tmp/stay_awake + sleepmon.elf (power-button sleep, system binary on
#     the launch-chain PATH), mirroring ports_launch.sh - which also never
#     clears stay_awake; the MinUI launch loop handles post-exit state
#   - CPU: all cores online + frequency ceiling raised to the hardware max
#     under schedutil (load-scaling, not a hard pin). Both 1GB devices get the
#     eMMC swapfile (OOM guard, verified 2026-08-06: game + voxel mod peak
#     ~750MB on a 1GB device; exFAT SD can't host swap); tg5050 additionally
#     gets big-core pinning. The MinUI launch loop restores CPU state after the
#     game exits.

SHDIR="$(cd "$(dirname "$0")" && pwd)"
GAMEDIR="$SHDIR/.data/gen1recomp"
CONFDIR="$GAMEDIR/conf"
mkdir -p "$CONFDIR"

cd "$GAMEDIR" || exit 1
: > "$GAMEDIR/log.txt"
exec > "$GAMEDIR/log.txt" 2>&1

export HOME="$CONFDIR"
export XDG_DATA_HOME="$CONFDIR"
export XDG_CONFIG_HOME="$CONFDIR"
# Original path kept for the system helpers spawned below (sleepmon.elf
# needs libmsettings.so from .system/lib; the game-first override must not
# shadow or hide that for them).
NX_ORIG_LDLP="${LD_LIBRARY_PATH:-}"
export LD_LIBRARY_PATH="$GAMEDIR/libs.aarch64:/usr/trimui/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# Audio-output routing: audiomon maintains the ALSA config for the active
# sink under USERDATA_PATH; ALSA discovers it via $HOME/.asoundrc. Refresh
# every launch (and drop a stale copy when routing was reset) so in-game
# audio follows the sink chosen in Settings.
if [ -f "${USERDATA_PATH:-}/.asoundrc" ]; then
    cp -f "$USERDATA_PATH/.asoundrc" "$HOME/.asoundrc"
else
    rm -f "$HOME/.asoundrc"
fi

# get_controls' positional (Xbox-convention) mapping would land confirm on
# physical B; apply the default-nintendo override for the TRIMUI Player1
# GUID unless the user opted into positional layout via the shared toggle.
NX_SHARED_USERDATA="${SHARED_USERDATA_PATH:-${SDCARD_PATH:-/mnt/SDCARD}/.userdata/shared}"
if [ ! -f "$NX_SHARED_USERDATA/PORTS-portmaster/xbox_layout" ]; then
    export SDL_GAMECONTROLLERCONFIG="030000005e0400008e02000014010000,TRIMUI Player1,a:b1,b:b0,back:b6,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b8,leftshoulder:b4,leftstick:b9,lefttrigger:a2,leftx:a0,lefty:a1,rightshoulder:b5,rightstick:b10,righttrigger:a5,rightx:a3,righty:a4,start:b7,x:b3,y:b2,platform:Linux,"
fi
export LOVE_GRAPHICS_USE_OPENGLES="${LOVE_GRAPHICS_USE_OPENGLES:-1}"
export POKEPORT_GBCFX="${POKEPORT_GBCFX:-0}"

# TLS trust for the game's own network features (mod index, mod installs,
# release checks - all shelled out to the firmware's curl): the firmware
# ships NO CA store at all (/etc/ssl/certs is empty, device-verified
# 2026-08-18), so every https fetch dies with a certificate error unless a
# bundle is handed to it. The system-shipped bundle (.system/shared/ssl,
# same seam install.sh's TLS check uses) is preferred; PortMaster's vendored
# copy is the fallback for a card running an older system build. curl reads
# CURL_CA_BUNDLE directly; SSL_CERT_FILE covers OpenSSL-level consumers.
for _ca in "${SDCARD_PATH:-/mnt/SDCARD}/.system/shared/ssl/ca-certificates.crt" \
           "${SDCARD_PATH:-/mnt/SDCARD}/Emus/shared/PortMaster/ssl/certs/ca-certificates.crt"; do
    if [ -f "$_ca" ]; then
        export CURL_CA_BUNDLE="$_ca"
        export SSL_CERT_FILE="$_ca"
        break
    fi
done

chmod a+x ./bin/love.aarch64 2>/dev/null

echo "1" > /tmp/stay_awake

NX_SLEEPMON_PID=""
cleanup() {
    [ -n "$NX_SLEEPMON_PID" ] && kill "$NX_SLEEPMON_PID" 2>/dev/null
    echo 0 > /sys/class/speaker/mute 2>/dev/null
    rm -f "$HOME/.asoundrc"
}
trap cleanup EXIT INT TERM HUP QUIT

# Anti-pop: mute the speaker over LOVE/OpenAL init, then unmute and let
# syncsettings.elf restore the user's saved volume (same dance
# ports_launch.sh does for every port).
echo 1 > /sys/class/speaker/mute 2>/dev/null
( sleep 5; echo 0 > /sys/class/speaker/mute 2>/dev/null; syncsettings.elf 2>/dev/null ) &

# Power-button sleep/poweroff handler for the duration of the game - run
# with the ORIGINAL library path (it links libmsettings.so from
# .system/lib, which the game-first override above doesn't carry).
if command -v sleepmon.elf >/dev/null 2>&1; then
    LD_LIBRARY_PATH="$NX_ORIG_LDLP" sleepmon.elf &
    NX_SLEEPMON_PID=$!
fi

# Per-platform tuning; MinUI's launch loop restores CPU state afterwards.
# The frontend caps the CPU low at idle (e.g. 600MHz on brick vs a 2GHz
# hardware max) and hotplugs cores out. At that clock LOVE launches slowly and
# starves its 48kHz OpenAL mixer thread into XRUN underruns -> audio
# distortion. Two things fix it: bring every core back online, and raise each
# cluster's frequency CEILING to the hardware max. We leave the governor on
# schedutil (not a hard 'performance' pin) so the clock still scales with load
# - cooler and easier on the battery - while still reaching full speed under
# the game's sustained load. This mirrors PortMaster's ports_launch.sh on tg5050.
NX_TASKSET=""
# Bring every core online: the frontend hotplugs cores out at idle (on tg5050
# it leaves all but one big core offline), which would otherwise trap the
# big-core taskset below - and LOVE's audio thread - on a single core.
for oc in /sys/devices/system/cpu/cpu[0-9]*/online; do
    echo 1 > "$oc" 2>/dev/null
done
# Raise each cluster to its full frequency range under schedutil. Write the max
# ceiling before the min floor so a min value can't momentarily exceed the old
# (frontend-lowered) max and get rejected by the driver.
for pol in /sys/devices/system/cpu/cpufreq/policy*; do
    [ -d "$pol" ] || continue
    hwmax="$(cat "$pol/cpuinfo_max_freq" 2>/dev/null)"
    hwmin="$(cat "$pol/cpuinfo_min_freq" 2>/dev/null)"
    echo schedutil > "$pol/scaling_governor" 2>/dev/null
    [ -n "$hwmax" ] && echo "$hwmax" > "$pol/scaling_max_freq" 2>/dev/null
    [ -n "$hwmin" ] && echo "$hwmin" > "$pol/scaling_min_freq" 2>/dev/null
done
# eMMC swapfile OOM guard for BOTH 1GB devices (brick and tg5050). Either peaks
# ~750MB with the Dramatic Shape voxel mod enabled; exFAT SD can't host swap, so
# the file lives on the internal rootfs. Confirmed swap-able on both layouts:
# tg5050's plain ext4 '/' and the brick's overlayfs '/' (upperdir is ext4).
# Created once and reused each launch; swapon failure is always non-fatal.
# Runs in the BACKGROUND: on a fresh device the 512MB dd takes 10-20s of
# eMMC writes, and in the foreground that was a black screen between menu
# and game window long enough to read as a broken launch (and backing out
# mid-dd left a partial file, so the next launch re-ran the whole dd -
# every attempt stalled). Swap only matters minutes into a session (voxel
# peak ~750MB), never at boot, so let the game start while it builds.
(
    if [ ! -f /swapfile ]; then
        # If dd or mkswap fails partway (e.g. a full rootfs leaves a truncated
        # /swapfile), remove it so `[ ! -f /swapfile ]` doesn't pass forever -
        # without this, every later launch skips creation, swapon silently fails,
        # and voxel-mod sessions OOM-kill with no swap ever retried.
        # shellcheck disable=SC2015
        # (deliberate, not an if/then/else stand-in: `|| rm -f` here means "clean
        # up on ANY failure in the chain", not just when dd fails.)
        dd if=/dev/zero of=/swapfile bs=1M count=512 2>/dev/null \
            && chmod 600 /swapfile && mkswap /swapfile >/dev/null 2>&1 \
            || rm -f /swapfile
    fi
    swapon /swapfile 2>/dev/null
) &

# Big-core affinity is tg5050-only: the brick has a single CPU cluster, so
# there are no performance cores to pin LOVE onto.
case "${PLATFORM:-}" in
  tg5050)
    command -v taskset >/dev/null 2>&1 && NX_TASKSET="taskset -c 4-7"
    ;;
esac

$NX_TASKSET ./bin/love.aarch64 "$GAMEDIR/lovegame"
