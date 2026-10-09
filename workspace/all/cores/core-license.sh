#!/bin/sh
# core-license.sh <checkout> <name> <repo> [license path] [notice]: prints the
# license that ships beside a core, headed by where it came from (repo + the
# commit actually checked out). The license path is relative to the checkout;
# without one (or given as "") the usual root names are tried. Fails when none
# is found, so a core can't ship without its license. The optional notice is a
# file of ours appended after the license (e.g. code we compile into the core).
set -e
SRC="$1"
NAME="$2"
REPO="$3"
LIC="$4"
NOTICE="$5"
if [ -z "$LIC" ]; then
	for f in LICENSE LICENSE.md LICENSE.MD LICENSE.txt LICENSE.TXT License.txt license.txt COPYING Copying COPYING.txt; do
		if [ -f "$SRC/$f" ]; then
			LIC="$f"
			break
		fi
	done
fi
if [ -z "$LIC" ] || [ ! -f "$SRC/$LIC" ]; then
	echo "error: no license file for $NAME in $SRC; set ${NAME}_LICENSE in the cores makefile" >&2
	exit 1
fi
COMMIT=$(git -C "$SRC" rev-parse HEAD 2>/dev/null || echo unknown)
printf '%s\nSource: %s\nCommit: %s\nLicense file: %s\n\n' "$NAME" "$REPO" "$COMMIT" "$LIC"
cat "$SRC/$LIC"
if [ -n "$NOTICE" ]; then
	cat "$NOTICE"
fi
