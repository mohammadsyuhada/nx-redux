#!/usr/bin/env bash
# Host-side test for the cheatdb (libretro cheat database) Xtras installer.
# Network and the archive extractor are shimmed via PATH; all paths sandboxed.
# shellcheck disable=SC2015,SC2016
set -u

FAILS=0
say()  { printf '%s\n' "$*"; }
pass() { say "PASS: $*"; }
fail() { say "FAIL: $*"; FAILS=$((FAILS+1)); }

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ENTRY="$ROOT/skeleton/SYSTEM/tg5050/paks/Tools/Xtras.pak/catalog/cheatdb"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# ---- fixture cheats.zip: two mapped systems + one unmapped system --------
FIX="$TMP/fix"
mkdir -p "$FIX/Nintendo - Game Boy Advance" \
         "$FIX/Sega - Mega Drive - Genesis" \
         "$FIX/Nintendo - Nintendo DS" \
         "$FIX/PuzzleScript"
printf 'cheats = 0\n' > "$FIX/Nintendo - Game Boy Advance/Advance Wars (USA, Europe) (Code Breaker).cht"
printf 'cheats = 0\n' > "$FIX/Nintendo - Game Boy Advance/Pokemon - Ruby Version (USA).cht"
printf 'cheats = 0\n' > "$FIX/Sega - Mega Drive - Genesis/Sonic The Hedgehog (USA, Europe).cht"
printf 'cheats = 0\n' > "$FIX/Nintendo - Nintendo DS/Some DS Game (USA).cht"
printf 'cheats = 0\n' > "$FIX/PuzzleScript/PuzzleScript.cht"   # unmapped -> skipped
(cd "$FIX" && zip -qr "$TMP/cheats.zip" .)

# ---- sandbox SD card -----------------------------------------------------
SD="$TMP/sd"
mkdir -p "$SD/.userdata/tg5050/logs" "$SD/Cheats/GBA"
# a hand-authored cheat that must survive install + uninstall
echo 'user cheat' > "$SD/Cheats/GBA/MyHomebrew.cht"

# ---- PATH shims ----------------------------------------------------------
BIN="$TMP/bin"; mkdir -p "$BIN"
cat > "$BIN/wget" <<SHIM
#!/usr/bin/env bash
out=""; url=""; spider=0
while [ \$# -gt 0 ]; do
  case "\$1" in
    -O) out="\$2"; shift ;;
    --spider) spider=1 ;;
    -S) ;;
    -*) ;;
    *) url="\$1" ;;
  esac; shift
done
if [ "\$spider" = "1" ]; then
  echo "  Last-Modified: Mon, 14 Sep 2026 18:05:15 GMT" >&2
  exit 0
fi
case "\$url" in
  *cheats.zip) cp "$TMP/cheats.zip" "\$out" ;;
  *) echo "wget shim: unknown url \$url" >&2; exit 1 ;;
esac
SHIM
chmod +x "$BIN/wget"

run_install() {
  PATH="$BIN:$PATH" \
  PLATFORM=tg5050 \
  SDCARD_PATH="$SD" \
  LOGS_PATH="$SD/.userdata/tg5050/logs" \
  CATALOG_DIR="$ENTRY" \
  XTRAS_STATE_DIR="$SD/.userdata/shared/xtras" \
  NX_EXTRAS_UNZIP="${NX_EXTRAS_UNZIP:-unzip}" \
  bash "$ENTRY/install.sh"
}
run_uninstall() {
  PATH="$BIN:$PATH" \
  PLATFORM=tg5050 \
  SDCARD_PATH="$SD" \
  LOGS_PATH="$SD/.userdata/tg5050/logs" \
  CATALOG_DIR="$ENTRY" \
  XTRAS_STATE_DIR="$SD/.userdata/shared/xtras" \
  bash "$ENTRY/uninstall.sh"
}

GBA="$SD/Cheats/GBA"
MD="$SD/Cheats/MD"
NDS="$SD/Cheats/NDS"
MAN="$SD/.userdata/shared/xtras/cheatdb.manifest"
VER="$SD/.userdata/shared/xtras/cheatdb.version"

# ---- 1. fresh install ----------------------------------------------------
if run_install > "$TMP/log1.txt" 2>&1; then pass "install exits 0"
else fail "install exited non-zero: $(tail -3 "$TMP/log1.txt")"; fi
[ -f "$GBA/Advance Wars (USA, Europe) (Code Breaker).cht" ] \
  && pass "GBA cheat extracted flat" || fail "GBA cheat missing"
[ -f "$MD/Sonic The Hedgehog (USA, Europe).cht" ] \
  && pass "MD cheat extracted flat" || fail "MD cheat missing"
[ -f "$NDS/Some DS Game (USA).cht" ] \
  && pass "standalone NDS cheat delivered too" || fail "NDS cheat missing"
[ ! -e "$SD/Cheats/PuzzleScript" ] && [ ! -d "$SD/Cheats/PUZZLE" ] \
  && pass "unmapped system skipped" || fail "unmapped system was written"
[ -f "$GBA/MyHomebrew.cht" ] && [ "$(cat "$GBA/MyHomebrew.cht")" = "user cheat" ] \
  && pass "hand-made cheat preserved on install" || fail "hand-made cheat clobbered"
grep -q "$GBA/Advance Wars" "$MAN" \
  && pass "manifest records pack files" || fail "manifest missing pack file"
! grep -q "MyHomebrew" "$MAN" \
  && pass "manifest excludes hand-made file" || fail "manifest wrongly lists hand-made file"
[ -s "$VER" ] && pass "version record written" || fail "version record missing"
grep -q '@100 ' "$TMP/log1.txt" \
  && pass "install emits @NN progress hints" || fail "no @NN progress hint"

# ---- 2. reinstall is idempotent -----------------------------------------
if run_install > "$TMP/log2.txt" 2>&1; then pass "reinstall exits 0"
else fail "reinstall exited non-zero: $(tail -3 "$TMP/log2.txt")"; fi
[ "$(grep -c "$GBA/Advance Wars" "$MAN")" = "1" ] \
  && pass "reinstall does not duplicate manifest lines" || fail "manifest duplicated on reinstall"
[ -f "$GBA/MyHomebrew.cht" ] \
  && pass "reinstall still preserves hand-made cheat" || fail "reinstall clobbered hand-made cheat"

say ""
if [ "$FAILS" -eq 0 ]; then say "ALL PASS"; exit 0; else say "$FAILS FAILURE(S)"; exit 1; fi
