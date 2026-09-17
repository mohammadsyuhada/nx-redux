#!/bin/sh
# Xtras catalog: Cheat Database uninstall. Full removal: the pak code, the
# downloaded cheat data (manifest-based, keeping hand-authored .cht), and the
# data-version marker. extras.elf removes the $XTRAS_STATE_DIR/cheatdb.version
# pak-code marker itself. Self-contained; no network.
set -u

: "${XTRAS_STATE_DIR:=$SDCARD_PATH/.userdata/shared/xtras}"
MANIFEST="$XTRAS_STATE_DIR/cheatdb.manifest"
TOOLS_PAK="$SDCARD_PATH/Tools/Cheat Database.pak"
USERDATA_DIR="$SDCARD_PATH/.userdata/$PLATFORM"

echo "@10 Removing cheat data..."
if [ -f "$MANIFEST" ]; then
    dirs=""
    while IFS= read -r p; do
        [ -n "$p" ] || continue
        rm -f "$p"
        d="$(dirname "$p")"
        case "$dirs" in *"|$d|"*) : ;; *) dirs="$dirs|$d|" ;; esac
    done < "$MANIFEST"
    OLDIFS="$IFS"; IFS='|'
    for d in $dirs; do [ -n "$d" ] && [ -d "$d" ] && rmdir "$d" 2>/dev/null; done
    IFS="$OLDIFS"
fi

echo "@70 Removing records..."
rm -f "$MANIFEST" "$XTRAS_STATE_DIR/cheatdb.db_lastmod"

echo "@85 Removing the tool..."
rm -rf "$TOOLS_PAK"
rm -f "$USERDATA_DIR/emulist_cache.txt" "$USERDATA_DIR/romindex_cache.txt"

echo "@100 Done"
echo "Removed. Hand-made cheats kept."
exit 0
