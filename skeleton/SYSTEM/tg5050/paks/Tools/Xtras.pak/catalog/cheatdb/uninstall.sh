#!/bin/sh
# Xtras catalog: cheat database uninstall.
# Contract: same env as install.sh, streamed the same way; "@NN status text"
# progress; idempotent; needs no network. Removes ONLY the files this pack
# wrote (listed in the manifest), then any now-empty Cheats/<TAG> dir it had
# created, then the manifest and version marker. Hand-authored .cht files
# (never in the manifest) are preserved, and a Cheats/<TAG> dir that still
# holds a hand-made file is kept.
set -u

: "${XTRAS_STATE_DIR:=$SDCARD_PATH/.userdata/shared/xtras}"
MANIFEST="$XTRAS_STATE_DIR/cheatdb.manifest"

echo "@10 Removing cheat files..."
if [ -f "$MANIFEST" ]; then
    # Remove each pack file, and collect the parent dirs to prune afterwards.
    dirs=""
    while IFS= read -r p; do
        [ -n "$p" ] || continue
        rm -f "$p"
        d="$(dirname "$p")"
        case "$dirs" in
            *"|$d|"*) : ;;
            *) dirs="$dirs|$d|" ;;
        esac
    done < "$MANIFEST"

    echo "@70 Removing empty folders..."
    # rmdir only succeeds on an empty dir, so hand-made files keep theirs.
    OLDIFS="$IFS"; IFS='|'
    for d in $dirs; do
        [ -n "$d" ] && [ -d "$d" ] && rmdir "$d" 2>/dev/null
    done
    IFS="$OLDIFS"
fi

echo "@90 Removing records..."
rm -f "$MANIFEST" "$XTRAS_STATE_DIR/cheatdb.version"

echo "@100 Done"
echo "Done. Hand-made cheats kept."
exit 0
