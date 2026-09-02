#!/bin/sh
# Rate-limit-free update check (github.com redirect, same trick as the
# device updater in settings_updater.c). Usage: check-update.sh <current-tag>
# stdout: "<latest-tag>\t<asset-url>". exit 0=update, 1=current, 2=error.
set -eu
CURRENT="${1:?usage: check-update.sh <current-tag>}"
BASE="${NXREDUX_UPDATE_BASE:-https://github.com/mohammadsyuhada/nx-redux}" # mirror settings_updater.c constants
# Refuse a plaintext base: an http endpoint lets an on-path attacker forge
# the Location redirect (and thus the tag/asset-url this script prints,
# which the C caller executes an update from) with no TLS to stop them.
# NXREDUX_ALLOW_INSECURE_UPDATE=1 is a deliberate debug escape hatch for
# tests that run their own local http dummy server -- never set in a real
# deployment.
case "$BASE" in
	https://*) : ;;
	*) [ "${NXREDUX_ALLOW_INSECURE_UPDATE:-}" = "1" ] || exit 2 ;;
esac
LOC="$(curl -sI --max-time 10 "$BASE/releases/latest" | tr -d '\r' \
	| awk 'tolower($1)=="location:"{print $2}' | tail -1)" || exit 2
TAG="${LOC##*/releases/tag/}"
[ -n "$TAG" ] && [ "$TAG" != "$LOC" ] || exit 2
[ "$TAG" != "$CURRENT" ] || exit 1
case "$(uname -s)" in
	Darwin) ASSET="NXRedux-$TAG-macos-arm64.zip" ;;
	*)      ASSET="NXRedux-$TAG-$(uname -m).AppImage" ;;
esac
printf '%s\t%s/releases/download/%s/%s\n' "$TAG" "$BASE" "$TAG" "$ASSET"
