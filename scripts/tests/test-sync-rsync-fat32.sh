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

PROTO=workspace/all/sync/sync_proto.h
LINE="$(grep 'const char\* rsync_opts = "' "$SRC")"
OPTS="$(sed -n 's/.*const char\* rsync_opts = "\([^"]*\)".*/\1/p' <<<"$LINE")"
[ -n "$OPTS" ] || { echo "FAIL: rsync_opts not found in $SRC"; exit 1; }
# The junk excludes come from sync_proto.h; their shell quotes are dropped
# here (run.sh disables globbing instead).
if [[ "$LINE" == *SYNC_JUNK_CLIENT_ARGS* ]]; then
	JUNK="$(sed -n 's/^#define SYNC_JUNK_CLIENT_ARGS "\([^"]*\)".*/\1/p' "$PROTO")"
	[ -n "$JUNK" ] || { echo "FAIL: SYNC_JUNK_CLIENT_ARGS not found in $PROTO"; exit 1; }
	OPTS="$OPTS ${JUNK//\'/}"
fi
# A cut transfer under --inplace leaves a truncated file with a fresh mtime
# that --update then spreads to both devices.
[[ " $OPTS " != *" --inplace "* ]] || { echo "FAIL: rsync_opts uses --inplace"; exit 1; }
# --info=progress2 is for the app's progress parser; keep it out of the itemized run
OPTS="${OPTS// --info=progress2/}"
OPTS="$(tr -s ' ' <<<"$OPTS")"

cat > "$TMP/run.sh" <<'EOF'
#!/bin/sh
set -ef
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
