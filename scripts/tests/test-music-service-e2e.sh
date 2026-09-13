#!/usr/bin/env bash
# On-device end-to-end test for the music service (tg5040, or tg5050 with NX_PLATFORM=tg5050) over adb.
#
# The music service tests need a real forked musicplayerd that opens the ALSA
# PCM, so they cannot run on the host — they run on a Brick. This script
# cross-compiles the two device tests plus the daemon and control CLI in the
# platform toolchain container, pushes all four to /tmp on the device under
# *.test.elf names (never over the installed /mnt/SDCARD binaries), and runs the
# tests with the live launcher environment imported from the running nextui.elf
# (HOME/USERDATA_PATH/SDCARD_PATH/LD_LIBRARY_PATH/... ), without which the owner
# cannot open the launcher's .asoundrc PCM.
#
#   test_music_service.test.elf <daemon> <ctl>
#       forks a real musicplayerd/musicplayerctl and drives the socket protocol:
#       split-header writes, back pressure, singleton rejection, oversize/stalled
#       client drops, and resume persistence across owner restarts.
#   test_music_client.test.elf
#       drives music_client.c against a fake in-process owner (no arguments).
#
# The flags come straight from the musicplayer Makefile (CC / MY_CFLAGS /
# MY_LDFLAGS via `make --eval`), so there is no test target to keep in sync.
#
# Preconditions:
#   - a tg5040 device (Brick / Brick Pro / Smart Pro), or a Smart Pro S with NX_PLATFORM=tg5050, attached over adb with
#     NX Redux running (idle at the main menu is fine). Set ANDROID_SERIAL if
#     more than one device is attached.
#   - docker, with the ghcr.io/loveretro/<platform>-toolchain:latest image.
#   - run from anywhere; the container build mounts this worktree's workspace/.
#
# Usage: scripts/tests/test-music-service-e2e.sh            (tg5040: Brick, Brick Pro, Smart Pro)
#        NX_PLATFORM=tg5050 scripts/tests/test-music-service-e2e.sh   (Smart Pro S)
set -uo pipefail

REPO=$(cd "$(dirname "$0")/../.." && pwd)
PLATFORM=${NX_PLATFORM:-tg5040}
IMAGE=ghcr.io/loveretro/${PLATFORM}-toolchain:latest
BIN=$REPO/workspace/all/musicplayer/build/$PLATFORM

die() { echo "ABORT: $*"; exit 2; }

command -v docker >/dev/null 2>&1 || die "docker not found"
command -v adb >/dev/null 2>&1 || die "adb not found"
adb get-state >/dev/null 2>&1 || die "no adb device (set ANDROID_SERIAL if several are attached)"

echo "== building test_music_service, test_music_client, musicplayerd, musicplayerctl ($PLATFORM)"
# -i so the heredoc reaches the container's bash. The daemon and ctl come from
# the plain `make`; the two device tests are compiled with the Makefile's own
# CC/MY_CFLAGS/MY_LDFLAGS, read back with a throwaway `print-%` pattern rule.
docker run --rm -i -e NX_PLATFORM="$PLATFORM" -v "$REPO/workspace:/root/workspace" "$IMAGE" /bin/bash -s <<'DOCKER'
set -e
source ~/.bashrc
cd /root/workspace/all/musicplayer
make PLATFORM=$NX_PLATFORM
vals=$(make -s PLATFORM=$NX_PLATFORM --eval='print-%: ; @echo $($*)' print-CC print-MY_CFLAGS print-MY_LDFLAGS)
CC=$(printf '%s\n' "$vals" | sed -n 1p)
CFLAGS=$(printf '%s\n' "$vals" | sed -n 2p)
LDFLAGS=$(printf '%s\n' "$vals" | sed -n 3p)
$CC tests/test_music_service.c music_service_client.c -o build/$NX_PLATFORM/test_music_service.elf $CFLAGS $LDFLAGS
$CC tests/test_music_client.c music_client.c music_service_client.c -o build/$NX_PLATFORM/test_music_client.elf $CFLAGS -lpthread
DOCKER
[ $? -eq 0 ] || die "container build failed"

for e in test_music_service test_music_client musicplayerd musicplayerctl; do
	[ -f "$BIN/$e.elf" ] || die "missing $BIN/$e.elf after build"
done

echo "== pushing binaries to /tmp/*.test.elf on the device"
adb push "$BIN/test_music_service.elf" /tmp/test_music_service.test.elf >/dev/null || die "push failed"
adb push "$BIN/test_music_client.elf" /tmp/test_music_client.test.elf >/dev/null || die "push failed"
adb push "$BIN/musicplayerd.elf" /tmp/musicplayerd.test.elf >/dev/null || die "push failed"
adb push "$BIN/musicplayerctl.elf" /tmp/musicplayerctl.test.elf >/dev/null || die "push failed"
adb shell 'chmod 755 /tmp/*.test.elf' >/dev/null 2>&1

# Import the launcher environment from the running nextui.elf (fallback keymon.elf)
# and run one test. The forked musicplayerd inherits this environment, so it gets
# the full LD_LIBRARY_PATH and the ALSA PCM it needs.
run_on_device() {
	adb shell "
src=\$(pidof nextui.elf); [ -n \"\$src\" ] || src=\$(pidof keymon.elf);
[ -n \"\$src\" ] || { echo 'NO_LAUNCHER (is NX Redux running?)'; exit 1; };
tr '\\0' '\\n' < /proc/\$src/environ | grep -E '^(HOME|USERDATA_PATH|SHARED_USERDATA_PATH|SDCARD_PATH|DEVICE|PLATFORM|LD_LIBRARY_PATH|PATH|LOGS_PATH)=' > /tmp/music_test_env.txt;
while IFS= read -r line; do export \"\$line\"; done < /tmp/music_test_env.txt;
cd /tmp && $1; echo \"__EXIT=\$?\"
" 2>&1 | tr -d '\r'
}

echo "== running test_music_service on the device"
svc_out=$(run_on_device "/tmp/test_music_service.test.elf /tmp/musicplayerd.test.elf /tmp/musicplayerctl.test.elf")
echo "$svc_out"

echo "== running test_music_client on the device"
cli_out=$(run_on_device "/tmp/test_music_client.test.elf")
echo "$cli_out"

rc=0
echo "$svc_out" | grep -q '__EXIT=0' || { echo "service test exited non-zero"; rc=1; }
echo "$cli_out" | grep -q '__EXIT=0' || { echo "client test exited non-zero"; rc=1; }
printf '%s\n%s\n' "$svc_out" "$cli_out" | grep -q 'FAIL:' && { echo "a test printed FAIL"; rc=1; }

echo "== cleaning up /tmp/*.test.elf on the device"
adb shell 'rm -f /tmp/*.test.elf /tmp/music_test_env.txt' >/dev/null 2>&1

[ $rc -eq 0 ] && echo "== PASS" || echo "== FAIL"
exit $rc
