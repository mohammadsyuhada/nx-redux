#!/usr/bin/env bash
# Host test for the MinUI.zip install/update branch of each platform's
# install/boot.sh (.tmp_update/<platform>.sh): a corrupt/truncated MinUI.zip
# must never wipe a working .system. The branch is extracted from boot.sh and
# run under sh against a fake card, with host unzip standing in for the
# bundled one and the splash fifo / sleeps stubbed out.
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT="$PWD"; TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
FAIL=0; fail() { echo "FAIL: $*" >&2; FAIL=1; }

TOOLS="$TMP/tools"; mkdir -p "$TOOLS"; ln -s "$(command -v unzip)" "$TOOLS/unzip"

# a payload zip: a new system plus its install.sh hook
P="$TMP/payload"; mkdir -p "$P/.system/bin" "$P/.system/paks/MinUI.pak"
echo new > "$P/.system/paks/MinUI.pak/launch.sh"
printf '#!/bin/sh\necho ran > "$(dirname "$0")/../../install-ran"\n' > "$P/.system/bin/install.sh"
chmod +x "$P/.system/bin/install.sh"
head -c 200000 /dev/urandom > "$P/.system/bin/blob"
(cd "$P" && zip -qr "$TMP/good.zip" .system)
head -c 100000 "$TMP/good.zip" > "$TMP/bad.zip"

card() { # dir [with-system]
	mkdir -p "$1"
	if [ "${2:-}" = sys ]; then
		mkdir -p "$1/.system/bin" "$1/.system/paks/MinUI.pak"
		echo old > "$1/.system/paks/MinUI.pak/launch.sh"
	fi
}

for PLAT in tg5040 tg5050; do
	BOOT="$ROOT/workspace/$PLAT/install/boot.sh"
	BLOCK="$(sed -n '/^# install\/update$/,/^#kill \$SHOW_PID/p' "$BOOT" \
		| sed -e 's#cd \$(dirname "\$0")/\$PLATFORM#cd "$TOOLS"#' \
		      -e "s#/tmp/show2.fifo#$TMP/fifo#g" -e 's#sleep [0-9]*#true#')"
	[ -n "$BLOCK" ] || { fail "$PLAT: install/update block not found"; continue; }
	run() { # card
		SDCARD_PATH="$1" SYSTEM_PATH="$1/.system" UPDATE_PATH="$1/MinUI.zip" \
			TOOLS="$TOOLS" sh -c "$BLOCK" >/dev/null 2>&1 || true
	}

	# corrupt zip over a working system: system untouched, zip set aside
	C="$TMP/$PLAT-bad"; card "$C" sys; cp "$TMP/bad.zip" "$C/MinUI.zip"
	run "$C"
	[ "$(cat "$C/.system/paks/MinUI.pak/launch.sh" 2>/dev/null)" = old ] || fail "$PLAT: corrupt zip wiped the system"
	[ ! -e "$C/MinUI.zip" ] && [ -f "$C/MinUI.zip.failed" ] || fail "$PLAT: corrupt zip not set aside"

	# good zip over a working system: replaced, zip consumed, install.sh ran
	C="$TMP/$PLAT-good"; card "$C" sys; cp "$TMP/good.zip" "$C/MinUI.zip"
	run "$C"
	[ "$(cat "$C/.system/paks/MinUI.pak/launch.sh" 2>/dev/null)" = new ] || fail "$PLAT: good zip not installed"
	[ ! -e "$C/MinUI.zip" ] || fail "$PLAT: good zip not consumed"
	[ -f "$C/install-ran" ] || fail "$PLAT: install.sh not run"

	# corrupt zip on a fresh card: nothing to keep, zip kept for the retry
	C="$TMP/$PLAT-fresh"; card "$C"; cp "$TMP/bad.zip" "$C/MinUI.zip"
	run "$C"
	[ -f "$C/MinUI.zip" ] || fail "$PLAT: fresh-card corrupt zip not kept"
done
[ "$FAIL" = 0 ] && echo "PASS: boot-update-zip-check" || exit 1
