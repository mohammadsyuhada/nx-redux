#!/usr/bin/env bash
# Host test for scripts/patch-osdd-layout.sh: every device variant is derived
# from the checked-in daemon with exactly the documented bytes changed and
# nothing else; a wrong source is refused. When llvm-objdump is available
# (macOS CommandLineTools) the patched instructions are also disassembled
# and their mnemonics asserted.
set -euo pipefail
cd "$(dirname "$0")/../.."
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
FAIL=0
fail() { echo "FAIL: $*" >&2; FAIL=1; }
OBJDUMP=""
for c in llvm-objdump /Library/Developer/CommandLineTools/usr/bin/llvm-objdump; do
    command -v "$c" >/dev/null 2>&1 && { OBJDUMP="$c"; break; }
done

check() { # dev  changed-byte-count  "vaddr:mnemonic-regex ..."
    local dev="$1" nbytes="$2" asm="$3"
    local src="skeleton/SYSTEM/osd/device/$dev/trimui_osdd"
    local out="$TMP/$dev.xbox"
    scripts/patch-osdd-layout.sh "$dev" "$src" "$out" || { fail "$dev: script failed"; return; }
    [ "$(wc -c < "$src")" = "$(wc -c < "$out")" ] || fail "$dev: size changed"
    local diffs
    # cmp -l exits 1 when the files differ (always, here) and pipefail
    # propagates it, so shield the assignment from set -e; the count is still
    # captured from stdout.
    diffs=$(cmp -l "$src" "$out" | wc -l | tr -d ' ') || true
    [ "$diffs" = "$nbytes" ] || fail "$dev: expected $nbytes changed bytes, got $diffs"
    # running twice on the same input is deterministic
    scripts/patch-osdd-layout.sh "$dev" "$src" "$TMP/$dev.again" >/dev/null
    cmp -s "$out" "$TMP/$dev.again" || fail "$dev: not deterministic"
    # committed variant (if present) matches what the script produces now
    if [ -f "$src.xbox" ]; then
        cmp -s "$out" "$src.xbox" || fail "$dev: committed trimui_osdd.xbox is stale — rerun scripts/patch-osdd-layout.sh $dev"
    fi
    if [ -n "$OBJDUMP" ]; then
        local dis
        dis=$("$OBJDUMP" -d "$out")
        for pair in $asm; do
            local addr="${pair%%:*}" re="${pair#*:}"
            echo "$dis" | grep -E "^ *${addr}:" | grep -Eq "$re" \
                || fail "$dev: expected /$re/ at $addr, got: $(echo "$dis" | grep -E "^ *${addr}:")"
        done
    fi
}

# Changed-byte counts come from the per-device table in the patch script
# (cmp -l counts bytes, not instructions):
#   brick     c0000054->00080054 (2) + e0070034->40080034 (2) + 00080054->c0faff54 (3) + 80008052->20008052 (1) = 8
#   brickpro  2 + 2 + 00080054->a0070054 (2) + 40008052->20008052 (1) = 7
#   smartpro  same bytes as brick = 8
#   smartpros 0010811a->0000811a = 1
check brick     8 "40b8ac:b\.eq.*0x40b9ac 40b8b0:cbz.*0x40b9b8 40b8b8:b\.eq.*0x40b810 40b9b8:mov.*w0,.*#0x1"
check brickpro  7 "40b8ac:b\.eq.*0x40b9ac 40b8b0:cbz.*0x40b9b8 40b8b8:b\.eq.*0x40b9ac 40b9b8:mov.*w0,.*#0x1"
check smartpro  8 "40b7fc:b\.eq.*0x40b8fc 40b800:cbz.*0x40b908 40b808:b\.eq.*0x40b760 40b908:mov.*w0,.*#0x1"
check smartpros 1 "12f0c:csel.*w0,.*w0,.*w1,.*eq"

# a wrong source must be refused, and the destination must not be written.
# Flip a byte to a value it does not already hold (offset 100 is 0x00 in the
# stock daemon) so the md5 actually changes and the guard has to reject it.
cp skeleton/SYSTEM/osd/device/brick/trimui_osdd "$TMP/tampered"
printf '\377' | dd of="$TMP/tampered" bs=1 seek=100 conv=notrunc 2>/dev/null
if scripts/patch-osdd-layout.sh brick "$TMP/tampered" "$TMP/tampered.xbox" 2>/dev/null; then
    fail "tampered source was accepted"
fi
[ ! -f "$TMP/tampered.xbox" ] || fail "destination written despite refusal"

[ "$FAIL" = 0 ] && echo "test-patch-osdd-layout: OK"
exit "$FAIL"
