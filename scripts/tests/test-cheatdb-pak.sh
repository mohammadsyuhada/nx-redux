#!/usr/bin/env bash
# Host test for the Cheat Database pak (data side). Shims options.elf/show2.elf/
# wget; uses host `unzip` as the extractor. Drives the menu via $PICK.
# shellcheck disable=SC2015,SC2016
set -u
FAILS=0
say(){ printf '%s\n' "$*"; }; pass(){ say "PASS: $*"; }; fail(){ say "FAIL: $*"; FAILS=$((FAILS+1)); }
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PAK="$ROOT/skeleton/SYSTEM/tg5050/paks/Tools/Xtras.pak/catalog/cheatdb/pak/launch.sh"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

# fixture cheats.zip: two mapped systems + one unmapped
FIX="$TMP/fix"; mkdir -p "$FIX/Nintendo - Game Boy Advance" "$FIX/Sega - Mega Drive - Genesis" "$FIX/PuzzleScript"
printf 'cheats = 0\n' > "$FIX/Nintendo - Game Boy Advance/Advance Wars (USA, Europe) (Code Breaker).cht"
printf 'cheats = 0\n' > "$FIX/Sega - Mega Drive - Genesis/Sonic The Hedgehog (USA, Europe).cht"
printf 'cheats = 0\n' > "$FIX/PuzzleScript/PuzzleScript.cht"
(cd "$FIX" && zip -qr "$TMP/cheats.zip" .)

SD="$TMP/sd"; mkdir -p "$SD/.userdata/shared/xtras" "$SD/.userdata/tg5050" "$SD/Cheats/GBA" "$SD/.system/shared/bin"
echo 'user cheat' > "$SD/Cheats/GBA/MyHomebrew.cht"

BIN="$TMP/bin"; mkdir -p "$BIN"
# wget shim: --spider prints Last-Modified from $LASTMOD; -O copies the fixture zip.
cat > "$BIN/wget" <<SHIM
#!/usr/bin/env bash
spider=0; out=""
while [ \$# -gt 0 ]; do case "\$1" in --spider) spider=1;; -O) out="\$2"; shift;; esac; shift; done
if [ "\$spider" = 1 ]; then echo "  Last-Modified: \${LASTMOD:-Mon, 14 Sep 2026 18:05:15 GMT}" >&2; exit 0; fi
[ -n "\$out" ] && cp "$TMP/cheats.zip" "\$out"
SHIM
# options.elf shim: prints \$PICK once, then empty (so the loop ends).
cat > "$BIN/options.elf" <<'SHIM'
#!/usr/bin/env bash
if [ -f "$PICKFILE" ] && [ -s "$PICKFILE" ]; then head -1 "$PICKFILE"; sed -i.bak '1d' "$PICKFILE" && rm -f "$PICKFILE.bak"; fi
SHIM
cat > "$BIN/show2.elf" <<'SHIM'
#!/usr/bin/env bash
exit 0
SHIM
# 7zzs shim -> use host unzip via NX_EXTRAS_UNZIP=unzip inside the pak (the pak
# must honor NX_EXTRAS_UNZIP override). If the pak hardcodes 7zzs, provide it.
chmod +x "$BIN/wget" "$BIN/options.elf" "$BIN/show2.elf"

run_pak() { # $1=pick sequence (newline-separated)
  printf '%s\n' "$1" > "$TMP/picks"
  PATH="$BIN:$PATH" \
  PLATFORM=tg5050 SDCARD_PATH="$SD" CHEATS_PATH="$SD/Cheats" \
  SHARED_USERDATA_PATH="$SD/.userdata/shared" LOGS_PATH="$SD/.userdata/tg5050" \
  NX_EXTRAS_UNZIP=unzip PICKFILE="$TMP/picks" \
  bash "$PAK"
}
GBA="$SD/Cheats/GBA"; MD="$SD/Cheats/MD"
MAN="$SD/.userdata/shared/xtras/cheatdb.manifest"; DBV="$SD/.userdata/shared/xtras/cheatdb.db_lastmod"

# 1. Download populates data + manifest + db_lastmod, keeps hand-made, skips unmapped
run_pak "download" > "$TMP/l1.txt" 2>&1
[ -f "$GBA/Advance Wars (USA, Europe) (Code Breaker).cht" ] && pass "GBA cheat extracted" || fail "GBA cheat missing"
[ -f "$MD/Sonic The Hedgehog (USA, Europe).cht" ] && pass "MD cheat extracted" || fail "MD cheat missing"
[ ! -e "$SD/Cheats/PuzzleScript" ] && pass "unmapped system skipped" || fail "unmapped written"
[ -f "$GBA/MyHomebrew.cht" ] && pass "hand-made kept" || fail "hand-made lost"
grep -q "Advance Wars" "$MAN" && pass "manifest records pack file" || fail "manifest missing pack file"
! grep -q "MyHomebrew" "$MAN" && pass "manifest excludes hand-made" || fail "manifest lists hand-made"
[ -s "$DBV" ] && pass "db_lastmod written" || fail "db_lastmod missing"
[ ! -f "$SD/.userdata/shared/xtras/cheatdb.version" ] && pass "pak does not write the pak-code marker" || fail "pak wrote cheatdb.version"

# 2. Check-for-updates with SAME Last-Modified -> no re-extract (data unchanged)
before="$(cat "$MAN" | wc -l)"
run_pak "update" > "$TMP/l2.txt" 2>&1
after="$(cat "$MAN" | wc -l)"
[ "$before" = "$after" ] && pass "same Last-Modified: no change" || fail "re-extracted despite same version"

# 3. Check-for-updates with NEWER Last-Modified -> re-extract
LASTMOD="Wed, 17 Sep 2026 10:00:00 GMT" run_pak "update" > "$TMP/l3.txt" 2>&1
[ "$(cat "$DBV")" = "Wed, 17 Sep 2026 10:00:00 GMT" ] && pass "newer Last-Modified updated db_lastmod" || fail "db_lastmod not updated"

# 4. Remove clears data + records, keeps hand-made
run_pak "remove" > "$TMP/l4.txt" 2>&1
[ ! -e "$GBA/Advance Wars (USA, Europe) (Code Breaker).cht" ] && pass "remove cleared pack data" || fail "pack data left"
[ -f "$GBA/MyHomebrew.cht" ] && pass "remove kept hand-made" || fail "hand-made lost on remove"
[ ! -e "$MAN" ] && [ ! -e "$DBV" ] && pass "remove cleared records" || fail "records left"

say ""; if [ "$FAILS" -eq 0 ]; then say "ALL PASS"; exit 0; else say "$FAILS FAILURE(S)"; exit 1; fi
