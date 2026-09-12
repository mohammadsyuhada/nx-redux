#!/usr/bin/env bash
# Host test for the Device Sync rsync options (workspace/all/sync/sync.c,
# `rsync_opts`) against a FAT32 card.
#
# FAT32 stores modification times at 2-second resolution, so after a sync the
# receiver's mtimes round down and, without a modify-window, every file whose
# source mtime is odd looks changed again on the next run and gets
# re-transferred. The exact option string from sync.c is run with the
# container's rsync against a vfat loop image: the first pass must copy the
# file, the second pass must transfer nothing.
# Needs docker.
set -euo pipefail
cd "$(dirname "$0")/../.."

SRC=workspace/all/sync/sync.c
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

OPTS="$(sed -n 's/.*const char\* rsync_opts = "\([^"]*\)";.*/\1/p' "$SRC")"
[ -n "$OPTS" ] || { echo "FAIL: rsync_opts not found in $SRC"; exit 1; }
# --info=progress2 is for the app's progress parser; keep it out of the itemized run
OPTS="${OPTS// --info=progress2/}"

cat > "$TMP/run.sh" <<'EOF'
#!/bin/sh
set -e
OPTS="$1"
apk add -q rsync dosfstools > /dev/null 2>&1
fail() { echo "FAIL: $*"; exit 1; }

dd if=/dev/zero of=/fat.img bs=1M count=32 status=none
mkfs.vfat /fat.img > /dev/null
mkdir -p /fat /src
mount -t vfat -o loop,iocharset=utf8,noatime /fat.img /fat

echo save > /src/game.sav
touch -d @1000000001 /src/game.sav   # odd second: vfat can only store 1000000000

# pass 1: must copy
rsync $OPTS --itemize-changes /src/ /fat/dst/ | grep -q '^>f' || fail "first pass did not transfer the file"
[ "$(cat /fat/dst/game.sav)" = save ] || fail "file content wrong after first pass"
[ "$(stat -c %Y /fat/dst/game.sav)" = 1000000000 ] || fail "expected vfat to round the mtime down to 1000000000, got $(stat -c %Y /fat/dst/game.sav)"

# pass 2: nothing changed, must transfer nothing
n=$(rsync $OPTS --itemize-changes /src/ /fat/dst/ | grep -c '^>f' || true)
[ "$n" = 0 ] || fail "second pass re-transferred $n file(s): FAT32 mtime rounding not tolerated"
echo "PASS: rsync opts '$OPTS' tolerate FAT32 timestamps"
EOF

docker run --rm --privileged -v "$TMP/run.sh:/run.sh:ro" alpine:3.19 sh /run.sh "$OPTS"
