#!/bin/sh
# Desktop OTA install backend. Usage: self-update.sh <asset-url>
# macOS: swap $NXREDUX_BUNDLE with the unpacked release zip.
# Linux:  overwrite $APPIMAGE with the downloaded file.
# Any failure exits non-zero with the current install untouched.
set -eu
URL="${1:?usage: self-update.sh <asset-url>}"

fail() { echo "self-update: $1" >&2; exit 1; }

if [ "$(uname -s)" = "Darwin" ]; then
	BUNDLE="${NXREDUX_BUNDLE:?not launched from the app bundle}"
	case "$BUNDLE" in
		*/AppTranslocation/*) fail "app is translocated; move NXRedux to Applications first" ;;
	esac
	[ -w "$(dirname "$BUNDLE")" ] || fail "install location not writable"
	TMP="$(mktemp -d "$(dirname "$BUNDLE")/.nxredux-update.XXXXXX")" # same volume => atomic mv
	trap 'rm -rf "$TMP"' EXIT
	curl -fL --max-time 600 -o "$TMP/update.zip" "$URL" || fail "download failed"
	ditto -x -k "$TMP/update.zip" "$TMP/unpacked" || fail "archive corrupt"
	[ -x "$TMP/unpacked/NXRedux.app/Contents/MacOS/NXRedux" ] || fail "archive missing app"
	mv "$BUNDLE" "$TMP/previous.app"
	if ! mv "$TMP/unpacked/NXRedux.app" "$BUNDLE"; then
		mv "$TMP/previous.app" "$BUNDLE"; fail "swap failed; restored previous"
	fi
	(sleep 1; open -n "$BUNDLE") &
else
	TARGET="${APPIMAGE:?not running from an AppImage}"
	[ -w "$(dirname "$TARGET")" ] || fail "install location not writable"
	curl -fL --max-time 600 -o "$TARGET.part" "$URL" || { rm -f "$TARGET.part"; fail "download failed"; }
	head -c2 "$TARGET.part" | grep -q '^#!' || file "$TARGET.part" | grep -qi elf \
		|| { rm -f "$TARGET.part"; fail "downloaded file is not an AppImage"; }
	chmod +x "$TARGET.part"
	mv -f "$TARGET.part" "$TARGET"
	(sleep 1; "$TARGET") &
fi
exit 0
