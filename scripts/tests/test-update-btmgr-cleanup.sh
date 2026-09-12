#!/usr/bin/env bash
# Host test for the tg5050 post-update script (workspace/tg5050/install/
# update.sh, shipped as .system/bin/install.sh and run by tg5050.sh right
# after a MinUI.zip install): it must delete the btmgr_<date>.tar backup
# that the bluez upgrade used to leave in the card root on Smart Pro S,
# and must not touch anything else.
#
# Runs the real script under busybox sh in a container with a fake
# /mnt/SDCARD (no /etc/version there, so the firmware gate is skipped;
# migrate-paks.sh is stubbed — it is `|| true` in the script anyway).
# Needs docker.
set -euo pipefail
cd "$(dirname "$0")/../.."

SRC=workspace/tg5050/install/update.sh
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cp "$SRC" "$TMP/update.sh"

cat > "$TMP/run.sh" <<'EOF'
#!/bin/sh
set -e
fail() { echo "FAIL: $*"; exit 1; }
SD=/mnt/SDCARD
mkdir -p $SD/.system/shared/bin
printf '#!/bin/sh\nexit 0\n' > $SD/.system/shared/bin/migrate-paks.sh
rm -f /etc/version
: > $SD/btmgr_19700331_123737.tar
: > $SD/btmgr_20260912_081500.tar
: > $SD/btmgr_notes.txt            # not a backup tar: must survive
: > "$SD/my btmgr_1.tar"           # prefix must anchor at the name start
mkdir -p $SD/Saves && : > $SD/Saves/keep.srm

sh /update.sh > /tmp/out.txt 2>&1 || fail "update.sh exited non-zero: $(cat /tmp/out.txt)"

[ ! -e $SD/btmgr_19700331_123737.tar ] || fail "old bluez backup tar still present"
[ ! -e $SD/btmgr_20260912_081500.tar ] || fail "second backup tar still present"
[ -e $SD/btmgr_notes.txt ] || fail "unrelated btmgr_ file was deleted"
[ -e "$SD/my btmgr_1.tar" ] || fail "file merely containing btmgr_ was deleted"
[ -e $SD/Saves/keep.srm ] || fail "user data touched"
echo "PASS: stale bluez backup tars removed, everything else kept"
EOF

docker run --rm -v "$TMP/update.sh:/update.sh:ro" -v "$TMP/run.sh:/run.sh:ro" alpine:3.19 sh /run.sh
