#!/usr/bin/env bash
# Host test for each platform's post-update script (workspace/<plat>/install/
# update.sh, shipped as .system/bin/install.sh and run by <plat>.sh right
# after a MinUI.zip install): it must delete what the bluez upgrade pakz
# used to leave in the card root - the btmgr_<date> stock-stack backup
# (.tar on Smart Pro S, a tar.gz named .zip on tg5040) and the unpacked
# .update_bluez/ payload - and must not touch anything else.
#
# Runs the real script under busybox sh in a container with a fake
# /mnt/SDCARD (no /etc/version there, so the firmware gate is skipped).
# Needs docker.
set -euo pipefail
cd "$(dirname "$0")/../.."

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

cat > "$TMP/run.sh" <<'EOF'
#!/bin/sh
set -e
EXT="$1"
fail() { echo "FAIL ($EXT): $*"; exit 1; }
SD=/mnt/SDCARD
mkdir -p $SD
rm -f /etc/version
: > $SD/btmgr_19700331_123737.$EXT
: > $SD/btmgr_20260912_081500.$EXT
: > $SD/btmgr_notes.txt            # not a backup: must survive
: > "$SD/my btmgr_1.$EXT"          # prefix must anchor at the name start
mkdir -p $SD/.update_bluez/usr/bin && : > $SD/.update_bluez/usr/bin/bluetoothd
mkdir -p $SD/Saves && : > $SD/Saves/keep.srm

sh /update.sh > /tmp/out.txt 2>&1 || fail "update.sh exited non-zero: $(cat /tmp/out.txt)"

[ ! -e $SD/btmgr_19700331_123737.$EXT ] || fail "old bluez backup still present"
[ ! -e $SD/btmgr_20260912_081500.$EXT ] || fail "second backup still present"
[ ! -e $SD/.update_bluez ] || fail "unpacked .update_bluez payload still present"
[ -e $SD/btmgr_notes.txt ] || fail "unrelated btmgr_ file was deleted"
[ -e "$SD/my btmgr_1.$EXT" ] || fail "file merely containing btmgr_ was deleted"
[ -e $SD/Saves/keep.srm ] || fail "user data touched"
echo "PASS ($EXT): stale bluez leftovers removed, everything else kept"
EOF

for PLAT_EXT in tg5040:zip tg5050:tar; do
	PLAT="${PLAT_EXT%%:*}"
	cp "workspace/$PLAT/install/update.sh" "$TMP/update-$PLAT.sh"
	docker run --rm -v "$TMP/update-$PLAT.sh:/update.sh:ro" -v "$TMP/run.sh:/run.sh:ro" \
		alpine:3.19 sh /run.sh "${PLAT_EXT##*:}"
done
