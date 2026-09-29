#!/bin/sh
# apply-patches.sh <checkout> <patch dir>: applies <patch dir>/*.patch (sorted)
# to a core checkout once. The marker <checkout>/.patched-all records a checksum
# of the patch set, so a patch added or changed after the checkout was patched
# fails loudly instead of being silently skipped. An empty marker comes from
# before the checksum and is adopted as-is.
set -e
SRC="$1"
DIR="$2"
MARK="$SRC/.patched-all"
PATCHES=""
[ -d "$DIR" ] && PATCHES=$(ls "$DIR"/*.patch 2>/dev/null | sort) || true
WANT=$(cat /dev/null $PATCHES | cksum | cut -d' ' -f1-2)
if [ -f "$MARK" ]; then
	HAVE=$(cat "$MARK")
	if [ -z "$HAVE" ] || [ "$HAVE" = "$WANT" ]; then
		echo "$WANT" > "$MARK"
		exit 0
	fi
	echo "error: the patches in $DIR changed after $SRC was patched; rm -rf $SRC and rebuild" >&2
	exit 1
fi
ABS=$(cd "${DIR:-.}" 2>/dev/null && pwd || true)
for P in $PATCHES; do
	(cd "$SRC" && git apply -p1 < "$ABS/$(basename "$P")")
done
echo "$WANT" > "$MARK"
