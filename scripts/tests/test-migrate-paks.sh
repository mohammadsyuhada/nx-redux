#!/usr/bin/env bash
# Fixture-based test for the on-device pak cleanup (platform-less layout +
# legacy-platform-dir hoist). Runs migrate-paks.sh with SDCARD_PATH overridden.
set -euo pipefail
cd "$(dirname "$0")/../.."
SCRIPT="skeleton/SYSTEM/shared/bin/migrate-paks.sh"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
fail() { echo "FAIL: $1" >&2; exit 1; }
SD="$TMP/sd"
PLAT=tg5040
FOREIGN=tg5050   # the OTHER known platform, present on a formerly-shared card
SYS="$SD/.system"
REPORT="$SD/.userdata/$PLAT/migration-report.txt"

reset_card() {
	rm -rf "$SD"
	mkdir -p "$SYS/paks/Emus/N64.pak" "$SYS/paks/Emus/FBN.pak" \
	         "$SYS/paks/Tools/Game Tracker.pak"
	# current-layout SD paks: shipped-name + community
	mkdir -p "$SD/Emus/N64.pak" "$SD/Emus/PSP.pak" "$SD/Tools/Game Tracker.pak"
	printf 'x' > "$SD/Emus/N64.pak/launch.sh"
	printf 'x' > "$SD/Emus/PSP.pak/launch.sh"
	printf 'x' > "$SD/Tools/Game Tracker.pak/launch.sh"
	# legacy platform dirs: shipped-name, community, collision, .media
	mkdir -p "$SD/Emus/$PLAT/FBN.pak" "$SD/Emus/$PLAT/PSP2.pak" \
	         "$SD/Emus/$PLAT/COLL.pak" "$SD/Emus/COLL.pak" \
	         "$SD/Tools/$PLAT/.media/Custom.pak"
	printf 'x'        > "$SD/Emus/$PLAT/FBN.pak/launch.sh"
	printf 'x'        > "$SD/Emus/$PLAT/PSP2.pak/launch.sh"
	printf 'legacy'   > "$SD/Emus/$PLAT/COLL.pak/launch.sh"
	printf 'current'  > "$SD/Emus/COLL.pak/launch.sh"
	printf 'old-bg'   > "$SD/Tools/$PLAT/.media/bg.png"
	printf 'user-art' > "$SD/Tools/$PLAT/.media/Custom.pak/bg.png"
	# freshly-shipped .media (as unzip -o would have left it)
	mkdir -p "$SD/Tools/.media"
	printf 'new-bg' > "$SD/Tools/.media/bg.png"
	# shared runtimes are out of scope entirely
	mkdir -p "$SD/Emus/shared/PortMaster"
	printf 'state' > "$SD/Emus/shared/PortMaster/user.cfg"
	# legacy .system/<plat> tree (pre-flatten): bin/lib/cores with a dummy each,
	# plus the two Task-11 compat shims that a legacy boot must not lose
	mkdir -p "$SYS/$PLAT/bin" "$SYS/$PLAT/lib" "$SYS/$PLAT/cores" \
	         "$SYS/$PLAT/paks/MinUI.pak"
	printf 'x'    > "$SYS/$PLAT/bin/other"
	printf 'x'    > "$SYS/$PLAT/lib/libfoo.so"
	printf 'x'    > "$SYS/$PLAT/cores/core.so"
	printf 'shim' > "$SYS/$PLAT/bin/install.sh"
	printf 'shim' > "$SYS/$PLAT/paks/MinUI.pak/launch.sh"
	# FOREIGN platform's legacy trees (formerly-shared card): shipped-tag pak +
	# community pak + .media (bg collides, foreignart is unique user art), and a
	# foreign .system tree with its own shims — all dead weight on a per-device
	# card, cleaned regardless of $PLATFORM or the legacy-boot flag.
	mkdir -p "$SD/Emus/$FOREIGN/FBN.pak" "$SD/Emus/$FOREIGN/FOREIGN.pak" \
	         "$SD/Tools/$FOREIGN/.media/foreignart"
	printf 'x'          > "$SD/Emus/$FOREIGN/FBN.pak/launch.sh"
	printf 'x'          > "$SD/Emus/$FOREIGN/FOREIGN.pak/launch.sh"
	printf 'foreign-bg' > "$SD/Tools/$FOREIGN/.media/bg.png"
	printf 'foreign-art'> "$SD/Tools/$FOREIGN/.media/foreignart/art.png"
	mkdir -p "$SYS/$FOREIGN/bin" "$SYS/$FOREIGN/lib" "$SYS/$FOREIGN/paks/MinUI.pak"
	printf 'x'    > "$SYS/$FOREIGN/lib/libbar.so"
	printf 'shim' > "$SYS/$FOREIGN/bin/install.sh"
	printf 'shim' > "$SYS/$FOREIGN/paks/MinUI.pak/launch.sh"
}

# run() is always a NORMAL (new-boot) run: force the flag path to a name that
# never exists so the legacy tree is removed wholesale, regardless of /tmp.
NOFLAG="$TMP/no_such_flag"
run() { SDCARD_PATH="$SD" NX_LEGACY_FLAG="$NOFLAG" sh "$SCRIPT" "$PLAT"; }

# First run is a LEGACY boot: the old boot.sh set the flag before exec'ing the
# updater, so the legacy tree must be pruned AROUND the two compat shims (the
# still-running old boot.sh execs the launch shim after install.sh returns).
reset_card
FLAG="$TMP/tmpdir/nx_legacy_boot"
mkdir -p "$TMP/tmpdir"
touch "$FLAG"
SDCARD_PATH="$SD" NX_LEGACY_FLAG="$FLAG" sh "$SCRIPT" "$PLAT"

# current layout
[ ! -d "$SD/Emus/N64.pak" ]              || fail "shipped-name emu pak not deleted"
[ ! -d "$SD/Tools/Game Tracker.pak" ]    || fail "shipped-name tool pak (space) not deleted"
[ -d "$SD/Emus/PSP.pak" ]                || fail "community pak touched"
# legacy dir
[ ! -d "$SD/Emus/$PLAT/FBN.pak" ]        || fail "legacy shipped-name pak not deleted"
[ -d "$SD/Emus/PSP2.pak" ]               || fail "legacy community pak not hoisted"
[ "$(cat "$SD/Emus/COLL.pak/launch.sh")" = "current" ] || fail "collision clobbered current pak"
[ -d "$SD/Emus/$PLAT" ]                  || fail "collision dir removed despite leftover"
[ -d "$SD/Emus/$PLAT/COLL.pak" ]         || fail "colliding legacy pak vanished"
grep -q "SKIPPED .*COLL.pak" "$REPORT"   || fail "no SKIPPED line for collision"
# .media merge: shipped bg wins, user art hoisted, legacy .media gone
[ "$(cat "$SD/Tools/.media/bg.png")" = "new-bg" ]              || fail "shipped bg clobbered"
[ "$(cat "$SD/Tools/.media/Custom.pak/bg.png")" = "user-art" ] || fail "user art not merged"
[ ! -d "$SD/Tools/$PLAT/.media" ]        || fail "legacy .media not removed"
[ ! -d "$SD/Tools/$PLAT" ]               || fail "empty legacy Tools dir not removed"
# untouchables + READMEs
[ -f "$SD/Emus/shared/PortMaster/user.cfg" ] || fail "Emus/shared touched"
[ -f "$SD/Emus/README.txt" ]             || fail "Emus README missing"
[ -f "$SD/Tools/README.txt" ]            || fail "Tools README missing"
# legacy .system/<plat> under a legacy boot: pruned around the two shims, kept
[ ! -d "$SYS/$PLAT/cores" ]                     || fail "legacy cores not pruned on legacy boot"
[ ! -d "$SYS/$PLAT/lib" ]                        || fail "legacy lib not pruned on legacy boot"
[ ! -f "$SYS/$PLAT/bin/other" ]                  || fail "legacy bin dummy not pruned on legacy boot"
[ -f "$SYS/$PLAT/bin/install.sh" ]               || fail "install shim pruned on legacy boot"
[ -f "$SYS/$PLAT/paks/MinUI.pak/launch.sh" ]     || fail "launch shim pruned on legacy boot"
[ -d "$SYS/$PLAT" ]                              || fail "legacy system dir removed on legacy boot"
# foreign platform (formerly-shared card): swept wholesale even under legacy boot
[ ! -d "$SD/Emus/$FOREIGN/FBN.pak" ]   || fail "foreign shipped-name pak not deleted"
[ -d "$SD/Emus/FOREIGN.pak" ]          || fail "foreign community pak not hoisted"
[ ! -d "$SD/Emus/$FOREIGN" ]           || fail "foreign legacy Emus dir not removed"
[ ! -d "$SD/Tools/$FOREIGN" ]          || fail "foreign legacy Tools dir not removed"
[ "$(cat "$SD/Tools/.media/foreignart/art.png")" = "foreign-art" ] || fail "foreign user art not rescued"
[ "$(cat "$SD/Tools/.media/bg.png")" = "new-bg" ] || fail "foreign bg clobbered current .media"
[ ! -d "$SYS/$FOREIGN" ]               || fail "foreign .system tree kept despite being foreign"

# normal (new-boot) run: the legacy system tree — shims and all — goes wholesale
run
[ ! -d "$SYS/$PLAT" ] || fail "legacy system tree not removed on normal run"

# idempotency: a further run adds no DELETED/MOVED lines (nothing left to do)
n1=$(grep -c "DELETED\|MOVED" "$REPORT")
run
n2=$(grep -c "DELETED\|MOVED" "$REPORT")
[ "$n1" = "$n2" ] || fail "second run acted again ($n1 -> $n2)"

# fresh card: no /Emus,/Tools and no legacy .system/<plat> at all -> no-op, exit 0
rm -rf "$SD/Emus" "$SD/Tools" "$SYS/$PLAT"
run
[ -f "$SD/Emus/README.txt" ] || fail "README not created on fresh card"
[ ! -d "$SYS/$PLAT" ]        || fail "legacy tree recreated on fresh card"

echo "PASS"
