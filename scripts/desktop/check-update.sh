#!/bin/sh
# Rate-limit-free update check (github.com redirect, same trick as the
# device updater in settings_updater.c). Usage: check-update.sh <current-tag>
# stdout: "<latest-tag>\t<asset-url>". exit 0=update, 1=current, 2=error.
set -eu
CURRENT="${1:?usage: check-update.sh <current-tag>}"
BASE="${NXREDUX_UPDATE_BASE:-https://github.com/mohammadsyuhada/nx-redux}" # mirror settings_updater.c constants
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
