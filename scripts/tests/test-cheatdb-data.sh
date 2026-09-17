#!/usr/bin/env bash
# Host unit test for cheatdb_data.c. No SDL: uses host `unzip` as the extractor
# (NX_EXTRAS_UNZIP=unzip) and a wget shim. Sandboxed under a tmpdir.
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/workspace/all/cheatdb"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

# fixture cheats.zip: two mapped systems + one unmapped
FIX="$TMP/fix"; mkdir -p "$FIX/Nintendo - Game Boy Advance" "$FIX/Sega - Mega Drive - Genesis" "$FIX/PuzzleScript"
printf 'cheats = 0\n' > "$FIX/Nintendo - Game Boy Advance/Advance Wars (USA, Europe) (Code Breaker).cht"
printf 'cheats = 0\n' > "$FIX/Sega - Mega Drive - Genesis/Sonic The Hedgehog (USA, Europe).cht"
printf 'cheats = 0\n' > "$FIX/PuzzleScript/PuzzleScript.cht"
(cd "$FIX" && zip -qr "$TMP/cheats.zip" .)

SD="$TMP/sd"; mkdir -p "$SD/Cheats" "$SD/.userdata/shared/xtras"
BIN="$TMP/bin"; mkdir -p "$BIN"
cat > "$BIN/wget" <<SHIM
#!/usr/bin/env bash
for a in "\$@"; do [ "\$a" = "--spider" ] && { echo "  Last-Modified: \${LASTMOD:-Mon, 14 Sep 2026 18:05:15 GMT}" >&2; exit 0; }; done
exit 0
SHIM
chmod +x "$BIN/wget"

cc -std=gnu99 -Wall -Wextra -I"$SRC" "$SRC/cheatdb_data.c" "$SRC/tests/test_cheatdb_data.c" -o "$TMP/t"
PATH="$BIN:$PATH" SDCARD_PATH="$SD" CHEATS_PATH="$SD/Cheats" \
  SHARED_USERDATA_PATH="$SD/.userdata/shared" NX_EXTRAS_UNZIP=unzip \
  CHEATDB_TEST_ZIP="$TMP/cheats.zip" "$TMP/t"
