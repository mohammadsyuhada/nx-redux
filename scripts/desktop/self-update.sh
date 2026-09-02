#!/bin/sh
# Desktop OTA install backend. Usage: self-update.sh <asset-url-or-file>
# The argument may be an https URL or an already-downloaded local file (the
# in-app updater downloads with its own progress UI and passes the file).
# macOS: swap $NXREDUX_BUNDLE with the unpacked release zip.
# Linux:  overwrite $APPIMAGE with the downloaded file.
# Any failure exits non-zero with the current install untouched.
set -eu
URL="${1:?usage: self-update.sh <asset-url-or-file>}"

fail() { echo "self-update: $1" >&2; exit 1; }

if [ "$(uname -s)" = "Darwin" ]; then
	BUNDLE="${NXREDUX_BUNDLE:?not launched from the app bundle}"
	case "$BUNDLE" in
		*/AppTranslocation/*) fail "app is translocated; move NXRedux to Applications first" ;;
	esac
	[ -w "$(dirname "$BUNDLE")" ] || fail "install location not writable"
	# Named so a kill between the two renames below leaves a self-explanatory,
	# visible folder next to the app instead of a dotfile: if that happens,
	# $TMP/previous.app is the intact prior install and can be dragged back
	# over $BUNDLE by hand (see the plan's accepted no-rollback-daemon residual).
	TMP="$(mktemp -d "$(dirname "$BUNDLE")/NXRedux.update-in-progress.XXXXXX")" # same volume => atomic mv
	trap 'rm -rf "$TMP"' EXIT
	if [ -f "$URL" ]; then
		cp "$URL" "$TMP/update.zip" || fail "could not read local file"
	else
		curl -fL --max-time 600 -o "$TMP/update.zip" "$URL" || fail "download failed"
	fi
	ditto -x -k "$TMP/update.zip" "$TMP/unpacked" || fail "archive corrupt"
	[ -x "$TMP/unpacked/NXRedux.app/Contents/MacOS/NXRedux" ] || fail "archive missing app"
	mv "$BUNDLE" "$TMP/previous.app" || fail "could not move current app aside"
	if ! mv "$TMP/unpacked/NXRedux.app" "$BUNDLE"; then
		mv "$TMP/previous.app" "$BUNDLE"; fail "swap failed; restored previous"
	fi
	(sleep 1; open -n "$BUNDLE") &
else
	TARGET="${APPIMAGE:?not running from an AppImage}"
	[ -w "$(dirname "$TARGET")" ] || fail "install location not writable"
	if [ -f "$URL" ]; then
		cp "$URL" "$TARGET.part" || fail "could not read local file"
	else
		curl -fL --max-time 600 -o "$TARGET.part" "$URL" || { rm -f "$TARGET.part"; fail "download failed"; }
	fi
	head -c2 "$TARGET.part" | grep -q '^#!' || file "$TARGET.part" | grep -qi elf \
		|| { rm -f "$TARGET.part"; fail "downloaded file is not an AppImage"; }
	chmod +x "$TARGET.part" || { rm -f "$TARGET.part"; fail "chmod failed"; }
	mv -f "$TARGET.part" "$TARGET" || { rm -f "$TARGET.part"; fail "swap failed"; }
	(sleep 1; "$TARGET") &
fi
exit 0
