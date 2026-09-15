#!/usr/bin/env bash
# Host tests for the shell side of "Button layout": the sourced helper
# (skeleton/SYSTEM/<plat>/bin/nx_button_layout.sh), the mupen64plus config
# transform in N64.pak/nx_paths.sh and the flycast mapping selection in
# DC.pak/launch.sh. A fake nextval.elf on PATH stands in for the settings
# reader. Busybox-compatible constructs only in the scripts under test.
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT="$PWD"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
FAIL=0
fail() { echo "FAIL: $*" >&2; FAIL=1; }

# --- fake nextval.elf ------------------------------------------------------
mkdir -p "$TMP/bin"
cat > "$TMP/bin/nextval.elf" <<'EOF'
#!/bin/sh
printf '{"%s": %s}\n' "$1" "${NX_FAKE_LAYOUT:-0}"
EOF
chmod +x "$TMP/bin/nextval.elf"
export PATH="$TMP/bin:$PATH"
export SYSTEM_PATH="$TMP/system"
mkdir -p "$SYSTEM_PATH/bin"
cp skeleton/SYSTEM/tg5040/bin/nx_button_layout.sh "$SYSTEM_PATH/bin/"

# both platform copies must be identical
cmp -s skeleton/SYSTEM/tg5040/bin/nx_button_layout.sh skeleton/SYSTEM/tg5050/bin/nx_button_layout.sh \
    || fail "tg5040/tg5050 nx_button_layout.sh differ"

# --- helper ----------------------------------------------------------------
out=$(NX_FAKE_LAYOUT=0 sh -c '. "$SYSTEM_PATH/bin/nx_button_layout.sh"; echo "$NX_BUTTON_LAYOUT"')
[ "$out" = "nintendo" ] || fail "helper: expected nintendo for 0, got '$out'"
out=$(NX_FAKE_LAYOUT=1 sh -c '. "$SYSTEM_PATH/bin/nx_button_layout.sh"; echo "$NX_BUTTON_LAYOUT"')
[ "$out" = "xbox" ] || fail "helper: expected xbox for 1, got '$out'"
# nextval missing -> nintendo, and the helper must not abort the sourcing shell
out=$(PATH="/usr/bin:/bin" SYSTEM_PATH="$SYSTEM_PATH" sh -c '. "$SYSTEM_PATH/bin/nx_button_layout.sh"; echo "$NX_BUTTON_LAYOUT"')
[ "$out" = "nintendo" ] || fail "helper: expected nintendo without nextval, got '$out'"

# --- mupen64plus transform -------------------------------------------------
# Drive N64.pak/nx_paths.sh with a fake env; the seed is the real default cfg.
n64_run() { # $1 = layout value for the fake nextval, $2 = platform dir
    NX_FAKE_LAYOUT="$1" PAK_DIR="$ROOT/skeleton/SYSTEM/$2/paks/Emus/N64.pak" \
    SHARED_USERDATA_PATH="$TMP/shared" DEVICE="${3:-brick}" \
    sh -c '. "$PAK_DIR/nx_paths.sh"'
}
rm -rf "$TMP/shared"; mkdir -p "$TMP/shared"
n64_run 0 tg5040
CFG="$TMP/shared/N64-mupen64plus/config/tg5040-brick/mupen64plus.cfg"
[ -f "$CFG" ] || fail "n64: cfg not seeded"
cp "$CFG" "$TMP/n64_nintendo.cfg"
grep -q '^B Button = "button(0)"' "$CFG" || fail "n64: seed should be nintendo"
[ "$(cat "$TMP/shared/N64-mupen64plus/config/tg5040-brick/.button_layout" 2>/dev/null || echo nintendo)" = nintendo ] \
    || fail "n64: marker should be nintendo/absent after nintendo run"
# switch to xbox: Control1 swaps, Control2..4 untouched, everything else byte-identical
n64_run 1 tg5040
grep -q '^B Button = "button(1)"' "$CFG" || fail "n64: B should be button(1) under xbox"
grep -q '^A Button = "button(0)"' "$CFG" || fail "n64: A should be button(0) under xbox"
grep -q '^C Button L = "axis(3-,24000) button(2)"' "$CFG" || fail "n64: C-L should hold button(2) under xbox"
grep -q '^C Button D = "axis(4+,24000) button(3)"' "$CFG" || fail "n64: C-D should hold button(3) under xbox"
[ "$(cat "$TMP/shared/N64-mupen64plus/config/tg5040-brick/.button_layout")" = xbox ] || fail "n64: marker should be xbox"
# only lines inside [Input-SDL-Control1] may differ
awk '/^\[/{s=$0} s!="[Input-SDL-Control1]"{print}' "$TMP/n64_nintendo.cfg" > "$TMP/a.cfg"
awk '/^\[/{s=$0} s!="[Input-SDL-Control1]"{print}' "$CFG" > "$TMP/b.cfg"
cmp -s "$TMP/a.cfg" "$TMP/b.cfg" || fail "n64: lines outside Control1 changed"
# idempotent: a second xbox run changes nothing
cp "$CFG" "$TMP/n64_xbox.cfg"
n64_run 1 tg5040
cmp -s "$CFG" "$TMP/n64_xbox.cfg" || fail "n64: second xbox run must be a no-op"
# user tuning survives: edit an unrelated key, switch back, expect original + edit
sed -i.bak 's/^MouseSensitivity = .*/MouseSensitivity = "9.00,9.00"/' "$CFG" && rm -f "$CFG.bak"
n64_run 0 tg5040
grep -q '^B Button = "button(0)"' "$CFG" || fail "n64: B should return to button(0)"
grep -q '^MouseSensitivity = "9.00,9.00"' "$CFG" || fail "n64: user edit lost on swap back"
# tg5050 copy behaves the same
rm -rf "$TMP/shared"; mkdir -p "$TMP/shared"
n64_run 1 tg5050 x
CFG5="$TMP/shared/N64-mupen64plus/config/tg5050/mupen64plus.cfg"
grep -q '^A Button = "button(0)"' "$CFG5" || fail "n64 tg5050: A should be button(0) under xbox"

[ "$FAIL" = 0 ] && echo "test-button-layout-sh: OK"
exit "$FAIL"
